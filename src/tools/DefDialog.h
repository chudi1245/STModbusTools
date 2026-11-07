#pragma once

// ============================================================================
// DefDialog.h — F8 读/写定义对话框 (对标 Modbus Poll Read/Write Definition)
//   从站/功能码(01-04 读 + 05/06/15/16 写)/地址/数量/扫描速率/启用/出错停
//   + 显示组: 隐藏别名列 · PLC 地址 Base1 · 单元格内显示地址;
//   读功能 (01-04) 支持 多段编辑器: 多个不连续地址段 (同从站+同功能码);
//   写功能仍为单一 地址/数量;
//   地址框接受 协议地址 0-65535 或 PLC 段地址 (1-9999 线圈, 10001- 离散,
//   30001- 输入, 40001- 保持; 6 位 410001 同式), 区域与功能码不符时自动切
// ============================================================================

#include "PollDefinition.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace DefDialog {

// ---- PLC 段地址换算 (纯函数, 可测) ----
// region: -1=协议地址直输 0=线圈 1=离散输入 3=输入寄存器 4=保持寄存器
struct PlcAddrParse
{
    int region = -1;
    int addr = 0;
    bool ok = false;
};

inline PlcAddrParse parsePlcAddress(const QString &text)
{
    PlcAddrParse r;
    QString t = text.trimmed();
    if (t.isEmpty())
        return r;
    // "4x00001" 风格前缀 → 剥掉
    static const struct
    {
        const char *p;
        int region;
    } kPrefix[] = {{"0x", 0}, {"1x", 1}, {"3x", 3}, {"4x", 4}};
    for (const auto &pre : kPrefix)
    {
        if (t.startsWith(QLatin1String(pre.p), Qt::CaseInsensitive))
        {
            bool ok = false;
            const qlonglong n = t.mid(2).toLongLong(&ok);
            if (!ok || n < 1 || n > 65536)
                return r;
            r.region = pre.region;
            r.addr = int(n - 1);
            r.ok = true;
            return r;
        }
    }
    bool ok = false;
    const qlonglong n = t.toLongLong(&ok);
    if (!ok || n < 0)
        return r;
    if (n <= 65535)
    {
        // 纯数字 = 协议地址 (区域不限定)。5 位 PLC 段 (40001) 与协议地址区间
        // 重叠且会误伤常见寄存器地址 (如 160), 故 PLC 风格仅认 6 位段/前缀
        r.region = -1;
        r.addr = int(n);
        r.ok = true;
        return r;
    }
    // PLC 段 6 位: 首位 1/3/4 + 5 位 Base1 序号 (400001 → 保持 0, 410001 → 保持 10000)
    qlonglong base = -1;
    int region = -1;
    if (n >= 100001 && n <= 165536)
    {
        base = 100001;
        region = 1;
    }
    else if (n >= 300001 && n <= 365536)
    {
        base = 300001;
        region = 3;
    }
    else if (n >= 400001 && n <= 465536)
    {
        base = 400001;
        region = 4;
    }
    if (base < 0)
        return r;
    const qlonglong addr = n - base; // base 1 → base 0
    if (addr > 65535)
        return r;
    r.region = region;
    r.addr = int(addr);
    r.ok = true;
    return r;
}

inline int regionDefaultFc(int region)
{
    switch (region)
    {
    case 0: return 1;
    case 1: return 2;
    case 3: return 4;
    case 4: return 3;
    default: return 3;
    }
}

inline int fcRegion(int fc)
{
    switch (fc)
    {
    case 1:
    case 5: return 0;
    case 2: return 1;
    case 4: return 3;
    default: return 4; // 03/06/16
    }
}

