#pragma once

// ============================================================================
// DeviceTreeWidget.h — 左侧设备树 (多连接/多从机监控面板)
//   顶层节点 = 连接: 串口显示端口名 + 串口参数, TCP 显示主机 + 端口;
//   串口连接的子节点 = 从机设备 (从机地址区分)。
//   状态点: 绿 = 连接在线 / 从机应答, 红 = 断开 / 无应答 (连接层跟
//   ConnectionManager::stateChanged, 从机层跟 SlavePoller 在线判定)。
//   交互: 单击 = 设为活动连接; 双击从机 = 以该连接+从机新建数据窗口;
//   右键菜单 = 连接/断开/属性/移除, 添加/移除从机 (动作经信号交主窗执行,
//   本控件不含业务对话框)。活动连接以粗体标识。
// ============================================================================

#include <QTreeWidget>

class ConnectionManager;
class QTreeWidgetItem;

class DeviceTreeWidget : public QTreeWidget
{
    Q_OBJECT
public:
    explicit DeviceTreeWidget(ConnectionManager *conns, QWidget *parent = nullptr);

    // 当前选中节点所属连接 (连接或从机节点; 无选中 = 空串)
    QString selectedConnectionId() const;
    int selectedSlaveAddress() const; // 选中的从机地址 (无 = -1)

signals:
    void activePickRequested(const QString &connectionId); // 单击 → 设为活动
    void connectRequested(const QString &connectionId);
    void disconnectRequested(const QString &connectionId);
    void editConnectionRequested(const QString &connectionId); // 属性 (F3 同款)
    void removeConnectionRequested(const QString &connectionId);
    void addSlavesRequested(const QString &connectionId); // 仅串口
    void removeSlaveRequested(const QString &connectionId, int address);
    void slaveActivated(const QString &connectionId, int address); // 双击从机
    void addSerialRequested(); // 空白处右键 → 添加串口连接 (主窗执行)
    void addTcpRequested();    // 空白处右键 → 添加 TCP 连接

private slots:
    void refresh(); // 全量重建 (listChanged/slavesChanged, 节点数小重建廉价)
    void onStateChanged(const QString &id, bool connected);
    void onSlaveOnline(const QString &id, int address, bool online);
    void onSlaveProbed(const QString &id, int address, bool online, double elapsedMs);
    void onActiveChanged(const QString &id);
    void showContextMenu(const QPoint &pos);

private:
    enum ItemRole
    {
        RoleId = Qt::UserRole + 1,    // 所属连接 id (连接与从机节点均有)
        RoleSlave = Qt::UserRole + 2, // 从机地址 (仅从机节点)
        RoleConnLevel = Qt::UserRole + 3 // 1 = 连接节点, 2 = 从机节点
    };
    QTreeWidgetItem *connItem(const QString &id) const;
    QTreeWidgetItem *slaveItem(const QString &id, int address) const;

    ConnectionManager *m_conns = nullptr;
};
