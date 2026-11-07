// ============================================================================
// DataViewWidget.cpp — 数据窗口体实现 (扫描/写窗口/写值对话框/记录)
//   多段轮询链: 一个扫描周期 = 按段顺序读完所有段 (同一时刻仅一段在途),
//   段应答按序链发下一段, 末段完成 → 状态/采样/记录; 中途出错中止本轮
// ============================================================================

#include "DataViewWidget.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTableView>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

DataViewWidget::DataViewWidget(MasterClient *mb, QWidget *parent)
    : QWidget(parent)
    , m_mb(mb)
{
    // 定时器先于 buildUi: applyDefinition→updatePolling 即会触到 m_poll
    m_poll = new QTimer(this);
    m_poll->setTimerType(Qt::CoarseTimer);
    connect(m_poll, &QTimer::timeout, this, &DataViewWidget::pollOnce);

    buildUi();

    connect(m_model, &PollTableModel::nameEdited, this,
            [this](int row, const QString &text) {
                if (m_def.nameCells.size() < m_def.totalQty())
                    m_def.nameCells.resize(m_def.totalQty());
                m_def.nameCells[row] = text;
                emit definitionAutoChanged(m_def); // 主窗同步工作区数据
            });
    connect(m_mb, &MasterClient::readDone, this, &DataViewWidget::onReadDone);
    connect(m_mb, &MasterClient::writeDone, this, &DataViewWidget::onWriteDone);
    // 连接态自订阅: 窗口与连接一对一绑定 (多连接并存), 不再由主窗广播下发;
    // setConnected 保留公开 (测试/手动注入)
    connect(m_mb, &MasterClient::connectionChanged, this,
            [this](bool on) { setConnected(on); });
    setConnected(m_mb->isConnected());

    m_logTimer = new QTimer(this);
    connect(m_logTimer, &QTimer::timeout, this, [this] {
        writeLogRow(m_model->rowCount() > 0
                        ? m_model->index(0, PollTableModel::ColValue)
                              .data(Qt::DisplayRole)
                              .toString()
                        : QString(),
                    false, QString());
    });
}

