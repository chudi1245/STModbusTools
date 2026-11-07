// ============================================================================
// ScanDialog.cpp — 扫描实现 (从站 / 地址 / 串口参数 / TCP 发现)
// ============================================================================

#include "ScanDialog.h"

#include "ToolFrames.h"

#include <QAbstractSocket>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QTcpSocket>
#include <QTextStream>
#include <QVBoxLayout>

namespace {
// 波特率候选 (与连接设置下拉一致); 展示按数值升序
const int kScanBauds[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
// 探测顺序按常见度优先: 命中即停时缩短扫描时间 (下标对应 kScanBauds)
const int kProbeOrder[] = {3, 4, 7, 2, 5, 6, 1, 0};
constexpr int kPortProbesParallel = 64; // 端口并行探测数
constexpr int kPortProbeTimeoutMs = 800;
constexpr int kMaxIpRange = 4096; // 网段手滑防护 (典型 /24 = 254)

QString parityText(QSerialPort::Parity p)
{
    switch (p)
    {
    case QSerialPort::EvenParity: return ScanDialog::tr("偶");
    case QSerialPort::OddParity: return ScanDialog::tr("奇");
    default: return ScanDialog::tr("无");
    }
}
int parityIndex(QSerialPort::Parity p) // 对齐 AppSettings::MbToolConn.parity 语义
{
    switch (p)
    {
    case QSerialPort::EvenParity: return 1;
    case QSerialPort::OddParity: return 2;
    default: return 0;
    }
}
int stopBitsNum(QSerialPort::StopBits s) { return s == QSerialPort::TwoStop ? 2 : 1; }

// 取首个非回传/非链路本地的 IPv4 的 /24 网段, 预填 TCP 发现扫描范围
QString guessLocalSegment()
{
    const QList<QHostAddress> addrs = QNetworkInterface::allAddresses();
    for (const QHostAddress &a : addrs)
    {
        if (a.protocol() != QAbstractSocket::IPv4Protocol || a.isLoopback())
            continue;
        const quint32 ip = a.toIPv4Address();
        if ((ip & 0xFFFF0000u) == 0xA9FE0000u) // 169.254/16 APIPA 无网段意义
            continue;
        return QStringLiteral("%1.%2.%3")
            .arg((ip >> 24) & 0xFF)
            .arg((ip >> 16) & 0xFF)
            .arg((ip >> 8) & 0xFF);
    }
    return QStringLiteral("192.168.1");
}
} // namespace

ScanDialog::ScanDialog(MasterClient *mb, QWidget *parent)
    : QDialog(parent)
    , m_mb(mb)
{
    setWindowTitle(tr("扫描 (从站 / 地址 / 串口参数 / TCP 发现)"));
    resize(660, 600);
    // 进扫描前的参数在构造时快照: 未扫描直接关闭时析构恢复不会把
    // 未初始化的 0 写回共享连接 (回归点); m_mb 可为空 (无连接, 起扫自检拦截)
    if (m_mb)
    {
        m_savedTimeoutMs = m_mb->timeoutMs();
        m_savedRetries = m_mb->retries();
        saveConnState();
    }
    buildUi();
}

void ScanDialog::setClient(MasterClient *mb)
{
    if (mb == m_mb)
        return;
    if (m_mode != Mode::None)
        return; // 扫描中拒绝切换 (共享连接正被重定向)
    if (m_mb)
        disconnect(m_mb, nullptr, this, nullptr);
    m_mb = mb;
    if (m_mb)
    {
        connect(m_mb, &MasterClient::readDone, this, &ScanDialog::onReadDone);
        connect(m_mb, &MasterClient::connectionChanged, this, &ScanDialog::onConnChanged);
        m_savedTimeoutMs = m_mb->timeoutMs();
        m_savedRetries = m_mb->retries();
        saveConnState();
        onConnChanged(m_mb->isConnected());
    }
}

ScanDialog::~ScanDialog()
{
    stopScan(); // 扫描中: 恢复原连接参数/超时重试 (原连接态则重连)
}

void ScanDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    auto *tabs = new QTabWidget;
    root->addWidget(tabs);

