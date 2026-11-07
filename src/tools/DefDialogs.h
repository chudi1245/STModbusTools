#pragma once

// ============================================================================
// DefDialogs.h — 轮询定义页的小对话框集合 (Modbus Poll 对标功能入口)
//   缩放 Scaling / 条件变色规则 / 别名 Name Cells / 值名映射 Value Names /
//   批量写 (0F/10) / CSV 记录配置 — 均为模态小窗, 直接改写 PollDefinition
//   不用 Q_OBJECT: 交互经 lambda + exec() 返回值, 避免元对象样板
// ============================================================================

#include "PollDefinition.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QObject>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace DefDialogs {

// ---- 单元格格式 (右键"单元格格式…"/行级显示覆盖入口): 格式 + 字序 ----
inline bool cellFormatDialog(QWidget *parent, ModbusFormat::Fmt *f,
                             ModbusFormat::ByteOrder *o)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QDialog::tr("单元格显示格式"));
    auto *v = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout;
    auto *fmt = new QComboBox;
    struct FItem
    {
        ModbusFormat::Fmt f;
        const char *text;
    };
    static const FItem items[] = {
        {ModbusFormat::Fmt::I16, "Signed 有符号 (16位)"},
        {ModbusFormat::Fmt::U16, "Unsigned 无符号 (16位)"},
        {ModbusFormat::Fmt::Hex, "Hex 十六进制 (16位)"},
        {ModbusFormat::Fmt::AsciiHex, "ASCII (16位)"},
        {ModbusFormat::Fmt::Bin, "Binary 二进制 (16位)"},
        {ModbusFormat::Fmt::I32, "Signed 有符号整数 (32位)"},
        {ModbusFormat::Fmt::U32, "Unsigned 无符号整数 (32位)"},
        {ModbusFormat::Fmt::F32, "Float 浮点数 (32位)"},
        {ModbusFormat::Fmt::I64, "Signed 有符号整数 (64位)"},
        {ModbusFormat::Fmt::U64, "Unsigned 无符号整数 (64位)"},
        {ModbusFormat::Fmt::F64, "Double 浮点数 (64位)"},
    };
    for (const FItem &it : items)
        fmt->addItem(QString::fromUtf8(it.text), int(it.f));
    fmt->setCurrentIndex(qMax(0, fmt->findData(int(*f))));
    auto *ord = new QComboBox;
    const char *const ordTexts[] = {"Big-endian (ABCD)", "Little-endian (DCBA)",
                                    "Big-endian byte swap (BADC)",
                                    "Little-endian byte swap (CDAB)"};
    for (int i = 0; i < 4; ++i)
        ord->addItem(QString::fromUtf8(ordTexts[i]), i);
    ord->setCurrentIndex(int(*o));
    form->addRow(QDialog::tr("显示格式"), fmt);
    form->addRow(QDialog::tr("字序 (32/64 位)"), ord);
    v->addLayout(form);
    v->addWidget(new QLabel(QDialog::tr("确定后应用到选中行 (无选中 = 全窗口)")));
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    v->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return false;
    *f = ModbusFormat::Fmt(fmt->currentData().toInt());
    *o = ModbusFormat::ByteOrder(ord->currentIndex());
    return true;
}