void DataViewWidget::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_model = new PollTableModel(this);
    m_table = new QTableView;
    m_table->setObjectName(QStringLiteral("mdef.table"));
    m_table->setModel(m_model);
    // 几何 (行高/列宽/行头宽) 统一由 applyDefinition 按 m_def 下发;
    // 交互调整优先: 两个表头恒 Interactive — 拖动边界即改即存 (回写见下
    // sectionResized), 双击边界 = 自适应内容; 值列不 stretch, 列宽显式管理
    m_table->verticalHeader()->setMinimumWidth(64);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->verticalHeader()->setMinimumSectionSize(14); // 可拖至紧凑行高
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableView::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                QMenu menu(m_table);
                if (!m_def.isWriteFc() && !m_def.readOnly())
                {
                    menu.addAction(tr("写入选中…"), this, [this] { writeSelected(); });
                    menu.addSeparator();
                }
                menu.addAction(tr("单元格格式…"), this, [this] {
                    // 初值 = 首选中行有效格式 (无覆盖 = 窗口级默认)
                    const QVector<int> rows = selectedRows();
                    ModbusFormat::Fmt f = m_def.fmt;
                    ModbusFormat::ByteOrder o = m_def.order;
                    if (!rows.isEmpty())
                    {
                        auto it = m_def.rowStyles.constFind(rows.front());
                        if (it != m_def.rowStyles.constEnd() && it->hasFmt)
                        {
                            f = it->fmt;
                            o = it->order;
                        }
                    }
                    if (DefDialogs::cellFormatDialog(this, &f, &o))
                        applyDisplayFormat(f, o);
                });
                menu.addAction(tr("单元格样式…"), this, [this] { editCellLook(); });
                menu.addAction(tr("缩放与前缀/后缀…"), this,
                               [this] { editScale(); }); // 作用域同格式: 选中行/全窗口
                menu.addAction(tr("清除单元格显示设置"), this,
                               [this] { clearCellStyles(); });
                menu.addSeparator();
                menu.addAction(tr("粘贴别名 (从此行)"), this, [this] {
                    const int row = m_table->currentIndex().row();
                    if (row < 0)
                        return;
                    const QStringList lines = QApplication::clipboard()
                                                  ->text()
                                                  .split(QLatin1Char('\n'),
                                                         Qt::SkipEmptyParts);
                    if (m_def.nameCells.size() < m_def.totalQty())
                        m_def.nameCells.resize(m_def.totalQty());
                    for (int i = 0; i < lines.size() && row + i < m_def.totalQty(); ++i)
                        m_def.nameCells[row + i] = lines[i].trimmed();
                    applyDefinition();
                    syncDefToMainWindow();
                });
                menu.addAction(tr("复制全表"), this, [this] {
                    QStringList lines;
                    for (int r = 0; r < m_model->rowCount(); ++r)
                    {
                        lines << QStringLiteral("%1\t%2\t%3")
                                     .arg(m_model->addrText(r),
                                          m_model->index(r, PollTableModel::ColValue)
                                              .data(Qt::DisplayRole)
                                              .toString(),
                                          m_model->index(r, PollTableModel::ColName)
                                              .data(Qt::DisplayRole)
                                              .toString());
                    }
                    QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
                });
                menu.exec(m_table->viewport()->mapToGlobal(pos));
            });
    // 读窗口: 双击值单元格 → 写值对话框 (写窗口走内联编辑, 不拦)
    connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex &idx) {
        if (!m_def.isWriteFc() && !m_def.readOnly()
            && idx.column() == PollTableModel::ColValue)
        {
            if (m_model->isCovered(idx.row()))
            {
                // 被上一行宽格式 (32/64 位) 占用: 显示 "-", 禁止写入
                emit statusMessage(tr("该寄存器已被上一行宽格式占用, 不可单独写入"), false);
                return;
            }
            openWriteDialog(idx.row());
        }
    });
    // 行选择: 单击任意列选中整行 (格式/写入作用于行; 单击值列也计入选中)
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    // 交互拖拽持久化 (与"单元格样式…"对话框同一份数据语义):
    //   列宽 → 窗口级字段; 行高 → 该行覆盖; 行头宽 → 窗口级字段;
    //   回写值与序列化同一套边界钳制
    connect(m_table->horizontalHeader(), &QHeaderView::sectionResized, this,
            [this](int col, int, int w) {
                if (m_applyingDef)
                    return;
                if (col == PollTableModel::ColName)
                    m_def.nameColW = qBound(40, w, 4000);
                else if (col == PollTableModel::ColValue)
                    m_def.valueColW = qBound(40, w, 4000);
                else
                    return;
                syncDefToMainWindow();
            });
    connect(m_table->verticalHeader(), &QHeaderView::sectionResized, this,
            [this](int row, int, int h) {
                if (m_applyingDef || row < 0 || row >= m_model->rowCount())
                    return;
                PollDefinition::RowStyle &st = m_def.rowStyles[row];
                st.hasHeight = true;
                st.rowHeight = qBound(14, h, 200);
                m_customHeightRows.insert(row);
                syncDefToMainWindow();
            });
    // 双击 行/列 边界 → 自适应内容 (触发的 sectionResized 走同一回写)
    connect(m_table->verticalHeader(), &QHeaderView::sectionHandleDoubleClicked,
            this, [this](int row) { m_table->resizeRowToContents(row); });
    connect(m_table->horizontalHeader(), &QHeaderView::sectionHandleDoubleClicked,
            this, [this](int col) { m_table->resizeColumnToContents(col); });
    m_table->verticalHeader()->installEventFilter(this);
    root->addWidget(m_table, 1);

    applyDefinition();
}

void DataViewWidget::setDefinition(const PollDefinition &d)
{
    m_def = d;
    m_def.clamp();
    applyDefinition();
}

void DataViewWidget::applyDefinition()
{
    // 模型重建与几何下发都会触发 sectionResized — 整段抑制回写
    m_applyingDef = true;
    m_model->setDefinition(m_def);
    m_model->setPlcAddr(m_def.plcBase1);
    m_model->setAddrInCell(m_def.addrInCell);
    m_table->setColumnHidden(PollTableModel::ColName, m_def.hideNames);
    // 双击/F2 触发行内编辑; 列级可编辑性由模型 flags 决定:
    //   别名列所有窗口可重命名; 值列仅写窗口 (读窗口双击 → 写值对话框)
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked
                             | QAbstractItemView::EditKeyPressed);
    // 外观几何: 默认行高 + 行级覆盖行高 + 两列列宽 + 行头宽;
    // 模型重建不清除旧行高 — 先把上次的自定义行高行落回默认再下发
    m_table->verticalHeader()->setDefaultSectionSize(m_def.rowHeight);
    const int n = m_model->rowCount();
    for (int r : m_customHeightRows)
        if (r < n)
            m_table->setRowHeight(r, m_def.rowHeight);
    m_customHeightRows.clear();
    for (auto it = m_def.rowStyles.cbegin(); it != m_def.rowStyles.cend(); ++it)
    {
        if (it->hasHeight && it.key() < n)
        {
            m_table->setRowHeight(it.key(), it->rowHeight);
            m_customHeightRows.insert(it.key());
        }
    }
    m_table->setColumnWidth(PollTableModel::ColName, m_def.nameColW);
    m_table->setColumnWidth(PollTableModel::ColValue, m_def.valueColW);
    if (m_def.vHeaderW > 0)
        m_table->verticalHeader()->resize(m_def.vHeaderW,
                                          m_table->verticalHeader()->height());
    m_applyingDef = false;
    updatePolling();
}