    // ---- 从站扫描 ----
    auto *slaveTab = new QWidget;
    auto *sv = new QVBoxLayout(slaveTab);
    auto *sform = new QFormLayout;
    sform->setHorizontalSpacing(12);
    auto *srange = new QHBoxLayout;
    m_slaveFrom = new QSpinBox;
    m_slaveFrom->setRange(1, 247);
    m_slaveFrom->setValue(1);
    m_slaveTo = new QSpinBox;
    m_slaveTo->setRange(1, 247);
    m_slaveTo->setValue(64);
    srange->addWidget(new QLabel(tr("从")));
    srange->addWidget(m_slaveFrom);
    srange->addWidget(new QLabel(tr("到")));
    srange->addWidget(m_slaveTo);
    srange->addStretch();
    auto *w = new QWidget;
    w->setLayout(srange);
    sform->addRow(tr("范围 (FC03 读地址 0 ×1)"), w);
    sv->addLayout(sform);
    m_startSlaveBtn = new QPushButton(tr("开始扫描"));
    m_startSlaveBtn->setObjectName(QStringLiteral("scan.startSlave"));
    sv->addWidget(m_startSlaveBtn);
    m_slaveList = new QListWidget;
    m_slaveList->setObjectName(QStringLiteral("scan.slaveList"));
    sv->addWidget(m_slaveList, 1);
    connect(m_slaveList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *it) {
                const int slave = it->data(Qt::UserRole).toInt();
                if (slave > 0)
                    emit slavePicked(slave);
            });
    sv->addWidget(new QLabel(tr("双击结果 → 应用为当前定义的从站地址")));
    tabs->addTab(slaveTab, tr("从站扫描"));

    // ---- 地址扫描 ----
    auto *addrTab = new QWidget;
    auto *av = new QVBoxLayout(addrTab);
    auto *aform = new QFormLayout;
    aform->setHorizontalSpacing(12);
    auto *aRow0 = new QHBoxLayout;
    m_addrSlave = new QSpinBox;
    m_addrSlave->setRange(1, 247);
    m_addrSlave->setValue(1);
    aRow0->addWidget(new QLabel(tr("从站")));
    aRow0->addWidget(m_addrSlave);
    m_addrFc = new QComboBox;
    m_addrFc->addItem(tr("03 保持寄存器"), 3);
    m_addrFc->addItem(tr("04 输入寄存器"), 4);
    m_addrFc->addItem(tr("01 线圈"), 1);
    m_addrFc->addItem(tr("02 离散输入"), 2);
    aRow0->addWidget(new QLabel(tr("功能码")));
    aRow0->addWidget(m_addrFc);
    aRow0->addStretch();
    auto *w0 = new QWidget;
    w0->setLayout(aRow0);
    aform->addRow(tr("探测参数"), w0);
    auto *aRow1 = new QHBoxLayout;
    m_addrFrom = new QSpinBox;
    m_addrFrom->setRange(0, 65535);
    m_addrFrom->setValue(0);
    m_addrTo = new QSpinBox;
    m_addrTo->setRange(0, 65535);
    m_addrTo->setValue(99);
    aRow1->addWidget(new QLabel(tr("从")));
    aRow1->addWidget(m_addrFrom);
    aRow1->addWidget(new QLabel(tr("到")));
    aRow1->addWidget(m_addrTo);
    aRow1->addStretch();
    auto *w1 = new QWidget;
    w1->setLayout(aRow1);
    aform->addRow(tr("地址范围"), w1);
    av->addLayout(aform);
    auto *aBtnRow = new QHBoxLayout;
    m_startAddrBtn = new QPushButton(tr("开始扫描"));
    m_startAddrBtn->setObjectName(QStringLiteral("scan.startAddr"));
    m_exportBtn = new QPushButton(tr("导出 CSV"));
    m_exportBtn->setEnabled(false);
    aBtnRow->addWidget(m_startAddrBtn);
    aBtnRow->addWidget(m_exportBtn);
    aBtnRow->addStretch();
    av->addLayout(aBtnRow);
    m_addrTable = new QTableWidget;
    m_addrTable->setObjectName(QStringLiteral("scan.addrTable"));
    m_addrTable->verticalHeader()->setVisible(false);
    m_addrTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_addrTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_addrTable->horizontalHeader()->setStretchLastSection(true);
    m_addrTable->setColumnCount(3);
    m_addrTable->setHorizontalHeaderLabels({tr("地址"), tr("结果"), tr("首寄存器值")});
    m_addrTable->setColumnWidth(0, 90);
    m_addrTable->setColumnWidth(1, 220);
    av->addWidget(m_addrTable, 1);
    tabs->addTab(addrTab, tr("地址扫描"));

    // ---- 串口参数扫描 ----
    auto *serTab = new QWidget;
    auto *qv = new QVBoxLayout(serTab);
    auto *qform = new QFormLayout;
    qform->setHorizontalSpacing(12);
    m_serPort = new QComboBox;
    m_serPort->setObjectName(QStringLiteral("scan.serialPort"));
    m_serPort->setMinimumWidth(160);
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts())
        m_serPort->addItem(info.description().isEmpty()
                               ? info.portName()
                               : QStringLiteral("%1 (%2)").arg(info.portName(),
                                                               info.description()),
                           info.portName());
    if (m_mb && m_mb->kind() == MasterClient::Kind::SerialRtu
        && !m_mb->serialPortName().isEmpty())
    {
        const int pi = m_serPort->findData(m_mb->serialPortName());
        if (pi >= 0)
            m_serPort->setCurrentIndex(pi); // 默认当前连接所用端口
    }
    qform->addRow(tr("端口 (Port)"), m_serPort);

    auto *baudRow = new QGridLayout;
    baudRow->setHorizontalSpacing(10);
    for (int i = 0; i < 8; ++i)
    {
        auto *cb = new QCheckBox(QString::number(kScanBauds[i]));
        cb->setChecked(true);
        m_baudChecks.append(cb);
        baudRow->addWidget(cb, i / 4, i % 4);
    }
    qform->addRow(tr("波特率 (Baud Rate)"), baudRow);

    auto *fmtRow = new QHBoxLayout;
    fmtRow->addWidget(new QLabel(tr("校验")));
    m_parNone = new QCheckBox(tr("无"));
    m_parNone->setChecked(true);
    m_parEven = new QCheckBox(tr("偶"));
    m_parEven->setChecked(true);
    m_parOdd = new QCheckBox(tr("奇"));
    fmtRow->addWidget(m_parNone);
    fmtRow->addWidget(m_parEven);
    fmtRow->addWidget(m_parOdd);
    fmtRow->addSpacing(16);
    fmtRow->addWidget(new QLabel(tr("停止位")));
    m_stop1 = new QCheckBox(QStringLiteral("1"));
    m_stop1->setChecked(true);
    m_stop2 = new QCheckBox(QStringLiteral("2"));
    fmtRow->addWidget(m_stop1);
    fmtRow->addWidget(m_stop2);
    fmtRow->addStretch();
    auto *wf = new QWidget;
    wf->setLayout(fmtRow);
    qform->addRow(tr("数据格式 (8 数据位, RTU 标准)"), wf);

    auto *qRow = new QHBoxLayout;
    m_serSlaveFrom = new QSpinBox;
    m_serSlaveFrom->setRange(1, 247);
    m_serSlaveFrom->setValue(1);
    m_serSlaveTo = new QSpinBox;
    m_serSlaveTo->setRange(1, 247);
    m_serSlaveTo->setValue(64);
    qRow->addWidget(new QLabel(tr("从")));
    qRow->addWidget(m_serSlaveFrom);
    qRow->addWidget(new QLabel(tr("到")));
    qRow->addWidget(m_serSlaveTo);
    qRow->addStretch();
    auto *wq = new QWidget;
    wq->setLayout(qRow);
    qform->addRow(tr("每组合探测从站范围"), wq);
    qv->addLayout(qform);

    m_serStopFirst = new QCheckBox(tr("找到第一个从站后停止"));
    m_serStopFirst->setObjectName(QStringLiteral("scan.serialStopFirst"));
    m_serStopFirst->setChecked(true);
    qv->addWidget(m_serStopFirst);
    m_startSerialBtn = new QPushButton(tr("开始扫描"));
    m_startSerialBtn->setObjectName(QStringLiteral("scan.startSerial"));
    qv->addWidget(m_startSerialBtn);
    m_serTable = new QTableWidget;
    m_serTable->setObjectName(QStringLiteral("scan.serialTable"));
    m_serTable->verticalHeader()->setVisible(false);
    m_serTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_serTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_serTable->horizontalHeader()->setStretchLastSection(true);
    m_serTable->setColumnCount(6);
    m_serTable->setHorizontalHeaderLabels({tr("波特率"), tr("校验"), tr("停止位"),
                                           tr("从站"), tr("耗时 ms"), tr("结果")});
    m_serTable->setColumnWidth(0, 90);
    m_serTable->setColumnWidth(1, 70);
    m_serTable->setColumnWidth(2, 70);
    m_serTable->setColumnWidth(3, 70);
    m_serTable->setColumnWidth(4, 90);
    qv->addWidget(m_serTable, 1);
    qv->addWidget(new QLabel(tr("双击结果 → 应用为连接参数与当前定义的从站地址")));
    tabs->addTab(serTab, tr("串口参数扫描"));

    // ---- TCP 发现 ----
    auto *tcpTab = new QWidget;
    auto *tv = new QVBoxLayout(tcpTab);
    auto *tform = new QFormLayout;
    tform->setHorizontalSpacing(12);
    const QRegularExpressionValidator ipVal(
        QRegularExpression(QStringLiteral(R"((\d{1,3}\.){3}\d{1,3})")));
    auto *ipRow = new QHBoxLayout;
    m_tcpFrom = new QLineEdit(guessLocalSegment() + QStringLiteral(".1"));
    m_tcpFrom->setObjectName(QStringLiteral("scan.tcpFrom"));
    m_tcpFrom->setValidator(&ipVal);
    m_tcpTo = new QLineEdit(guessLocalSegment() + QStringLiteral(".254"));
    m_tcpTo->setObjectName(QStringLiteral("scan.tcpTo"));
    m_tcpTo->setValidator(&ipVal);
    ipRow->addWidget(new QLabel(tr("从")));
    ipRow->addWidget(m_tcpFrom, 1);
    ipRow->addWidget(new QLabel(tr("到")));
    ipRow->addWidget(m_tcpTo, 1);
    auto *wi = new QWidget;
    wi->setLayout(ipRow);
    tform->addRow(tr("IP 范围 (按本机网段预填)"), wi);
    m_tcpPortSpin = new QSpinBox;
    m_tcpPortSpin->setObjectName(QStringLiteral("scan.tcpPort"));
    m_tcpPortSpin->setRange(1, 65535);
    m_tcpPortSpin->setValue(502); // Modbus/TCP 标准端口
    tform->addRow(tr("端口 (Server Port)"), m_tcpPortSpin);
    auto *uRow = new QHBoxLayout;
    m_unitFrom = new QSpinBox;
    m_unitFrom->setObjectName(QStringLiteral("scan.tcpUnitFrom"));
    m_unitFrom->setRange(1, 247);
    m_unitFrom->setValue(1);
    m_unitTo = new QSpinBox;
    m_unitTo->setObjectName(QStringLiteral("scan.tcpUnitTo"));
    m_unitTo->setRange(1, 247);
    m_unitTo->setValue(64);
    uRow->addWidget(new QLabel(tr("从")));
    uRow->addWidget(m_unitFrom);
    uRow->addWidget(new QLabel(tr("到")));
    uRow->addWidget(m_unitTo);
    uRow->addStretch();
    auto *wu = new QWidget;
    wu->setLayout(uRow);
    tform->addRow(tr("单元 ID 范围 (FC03 读地址 0 ×1)"), wu);
    tv->addLayout(tform);

    m_tcpStopFirst = new QCheckBox(tr("找到第一个设备后停止"));
    m_tcpStopFirst->setObjectName(QStringLiteral("scan.tcpStopFirst"));
    m_tcpStopFirst->setChecked(true);
    tv->addWidget(m_tcpStopFirst);
    auto *tBtnRow = new QHBoxLayout;
    m_startTcpBtn = new QPushButton(tr("开始扫描"));
    m_startTcpBtn->setObjectName(QStringLiteral("scan.startTcp"));
    tBtnRow->addWidget(m_startTcpBtn);
    tBtnRow->addStretch();
    tv->addLayout(tBtnRow);
    m_tcpTable = new QTableWidget;
    m_tcpTable->setObjectName(QStringLiteral("scan.tcpTable"));
    m_tcpTable->verticalHeader()->setVisible(false);
    m_tcpTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tcpTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tcpTable->horizontalHeader()->setStretchLastSection(true);
    m_tcpTable->setColumnCount(5);
    m_tcpTable->setHorizontalHeaderLabels({tr("IP 地址"), tr("端口"), tr("单元 ID"),
                                           tr("耗时 ms"), tr("结果")});
    m_tcpTable->setColumnWidth(0, 130);
    m_tcpTable->setColumnWidth(1, 70);
    m_tcpTable->setColumnWidth(2, 80);
    m_tcpTable->setColumnWidth(3, 90);
    tv->addWidget(m_tcpTable, 1);
    tv->addWidget(new QLabel(tr("双击结果 → 应用为连接参数与当前定义的从站地址")));
    tabs->addTab(tcpTab, tr("TCP 发现"));

    // ---- 公共: 进度 + 无应答超时 + 停止 ----
    auto *bar = new QHBoxLayout;
    m_progress = new QLabel(tr("空闲"));
    m_progress->setObjectName(QStringLiteral("scan.progress"));
    m_stopBtn = new QPushButton(tr("停止"));
    m_stopBtn->setEnabled(false);
    bar->addWidget(m_progress, 1);
    auto *toLbl = new QLabel(tr("无应答超时"));
    toLbl->setToolTip(tr("单次探测的等待上限, 到点判无应答即跳下一个;"
                         " 低波特率 (≤2400) 一问一答较慢, 建议加大"));
    m_timeoutSpin = new QSpinBox;
    m_timeoutSpin->setObjectName(QStringLiteral("scan.probeTimeoutMs"));
    m_timeoutSpin->setRange(50, 3000);
    m_timeoutSpin->setValue(100); // 常见波特率足够; 低波特率用户自行加大
    m_timeoutSpin->setSuffix(tr(" ms"));
    bar->addWidget(toLbl);
    bar->addWidget(m_timeoutSpin);
    bar->addWidget(m_stopBtn);
    root->addLayout(bar);

    m_connTimer = new QTimer(this);
    m_connTimer->setSingleShot(true);
    connect(m_connTimer, &QTimer::timeout, this, [this] {
        if (!m_awaitConnect)
            return;
        // connect 异步失败 (TCP 拒绝/串口占用) 不发 stateChanged — 兜底跳目标
        m_awaitConnect = false;
        if (m_mode == Mode::SerialParam)
            beginCombo(m_comboIdx + 1);
        else if (m_mode == Mode::TcpUnit)
            beginTcpHost(m_hostIdx + 1);
    });
    m_probeTick = new QTimer(this);
    connect(m_probeTick, &QTimer::timeout, this, [this] {
        const qint64 now = m_probeClock.elapsed();
        QList<QTcpSocket *> expired;
        for (auto it = m_probeDeadline.cbegin(); it != m_probeDeadline.cend(); ++it)
            if (now >= it.value())
                expired.append(it.key());
        for (QTcpSocket *s : expired)
            settleProbe(s, false);
    });

    connect(m_startSlaveBtn, &QPushButton::clicked, this, &ScanDialog::startSlaveScan);
    connect(m_startAddrBtn, &QPushButton::clicked, this, &ScanDialog::startAddrScan);
    connect(m_startSerialBtn, &QPushButton::clicked, this, &ScanDialog::startSerialScan);
    connect(m_startTcpBtn, &QPushButton::clicked, this, &ScanDialog::startTcpScan);
    connect(m_stopBtn, &QPushButton::clicked, this, &ScanDialog::stopScan);
    connect(m_exportBtn, &QPushButton::clicked, this, &ScanDialog::exportAddrCsv);
    if (m_mb) // 无连接时允许打开 (离线浏览), 起扫自检拦截; setClient 时补接
    {
        connect(m_mb, &MasterClient::readDone, this, &ScanDialog::onReadDone);
        connect(m_mb, &MasterClient::connectionChanged, this, &ScanDialog::onConnChanged);
    }
    connect(m_serTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (m_mode != Mode::None || row < 0 || row >= m_serTable->rowCount())
            return; // 扫描中禁止回填 (会打断状态机)
        const int baud = m_serTable->item(row, 0)->data(Qt::UserRole).toInt();
        const int parity = m_serTable->item(row, 1)->data(Qt::UserRole).toInt();
        const int stop = m_serTable->item(row, 2)->data(Qt::UserRole).toInt();
        const int slave = m_serTable->item(row, 3)->data(Qt::UserRole).toInt();
        if (baud > 0 && slave > 0)
            emit serialPicked(baud, parity, stop, slave);
    });
    connect(m_tcpTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (m_mode != Mode::None || row < 0 || row >= m_tcpTable->rowCount())
            return;
        const int port = m_tcpTable->item(row, 1)->data(Qt::UserRole).toInt();
        const int slave = m_tcpTable->item(row, 2)->data(Qt::UserRole).toInt();
        const QString host = m_tcpTable->item(row, 0)->text();
        if (port > 0 && slave > 0)
            emit tcpPicked(host, quint16(port), slave);
    });
}

