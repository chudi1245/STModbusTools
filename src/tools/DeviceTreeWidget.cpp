// ============================================================================
// DeviceTreeWidget.cpp — 设备树实现 (manager 信号驱动, 全量重建 + 定点刷新)
// ============================================================================

#include "DeviceTreeWidget.h"

#include "ConnectionManager.h"
#include "SlavePoller.h"

#include <QApplication>
#include <QFont>
#include <QHeaderView>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QTreeWidgetItem>

namespace {
// 状态点: 12px 反走样实心圆 (绿 #16a34a / 红 #dc2626, 与状态栏配色一致);
// 静态缓存 QIcon 复用 (QTreeWidgetItem::setIcon 按引用持有, 借此避免逐节点 pixmap)
QIcon dotIcon(bool on)
{
    static QIcon icons[2];
    static bool inited = false;
    if (!inited)
    {
        for (int i = 0; i < 2; ++i)
        {
            QPixmap pm(14, 14);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            p.setBrush(QColor(i == 1 ? QStringLiteral("#16a34a")
                                     : QStringLiteral("#dc2626")));
            p.setPen(Qt::NoPen);
            p.drawEllipse(2, 2, 10, 10);
            icons[i].addPixmap(pm);
        }
        inited = true;
    }
    return icons[on ? 1 : 0];
}

QString serialInfoText(const AppSettings::MbToolConn &p)
{
    const QString parity = p.parity == 1 ? QStringLiteral("E")
                           : p.parity == 2 ? QStringLiteral("O")
                                           : QStringLiteral("N");
    return QObject::tr("%1 8%2%3 · %4 ms").arg(p.baud).arg(parity, QString::number(p.stopBits)).arg(p.timeoutMs);
}

// 连接/从机节点的"未连接/未探测"信息占位
QString idleText()
{
    return QStringLiteral("—");
}
} // namespace

DeviceTreeWidget::DeviceTreeWidget(ConnectionManager *conns, QWidget *parent)
    : QTreeWidget(parent)
    , m_conns(conns)
{
    setObjectName(QStringLiteral("master.deviceTree"));
    setColumnCount(2);
    setHeaderLabels({tr("设备"), tr("信息")});
    setRootIsDecorated(true);
    setUniformRowHeights(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(1, QHeaderView::Stretch);
    header()->setStretchLastSection(false);
    setMinimumWidth(200);
    setContextMenuPolicy(Qt::CustomContextMenu);

    connect(m_conns, &ConnectionManager::listChanged, this, &DeviceTreeWidget::refresh);
    connect(m_conns, &ConnectionManager::slavesChanged, this,
            [this](const QString &) { refresh(); });
    connect(m_conns, &ConnectionManager::stateChanged, this,
            &DeviceTreeWidget::onStateChanged);
    connect(m_conns, &ConnectionManager::slaveOnlineChanged, this,
            &DeviceTreeWidget::onSlaveOnline);
    connect(m_conns, &ConnectionManager::slaveProbed, this, &DeviceTreeWidget::onSlaveProbed);
    connect(m_conns, &ConnectionManager::activeChanged, this, &DeviceTreeWidget::onActiveChanged);

    // 单击连接或从机 → 所属连接设为活动 (工具栏 连接/断开/F3 的作用对象)
    connect(this, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it, int) {
        const QString id = it->data(0, RoleId).toString();
        if (!id.isEmpty())
            emit activePickRequested(id);
    });
    // 双击从机 → 新建数据窗口 (从机地址预填); 双击连接 = 展开/收起默认行为
    connect(this, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *it, int) {
                const QString id = it->data(0, RoleId).toString();
                const int slave = it->data(0, RoleSlave).toInt();
                if (!id.isEmpty() && slave > 0)
                    emit slaveActivated(id, slave);
            });
    connect(this, &QTreeWidget::customContextMenuRequested, this,
            &DeviceTreeWidget::showContextMenu);

    refresh();
}

void DeviceTreeWidget::refresh()
{
    const QString keepId = selectedConnectionId();
    clear();
    for (const QString &id : m_conns->ids())
    {
        const ConnectionManager::Entry e = m_conns->entry(id);
        auto *it = new QTreeWidgetItem(this);
        const bool tcp = e.params.kind == 1;
        it->setText(0, m_conns->label(id));
        it->setText(1, tcp ? tr("TCP · 端口 %1").arg(e.params.tcpPort)
                           : serialInfoText(e.params));
        it->setToolTip(
            0, tcp ? tr("Modbus/TCP — %1:%2 · 超时 %3 ms · 重试 %4")
                           .arg(e.params.host, QString::number(e.params.tcpPort),
                                QString::number(e.params.timeoutMs),
                                QString::number(e.params.retries))
                   : tr("Modbus RTU — %1 · 超时 %2 ms · 重试 %3")
                         .arg(serialInfoText(e.params),
                              QString::number(e.params.timeoutMs),
                              QString::number(e.params.retries)));
        it->setData(0, RoleId, id);
        it->setData(0, RoleConnLevel, 1);
        it->setIcon(0, dotIcon(e.client && e.client->isConnected()));
        // 从机子节点 (仅串口; 每台一个地址)
        for (int addr : e.slaves)
        {
            auto *sl = new QTreeWidgetItem(it);
            sl->setText(0, tr("从机 %1").arg(addr));
            const bool online = e.poller && e.poller->isOnline(addr);
            sl->setIcon(0, dotIcon(online));
            sl->setText(1, idleText()); // 探测结果到达后经 onSlaveProbed 填充
            sl->setData(0, RoleId, id);
            sl->setData(0, RoleSlave, addr);
            sl->setData(0, RoleConnLevel, 2);
        }
        it->setExpanded(true);
    }
    onActiveChanged(m_conns->activeId());
    // 重建后尽量保住选中 (同连接仍存在)
    if (!keepId.isEmpty())
    {
        if (QTreeWidgetItem *it = connItem(keepId))
            setCurrentItem(it);
    }
}

