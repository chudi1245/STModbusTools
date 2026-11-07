#pragma once

// ============================================================================
// ModbusClient.h — 基于 Qt SerialBus 的封装 (串口 RTU + Modbus TCP 双传输)
//
// 协议 (RTU 帧/CRC/超时/重试/TCP) 完全由 Qt 组件实现, 本类只做管理:
//   * 读写分离双队列: 写队列优先 (用户交互不被轮询积压阻塞)
//   * 轮询读可丢弃 (只保留最新), 写请求同地址合并为最新值
//   * 回调契约: 任何排队/在途请求的回调保证被调用一次 —
//     被丢弃/被合并/断连清理时以 ok=false 通知, 绝不静默销毁
//   * 每个请求带事务号, 迟到的旧事务应答直接忽略 (防断连后错配回调)
//   * 连续错误熔断: 连续 5 次传输失败暂停调度并报警; 协议异常帧
//     (如寄存器越界) 不计熔断 — 链路健康, 属档案/窗口配置问题
// ============================================================================

#include "CommTypes.h"
#include "LuaPorts.h"

#include <QElapsedTimer>
#include <QModbusClient>
#include <QSerialPort>

class ModbusClient : public QObject, public ILuaRegIo
{
    Q_OBJECT
public:
    enum class Transport
    {
        SerialRtu,
        Tcp
    };

    using ReadCb  = ILuaRegIo::ReadCb;
    using WriteCb = ILuaRegIo::WriteCb;

    explicit ModbusClient(QObject *parent = nullptr);
    ~ModbusClient() override;

    void setTransport(Transport t);
    Transport transport() const { return m_transport; }

    void setSerialParams(const QString &portName, qint32 baud,
                         QSerialPort::Parity parity, int slaveAddr);
    void setTcpParams(const QString &host, quint16 port, int slaveAddr);
    // 部分设备 (如 ST100) 仅支持 FC10 多寄存器写, 不支持 FC06 单写:
    // 置 true 后所有写 (含单寄存器) 均以 FC10 原始请求发送 (档案 P.write_multi)
    void setForceMultiWrite(bool on) { m_forceMultiWrite = on; }
    bool forceMultiWrite() const { return m_forceMultiWrite; }
    // 当前串口参数回读 (流模式接管串口时镜像给 StreamClient)
    QString serialPortName() const { return m_serialPort; }
    qint32 serialBaud() const { return m_baud; }
    QSerialPort::Parity serialParity() const { return m_parity; }

    bool isConnected() const override; // ILuaRegIo
    int  slaveAddr() const { return m_slave; }
    QString lastError() const { return m_lastErr; }

    bool connectPort();
    void disconnectPort();

    // 读保持寄存器 (03): 轮询性质, 队列满时丢弃最旧读请求 (其回调收 ok=false)
    void readRegs(quint16 addr, int count, ReadCb cb) override;
    // 写单寄存器 (06): 便捷入口, 内部转 writeRegs
    void writeReg(quint16 addr, quint16 value, WriteCb cb = nullptr);
    // 写多寄存器 (10): 同起始地址的写合并为最新值
    // (被合并的旧回调立即收 ok=false, 表示已被更新值取代)
    void writeRegs(quint16 addr, const QVector<quint16> &values, WriteCb cb = nullptr) override;
    // 插队写 (标定命令等关键操作): 清掉同地址排队写 (其回调收 ok=false)
    void writeRegFront(quint16 addr, quint16 value, WriteCb cb = nullptr) override;

    // 读队列中是否已有未完成的读 (轮询引擎用, 防积压)
    bool hasPendingRead() const override // ILuaRegIo (轮询引擎防积压)
    {
        return !m_reads.isEmpty() || m_busyIsRead;
    }

    // 熔断恢复
    void resume();

signals:
    void connectionChanged(bool connected);
    void logMessage(const QString &msg);
    void commStatistics(int okCount, int errCount);
    void linkAlarmed();
    void protocolError(const QString &msg);
    // 通信体检数据源: 每个完成事务恰一次 (含失败/被丢弃路径);
    // 帧存 PDU + 从站地址 + TCP 事务 ID 镜像, 展示端补全为线上完整帧 (CommTypes.h)
    void transactionDone(const TxnRecord &rec);
    void frameLogged(const FrameRecord &fr);

private:
    struct ReadReq
    {
        quint16 addr = 0;
        int count = 0;
        quint64 txn = 0;
        ReadCb cb;
    };
    struct WriteReq
    {
        quint16 addr = 0;
        QVector<quint16> values;
        quint64 txn = 0;
        WriteCb cb;
    };

    void createClient();
    void scheduleNext();
    void sendRead(const ReadReq &r);
    void sendWrite(const WriteReq &w);
    void onReplyFinished();
    void failAllPending(const QString &reason); // 断连/熔断: 所有排队+在途回调以失败通知
    // 通信体检发射 (覆盖全部收口路径): 事务恰一次; 帧仅对上线字节
    void finishTxn(bool isWrite, quint64 txn, quint16 addr, int count,
                   TxnErr err, const QString &note, double elapsedMs);
    void emitFrame(bool isTx, bool isErr, quint8 slaveAddr, quint16 mbapTid,
                   const QByteArray &pdu, const QString &note, double elapsedMs);
    QByteArray buildTxPdu(bool isRead, quint16 addr, int count,
                          const QVector<quint16> &values) const;

    QModbusClient *m_mb = nullptr;
    Transport m_transport = Transport::SerialRtu;

    // 连接参数 (两种传输各自使用)
    QString m_serialPort;
    qint32 m_baud = 115200;
    QSerialPort::Parity m_parity = QSerialPort::NoParity;
    QString m_host;
    quint16 m_tcpPort = 502;

    QList<ReadReq> m_reads;
    QList<WriteReq> m_writes;

    bool m_busyIsRead = false;
    bool m_busyIsWrite = false;
    ReadReq m_curRead;
    WriteReq m_curWrite;
    quint64 m_curReadTxn = 0;
    quint64 m_curWriteTxn = 0;
    quint64 m_txnSeq = 0;
    QElapsedTimer m_txnClock; // 当前在途事务计时 (发送时重启, 断连清理时读走)
    bool m_fused = false;
    int m_consecErr = 0;

    int m_slave = 1;
    int m_okCount = 0;
    int m_errCount = 0;
    QString m_lastErr;
    bool m_forceMultiWrite = false;

    // TCP 事务 ID 镜像 (与 Qt 内部计数同规则: 每 0 起, 每次成功发出 +1,
    // 超时重发不增; 重建底层客户端时归零) — 仅用于帧日志 MBAP 头展示
    quint16 m_tidMirror = 0;
    quint16 m_curTid = 0; // 当前在途事务的 TID (严格串行, RX 帧展示复用)

    static constexpr int kReadQueueMax = 4;
    static constexpr int kFuseThreshold = 5;
};