// ============================================================================
// 公共流程
// ============================================================================
bool ScanDialog::probeIsAlive(bool ok, const QString &errText) const
{
    if (ok)
        return true;
    // 协议异常 (含 CRC 错) = 有设备应答; 超时/连接错 = 判不存在
    return errText.contains(QStringLiteral("协议"), Qt::CaseInsensitive)
           || errText.contains(QStringLiteral("Protocol"), Qt::CaseInsensitive)
           || errText.contains(QStringLiteral("CRC"), Qt::CaseInsensitive)
           || errText.contains(QStringLiteral("exception"), Qt::CaseInsensitive);
}

bool ScanDialog::beginScanCommon(Mode m)
{
    if (m_mode != Mode::None)
        return false;
    if (!m_mb)
    {
        m_progress->setText(tr("无连接 — 请先添加连接 (设备树 / 连接菜单)"));
        return false;
    }
    m_alive = m_done = 0;
    m_mode = m;
    m_savedTimeoutMs = m_mb->timeoutMs();
    m_savedRetries = m_mb->retries();
    resetButtons(true);
    if (m == Mode::SerialParam || m == Mode::TcpUnit)
    {
        saveConnState();
        emit scanBusyChanged(true); // 主窗暂停轮询, 共享连接将被重定向
    }
    // 各模式统一用界面的无应答超时 (默认 100ms), 0 重试 — 到点即跳下一个
    m_mb->setTimeoutMs(m_timeoutSpin->value());
    m_mb->setRetries(0);
    return true;
}

