#pragma once

// ============================================================================
// ConnectionManager.h — 多连接注册表 (设备树/多机监控的连接层)
//   每个连接一个 MasterClient 实例 (广播自认领按实例天然隔离, 互不串扰),
//   外加一个 SlavePoller (从机在线轮询, 串口多从机场景)。
//   单连接时代的 "主窗唯一 m_mb" 语义由 activeId 取代: 树中选中 = 活动,
//   F3/连接/断开/扫描/Test Center 均作用于活动连接。
//   id 为短随机串 (跨会话不稳定, 工作区持久化时随文件保存/恢复)。
// ============================================================================

#include "AppSettings.h"
#include "MasterClient.h"

#include <QList>
#include <QObject>
#include <QStringList>

class SlavePoller;

class ConnectionManager : public QObject
{
    Q_OBJECT
public:
    struct Entry
    {
        QString id;
        AppSettings::MbToolConn params;
        QList<int> slaves; // 多从机地址 (仅串口; TCP 一连接一设备恒空)
        MasterClient *client = nullptr;
        SlavePoller *poller = nullptr;
    };

    explicit ConnectionManager(QObject *parent = nullptr);
    ~ConnectionManager() override;

    // 添加连接 (不自动连接)。串口端口名与其他串口连接重复 → 返回空串
    // (Windows 串口独占); forcedId 供工作区载入恢复持久化 id (冲突拒绝)
    QString addConnection(const AppSettings::MbToolConn &params,
                          const QString &forcedId = QString());
    // 移除: 断开并销毁 client/poller; 活动被移除 → 移交首个剩余 (无则清空)
    void removeConnection(const QString &id);
    // F3 参数编辑回写 (应用到 client, 已连接的连接参数在下次重连生效);
    // 串口端口名改与其他连接重复 → false
    bool updateConnection(const QString &id, const AppSettings::MbToolConn &params);

    bool connectOne(const QString &id); // false = 同步失败 (client->lastError())
    void disconnectOne(const QString &id);

    int count() const { return m_entries.size(); }
    QStringList ids() const;
    bool contains(const QString &id) const;
    Entry entry(const QString &id) const; // 无效 id → 空 Entry (id 为空串)
    MasterClient *client(const QString &id) const;
    SlavePoller *poller(const QString &id) const;
    // 短标签: 串口 = 端口名, TCP = 主机:端口 (设备树/状态栏/消息共用)
    QString label(const QString &id) const;

    QString activeId() const { return m_activeId; }
    MasterClient *activeClient() const { return client(m_activeId); }
    Entry activeEntry() const { return entry(m_activeId); }
    void setActiveId(const QString &id); // 无效 id = 清空活动

    // 多从机列表: 同步进 poller (slavesChanged), 工作区持久化由此读取
    void setSlaves(const QString &id, const QList<int> &addresses);
    QList<int> slaves(const QString &id) const;

signals:
    void clientAdded(MasterClient *client); // 主窗接 txnDone/frameLogged 的挂点
    void listChanged();                     // 增/删/改参 (树全量重建)
    void stateChanged(const QString &id, bool connected);
    void activeChanged(const QString &id); // 可为空串 (全部移除)
    void slavesChanged(const QString &id);
    // SlavePoller 转发 (携带连接 id, 树只接这一个信号源)
    void slaveOnlineChanged(const QString &id, int address, bool online);
    void slaveProbed(const QString &id, int address, bool online, double elapsedMs);

private:
    QString makeId() const;
    int indexOf(const QString &id) const;
    static void applyParams(MasterClient *c, const AppSettings::MbToolConn &p);

    QList<Entry> m_entries;
    QString m_activeId;
};
