// ============================================================================
// ModbusClient.cpp — Qt SerialBus 封装: 串口 RTU / TCP 双传输 + 读写分离调度
// ============================================================================

#include "ModbusClient.h"

#include "ModbusTransport.h"

#include <QDateTime>
#include <QDataStream>
#include <QModbusReply>
#include <QModbusRequest>
#include <QVariant>

ModbusClient::ModbusClient(QObject *parent)
    : QObject(parent)
{
    createClient();
}

ModbusClient::~ModbusClient()
{
    if (m_mb)
        m_mb->disconnectDevice();
}

void ModbusClient::createClient()
{
    if (m_mb)
    {
        m_mb->disconnectDevice();
        m_mb->deleteLater();
        m_mb = nullptr;
    }
    m_mb = ModbusTransport::createClient(m_transport == Transport::Tcp
                                             ? ModbusTransport::Kind::Tcp
                                             : ModbusTransport::Kind::SerialRtu,
                                         this);
    m_mb->setTimeout(300);
    m_mb->setNumberOfRetries(2);
    m_tidMirror = 0; // Qt 底层客户端重建, 内部事务计数归零, 镜像同步
    m_curTid = 0;

    connect(m_mb, &QModbusClient::stateChanged, this, [this](QModbusDevice::State st) {
        const bool on = (st == QModbusDevice::ConnectedState);
        if (!on && (st == QModbusDevice::UnconnectedState || st == QModbusDevice::ClosingState))
            failAllPending(QStringLiteral("连接断开"));
        emit connectionChanged(on);
    });
    connect(m_mb, &QModbusDevice::errorOccurred, this, [this](QModbusDevice::Error) {
        m_lastErr = m_mb->errorString();
        emit logMessage(tr("Modbus 设备错误: %1").arg(m_lastErr));
    });
}

void ModbusClient::setTransport(Transport t)
{
    if (m_transport == t)
        return;
    const bool wasConnected = isConnected();
    m_transport = t;
    createClient();
    if (wasConnected)
        emit connectionChanged(false);
}

void ModbusClient::setSerialParams(const QString &portName, qint32 baud,
                                   QSerialPort::Parity parity, int slaveAddr)
{
    m_serialPort = portName;
    m_baud = baud;
    m_parity = parity;
    m_slave = slaveAddr;
    ModbusTransport::applySerialParams(m_mb, portName, baud, parity);
}

void ModbusClient::setTcpParams(const QString &host, quint16 port, int slaveAddr)
{
    m_host = host;
    m_tcpPort = port;
    m_slave = slaveAddr;
    ModbusTransport::applyTcpParams(m_mb, host, port);
}

bool ModbusClient::isConnected() const
{
    return m_mb && m_mb->state() == QModbusDevice::ConnectedState;
}

bool ModbusClient::connectPort()
{
    if (isConnected())
        return true;
    m_fused = false;
    m_consecErr = 0;
    failAllPending(QStringLiteral("重新连接前清理")); // 防止断连期间的滞留请求泄入新连接
    if (!m_mb->connectDevice())
    {
        m_lastErr = m_mb->errorString();
        emit logMessage(tr("连接失败: %1").arg(m_lastErr));
        emit connectionChanged(false);
        return false;
    }
    return true;
}

void ModbusClient::disconnectPort()
{
    m_mb->disconnectDevice();
}

void ModbusClient::resume()
{
    m_fused = false;
    m_consecErr = 0;
    emit logMessage(tr("通信恢复调度"));
    scheduleNext();
}

void ModbusClient::readRegs(quint16 addr, int count, ReadCb cb)
{
    if (m_reads.size() >= kReadQueueMax)
    {
        // 丢弃最旧读请求, 但其回调必须以失败收场 (契约: 回调恰好一次)
        ReadReq dropped = m_reads.takeFirst();
        emit logMessage(tr("读队列限深, 丢弃旧请求 (addr=%1)").arg(dropped.addr));
        finishTxn(false, dropped.txn, dropped.addr, dropped.count, TxnErr::Dropped,
                  tr("读队列限深"), 0);
        if (dropped.cb)
            dropped.cb(false, {});
    }
    ReadReq r;
    r.addr = addr;
    r.count = count;
    r.txn = ++m_txnSeq;
    r.cb = std::move(cb);
    m_reads.append(std::move(r));
    scheduleNext();
}

void ModbusClient::writeReg(quint16 addr, quint16 value, WriteCb cb)
{
    writeRegs(addr, {value}, std::move(cb));
}

