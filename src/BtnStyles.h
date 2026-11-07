#pragma once

// ============================================================================
// BtnStyles.h — 语义按钮应用层 (Design Token 见 Theme.h, QSS 见 resources/app.qss)
//   Lua 设备档案的 style 字段沿用旧四色命名, 此处映射到语义按钮:
//     green=正向操作→primary  blue=辅助→secondary
//     red=危险→danger         orange=次级危险→warn
//   旧的四色渐变 QSS 已退役 (property 选择器 + 全局 app.qss 取代)。
//   用法: BtnStyles::apply(btn, QStringLiteral("green"));  — 构造期调用即可
// ============================================================================

#include "Theme.h"

#include <QPushButton>
#include <QString>

namespace BtnStyles {

inline void apply(QPushButton *btn, const QString &legacyStyle)
{
    const char *sem = Theme::BtnPrimary; // green / 其他 = 正向操作
    if (legacyStyle == QLatin1String("blue"))
        sem = Theme::BtnSecondary;
    else if (legacyStyle == QLatin1String("red"))
        sem = Theme::BtnDanger;
    else if (legacyStyle == QLatin1String("orange"))
        sem = Theme::BtnWarn;
    btn->setProperty("btn", sem);
}

} // namespace BtnStyles
