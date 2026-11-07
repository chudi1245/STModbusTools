#pragma once

// ============================================================================
// ModbusWorkspace.h — 调试工具工作区序列化 (纯函数, 无 GUI, 可测)
//   v3: 多连接 (设备树) + 每连接从机列表 + 活动连接 + 全部数据窗口定义
//       (PollDefinition.connectionId 随 defs 持久化, 空串 = 首个连接);
//   v2 及更早: 单 conn 对象 → 包装为 conns[0] (defs 的 connectionId 为空,
//       载入后绑定首个连接), 行为与单连接时代完全一致。
// ============================================================================

#include "AppSettings.h"
#include "PollDefinition.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QVector>

namespace ModbusWorkspace {

struct ConnEntry
{
    QString id;                     // ConnectionManager id (载入时按它恢复绑定)
    AppSettings::MbToolConn params;
    QList<int> slaves;              // 多从机地址 (仅串口)
};

struct Data
{
    QList<ConnEntry> conns;
    QString activeId;               // 活动连接 id (空 = 首个)
    QVector<PollDefinition> defs;
};

namespace Detail {
inline QJsonObject connToJson(const AppSettings::MbToolConn &p, const QList<int> &slaves,
                              const QString &id)
{
    QJsonObject c;
    if (!id.isEmpty())
        c.insert(QStringLiteral("id"), id);
    c.insert(QStringLiteral("kind"), p.kind);
    c.insert(QStringLiteral("port"), p.portName);
    c.insert(QStringLiteral("baud"), p.baud);
    c.insert(QStringLiteral("parity"), p.parity);
    c.insert(QStringLiteral("stop"), p.stopBits);
    c.insert(QStringLiteral("host"), p.host);
    c.insert(QStringLiteral("tcpPort"), p.tcpPort);
    c.insert(QStringLiteral("timeout"), p.timeoutMs);
    c.insert(QStringLiteral("retries"), p.retries);
    c.insert(QStringLiteral("plc"), p.plcAddr);
    if (!slaves.isEmpty())
    {
        QJsonArray sa;
        for (int a : slaves)
            sa.append(a);
        c.insert(QStringLiteral("slaves"), sa);
    }
    return c;
}

inline ConnEntry connFromJson(const QJsonObject &c)
{
    ConnEntry e;
    e.id = c.value(QStringLiteral("id")).toString();
    e.params.kind = qBound(0, c.value(QStringLiteral("kind")).toInt(0), 1);
    e.params.portName = c.value(QStringLiteral("port")).toString();
    e.params.baud = qBound(300, c.value(QStringLiteral("baud")).toInt(115200), 256000);
    e.params.parity = qBound(0, c.value(QStringLiteral("parity")).toInt(0), 2);
    e.params.stopBits = qBound(1, c.value(QStringLiteral("stop")).toInt(1), 2);
    e.params.host = c.value(QStringLiteral("host")).toString();
    e.params.tcpPort = qBound(1, c.value(QStringLiteral("tcpPort")).toInt(502), 65535);
    e.params.timeoutMs = qBound(100, c.value(QStringLiteral("timeout")).toInt(1000), 10000);
    e.params.retries = qBound(0, c.value(QStringLiteral("retries")).toInt(2), 5);
    e.params.plcAddr = c.value(QStringLiteral("plc")).toBool(false);
    const QJsonArray sa = c.value(QStringLiteral("slaves")).toArray();
    for (const auto &v : sa)
    {
        const int a = qBound(1, v.toInt(1), 247);
        if (!e.slaves.contains(a))
            e.slaves.append(a);
    }
    return e;
}
} // namespace Detail

inline QByteArray toJson(const Data &w)
{
    QJsonObject root;
    root.insert(QStringLiteral("app"), QStringLiteral("ST072G-ModbusTools"));
    root.insert(QStringLiteral("version"), 3); // v3: 多连接 + 从机列表 (v1/v2 兼容加载)
    QJsonArray conns;
    for (const ConnEntry &e : w.conns)
        conns.append(Detail::connToJson(e.params, e.slaves, e.id));
    root.insert(QStringLiteral("conns"), conns);
    root.insert(QStringLiteral("active"), w.activeId);
    QJsonArray defs;
    for (const auto &d : w.defs)
        defs.append(d.toJson());
    root.insert(QStringLiteral("defs"), defs);
    return QJsonDocument(root).toJson();
}

inline bool fromJson(const QByteArray &raw, Data *w)
{
    if (!w)
        return false;
    *w = Data{}; // 缺失字段落默认
    const QJsonObject root = QJsonDocument::fromJson(raw).object();
    if (root.isEmpty())
        return false;
    const int version = root.value(QStringLiteral("version")).toInt(2);
    if (version >= 3)
    {
        const QJsonArray conns = root.value(QStringLiteral("conns")).toArray();
        for (const auto &v : conns)
            w->conns.append(Detail::connFromJson(v.toObject()));
        w->activeId = root.value(QStringLiteral("active")).toString();
    }
    else
    {
        // v2 及更早: 单 conn 对象 → 包装为 conns[0] (defs 的 connectionId 为空,
        // 载入方将其绑定到首个连接, 行为与单连接时代一致)
        const QJsonObject c = root.value(QStringLiteral("conn")).toObject();
        if (!c.isEmpty())
            w->conns.append(Detail::connFromJson(c));
    }
    const QJsonArray defs = root.value(QStringLiteral("defs")).toArray();
    for (const auto &v : defs)
        w->defs.append(PollDefinition::fromJson(v.toObject()));
    return true;
}

} // namespace ModbusWorkspace