// ---- F8 对话框 (控件带 objectName 供测试; apply() 读控件 → def) ----
class DefDialog : public QDialog
{
public:
    explicit DefDialog(QWidget *parent, PollDefinition &def)
        : QDialog(parent)
        , m_def(def)
    {
        setWindowTitle(tr("读/写定义 (Read/Write Definition) — F8"));
        auto *root = new QVBoxLayout(this);

        m_form = new QFormLayout;
        m_form->setHorizontalSpacing(12);
        m_form->setVerticalSpacing(8);
        m_slave = new QSpinBox;
        m_slave->setObjectName(QStringLiteral("f8.slave"));
        m_slave->setRange(0, 247);
        m_slave->setValue(def.slave);
        m_slave->setToolTip(tr("1-247; 0 仅对写功能有效 (广播)"));
        m_form->addRow(tr("从站 ID (Slave ID)"), m_slave);

        m_fc = new QComboBox;
        m_fc->setObjectName(QStringLiteral("f8.fc"));
        struct FcItem
        {
            int code;
            const char *text;
        };
        static const FcItem fcs[] = {
            {1, "01 读线圈 (Read Coils)"},
            {2, "02 读离散输入 (Read Discrete Inputs)"},
            {3, "03 读保持寄存器 (Read Holding Registers)"},
            {4, "04 读输入寄存器 (Read Input Registers)"},
            {5, "05 写单线圈 (Write Single Coil)"},
            {6, "06 写单寄存器 (Write Single Register)"},
            {15, "15 写多线圈 (Write Multiple Coils)"},
            {16, "16 写多寄存器 (Write Multiple Registers)"},
        };
        for (const FcItem &f : fcs)
            m_fc->addItem(QString::fromUtf8(f.text), f.code);
        m_fc->setCurrentIndex(qMax(0, m_fc->findData(def.fc)));
        m_form->addRow(tr("功能码 (Function)"), m_fc);

        m_addr = new QLineEdit(QString::number(def.addr));
        m_addr->setObjectName(QStringLiteral("f8.addr"));
        m_addr->setToolTip(tr("协议地址 0-65535; PLC 风格: 6 位段地址 (400001 → 保持 0)"
                              "或前缀 4x00001 / 0x00001"));
        m_form->addRow(tr("地址 (Address)"), m_addr);

        m_qty = new QSpinBox;
        m_qty->setObjectName(QStringLiteral("f8.qty"));
        m_qty->setRange(1, 2000);
        m_qty->setValue(def.qty);
        m_form->addRow(tr("数量 (Quantity)"), m_qty);

        // ---- 多段编辑器 (读功能 01-04; 同从站+同功能码, 段间可不连续) ----
        m_segBox = new QWidget;
        auto *sv = new QVBoxLayout(m_segBox);
        sv->setContentsMargins(0, 0, 0, 0);
        m_segTable = new QTableWidget(0, 2, m_segBox);
        m_segTable->setObjectName(QStringLiteral("f8.segs"));
        m_segTable->setHorizontalHeaderLabels({tr("起始地址 (Address)"),
                                               tr("数量 (Quantity)")});
        m_segTable->verticalHeader()->hide();
        m_segTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_segTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_segTable->setMinimumHeight(120);
        sv->addWidget(m_segTable);
        auto *segBtns = new QHBoxLayout;
        auto *btnAdd = new QPushButton(tr("添加段"));
        auto *btnDel = new QPushButton(tr("删除选中段"));
        m_segTotal = new QLabel;
        segBtns->addWidget(btnAdd);
        segBtns->addWidget(btnDel);
        segBtns->addStretch();
        segBtns->addWidget(m_segTotal);
        sv->addLayout(segBtns);
        m_form->addRow(tr("地址段 (读)"), m_segBox);
        for (const PollDefinition::Segment &s : def.segments())
            addSegRow(s.addr, s.qty);
        connect(btnAdd, &QPushButton::clicked, this, [this] {
            const int r = m_segTable->rowCount();
            addSegRow(0, 10);
            m_segTable->editItem(m_segTable->item(r, 0));
        });
        connect(btnDel, &QPushButton::clicked, this, [this] {
            const int r = m_segTable->currentRow();
            if (r >= 0 && m_segTable->rowCount() > 1)
            {
                m_segTable->removeRow(r);
                updateSegTotal();
            }
        });
        connect(m_segTable, &QTableWidget::cellChanged, this,
                [this](int, int) { updateSegTotal(); });
        updateSegTotal();

        m_rate = new QSpinBox;
        m_rate->setObjectName(QStringLiteral("f8.rate"));
        m_rate->setRange(0, 3600000);
        m_rate->setValue(def.scanRateMs);
        m_rate->setSuffix(tr(" ms"));
        m_rate->setSpecialValueText(tr("单次 (F6)"));
        m_form->addRow(tr("扫描速率 (Scan Rate)"), m_rate);
        root->addLayout(m_form);

        auto *opts = new QHBoxLayout;
        m_disabled = new QCheckBox(tr("读/写禁用 (Disabled)"));
        m_disabled->setObjectName(QStringLiteral("f8.disabled"));
        m_disabled->setChecked(!def.enabled);
        m_autoStop = new QCheckBox(tr("出错自动停 (Disable on error)"));
        m_autoStop->setObjectName(QStringLiteral("f8.autoStop"));
        m_autoStop->setChecked(def.autoStopOnError);
        opts->addWidget(m_disabled);
        opts->addWidget(m_autoStop);
        opts->addStretch();
        root->addLayout(opts);

        // ---- 显示组 ----
        auto *dispBox = new QLabel(tr("显示"));
        dispBox->setStyleSheet(QStringLiteral("font-weight:bold;"));
        root->addWidget(dispBox);
        auto *disp = new QHBoxLayout;
        m_hideNames = new QCheckBox(tr("隐藏别名列"));
        m_hideNames->setObjectName(QStringLiteral("f8.hideNames"));
        m_hideNames->setChecked(def.hideNames);
        m_plc = new QCheckBox(tr("PLC 地址 (Base 1)"));
        m_plc->setObjectName(QStringLiteral("f8.plcBase1"));
        m_plc->setChecked(def.plcBase1);
        m_inCell = new QCheckBox(tr("单元格内显示地址"));
        m_inCell->setObjectName(QStringLiteral("f8.addrInCell"));
        m_inCell->setChecked(def.addrInCell);
        disp->addWidget(m_hideNames);
        disp->addWidget(m_plc);
        disp->addWidget(m_inCell);
        disp->addStretch();
        root->addLayout(disp);

        auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        root->addWidget(btns);
        connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);

