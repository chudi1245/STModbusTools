#pragma once

// ============================================================================
// ModbusMasterTool.h — Modbus 调试工具主窗 (Modbus Poll UI 操作逻辑对齐)
//   QMainWindow + 左侧设备树 + QMdiArea: 多连接 (串口/TCP) 并存, 每连接
//   一个 MasterClient; 树中选中 = 活动连接 (F3/连接/断开/扫描/Test Center
//   的作用对象); 串口连接可挂多从机 (从机地址区分) 由 SlavePoller 轮询探测;
//   每个读/写定义一个数据子窗口 (绑定创建时的连接), 菜单 + 工具栏驱动
//   (F3 连接设置 / F8 定义 / F6 读写一次 / Ctrl+N 新建窗口),
//   状态栏 = 消息窗格 + Tx/Err 计数; Test Center 为单例子窗口
// ============================================================================

#include "AppSettings.h"
#include "CommStatsCollector.h"
#include "PollDefinition.h"

#include <QMainWindow>
#include <QVector>

class QAction;
class QCheckBox;
class QLabel;
class QMdiArea;
class QMdiSubWindow;
class QProgressDialog;
class QSplitter;
class ConnectionManager;
class MasterClient;
class DataViewWidget;
class DeviceTreeWidget;
class TestCenterPage;
class ScanDialog;
class FrameLogDialog;

class ModbusMasterTool : public QMainWindow
{
    Q_OBJECT
public:
    explicit ModbusMasterTool(QWidget *parent = nullptr);
    ~ModbusMasterTool() override;

private slots:
    void newWindow();          // Ctrl+N: 新数据窗口 (绑活动连接)
    void editDefinition();     // F8: 活动数据窗口定义
    void showConnDialog();     // F3: 编辑活动连接参数 (无连接 = 新建)
    void addSerialConnection(); // 连接菜单: 添加串口连接 (确定即连)
    void addTcpConnection();    // 连接菜单: 添加 TCP 连接 (确定即连)
    void connectDevice();      // 连接活动连接 (工具栏/菜单)
    void disconnectDevice();   // 断开活动连接 (在途时作取消)
    void togglePause(bool on); // 扫描暂停 (全局: 全部窗口 + 全部从机轮询)
    void readWriteOnce();      // F6: 活动窗口
    void onConnStateChanged(const QString &id, bool on); // 任一连接状态变化
    void onActiveConnectionChanged(const QString &id);   // 活动连接切换
    void onTxn(const TxnRecord &rec); // Tx/Err 计数 (全部连接汇总)

    // 显示菜单 (作用于活动数据窗口)
    void editScale();
    void editRules();
    void editAlias();
    void editValueNames();
    void editCellLook(); // 单元格样式… (字体/行高/背景 + 列宽)
    void setHideNames(bool on);
    void setPlcBase1(bool on);
    void setAddrInCell(bool on);
    void toggleDeviceTree(bool on); // 显示/隐藏左侧设备树

    void showTestCenter();
    void showScan();
    void showFrameLog();
    void exportCsv();

    // 扫描结果回填: 回写活动连接参数 (保存设置) 并立即重连生效
    void applySerialPick(int baud, int parity, int stopBits, int slave);
    void applyTcpPick(const QString &host, quint16 port, int slave);
    void applySlaveToActive(int slave);
    void onScanBusy(bool busy); // 参数扫描重定向活动连接期间暂停该连接轮询

    void tileWindows();
    void cascadeWindows();
    void closeDataWindows();
    void showAbout();

    void saveWorkspace();
    void openWorkspace();

private:
    // ---- 连接层 (多连接) ----
    void addConnectionWithDialog(int kind);          // ConnDialog → addConnection → 即连
    void connectConnection(const QString &id);       // 带进度窗/引导的连接
    void disconnectConnection(const QString &id);    // 在途时兼作取消
    void editConnectionParams(const QString &id);    // F3 同款参数编辑
    void removeConnectionConfirmed(const QString &id);
    void setupSlaves(const QString &id);             // 从机设备对话框 (仅串口)
    void pickActiveConnection(const QString &id);    // 树单击 → 活动切换
    void removeSlave(const QString &id, int address);
    void onSlaveActivated(const QString &id, int slave); // 双击从机 → 新数据窗口
    void onClientAdded(MasterClient *c);             // 接 txnDone/frameLogged
    void pauseForConnection(const QString &id, bool on); // 指定连接的窗口轮询+从机探测
    void mirrorActiveToSettings();                   // 活动连接参数 → AppSettings (记忆)

    void buildMenus();
    void buildToolBar();
    void buildStatusBar();
    DataViewWidget *createDataWindow(const PollDefinition &d, bool tile);
    void removeDataWindow(QObject *sub);
    void syncWindowTitle(int idx);
    void syncDisplayActions(); // 活动窗口 → 显示菜单勾选态
    DataViewWidget *activeData() const;
    int activeIndex() const;
    void updateConnectUi();
    void updateStatsPanes();
    void ensureConnDlg(); // 惰性创建连接中模态窗
    void closeConnDlg();
    void updateConnPane(bool on); // 状态栏右下角连接参数窗格

    ConnectionManager *m_conns = nullptr;
    DeviceTreeWidget *m_tree = nullptr;
    QSplitter *m_splitter = nullptr;
    QMdiArea *m_mdi = nullptr;

    QVector<PollDefinition> m_defs; // 与 m_windows/m_subs 平行
    QVector<DataViewWidget *> m_windows;
    QVector<QMdiSubWindow *> m_subs;

    TestCenterPage *m_tc = nullptr;
    QMdiSubWindow *m_tcSub = nullptr;
    ScanDialog *m_scanDlg = nullptr;
    FrameLogDialog *m_frameLog = nullptr;

    QString m_connectingId; // 连接尝试在途的连接 (空 = 无在途)
    QString m_scanConnId;   // 参数扫描重定向中的连接 (busy 期间暂停它)
    bool m_cancelConn = false;      // 用户经连接窗体取消在途连接
    qint64 m_txCount = 0;
    qint64 m_errCount = 0;

    // 动作 (objectName 供测试)
    QAction *aNewWindow = nullptr;
    QAction *aF8 = nullptr;
    QAction *aF6 = nullptr;
    QAction *aConnect = nullptr;
    QAction *aDisconnect = nullptr;
    QAction *aPause = nullptr;
    QAction *aAddSerial = nullptr;
    QAction *aAddTcp = nullptr;
    QAction *aDevTree = nullptr;
    QAction *aHideNames = nullptr;
    QAction *aPlcBase1 = nullptr;
    QAction *aAddrInCell = nullptr;
    QAction *aClearStyles = nullptr; // 清除单元格显示设置
    QList<QAction *> m_fmtActions; // 显示格式 29 项 (勾选组)
    QList<QAction *> m_winMenuDyn; // 窗口菜单动态窗口项 (aboutToShow 重建)
    QLabel *m_msgPane = nullptr;
    QLabel *m_txPane = nullptr;
    QLabel *m_errPane = nullptr;
    QLabel *m_connPane = nullptr;   // 右下角连接参数 (活动连接, 连接成功后显示)
    QProgressDialog *m_connDlg = nullptr; // 连接中模态窗 (取消 = 中止在途尝试)
    int m_naming = 0; // 窗口自增命名
};
