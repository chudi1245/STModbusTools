// ============================================================================
// FrameLogDialog.cpp — 帧日志窗口实现
// ============================================================================

#include "FrameLogDialog.h"

#include "ModbusClient.h"
#include "Theme.h"

#include <QCheckBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace
{
// 帧展示: RTU = 线上完整帧 [从站地址][PDU][CRC] (CRC 按帧内容本地补算);
// TCP = 线上完整帧 [事务ID][协议ID][长度][单元ID][PDU] (MBAP 头按本地镜像补全);
// 无线上字节 (超时等) 返回占位
QString frameHex(const FrameRecord &fr)
{
    if (fr.pdu.isEmpty())
        return FrameLogDialog::tr("— (无线上字节)");
    const QByteArray shown = fr.isRtu ? commRtuAdu(fr.addr, fr.pdu)
                                      : commTcpAdu(fr.mbapTid, fr.addr, fr.pdu);
    return QString::fromLatin1(shown.toHex(' ').toUpper());
}

// PDU → ASCII 侧栏 (不可打印字符作 '.'); 控制字节 (地址/CRC) 不参与, 只看数据
QString pduAscii(const QByteArray &pdu)
{
    QString s;
    s.reserve(pdu.size());
    for (char c : pdu)
        s += (c >= 0x20 && c < 0x7F) ? QChar(c) : QChar('.');
    return s.isEmpty() ? QStringLiteral("—") : s;
}
} // namespace

FrameLogDialog::FrameLogDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("通信帧日志"));
    resize(1280, 560);
    setAttribute(Qt::WA_DeleteOnClose, false); // 常驻缓冲, 关闭仅隐藏

    m_table = new QTableWidget;
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->horizontalHeader()->setStretchLastSection(true);
    const QStringList headers = { tr("时间"), tr("方向"), tr("数据帧 (hex)"),
                                  tr("ASCII"), tr("耗时 ms"), tr("备注") };
    m_table->setColumnCount(headers.size());
    m_table->setHorizontalHeaderLabels(headers);
    m_table->setColumnWidth(0, 95);
    m_table->setColumnWidth(1, 45);
    // 数据帧列加宽: 完整帧 (RTU 地址+CRC / TCP MBAP) 是客户调试要看的核心内容
    m_table->setColumnWidth(2, 700);
    m_table->setColumnWidth(3, 120);
    m_table->setColumnWidth(4, 65);

    m_chkPause = new QCheckBox(tr("暂停滚动 (仍入缓冲)"));
    m_chkErrOnly = new QCheckBox(tr("仅错误"));
    m_chkStopOnError = new QCheckBox(tr("遇错自动暂停"));
    connect(m_chkPause, &QCheckBox::toggled, this, [this](bool on) {
        m_paused = on;
        if (!on)
            rebuildTable(); // 恢复滚动: 补齐暂停期间缓冲里的帧
    });
    connect(m_chkErrOnly, &QCheckBox::toggled, this, [this](bool) { rebuildTable(); });
    auto *btnClear = new QPushButton(tr("清空"));
    btnClear->setProperty("btn", Theme::BtnGhost);
    connect(btnClear, &QPushButton::clicked, this, [this] {
        m_frames.clear();
        rebuildTable();
    });
    auto *btnExport = new QPushButton(tr("导出 CSV"));
    btnExport->setProperty("btn", Theme::BtnSecondary);
    connect(btnExport, &QPushButton::clicked, this, &FrameLogDialog::exportCsv);

    auto *bar = new QHBoxLayout;
    bar->addWidget(m_chkPause);
    bar->addWidget(m_chkStopOnError);
    bar->addWidget(m_chkErrOnly);
    bar->addStretch();
    bar->addWidget(btnClear);
    bar->addWidget(btnExport);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(Theme::SpaceM, Theme::SpaceM, Theme::SpaceM, Theme::SpaceM);
    auto *hint = new QLabel(
        tr("帧为线上完整帧 — RTU: 从站地址+功能码+数据+CRC (CRC 本地补算); "
           "TCP: 事务ID+协议ID+长度+单元ID+PDU; ASCII 列仅 PDU 数据"));
    hint->setStyleSheet(QStringLiteral("color:%1; font-size:%2px;")
                            .arg(Theme::Text2)
                            .arg(Theme::FontAux));
    lay->addWidget(hint);
    lay->addLayout(bar);
    lay->addWidget(m_table);
}