// ---- 单元格样式 (外观): 字体大小/粗斜下划线 + 行高 + 背景 (+ 列宽) ----
//   字体/行高/背景: 作用域由调用方决定 (选中行 = 行级覆盖, 无选中 = 窗口级);
//   列宽/行头宽恒为窗口级 — 表格列宽按列共享, 无法逐行设置
inline bool cellLookDialog(QWidget *parent, PollDefinition &d)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QDialog::tr("单元格样式"));
    auto *v = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout;

    auto *pt = new QSpinBox;
    pt->setRange(6, 48);
    pt->setSuffix(QStringLiteral(" pt"));
    pt->setValue(d.fontPt);
    auto *bold = new QToolButton;
    bold->setText(QStringLiteral("B"));
    QFont bf = bold->font();
    bf.setBold(true);
    bold->setFont(bf);
    bold->setCheckable(true);
    bold->setChecked(d.fontBold);
    auto *ital = new QToolButton;
    ital->setText(QStringLiteral("I"));
    QFont itf = ital->font();
    itf.setItalic(true);
    ital->setFont(itf);
    ital->setCheckable(true);
    ital->setChecked(d.fontItalic);
    auto *und = new QToolButton;
    und->setText(QStringLiteral("U"));
    QFont uf = und->font();
    uf.setUnderline(true);
    und->setFont(uf);
    und->setCheckable(true);
    und->setChecked(d.fontUnderline);
    auto *styleBox = new QWidget;
    auto *styleLay = new QHBoxLayout(styleBox);
    styleLay->setContentsMargins(0, 0, 0, 0);
    styleLay->addWidget(bold);
    styleLay->addWidget(ital);
    styleLay->addWidget(und);
    styleLay->addStretch();
    form->addRow(QDialog::tr("字体大小"), pt);
    form->addRow(QDialog::tr("字体样式"), styleBox);

    auto *rh = new QSpinBox;
    rh->setRange(14, 200);
    rh->setSuffix(QStringLiteral(" px"));
    rh->setValue(d.rowHeight);
    form->addRow(QDialog::tr("行高"), rh);

    auto *ncw = new QSpinBox;
    ncw->setRange(40, 4000);
    ncw->setSuffix(QStringLiteral(" px"));
    ncw->setValue(d.nameColW);
    auto *vcw = new QSpinBox;
    vcw->setRange(40, 4000);
    vcw->setSuffix(QStringLiteral(" px"));
    vcw->setValue(d.valueColW);
    auto *vhw = new QSpinBox;
    vhw->setRange(0, 400);
    vhw->setSpecialValueText(QDialog::tr("默认"));
    vhw->setValue(d.vHeaderW);
    form->addRow(QDialog::tr("别名列宽"), ncw);
    form->addRow(QDialog::tr("值列宽"), vcw);
    form->addRow(QDialog::tr("行头宽"), vhw);

    QString bg = d.bg; // "#rrggbb"; 空 = 主题默认
    auto *pickBg = new QPushButton(QDialog::tr("选择颜色…"));
    auto *resetBg = new QPushButton(QDialog::tr("默认"));
    auto *bgBox = new QWidget;
    auto *bgLay = new QHBoxLayout(bgBox);
    bgLay->setContentsMargins(0, 0, 0, 0);
    bgLay->addWidget(pickBg);
    bgLay->addWidget(resetBg);
    bgLay->addStretch();
    form->addRow(QDialog::tr("背景色"), bgBox);

    auto *preview = new QLabel(QStringLiteral("12345.67"));
    preview->setAlignment(Qt::AlignCenter);
    v->addLayout(form);
    v->addWidget(preview);
    v->addWidget(new QLabel(QDialog::tr(
        "确定后应用到选中行 (无选中 = 全窗口); 列宽与行头宽始终作用于全窗口")));
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    v->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    const auto updatePreview = [pt, bold, ital, und, preview, &bg]() {
        QFont f;
        f.setPointSize(pt->value());
        f.setBold(bold->isChecked());
        f.setItalic(ital->isChecked());
        f.setUnderline(und->isChecked());
        preview->setFont(f);
        const QColor c(bg);
        if (c.isValid())
        {
            // 前景按亮度取黑/白, 保证预览可读
            preview->setStyleSheet(
                QStringLiteral("background:%1; color:%2;")
                    .arg(c.name(),
                         c.lightness() > 128 ? QStringLiteral("black")
                                             : QStringLiteral("white")));
        }
        else
            preview->setStyleSheet(QString());
    };
    updatePreview();
    QObject::connect(pt, &QSpinBox::valueChanged, &dlg, updatePreview);
    QObject::connect(bold, &QToolButton::toggled, &dlg, updatePreview);
    QObject::connect(ital, &QToolButton::toggled, &dlg, updatePreview);
    QObject::connect(und, &QToolButton::toggled, &dlg, updatePreview);
    QObject::connect(pickBg, &QPushButton::clicked, &dlg,
                     [pickBg, &bg, updatePreview]() {
                         const QColor c = QColorDialog::getColor(QColor(bg), pickBg);
                         if (c.isValid())
                         {
                             bg = c.name();
                             updatePreview();
                         }
                     });
    QObject::connect(resetBg, &QPushButton::clicked, &dlg,
                     [&bg, updatePreview]() {
                         bg.clear();
                         updatePreview();
                     });
    if (dlg.exec() != QDialog::Accepted)
        return false;
    d.fontPt = pt->value();
    d.fontBold = bold->isChecked();
    d.fontItalic = ital->isChecked();
    d.fontUnderline = und->isChecked();
    d.rowHeight = rh->value();
    d.nameColW = ncw->value();
    d.valueColW = vcw->value();
    d.vHeaderW = vhw->value();
    d.bg = bg;
    return true;
}