void ScanDialog::saveConnState()
{
    m_savedKind = m_mb->kind();
    m_savedPort = m_mb->serialPortName();
    m_savedBaud = m_mb->baud();
    m_savedParity = m_mb->parity();
    m_savedStop = m_mb->stopBits();
    m_savedHost = m_mb->host();
    m_savedTcpPort = m_mb->tcpPort();
    m_wasConnected = m_mb->isConnected();
}

void ScanDialog::restoreConnection()
{
    m_mb->disconnectDevice();
    m_mb->setKind(m_savedKind);
    m_mb->setSerialParams(m_savedPort, m_savedBaud, m_savedParity, m_savedStop);
    m_mb->setTcpParams(m_savedHost, m_savedTcpPort);
    m_mb->setTimeoutMs(m_savedTimeoutMs);
    m_mb->setRetries(m_savedRetries);
    if (m_wasConnected)
        m_mb->connectDevice(); // 异步失败由主窗状态栏呈现
}

void ScanDialog::resetButtons(bool running)
{
    m_stopBtn->setEnabled(running);
    m_startSlaveBtn->setEnabled(!running);
    m_startAddrBtn->setEnabled(!running);
    m_startSerialBtn->setEnabled(!running);
    m_startTcpBtn->setEnabled(!running);
}