        // 数量上限随功能码; 读功能 = 段编辑器, 写功能 = 单地址/数量;
        // 地址段自动切功能码
        connect(m_fc, &QComboBox::currentIndexChanged, this,
                [this](int) { clampQty(); syncFcUi(); });
        connect(m_addr, &QLineEdit::textEdited, this, [this] {
            const PlcAddrParse p = parsePlcAddress(m_addr->text());
            if (!p.ok || p.region < 0)
                return;
            const int wantFc = regionDefaultFc(p.region);
            if (fcRegion(m_fc->currentData().toInt()) != p.region)
            {
                // 区域与功能码不符: 自动切换 (输入 40001 而当前 01 → 切 03)
                const int i = m_fc->findData(wantFc);
                if (i >= 0)
                    m_fc->setCurrentIndex(i);
            }
        });
        syncFcUi(); // 初始可见性 (读 = 段编辑器 / 写 = 单地址数量) + 数量钳制
    }

    // 控件 → m_def; 校验失败返回 false (err 文本可选带回, 由调用方呈现)
    bool apply(QString *err = nullptr)
    {
        const int fc = m_fc->currentData().toInt();
        const bool isRead = fc >= 1 && fc <= 4;
        QVector<PollDefinition::Segment> segs;
        if (!isRead)
        {
            const PlcAddrParse p = parsePlcAddress(m_addr->text());
            if (!p.ok)
            {
                if (err)
                    *err = tr("地址无效: %1").arg(m_addr->text());
                return false;
            }
            segs.append({quint16(p.addr), m_qty->value()});
        }
        else
        {
            PollDefinition tmp;
            tmp.fc = fc;
            for (int r = 0; r < m_segTable->rowCount(); ++r)
            {
                const QTableWidgetItem *aItem = m_segTable->item(r, 0);
                const QTableWidgetItem *qItem = m_segTable->item(r, 1);
                const QString aText = aItem ? aItem->text().trimmed() : QString();
                const PlcAddrParse p = parsePlcAddress(aText);
                if (!p.ok)
                {
                    if (err)
                        *err = tr("第 %1 段地址无效: %2").arg(r + 1).arg(aText);
                    return false;
                }
                if (p.region >= 0 && fcRegion(tmp.fc) != p.region)
                {
                    if (err)
                        *err = tr("第 %1 段地址区域与功能码不符: %2").arg(r + 1).arg(aText);
                    return false;
                }
                bool ok = false;
                const int q = qItem ? qItem->text().toInt(&ok) : 0;
                if (!ok || q < 1)
                {
                    if (err)
                        *err = tr("第 %1 段数量无效 (需 ≥1)").arg(r + 1);
                    return false;
                }
                if (q > tmp.maxQty())
                {
                    if (err)
                        *err = tr("第 %1 段数量超出单帧上限 %2").arg(r + 1).arg(tmp.maxQty());
                    return false;
                }
                segs.append({quint16(p.addr), q});
            }
            if (segs.isEmpty())
            {
                if (err)
                    *err = tr("至少需要一个地址段");
                return false;
            }
        }
        m_def.slave = m_slave->value();
        m_def.fc = fc;
        m_def.addr = quint16(segs[0].addr);
        m_def.qty = segs[0].qty;
        m_def.segs = segs; // 单段经 clamp 回落 addr/qty 表示
        m_def.scanRateMs = m_rate->value();
        m_def.enabled = !m_disabled->isChecked();
        m_def.autoStopOnError = m_autoStop->isChecked();
        m_def.hideNames = m_hideNames->isChecked();
        m_def.plcBase1 = m_plc->isChecked();
        m_def.addrInCell = m_inCell->isChecked();
        m_def.clamp();
        return true;
    }