QTreeWidgetItem *DeviceTreeWidget::connItem(const QString &id) const
{
    for (int i = 0; i < topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *it = topLevelItem(i);
        if (it->data(0, RoleId).toString() == id)
            return it;
    }
    return nullptr;
}

QTreeWidgetItem *DeviceTreeWidget::slaveItem(const QString &id, int address) const
{
    QTreeWidgetItem *c = connItem(id);
    if (!c)
        return nullptr;
    for (int i = 0; i < c->childCount(); ++i)
    {
        QTreeWidgetItem *sl = c->child(i);
        if (sl->data(0, RoleSlave).toInt() == address)
            return sl;
    }
    return nullptr;
}

void DeviceTreeWidget::onStateChanged(const QString &id, bool connected)
{
    if (QTreeWidgetItem *it = connItem(id))
        it->setIcon(0, dotIcon(connected));
}

void DeviceTreeWidget::onSlaveOnline(const QString &id, int address, bool online)
{
    if (QTreeWidgetItem *sl = slaveItem(id, address))
        sl->setIcon(0, dotIcon(online));
}

void DeviceTreeWidget::onSlaveProbed(const QString &id, int address, bool online,
                                     double elapsedMs)
{
    if (QTreeWidgetItem *sl = slaveItem(id, address))
        sl->setText(1, online ? tr("%1 ms").arg(int(elapsedMs + 0.5)) : tr("无应答"));
}

void DeviceTreeWidget::onActiveChanged(const QString &id)
{
    for (int i = 0; i < topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *it = topLevelItem(i);
        QFont f = it->font(0);
        f.setBold(it->data(0, RoleId).toString() == id);
        it->setFont(0, f);
        it->setFont(1, f);
    }
}

void DeviceTreeWidget::showContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *it = itemAt(pos);
    if (!it)
    {
        // 空白处: 添加连接 (与「连接」菜单同款入口)
        QMenu menu(this);
        menu.addAction(tr("添加串口连接…"), this,
                       [this] { emit addSerialRequested(); });
        menu.addAction(tr("添加 TCP 连接…"), this, [this] { emit addTcpRequested(); });
        menu.exec(viewport()->mapToGlobal(pos));
        return;
    }
    const QString id = it->data(0, RoleId).toString();
    if (id.isEmpty())
        return;
    const int level = it->data(0, RoleConnLevel).toInt();
    QMenu menu(this);
    if (level == 1)
    {
        const bool on = m_conns->client(id) && m_conns->client(id)->isConnected();
        menu.addAction(on ? tr("断开") : tr("连接"), this,
                       [this, id, on] {
                           emit on ? disconnectRequested(id) : connectRequested(id);
                       });
        menu.addSeparator();
        menu.addAction(tr("连接属性…"), this,
                       [this, id] { emit editConnectionRequested(id); });
        const bool serial = m_conns->entry(id).params.kind == 0;
        if (serial)
            menu.addAction(tr("从机设备…"), this,
                           [this, id] { emit addSlavesRequested(id); });
        menu.addSeparator();
        menu.addAction(tr("移除连接"), this,
                       [this, id] { emit removeConnectionRequested(id); });
    }
    else
    {
        const int addr = it->data(0, RoleSlave).toInt();
        menu.addAction(tr("新建数据窗口"), this, [this, id, addr] {
            emit slaveActivated(id, addr);
        });
        menu.addSeparator();
        menu.addAction(tr("移除从机"), this,
                       [this, id, addr] { emit removeSlaveRequested(id, addr); });
    }
    menu.exec(viewport()->mapToGlobal(pos));
}

QString DeviceTreeWidget::selectedConnectionId() const
{
    QTreeWidgetItem *it = currentItem();
    return it ? it->data(0, RoleId).toString() : QString();
}

int DeviceTreeWidget::selectedSlaveAddress() const
{
    QTreeWidgetItem *it = currentItem();
    if (!it || it->data(0, RoleConnLevel).toInt() != 2)
        return -1;
    return it->data(0, RoleSlave).toInt();
}
