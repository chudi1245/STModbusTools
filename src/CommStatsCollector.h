#pragma once

// ============================================================================
// CommStatsCollector.h — 通信质量聚合器 (滚动窗口统计 + 响应时间分布)
//   订阅 ModbusClient::transactionDone (attach, 组合根接线); 测试可直喂 addTxn。
//   口径: okRate/timeoutRate 分母 = 窗口内全部事务 (含被丢弃);
//         响应时间分位/直方图只计实际上线事务 (elapsedMs > 0)。
//   调研依据: 主流 Modbus 工具无逐事务响应时间统计 — 行业空白 (doc/功能设计方案)。
// ============================================================================

#include "CommTypes.h"

#include <QList>
#include <QObject>
#include <QVector>

class ModbusClient;

class CommStatsCollector : public QObject
{
    Q_OBJECT
public:
    explicit CommStatsCollector(QObject *parent = nullptr);

    void attach(ModbusClient *mb);       // 订阅事务流 (仅组合根调用)
    void addTxn(const TxnRecord &rec);   // 数据入口 (attach 转发至此; 测试直喂)
    void setWindowSec(int s);
    int windowSec() const { return m_windowSec; }

    struct Bucket
    {
        double loMs = 0, hiMs = 0; // hiMs < 0 = 开放上界 (500+)
        int count = 0;
    };

    struct Report
    {
        int windowSec = 300;
        qint64 total = 0, ok = 0, timeout = 0, protoErr = 0, connErr = 0, dropped = 0;
        double okRate = 0, timeoutRate = 0; // 0..1
        int latencyN = 0;                   // 上线事务数 (分位/直方图样本量)
        double respMin = 0, respMax = 0, respMean = 0;
        double respP50 = 0, respP95 = 0, respP99 = 0;
        QVector<Bucket> histogram;
    };

    Report report();                        // 非 const: 惰性清理窗外样本
    QVector<TxnRecord> recent(int maxN) const; // 最近事务 (新在后, 事务表数据源)
    void reset();

signals:
    void updated();                         // 新事务到达 (UI 可据此节流刷新)

private:
    struct Sample
    {
        qint64 tMs = 0;
        double elapsedMs = 0;
        int err = 0;
    };
    void prune(qint64 nowMs);               // 淘汰窗外最旧样本

    QList<Sample> m_samples;
    QList<TxnRecord> m_recent;              // 最近事务环形 (含完整记录)
    int m_windowSec = 300;
    static constexpr int kMaxSamples = 10000;
    static constexpr int kRecentMax = 500;
};