// ---- 缩放与前缀/后缀: Y = m(X − X1) + Y1 (仅整数格式生效, 模型保证) ----
//   前缀/后缀常用于单位 (如 "12.5 V"): 包裹数值文本, 值名映射不包裹;
//   写窗口内联编辑输入时自动剥离后再解析
inline bool scaleDialog(QWidget *parent, PollDefinition &d)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QDialog::tr("缩放与前缀/后缀 (Scaling & Affix)"));
    auto *v = new QVBoxLayout(&dlg);
    auto *chk = new QCheckBox(QDialog::tr("启用缩放 (仅整数显示格式)"));
    chk->setChecked(d.scaleOn);
    auto *m = new QDoubleSpinBox;
    auto *x1 = new QDoubleSpinBox;
    auto *y1 = new QDoubleSpinBox;
    for (QDoubleSpinBox *s : {m, x1, y1})
    {
        s->setRange(-1e12, 1e12);
        s->setDecimals(6);
    }
    m->setValue(d.scaleM);
    x1->setValue(d.scaleX1);
    y1->setValue(d.scaleY1);
    auto *pfx = new QLineEdit(d.valPrefix);
    pfx->setPlaceholderText(QDialog::tr("如 $ / ≈ (留空 = 无)"));
    auto *sfx = new QLineEdit(d.valSuffix);
    sfx->setPlaceholderText(QDialog::tr("如 V / A / ℃ (留空 = 无; 可含空格)"));
    auto *form = new QFormLayout;
    form->addRow(QDialog::tr("系数 m (Y = m(X−X1)+Y1)"), m);
    form->addRow(QDialog::tr("偏移 X1"), x1);
    form->addRow(QDialog::tr("目标 Y1"), y1);
    form->addRow(QDialog::tr("值前缀"), pfx);
    form->addRow(QDialog::tr("值后缀 (单位)"), sfx);
    v->addWidget(chk);
    v->addLayout(form);
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    v->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return false;
    d.scaleOn = chk->isChecked();
    d.scaleM = m->value();
    d.scaleX1 = x1->value();
    d.scaleY1 = y1->value();
    d.valPrefix = pfx->text(); // 不 trim: 前后缀可有意包含空格 (如 " V")
    d.valSuffix = sfx->text();
    return true;
}

