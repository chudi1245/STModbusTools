#pragma once

// ============================================================================
// ScanDialog.h — 扫描: 从站 / 地址 / 串口参数 / TCP 发现
//   从站扫描: 逐从站 FC03 读 1 寄存器 (0 重试), 应答即存活
//     (协议异常也算存活 — 设备在, 只是该寄存器无效; 仅超时/连接错判不存在)
//   地址扫描: 指定从站+FC 逐地址探测, 结果表可导出 CSV
//   串口参数扫描: 组合 波特率×校验×停止位 (数据位固定 8, RTU 标准) 逐组合
//     重开串口, 每组合扫一遍从站段 — 找到即得"该怎么连"; 探测顺序按常见度
//     优先 (9600/19200/115200 靠前), 命中即停时显著缩短扫描时间
//   TCP 发现: 先并行 QTcpSocket 探端口 (默认 502), 开放主机再逐单元 ID
//     FC03 探测 — 得"连哪台机器、哪个单元"; 双击结果回填连接参数
//   无应答超时为全模式共用 (默认 100 ms, 低波特率建议加大), 0 重试 —
//     超时判无应答即跳下一个; 发送被拒/连接瞬断也跳过当前点继续,
//     不整场中止 (曾表现为"扫第一个就停止")
//   从站/地址扫描要求已连接; 参数扫描/发现会重定向共享 MasterClient
//     (主窗收到 scanBusyChanged 自动暂停轮询, 结束恢复原参数并重连);
//   全部单在途逐发 (收到一答再发下一个), [停止] 随时中断
// ============================================================================

#include "MasterClient.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QHash>
#include <QTimer>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTcpSocket;

class ScanDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ScanDialog(MasterClient *mb, QWidget *parent = nullptr);
    ~ScanDialog() override;

    // 活动连接切换时主窗重指向 (可空 = 各页禁用起扫)。
    // 扫描进行中拒绝切换 (共享连接正被重定向, 半路换目标是未定义行为)
    void setClient(MasterClient *mb);
    bool isBusy() const { return m_mode != Mode::None; } // 主窗据此拦移除连接/切换活动

signals:
    void slavePicked(int slave); // [用作从站] → 主窗应用到当前定义
    // 参数扫描/TCP 发现双击结果 → 主窗回填连接参数 + 当前定义从站 (并重连)
    void serialPicked(int baud, int parity, int stopBits, int slave);
    void tcpPicked(const QString &host, quint16 port, int slave);
    // 参数扫描/TCP 发现期间共享连接被重定向 → 主窗暂停/恢复轮询
    void scanBusyChanged(bool busy);

private slots:
    void startSlaveScan();
    void startAddrScan();
    void startSerialScan();
    void startTcpScan();
    void stopScan();
    void onReadDone(int fc, int slave, quint16 addr, bool ok,
                    const QVector<quint16> &values, const QString &errText,
                    double elapsedMs);
    void onConnChanged(bool connected);
    void exportAddrCsv();