void DataViewWidget::setConnected(bool on)
{
    m_connected = on;
    updatePolling();
    if (!on)
        emit statusMessage(tr("未连接"), false);
}

void DataViewWidget::setPollPaused(bool on)
{
    m_pollPaused = on;
    updatePolling();
}

void DataViewWidget::setPlcAddr(bool on)
{
    m_model->setPlcAddr(on);
}

void DataViewWidget::setAddrInCell(bool on)
{
    m_model->setAddrInCell(on);
}

void DataViewWidget::updatePolling()
{
    const bool run = m_connected && m_def.enabled && !m_pollPaused && m_def.scanRateMs > 0;
    if (run)
    {
        if (m_poll->interval() != m_def.scanRateMs || !m_poll->isActive())
            m_poll->start(m_def.scanRateMs);
    }
    else
    {
        m_poll->stop();
    }
}

void DataViewWidget::readWriteOnce()
{
    pollOnce(); // F6: 读窗口读一次 / 写窗口写一次 (不受 启用/暂停 限制)
}

void DataViewWidget::pollOnce()
{
    if (!m_connected)
    {
        emit statusMessage(tr("未连接 — 无法执行"), false);
        return;
    }
    if (m_inFlight)
        return; // 在途守卫: 上一事务未回, 跳过本 tick (防积压)
    if (m_def.isWriteFc())
    {
        // 写窗口: 把网格显示的数据写入设备 ("写功能写的是网格上的数据")
        const QVector<quint16> regs = m_model->regs();
        bool sent = false;
        switch (m_def.fc)
        {
        case 5:
            sent = m_mb->writeCoils(m_def.slave, m_def.addr,
                                    QVector<quint8>{regs.value(0) ? quint8(1) : quint8(0)});
            break;
        case 6:
            sent = m_mb->writeRegs(m_def.slave, m_def.addr, regs.mid(0, 1));
            break;
        case 15:
        {
            QVector<quint8> bits;
            for (quint16 v : regs)
                bits.append(v ? 1 : 0);
            sent = m_mb->writeCoils(m_def.slave, m_def.addr, bits);
            break;
        }
        default:
            sent = m_mb->writeRegs(m_def.slave, m_def.addr, regs, /*forceMulti*/ true);
            break;
        }
        if (sent)
            m_inFlight = true;
        return;
    }
    // 读窗口: 本轮从第 1 段开始, onReadDone 成功后链发下一段
    const QVector<PollDefinition::Segment> segs = m_def.segments();
    if (segs.isEmpty())
        return;
    m_cycleMs = 0.0;
    m_flightSeg = 0;
    if (m_mb->read(m_def.fc, m_def.slave, segs[0].addr, quint16(segs[0].qty)))
        m_inFlight = true;
    else
        m_flightSeg = -1;
}