// ---- 条件变色规则 ×2 (规则1 红 / 规则2 橙) ----
inline bool rulesDialog(QWidget *parent, PollDefinition &d)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QDialog::tr("条件变色 (Conditional Colors)"));
    auto *v = new QVBoxLayout(&dlg);
    v->addWidget(new QLabel(QDialog::tr("两条规则按序匹配, 首条命中生效; 基准为整数值"
                                        "(整数格式 + 缩放后)")));
    struct Row
    {
        QComboBox *op = nullptr;
        QDoubleSpinBox *lo = nullptr;
        QDoubleSpinBox *hi = nullptr;
    };
    Row rows[2];
    auto *grid = new QFormLayout;
    for (int i = 0; i < 2; ++i)
    {
        auto *lay = new QHBoxLayout;
        auto &r = rows[i];
        r.op = new QComboBox;
        r.op->addItem(QDialog::tr("关闭"));
        r.op->addItem(QDialog::tr("大于"));
        r.op->addItem(QDialog::tr("小于"));
        r.op->addItem(QDialog::tr("区间内 [lo, hi]"));
        r.op->addItem(QDialog::tr("区间外"));
        r.op->addItem(QDialog::tr("等于"));
        r.op->setCurrentIndex(d.rules[i].op);
        r.lo = new QDoubleSpinBox;
        r.hi = new QDoubleSpinBox;
        for (QDoubleSpinBox *s : {r.lo, r.hi})
        {
            s->setRange(-1e12, 1e12);
            s->setDecimals(6);
        }
        r.lo->setValue(d.rules[i].lo);
        r.hi->setValue(d.rules[i].hi);
        lay->addWidget(r.op);
        lay->addWidget(new QLabel(QDialog::tr("lo")));
        lay->addWidget(r.lo);
        lay->addWidget(new QLabel(QDialog::tr("hi")));
        lay->addWidget(r.hi);
        auto *w = new QWidget;
        w->setLayout(lay);
        grid->addRow(QDialog::tr("规则 %1 (%2)").arg(i + 1).arg(i == 0 ? QDialog::tr("红")
                                                                      : QDialog::tr("橙")),
                     w);
    }
    v->addLayout(grid);
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    v->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return false;
    for (int i = 0; i < 2; ++i)
    {
        d.rules[i].op = rows[i].op->currentIndex();
        d.rules[i].lo = rows[i].lo->value();
        d.rules[i].hi = rows[i].hi->value();
    }
    return true;
}

// ---- 别名 Name Cells: 每行一个, 行号 = 表格行 ----
inline bool aliasDialog(QWidget *parent, PollDefinition &d)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QDialog::tr("别名 (Name Cells)"));
    dlg.resize(360, 420);
    auto *v = new QVBoxLayout(&dlg);
    v->addWidget(new QLabel(QDialog::tr("每行一个标签, 与表格行一一对应"
                                        "(多段窗口见行头地址):")));
    auto *edit = new QPlainTextEdit;
    edit->setPlainText(d.nameCells.join(QLatin1Char('\n')));
    edit->setPlaceholderText(QDialog::tr("例:\n现场温度\n设定值\n输出电流"));
    v->addWidget(edit);
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    v->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return false;
    const QStringList lines =
        edit->toPlainText().split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    d.nameCells = lines;
    return true;
}

// ---- 值名映射 Value Names: 每行 "值=文本" ----
inline bool valueNamesDialog(QWidget *parent, PollDefinition &d)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QDialog::tr("值名映射 (Value Names)"));
    dlg.resize(360, 420);
    auto *v = new QVBoxLayout(&dlg);
    v->addWidget(new QLabel(QDialog::tr("每行 \"值=文本\" (16 位整数格式生效), 例:\n"
                                        "0=停止\n1=运行")));
    auto *edit = new QPlainTextEdit;
    QStringList lines;
    for (auto it = d.valueNames.cbegin(); it != d.valueNames.cend(); ++it)
        lines << QStringLiteral("%1=%2").arg(it.key()).arg(it.value());
    edit->setPlainText(lines.join(QLatin1Char('\n')));
    v->addWidget(edit);
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    v->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return false;
    QMap<quint16, QString> parsed;
    const QStringList all = edit->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : all)
    {
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        bool ok = false;
        const qlonglong k = line.left(eq).trimmed().toLongLong(&ok);
        if (!ok || k < 0 || k > 65535)
            continue;
        parsed.insert(quint16(k), line.mid(eq + 1).trimmed());
    }
    d.valueNames = parsed;
    return true;
}

