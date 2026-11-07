// ============================================================================
// CommStatsCollector.cpp — 滚动窗口通信统计实现
// ============================================================================

#include "CommStatsCollector.h"

#include "ModbusClient.h"

#include <algorithm>
#include <cmath>

namespace
{
// 响应时间分布桶 (ms); 末桶开放上界
constexpr struct
{
    double lo, hi;
} kBucketEdges[] = {
    {0, 10}, {10, 20}, {20, 50}, {50, 100}, {100, 200}, {200, 500}, {500, -1}};
constexpr int kBucketN = 7;
} // namespace

CommStatsCollector::CommStatsCollector(QObject *parent)
    : QObject(parent)
{
}

void CommStatsCollector::attach(ModbusClient *mb)
{
    connect(mb, &ModbusClient::transactionDone, this, &CommStatsCollector::addTxn);
}

void CommStatsCollector::addTxn(const TxnRecord &rec)
{
    m_samples.append(Sample { rec.tMs, rec.elapsedMs, rec.err });
    while (m_samples.size() > kMaxSamples)
        m_samples.removeFirst();
    m_recent.append(rec);
    while (m_recent.size() > kRecentMax)
        m_recent.removeFirst();
    prune(rec.tMs);
    emit updated();
}

void CommStatsCollector::setWindowSec(int s)
{
    m_windowSec = s > 0 ? s : 300;
}

void CommStatsCollector::prune(qint64 nowMs)
{
    const qint64 cutoff = nowMs - qint64(m_windowSec) * 1000;
    while (!m_samples.isEmpty() && m_samples.front().tMs < cutoff)
        m_samples.removeFirst();
    while (!m_recent.isEmpty() && m_recent.front().tMs < cutoff)
        m_recent.removeFirst();
}

CommStatsCollector::Report CommStatsCollector::report()
{
    Report r;
    r.windowSec = m_windowSec;
    r.histogram.resize(kBucketN);
    for (int i = 0; i < kBucketN; ++i)
    {
        r.histogram[i].loMs = kBucketEdges[i].lo;
        r.histogram[i].hiMs = kBucketEdges[i].hi;
    }
    QVector<double> latencies;
    latencies.reserve(m_samples.size());
    for (const Sample &s : m_samples)
    {
        ++r.total;
        switch (s.err)
        {
        case static_cast<int>(TxnErr::Ok): ++r.ok; break;
        case static_cast<int>(TxnErr::Timeout): ++r.timeout; break;
        case static_cast<int>(TxnErr::Protocol): ++r.protoErr; break;
        case static_cast<int>(TxnErr::Connection): ++r.connErr; break;
        default: ++r.dropped; break;
        }
        if (s.elapsedMs > 0)
            latencies.append(s.elapsedMs);
    }
    if (r.total > 0)
    {
        r.okRate = double(r.ok) / double(r.total);
        r.timeoutRate = double(r.timeout) / double(r.total);
    }
    if (!latencies.isEmpty())
    {
        std::sort(latencies.begin(), latencies.end());
        r.latencyN = latencies.size();
        r.respMin = latencies.front();
        r.respMax = latencies.back();
        double sum = 0;
        for (double v : latencies)
            sum += v;
        r.respMean = sum / r.latencyN;
        const auto pick = [&latencies](double p) {
            // 最近邻秩: ceil(p*n)-1, 钳到表内
            const int idx = qBound(0, int(std::ceil(p * latencies.size())) - 1,
                                   latencies.size() - 1);
            return latencies[idx];
        };
        r.respP50 = pick(0.50);
        r.respP95 = pick(0.95);
        r.respP99 = pick(0.99);
        for (double v : latencies)
        {
            for (int i = kBucketN - 1; i >= 0; --i)
            {
                if (v >= kBucketEdges[i].lo)
                {
                    ++r.histogram[i].count;
                    break;
                }
            }
        }
    }
    return r;
}

QVector<TxnRecord> CommStatsCollector::recent(int maxN) const
{
    const int n = qBound(0, maxN, m_recent.size());
    QVector<TxnRecord> out(m_recent.end() - n, m_recent.end());
    return out;
}

void CommStatsCollector::reset()
{
    m_samples.clear();
    m_recent.clear();
}
