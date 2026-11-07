// ============================================================================
// ModbusMasterTool.cpp — Modbus 调试工具主窗实现 (MDI + 菜单/工具栏/状态栏)
// ============================================================================

#include "ModbusMasterTool.h"

#include "AppSettings.h"
#include "ConnDialog.h"
#include "ConnectionManager.h"
#include "DataViewWidget.h"
#include "DefDialog.h"
#include "DefDialogs.h"
#include "DeviceTreeWidget.h"
#include "FrameLogDialog.h"
#include "MasterClient.h"
#include "ModbusFormat.h"
#include "ModbusWorkspace.h"
#include "ScanDialog.h"
#include "SlavePoller.h"
#include "SlaveSetupDialog.h"
#include "TestCenterPage.h"
#include "ToolFrames.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenuBar>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QSerialPort>
#include <QVBoxLayout>

#include "Icons.h"

namespace {
// 显示格式菜单项 (16 位 5 + 32/64 位 6 类×4 字序 = 29, 对齐 Modbus Poll Display)
struct FmtEntry
{
    ModbusFormat::Fmt fmt;
    ModbusFormat::ByteOrder order;
    const char *group; // 子菜单名
    const char *text;
};
const FmtEntry kFmtMenu[] = {
    {ModbusFormat::Fmt::I16, ModbusFormat::ByteOrder::ABCD, "16 位", "Signed 有符号"},
    {ModbusFormat::Fmt::U16, ModbusFormat::ByteOrder::ABCD, "16 位", "Unsigned 无符号"},
    {ModbusFormat::Fmt::Hex, ModbusFormat::ByteOrder::ABCD, "16 位", "Hex 十六进制"},
    {ModbusFormat::Fmt::AsciiHex, ModbusFormat::ByteOrder::ABCD, "16 位", "ASCII"},
    {ModbusFormat::Fmt::Bin, ModbusFormat::ByteOrder::ABCD, "16 位", "Binary 二进制"},
};
const char *const kOrderNames[] = {"Big-endian (ABCD)", "Little-endian (DCBA)",
                                   "Big-endian byte swap (BADC)",
                                   "Little-endian byte swap (CDAB)"};
const ModbusFormat::ByteOrder kOrders[] = {
    ModbusFormat::ByteOrder::ABCD, ModbusFormat::ByteOrder::DCBA,
    ModbusFormat::ByteOrder::BADC, ModbusFormat::ByteOrder::CDAB};
const char *const kWideGroups[] = {"32 位", "64 位"};
const ModbusFormat::Fmt kWideFmts[2][3] = {
    {ModbusFormat::Fmt::I32, ModbusFormat::Fmt::U32, ModbusFormat::Fmt::F32},
    {ModbusFormat::Fmt::I64, ModbusFormat::Fmt::U64, ModbusFormat::Fmt::F64}};
const char *const kWideNames[2][3] = {
    {"Signed 有符号整数", "Unsigned 无符号整数", "Float 浮点数"},
    {"Signed 有符号整数", "Unsigned 无符号整数", "Double 浮点数"}};

QString fcShort(int fc)
{
    switch (fc)
    {
    case 1: return QStringLiteral("01 线圈");
    case 2: return QStringLiteral("02 离散输入");
    case 4: return QStringLiteral("04 输入寄存器");
    case 5: return QStringLiteral("05 写单线圈");
    case 6: return QStringLiteral("06 写单寄存器");
    case 15: return QStringLiteral("15 写多线圈");
    case 16: return QStringLiteral("16 写多寄存器");
    default: return QStringLiteral("03 保持寄存器");
    }
}
} // namespace

ModbusMasterTool::ModbusMasterTool(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("STModbusTools v%1")
                       .arg(QString::fromLatin1(MODBUSTOOLS_VERSION)));

    // ---- 连接管理层: 多连接注册表 + 初始连接 (上次使用参数, 未连接) ----
    m_conns = new ConnectionManager(this);
    connect(m_conns, &ConnectionManager::clientAdded, this,
            &ModbusMasterTool::onClientAdded);
    connect(m_conns, &ConnectionManager::stateChanged, this,
            &ModbusMasterTool::onConnStateChanged);
    connect(m_conns, &ConnectionManager::activeChanged, this,
            &ModbusMasterTool::onActiveConnectionChanged);
    m_conns->addConnection(AppSettings::mbToolConn());

    // ---- MDI 区 + 左侧设备树 (QSplitter) ----
    m_mdi = new QMdiArea;
    m_mdi->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_mdi->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    connect(m_mdi, &QMdiArea::subWindowActivated, this,
            [this](QMdiSubWindow *) { syncDisplayActions(); });
    m_tree = new DeviceTreeWidget(m_conns);
    connect(m_tree, &DeviceTreeWidget::activePickRequested, this,
            &ModbusMasterTool::pickActiveConnection);
    connect(m_tree, &DeviceTreeWidget::connectRequested, this,
            &ModbusMasterTool::connectConnection);
    connect(m_tree, &DeviceTreeWidget::disconnectRequested, this,
            &ModbusMasterTool::disconnectConnection);
    connect(m_tree, &DeviceTreeWidget::editConnectionRequested, this,
            &ModbusMasterTool::editConnectionParams);
    connect(m_tree, &DeviceTreeWidget::removeConnectionRequested, this,
            &ModbusMasterTool::removeConnectionConfirmed);
    connect(m_tree, &DeviceTreeWidget::addSlavesRequested, this,
            &ModbusMasterTool::setupSlaves);
    connect(m_tree, &DeviceTreeWidget::removeSlaveRequested, this,
            &ModbusMasterTool::removeSlave);
    connect(m_tree, &DeviceTreeWidget::slaveActivated, this,
            &ModbusMasterTool::onSlaveActivated);
    connect(m_tree, &DeviceTreeWidget::addSerialRequested, this,
            &ModbusMasterTool::addSerialConnection);
    connect(m_tree, &DeviceTreeWidget::addTcpRequested, this,
            &ModbusMasterTool::addTcpConnection);
    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->addWidget(m_tree);
    m_splitter->addWidget(m_mdi);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({260, 900});
    setCentralWidget(m_splitter);

    buildMenus();
    buildToolBar();
    buildStatusBar();
    updateConnectUi(); // 初始按钮可用态: 未连接 → 仅[连接]可用, 其余置灰

    // 首个数据窗口 (窗口 1, 对齐 Modbus Poll 启动即有一个数据窗口)
    PollDefinition d;
    d.name = tr("窗口 %1").arg(++m_naming);
    d.connectionId = m_conns->activeId();
    createDataWindow(d, false);
}

ModbusMasterTool::~ModbusMasterTool()
{
    // 连接由 ConnectionManager (QObject 父子) 析构断开;
    // 子窗口销毁会触发 removeDataWindow — 先断开避免析构中改容器
    for (QMdiSubWindow *sub : m_subs)
        disconnect(sub, &QObject::destroyed, this, nullptr);
}

