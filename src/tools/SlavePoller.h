#pragma once

// ============================================================================
// SlavePoller.h — 从机在线轮询引擎 (纯逻辑无 UI, 可测)
//   同一串口连接多台从机设备的"插件式"轮转探测: 按地址列表依次 FC03 读
//   地址 0 ×1 (与 ScanDialog 从站扫描同口径), 单在途链式推进 —
//   发一台 → 收到应答 (含超时/异常) → 判定在线/离线 → 下一台, 回绕循环。
//   在线判定: ok, 或 errText 含 协议/Protocol/CRC/exception (设备在, 只是
//   该地址无效); 仅超时/连接错判离线。
//   readDone 为广播信号: 本类按 (fc=3, slave=在途地址, addr=0) 自认领,
//   与同一 MasterClient 上的 DataViewWidget 轮询共存 (互不干扰, 双认领无害)。
//   门控: 连接断开 → 全部从机转离线并停表; setPaused 供扫描重定向期间挂起。
// ============================================================================

#include <QList>
#include <QObject>
#include <QSet>

class MasterClient;
class QTimer;

class SlavePoller : public QObject
{
    Q_OBJECT
public:
    explicit SlavePoller(MasterClient *mb, QObject *parent = nullptr);

    // 地址列表 (升序去重); 空表 = 停止轮询, 移除地址的在线态一并清除
    void setAddresses(const QList<int> &addresses);
    QList<int> addresses() const { return m_addresses; }

    void setIntervalMs(int ms); // 两次探测的调度间隔 (实际节律 = max(间隔, 单台应答耗时))
    int intervalMs() const { return m_intervalMs; }

    void setPaused(bool on); // 外部挂起 (扫描重定向期间); 恢复后继续轮转
    bool isPaused() const { return m_paused; }

    bool isOnline(int address) const { return m_online.contains(address); }

signals:
    void slaveOnlineChanged(int address, bool online); // 状态翻转时发射
    void slaveProbed(int address, bool online, double elapsedMs); // 每次探测结果

private slots:
    void probeNext();
    void onReadDone(int fc, int slave, quint16 addr, bool ok,
                    const QVector<quint16> &values, const QString &errText,
                    double elapsedMs);
    void onConnChanged(bool connected);

private:
    void updateTimer();
    void setOnline(int address, bool online);

    MasterClient *m_mb = nullptr;
    QTimer *m_timer = nullptr;
    QList<int> m_addresses;
    QSet<int> m_online;
    int m_cursor = 0;        // 下一个探测的地址下标
    bool m_inFlight = false; // 在途守卫 (超时未答时跳 tick, 不重发)
    int m_flightSlave = -1;  // 在途探测的从机地址 (readDone 自认领)
    bool m_paused = false;
    bool m_connected = false;
    int m_intervalMs = 200;
};