// ============================================================================
// 从站 / 地址扫描 (既有页, 要求已连接)
// ============================================================================
void ScanDialog::startSlaveScan()
{
    if (!beginScanCommon(Mode::Slave))
        return;
    if (!m_mb->isConnected())
    {
        finishScan(tr("从站扫描需先连接 (F3)"));
        return;
    }
    m_cursor = m_slaveFrom->value();
    m_end = qMax(m_cursor, m_slaveTo->value());
    m_slaveList->clear();
    sendNext();
}

void ScanDialog::startAddrScan()
{
    if (!beginScanCommon(Mode::Addr))
        return;
    if (!m_mb->isConnected())
    {
        finishScan(tr("地址扫描需先连接 (F3)"));
        return;
    }
    m_cursor = m_addrFrom->value();
    m_end = qMax(m_cursor, m_addrTo->value());
    m_addrTable->setRowCount(0);
    m_exportBtn->setEnabled(false);
    sendNext();
}

void ScanDialog::sendNext()
{
    if (m_mode != Mode::Slave && m_mode != Mode::Addr)
        return;
    const bool beyond = m_cursor > m_end;
    const int from = m_mode == Mode::Addr ? m_addrFrom->value() : m_slaveFrom->value();
    m_progress->setText(tr("扫描中… %1 / %2 (存活 %3)")
                            .arg(m_done)
                            .arg(m_end - from + 1)
                            .arg(m_alive));
    if (beyond || !m_mb->isConnected())
    {
        finishScan(beyond ? tr("完成") : tr("中断 (连接已断开)"));
        return;
    }
    const int fc = m_mode == Mode::Addr ? m_addrFc->currentData().toInt() : 3;
    const quint16 addr = m_mode == Mode::Addr ? quint16(m_cursor) : 0;
    const int slave = m_mode == Mode::Addr ? m_addrSlave->value() : m_cursor;
    if (!m_mb->read(fc, slave, addr, 1))
    {
        if (m_mode == Mode::None)
            return; // 失败同步触发 readDone → finishScan 已收尾
        m_cursor++; // 发送被拒/连接瞬断: 跳过该点继续 (下轮 isConnected 兜底终止)
        sendNext();
        return;
    }
    // 结果在 onReadDone 推进游标
}

// ============================================================================
// 串口参数扫描 (逐组合重开串口)
// ============================================================================
void ScanDialog::startSerialScan()
{
    if (!beginScanCommon(Mode::SerialParam))
        return;
    const QString port = m_serPort->currentData().toString();
    if (port.isEmpty())
    {
        finishScan(tr("串口参数扫描: 未选择端口"));
        return;
    }
    const QSerialPort::Parity pars[] = {QSerialPort::NoParity, QSerialPort::EvenParity,
                                        QSerialPort::OddParity};
    const bool parOn[] = {m_parNone->isChecked(), m_parEven->isChecked(),
                          m_parOdd->isChecked()};
    const QSerialPort::StopBits stops[] = {QSerialPort::OneStop, QSerialPort::TwoStop};
    const bool stopOn[] = {m_stop1->isChecked(), m_stop2->isChecked()};
    m_combos.clear();
    for (int bi : kProbeOrder)
    {
        if (bi >= m_baudChecks.size() || !m_baudChecks[bi]->isChecked())
            continue;
        for (int pi = 0; pi < 3; ++pi)
        {
            if (!parOn[pi])
                continue;
            for (int si = 0; si < 2; ++si)
            {
                if (!stopOn[si])
                    continue;
                m_combos.append({kScanBauds[bi], pars[pi], stops[si]});
            }
        }
    }
    if (m_combos.isEmpty())
    {
        finishScan(tr("串口参数扫描: 至少勾选一个波特率/校验/停止位"));
        return;
    }
    m_mb->setKind(MasterClient::Kind::SerialRtu);
    m_probeSlaveEnd = qMax(m_serSlaveFrom->value(), m_serSlaveTo->value());
    m_stopOnFound = m_serStopFirst->isChecked();
    m_serTable->setRowCount(0);
    beginCombo(0);
}

