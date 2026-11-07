// ============================================================================
// PollTableModel.cpp — 数据表模型实现 (Name/Value 两列 + 行头地址)
// ============================================================================

#include "PollTableModel.h"

#include "ModbusFormat.h"

#include <QBrush>
#include <QColor>

namespace {
// PLC 地址区前缀 (Modbus 数据区惯例): 0x 线圈 / 1x 离散输入 / 3x 输入 / 4x 保持
QString fcPrefix(int fc)
{
    switch (fc)
    {
    case 1:
    case 5: return QStringLiteral("0x");   // 线圈区 (01 读 / 05 写)
    case 2: return QStringLiteral("1x");   // 离散输入
    case 4: return QStringLiteral("3x");   // 输入寄存器
    default: return QStringLiteral("4x");  // 保持寄存器 (03 读 / 06/16 写)
    }
}

// 多段读窗口行底色 (8 色循环): 浅色系, 避开条件变色规则色 (红 #fee2e2 / 橙 #ffedd5)
QColor segTint(int i)
{
    static const char *kPal[] = {
        "#e3f2fd", // 蓝
        "#e8f5e9", // 绿
        "#f3e5f5", // 紫
        "#e0f7fa", // 青
        "#ede7f6", // 深紫
        "#e8eaf6", // 靛
        "#f1f8e9", // 黄绿
        "#efebe9", // 棕灰
    };
    return QColor(kPal[i % (sizeof(kPal) / sizeof(kPal[0]))]);
}
} // namespace

PollTableModel::PollTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void PollTableModel::setDefinition(const PollDefinition &d)
{
    beginResetModel();
    m_def = d;
    m_def.clamp();
    m_regs.resize(m_def.totalQty());
    m_addrInCell = m_def.addrInCell; // 定义级显示选项随 F8 下发
    endResetModel();
}

void PollTableModel::setPlcAddr(bool on)
{
    if (m_plcAddr == on)
        return;
    m_plcAddr = on;
    if (rowCount() > 0)
        emit headerDataChanged(Qt::Vertical, 0, rowCount() - 1);
    if (m_addrInCell) // 单元格内地址同风格变化
        emit dataChanged(index(0, ColName), index(rowCount() - 1, ColName),
                         {Qt::DisplayRole, Qt::EditRole});
}

void PollTableModel::setAddrInCell(bool on)
{
    if (m_addrInCell == on)
        return;
    m_addrInCell = on;
    if (rowCount() > 0)
        emit dataChanged(index(0, ColName), index(rowCount() - 1, ColName),
                         {Qt::DisplayRole, Qt::EditRole});
}

void PollTableModel::applyRegs(const QVector<quint16> &regs)
{
    const int n = qMin(regs.size(), m_regs.size());
    if (n <= 0)
        return;
    for (int i = 0; i < n; ++i)
        m_regs[i] = regs[i];
    // 部分应答 (数量被从站截断) 也要刷新全部行: 未覆盖行保持旧值;
    // 只发值列 — 别名列与寄存器无关, 刷它会把行内编辑中的别名编辑器打断
    emit dataChanged(index(0, ColValue), index(n - 1, ColValue),
                     {Qt::DisplayRole, Qt::EditRole, Qt::ForegroundRole, Qt::BackgroundRole});
}

void PollTableModel::setRegs(int row, const QVector<quint16> &vals)
{
    if (row < 0 || row >= m_regs.size())
        return;
    const int n = qMin(int(vals.size()), m_regs.size() - row);
    for (int i = 0; i < n; ++i)
        m_regs[row + i] = vals[i];
    // 同 applyRegs: 只发值列, 轮询上屏不打断别名行内编辑
    emit dataChanged(index(row, ColValue), index(row + n - 1, ColValue),
                     {Qt::DisplayRole, Qt::EditRole, Qt::ForegroundRole, Qt::BackgroundRole});
}

int PollTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_regs.size();
}

int PollTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QString PollTableModel::addrText(int row) const
{
    const int a = m_def.rowAddr(row);
    if (a < 0)
        return QString();
    if (!m_plcAddr)
        return QString::number(a);
    // PLC 风格: Base 1 + 区前缀, 5 位编号 (4x00001 口径)
    return QStringLiteral("%1%2").arg(fcPrefix(m_def.fc),
                                      QString::number(a + 1).rightJustified(5, QLatin1Char('0')));
}

