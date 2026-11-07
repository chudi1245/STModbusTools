#pragma once

// ============================================================================
// Theme.h — UI 设计令牌唯一来源 (色板/字号/间距/圆角/语义按钮)
//   规则: 颜色/字号/间距一律引用此处常量, 禁止在页面里写裸色值;
//   语义色只表达状态 (ok/warn/danger) 与主操作 (accent), 不作装饰。
//   字号一律 px (96dpi 基准, 见 main.cpp 说明), 两平台逐像素一致。
// ============================================================================

namespace Theme {

// ---- 中性色板 (浅色系) ----
inline constexpr const char *Bg = "#f5f6f8";     // 窗口底
inline constexpr const char *Panel = "#ffffff";  // 面板/卡片
inline constexpr const char *Border = "#d8dce2"; // 边框/分隔
inline constexpr const char *Text = "#1a1d21";   // 主文字
inline constexpr const char *Text2 = "#6b7280";  // 次文字/标签
inline constexpr const char *Hover = "#eceff3";  // 悬停底

// ---- 语义色 ----
inline constexpr const char *Accent = "#1e88e5";   // 主操作/选中
inline constexpr const char *AccentDk = "#1565c0"; // 主操作按下/描边
inline constexpr const char *Ok = "#2e9e5b";       // 合格/已连接/稳定
inline constexpr const char *Warn = "#e8a33d";     // 警示/次级危险
inline constexpr const char *Danger = "#d64541";   // 危险操作/超限/断开

// ---- 字号阶梯 (px) ----
inline constexpr int FontAux = 12;   // 辅助/标签
inline constexpr int FontBody = 13;  // 正文 (全局默认)
inline constexpr int FontTitle = 15; // 小标题
inline constexpr int FontNum = 20;   // 读数
inline constexpr int FontBig = 34;   // 大数字 (测量主值/峰值)

// ---- 间距阶梯 (px) / 圆角 ----
inline constexpr int SpaceS = 4, SpaceM = 8, SpaceL = 12, SpaceXL = 16;
inline constexpr int Radius = 4;

// ---- 数字字体栈 (等宽; 平台依序回退, 修 Consolas 缺字) ----
inline constexpr const char *FontNumeric =
    "'SF Mono','Cascadia Mono','Consolas','Courier New',monospace";

// ---- 语义按钮 ----
// QSS 规则 (property 选择器 QPushButton[btn=...]) 统一在 resources/app.qss;
// 用法: btn->setProperty("btn", "primary") — 取值见下方取值表。
//   primary=主操作(实心) secondary=辅助(描边) warn=次级危险(描边)
//   danger=危险(实心) ghost=工具条/次级文字按钮
inline constexpr const char *BtnPrimary = "primary";
inline constexpr const char *BtnSecondary = "secondary";
inline constexpr const char *BtnWarn = "warn";
inline constexpr const char *BtnDanger = "danger";
inline constexpr const char *BtnGhost = "ghost";

} // namespace Theme