void ModbusClient::writeRegs(quint16 addr, const QVector<quint16> &values, WriteCb cb)
{
    for (auto &w : m_writes)
    {
        if (w.addr == addr)
        {
            // 合并: 新值取代旧值, 旧回调立即以失败收场 (其请求已不会执行)
            finishTxn(true, w.txn, w.addr, w.values.size(), TxnErr::Dropped,
                      tr("同地址写合并"), 0);
            WriteCb superseded = std::move(w.cb);
            w.cb = nullptr;
            w.values = values;
            w.txn = ++m_txnSeq;
            w.cb = std::move(cb);
            if (superseded)
                superseded(false);
            return;
        }
    }
    WriteReq wr;
    wr.addr = addr;
    wr.values = values;
    wr.txn = ++m_txnSeq;
    wr.cb = std::move(cb);
    m_writes.append(std::move(wr));
    scheduleNext();
}

void ModbusClient::writeRegFront(quint16 addr, quint16 value, WriteCb cb)
{
    WriteReq wr;
    wr.addr = addr;
    wr.values = {value};
    wr.txn = ++m_txnSeq;
    wr.cb = std::move(cb);
    for (int i = m_writes.size() - 1; i >= 0; --i)
    {
        if (m_writes[i].addr == addr)
        {
            // 被插队写取代的排队写: 回调以失败收场
            finishTxn(true, m_writes[i].txn, addr, m_writes[i].values.size(),
                      TxnErr::Dropped, tr("被插队写取代"), 0);
            WriteCb superseded = std::move(m_writes[i].cb);
            m_writes[i].cb = nullptr;
            m_writes.removeAt(i);
            if (superseded)
                superseded(false);
        }
    }
    m_writes.prepend(std::move(wr));
    scheduleNext();
}

void ModbusClient::scheduleNext()
{
    if (!isConnected() || m_fused || m_busyIsRead || m_busyIsWrite)
        return;
    if (!m_writes.isEmpty())
        sendWrite(m_writes.takeFirst());
    else if (!m_reads.isEmpty())
        sendRead(m_reads.takeFirst());
}

void ModbusClient::sendRead(const ReadReq &r)
{
    QModbusDataUnit unit(QModbusDataUnit::HoldingRegisters, r.addr, r.count);
    QModbusReply *reply = m_mb->sendReadRequest(unit, m_slave);
    if (!reply || reply->isFinished())
    {
        emit logMessage(tr("读请求发送失败: %1").arg(m_mb->errorString()));
        emitFrame(false, true, quint8(m_slave), m_curTid, {}, m_mb->errorString(), 0);
        finishTxn(false, r.txn, r.addr, r.count, TxnErr::Connection, m_mb->errorString(), 0);
        if (reply)
            reply->deleteLater(); // 调用方拥有已完成的 reply, 必须释放
        if (r.cb)
            r.cb(false, {});
        scheduleNext();
        return;
    }
    m_busyIsRead = true;
    m_curRead = r;
    m_curReadTxn = r.txn;
    m_txnClock.start();
    // Qt TCP 底层入队即取号 (首帧 0, 此后每请求 +1, 超时重发不变) — 镜像同步
    m_curTid = (m_transport == Transport::Tcp) ? m_tidMirror++ : quint16(0);
    reply->setProperty("isRead", true);
    reply->setProperty("txn", QVariant::fromValue(r.txn));
    connect(reply, &QModbusReply::finished, this, &ModbusClient::onReplyFinished);
    emitFrame(true, false, quint8(m_slave), m_curTid,
              buildTxPdu(true, r.addr, r.count, {}), QString(), 0);
}

void ModbusClient::sendWrite(const WriteReq &w)
{
    QModbusReply *reply = nullptr;

    if (m_forceMultiWrite)
    {
        // FC10 原始请求: Qt SerialBus 对单寄存器数据单元自动选 FC06,
        // ST100 等设备只实现了 03/10 两种功能码 → 单写也必须走 10H
        QByteArray pdu;
        QDataStream ds(&pdu, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::BigEndian);
        ds << w.addr << quint16(w.values.size()) << quint8(w.values.size() * 2);
        for (quint16 v : w.values)
            ds << v;
        QModbusRequest req(QModbusRequest::WriteMultipleRegisters, pdu);
        reply = m_mb->sendRawRequest(req, m_slave);
    }
    else
    {
        QModbusDataUnit unit(QModbusDataUnit::HoldingRegisters, w.addr, w.values.size());
        for (int i = 0; i < w.values.size(); ++i)
            unit.setValue(i, w.values[i]);
        reply = m_mb->sendWriteRequest(unit, m_slave);
    }

    if (!reply || reply->isFinished())
    {
        emit logMessage(tr("写请求发送失败: %1").arg(m_mb->errorString()));
        emitFrame(false, true, quint8(m_slave), m_curTid, {}, m_mb->errorString(), 0);
        finishTxn(true, w.txn, w.addr, w.values.size(), TxnErr::Connection,
                  m_mb->errorString(), 0);
        if (reply)
            reply->deleteLater();
        if (w.cb)
            w.cb(false);
        scheduleNext();
        return;
    }
    m_busyIsWrite = true;
    m_curWrite = w;
    m_curWriteTxn = w.txn;
    m_txnClock.start();
    m_curTid = (m_transport == Transport::Tcp) ? m_tidMirror++ : quint16(0);
    reply->setProperty("isRead", false);
    reply->setProperty("txn", QVariant::fromValue(w.txn));
    connect(reply, &QModbusReply::finished, this, &ModbusClient::onReplyFinished);
    emitFrame(true, false, quint8(m_slave), m_curTid,
              buildTxPdu(false, w.addr, w.values.size(), w.values), QString(), 0);
}