void ModbusMasterTool::buildMenus()
{
    // ---- 文件 ----
    QMenu *mFile = menuBar()->addMenu(tr("文件(&F)"));
    aNewWindow = mFile->addAction(tr("新建窗口"), this, &ModbusMasterTool::newWindow);
    aNewWindow->setObjectName(QStringLiteral("act.newWindow"));
    aNewWindow->setShortcut(QKeySequence::New);
    aNewWindow->setIcon(Icons::newWindow());
    QAction *aWsOpen = mFile->addAction(tr("打开工作区…"), this, &ModbusMasterTool::openWorkspace);
    aWsOpen->setShortcut(QKeySequence::Open);
    aWsOpen->setObjectName(QStringLiteral("act.wsOpen"));
    QAction *aWsSave = mFile->addAction(tr("保存工作区…"), this, &ModbusMasterTool::saveWorkspace);
    aWsSave->setShortcut(QKeySequence::Save);
    aWsSave->setObjectName(QStringLiteral("act.wsSave"));
    mFile->addSeparator();
    mFile->addAction(tr("退出"), this, &QWidget::close);

    // ---- 设置 ----
    QMenu *mSetup = menuBar()->addMenu(tr("设置(&S)"));
    aF8 = mSetup->addAction(tr("读/写定义…"), this, &ModbusMasterTool::editDefinition);
    aF8->setObjectName(QStringLiteral("act.f8"));
    aF8->setShortcut(Qt::Key_F8);
    aF8->setIcon(Icons::definition());

    // ---- 显示 ----
    QMenu *mDisplay = menuBar()->addMenu(tr("显示(&D)"));
    QMenu *sub16 = mDisplay->addMenu(tr("16 位"));
    QMenu *subWide[2] = {mDisplay->addMenu(tr(kWideGroups[0])),
                         mDisplay->addMenu(tr(kWideGroups[1]))};
    auto *fmtGrp = new QActionGroup(this);
    fmtGrp->setExclusive(true);
    auto addFmt = [this, fmtGrp](QMenu *menu, ModbusFormat::Fmt f,
                                 ModbusFormat::ByteOrder o, const QString &text) {
        QAction *a = menu->addAction(text);
        a->setCheckable(true);
        a->setData(int(f) * 8 + int(o));
        fmtGrp->addAction(a);
        m_fmtActions.append(a);
        connect(a, &QAction::triggered, this, [this, f, o]() {
            // 作用域: 活动窗口有选中行 → 行级覆盖; 无选中 → 窗口级默认
            if (DataViewWidget *w = activeData())
                w->applyDisplayFormat(f, o);
        });
    };
    for (const FmtEntry &e : kFmtMenu)
        addFmt(sub16, e.fmt, e.order, tr(e.text));
    for (int g = 0; g < 2; ++g)
        for (int k = 0; k < 3; ++k)
            for (int oi = 0; oi < 4; ++oi)
                addFmt(subWide[g], kWideFmts[g][k], kOrders[oi],
                       tr("%1 (%2)").arg(tr(kWideNames[g][k]), tr(kOrderNames[oi])));

    mDisplay->addSeparator();
    mDisplay->addAction(tr("别名单元格…"), this, &ModbusMasterTool::editAlias)
        ->setObjectName(QStringLiteral("act.alias"));
    mDisplay->addAction(tr("值名映射…"), this, &ModbusMasterTool::editValueNames)
        ->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    mDisplay->addAction(tr("缩放与前缀/后缀…"), this, &ModbusMasterTool::editScale)
        ->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    mDisplay->addAction(tr("条件变色…"), this, &ModbusMasterTool::editRules)
        ->setShortcut(QKeySequence(QStringLiteral("Alt+Shift+C")));
    QAction *aCellLook = mDisplay->addAction(tr("单元格样式…"), this,
                                             &ModbusMasterTool::editCellLook);
    aCellLook->setObjectName(QStringLiteral("act.cellLook"));
    aCellLook->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+L")));
    aCellLook->setToolTip(
        tr("字体大小/粗斜下划线、行高、背景色 (选中行 = 行级覆盖, "
           "无选中 = 窗口级默认) 与列宽、行头宽"));
    aClearStyles = mDisplay->addAction(tr("清除单元格显示设置"), this, [this] {
        if (DataViewWidget *w = activeData())
            w->clearCellStyles();
    });
    aClearStyles->setObjectName(QStringLiteral("act.clearStyles"));
    aClearStyles->setToolTip(tr("清除选中行 (无选中 = 全部行) 的行级显示覆盖,"
                                " 恢复窗口级默认"));
    mDisplay->addSeparator();
    aHideNames = mDisplay->addAction(tr("隐藏别名列"));
    aHideNames->setObjectName(QStringLiteral("act.hideNames"));
    aHideNames->setCheckable(true);
    connect(aHideNames, &QAction::toggled, this, &ModbusMasterTool::setHideNames);
    aPlcBase1 = mDisplay->addAction(tr("PLC 地址 (Base 1)"));
    aPlcBase1->setObjectName(QStringLiteral("act.plcBase1"));
    aPlcBase1->setCheckable(true);
    connect(aPlcBase1, &QAction::toggled, this, &ModbusMasterTool::setPlcBase1);
    aAddrInCell = mDisplay->addAction(tr("单元格内显示地址"));
    aAddrInCell->setObjectName(QStringLiteral("act.addrInCell"));
    aAddrInCell->setCheckable(true);
    connect(aAddrInCell, &QAction::toggled, this, &ModbusMasterTool::setAddrInCell);
    mDisplay->addSeparator();
    aDevTree = mDisplay->addAction(tr("设备树"));
    aDevTree->setObjectName(QStringLiteral("act.devTree"));
    aDevTree->setCheckable(true);
    aDevTree->setChecked(true);
    aDevTree->setToolTip(tr("左侧设备树 (多连接/多从机监控面板)"));
    connect(aDevTree, &QAction::toggled, this, &ModbusMasterTool::toggleDeviceTree);
    mDisplay->addSeparator();
    mDisplay->addAction(tr("通信显示…"), this, &ModbusMasterTool::showFrameLog);
    mDisplay->addAction(tr("记录…"), this, [this] {
        if (DataViewWidget *w = activeData())
            w->toggleLogging();
    });
    connect(mDisplay, &QMenu::aboutToShow, this,
            &ModbusMasterTool::syncDisplayActions); // 打开菜单时刷新作用域勾选态

    // ---- 功能 ----
    QMenu *mFunc = menuBar()->addMenu(tr("功能(&U)"));
    mFunc->addAction(tr("Test Center"), this, &ModbusMasterTool::showTestCenter)
        ->setShortcut(QKeySequence(QStringLiteral("Alt+T")));
    mFunc->addAction(tr("扫描 (从站 / 地址 / 串口参数 / TCP 发现)…"), this,
                     &ModbusMasterTool::showScan)
        ->setShortcut(QKeySequence(QStringLiteral("Alt+A")));
    mFunc->addSeparator();
    mFunc->addAction(tr("导出 CSV…"), this, &ModbusMasterTool::exportCsv);

    // ---- 连接 ----
    QMenu *mConn = menuBar()->addMenu(tr("连接(&C)"));
    aAddSerial = mConn->addAction(tr("添加串口连接…"), this,
                                  &ModbusMasterTool::addSerialConnection);
    aAddSerial->setObjectName(QStringLiteral("act.addSerial"));
    aAddTcp = mConn->addAction(tr("添加 TCP 连接…"), this,
                               &ModbusMasterTool::addTcpConnection);
    aAddTcp->setObjectName(QStringLiteral("act.addTcp"));
    mConn->addSeparator();
    mConn->addAction(tr("连接设置 (活动连接)…"), this, &ModbusMasterTool::showConnDialog)
        ->setShortcut(Qt::Key_F3);
    mConn->addSeparator();
    aConnect = mConn->addAction(tr("连接"), this, &ModbusMasterTool::connectDevice);
    aConnect->setObjectName(QStringLiteral("act.connect"));
    aConnect->setIcon(Icons::connect());
    aConnect->setToolTip(tr("连接设备 (未连接时可用; 作用于设备树选中的活动连接)"));
    aDisconnect = mConn->addAction(tr("断开"), this, &ModbusMasterTool::disconnectDevice);
    aDisconnect->setObjectName(QStringLiteral("act.disconnect"));
    aDisconnect->setIcon(Icons::disconnect());
    aDisconnect->setToolTip(tr("断开连接 (连接在途时作取消连接)"));
    mConn->addSeparator();
    aF6 = mConn->addAction(tr("读/写一次"), this, &ModbusMasterTool::readWriteOnce);
    aF6->setObjectName(QStringLiteral("act.f6"));
    aF6->setShortcut(Qt::Key_F6);
    aF6->setIcon(Icons::readOnce());
    aF6->setToolTip(tr("读/写一次 (已连接时可用)"));
    aPause = mConn->addAction(tr("扫描暂停"));
    aPause->setObjectName(QStringLiteral("act.pause"));
    aPause->setCheckable(true);
    aPause->setIcon(Icons::pause());
    aPause->setToolTip(tr("扫描暂停 (已连接时可用)"));
    connect(aPause, &QAction::toggled, this, &ModbusMasterTool::togglePause);

    // ---- 窗口 ----
    QMenu *mWin = menuBar()->addMenu(tr("窗口(&W)"));
    mWin->addAction(tr("层叠"), this, &ModbusMasterTool::cascadeWindows);
    mWin->addAction(tr("平铺"), this, &ModbusMasterTool::tileWindows);
    mWin->addAction(tr("关闭全部数据窗口"), this, &ModbusMasterTool::closeDataWindows);
    connect(mWin, &QMenu::aboutToShow, this, [this, mWin]() {
        // 动态窗口列表 rebuild: 按 m_winMenuDyn 精确回收上次动态项 —
        // 不按下标推算 (曾因 at(4) 越界读出野指针: 崩溃或死循环卡死)
        for (int i = m_winMenuDyn.size() - 1; i >= 0; --i)
        {
            mWin->removeAction(m_winMenuDyn.at(i));
            delete m_winMenuDyn.at(i);
        }
        m_winMenuDyn.clear();
        const QList<QMdiSubWindow *> subs = m_mdi->subWindowList();
        for (int i = 0; i < subs.size(); ++i)
        {
            QAction *a = mWin->addAction(
                QStringLiteral("&%1 %2").arg(i + 1).arg(subs.at(i)->windowTitle()));
            a->setCheckable(true);
            a->setChecked(subs.at(i) == m_mdi->activeSubWindow());
            connect(a, &QAction::triggered, this,
                    [this, sub = subs.at(i)]() { sub->show();
                                                 m_mdi->setActiveSubWindow(sub); });
            m_winMenuDyn.append(a);
        }
    });

    // ---- 帮助 ----
    QMenu *mHelp = menuBar()->addMenu(tr("帮助(&H)"));
    mHelp->addAction(tr("关于 STModbusTools…"), this, &ModbusMasterTool::showAbout);
}

