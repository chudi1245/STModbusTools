// ============================================================================
// SlavePoller.cpp — 从机在线轮询实现 (QTimer 节拍 + 在途守卫 + 广播自认领)
// ============================================================================

#include "SlavePoller.h"

#include "MasterClient.h"

#include <QTimer>
#include <QVector>

#include <algorithm>

SlavePoller::SlavePoller(MasterClient *mb, QObject *parent)
    : QObject(parent)
    , m_mb(mb)
{
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::CoarseTimer);
    connect(m_timer, &QTimer::timeout, this, &SlavePoller::probeNext);
    connect(m_mb, &MasterClient::readDone, this, &SlavePoller::onReadDone);
    connect(m_mb, &MasterClient::connectionChanged, this, &SlavePoller::onConnChanged);
    m_connected = m_mb->isConnected();
}

void SlavePoller::setAddresses(const QList<int> &addresses)
{
    QList<int> sorted = addresses;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    m_addresses = sorted;
    m_cursor = 0;
    m_inFlight = false;
    m_flightSlave = -1;
    // 移除地址的在线态清除 (保留地址的状态延续, 不整场清零)
    for (auto it = m_online.begin(); it != m_online.end();)
    {
        if (!m_addresses.contains(*it))
            it = m_online.erase(it);
        else
            ++it;
    }
    updateTimer();
}

void SlavePoller::setIntervalMs(int ms)
{
    m_intervalMs = qBound(20, ms, 60000);
    updateTimer();
}

void SlavePoller::setPaused(bool on)
{
    if (m_paused == on)
        return;
    m_paused = on;
    updateTimer();
}

void SlavePoller::updateTimer()
{
    if (m_connected && !m_paused && !m_addresses.isEmpty())
        m_timer->start(m_intervalMs);
    else
        m_timer->stop();
}

void SlavePoller::probeNext()
{
    if (m_inFlight || m_paused || m_addresses.isEmpty() || !m_connected)
        return;
    if (m_cursor >= m_addresses.size())
        m_cursor = 0;
    m_flightSlave = m_addresses.at(m_cursor);
    // read() 返回 false = 未连接瞬断: 不置在途 (无 readDone 回报), 下个 tick 重试
    if (!m_mb->read(3, m_flightSlave, 0, 1))
        return;
    m_inFlight = true;
}

void SlavePoller::onReadDone(int fc, int slave, quint16 addr, bool ok,
                             const QVector<quint16> &values, const QString &errText,
                             double elapsedMs)
{
    Q_UNUSED(values);
    if (!m_inFlight || fc != 3 || slave != m_flightSlave || addr != 0)
        return; // 他方事务 (数据窗口轮询等), 不认领
    m_inFlight = false;
    // 与 ScanDialog::probeIsAlive 同口径: 协议异常/CRC 错 = 有设备应答
    const bool alive =
        ok || errText.contains(QStringLiteral("协议"), Qt::CaseInsensitive)
        || errText.contains(QStringLiteral("Protocol"), Qt::CaseInsensitive)
        || errText.contains(QStringLiteral("CRC"), Qt::CaseInsensitive)
        || errText.contains(QStringLiteral("exception"), Qt::CaseInsensitive);
    setOnline(slave, alive);
    emit slaveProbed(slave, alive, elapsedMs);
    m_cursor = (m_cursor + 1) % m_addresses.size();
}

void SlavePoller::onConnChanged(bool connected)
{
    m_connected = connected;
    m_inFlight = false;
    if (!connected)
    {
        const QList<int> wasOnline = m_online.values();
        m_online.clear();
        for (int a : wasOnline)
            emit slaveOnlineChanged(a, false);
    }
    updateTimer();
}

void SlavePoller::setOnline(int address, bool online)
{
    const bool was = m_online.contains(address);
    if (online)
        m_online.insert(address);
    else
        m_online.remove(address);
    if (was != online)
        emit slaveOnlineChanged(address, online);
}