FrameLogDialog::FrameLogDialog(ModbusClient *mb, QWidget *parent)
    : FrameLogDialog(parent)
{
    if (mb)
        connect(mb, &ModbusClient::frameLogged, this, &FrameLogDialog::appendFrame);
}

void FrameLogDialog::appendFrame(const FrameRecord &fr)
{
    m_frames.append(fr);
    while (m_frames.size() > kRingMax)
        m_frames.removeFirst();
    if (!m_paused)
        appendRow(fr);
    // 遇错自动暂停: 错误帧已入表 (证据可见), 随即冻结滚动 (勾选框同步)
    if (m_chkStopOnError->isChecked() && fr.isErr && !m_paused)
        m_chkPause->setChecked(true);
}

void FrameLogDialog::appendRow(const FrameRecord &fr)
{
    if (m_chkErrOnly->isChecked() && !fr.isErr)
        return; // 仅错误过滤激活时的实时追加与重建共用本入口
    // 表行数与缓冲对齐 (暂停期间缓冲继续增长, 恢复时 rebuildTable 补齐)
    if (m_table->rowCount() >= kRingMax)
        m_table->removeRow(0);
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    auto *c0 = new QTableWidgetItem(
        QDateTime::fromMSecsSinceEpoch(fr.tMs).toString(QStringLiteral("HH:mm:ss.zzz")));
    c0->setForeground(QColor(Theme::Text2));
    m_table->setItem(row, 0, c0);
    auto *c1 = new QTableWidgetItem(fr.isTx ? tr("TX") : tr("RX"));
    c1->setForeground(QColor(fr.isTx ? Theme::Ok : Theme::Accent));
    m_table->setItem(row, 1, c1);
    auto *c2 = new QTableWidgetItem(frameHex(fr));
    if (fr.isErr)
        c2->setForeground(QColor(Theme::Danger));
    m_table->setItem(row, 2, c2);
    m_table->setItem(row, 3, new QTableWidgetItem(pduAscii(fr.pdu)));
    m_table->setItem(row, 4,
                     new QTableWidgetItem(fr.elapsedMs > 0
                                              ? QString::number(fr.elapsedMs, 'f', 1)
                                              : QString()));
    m_table->setItem(row, 5, new QTableWidgetItem(fr.note));
    m_table->scrollToBottom();
}

void FrameLogDialog::rebuildTable()
{
    m_table->setRowCount(0);
    for (const auto &fr : m_frames)
        appendRow(fr); // 错误过滤在 appendRow 内统一处理
}

void FrameLogDialog::exportCsv()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("导出帧日志"), QStringLiteral("framelog.csv"),
        tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("导出失败"), f.errorString());
        return;
    }
    // Qt6 无 setGenerateBom: 手写 BOM, 保证 Excel 识别 UTF-8 (对齐 CaptureExport)
    f.write("\xEF\xBB\xBF");
    f.write("time,dir,slave,err,hex,elapsed_ms,note\n");
    for (const auto &fr : m_frames)
    {
        const QString line = QStringLiteral("%1,%2,%3,%4,\"%5\",%6,\"%7\"\n")
                                 .arg(QDateTime::fromMSecsSinceEpoch(fr.tMs)
                                          .toString(Qt::ISODateWithMs),
                                      fr.isTx ? "TX" : "RX", QString::number(fr.addr),
                                      fr.isErr ? "1" : "0", frameHex(fr),
                                      QString::number(fr.elapsedMs, 'f', 3),
                                      QString(fr.note).replace('"', "'"));
        f.write(line.toUtf8());
    }
    QMessageBox::information(this, tr("导出完成"),
                             tr("已导出 %1 条帧记录").arg(m_frames.size()));
}
