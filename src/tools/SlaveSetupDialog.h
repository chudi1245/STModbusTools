#pragma once

// ============================================================================
// SlaveSetupDialog.h — 从机设备配置对话框 (串口连接挂多从机)
//   两种模式 (单选):
//     数量模式: 指定从机数量 N → 地址 1..N 依次轮询;
//     手动模式: 指定地址列表 (支持 "1,3,5-8" 逗号/范围语法, 1..247 钳制)。
//   重开对话框回显当前列表 (恰为 1..N 时预选数量模式), 确定后整体替换。
//   parseAddresses/formatAddresses 为纯函数 (可测)。
// ============================================================================

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace SlaveSetupDialog {

// 解析 "1, 3,5-8" (逗号/中文逗号/分号/空格分隔, 支持 a-b 范围);
// 有非法 token 或无有效地址 → false; 各值钳制 1..247, 升序去重
inline bool parseAddresses(const QString &text, QList<int> *out)
{
    out->clear();
    QString t = text;
    t.replace(QChar(u'，'), QLatin1Char(','))
        .replace(QChar(u'；'), QLatin1Char(','))
        .replace(QLatin1Char(';'), QLatin1Char(','))
        .replace(QLatin1Char(' '), QLatin1Char(','));
    const QStringList tokens = t.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &raw : tokens)
    {
        const QString tok = raw.trimmed();
        bool okA = false, okB = false;
        int a = 0, b = 0;
        const int dash = tok.indexOf(QLatin1Char('-'));
        if (dash < 0)
        {
            a = b = tok.toInt(&okA);
            okB = true;
        }
        else if (dash > 0 && dash == tok.lastIndexOf(QLatin1Char('-')))
        {
            a = tok.left(dash).toInt(&okA);
            b = tok.mid(dash + 1).toInt(&okB);
        }
        if (!okA || !okB)
            return false; // 非 "N" / "N-M" 形式 (含负数/垃圾)
        a = qBound(1, a, 247);
        b = qBound(1, b, 247);
        if (a > b)
            std::swap(a, b);
        for (int v = a; v <= b; ++v)
            if (!out->contains(v))
                out->append(v);
    }
    if (out->isEmpty())
        return false;
    std::sort(out->begin(), out->end());
    return true;
}

// 反向: 升序列表 → 连续段压缩的 "1,3,5-8" (回显用)
inline QString formatAddresses(const QList<int> &list)
{
    QStringList parts;
    int i = 0;
    while (i < list.size())
    {
        int j = i;
        while (j + 1 < list.size() && list.at(j + 1) == list.at(j) + 1)
            ++j;
        parts << QString::number(list.at(i));
        if (j > i)
            parts.last() += QStringLiteral("-%1").arg(list.at(j));
        i = j + 1;
    }
    return parts.join(QLatin1Char(','));
}

// 确定 → 写回 addresses 并返回 true
inline bool edit(QWidget *parent, QList<int> *addresses)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("从机设备 (Slave Devices)"));
    auto *root = new QVBoxLayout(&dlg);

    // 当前列表恰为 1..N → 数量模式预选, 否则手动模式
    bool contiguous = !addresses->isEmpty();
    for (int i = 0; i < addresses->size(); ++i)
    {
        if (addresses->at(i) != i + 1)
        {
            contiguous = false;
            break;
        }
    }

    auto *byCount = new QRadioButton(
        QObject::tr("数量模式 — 指定从机数量, 地址 1..N 依次轮询"), &dlg);
    byCount->setObjectName(QStringLiteral("slvSetup.byCount"));
    auto *countRow = new QHBoxLayout;
    auto *countSpin = new QSpinBox(&dlg);
    countSpin->setObjectName(QStringLiteral("slvSetup.count"));
    countSpin->setRange(1, 247);
    countSpin->setValue(contiguous ? addresses->size() : 1);
    countRow->addSpacing(20);
    countRow->addWidget(new QLabel(QObject::tr("从机数量 (N):"), &dlg));
    countRow->addWidget(countSpin);
    countRow->addStretch();
    root->addWidget(byCount);
    root->addLayout(countRow);

    root->addSpacing(8);
    auto *manual = new QRadioButton(QObject::tr("手动模式 — 指定每个从机的地址"), &dlg);
    manual->setObjectName(QStringLiteral("slvSetup.manual"));
    auto *addrRow = new QHBoxLayout;
    addrRow->addSpacing(20);
    addrRow->addWidget(new QLabel(QObject::tr("地址列表:"), &dlg));
    auto *addrEdit = new QLineEdit(&dlg);
    addrEdit->setObjectName(QStringLiteral("slvSetup.addresses"));
    addrEdit->setText(formatAddresses(*addresses));
    addrEdit->setPlaceholderText(QObject::tr("例如: 1,3,5-8 (范围 1..247)"));
    addrEdit->setMinimumWidth(220);
    addrRow->addWidget(addrEdit, 1);
    root->addWidget(manual);
    root->addLayout(addrRow);

    root->addSpacing(4);
    auto *hint = new QLabel(
        QObject::tr("轮询: 按列表顺序逐台 FC03 探测 (地址 0 ×1), 在线/离线由设备树状态点显示"),
        &dlg);
    hint->setStyleSheet(QStringLiteral("color:#64748b;"));
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    root->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    byCount->setChecked(contiguous || addresses->isEmpty());
    manual->setChecked(!byCount->isChecked());
    auto syncMode = [byCount, manual, countSpin, addrEdit]() {
        countSpin->setEnabled(byCount->isChecked());
        addrEdit->setEnabled(manual->isChecked());
    };
    QObject::connect(byCount, &QRadioButton::toggled, &dlg, syncMode);
    QObject::connect(manual, &QRadioButton::toggled, &dlg, syncMode);
    syncMode();

    // 确定前校验: 手动模式解析失败留在对话框内提示
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, [&]() {
        if (byCount->isChecked())
        {
            addresses->clear();
            for (int i = 1; i <= countSpin->value(); ++i)
                addresses->append(i);
            dlg.accept();
            return;
        }
        QList<int> parsed;
        if (!parseAddresses(addrEdit->text(), &parsed))
        {
            QMessageBox::warning(&dlg, QObject::tr("地址列表无效"),
                                 QObject::tr("地址列表无法解析。\n"
                                             "用逗号分隔, 支持范围写法, 例如: 1,3,5-8\n"
                                             "地址范围 1..247。"));
            return;
        }
        *addresses = parsed;
        dlg.accept();
    });

    return dlg.exec() == QDialog::Accepted;
}

} // namespace SlaveSetupDialog
