// ============================================================================
// MasterClient.cpp — 多从站手动事务实现
//   Qt SerialBus 对 QModbusDataUnit 读写自动选功能码:
//     读 01/02/03/04 按 unit type; 写单点 05/06, 多点 0F/10 — 无需自拼帧
// ============================================================================

#include "MasterClient.h"

#include "ModbusTransport.h"

#include <QDateTime>
#include <QDataStream>
#include <QModbusReply>
#include <QModbusRequest>

MasterClient::MasterClient(QObject *parent)
    : QObject(parent)
{
    createInner();
}

MasterClient::~MasterClient()
{
    if (m_mb)
        m_mb->disconnectDevice();
}

void MasterClient::createInner()
{
    if (m_mb)
    {
        m_mb->disconnectDevice();
        m_mb->deleteLater();
        m_mb = nullptr;
    }
    m_mb = ModbusTransport::createClient(m_kind == Kind::Tcp ? ModbusTransport::Kind::Tcp
                                                             : ModbusTransport::Kind::SerialRtu,
                                         this);
    m_mb->setTimeout(m_timeoutMs);
    m_mb->setNumberOfRetries(m_retries);
    m_tidMirror = 0; // Qt 底层客户端重建, 内部事务计数归零, 镜像同步
    if (m_kind == Kind::Tcp)
        ModbusTransport::applyTcpParams(m_mb, m_host, m_tcpPort);
    else
        ModbusTransport::applySerialParams(m_mb, m_serialPort, m_baud, m_parity, m_stop);

    connect(m_mb, &QModbusClient::stateChanged, this, [this](QModbusDevice::State st) {
        if (st == QModbusDevice::ConnectedState || st == QModbusDevice::UnconnectedState
            || st == QModbusDevice::ClosingState)
            emit connectionChanged(st == QModbusDevice::ConnectedState);
    });
    connect(m_mb, &QModbusDevice::errorOccurred, this, [this](QModbusDevice::Error) {
        m_lastErr = m_mb->errorString();
    });
}

void MasterClient::setKind(Kind k)
{
    if (m_kind == k)
        return;
    const bool wasConnected = isConnected();
    m_kind = k;
    createInner();
    if (wasConnected)
        emit connectionChanged(false);
}

void MasterClient::setSerialParams(const QString &portName, qint32 baud,
                                   QSerialPort::Parity parity, QSerialPort::StopBits stopBits)
{
    m_serialPort = portName;
    m_baud = baud;
    m_parity = parity;
    m_stop = stopBits;
    if (m_kind == Kind::SerialRtu)
        ModbusTransport::applySerialParams(m_mb, m_serialPort, m_baud, m_parity, m_stop);
}

void MasterClient::setTcpParams(const QString &host, quint16 port)
{
    m_host = host;
    m_tcpPort = port;
    if (m_kind == Kind::Tcp)
        ModbusTransport::applyTcpParams(m_mb, m_host, m_tcpPort);
}

void MasterClient::setTimeoutMs(int ms)
{
    m_timeoutMs = ms;
    m_mb->setTimeout(ms);
}

void MasterClient::setRetries(int n)
{
    m_retries = n;
    m_mb->setNumberOfRetries(n);
}

bool MasterClient::isConnected() const
{
    return m_mb && m_mb->state() == QModbusDevice::ConnectedState;
}

bool MasterClient::connectDevice()
{
    if (isConnected())
        return true;
    if (!m_mb->connectDevice())
    {
        m_lastErr = m_mb->errorString();
        return false;
    }
    return true;
}

void MasterClient::disconnectDevice()
{
    m_mb->disconnectDevice();
}

// ---- TX PDU 重建 (与 Qt 按 unit 自动选码的规则一致) ----
QByteArray MasterClient::unitPdu(const QModbusDataUnit &unit, bool isWrite) const
{
    QByteArray pdu;
    QDataStream ds(&pdu, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);
    const int n = int(unit.valueCount());
    const bool bitArea = unit.registerType() == QModbusDataUnit::Coils
                         || unit.registerType() == QModbusDataUnit::DiscreteInputs;
    if (!isWrite)
    {
        // 读: FC01/02/03/04 = [fc][addr][qty]
        quint8 fc = 0x03;
        switch (unit.registerType())
        {
        case QModbusDataUnit::Coils: fc = 0x01; break;
        case QModbusDataUnit::DiscreteInputs: fc = 0x02; break;
        case QModbusDataUnit::InputRegisters: fc = 0x04; break;
        default: fc = 0x03; break;
        }
        ds << fc << quint16(unit.startAddress()) << quint16(n);
        return pdu;
    }
    if (n == 1)
    {
        // 单点写: FC05 线圈 ON=FF00 / OFF=0000, FC06 寄存器原值
        const quint8 fc = bitArea ? quint8(0x05) : quint8(0x06);
        const quint16 val = bitArea ? (unit.value(0) != 0 ? quint16(0xFF00) : quint16(0x0000))
                                    : quint16(unit.value(0));
        ds << fc << quint16(unit.startAddress()) << val;
        return pdu;
    }
    if (bitArea)
    {
        // FC0F: 线圈按位打包, 首字节 bit0 = 第一个线圈
        QByteArray bits((n + 7) / 8, char(0));
        for (int i = 0; i < n; ++i)
            if (unit.value(i) != 0)
                bits[i / 8] = char(quint8(bits[i / 8]) | quint8(1 << (i % 8)));
        ds << quint8(0x0F) << quint16(unit.startAddress()) << quint16(n)
           << quint8(bits.size());
        pdu.append(bits);
        return pdu;
    }
    // FC10: [10][addr][qty][byteCount][values]
    ds << quint8(0x10) << quint16(unit.startAddress()) << quint16(n) << quint8(n * 2);
    for (int i = 0; i < n; ++i)
        ds << quint16(unit.value(i));
    return pdu;
}

