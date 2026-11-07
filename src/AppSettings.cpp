// ============================================================================
// AppSettings.cpp — 应用设置统一访问实现
// ============================================================================

#include "AppSettings.h"

#include <QSettings>

namespace AppSettings
{
// 软件自称重测力调试软件拆分而来: 主程序 (org=ST072G app=称重测力调试软件)
// 的 mbtool/ 域与 settings/lang 若存在而本应用尚无 mbtool/ 记忆, 整体搬入
// 一次 — 老用户从主程序工具菜单用过的连接参数/工作区路径不因拆分丢失。
void migrateLegacyIfNeeded()
{
    QSettings st;
    if (st.contains(QStringLiteral("mbtool/kind")))
        return;
    QSettings legacy(QSettings::NativeFormat, QSettings::UserScope,
                     QStringLiteral("ST072G"), QStringLiteral("称重测力调试软件"));
    const QStringList keys = legacy.allKeys();
    for (const QString &k : keys)
    {
        if (k == QLatin1String("settings/lang") || k.startsWith(QLatin1String("mbtool/")))
            st.setValue(k, legacy.value(k));
    }
    st.sync();
}

QString language()
{
    QSettings st;
    return st.value(QStringLiteral("settings/lang")).toString();
}

void setLanguage(const QString &lang)
{
    QSettings st;
    st.setValue(QStringLiteral("settings/lang"), lang);
}

MbToolConn mbToolConn()
{
    QSettings st;
    MbToolConn c;
    c.kind = qBound(0, st.value(QStringLiteral("mbtool/kind"), 0).toInt(), 1);
    c.portName = st.value(QStringLiteral("mbtool/port")).toString();
    c.baud = qBound(300, st.value(QStringLiteral("mbtool/baud"), 115200).toInt(), 256000);
    c.parity = qBound(0, st.value(QStringLiteral("mbtool/parity"), 0).toInt(), 2);
    c.stopBits = qBound(1, st.value(QStringLiteral("mbtool/stop"), 1).toInt(), 2);
    c.host = st.value(QStringLiteral("mbtool/host"), QStringLiteral("192.168.1.100")).toString();
    c.tcpPort = qBound(1, st.value(QStringLiteral("mbtool/tcpport"), 502).toInt(), 65535);
    c.timeoutMs = qBound(100, st.value(QStringLiteral("mbtool/timeout"), 1000).toInt(), 10000);
    c.retries = qBound(0, st.value(QStringLiteral("mbtool/retries"), 2).toInt(), 5);
    c.plcAddr = st.value(QStringLiteral("mbtool/plc"), false).toBool();
    return c;
}

void setMbToolConn(const MbToolConn &c)
{
    QSettings st;
    st.setValue(QStringLiteral("mbtool/kind"), qBound(0, c.kind, 1));
    st.setValue(QStringLiteral("mbtool/port"), c.portName);
    st.setValue(QStringLiteral("mbtool/baud"), qBound(300, c.baud, 256000));
    st.setValue(QStringLiteral("mbtool/parity"), qBound(0, c.parity, 2));
    st.setValue(QStringLiteral("mbtool/stop"), qBound(1, c.stopBits, 2));
    st.setValue(QStringLiteral("mbtool/host"), c.host);
    st.setValue(QStringLiteral("mbtool/tcpport"), qBound(1, c.tcpPort, 65535));
    st.setValue(QStringLiteral("mbtool/timeout"), qBound(100, c.timeoutMs, 10000));
    st.setValue(QStringLiteral("mbtool/retries"), qBound(0, c.retries, 5));
    st.setValue(QStringLiteral("mbtool/plc"), c.plcAddr);
}

QString mbToolWorkspacePath()
{
    QSettings st;
    return st.value(QStringLiteral("mbtool/workspace")).toString();
}

void setMbToolWorkspacePath(const QString &path)
{
    QSettings st;
    st.setValue(QStringLiteral("mbtool/workspace"), path);
}
} // namespace AppSettings