PollTableModel::EffDisp PollTableModel::effBase(int row) const
{
    EffDisp e;
    e.fmt = m_def.fmt;
    e.order = m_def.order;
    e.scaleOn = m_def.scaleOn;
    e.sm = m_def.scaleM;
    e.sx1 = m_def.scaleX1;
    e.sy1 = m_def.scaleY1;
    auto it = m_def.rowStyles.constFind(row);
    if (it != m_def.rowStyles.constEnd())
    {
        const PollDefinition::RowStyle &st = it.value();
        if (st.hasFmt)
        {
            e.fmt = st.fmt;
            e.order = st.order;
        }
        if (st.hasScale)
        {
            e.scaleOn = st.scaleOn;
            e.sm = st.scaleM;
            e.sx1 = st.scaleX1;
            e.sy1 = st.scaleY1;
        }
        if (st.hasRules)
            e.rules = st.rules;
        if (st.hasVNames)
            e.vnames = &st.valueNames;
        if (st.hasAffix)
        {
            e.pfx = &st.valPrefix;
            e.sfx = &st.valSuffix;
        }
    }
    // 该行到所在段末的寄存器数 (宽格式跨行/跨段拦截)
    e.avail = int(m_regs.size()) - row;
    if (!m_def.segs.isEmpty() && !m_def.isWriteFc())
    {
        int base = 0;
        for (const PollDefinition::Segment &s : m_def.segs)
        {
            if (row < base + s.qty)
            {
                e.avail = base + s.qty - row;
                break;
            }
            base += s.qty;
        }
    }
    return e;
}

PollTableModel::EffDisp PollTableModel::eff(int row) const
{
    EffDisp e = effBase(row);
    // 宽格式占用: 向上找覆盖本行的 32/64 位格式 (最多回看 3 行 = 64 位 4 寄存器);
    // 上行宽格式自身放不下 (rc0 > avail) 时不占用后续行
    for (int k = 1; k <= 3 && row - k >= 0; ++k)
    {
        const EffDisp p = effBase(row - k);
        const int rc0 = ModbusFormat::regCount(p.fmt);
        if (rc0 > k && rc0 <= p.avail)
        {
            e.covered = true;
            break;
        }
    }
    return e;
}

bool PollTableModel::isCovered(int row) const
{
    return eff(row).covered;
}

QString PollTableModel::valueText(int row) const
{
    const EffDisp e = eff(row);
    if (e.covered)
        return QStringLiteral("-"); // 被上一行宽格式占用
    const int rc = ModbusFormat::regCount(e.fmt);
    if (rc > e.avail)
        return QString(); // 末行跨行不完整 / 跨段边界 (宽格式残留行)
    const quint16 *r = m_regs.constData() + row;

    // 值前缀/后缀 (Affix): 包裹数值文本 (值名映射是语义文本, 不包裹)
    const QString pfx = e.pfx ? *e.pfx : m_def.valPrefix;
    const QString sfx = e.sfx ? *e.sfx : m_def.valSuffix;

    // 值名映射 (Value Names): 仅 16 位整数格式
    if (rc == 1 && ModbusFormat::isIntegerFmt(e.fmt))
    {
        const QMap<quint16, QString> &vn = e.vnames ? *e.vnames : m_def.valueNames;
        if (vn.contains(r[0]))
            return vn.value(r[0]);
    }

    // 缩放 Scaling: Y = m(X−X1)+Y1, 仅整数格式
    if (e.scaleOn && ModbusFormat::isIntegerFmt(e.fmt))
    {
        bool ok = false;
        const double x = ModbusFormat::numericValue(e.fmt, e.order, r, rc, &ok);
        if (ok)
            return pfx + QString::number(e.sm * (x - e.sx1) + e.sy1, 'g', 10) + sfx;
    }
    return pfx + ModbusFormat::formatRegs(e.fmt, e.order, r, rc) + sfx;
}