void ScanDialog::beginCombo(int idx)
{
    if (m_mode != Mode::SerialParam)
        return;
    m_comboIdx = idx;
    if (idx >= m_combos.size())
    {
        finishScan(tr("完成 — 所有组合无应答"));
        return;
    }
    const Combo c = m_combos[idx];
    m_progress->setText(tr("串口参数扫描… 组合 %1/%2 (%3 %4%5) · 从站 %6-%7 (命中 %8)")
                            .arg(idx + 1)
                            .arg(m_combos.size())
                            .arg(c.baud)
                            .arg(parityText(c.parity))
                            .arg(stopBitsNum(c.stop))
                            .arg(m_serSlaveFrom->value())
                            .arg(m_probeSlaveEnd)
                            .arg(m_alive));
    m_mb->disconnectDevice();
    m_mb->setSerialParams(m_serPort->currentData().toString(), c.baud, c.parity, c.stop);
    m_awaitConnect = false;
    if (!m_mb->connectDevice())
    {
        beginCombo(idx + 1); // 端口打不开 (被占用等): 跳过该组合
        return;
    }
    if (m_mb->isConnected())
    {
        m_probeSlave = m_serSlaveFrom->value(); // 串口打开通常同步完成
        probeNextComboSlave();
    }
    else
    {
        m_awaitConnect = true; // 异步失败由 m_connTimer 兜底收敛
        m_connTimer->start(2000);
    }
}

void ScanDialog::probeNextComboSlave()
{
    if (m_mode != Mode::SerialParam)
        return;
    if (m_probeSlave > m_probeSlaveEnd)
    {
        beginCombo(m_comboIdx + 1);
        return;
    }
    if (!m_mb->read(3, m_probeSlave, 0, 1))
    {
        if (m_mode == Mode::None)
            return;
        // 发送被拒/串口被系统断开 (错误参数下的杂波可致 Qt 自动关口):
        // 放弃该组合跳下一组重连 — 不整场中止 (此前表现为扫第一个就停)
        beginCombo(m_comboIdx + 1);
    }
}

// ============================================================================
// TCP 发现 (并行探端口 → 逐主机探单元)
// ============================================================================
void ScanDialog::startTcpScan()
{
    if (!beginScanCommon(Mode::TcpUnit))
        return;
    const QHostAddress aFrom(m_tcpFrom->text().trimmed());
    const QHostAddress aTo(m_tcpTo->text().trimmed());
    if (aFrom.protocol() != QAbstractSocket::IPv4Protocol
        || aTo.protocol() != QAbstractSocket::IPv4Protocol)
    {
        finishScan(tr("TCP 发现: IP 地址无效"));
        return;
    }
    quint32 from = aFrom.toIPv4Address();
    quint32 to = aTo.toIPv4Address();
    if (from > to)
        std::swap(from, to);
    if (to - from + 1 > quint32(kMaxIpRange))
    {
        finishScan(tr("TCP 发现: IP 范围过大 (最多 %1 个)").arg(kMaxIpRange));
        return;
    }
    m_scanPort = quint16(m_tcpPortSpin->value());
    m_unitEnd = qMax(m_unitFrom->value(), m_unitTo->value());
    m_stopOnFound = m_tcpStopFirst->isChecked();
    m_tcpTable->setRowCount(0);
    m_tcpHosts.clear();
    m_portOpen = m_portDone = 0;
    m_portTotal = int(to - from + 1);
    m_probeClock.start();
    m_probeQueue.clear();
    for (quint32 ip = from;; ++ip)
    {
        m_probeQueue.append(QStringLiteral("%1.%2.%3.%4")
                                .arg((ip >> 24) & 0xFF)
                                .arg((ip >> 16) & 0xFF)
                                .arg((ip >> 8) & 0xFF)
                                .arg(ip & 0xFF));
        if (ip == to) // 末地址 +1 会回绕, 不能用 <= 判终止
            break;
    }
    m_probeTick->start(100);
    launchPortProbes();
}

void ScanDialog::launchPortProbes()
{
    if (m_mode != Mode::TcpUnit)
        return;
    while (m_probing.size() < kPortProbesParallel && !m_probeQueue.isEmpty())
    {
        const QString ip = m_probeQueue.takeFirst();
        auto *s = new QTcpSocket(this);
        m_probing.append(s);
        m_probeIp[s] = ip;
        m_probeDeadline[s] = m_probeClock.elapsed() + kPortProbeTimeoutMs;
        connect(s, &QTcpSocket::connected, this, [this, s] { settleProbe(s, true); });
        connect(s, &QTcpSocket::errorOccurred, this, [this, s] { settleProbe(s, false); });
        s->connectToHost(ip, m_scanPort);
    }
    m_progress->setText(tr("TCP 端口探测… %1/%2 (开放 %3)")
                            .arg(m_portDone)
                            .arg(m_portTotal)
                            .arg(m_portOpen));
    if (m_probing.isEmpty() && m_probeQueue.isEmpty())
    {
        m_probeTick->stop();
        if (m_tcpHosts.isEmpty())
        {
            finishScan(tr("完成 — 未发现开放 %1 端口的主机").arg(m_scanPort));
            return;
        }
        beginTcpHost(0);
    }
}