// ---- 批量写 (FC0F/FC10): 每行一个值, 预填当前值 ----
//   addr = 实际起始地址 (多段窗口为所在段内偏移), fmt/order = 首行有效显示格式
class MultiWriteDialog : public QDialog
{
public:
    MultiWriteDialog(QWidget *parent, const PollDefinition &d, quint16 addr,
                     const QVector<quint16> &curVals, ModbusFormat::Fmt fmt,
                     ModbusFormat::ByteOrder order)
        : QDialog(parent)
        , m_bit(d.isBitFc())
        , m_fmt(fmt)
        , m_order(order)
    {
        m_max = m_bit ? 1968 : 123;
        setWindowTitle(QDialog::tr("写入选中 (FC%1)").arg(m_bit ? "0F" : "10"));
        resize(300, 380);
        auto *v = new QVBoxLayout(this);
        v->addWidget(new QLabel(QDialog::tr("地址 %1 起 ×%2 — 每行一个值 (%3):")
                                    .arg(addr)
                                    .arg(curVals.size())
                                    .arg(m_bit ? QDialog::tr("0/1")
                                               : QDialog::tr("按显示格式"))));
        m_edit = new QPlainTextEdit;
        for (quint16 val : curVals)
            m_edit->appendPlainText(ModbusFormat::formatRegs(m_fmt, m_order, &val, 1));
        v->addWidget(m_edit);
        m_err = new QLabel;
        m_err->setStyleSheet(QDialog::tr("color:#dc2626;"));
        m_err->hide();
        v->addWidget(m_err);
        auto *btnRow = new QHBoxLayout;
        m_btnWrite = new QPushButton(QDialog::tr("写入"));
        m_btnWrite->setDefault(true);
        auto *btnCancel = new QPushButton(QDialog::tr("取消"));
        btnRow->addStretch();
        btnRow->addWidget(m_btnCancel);
        btnRow->addWidget(m_btnWrite);
        v->addLayout(btnRow);
        QObject::connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    }

    // 解析全部行 → vals; 失败时显示错误并返回 false
    bool takeValues(QVector<quint16> *vals)
    {
        const QStringList lines =
            m_edit->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (lines.isEmpty() || lines.size() > m_max)
        {
            m_err->setText(QDialog::tr("需要 1-%1 行值").arg(m_max));
            m_err->show();
            return false;
        }
        QVector<quint16> out;
        for (const QString &line : lines)
        {
            quint16 regs[4] = {};
            if (!ModbusFormat::parseInput(m_fmt, m_order, line, regs,
                                          ModbusFormat::regCount(m_fmt)))
            {
                m_err->setText(QDialog::tr("无法解析: %1").arg(line));
                m_err->show();
                return false;
            }
            if (m_bit)
                out.append(regs[0] ? 1 : 0);
            else
                for (int i = 0; i < ModbusFormat::regCount(m_fmt); ++i)
                    out.append(regs[i]);
        }
        if (out.size() > m_max)
        {
            m_err->setText(QDialog::tr("超出单帧上限 %1").arg(m_max));
            m_err->show();
            return false;
        }
        *vals = out;
        return true;
    }

    QPushButton *writeBtn() const { return m_btnWrite; }
    void showResult(bool ok, const QString &err)
    {
        if (ok)
        {
            accept();
            return;
        }
        m_err->setText(err);
        m_err->show();
        m_btnWrite->setEnabled(true);
    }

private:
    bool m_bit = false;
    ModbusFormat::Fmt m_fmt = ModbusFormat::Fmt::U16;
    ModbusFormat::ByteOrder m_order = ModbusFormat::ByteOrder::ABCD;
    QPlainTextEdit *m_edit = nullptr;
    QLabel *m_err = nullptr;
    QPushButton *m_btnWrite = nullptr;
    QPushButton *m_btnCancel = nullptr;
    int m_max = 123;
};