void DataViewWidget::onReadDone(int fc, int slave, quint16 addr, bool ok,
                                const QVector<quint16> &values, const QString &errText,
                                double elapsedMs)
{
    m_inFlight = false;
    const QVector<PollDefinition::Segment> segs = m_def.segments();
    const int fSeg = m_flightSeg < 0 ? 0 : qBound(0, m_flightSeg, segs.size() - 1);
    // 迟到/失配应答 (定义已改): 只清守卫, 不上屏 — 与在途段比对
    if (fc != m_def.fc || slave != m_def.slave || segs.isEmpty()
        || addr != segs[fSeg].addr)
    {
        m_flightSeg = -1;
        return;
    }
    m_cycleMs += elapsedMs;
    if (!ok)
    {
        m_flightSeg = -1;
        emit statusMessage(tr("%1 · 错误 · %2").arg(m_def.name, errText), false);
        if (m_logFile && m_logCfg.logErrors)
            writeLogRow(QString(), true, errText);
        if (m_def.autoStopOnError && m_def.enabled)
        {
            m_def.enabled = false;
            updatePolling();
            syncDefToMainWindow();
            emit statusMessage(tr("「%1」出错自动停轮询").arg(m_def.name), false);
        }
        return;
    }
    int rowStart = 0;
    for (int i = 0; i < fSeg; ++i)
        rowStart += segs[i].qty;
    m_model->setRegs(rowStart, values); // 段内数据上屏 (未读段保持旧值)
    const bool last = fSeg >= segs.size() - 1;
    if (!last)
    {
        const int next = fSeg + 1;
        if (m_mb->read(m_def.fc, m_def.slave, segs[next].addr, quint16(segs[next].qty)))
        {
            m_flightSeg = next;
            m_inFlight = true;
            return; // 本轮未完, 等下一段应答
        }
        // 链发失败 (连接断开等): 中止本轮
        m_flightSeg = -1;
        emit statusMessage(tr("%1 · 错误 · 第 %2 段发送失败")
                               .arg(m_def.name)
                               .arg(next + 1),
                           false);
        return;
    }
    m_flightSeg = -1;
    // 本轮完成: 状态 + perRead 日志
    emit statusMessage(tr("%1 · OK · %2 ms · %3 项")
                           .arg(m_def.name, QString::number(m_cycleMs, 'f', 1))
                           .arg(m_model->rowCount()),
                       true);
    if (m_logFile && m_logCfg.perRead)
    {
        QStringList vals;
        for (int r = 0; r < m_model->rowCount(); ++r)
            vals << m_model->index(r, PollTableModel::ColValue).data(Qt::DisplayRole).toString();
        writeLogRow(vals.join(QLatin1Char(',')), false, QString());
    }
}

void DataViewWidget::onWriteDone(int slave, quint16 addr, int count, bool ok,
                                 const QString &errText, double elapsedMs)
{
    // 写值/批量写对话框在途: 结果路由给对话框 (OK+勾选时自动关)
    if (m_writeDlg)
    {
        if (slave == m_writeDlgSlave && addr == m_writeDlgAddr)
        {
            if (m_writeResult)
                m_writeResult(ok, ok ? QStringLiteral("%1 ms")
                                           .arg(QString::number(elapsedMs, 'f', 1))
                                     : tr("写入失败 · %1").arg(errText));
            m_writeDlg = nullptr;
        }
        return;
    }
    // 写窗口周期写: 网格即数据源, 只报状态
    if (m_def.isWriteFc())
    {
        if (slave != m_def.slave || addr != m_def.addr)
            return;
        m_inFlight = false;
        emit statusMessage(ok ? tr("%1 · 写入 OK · %2 ms")
                                    .arg(m_def.name, QString::number(elapsedMs, 'f', 1))
                              : tr("%1 · 写入失败 · %2").arg(m_def.name, errText),
                            ok);
        if (!ok && m_def.autoStopOnError && m_def.enabled)
        {
            m_def.enabled = false;
            updatePolling();
            syncDefToMainWindow();
        }
        return;
    }
    // 读窗口对话框写 (对话框非自动关路径): 地址落在任一段内即接受 (多段)
    if (slave != m_def.slave || m_def.rowForAddr(addr) < 0)
        return;
    emit statusMessage(ok ? tr("%1 · 写入 OK · 地址 %2 ×%3 · %4 ms")
                                .arg(m_def.name)
                                .arg(addr)
                                .arg(count)
                                .arg(QString::number(elapsedMs, 'f', 1))
                          : tr("%1 · 写入失败 · %2").arg(m_def.name, errText),
                        ok);
}