int PollTableModel::hitRule(double v, int row) const
{
    const EffDisp e = eff(row);
    if (!ModbusFormat::isIntegerFmt(e.fmt))
        return -1; // 变色基准为整数值 (对标 Modbus Poll 整数比较域)
    const PollDefinition::ColorRule *rules = e.rules ? e.rules : m_def.rules;
    for (int i = 0; i < 2; ++i)
        if (rules[i].op != 0 && rules[i].on(v))
            return i;
    return -1;
}

QFont PollTableModel::effectiveFont(int row) const
{
    // 行级字体覆盖 → 窗口级默认; 字体族跟随应用默认字体
    int pt = m_def.fontPt;
    bool b = m_def.fontBold, i = m_def.fontItalic, u = m_def.fontUnderline;
    auto it = m_def.rowStyles.constFind(row);
    if (it != m_def.rowStyles.constEnd() && it->hasFont)
    {
        pt = it->fontPt;
        b = it->bold;
        i = it->italic;
        u = it->underline;
    }
    QFont f;
    f.setPointSize(qBound(6, pt, 48));
    f.setBold(b);
    f.setItalic(i);
    f.setUnderline(u);
    return f;
}

QColor PollTableModel::rowBg(int row) const
{
    auto it = m_def.rowStyles.constFind(row);
    if (it != m_def.rowStyles.constEnd() && it->hasBg)
    {
        const QColor c(it->bg);
        if (c.isValid())
            return c;
    }
    return QColor();
}

QVariant PollTableModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() >= m_regs.size())
        return QVariant();
    const int row = idx.row();
    switch (role)
    {
    case Qt::DisplayRole:
        if (idx.column() == ColName)
        {
            const QString alias = m_def.nameCells.value(row);
            return m_addrInCell ? QStringLiteral("%1  %2").arg(addrText(row), alias)
                                : alias;
        }
        if (idx.column() == ColValue)
            return valueText(row);
        return QVariant();
    case Qt::ForegroundRole:
    case Qt::BackgroundRole:
    {
        // 优先链: 行级手动背景 (绝对) > 值列条件变色 > 段底色 > 窗口底色;
        // 设过手动背景的行整行着色, 该行条件变色/段底色让位
        const QColor rb = rowBg(row);
        if (rb.isValid())
            return role == Qt::BackgroundRole ? QBrush(rb) : QVariant();
        // 值列条件变色规则命中: 规则色优先于段色 (前景 + 背景)
        if (idx.column() != ColValue)
        {
            if (role == Qt::BackgroundRole)
            {
                if (hasMultiSeg())
                {
                    const int seg = m_def.segmentIndexAt(row);
                    if (seg >= 0)
                        return QBrush(segTint(seg));
                }
                const QColor c(m_def.bg); // 窗口底色 (空/非法 = 主题默认)
                if (c.isValid())
                    return QBrush(c);
            }
            return QVariant();
        }
        const EffDisp e = eff(row);
        if (!e.covered) // 占用行显示 "-", 不参与条件变色
        {
            bool ok = false;
            const double v = ModbusFormat::numericValue(e.fmt, e.order,
                                                        m_regs.constData() + row,
                                                        qMin(ModbusFormat::regCount(e.fmt),
                                                             e.avail),
                                                        &ok);
            if (ok)
            {
                const int hit = hitRule(e.scaleOn && ModbusFormat::isIntegerFmt(e.fmt)
                                            ? e.sm * (v - e.sx1) + e.sy1
                                            : v,
                                        row);
                if (hit >= 0)
                {
                    // 规则1 红 / 规则2 橙
                    return role == Qt::ForegroundRole
                               ? QBrush(QColor(hit == 0 ? QStringLiteral("#dc2626")
                                                        : QStringLiteral("#d97706")))
                               : QBrush(QColor(hit == 0 ? QStringLiteral("#fee2e2")
                                                        : QStringLiteral("#ffedd5")));
                }
            }
        }
        if (role == Qt::BackgroundRole)
        {
            if (hasMultiSeg())
            {
                const int seg = m_def.segmentIndexAt(row);
                if (seg >= 0)
                    return QBrush(segTint(seg));
            }
            const QColor c(m_def.bg);
            if (c.isValid())
                return QBrush(c);
        }
        return QVariant();
    }
    case Qt::FontRole:
        return effectiveFont(row); // 行级覆盖 → 窗口级默认
    case Qt::TextAlignmentRole:
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    default:
        return QVariant();
    }
}