// ---- CSV 记录配置 ----
struct LogConfig
{
    QString path;
    bool perRead = true;  // true = 每次读记录; false = 按秒
    int intervalSec = 1;
    bool ifChanged = false;
    bool logErrors = false;
    bool append = false;
};

inline bool logConfigDialog(QWidget *parent, LogConfig *cfg)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QDialog::tr("数据记录 (Logging)"));
    auto *v = new QVBoxLayout(&dlg);
    auto *pathRow = new QHBoxLayout;
    auto *path = new QLineEdit(cfg->path);
    path->setPlaceholderText(QDialog::tr("选择 CSV 文件…"));
    auto *browse = new QPushButton(QDialog::tr("浏览…"));
    pathRow->addWidget(path, 1);
    pathRow->addWidget(browse);
    v->addLayout(pathRow);
    auto *perRead = new QRadioButton(QDialog::tr("每次读取后记录"));
    auto *perSec = new QRadioButton(QDialog::tr("按间隔记录"));
    (cfg->perRead ? perRead : perSec)->setChecked(true);
    auto *sec = new QSpinBox;
    sec->setRange(1, 3600);
    sec->setValue(qMax(1, cfg->intervalSec));
    sec->setSuffix(QDialog::tr(" 秒"));
    auto *secRow = new QHBoxLayout;
    secRow->addWidget(perSec);
    secRow->addWidget(sec);
    secRow->addStretch();
    auto *chkChanged = new QCheckBox(QDialog::tr("仅值变化时记录 (Log if changed)"));
    chkChanged->setChecked(cfg->ifChanged);
    auto *chkErr = new QCheckBox(QDialog::tr("记录错误行 (Log Errors)"));
    chkErr->setChecked(cfg->logErrors);
    auto *chkAppend = new QCheckBox(QDialog::tr("追加写入 (Append)"));
    chkAppend->setChecked(cfg->append);
    v->addWidget(perRead);
    v->addLayout(secRow);
    v->addWidget(chkChanged);
    v->addWidget(chkErr);
    v->addWidget(chkAppend);
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    v->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(browse, &QPushButton::clicked, &dlg, [path, &dlg]() {
        const QString f = QFileDialog::getSaveFileName(
            &dlg, QDialog::tr("记录文件"), QStringLiteral("polllog.csv"),
            QDialog::tr("CSV (*.csv)"));
        if (!f.isEmpty())
            path->setText(f);
    });
    if (dlg.exec() != QDialog::Accepted)
        return false;
    if (path->text().trimmed().isEmpty())
        return false;
    cfg->path = path->text().trimmed();
    cfg->perRead = perRead->isChecked();
    cfg->intervalSec = sec->value();
    cfg->ifChanged = chkChanged->isChecked();
    cfg->logErrors = chkErr->isChecked();
    cfg->append = chkAppend->isChecked();
    return true;
}