void ScanDialog::settleProbe(QTcpSocket *s, bool open)
{
    if (!m_probing.contains(s))
        return; // 已停止/重复信号 (错误后仍可能补发 errorOccurred)
    const QString ip = m_probeIp.value(s);
    m_probing.removeOne(s);
    m_probeDeadline.remove(s);
    m_probeIp.remove(s);
    m_portDone++;
    if (open)
    {
        m_portOpen++;
        m_tcpHosts.append(ip);
    }
    s->abort();
    s->deleteLater();
    launchPortProbes();
}

void ScanDialog::abortPortProbes()
{
    m_probeTick->stop();
    m_probeQueue.clear();
    const QList<QTcpSocket *> socks = m_probing;
    for (QTcpSocket *s : socks)
    {
        m_probing.removeOne(s);
        m_probeDeadline.remove(s);
        m_probeIp.remove(s);
        s->abort();
        s->deleteLater();
    }
}

void ScanDialog::beginTcpHost(int idx)
{
    if (m_mode != Mode::TcpUnit)
        return;
    m_hostIdx = idx;
    if (idx >= m_tcpHosts.size())
    {
        finishScan(tr("完成 — 开放 %1, 单元命中 %2").arg(m_portOpen).arg(m_alive));
        return;
    }
    const QString host = m_tcpHosts[idx];
    m_progress->setText(tr("TCP 单元探测… %1:%2 (主机 %3/%4) · 单元 %5-%6 (命中 %7)")
                            .arg(host)
                            .arg(m_scanPort)
                            .arg(idx + 1)
                            .arg(m_tcpHosts.size())
                            .arg(m_unitFrom->value())
                            .arg(m_unitEnd)
                            .arg(m_alive));
    m_mb->disconnectDevice();
    m_mb->setKind(MasterClient::Kind::Tcp);
    m_mb->setTcpParams(host, m_scanPort);
    m_awaitConnect = false;
    if (!m_mb->connectDevice())
    {
        beginTcpHost(idx + 1);
        return;
    }
    if (m_mb->isConnected())
    {
        m_unitCur = m_unitFrom->value();
        probeNextUnit();
    }
    else
    {
        m_awaitConnect = true;
        m_connTimer->start(2500); // TCP 连接尝试可悬挂数秒
    }
}

void ScanDialog::probeNextUnit()
{
    if (m_mode != Mode::TcpUnit)
        return;
    if (m_unitCur > m_unitEnd)
    {
        beginTcpHost(m_hostIdx + 1);
        return;
    }
    if (!m_mb->read(3, m_unitCur, 0, 1))
    {
        if (m_mode == Mode::None)
            return;
        beginTcpHost(m_hostIdx + 1); // 该主机请求受阻: 跳下一主机, 不整场中止
    }
}

// ============================================================================
// 应答处理 / 结束
// ============================================================================
void ScanDialog::onConnChanged(bool connected)
{
    if (!m_awaitConnect || m_mode == Mode::None || !connected)
        return; // 断开事件忽略: 失败由 m_connTimer 兜底, 中断由发送路径判
    m_awaitConnect = false;
    m_connTimer->stop();
    if (m_mode == Mode::SerialParam)
    {
        m_probeSlave = m_serSlaveFrom->value();
        probeNextComboSlave();
    }
    else if (m_mode == Mode::TcpUnit)
    {
        m_unitCur = m_unitFrom->value();
        probeNextUnit();
    }
}