void DataViewWidget::openWriteDialog(int row)
{
    const int total = m_def.totalQty();
    if (m_def.readOnly() || row < 0 || row >= total)
        return;
    // 被上一行宽格式 (32/64 位) 占用: 显示 "-", 禁止单独写值
    if (m_model->isCovered(row))
    {
        emit statusMessage(tr("该寄存器已被上一行宽格式占用, 不可单独写入"), false);
        return;
    }
    // 该行到所在段末 (多段写不跨段)
    const QVector<PollDefinition::Segment> segs = m_def.segments();
    int avail = total - row;
    int base = 0;
    for (const PollDefinition::Segment &s : segs)
    {
        if (row < base + s.qty)
        {
            avail = base + s.qty - row;
            break;
        }
        base += s.qty;
    }
    QVector<quint16> tail;
    for (int r = row; r < row + avail; ++r)
        tail.append(m_model->regs()[r]);
    const int a = m_def.rowAddr(row);
    // 对话框解析用首行有效显示格式 (行覆盖优先)
    PollDefinition dview = m_def;
    {
        auto it = m_def.rowStyles.constFind(row);
        if (it != m_def.rowStyles.constEnd() && it->hasFmt)
        {
            dview.fmt = it->fmt;
            dview.order = it->order;
        }
    }
    auto *dlg = new DefDialogs::WriteCellDialog(this, dview, row,
                                                m_model->addrText(row), tail);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    m_writeDlg = dlg;
    m_writeDlgSlave = m_def.slave;
    m_writeDlgAddr = quint16(a < 0 ? 0 : a);
    connect(dlg->writeBtn(), &QPushButton::clicked, this, [this, dlg, row, a] {
        int fcCode = 6;
        QVector<quint16> vals;
        if (!dlg->takeValues(&fcCode, &vals))
            return;
        dlg->writeBtn()->setEnabled(false);
        if (fcCode == 5 || fcCode == 6)
        {
            // 单点写: 应答 OK 后乐观上屏
            m_pendingRow = row;
            m_pendingVals = vals;
        }
        else
        {
            m_pendingRow = -1;
        }
        if (fcCode == 5 || fcCode == 15)
        {
            QVector<quint8> bits;
            for (quint16 v : vals)
                bits.append(v ? 1 : 0);
            m_mb->writeCoils(m_def.slave, quint16(a), bits);
        }
        else
        {
            m_mb->writeRegs(m_def.slave, quint16(a), vals,
                            /*forceMulti*/ m_def.fc == 16);
        }
        // 应答路由: onWriteDone 里按 m_writeDlgSlave/m_writeDlgAddr 匹配
    });
    connect(dlg, &QDialog::finished, this, [this]() { m_writeDlg = nullptr; });
    m_writeResult = [this, dlg](bool ok, const QString &info) {
        dlg->showResult(ok, info);
        if (ok && m_pendingRow >= 0 && m_pendingVals.size() >= 1)
            m_model->setRegs(m_pendingRow, m_pendingVals);
        m_pendingRow = -1;
    };
    dlg->show();
}

void DataViewWidget::writeSelected()
{
    const QModelIndexList sel =
        m_table->selectionModel()->selectedIndexes();
    if (sel.isEmpty())
        return;
    QVector<int> rows;
    for (const auto &ix : sel)
        rows.append(ix.row());
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    // 被上一行宽格式占用的行: 显示 "-", 跳过写入
    for (int i = rows.size() - 1; i >= 0; --i)
        if (m_model->isCovered(rows[i]))
            rows.remove(i);
    if (rows.isEmpty())
        return;
    const int first = rows.front();
    const int total = m_def.totalQty();
    // 写范围止于 first 所在段末 (多段不跨段)
    const QVector<PollDefinition::Segment> segs = m_def.segments();
    int avail = total - first;
    int base = 0;
    for (const PollDefinition::Segment &s : segs)
    {
        if (first < base + s.qty)
        {
            avail = base + s.qty - first;
            break;
        }
        base += s.qty;
    }
    const int span = qMin(int(rows.size()), avail);
    if (span <= 0)
        return;
    QVector<quint16> cur;
    for (int i = 0; i < span; ++i)
        cur.append(m_model->regs().value(first + i));
    const int a = m_def.rowAddr(first);
    // 显示格式取首行有效格式 (行覆盖优先)
    ModbusFormat::Fmt dispFmt = m_def.fmt;
    ModbusFormat::ByteOrder dispOrd = m_def.order;
    {
        auto it = m_def.rowStyles.constFind(first);
        if (it != m_def.rowStyles.constEnd() && it->hasFmt)
        {
            dispFmt = it->fmt;
            dispOrd = it->order;
        }
    }
    auto *dlg = new DefDialogs::MultiWriteDialog(this, m_def, quint16(a < 0 ? 0 : a),
                                                 cur, dispFmt, dispOrd);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    m_writeDlg = dlg;
    m_writeDlgSlave = m_def.slave;
    m_writeDlgAddr = quint16(a < 0 ? 0 : a);
    connect(dlg->writeBtn(), &QPushButton::clicked, this, [this, dlg, first, a] {
        QVector<quint16> vals;
        if (!dlg->takeValues(&vals))
            return;
        dlg->writeBtn()->setEnabled(false);
        if (m_def.fc == 1)
        {
            QVector<quint8> bits;
            for (quint16 v : vals)
                bits.append(v ? 1 : 0);
            m_mb->writeCoils(m_def.slave, quint16(a), bits);
        }
        else
        {
            m_mb->writeRegs(m_def.slave, quint16(a), vals,
                            /*forceMulti*/ m_def.fc == 16);
        }
        // 应答路由: onWriteDone 里按 m_writeDlgSlave/m_writeDlgAddr 匹配
    });
    connect(dlg, &QDialog::finished, this, [this]() { m_writeDlg = nullptr; });
    m_writeResult = [dlg](bool ok, const QString &info) { dlg->showResult(ok, info); };
    dlg->show();
}