bool MasterClient::sendChecked(QModbusReply *reply, const Ctx &ctx, const QByteArray &txPdu)
{
    if (!reply || reply->isFinished())
    {
        // 未连接/请求被拒: 不产生事务记录, 只回报失败 (调用方已检查连接, 属防御)
        // Qt 未入队取号: 帧展示用当前镜像值 (将被取的号), 不自增
        m_lastErr = reply ? tr("请求被拒绝") : m_mb->errorString();
        if (reply)
            reply->deleteLater(); // 调用方拥有已完成的 reply, 必须释放
        emitFrame(true, true, ctx.slave, m_tidMirror, txPdu, m_lastErr, 0);
        const QVector<quint16> empty;
        if (ctx.kind == 0)
            emit readDone(ctx.fc, ctx.slave, ctx.addr, false, empty, m_lastErr, 0);
        else if (ctx.kind == 1)
            emit writeDone(ctx.slave, ctx.addr, ctx.qty, false, m_lastErr, 0);
        else
            emit rawDone(ctx.slave, QByteArray(), false, m_lastErr, 0);
        return false;
    }
    // Qt TCP 底层入队即取号 (首帧 0, 此后每请求 +1, 超时重发不变) — 镜像同步
    Ctx c = ctx;
    c.tid = (m_kind == Kind::Tcp) ? m_tidMirror++ : quint16(0);
    emitFrame(true, false, c.slave, c.tid, txPdu, QString(), 0);
    c.clock.start();
    connect(reply, &QModbusReply::finished, this,
            [this, c, reply] { handleFinish(c, reply); });
    return true;
}

bool MasterClient::read(int fc, int slave, quint16 addr, quint16 qty)
{
    if (!isConnected() || qty == 0)
        return false;
    QModbusDataUnit::RegisterType type = QModbusDataUnit::HoldingRegisters;
    switch (fc)
    {
    case 1: type = QModbusDataUnit::Coils; break;
    case 2: type = QModbusDataUnit::DiscreteInputs; break;
    case 3: type = QModbusDataUnit::HoldingRegisters; break;
    case 4: type = QModbusDataUnit::InputRegisters; break;
    default: return false;
    }
    QModbusDataUnit unit(type, addr, qty);
    Ctx ctx;
    ctx.kind = 0;
    ctx.fc = fc;
    ctx.slave = slave;
    ctx.addr = addr;
    ctx.qty = qty;
    return sendChecked(m_mb->sendReadRequest(unit, slave), ctx, unitPdu(unit, false));
}

bool MasterClient::writeCoils(int slave, quint16 addr, const QVector<quint8> &bits)
{
    if (!isConnected() || bits.isEmpty())
        return false;
    QModbusDataUnit unit(QModbusDataUnit::Coils, addr, bits.size());
    for (int i = 0; i < bits.size(); ++i)
        unit.setValue(i, bits[i] ? 1 : 0);
    Ctx ctx;
    ctx.kind = 1;
    ctx.fc = bits.size() == 1 ? 5 : 15;
    ctx.slave = slave;
    ctx.addr = addr;
    ctx.qty = bits.size();
    return sendChecked(m_mb->sendWriteRequest(unit, slave), ctx, unitPdu(unit, true));
}

bool MasterClient::writeRegs(int slave, quint16 addr, const QVector<quint16> &values,
                             bool forceMulti)
{
    if (!isConnected() || values.isEmpty())
        return false;
    if (forceMulti && values.size() == 1)
    {
        // 单寄存器 FC10 原始请求: [10][addr][qty=1][bc=2][val]
        // (Qt 对单寄存器数据单元自动选 FC06, 只实现 03/10 的设备会拒绝)
        QByteArray pdu;
        QDataStream ds(&pdu, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::BigEndian);
        ds << addr << quint16(1) << quint8(2) << values.front();
        Ctx ctx;
        ctx.kind = 1;
        ctx.fc = 16;
        ctx.slave = slave;
        ctx.addr = addr;
        ctx.qty = 1;
        const QModbusRequest req(QModbusRequest::WriteMultipleRegisters, pdu);
        return sendChecked(m_mb->sendRawRequest(req, slave), ctx, pdu);
    }
    QModbusDataUnit unit(QModbusDataUnit::HoldingRegisters, addr, values.size());
    for (int i = 0; i < values.size(); ++i)
        unit.setValue(i, values[i]);
    Ctx ctx;
    ctx.kind = 1;
    ctx.fc = values.size() == 1 ? 6 : 16;
    ctx.slave = slave;
    ctx.addr = addr;
    ctx.qty = values.size();
    return sendChecked(m_mb->sendWriteRequest(unit, slave), ctx, unitPdu(unit, true));
}