void ScanDialog::onReadDone(int fc, int slave, quint16 addr, bool ok,
                            const QVector<quint16> &values, const QString &errText,
                            double elapsedMs)
{
    if (m_mode == Mode::None)
        return;

    if (m_mode == Mode::SerialParam)
    {
        if (fc != 3 || addr != 0 || slave != m_probeSlave)
            return; // 他人事务
        m_done++;
        const bool alive = probeIsAlive(ok, errText);
        if (alive)
        {
            m_alive++;
            const Combo c = m_combos[m_comboIdx];
            const int row = m_serTable->rowCount();
            m_serTable->insertRow(row);
            auto *c0 = new QTableWidgetItem(QString::number(c.baud));
            c0->setData(Qt::UserRole, c.baud);
            m_serTable->setItem(row, 0, c0);
            auto *c1 = new QTableWidgetItem(parityText(c.parity));
            c1->setData(Qt::UserRole, parityIndex(c.parity));
            m_serTable->setItem(row, 1, c1);
            auto *c2 = new QTableWidgetItem(QString::number(stopBitsNum(c.stop)));
            c2->setData(Qt::UserRole, stopBitsNum(c.stop));
            m_serTable->setItem(row, 2, c2);
            auto *c3 = new QTableWidgetItem(QString::number(m_probeSlave));
            c3->setData(Qt::UserRole, m_probeSlave);
            m_serTable->setItem(row, 3, c3);
            m_serTable->setItem(row, 4,
                                new QTableWidgetItem(QString::number(elapsedMs, 'f', 1)));
            auto *c5 = new QTableWidgetItem(ok ? tr("OK") : tr("异常应答 (设备在)"));
            c5->setForeground(QColor(ok ? QStringLiteral("#16a34a")
                                        : QStringLiteral("#d97706")));
            m_serTable->setItem(row, 5, c5);
            if (m_stopOnFound)
            {
                finishScan(tr("找到 — %1 波特 %2%3 从站 %4")
                               .arg(c.baud)
                               .arg(parityText(c.parity))
                               .arg(stopBitsNum(c.stop))
                               .arg(m_probeSlave));
                return;
            }
        }
        m_probeSlave++;
        probeNextComboSlave();
        return;
    }

    if (m_mode == Mode::TcpUnit)
    {
        if (fc != 3 || addr != 0 || slave != m_unitCur)
            return;
        m_done++;
        const bool alive = probeIsAlive(ok, errText);
        if (alive)
        {
            m_alive++;
            const QString host = m_tcpHosts[m_hostIdx];
            const int row = m_tcpTable->rowCount();
            m_tcpTable->insertRow(row);
            m_tcpTable->setItem(row, 0, new QTableWidgetItem(host));
            auto *c1 = new QTableWidgetItem(QString::number(m_scanPort));
            c1->setData(Qt::UserRole, m_scanPort);
            m_tcpTable->setItem(row, 1, c1);
            auto *c2 = new QTableWidgetItem(QString::number(m_unitCur));
            c2->setData(Qt::UserRole, m_unitCur);
            m_tcpTable->setItem(row, 2, c2);
            m_tcpTable->setItem(row, 3,
                                new QTableWidgetItem(QString::number(elapsedMs, 'f', 1)));
            auto *c4 = new QTableWidgetItem(ok ? tr("OK") : tr("异常应答 (设备在)"));
            c4->setForeground(QColor(ok ? QStringLiteral("#16a34a")
                                        : QStringLiteral("#d97706")));
            m_tcpTable->setItem(row, 4, c4);
            if (m_stopOnFound)
            {
                finishScan(tr("找到 — %1:%2 单元 %3")
                               .arg(host)
                               .arg(m_scanPort)
                               .arg(m_unitCur));
                return;
            }
        }
        m_unitCur++;
        probeNextUnit();
        return;
    }

    // ---- 从站 / 地址扫描 (既有页) ----
    const bool addrMode = m_mode == Mode::Addr;
    const int expectSlave = addrMode ? m_addrSlave->value() : m_cursor;
    const quint16 expectAddr = addrMode ? quint16(m_cursor) : 0;
    if (fc != (addrMode ? m_addrFc->currentData().toInt() : 3) || slave != expectSlave
        || addr != expectAddr)
        return; // 他人事务
    m_done++;
    const bool alive = probeIsAlive(ok, errText);
    if (alive)
        m_alive++;
    if (addrMode)
    {
        const int row = m_addrTable->rowCount();
        m_addrTable->insertRow(row);
        auto *c0 = new QTableWidgetItem(QString::number(m_cursor));
        m_addrTable->setItem(row, 0, c0);
        QString res;
        if (ok)
            res = tr("OK · %1 ms").arg(QString::number(elapsedMs, 'f', 1));
        else if (alive)
            res = tr("设备应答但地址无效 · %1").arg(errText);
        else
            res = tr("无应答 · %1").arg(errText);
        auto *c1 = new QTableWidgetItem(res);
        c1->setForeground(QColor(ok ? QStringLiteral("#16a34a")
                                    : (alive ? QStringLiteral("#d97706")
                                             : QStringLiteral("#94a3b8"))));
        m_addrTable->setItem(row, 1, c1);
        m_addrTable->setItem(row, 2, new QTableWidgetItem(
                                         ok && !values.isEmpty()
                                             ? QString::number(values.front())
                                             : QString()));
    }
    else if (alive)
    {
        auto *item = new QListWidgetItem(
            tr("从机 %1 存活 · %2 ms").arg(m_cursor).arg(QString::number(elapsedMs, 'f', 1)));
        item->setData(Qt::UserRole, m_cursor);
        m_slaveList->addItem(item);
    }
    m_cursor++;
    sendNext();
}

void ScanDialog::finishScan(const QString &summary)
{
    const Mode m = m_mode;
    m_mode = Mode::None;
    m_awaitConnect = false;
    abortPortProbes();
    m_connTimer->stop();
    resetButtons(false);
    if (m == Mode::SerialParam || m == Mode::TcpUnit)
    {
        emit scanBusyChanged(false);
        restoreConnection();
    }
    else
    {
        m_mb->setTimeoutMs(m_savedTimeoutMs);
        m_mb->setRetries(m_savedRetries);
    }
    m_exportBtn->setEnabled(m == Mode::Addr && m_addrTable->rowCount() > 0);
    m_progress->setText(tr("%1 — 探测 %2, 存活 %3").arg(summary).arg(m_done).arg(m_alive));
}

void ScanDialog::stopScan()
{
    if (m_mode == Mode::None)
        return;
    const Mode m = m_mode;
    m_mode = Mode::None; // 直接终止: 迟到应答被 onReadDone 的 Mode 守卫丢弃
    m_awaitConnect = false;
    abortPortProbes();
    m_connTimer->stop();
    resetButtons(false);
    if (m == Mode::SerialParam || m == Mode::TcpUnit)
    {
        emit scanBusyChanged(false);
        restoreConnection();
    }
    else
    {
        m_mb->setTimeoutMs(m_savedTimeoutMs);
        m_mb->setRetries(m_savedRetries);
    }
    m_exportBtn->setEnabled(m == Mode::Addr && m_addrTable->rowCount() > 0);
    m_progress->setText(tr("已停止 — 探测 %1, 存活 %2").arg(m_done).arg(m_alive));
}

void ScanDialog::exportAddrCsv()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("导出扫描结果"), QStringLiteral("modbusscan.csv"), tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("导出失败"), f.errorString());
        return;
    }
    f.write("\xEF\xBB\xBF"); // Excel UTF-8 识别 (对齐 CaptureExport)
    f.write("address,result,value\n");
    for (int r = 0; r < m_addrTable->rowCount(); ++r)
    {
        const QString line = QStringLiteral("%1,\"%2\",\"%3\"\n")
                                 .arg(m_addrTable->item(r, 0)->text(),
                                      m_addrTable->item(r, 1)->text(),
                                      m_addrTable->item(r, 2)->text());
        f.write(line.toUtf8());
    }
    QMessageBox::information(this, tr("导出完成"),
                             tr("已导出 %1 行").arg(m_addrTable->rowCount()));
}
