#pragma once

// ============================================================================
// FrameLogDialog.h — 通信帧日志独立窗口 (调试台 通信体检 → [帧日志…])
//   帧展示为线上完整帧: RTU = 地址+PDU+CRC 补全; TCP = MBAP 头+PDU (CommTypes.h);
//   环形缓冲 5000 条常驻 (后台持续入缓冲, 打开即有近期历史);
//   暂停滚动 (仍入缓冲) / 仅错误过滤 / 遇错自动暂停 / 清空 / 导出 CSV
// ============================================================================

#include "CommTypes.h"

#include <QDialog>
#include <QList>

class QCheckBox;
class QLabel;
class QTableWidget;
class ModbusClient;

class FrameLogDialog : public QDialog
{
    Q_OBJECT
public:
    // 不绑数据源: 调用方自行 connect(frameSource, frameLogged, appendFrame)
    // (调试工具的 MasterClient 等任意 FrameRecord 发射方均可复用本窗口)
    explicit FrameLogDialog(QWidget *parent = nullptr);
    explicit FrameLogDialog(ModbusClient *mb, QWidget *parent = nullptr);

public slots:
    void appendFrame(const FrameRecord &fr); // ModbusClient::frameLogged 订阅入口

private slots:
    void rebuildTable();
    void exportCsv();

private:
    void appendRow(const FrameRecord &fr);

    QList<FrameRecord> m_frames; // 环形 (kRingMax 封顶, 淘汰最旧)
    QTableWidget *m_table = nullptr;
    QCheckBox *m_chkPause = nullptr;       // 暂停滚动 (仍入缓冲)
    QCheckBox *m_chkErrOnly = nullptr;     // 仅错误帧
    QCheckBox *m_chkStopOnError = nullptr; // 遇错自动暂停
    bool m_paused = false;                 // 与 m_chkPause 同步 (遇错自动暂停置位)

    static constexpr int kRingMax = 5000;
};