void ModbusMasterTool::buildToolBar()
{
    QToolBar *tb = addToolBar(tr("主工具栏"));
    tb->setObjectName(QStringLiteral("master.toolbar"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tb->addAction(aNewWindow);
    tb->addAction(aF8);
    tb->addSeparator();
    tb->addAction(aConnect);
    tb->addAction(aDisconnect);
    tb->addSeparator();
    // 扫描紧跟 连接/断开 (连接域动作), 竖线单独隔开
    tb->addAction(Icons::refresh(), tr("扫描"), this, &ModbusMasterTool::showScan);
    tb->addSeparator();
    tb->addAction(aF6);
    tb->addAction(aPause);
    tb->addSeparator();
    QAction *tcAct = tb->addAction(Icons::book(), tr("Test Center"), this,
                                   &ModbusMasterTool::showTestCenter);
    Q_UNUSED(tcAct);
    tb->addAction(Icons::database(), tr("帧日志"), this, &ModbusMasterTool::showFrameLog);
}

void ModbusMasterTool::buildStatusBar()
{
    m_msgPane = new QLabel(tr("未连接 — 按 F3 配置连接"));
    m_msgPane->setObjectName(QStringLiteral("master.msgPane"));
    statusBar()->addWidget(m_msgPane, 1);
    m_txPane = new QLabel(tr("Tx: 0"));
    m_txPane->setObjectName(QStringLiteral("master.txPane"));
    m_errPane = new QLabel(tr("Err: 0"));
    m_errPane->setObjectName(QStringLiteral("master.errPane"));
    statusBar()->addPermanentWidget(m_txPane);
    statusBar()->addPermanentWidget(m_errPane);
    // 右下角连接参数窗格: 连接成功后显示 (串口: COM3 · 115200 8N1 / TCP: IP:端口)
    m_connPane = new QLabel;
    m_connPane->setObjectName(QStringLiteral("master.connPane"));
    m_connPane->hide();
    statusBar()->addPermanentWidget(m_connPane);
}

// ============================================================================
// 数据窗口管理
// ============================================================================
DataViewWidget *ModbusMasterTool::createDataWindow(const PollDefinition &def, bool tile)
{
    PollDefinition d = def;
    // 绑定所属连接: 显式 id → 该连接; 空/失效 id → 活动连接 (旧工作区兼容)
    MasterClient *c = m_conns->client(d.connectionId);
    if (!c)
    {
        if (m_conns->count() == 0)
        {
            m_msgPane->setText(tr("无连接 — 请先添加连接 (连接菜单或设备树)"));
            m_msgPane->setStyleSheet(QStringLiteral("color:#d97706;"));
            return nullptr;
        }
        if (m_conns->activeId().isEmpty())
            d.connectionId = m_conns->ids().first();
        else
            d.connectionId = m_conns->activeId();
        c = m_conns->client(d.connectionId);
    }
    const int idx = m_defs.size();
    m_defs.append(d);
    auto *w = new DataViewWidget(c); // 连接态由窗口自订阅 (connectionChanged)
    w->setDefinition(d);
    m_windows.append(w);
    auto *sub = new QMdiSubWindow;
    sub->setAttribute(Qt::WA_DeleteOnClose); // 数据窗口关闭 = 删除定义
    sub->setWidget(w);
    sub->setObjectName(QStringLiteral("master.dataSub"));
    m_subs.append(sub);
    m_mdi->addSubWindow(sub);
    syncWindowTitle(idx);
    connect(w, &DataViewWidget::statusMessage, this,
            [this](const QString &msg, bool ok) {
                m_msgPane->setText(msg);
                m_msgPane->setStyleSheet(
                    QStringLiteral("color:%1;").arg(ok ? QStringLiteral("#16a34a")
                                                       : QStringLiteral("#dc2626")));
            });
    connect(w, &DataViewWidget::definitionAutoChanged, this,
            [this, w](const PollDefinition &pd) {
                const int i = m_windows.indexOf(w);
                if (i >= 0)
                {
                    m_defs[i] = pd;
                    syncWindowTitle(i);
                }
            });
    connect(sub, &QObject::destroyed, this,
            [this](QObject *obj) { removeDataWindow(obj); });
    sub->show();
    m_mdi->setActiveSubWindow(sub); // 新窗口即活动窗口
    if (tile)
        m_mdi->tileSubWindows();
    syncDisplayActions();
    return w;
}

void ModbusMasterTool::removeDataWindow(QObject *obj)
{
    // QMdiSubWindow 销毁 (WA_DeleteOnClose) → 删除平行定义
    for (int i = 0; i < m_subs.size(); ++i)
    {
        if (m_subs[i] == obj)
        {
            m_subs.removeAt(i);
            m_defs.removeAt(i);
            m_windows.removeAt(i);
            return;
        }
    }
}

void ModbusMasterTool::syncWindowTitle(int idx)
{
    if (idx < 0 || idx >= m_subs.size())
        return;
    const PollDefinition &d = m_defs[idx];
    // 多段: 首段地址×数量 + 剩余段数
    const QString range =
        d.segs.size() >= 2 && !d.isWriteFc()
            ? tr("%1×%2 +%3 段").arg(d.addr).arg(d.qty).arg(d.segs.size() - 1)
            : tr("%1 ×%2").arg(d.addr).arg(d.qty);
    // 所属连接标签 (COM3 / 192.168.1.10:502): 多连接时标题即见窗口挂在哪台设备;
    // id 失效 (连接已删/旧数据) 时省略, 不显示 [?]
    const QString conn = m_conns->label(d.connectionId);
    const QString connTag = conn.isEmpty() ? QString()
                                           : QStringLiteral("[%1] ").arg(conn);
    m_subs[idx]->setWindowTitle(tr("窗口 %1 — %2从机 %3 · %4 @ %5%6")
                                    .arg(idx + 1)
                                    .arg(connTag)
                                    .arg(d.slave)
                                    .arg(fcShort(d.fc))
                                    .arg(range)
                                    .arg(d.enabled ? QString()
                                                   : QStringLiteral(" [禁用]")));
    m_subs[idx]->setToolTip(d.name);
}

DataViewWidget *ModbusMasterTool::activeData() const
{
    QMdiSubWindow *sub = m_mdi ? m_mdi->activeSubWindow() : nullptr;
    if (!sub)
        return nullptr;
    auto *w = qobject_cast<DataViewWidget *>(sub->widget());
    return w;
}

int ModbusMasterTool::activeIndex() const
{
    QMdiSubWindow *sub = m_mdi ? m_mdi->activeSubWindow() : nullptr;
    const int i = m_subs.indexOf(sub);
    return i;
}

void ModbusMasterTool::syncDisplayActions()
{
    if (!aF8)
        return; // 菜单未建 (ctor 早期 addSubWindow 触发 subWindowActivated)
    DataViewWidget *w = activeData();
    const bool has = w != nullptr;
    const MasterClient *ac = m_conns->activeClient();
    aF8->setEnabled(has);
    aF6->setEnabled(has && ac && ac->isConnected()); // 活动窗口 + 连接态 双重门控
    aHideNames->setEnabled(has);
    aPlcBase1->setEnabled(has);
    aAddrInCell->setEnabled(has);
    aClearStyles->setEnabled(has);
    for (QAction *a : m_fmtActions)
        a->setEnabled(has);
    if (!has)
        return;
    const PollDefinition &d = w->definition();
    aHideNames->setChecked(d.hideNames);
    aPlcBase1->setChecked(d.plcBase1);
    aAddrInCell->setChecked(d.addrInCell);
    // 选中行存在 = 行级作用域 (各行格式可能互异) → 不显示勾选; 否则勾窗口级
    const bool scoped = w->hasRowSelection();
    const int key = int(d.fmt) * 8 + int(d.order);
    for (QAction *a : m_fmtActions)
    {
        QSignalBlocker block(a);
        a->setChecked(!scoped && a->data().toInt() == key);
    }
}

// ============================================================================
// 窗口/定义动作
// ============================================================================
void ModbusMasterTool::newWindow()
{
    PollDefinition d;
    d.name = tr("窗口 %1").arg(++m_naming);
    d.connectionId = m_conns->activeId();
    // 设备树选中了从机 → 从机地址预填
    const int picked = m_tree->selectedSlaveAddress();
    if (picked > 0)
        d.slave = picked;
    createDataWindow(d, true);
}

void ModbusMasterTool::editDefinition()
{
    DataViewWidget *w = activeData();
    const int idx = activeIndex();
    if (!w || idx < 0)
        return;
    DefDialog::DefDialog dlg(this, m_defs[idx]);
    QString err;
    if (dlg.exec() != QDialog::Accepted)
        return;
    if (!dlg.apply(&err))
    {
        QMessageBox::warning(this, tr("读/写定义"), err);
        return;
    }
    const PollDefinition &d = m_defs[idx];
    w->setDefinition(d);
    syncWindowTitle(idx);
    syncDisplayActions();
    if (d.isWriteFc())
    {
        // 写窗口警示 (一次性): 配置后将按扫描率周期性改写设备数据
        static bool warned = false;
        if (!warned)
        {
            warned = true;
            QMessageBox::information(
                this, tr("写窗口"),
                tr("该窗口已定义为写功能 (%1)。\n启用扫描后, 网格数据将按扫描率"
                   "周期性写入设备 (速率 0 或禁用时按 F6 写一次)。\n请确认从站与"
                   "地址无误。").arg(fcShort(d.fc)));
        }
    }
}

void ModbusMasterTool::showConnDialog()
{
    // F3 = 编辑活动连接参数; 无连接 = 直接进入新建流程 (确定即连)
    if (m_conns->count() == 0)
    {
        addConnectionWithDialog(AppSettings::mbToolConn().kind);
        return;
    }
    editConnectionParams(m_conns->activeId());
}

void ModbusMasterTool::addSerialConnection() { addConnectionWithDialog(0); }

void ModbusMasterTool::addTcpConnection() { addConnectionWithDialog(1); }

void ModbusMasterTool::addConnectionWithDialog(int kind)
{
    // 初值: 活动连接参数 (无连接 = 上次使用参数), 强制为菜单指定的类型
    AppSettings::MbToolConn p = m_conns->count() ? m_conns->activeEntry().params
                                                 : AppSettings::mbToolConn();
    p.kind = qBound(0, kind, 1);
    if (!ConnDialog::edit(this, p))
        return;
    const QString id = m_conns->addConnection(p);
    if (id.isEmpty())
    {
        QMessageBox::warning(this, tr("添加连接"),
                             tr("串口 %1 已被另一个连接使用 (串口为独占资源)。")
                                 .arg(p.portName));
        return;
    }
    m_conns->setActiveId(id);
    mirrorActiveToSettings();
    // 添加即连接: 对齐"连上一个串口 = 多一台设备"的使用心智
    connectConnection(id);
}

void ModbusMasterTool::editConnectionParams(const QString &id)
{
    if (!m_conns->contains(id))
        return;
    if (m_scanDlg && m_scanDlg->isBusy() && id == m_scanConnId)
    {
        m_msgPane->setText(tr("扫描进行中 — 无法编辑该连接参数"));
        m_msgPane->setStyleSheet(QStringLiteral("color:#d97706;"));
        return;
    }
    AppSettings::MbToolConn p = m_conns->entry(id).params;
    if (!ConnDialog::edit(this, p))
        return;
    if (!m_conns->updateConnection(id, p))
    {
        QMessageBox::warning(this, tr("连接设置"),
                             tr("串口 %1 已被另一个连接使用 (串口为独占资源)。")
                                 .arg(p.portName));
        return;
    }
    // 连接标签可能变化 (如改 TCP 主机): 全部窗口标题重同步
    for (int i = 0; i < m_subs.size(); ++i)
        syncWindowTitle(i);
    if (id == m_conns->activeId())
        mirrorActiveToSettings();
    const bool on = m_conns->client(id)->isConnected();
    if (id == m_conns->activeId())
    {
        updateConnPane(on);
        if (on)
        {
            m_msgPane->setText(tr("连接参数已更新 — 断开后重连生效"));
            m_msgPane->setStyleSheet(QStringLiteral("color:#d97706;"));
        }
        else
        {
            m_msgPane->setText(tr("连接参数已保存"));
            m_msgPane->setStyleSheet(QStringLiteral("color:#16a34a;"));
        }
    }
}

void ModbusMasterTool::removeConnectionConfirmed(const QString &id)
{
    if (!m_conns->contains(id))
        return;
    if (m_scanDlg && m_scanDlg->isBusy())
    {
        m_msgPane->setText(tr("扫描进行中 — 无法移除连接"));
        m_msgPane->setStyleSheet(QStringLiteral("color:#d97706;"));
        return;
    }
    if (id == m_connectingId)
    {
        // 连接尝试在途时移除会让在途标志悬挂 (按钮永久置灰): 先取消再移除
        disconnectConnection(id);
        m_connectingId.clear();
        closeConnDlg();
    }
    if (QMessageBox::question(
            this, tr("移除连接"),
            tr("移除连接 %1?\n绑定它的数据窗口将一并关闭。").arg(m_conns->label(id)))
        != QMessageBox::Yes)
        return;
    // 先关闭绑定窗口 (其 client 指针随连接销毁失效)
    for (int i = m_subs.size() - 1; i >= 0; --i)
    {
        if (m_defs.at(i).connectionId == id)
            m_subs.at(i)->close();
    }
    m_conns->removeConnection(id); // 活动被移除 → manager 移交剩余首个
}

void ModbusMasterTool::setupSlaves(const QString &id)
{
    if (m_conns->entry(id).params.kind != 0)
        return; // 多从机仅串口 (TCP 一连接一设备)
    QList<int> addrs = m_conns->slaves(id);
    if (!SlaveSetupDialog::edit(this, &addrs))
        return;
    m_conns->setSlaves(id, addrs);
    m_msgPane->setText(tr("从机列表已更新 (%1 台, 依地址轮询探测)").arg(addrs.size()));
    m_msgPane->setStyleSheet(QStringLiteral("color:#16a34a;"));
}

void ModbusMasterTool::removeSlave(const QString &id, int address)
{
    QList<int> addrs = m_conns->slaves(id);
    if (!addrs.removeOne(address))
        return;
    m_conns->setSlaves(id, addrs);
}

void ModbusMasterTool::pickActiveConnection(const QString &id)
{
    if (m_scanDlg && m_scanDlg->isBusy())
    {
        m_msgPane->setText(tr("扫描进行中 — 暂不能切换活动连接"));
        m_msgPane->setStyleSheet(QStringLiteral("color:#d97706;"));
        return;
    }
    m_conns->setActiveId(id);
}

void ModbusMasterTool::onSlaveActivated(const QString &id, int slave)
{
    // 双击从机 = 以该连接+从机新建数据窗口 (从机地址预填)
    PollDefinition d;
    d.name = tr("窗口 %1").arg(++m_naming);
    d.slave = qBound(1, slave, 247);
    d.connectionId = id;
    createDataWindow(d, true);
}

void ModbusMasterTool::onClientAdded(MasterClient *c)
{
    // Tx/Err 汇总全部连接; 帧日志在对话框存在时接入 (UniqueConnection 防重复挂)
    connect(c, &MasterClient::txnDone, this, &ModbusMasterTool::onTxn);
    if (m_frameLog)
        connect(c, &MasterClient::frameLogged, m_frameLog, &FrameLogDialog::appendFrame,
                Qt::UniqueConnection);
}

void ModbusMasterTool::pauseForConnection(const QString &id, bool on)
{
    if (id.isEmpty())
        return;
    for (int i = 0; i < m_windows.size(); ++i)
    {
        if (m_defs.at(i).connectionId == id)
            m_windows.at(i)->setPollPaused(on);
    }
    if (auto *p = m_conns->poller(id))
        p->setPaused(on);
}

void ModbusMasterTool::mirrorActiveToSettings()
{
    const ConnectionManager::Entry e = m_conns->activeEntry();
    if (!e.id.isEmpty())
        AppSettings::setMbToolConn(e.params);
}

void ModbusMasterTool::connectConnection(const QString &id)
{
    MasterClient *c = m_conns->client(id);
    if (!c || c->isConnected() || !m_connectingId.isEmpty())
        return; // 在途防重复触发 (同一时刻仅一个连接尝试)
    if (m_scanDlg && m_scanDlg->isBusy() && id == m_scanConnId)
    {
        m_msgPane->setText(tr("扫描进行中 — 该连接正被重定向, 暂不能连接/断开"));
        m_msgPane->setStyleSheet(QStringLiteral("color:#d97706;"));
        return;
    }
    AppSettings::MbToolConn conn = m_conns->entry(id).params;
    // 首次使用/未选串口: 引导先完成连接设置 (否则点连接会"无声失败")
    if (conn.kind == 0 && conn.portName.isEmpty())
    {
        m_msgPane->setText(tr("尚未选择串口 — 请在连接设置中选择端口"));
        m_msgPane->setStyleSheet(QStringLiteral("color:#d97706;"));
        editConnectionParams(id);
        conn = m_conns->entry(id).params;
        if (conn.kind == 0 && conn.portName.isEmpty())
            return; // 用户取消或仍未选端口
    }
    m_connectingId = id;
    updateConnectUi(); // 连接在途: [连接]/[断开]均置灰, 防重复触发
    const QString target = m_conns->label(id);
    if (id == m_conns->activeId())
    {
        m_msgPane->setText(tr("连接中… %1").arg(target));
        m_msgPane->setStyleSheet(QStringLiteral("color:#64748b;"));
    }
    // 连接中窗体: 忙碌指示 + [取消连接] (TCP 目标不可达时尝试可悬挂,
    // 窗体让用户明确知道当前状态, 取消即中止在途尝试)
    ensureConnDlg();
    m_connDlg->setLabelText(tr("连接中…\n%1").arg(target));
    m_connDlg->show();
    if (!c->connectDevice())
    {
        // 同步失败 (串口占用等); 异步失败走 onConnStateChanged
        if (m_connectingId == id)
        {
            m_connectingId.clear();
            closeConnDlg(); // 先关窗体再弹警告框, 避免两层模态叠置
            updateConnectUi();
        }
        const QString err = c->lastError();
        if (id == m_conns->activeId())
        {
            m_msgPane->setText(tr("连接失败: %1").arg(err));
            m_msgPane->setStyleSheet(QStringLiteral("color:#dc2626;"));
        }
        QMessageBox::warning(this, tr("连接失败"),
                             tr("无法打开 %1:\n%2\n\n请检查连接参数 "
                                "(端口是否被其他程序占用)。")
                                 .arg(target, err));
    }
}

void ModbusMasterTool::disconnectConnection(const QString &id)
{
    if (m_scanDlg && m_scanDlg->isBusy() && id == m_scanConnId)
        return; // 扫描重定向中的连接不可断开 (状态机收尾自会恢复)
    // 连接在途时兼作"取消连接": 先复位在途标志, 随后的
    // stateChanged(false) 走「已取消」分支而非「连接失败」
    if (m_connectingId == id)
    {
        m_cancelConn = true;
        m_connectingId.clear();
    }
    m_conns->disconnectOne(id);
    updateConnectUi();
}

void ModbusMasterTool::connectDevice()
{
    if (m_conns->count() == 0)
    {
        addConnectionWithDialog(AppSettings::mbToolConn().kind);
        return;
    }
    connectConnection(m_conns->activeId());
}

void ModbusMasterTool::disconnectDevice()
{
    if (m_conns->activeId().isEmpty())
        return;
    disconnectConnection(m_conns->activeId());
}

void ModbusMasterTool::onConnStateChanged(const QString &id, bool on)
{
    // 先结算在途标志再刷 UI: 异步失败到达时 m_connectingId 仍指向该连接,
    // 若先 updateConnectUi 会把 [连接] 一并置灰且此后无事件补救 (回归点)
    const bool wasConnecting = (m_connectingId == id);
    bool canceled = false;
    if (wasConnecting)
    {
        canceled = m_cancelConn;
        m_connectingId.clear();
        m_cancelConn = false;
        closeConnDlg(); // 连接中窗体随结果关闭 (成功/失败/取消)
    }
    if (id != m_conns->activeId())
    {
        updateConnectUi(); // 在途状态可能变化
        return; // 非活动连接: 设备树状态点已跟进, 状态栏只报活动连接
    }
    const MasterClient *c = m_conns->client(id);
    updateConnectUi();
    updateConnPane(on); // 右下角连接参数: 连接成功显示, 断开隐藏
    if (m_tc)
        m_tc->setConnected(on, c && c->kind() == MasterClient::Kind::Tcp);
    if (on)
    {
        m_msgPane->setText(
            m_conns->entry(id).params.kind == 1
                ? tr("Modbus/TCP 已连接 — %1").arg(m_conns->label(id))
                : tr("Modbus RTU 已连接 — %1").arg(m_conns->label(id)));
        m_msgPane->setStyleSheet(QStringLiteral("color:#16a34a;"));
    }
    else if (canceled)
    {
        m_msgPane->setText(tr("已取消连接"));
        m_msgPane->setStyleSheet(QStringLiteral("color:#dc2626;"));
    }
    else if (wasConnecting)
    {
        // 连接尝试的异步失败 (TCP 拒绝/串口被占用等): 给出显式原因
        m_msgPane->setText(tr("连接失败: %1").arg(c ? c->lastError() : QString()));
        m_msgPane->setStyleSheet(QStringLiteral("color:#dc2626;"));
    }
    else
    {
        m_msgPane->setText(tr("已断开"));
        m_msgPane->setStyleSheet(QStringLiteral("color:#dc2626;"));
    }
}

void ModbusMasterTool::onActiveConnectionChanged(const QString &id)
{
    if (!aConnect || !m_msgPane)
        return; // ctor 早期 (addConnection 首个活动, 菜单/状态栏未建)
    // 活动切换: 状态栏/按钮跟随, Test Center 与扫描重指向新 client
    updateConnectUi();
    const MasterClient *c = m_conns->client(id);
    const bool on = c && c->isConnected();
    updateConnPane(on);
    if (m_tc)
        m_tc->setClient(m_conns->client(id));
    if (m_scanDlg)
        m_scanDlg->setClient(m_conns->client(id));
    if (!id.isEmpty())
    {
        m_msgPane->setText(tr("活动连接: %1").arg(m_conns->label(id)));
        m_msgPane->setStyleSheet(QStringLiteral("color:#64748b;"));
    }
}

void ModbusMasterTool::updateConnectUi()
{
    if (!aConnect || !m_msgPane)
        return; // ctor 早期 (addConnection 触发 activeChanged 时菜单未建)
    const MasterClient *c = m_conns->activeClient();
    const bool has = c != nullptr;
    const bool on = has && c->isConnected();
    const bool connecting = !m_connectingId.isEmpty();
    aConnect->setEnabled(has && !on && !connecting); // 在途防重复触发 (全局单在途)
    // 连接在途时 [断开] 兼作取消通道: TCP 目标不可达时尝试可悬挂数秒,
    // 不能与 [连接] 同时置灰把用户锁死
    aDisconnect->setEnabled(on || (connecting && m_connectingId == m_conns->activeId()));
    aF6->setEnabled(on && m_mdi->activeSubWindow() != nullptr); // 连接态 + 有活动窗口
    aPause->setEnabled(on); // 扫描仅在连接态运行
}

void ModbusMasterTool::ensureConnDlg()
{
    if (m_connDlg)
        return;
    // 0..0 = 忙碌指示 (无百分比); 手动 show/close, 不用 exec 阻塞
    m_connDlg = new QProgressDialog(QString(), tr("取消连接"), 0, 0, this);
    m_connDlg->setObjectName(QStringLiteral("master.connDlg"));
    m_connDlg->setWindowTitle(tr("连接"));
    m_connDlg->setWindowModality(Qt::WindowModal);
    m_connDlg->setAutoClose(false);
    m_connDlg->setAutoReset(false);
    m_connDlg->setMinimumDuration(0);
    connect(m_connDlg, &QProgressDialog::canceled, this, [this]() {
        if (m_connectingId.isEmpty())
            return; // 成功/失败路径已自行关闭窗体
        m_cancelConn = true;
        m_msgPane->setText(tr("正在取消连接…"));
        m_msgPane->setStyleSheet(QStringLiteral("color:#64748b;"));
        disconnectConnection(m_connectingId); // 复位在途标志 + 中止在途尝试
    });
}

void ModbusMasterTool::closeConnDlg()
{
    if (m_connDlg)
        m_connDlg->hide();
}

void ModbusMasterTool::updateConnPane(bool on)
{
    if (!m_connPane)
        return; // ctor 早期
    const ConnectionManager::Entry e = m_conns->activeEntry();
    if (e.id.isEmpty() || !on)
    {
        m_connPane->hide();
        return;
    }
    if (e.params.kind == 1)
    {
        m_connPane->setText(tr("TCP %1:%2").arg(e.params.host).arg(e.params.tcpPort));
        m_connPane->setToolTip(tr("Modbus/TCP — 响应超时 %1 ms · 重试 %2")
                                   .arg(e.params.timeoutMs)
                                   .arg(e.params.retries));
    }
    else
    {
        // 串口参数一览: 端口 · 波特率 数据位校验停止位 (如 COM3 · 115200 8N1);
        // 数据位固定 8 (RTU 标准), 校验 0=无(N) 1=偶(E) 2=奇(O)
        const QString parity = e.params.parity == 1 ? QStringLiteral("E")
                               : e.params.parity == 2 ? QStringLiteral("O")
                                                      : QStringLiteral("N");
        m_connPane->setText(QStringLiteral("%1 · %2 8%3%4")
                                .arg(e.params.portName)
                                .arg(e.params.baud)
                                .arg(parity)
                                .arg(e.params.stopBits));
        m_connPane->setToolTip(tr("Modbus RTU — 响应超时 %1 ms · 重试 %2")
                                   .arg(e.params.timeoutMs)
                                   .arg(e.params.retries));
    }
    m_connPane->show();
}

void ModbusMasterTool::togglePause(bool on)
{
    // 全局暂停: 全部数据窗口 + 全部从机在线轮询
    for (DataViewWidget *w : m_windows)
        w->setPollPaused(on);
    const QStringList ids = m_conns->ids();
    for (const QString &id : ids)
        if (auto *p = m_conns->poller(id))
            p->setPaused(on);
    m_msgPane->setText(on ? tr("扫描已暂停") : tr("扫描已恢复"));
    m_msgPane->setStyleSheet(QStringLiteral("color:#64748b;"));
}

void ModbusMasterTool::toggleDeviceTree(bool on)
{
    m_tree->setVisible(on);
}

void ModbusMasterTool::readWriteOnce()
{
    if (DataViewWidget *w = activeData())
        w->readWriteOnce();
}

void ModbusMasterTool::onTxn(const TxnRecord &rec)
{
    ++m_txCount;
    if (rec.err != static_cast<int>(TxnErr::Ok)
        && rec.err != static_cast<int>(TxnErr::Dropped))
        ++m_errCount;
    updateStatsPanes();
}

void ModbusMasterTool::updateStatsPanes()
{
    m_txPane->setText(tr("Tx: %1").arg(m_txCount));
    m_errPane->setText(tr("Err: %1").arg(m_errCount));
}

// ============================================================================
// 显示菜单 (作用于活动数据窗口; 行级变化经 definitionAutoChanged 回同步)
// ============================================================================
void ModbusMasterTool::editScale()
{
    if (DataViewWidget *w = activeData())
        w->editScale();
}

void ModbusMasterTool::editRules()
{
    if (DataViewWidget *w = activeData())
        w->editRules();
}

void ModbusMasterTool::editAlias()
{
    if (DataViewWidget *w = activeData())
        w->editAlias();
}

void ModbusMasterTool::editValueNames()
{
    if (DataViewWidget *w = activeData())
        w->editValueNames();
}

void ModbusMasterTool::editCellLook()
{
    if (DataViewWidget *w = activeData())
        w->editCellLook();
}

void ModbusMasterTool::setHideNames(bool on)
{
    if (DataViewWidget *w = activeData())
    {
        PollDefinition d = w->definition();
        d.hideNames = on;
        w->setDefinition(d);
        m_defs[activeIndex()] = w->definition();
    }
}

void ModbusMasterTool::setPlcBase1(bool on)
{
    if (DataViewWidget *w = activeData())
    {
        PollDefinition d = w->definition();
        d.plcBase1 = on;
        w->setDefinition(d);
        m_defs[activeIndex()] = w->definition();
    }
}

void ModbusMasterTool::setAddrInCell(bool on)
{
    if (DataViewWidget *w = activeData())
    {
        PollDefinition d = w->definition();
        d.addrInCell = on;
        w->setDefinition(d);
        m_defs[activeIndex()] = w->definition();
    }
}

void ModbusMasterTool::exportCsv()
{
    if (DataViewWidget *w = activeData())
        w->exportTable();
}

// ============================================================================
// Test Center / 扫描 / 帧日志
// ============================================================================
void ModbusMasterTool::showTestCenter()
{
    if (!m_tc)
    {
        m_tc = new TestCenterPage(m_conns->activeClient());
        m_tcSub = new QMdiSubWindow;
        m_tcSub->setWidget(m_tc);
        m_tcSub->setAttribute(Qt::WA_DeleteOnClose, false); // 关闭仅隐藏 (单例)
        m_tcSub->setWindowTitle(tr("Test Center"));
        m_mdi->addSubWindow(m_tcSub);
        m_tcSub->resize(880, 560);
        if (const MasterClient *c = m_conns->activeClient())
            m_tc->setConnected(c->isConnected(), c->kind() == MasterClient::Kind::Tcp);
    }
    // QMdiSubWindow 关闭 (无 DeleteOnClose) 会对内容页显式 hide() 留下隐藏
    // 标志 — 只 show 子窗口时页面仍隐藏 (窗体空白), 重开必须一并恢复
    m_tcSub->show();
    m_tc->show();
    m_mdi->setActiveSubWindow(m_tcSub);
}

void ModbusMasterTool::showScan()
{
    // 从站/地址扫描页要求已连接 (页内开始时自检提示);
    // 串口参数扫描/TCP 发现页不需要先连上 — 本来就是要"找出该怎么连"
    if (!m_scanDlg)
    {
        m_scanDlg = new ScanDialog(m_conns->activeClient(), this);
        connect(m_scanDlg, &ScanDialog::slavePicked, this,
                [this](int slave) { applySlaveToActive(slave); });
        // 参数扫描/TCP 发现会重定向活动连接: 暂停该连接轮询, 结束恢复
        connect(m_scanDlg, &ScanDialog::scanBusyChanged, this,
                &ModbusMasterTool::onScanBusy);
        // 双击结果 → 回填连接参数 (保存设置) 并立即重连生效
        connect(m_scanDlg, &ScanDialog::serialPicked, this,
                &ModbusMasterTool::applySerialPick);
        connect(m_scanDlg, &ScanDialog::tcpPicked, this,
                &ModbusMasterTool::applyTcpPick);
    }
    else
    {
        // 单例复用: 活动连接可能已切换
        m_scanDlg->setClient(m_conns->activeClient());
    }
    m_scanDlg->show();
    m_scanDlg->raise();
    m_scanDlg->activateWindow();
}

void ModbusMasterTool::applySlaveToActive(int slave)
{
    if (DataViewWidget *w = activeData())
    {
        PollDefinition d = w->definition();
        d.slave = slave;
        const int idx = activeIndex();
        w->setDefinition(d);
        m_defs[idx] = d;
        syncWindowTitle(idx);
        m_msgPane->setText(tr("已将活动窗口从站设为 %1").arg(slave));
        m_msgPane->setStyleSheet(QStringLiteral("color:#16a34a;"));
    }
}

void ModbusMasterTool::onScanBusy(bool busy)
{
    // 重定向的是发起扫描时的活动连接: 精确暂停该连接的数据窗口轮询 +
    // 从机在线探测, 不误伤其他连接; 结束恢复 (不回写用户手动暂停态)
    if (busy)
    {
        m_scanConnId = m_conns->activeId();
        pauseForConnection(m_scanConnId, true);
    }
    else
    {
        pauseForConnection(m_scanConnId, false);
        m_scanConnId.clear();
    }
}

void ModbusMasterTool::applySerialPick(int baud, int parity, int stopBits, int slave)
{
    const QString id = m_conns->activeId();
    if (id.isEmpty())
        return;
    AppSettings::MbToolConn p = m_conns->entry(id).params;
    p.kind = 0; // RTU
    p.baud = baud;
    p.parity = parity;
    p.stopBits = stopBits;
    m_conns->updateConnection(id, p);
    mirrorActiveToSettings();
    applySlaveToActive(slave);
    // 参数在打开串口时生效: 断开当前连接立即以新参数重连 (状态栏跟进)
    m_connectingId.clear();
    m_conns->disconnectOne(id);
    updateConnectUi();
    connectConnection(id);
}

void ModbusMasterTool::applyTcpPick(const QString &host, quint16 port, int slave)
{
    const QString id = m_conns->activeId();
    if (id.isEmpty())
        return;
    AppSettings::MbToolConn p = m_conns->entry(id).params;
    p.kind = 1; // TCP
    p.host = host;
    p.tcpPort = port;
    m_conns->updateConnection(id, p);
    mirrorActiveToSettings();
    applySlaveToActive(slave);
    m_connectingId.clear();
    m_conns->disconnectOne(id);
    updateConnectUi();
    connectConnection(id);
}

void ModbusMasterTool::showFrameLog()
{
    if (!m_frameLog)
    {
        m_frameLog = new FrameLogDialog(this);
        // 全部连接的帧都接入 (帧记录含从站地址/TID, 混排展示);
        // 之后新增连接经 onClientAdded 接入
        for (const QString &id : m_conns->ids())
        {
            if (MasterClient *c = m_conns->client(id))
                connect(c, &MasterClient::frameLogged, m_frameLog,
                        &FrameLogDialog::appendFrame, Qt::UniqueConnection);
        }
    }
    m_frameLog->show();
    m_frameLog->raise();
    m_frameLog->activateWindow();
}

// ============================================================================
// 窗口菜单动作
// ============================================================================
void ModbusMasterTool::tileWindows()
{
    m_mdi->tileSubWindows();
}

void ModbusMasterTool::cascadeWindows()
{
    m_mdi->cascadeSubWindows();
}

void ModbusMasterTool::closeDataWindows()
{
    const QList<QMdiSubWindow *> subs = m_mdi->subWindowList();
    for (QMdiSubWindow *sub : subs)
        if (qobject_cast<DataViewWidget *>(sub->widget()))
            sub->close();
}

void ModbusMasterTool::showAbout()
{
    // 版本取注入的 MODBUSTOOLS_VERSION (与 app.rc / 安装包同源 project(VERSION))
    const QString ver = QString::fromLatin1(MODBUSTOOLS_VERSION);
    QMessageBox::information(
        this, tr("关于 STModbusTools"),
        tr("STModbusTools — Modbus 主站调试工具 (RTU / TCP)。\n\n"
           "本程序基于 Qt 框架开发 (Qt %1)。Qt 为 The Qt Company 的产品，"
           "本程序以动态链接方式使用 Qt，遵循 GNU LGPL v3 许可；"
           "Qt 的许可文本与源码获取方式见随附 LICENSES 目录。\n\n"
           "主要功能: RTU 串口 / Modbus TCP 主站调试, 多数据窗口与写窗口, "
           "Test Center, 从站 / 地址 / 串口参数扫描, TCP 发现, 工作区保存。\n"
           "快捷键: F3 连接设置 · F8 读/写定义 · F6 读/写一次 · Ctrl+N 新建窗口\n\n"
           "联系方式: QQ 271314609 · iichudi@hotmail.com · https://simainaite.com\n"
           "版本 v%2").arg(QStringLiteral(QT_VERSION_STR), ver));
}

// ============================================================================
// 工作区 JSON (v3): 多连接 + 从机列表 + 活动连接 + 全部数据窗口定义
// ============================================================================
void ModbusMasterTool::saveWorkspace()
{
    ModbusWorkspace::Data w;
    for (const QString &id : m_conns->ids())
    {
        const ConnectionManager::Entry e = m_conns->entry(id);
        ModbusWorkspace::ConnEntry ce;
        ce.id = e.id;
        ce.params = e.params;
        ce.slaves = e.slaves;
        w.conns.append(ce);
    }
    w.activeId = m_conns->activeId();
    w.defs = m_defs;
    const QString last = AppSettings::mbToolWorkspacePath();
    const QString path = QFileDialog::getSaveFileName(
        this, tr("保存工作区"),
        last.isEmpty() ? QStringLiteral("modbusmaster.json") : last,
        tr("JSON (*.json)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QMessageBox::warning(this, tr("保存工作区"), f.errorString());
        return;
    }
    f.write(ModbusWorkspace::toJson(w));
    AppSettings::setMbToolWorkspacePath(path);
    m_msgPane->setText(tr("工作区已保存 (%1 个连接 · %2 个数据窗口)")
                           .arg(w.conns.size())
                           .arg(m_defs.size()));
    m_msgPane->setStyleSheet(QStringLiteral("color:#16a34a;"));
}

void ModbusMasterTool::openWorkspace()
{
    if (m_scanDlg && m_scanDlg->isBusy())
    {
        QMessageBox::information(this, tr("打开工作区"),
                                 tr("扫描进行中 — 请先停止扫描再打开工作区。"));
        return;
    }
    if (!m_connectingId.isEmpty())
    {
        QMessageBox::information(this, tr("打开工作区"),
                                 tr("连接尝试在途 — 请等待完成或取消后再打开工作区。"));
        return;
    }
    const QString last = AppSettings::mbToolWorkspacePath();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开工作区"), last.isEmpty() ? QString() : last, tr("JSON (*.json)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this, tr("打开工作区"), f.errorString());
        return;
    }
    ModbusWorkspace::Data w;
    if (!ModbusWorkspace::fromJson(f.readAll(), &w))
    {
        QMessageBox::warning(this, tr("打开工作区"), tr("工作区格式无效"));
        return;
    }
    AppSettings::setMbToolWorkspacePath(path);
    // 全量重建: 关闭数据窗口 → 清空现有连接 → 按文件恢复 (id 保留, 窗口
    // 定义里的 connectionId 直接有效; v2 旧文件 defs 无 id → 绑首个连接)
    const QList<QMdiSubWindow *> subs = m_mdi->subWindowList();
    for (QMdiSubWindow *sub : subs)
        if (qobject_cast<DataViewWidget *>(sub->widget()))
            sub->close();
    const QStringList old = m_conns->ids();
    for (const QString &id : old)
        m_conns->removeConnection(id);
    for (const ModbusWorkspace::ConnEntry &ce : w.conns)
        m_conns->addConnection(ce.params, ce.id);
    if (!w.activeId.isEmpty() && m_conns->contains(w.activeId))
        m_conns->setActiveId(w.activeId);
    else if (!m_conns->ids().isEmpty())
        m_conns->setActiveId(m_conns->ids().first());
    mirrorActiveToSettings();
    for (const auto &d : w.defs)
        createDataWindow(d, false);
    m_mdi->tileSubWindows();
    m_msgPane->setText(tr("工作区已载入 (%1 个连接 · %2 个数据窗口)")
                           .arg(w.conns.size())
                           .arg(w.defs.size()));
    m_msgPane->setStyleSheet(QStringLiteral("color:#16a34a;"));
}