QVariant PollTableModel::headerData(int section, Qt::Orientation orient, int role) const
{
    if (orient == Qt::Horizontal && role == Qt::DisplayRole)
    {
        switch (section)
        {
        case ColName: return tr("别名");
        case ColValue: return tr("值");
        default: return QVariant();
        }
    }
    if (orient == Qt::Vertical)
    {
        if (role == Qt::BackgroundRole)
        {
            // 同 data() 优先链: 行手动背景 > 段底色 > 窗口底色
            const QColor rb = rowBg(section);
            if (rb.isValid())
                return QBrush(rb);
            if (hasMultiSeg())
            {
                const int seg = m_def.segmentIndexAt(section);
                if (seg >= 0)
                    return QBrush(segTint(seg)); // 行头与所在段同色 (分段边界可辨)
            }
            const QColor c(m_def.bg);
            if (c.isValid())
                return QBrush(c);
        }
        else if (role == Qt::FontRole)
            return effectiveFont(section); // 行头地址字号随行 (含行级覆盖)
        if (role == Qt::DisplayRole)
            return addrText(section); // 行头 = 地址 (Base0 偏移 / PLC 风格)
    }
    return QAbstractTableModel::headerData(section, orient, role);
}

Qt::ItemFlags PollTableModel::flags(const QModelIndex &idx) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(idx);
    if (!idx.isValid())
        return f;
    if (idx.column() == ColName)
        f |= Qt::ItemIsEditable; // 别名: 所有窗口均可直接行内重命名
    // 写窗口: 值单元格内联编辑 (网格即数据源, 扫描/F6 发送);
    // 读窗口值列不可内联编辑 — 双击由视图打开写值对话框;
    // 被上一行宽格式占用的行: 显示 "-", 禁止写入
    if (idx.column() == ColValue && m_def.isWriteFc() && !eff(idx.row()).covered)
        f |= Qt::ItemIsEditable;
    return f;
}

bool PollTableModel::setData(const QModelIndex &idx, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !idx.isValid())
        return false;
    if (idx.column() == ColName)
    {
        // 别名重命名: nameCells 跳行补位, 空串 = 清除
        const int row = idx.row();
        if (row < 0 || row >= m_regs.size())
            return false;
        if (m_def.nameCells.size() < m_regs.size())
            m_def.nameCells.resize(m_regs.size());
        const QString text = value.toString().trimmed();
        m_def.nameCells[row] = text;
        emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
        emit nameEdited(row, text);
        return true;
    }
    if (idx.column() != ColValue)
        return false;
    if (!m_def.isWriteFc())
        return false; // 读窗口: 编辑走写值对话框
    const EffDisp e = eff(idx.row());
    if (e.covered)
        return false; // 被上一行宽格式占用, 禁止写入
    const int rc = ModbusFormat::regCount(e.fmt);
    if (rc > e.avail)
        return false; // 跨行/跨段不完整, 不接受编辑
    // 剥离有效前缀/后缀 (用户常照显示文本输入 "12.5 V") 后再解析
    QString text = value.toString().trimmed();
    const QString pfx = e.pfx ? *e.pfx : m_def.valPrefix;
    const QString sfx = e.sfx ? *e.sfx : m_def.valSuffix;
    if (!pfx.isEmpty() && text.startsWith(pfx))
        text.remove(0, pfx.size());
    if (!sfx.isEmpty() && text.endsWith(sfx))
        text.chop(sfx.size());
    text = text.trimmed();
    quint16 regs[4] = {};
    if (!ModbusFormat::parseInput(e.fmt, e.order, text, regs, rc))
        return false;
    for (int i = 0; i < rc; ++i)
        m_regs[idx.row() + i] = regs[i];
    emit dataChanged(index(idx.row(), ColValue), index(idx.row() + rc - 1, ColValue),
                     {Qt::DisplayRole, Qt::EditRole});
    return true; // 网格已更新; 发送由扫描周期/F6 执行 (写窗口语义)
}