// ---- 写值对话框 (读窗口双击值单元格, 对标 Modbus Poll Write Register/Coil) ----
//   地址/当前值只读展示 + 新值输入 + 写功能码选择 (单写 / 多写到窗口末行)
//   + "响应 OK 自动关闭"; 解析失败/写失败在框内显示, 不丢已输内容
class WriteCellDialog : public QDialog
{
public:
    // addrLabel: 按当前显示风格的地址文本; tail: 从该行到窗口末行的当前网格值
    WriteCellDialog(QWidget *parent, const PollDefinition &d, int row,
                    const QString &addrLabel, const QVector<quint16> &tail)
        : QDialog(parent)
        , m_def(d)
        , m_tail(tail)
    {
        const bool bit = d.isBitFc();
        setWindowTitle(bit ? tr("写入线圈 (Write Coils)")
                           : tr("写入寄存器 (Write Register)"));
        auto *v = new QVBoxLayout(this);
        auto *form = new QFormLayout;
        form->addRow(tr("地址"), new QLabel(addrLabel));
        form->addRow(tr("当前值"),
                     new QLabel(ModbusFormat::formatRegs(d.fmt, d.order, tail.data(),
                                                         int(tail.size()))));
        m_edit = new QLineEdit;
        m_edit->setPlaceholderText(bit ? tr("0 = OFF, 1 = ON") : tr("按显示格式输入"));
        form->addRow(tr("新值"), m_edit);
        m_fc = new QComboBox;
        m_fc->addItem(bit ? tr("05 写单线圈") : tr("06 写单寄存器"), 0);
        if (tail.size() > 1)
            m_fc->addItem(bit ? tr("15 写多线圈 (至末行共 %1 行)").arg(tail.size())
                              : tr("16 写多寄存器 (至末行共 %1 行)").arg(tail.size()),
                          1);
        form->addRow(tr("写功能码"), m_fc);
        v->addLayout(form);
        m_autoClose = new QCheckBox(tr("响应 OK 自动关闭"));
        m_autoClose->setChecked(true);
        v->addWidget(m_autoClose);
        m_err = new QLabel;
        m_err->hide();
        v->addWidget(m_err);
        auto *btnRow = new QHBoxLayout;
        btnRow->addStretch();
        m_btnCancel = new QPushButton(tr("取消"));
        m_btnWrite = new QPushButton(tr("写入"));
        m_btnWrite->setDefault(true);
        btnRow->addWidget(m_btnCancel);
        btnRow->addWidget(m_btnWrite);
        v->addLayout(btnRow);
        connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    }

    // 解析输入; fcCode = 实际功能码 (5/6/15/16), vals = 待写寄存器序列
    bool takeValues(int *fcCode, QVector<quint16> *vals)
    {
        quint16 regs[4] = {};
        const int rc = ModbusFormat::regCount(m_def.fmt);
        if (!ModbusFormat::parseInput(m_def.fmt, m_def.order, m_edit->text(), regs, rc))
        {
            m_err->setStyleSheet(QStringLiteral("color:#dc2626;"));
            m_err->setText(tr("无法解析新值"));
            m_err->show();
            return false;
        }
        QVector<quint16> out;
        for (int i = 0; i < rc; ++i)
            out.append(regs[i]);
        const bool multi = m_fc->currentData().toInt() != 0;
        if (multi)
            for (int i = rc; i < m_tail.size(); ++i) // 余行取网格当前值
                out.append(m_tail[i]);
        *fcCode = m_def.isBitFc() ? (multi ? 15 : 5) : (multi ? 16 : 6);
        *vals = out;
        return true;
    }

    QPushButton *writeBtn() const { return m_btnWrite; }
    bool autoClose() const { return m_autoClose->isChecked(); }
    void showResult(bool ok, const QString &info)
    {
        if (ok && m_autoClose->isChecked())
        {
            accept();
            return;
        }
        if (ok)
        {
            m_err->setStyleSheet(QStringLiteral("color:#16a34a;"));
            m_err->setText(tr("写入 OK — %1").arg(info));
        }
        else
        {
            m_err->setStyleSheet(QStringLiteral("color:#dc2626;"));
            m_err->setText(info);
            m_btnWrite->setEnabled(true);
        }
        m_err->show();
    }

private:
    PollDefinition m_def;
    QVector<quint16> m_tail;
    QLineEdit *m_edit = nullptr;
    QComboBox *m_fc = nullptr;
    QCheckBox *m_autoClose = nullptr;
    QLabel *m_err = nullptr;
    QPushButton *m_btnWrite = nullptr;
    QPushButton *m_btnCancel = nullptr;
};

} // namespace DefDialogs
