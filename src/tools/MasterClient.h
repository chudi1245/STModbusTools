#pragma once

// ============================================================================
// MasterClient.h — Modbus 调试工具专用主站传输 (多从站手动事务)
//   与主界面 ModbusClient (单从站 + 读写队列 + 熔断) 的机制差异:
//     * 从站地址随每个请求携带 — 多个轮询定义可指向不同从站
//     * 无队列策略: 在途并发由调用方限速 (DataViewWidget 在途守卫跳 tick);
//       调试场景要求持续观察错误而非停表, 故无熔断
//     * 事务无回调参数, 结果经信号广播 (同一应答可能喂页面/日志/统计多处)
//   帧/事务记录口径与 ModbusClient 一致 (CommTypes.h): 帧存 PDU + 从站地址
//   (+ TCP 事务 ID 镜像), TX PDU 本地重建 (Qt 不透传编帧, 但编帧规则确定),
//   RX 取自 Qt 解码结果, 帧日志按传输模式补全为线上完整帧展示
//   (RTU: 地址+CRC; TCP: MBAP 事务ID+协议ID+长度+单元ID)
// ============================================================================

#include "CommTypes.h"

#include <QElapsedTimer>
#include <QModbusClient>
#include <QObject>
#include <QSerialPort>
#include <QVector>

class QModbusReply;

class MasterClient : public QObject
{
    Q_OBJECT
public:
    enum class Kind
    {
        SerialRtu,
        Tcp
    };

    explicit MasterClient(QObject *parent = nullptr);
    ~MasterClient() override;

    // 切换传输并重建底层客户端 (断开当前连接, connectionChanged(false) 必发)
    void setKind(Kind k);
    Kind kind() const { return m_kind; }

    // 连接参数 (连接前后均可改; RTU 停止位 1/2)
    void setSerialParams(const QString &portName, qint32 baud,
                         QSerialPort::Parity parity, QSerialPort::StopBits stopBits);
    void setTcpParams(const QString &host, quint16 port);
    void setTimeoutMs(int ms); // Response Timeout (Modbus Poll F3 同名项)
    void setRetries(int n);
    int timeoutMs() const { return m_timeoutMs; } // 扫描等临时改参方恢复用
    int retries() const { return m_retries; }

    // 当前连接参数读回 (串口参数扫描等临时改参方结束后恢复原值用)
    QString serialPortName() const { return m_serialPort; }
    qint32 baud() const { return m_baud; }
    QSerialPort::Parity parity() const { return m_parity; }
    QSerialPort::StopBits stopBits() const { return m_stop; }
    QString host() const { return m_host; }
    quint16 tcpPort() const { return m_tcpPort; }

    bool isConnected() const;
    bool connectDevice(); // false = 打开失败 (lastError() 有因)
    void disconnectDevice();
    QString lastError() const { return m_lastErr; }

    // ---- 事务入口 (返回 false = 未上线: 未连接/参数拒绝, 不产生事务记录) ----
    // 读 FC01/02/03/04; 线圈区 values 元素为 0/1
    bool read(int fc, int slave, quint16 addr, quint16 qty);
    // 写线圈: 单点 FC05, 多点 FC0F (bits 元素非 0 = ON)
    bool writeCoils(int slave, quint16 addr, const QVector<quint8> &bits);
    // 写寄存器: 单点 FC06, 多点 FC10; forceMulti=true 时单寄存器也走 FC10
    // (仅实现 03/10 的设备, 如协议档案 P.write_multi 变送器, FC06 会被拒)
    bool writeRegs(int slave, quint16 addr, const QVector<quint16> &values,
                   bool forceMulti = false);
    // Test Center 原始 PDU (fc+data 字节) 直发, 应答原文经 rawDone
    bool sendRaw(int slave, const QByteArray &pdu);

signals:
    void connectionChanged(bool connected);
    // 迟到/失配应答也必发 (ok=false), 由接收方自行比对地址丢弃
    void readDone(int fc, int slave, quint16 addr, bool ok,
                  const QVector<quint16> &values, const QString &errText,
                  double elapsedMs);
    void writeDone(int slave, quint16 addr, int count, bool ok,
                   const QString &errText, double elapsedMs);
    void rawDone(int slave, const QByteArray &respPdu, bool ok,
                 const QString &errText, double elapsedMs);
    void frameLogged(const FrameRecord &fr); // FrameLogDialog 兼容口径
    void txnDone(const TxnRecord &rec);      // CommStatsCollector 数据源

private:
    struct Ctx
    {
        int kind = 0; // 0=read 1=write 2=raw
        int fc = 0;
        int slave = 0;
        quint16 addr = 0;
        quint16 qty = 0;
        quint16 tid = 0; // TCP 事务 ID (RX 帧展示复用请求号)
        QElapsedTimer clock;
    };
    void createInner();
    bool sendChecked(QModbusReply *reply, const Ctx &ctx, const QByteArray &txPdu);
    void handleFinish(const Ctx &ctx, QModbusReply *reply);
    void finishTxn(const Ctx &ctx, TxnErr err, const QString &note, double elapsedMs);
    void emitFrame(bool isTx, bool isErr, int slave, quint16 mbapTid,
                   const QByteArray &pdu, const QString &note, double elapsedMs);
    QByteArray unitPdu(const QModbusDataUnit &unit, bool isWrite) const; // TX PDU 重建

    QModbusClient *m_mb = nullptr;
    Kind m_kind = Kind::SerialRtu;

    QString m_serialPort;
    qint32 m_baud = 115200;
    QSerialPort::Parity m_parity = QSerialPort::NoParity;
    QSerialPort::StopBits m_stop = QSerialPort::OneStop;
    QString m_host;
    quint16 m_tcpPort = 502;
    int m_timeoutMs = 1000;
    int m_retries = 2;

    QString m_lastErr;
    quint64 m_txnSeq = 0;
    quint16 m_tidMirror = 0; // TCP 事务 ID 镜像 (与 Qt 内部计数同规则, 展示用)
};