private:
    enum class Mode
    {
        None,
        Slave,
        Addr,
        SerialParam, // 串口参数扫描 (重定向共享连接)
        TcpUnit      // TCP 端口 + 单元发现 (重定向共享连接)
    };
    struct Combo
    {
        int baud = 0;
        QSerialPort::Parity parity = QSerialPort::NoParity;
        QSerialPort::StopBits stop = QSerialPort::OneStop;
    };

    void buildUi();
    void sendNext();      // 从站/地址扫描: 当前游标的下一个探测
    void finishScan(const QString &summary);
    void resetButtons(bool running);
    bool probeIsAlive(bool ok, const QString &errText) const;
    bool beginScanCommon(Mode m); // 公共起手: 存参/短超时/按钮态, 扫描中拒绝
    void saveConnState();
    void restoreConnection();     // 恢复原连接参数, 原连接态则重连

    // 串口参数扫描状态机
    void beginCombo(int idx);          // 断开 → 设参 → 重开 → 探从站段
    void probeNextComboSlave();
    // TCP 发现状态机
    void launchPortProbes();           // 并行探端口, 空批时转入单元探测
    void settleProbe(QTcpSocket *s, bool open);
    void abortPortProbes();
    void beginTcpHost(int idx);
    void probeNextUnit();

    MasterClient *m_mb = nullptr;
    Mode m_mode = Mode::None;
    int m_cursor = 0;        // 从站/地址扫描游标
    int m_end = 0;           // 终止值 (含)
    int m_alive = 0;
    int m_done = 0;
    int m_savedTimeoutMs = 0;
    int m_savedRetries = 0;

    // 进入扫描前的共享连接参数 (参数扫描/发现结束恢复)
    MasterClient::Kind m_savedKind = MasterClient::Kind::SerialRtu;
    QString m_savedPort;
    qint32 m_savedBaud = 0;
    QSerialPort::Parity m_savedParity = QSerialPort::NoParity;
    QSerialPort::StopBits m_savedStop = QSerialPort::OneStop;
    QString m_savedHost;
    quint16 m_savedTcpPort = 0;
    bool m_wasConnected = false;

    // 串口参数扫描
    QVector<Combo> m_combos;
    int m_comboIdx = 0;
    int m_probeSlave = 0;
    int m_probeSlaveEnd = 0;

    // TCP 发现
    QStringList m_tcpHosts; // 端口开放的主机
    int m_hostIdx = 0;
    quint16 m_scanPort = 502;
    int m_unitCur = 0;
    int m_unitEnd = 0;
    int m_portTotal = 0;
    int m_portDone = 0;
    int m_portOpen = 0;
    QList<QTcpSocket *> m_probing;
    QHash<QTcpSocket *, qint64> m_probeDeadline; // m_probeClock 基准 ms
    QHash<QTcpSocket *, QString> m_probeIp;
    QStringList m_probeQueue;
    QElapsedTimer m_probeClock;

    bool m_stopOnFound = true;  // 命中首个结果即结束 (两个新页各自勾选注入)
    bool m_awaitConnect = false; // connectDevice 已发起, 等 connected/兜底定时器
    QTimer *m_connTimer = nullptr; // connect 异步失败兜底 (失败不发 stateChanged)
    QTimer *m_probeTick = nullptr; // 端口探测超时巡检

    QSpinBox *m_slaveFrom = nullptr;
    QSpinBox *m_slaveTo = nullptr;
    QSpinBox *m_addrSlave = nullptr;
    QComboBox *m_addrFc = nullptr;
    QSpinBox *m_addrFrom = nullptr;
    QSpinBox *m_addrTo = nullptr;
    QPushButton *m_startSlaveBtn = nullptr;
    QPushButton *m_startAddrBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QLabel *m_progress = nullptr;
    QSpinBox *m_timeoutSpin = nullptr; // 无应答超时 (全模式共用, 默认 100ms)
    QListWidget *m_slaveList = nullptr;
    QTableWidget *m_addrTable = nullptr;
    QPushButton *m_exportBtn = nullptr;

    // 串口参数扫描页
    QComboBox *m_serPort = nullptr;
    QList<QCheckBox *> m_baudChecks; // 与 kScanBauds 下标对应
    QCheckBox *m_parNone = nullptr;
    QCheckBox *m_parEven = nullptr;
    QCheckBox *m_parOdd = nullptr;
    QCheckBox *m_stop1 = nullptr;
    QCheckBox *m_stop2 = nullptr;
    QSpinBox *m_serSlaveFrom = nullptr;
    QSpinBox *m_serSlaveTo = nullptr;
    QCheckBox *m_serStopFirst = nullptr;
    QPushButton *m_startSerialBtn = nullptr;
    QTableWidget *m_serTable = nullptr;

    // TCP 发现页
    QLineEdit *m_tcpFrom = nullptr;
    QLineEdit *m_tcpTo = nullptr;
    QSpinBox *m_tcpPortSpin = nullptr;
    QSpinBox *m_unitFrom = nullptr;
    QSpinBox *m_unitTo = nullptr;
    QCheckBox *m_tcpStopFirst = nullptr;
    QPushButton *m_startTcpBtn = nullptr;
    QTableWidget *m_tcpTable = nullptr;
};