void ModbusClient::onReplyFinished()
{
    auto *reply = qobject_cast<QModbusReply *>(sender());
    if (!reply)
        return;
    const bool isRead = reply->property("isRead").toBool();
    const quint64 txn = reply->property("txn").toULongLong();
    reply->deleteLater();

    // 迟到的旧事务应答 (断连清理/已被取代): 忽略, 不动当前事务的忙标志
    if (isRead ? (txn != m_curReadTxn) : (txn != m_curWriteTxn))
        return;

    const double elapsedMs = m_txnClock.isValid() ? double(m_txnClock.elapsed()) : 0.0;
    ReadCb readCb;
    WriteCb writeCb;
    quint16 addr = 0;
    int count = 0;
    if (isRead)
    {
        m_busyIsRead = false;
        readCb = std::move(m_curRead.cb);
        addr = m_curRead.addr;
        count = m_curRead.count;
        m_curReadTxn = 0;
    }
    else
    {
        m_busyIsWrite = false;
        writeCb = std::move(m_curWrite.cb);
        addr = m_curWrite.addr;
        count = m_curWrite.values.size();
        m_curWriteTxn = 0;
    }

    if (reply->error() == QModbusDevice::NoError)
    {
        const QModbusResponse resp = reply->rawResult();
        emitFrame(false, false, quint8(reply->serverAddress()), m_curTid,
                  commPduBytes(int(resp.functionCode()), resp.data()), QString(), elapsedMs);
        finishTxn(!isRead, txn, addr, count, TxnErr::Ok, QString(), elapsedMs);
        ++m_okCount;
        m_consecErr = 0;
        if (isRead)
        {
            const QModbusDataUnit unit = reply->result();
            QVector<quint16> regs(unit.valueCount());
            for (int i = 0; i < unit.valueCount(); ++i)
                regs[i] = unit.value(i);
            if (readCb)
                readCb(true, regs);
        }
        else if (writeCb)
        {
            writeCb(true);
        }
    }
    else if (reply->error() == QModbusDevice::ProtocolError)
    {
        // 协议异常帧 (非法地址/值等): 链路本身健康, 不计入传输熔断
        const QModbusResponse resp = reply->rawResult();
        emitFrame(false, true, quint8(reply->serverAddress()), m_curTid,
                  commPduBytes(int(resp.functionCode()), resp.data()), reply->errorString(),
                  elapsedMs);
        finishTxn(!isRead, txn, addr, count, TxnErr::Protocol, reply->errorString(), elapsedMs);
        ++m_errCount;
        m_consecErr = 0;
        m_lastErr = reply->errorString();
        emit logMessage(tr("协议异常 (addr=%1, code=0x%2): %3 — 请检查设备档案寄存器窗口")
                            .arg(addr)
                            .arg(static_cast<int>(reply->rawResult().exceptionCode()), 2, 16,
                                 QChar('0'))
                            .arg(m_lastErr));
        emit protocolError(tr("设备返回协议异常 (addr=%1): %2").arg(addr).arg(m_lastErr));
        if (isRead && readCb)
            readCb(false, {});
        if (!isRead && writeCb)
            writeCb(false);
        emit commStatistics(m_okCount, m_errCount);
        scheduleNext();
        return;
    }
    else
    {
        const TxnErr cls = (reply->error() == QModbusDevice::TimeoutError) ? TxnErr::Timeout
                                                                          : TxnErr::Connection;
        emitFrame(false, true, quint8(reply->serverAddress()), m_curTid, {},
                  reply->errorString(), elapsedMs);
        finishTxn(!isRead, txn, addr, count, cls, reply->errorString(), elapsedMs);
        ++m_errCount;
        ++m_consecErr;
        m_lastErr = reply->errorString();
        emit logMessage(tr("事务失败 (addr=%1): %2").arg(addr).arg(m_lastErr));
        if (isRead && readCb)
            readCb(false, {});
        if (!isRead && writeCb)
            writeCb(false);

        if (m_consecErr >= kFuseThreshold && !m_fused)
        {
            m_fused = true;
            failAllPending(QStringLiteral("通信熔断"));
            emit linkAlarmed();
            emit logMessage(tr("连续 %1 次通信失败, 调度已熔断").arg(kFuseThreshold));
            emit commStatistics(m_okCount, m_errCount);
            return;
        }
    }
    emit commStatistics(m_okCount, m_errCount);
    scheduleNext();
}