private:
    bool isReadFc() const
    {
        const int fc = m_fc->currentData().toInt();
        return fc >= 1 && fc <= 4;
    }

    // 读功能 = 段编辑器, 写功能 = 单地址/数量
    void syncFcUi()
    {
        const bool isRead = isReadFc();
        m_form->setRowVisible(m_addr, !isRead);
        m_form->setRowVisible(m_qty, !isRead);
        m_segBox->setVisible(isRead);
        clampQty();
    }

    void clampQty()
    {
        PollDefinition tmp;
        tmp.fc = m_fc->currentData().toInt();
        m_qty->setMaximum(tmp.maxQty());
        if (m_qty->value() > m_qty->maximum())
            m_qty->setValue(m_qty->maximum());
    }

    void addSegRow(quint16 addr, int qty)
    {
        const int r = m_segTable->rowCount();
        m_segTable->insertRow(r);
        m_segTable->setItem(r, 1, new QTableWidgetItem(QString::number(qty)));
        m_segTable->setItem(r, 0, new QTableWidgetItem(QString::number(addr)));
    }

    void updateSegTotal()
    {
        int total = 0;
        for (int r = 0; r < m_segTable->rowCount(); ++r)
        {
            const QTableWidgetItem *it = m_segTable->item(r, 1);
            if (it)
                total += qMax(0, it->text().toInt());
        }
        m_segTotal->setText(tr("共 %1 行").arg(total));
    }

    PollDefinition &m_def;
    QFormLayout *m_form = nullptr;
    QSpinBox *m_slave = nullptr;
    QComboBox *m_fc = nullptr;
    QLineEdit *m_addr = nullptr;
    QSpinBox *m_qty = nullptr;
    QSpinBox *m_rate = nullptr;
    QWidget *m_segBox = nullptr;
    QTableWidget *m_segTable = nullptr;
    QLabel *m_segTotal = nullptr;
    QCheckBox *m_disabled = nullptr;
    QCheckBox *m_autoStop = nullptr;
    QCheckBox *m_hideNames = nullptr;
    QCheckBox *m_plc = nullptr;
    QCheckBox *m_inCell = nullptr;
};

} // namespace DefDialog