// ============================================================================
// 行作用域辅助
// ============================================================================
QVector<int> DataViewWidget::selectedRows() const
{
    QVector<int> rows;
    if (!m_table || !m_table->selectionModel())
        return rows;
    // 任意列的选中都计行 (单击值列单元格也应命中该行; SelectRows 下整行选中)
    const QModelIndexList sel = m_table->selectionModel()->selectedIndexes();
    for (const auto &ix : sel)
        rows.append(ix.row());
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return rows;
}

bool DataViewWidget::hasRowSelection() const
{
    return !selectedRows().isEmpty();
}

void DataViewWidget::syncDefToMainWindow()
{
    emit definitionAutoChanged(m_def); // 主窗 m_defs 同步 + 标题刷新
}

bool DataViewWidget::eventFilter(QObject *watched, QEvent *event)
{
    // 地址行头宽拖拽/程序化变化 → 回写窗口级 vHeaderW (0 = 默认, 不落盘)
    if (watched == m_table->verticalHeader() && event->type() == QEvent::Resize
        && !m_applyingDef && m_table->isVisible())
    {
        const int w = qBound(64, m_table->verticalHeader()->width(), 400);
        if (w != m_def.vHeaderW)
        {
            m_def.vHeaderW = w;
            syncDefToMainWindow();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void DataViewWidget::setRowsFmt(const QVector<int> &rows, ModbusFormat::Fmt f,
                                ModbusFormat::ByteOrder o)
{
    for (int r : rows)
    {
        PollDefinition::RowStyle &st = m_def.rowStyles[r];
        st.hasFmt = true;
        st.fmt = f;
        st.order = o;
    }
}

void DataViewWidget::setRowsScale(const QVector<int> &rows, bool on, double m,
                                  double x1, double y1)
{
    for (int r : rows)
    {
        PollDefinition::RowStyle &st = m_def.rowStyles[r];
        st.hasScale = true;
        st.scaleOn = on;
        st.scaleM = m;
        st.scaleX1 = x1;
        st.scaleY1 = y1;
    }
}

void DataViewWidget::setRowsAffix(const QVector<int> &rows, const QString &pfx,
                                  const QString &sfx)
{
    // 前缀/后缀覆盖: 空串也是有效覆盖 (显式去掉该行的窗口级单位)
    for (int r : rows)
    {
        PollDefinition::RowStyle &st = m_def.rowStyles[r];
        st.hasAffix = true;
        st.valPrefix = pfx;
        st.valSuffix = sfx;
    }
}

void DataViewWidget::setRowsRules(const QVector<int> &rows,
                                  const PollDefinition::ColorRule r[2])
{
    for (int row : rows)
    {
        PollDefinition::RowStyle &st = m_def.rowStyles[row];
        st.hasRules = true;
        st.rules[0] = r[0];
        st.rules[1] = r[1];
    }
}

void DataViewWidget::setRowsVNames(const QVector<int> &rows,
                                   const QMap<quint16, QString> &vn)
{
    for (int r : rows)
    {
        PollDefinition::RowStyle &st = m_def.rowStyles[r];
        st.hasVNames = true;
        st.valueNames = vn;
    }
}

void DataViewWidget::setRowsLook(const QVector<int> &rows, const PollDefinition &look)
{
    // 外观覆盖 (整组写入): 字体 + 行高 + 背景; 空 bg = 该行不覆盖背景
    for (int r : rows)
    {
        PollDefinition::RowStyle &st = m_def.rowStyles[r];
        st.hasFont = true;
        st.fontPt = look.fontPt;
        st.bold = look.fontBold;
        st.italic = look.fontItalic;
        st.underline = look.fontUnderline;
        st.hasHeight = true;
        st.rowHeight = look.rowHeight;
        st.hasBg = !look.bg.isEmpty();
        st.bg = look.bg;
    }
}

// ---- 显示菜单入口 (选中行 = 行级覆盖; 无选中 = 窗口级默认) ----
void DataViewWidget::applyDisplayFormat(ModbusFormat::Fmt f, ModbusFormat::ByteOrder o)
{
    const QVector<int> rows = selectedRows();
    if (rows.isEmpty())
    {
        m_def.fmt = f;
        m_def.order = o;
        applyDefinition();
        syncDefToMainWindow();
        emit statusMessage(tr("显示格式已应用到全窗口"), true);
        return;
    }
    setRowsFmt(rows, f, o);
    applyDefinition();
    syncDefToMainWindow();
    emit statusMessage(tr("显示格式已应用到 %1 行").arg(rows.size()), true);
}

void DataViewWidget::editCellLook()
{
    const QVector<int> rows = selectedRows();
    if (rows.isEmpty())
    {
        if (DefDialogs::cellLookDialog(this, m_def))
        {
            applyDefinition();
            syncDefToMainWindow();
        }
        return;
    }
    // 行作用域初值: 窗口级默认 ← 首选中行覆盖 (字体/行高/背景);
    // 列宽/行头宽恒窗口级, 初值即窗口值
    PollDefinition scratch = m_def;
    auto it = m_def.rowStyles.constFind(rows.front());
    if (it != m_def.rowStyles.constEnd())
    {
        if (it->hasFont)
        {
            scratch.fontPt = it->fontPt;
            scratch.fontBold = it->bold;
            scratch.fontItalic = it->italic;
            scratch.fontUnderline = it->underline;
        }
        if (it->hasHeight)
            scratch.rowHeight = it->rowHeight;
        if (it->hasBg)
            scratch.bg = it->bg;
    }
    if (!DefDialogs::cellLookDialog(this, scratch))
        return;
    m_def.nameColW = scratch.nameColW;
    m_def.valueColW = scratch.valueColW;
    m_def.vHeaderW = scratch.vHeaderW;
    setRowsLook(rows, scratch);
    applyDefinition();
    syncDefToMainWindow();
    emit statusMessage(tr("单元格样式已应用到 %1 行").arg(rows.size()), true);
}

void DataViewWidget::clearCellStyles()
{
    const QVector<int> rows = selectedRows();
    if (rows.isEmpty())
    {
        if (m_def.rowStyles.isEmpty())
            return;
        m_def.rowStyles.clear();
        applyDefinition();
        syncDefToMainWindow();
        emit statusMessage(tr("已清除全部单元格显示设置"), true);
        return;
    }
    bool changed = false;
    for (int r : rows)
        changed |= m_def.rowStyles.remove(r) > 0;
    if (!changed)
        return;
    applyDefinition();
    syncDefToMainWindow();
    emit statusMessage(tr("已清除 %1 行的单元格显示设置").arg(rows.size()), true);
}

void DataViewWidget::editScale()
{
    const QVector<int> rows = selectedRows();
    if (rows.isEmpty())
    {
        if (DefDialogs::scaleDialog(this, m_def))
        {
            applyDefinition();
            syncDefToMainWindow();
        }
        return;
    }
    // 行作用域: 初值 = 首选中行有效值, 结果写各行覆盖
    bool on = m_def.scaleOn;
    double m = m_def.scaleM, x1 = m_def.scaleX1, y1 = m_def.scaleY1;
    QString pfx = m_def.valPrefix, sfx = m_def.valSuffix;
    auto it = m_def.rowStyles.constFind(rows.front());
    if (it != m_def.rowStyles.constEnd())
    {
        if (it->hasScale)
        {
            on = it->scaleOn;
            m = it->scaleM;
            x1 = it->scaleX1;
            y1 = it->scaleY1;
        }
        if (it->hasAffix)
        {
            pfx = it->valPrefix;
            sfx = it->valSuffix;
        }
    }
    PollDefinition scratch;
    scratch.scaleOn = on;
    scratch.scaleM = m;
    scratch.scaleX1 = x1;
    scratch.scaleY1 = y1;
    scratch.valPrefix = pfx;
    scratch.valSuffix = sfx;
    if (!DefDialogs::scaleDialog(this, scratch))
        return;
    setRowsScale(rows, scratch.scaleOn, scratch.scaleM, scratch.scaleX1,
                 scratch.scaleY1);
    setRowsAffix(rows, scratch.valPrefix, scratch.valSuffix);
    applyDefinition();
    syncDefToMainWindow();
    emit statusMessage(tr("缩放/前后缀已应用到 %1 行").arg(rows.size()), true);
}

void DataViewWidget::editRules()
{
    const QVector<int> rows = selectedRows();
    if (rows.isEmpty())
    {
        if (DefDialogs::rulesDialog(this, m_def))
        {
            applyDefinition();
            syncDefToMainWindow();
        }
        return;
    }
    PollDefinition::ColorRule r[2] = {m_def.rules[0], m_def.rules[1]};
    auto it = m_def.rowStyles.constFind(rows.front());
    if (it != m_def.rowStyles.constEnd() && it->hasRules)
    {
        r[0] = it->rules[0];
        r[1] = it->rules[1];
    }
    PollDefinition scratch;
    scratch.rules[0] = r[0];
    scratch.rules[1] = r[1];
    if (!DefDialogs::rulesDialog(this, scratch))
        return;
    setRowsRules(rows, scratch.rules);
    applyDefinition();
    syncDefToMainWindow();
    emit statusMessage(tr("条件变色已应用到 %1 行").arg(rows.size()), true);
}

void DataViewWidget::editAlias()
{
    if (DefDialogs::aliasDialog(this, m_def))
    {
        applyDefinition();
        syncDefToMainWindow();
    }
}

void DataViewWidget::editValueNames()
{
    const QVector<int> rows = selectedRows();
    if (rows.isEmpty())
    {
        if (DefDialogs::valueNamesDialog(this, m_def))
        {
            applyDefinition();
            syncDefToMainWindow();
        }
        return;
    }
    QMap<quint16, QString> vn = m_def.valueNames;
    auto it = m_def.rowStyles.constFind(rows.front());
    if (it != m_def.rowStyles.constEnd() && it->hasVNames)
        vn = it->valueNames;
    PollDefinition scratch;
    scratch.valueNames = vn;
    if (!DefDialogs::valueNamesDialog(this, scratch))
        return;
    setRowsVNames(rows, scratch.valueNames);
    applyDefinition();
    syncDefToMainWindow();
    emit statusMessage(tr("值名映射已应用到 %1 行").arg(rows.size()), true);
}

void DataViewWidget::exportTable()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("导出表格"), QStringLiteral("polltable.csv"), tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    const bool ok = exportCsv(path, m_model);
    emit statusMessage(ok ? tr("已导出 %1 行").arg(m_model->rowCount()) : tr("导出失败"),
                       ok);
}

void DataViewWidget::toggleLogging()
{
    if (m_logFile)
    {
        stopLogging();
        emit statusMessage(tr("记录已停止"), true);
        return;
    }
    if (DefDialogs::logConfigDialog(this, &m_logCfg))
    {
        startLogging();
        emit statusMessage(m_logFile ? tr("记录中 → %1").arg(m_logCfg.path)
                                     : tr("记录文件打开失败"),
                           m_logFile != nullptr);
    }
}

void DataViewWidget::startLogging()
{
    stopLogging();
    m_logFile = new QFile(m_logCfg.path, this);
    const bool openOk = m_logFile->open(QIODevice::WriteOnly | QIODevice::Text
                                        | (m_logCfg.append ? QIODevice::Append
                                                           : QIODevice::Truncate));
    if (!openOk)
    {
        m_logFile->deleteLater();
        m_logFile = nullptr;
        return;
    }
    if (m_logFile->size() == 0)
    {
        QTextStream ts(m_logFile);
        ts.setEncoding(QStringConverter::Utf8);
        ts << "time,value,err\n";
        ts.flush();
    }
    m_lastLogRow.clear();
    if (!m_logCfg.perRead)
        m_logTimer->start(qMax(1, m_logCfg.intervalSec) * 1000);
}

void DataViewWidget::stopLogging()
{
    if (m_logTimer)
        m_logTimer->stop();
    if (m_logFile)
    {
        m_logFile->flush();
        m_logFile->close();
        m_logFile->deleteLater();
        m_logFile = nullptr;
    }
}

void DataViewWidget::writeLogRow(const QString &valueCell, bool isErr, const QString &errText)
{
    if (!m_logFile)
        return;
    if (m_logCfg.ifChanged && !isErr && valueCell == m_lastLogRow)
        return; // Log if changed
    if (!isErr)
        m_lastLogRow = valueCell;
    QTextStream ts(m_logFile);
    ts.setEncoding(QStringConverter::Utf8);
    ts << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"))
       << ',';
    // 值列可能含逗号 (多值行) — 引号包裹; err 列 = 0 或 "1: 错误文本"
    ts << '"' << valueCell << "\",";
    ts << (isErr ? QStringLiteral("1: %1").arg(errText) : QStringLiteral("0"));
    ts << '\n';
    ts.flush();
}

bool DataViewWidget::exportCsv(const QString &path, const PollTableModel *model)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    // Qt6 无 setGenerateBom: 手写 BOM, 保证 Excel 识别 UTF-8 (对齐 FrameLogDialog)
    f.write("\xEF\xBB\xBF");
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << "address,value,name\n";
    for (int r = 0; r < model->rowCount(); ++r)
    {
        ts << model->addrText(r) << ",\""
           << model->index(r, PollTableModel::ColValue).data(Qt::DisplayRole).toString()
           << "\",\""
           << model->index(r, PollTableModel::ColName).data(Qt::DisplayRole).toString()
           << "\"\n";
    }
    ts.flush();
    return true;
}
