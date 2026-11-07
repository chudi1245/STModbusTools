// ============================================================================
// ConnectionManager.cpp — 多连接注册表实现
// ============================================================================

#include "ConnectionManager.h"

#include "SlavePoller.h"

#include <QUuid>
#include <QSerialPort>

#include <algorithm>

ConnectionManager::ConnectionManager(QObject *parent)
    : QObject(parent)
{
}

ConnectionManager::~ConnectionManager()
{
    for (const Entry &e : m_entries)
        e.client->disconnectDevice();
}

QString ConnectionManager::makeId() const
{
    forever
    {
        const QString id = QUuid::createUuid().toString(QUuid::Id128).left(8);
        if (!contains(id))
            return id;
    }
}

int ConnectionManager::indexOf(const QString &id) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).id == id)
            return i;
    return -1;
}

QString ConnectionManager::addConnection(const AppSettings::MbToolConn &params,
                                         const QString &forcedId)
{
    // Windows 串口独占: 同一端口名只允许一个串口连接 (空端口名尚待选择, 放行)
    if (params.kind == 0 && !params.portName.isEmpty())
    {
        for (const Entry &e : m_entries)
            if (e.params.kind == 0 && e.params.portName == params.portName)
                return QString();
    }
    const QString id = forcedId.isEmpty() ? makeId() : forcedId;
    if (id.isEmpty() || contains(id))
        return QString();

    Entry e;
    e.id = id;
    e.params = params;
    e.client = new MasterClient(this);
    applyParams(e.client, params);
    e.poller = new SlavePoller(e.client, this);
    m_entries.append(e);

    connect(e.client, &MasterClient::connectionChanged, this,
            [this, id](bool on) { emit stateChanged(id, on); });
    connect(e.poller, &SlavePoller::slaveOnlineChanged, this,
            [this, id](int addr, bool on) { emit slaveOnlineChanged(id, addr, on); });
    connect(e.poller, &SlavePoller::slaveProbed, this,
            [this, id](int addr, bool on, double ms) { emit slaveProbed(id, addr, on, ms); });

    if (m_activeId.isEmpty())
    {
        m_activeId = id;
        emit activeChanged(m_activeId);
    }
    emit clientAdded(e.client);
    emit listChanged();
    return id;
}

void ConnectionManager::removeConnection(const QString &id)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    Entry e = m_entries.takeAt(i);
    e.poller->deleteLater();
    e.client->disconnectDevice();
    e.client->deleteLater();
    if (m_activeId == id)
    {
        m_activeId = m_entries.isEmpty() ? QString() : m_entries.first().id;
        emit activeChanged(m_activeId);
    }
    emit listChanged();
}

bool ConnectionManager::updateConnection(const QString &id,
                                         const AppSettings::MbToolConn &params)
{
    const int i = indexOf(id);
    if (i < 0)
        return false;
    if (params.kind == 0 && !params.portName.isEmpty())
    {
        for (int k = 0; k < m_entries.size(); ++k)
        {
            if (k == i)
                continue;
            if (m_entries.at(k).params.kind == 0
                && m_entries.at(k).params.portName == params.portName)
                return false;
        }
    }
    m_entries[i].params = params;
    applyParams(m_entries[i].client, params);
    emit listChanged();
    return true;
}

bool ConnectionManager::connectOne(const QString &id)
{
    MasterClient *c = client(id);
    return c && c->connectDevice();
}

void ConnectionManager::disconnectOne(const QString &id)
{
    if (MasterClient *c = client(id))
        c->disconnectDevice();
}

QStringList ConnectionManager::ids() const
{
    QStringList out;
    out.reserve(m_entries.size());
    for (const Entry &e : m_entries)
        out.append(e.id);
    return out;
}

bool ConnectionManager::contains(const QString &id) const
{
    return indexOf(id) >= 0;
}

ConnectionManager::Entry ConnectionManager::entry(const QString &id) const
{
    const int i = indexOf(id);
    return i >= 0 ? m_entries.at(i) : Entry();
}

MasterClient *ConnectionManager::client(const QString &id) const
{
    const int i = indexOf(id);
    return i >= 0 ? m_entries.at(i).client : nullptr;
}

SlavePoller *ConnectionManager::poller(const QString &id) const
{
    const int i = indexOf(id);
    return i >= 0 ? m_entries.at(i).poller : nullptr;
}

QString ConnectionManager::label(const QString &id) const
{
    const Entry e = entry(id);
    if (e.id.isEmpty())
        return QString();
    if (e.params.kind == 1)
        return QStringLiteral("%1:%2").arg(e.params.host).arg(e.params.tcpPort);
    return e.params.portName;
}

void ConnectionManager::setActiveId(const QString &id)
{
    if (id == m_activeId)
        return;
    if (!id.isEmpty() && !contains(id))
        return;
    m_activeId = id;
    emit activeChanged(m_activeId);
}

void ConnectionManager::setSlaves(const QString &id, const QList<int> &addresses)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    // 升序去重 (与 poller 一致; 持久化/树展示用同一份规范列表)
    QList<int> sorted = addresses;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    m_entries[i].slaves = sorted;
    if (m_entries[i].poller)
        m_entries[i].poller->setAddresses(sorted);
    emit slavesChanged(id);
}

QList<int> ConnectionManager::slaves(const QString &id) const
{
    const int i = indexOf(id);
    return i >= 0 ? m_entries.at(i).slaves : QList<int>();
}

void ConnectionManager::applyParams(MasterClient *c, const AppSettings::MbToolConn &p)
{
    c->setKind(p.kind == 1 ? MasterClient::Kind::Tcp : MasterClient::Kind::SerialRtu);
    const QSerialPort::Parity parity = p.parity == 1 ? QSerialPort::EvenParity
                                       : p.parity == 2 ? QSerialPort::OddParity
                                                       : QSerialPort::NoParity;
    const QSerialPort::StopBits stop =
        p.stopBits == 2 ? QSerialPort::TwoStop : QSerialPort::OneStop;
    c->setSerialParams(p.portName, p.baud, parity, stop);
    c->setTcpParams(p.host, quint16(p.tcpPort));
    c->setTimeoutMs(p.timeoutMs);
    c->setRetries(p.retries);
}