bool MasterClient::sendRaw(int slave, const QByteArray &pdu)
{
    if (!isConnected() || pdu.isEmpty())
        return false;
    const QModbusRequest req(QModbusRequest::FunctionCode(quint8(pdu.at(0))), pdu.mid(1));
    Ctx ctx;
    ctx.kind = 2;
    ctx.fc = quint8(pdu.at(0));
    ctx.slave = slave;
    return sendChecked(m_mb->sendRawRequest(req, slave), ctx, pdu);
}

void MasterClient::handleFinish(const Ctx &ctx, QModbusReply *reply)
{
    reply->deleteLater();
    const double elapsedMs = ctx.clock.isValid() ? double(ctx.clock.elapsed()) : 0.0;
    const QModbusResponse resp = reply->rawResult();
    // 应答 PDU 原文 (功能码+数据); 无应答字节 (超时/断连) 时为空
    const QByteArray rxPdu =
        resp.isValid() ? QByteArray(commPduBytes(int(resp.functionCode()), resp.data()))
                       : QByteArray();

    if (reply->error() == QModbusDevice::NoError)
    {
        emitFrame(false, false, reply->serverAddress(), ctx.tid, rxPdu, QString(), elapsedMs);
        finishTxn(ctx, TxnErr::Ok, QString(), elapsedMs);
        if (ctx.kind == 0)
        {
            const QModbusDataUnit unit = reply->result();
            QVector<quint16> regs(int(unit.valueCount()));
            for (int i = 0; i < int(unit.valueCount()); ++i)
                regs[i] = unit.value(i);
            emit readDone(ctx.fc, ctx.slave, ctx.addr, true, regs, QString(), elapsedMs);
        }
        else if (ctx.kind == 1)
        {
            emit writeDone(ctx.slave, ctx.addr, ctx.qty, true, QString(), elapsedMs);
        }
        else
        {
            emit rawDone(ctx.slave, rxPdu, true, QString(), elapsedMs);
        }
        return;
    }

    const bool proto = reply->error() == QModbusDevice::ProtocolError;
    // 协议异常帧应答原文可见 (功能码|0x80) — 调试工具必须展示
    emitFrame(false, true, reply->serverAddress(), ctx.tid, proto ? rxPdu : QByteArray(),
              reply->errorString(), elapsedMs);
    const TxnErr cls = proto ? TxnErr::Protocol
                             : (reply->error() == QModbusDevice::TimeoutError ? TxnErr::Timeout
                                                                              : TxnErr::Connection);
    finishTxn(ctx, cls, reply->errorString(), elapsedMs);
    m_lastErr = reply->errorString();
    const QVector<quint16> empty;
    if (ctx.kind == 0)
        emit readDone(ctx.fc, ctx.slave, ctx.addr, false, empty, reply->errorString(), elapsedMs);
    else if (ctx.kind == 1)
        emit writeDone(ctx.slave, ctx.addr, ctx.qty, false, reply->errorString(), elapsedMs);
    else
        emit rawDone(ctx.slave, proto ? rxPdu : QByteArray(), false, reply->errorString(),
                     elapsedMs);
}

void MasterClient::finishTxn(const Ctx &ctx, TxnErr err, const QString &note, double elapsedMs)
{
    TxnRecord rec;
    rec.txn = ++m_txnSeq;
    rec.isWrite = ctx.kind == 1;
    rec.addr = ctx.addr;
    rec.count = ctx.qty;
    rec.tMs = QDateTime::currentMSecsSinceEpoch();
    rec.elapsedMs = elapsedMs;
    rec.err = static_cast<int>(err);
    rec.note = note;
    emit txnDone(rec);
}

void MasterClient::emitFrame(bool isTx, bool isErr, int slave, quint16 mbapTid,
                             const QByteArray &pdu, const QString &note, double elapsedMs)
{
    FrameRecord fr;
    fr.tMs = QDateTime::currentMSecsSinceEpoch();
    fr.isTx = isTx;
    fr.isErr = isErr;
    fr.addr = quint8(slave);
    fr.isRtu = (m_kind == Kind::SerialRtu);
    fr.mbapTid = mbapTid;
    fr.pdu = pdu;
    fr.elapsedMs = elapsedMs;
    fr.note = note;
    emit frameLogged(fr);
}