void ModbusClient::failAllPending(const QString &reason)
{
    // 在途事务 (有真实耗时), 排队事务 (未上线, 耗时 0)
    const double inflightMs = m_txnClock.isValid() ? double(m_txnClock.elapsed()) : 0.0;
    m_txnClock.invalidate();
    ReadReq rr = m_curRead;
    WriteReq wr = m_curWrite;
    m_curRead = ReadReq{};
    m_curWrite = WriteReq{};
    m_curReadTxn = m_curWriteTxn = 0;
    m_busyIsRead = m_busyIsWrite = false;
    // 排队事务
    const QList<ReadReq> reads = std::move(m_reads);
    const QList<WriteReq> writes = std::move(m_writes);
    m_reads.clear();
    m_writes.clear();
    if (!reads.isEmpty() || !writes.isEmpty() || rr.cb || wr.cb)
        emit logMessage(tr("清理未完成请求 (%1): 读 %2 项, 写 %3 项")
                            .arg(reason)
                            .arg(reads.size() + (rr.cb ? 1 : 0))
                            .arg(writes.size() + (wr.cb ? 1 : 0)));
    // 先通知在途, 再按序通知排队
    if (rr.cb)
    {
        finishTxn(false, rr.txn, rr.addr, rr.count, TxnErr::Dropped, reason, inflightMs);
        rr.cb(false, {});
    }
    if (wr.cb)
    {
        finishTxn(true, wr.txn, wr.addr, wr.values.size(), TxnErr::Dropped, reason, inflightMs);
        wr.cb(false);
    }
    for (const auto &r : reads)
    {
        finishTxn(false, r.txn, r.addr, r.count, TxnErr::Dropped, reason, 0);
        if (r.cb)
            r.cb(false, {});
    }
    for (const auto &w : writes)
    {
        finishTxn(true, w.txn, w.addr, w.values.size(), TxnErr::Dropped, reason, 0);
        if (w.cb)
            w.cb(false);
    }
}

// ============================================================================
// 通信体检发射: 事务记录 + 帧记录 (PDU + 从站地址; RTU 展示端补全 ADU 的 CRC)
//   TX PDU 本地重建 (Qt 不透传已编帧请求, 但编帧规则确定):
//   读 = FC03 [addr][count]; 写 = 单值 FC06 [addr][val] / 多值 FC10
//   [addr][qty][byteCount][vals] — 与 forceMultiWrite 的 raw 请求一致
// ============================================================================
QByteArray ModbusClient::buildTxPdu(bool isRead, quint16 addr, int count,
                                    const QVector<quint16> &values) const
{
    QByteArray pdu;
    QDataStream ds(&pdu, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);
    if (isRead)
    {
        ds << quint8(0x03) << addr << quint16(count);
        return pdu;
    }
    if (values.size() == 1)
    {
        ds << quint8(0x06) << addr << values.front();
        return pdu;
    }
    ds << quint8(0x10) << addr << quint16(values.size())
       << quint8(values.size() * 2);
    for (quint16 v : values)
        ds << v;
    return pdu;
}

void ModbusClient::finishTxn(bool isWrite, quint64 txn, quint16 addr, int count,
                             TxnErr err, const QString &note, double elapsedMs)
{
    TxnRecord rec;
    rec.txn = txn;
    rec.isWrite = isWrite;
    rec.addr = addr;
    rec.count = count;
    rec.tMs = QDateTime::currentMSecsSinceEpoch();
    rec.elapsedMs = elapsedMs;
    rec.err = static_cast<int>(err);
    rec.note = note;
    emit transactionDone(rec);
}

void ModbusClient::emitFrame(bool isTx, bool isErr, quint8 slaveAddr, quint16 mbapTid,
                             const QByteArray &pdu, const QString &note, double elapsedMs)
{
    FrameRecord fr;
    fr.tMs = QDateTime::currentMSecsSinceEpoch();
    fr.isTx = isTx;
    fr.isErr = isErr;
    fr.addr = slaveAddr;
    fr.isRtu = (m_transport == Transport::SerialRtu);
    fr.mbapTid = mbapTid;
    fr.pdu = pdu;
    fr.elapsedMs = elapsedMs;
    fr.note = note;
    emit frameLogged(fr);
}
