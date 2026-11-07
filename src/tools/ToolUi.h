#pragma once

// ============================================================================
// ToolUi.h — 调试工具窗口共享样式 (网页工具箱 → Qt 移植)
//   卡片 = 白底圆角描边; 结果框 = 深底绿字等宽 (网页 result-box 口径);
//   开关按钮 = 可选中描边式; 提示条 = 底部彩色文字 3s 自动消失
// ============================================================================

#include <QBasicTimer>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QTimerEvent>
#include <QVBoxLayout>
#include <QWidget>

namespace ToolUi {

// ---- 卡片容器: 标题 + 纵向布局 ----
inline QFrame *card(const QString &title)
{
    auto *f = new QFrame;
    f->setObjectName(QStringLiteral("toolCard"));
    f->setStyleSheet(QStringLiteral(
        "QFrame#toolCard { background:white; border:1px solid #d8e0ea;"
        "border-radius:10px; }"));
    auto *v = new QVBoxLayout(f);
    v->setContentsMargins(14, 12, 14, 14);
    v->setSpacing(10);
    if (!title.isEmpty())
    {
        auto *t = new QLabel(title);
        QFont fo = t->font();
        fo.setBold(true);
        fo.setPixelSize(15);
        t->setFont(fo);
        t->setStyleSheet(QStringLiteral("color:#1e293b; border:none;"));
        v->addWidget(t);
    }
    return f;
}

inline QVBoxLayout *cardLayout(QFrame *card) { return qobject_cast<QVBoxLayout *>(card->layout()); }

// ---- 深底绿字等宽结果框 (网页 .result-box) ----
inline QLabel *resultLabel(const QString &text = QString())
{
    auto *l = new QLabel(text);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    l->setStyleSheet(QStringLiteral(
        "background:#111827; color:#4ade80;"
        "font-family:SF Mono,'Cascadia Mono',Consolas,'Courier New',monospace; font-size:13px;"
        "padding:10px; border-radius:6px; border:none;"));
    return l;
}

// ---- 表格内的等宽指令单元格 ----
inline QLabel *monoCell(const QString &text)
{
    auto *l = new QLabel(text);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setStyleSheet(QStringLiteral(
        "background:#111827; color:#4ade80;"
        "font-family:SF Mono,'Cascadia Mono',Consolas,'Courier New',monospace; font-size:12px;"
        "padding:6px; border-radius:4px; border:none;"));
    return l;
}

// ---- 可选中开关按钮 (进制/协议/符号选择) ----
constexpr const char *kToggleQss =
    "QPushButton { background:white; border:1px solid #cbd5e1; border-radius:6px;"
    "             padding:6px 12px; color:#374151; }"
    "QPushButton:hover { border-color:#93c5fd; }"
    "QPushButton:checked { background:#eff6ff; border:1px solid #3b82f6;"
    "                      color:#1d4ed8; font-weight:bold; }";

inline QPushButton *toggle(const QString &text, bool checked = false)
{
    auto *b = new QPushButton(text);
    b->setCheckable(true);
    b->setChecked(checked);
    b->setStyleSheet(QLatin1String(kToggleQss));
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

// ---- 小徽章 (RTU/TCP 标签) ----
inline QLabel *badge(const QString &text)
{
    auto *l = new QLabel(text);
    l->setStyleSheet(QStringLiteral(
        "background:#eef2ff; color:#4338ca; border-radius:4px;"
        "padding:1px 8px; font-size:12px; font-weight:bold;"));
    return l;
}

// ---- 流程图节点框 (校准向导流程图) ----
inline QLabel *flowNode(const QString &text)
{
    auto *l = new QLabel(text);
    l->setAlignment(Qt::AlignCenter);
    l->setStyleSheet(QStringLiteral(
        "background:white; border:1px solid #cbd5e1; border-radius:6px;"
        "padding:4px 8px; color:#334155;"));
    return l;
}

inline QLabel *flowArrow()
{
    auto *l = new QLabel(QStringLiteral("↓"));
    l->setAlignment(Qt::AlignCenter);
    l->setStyleSheet(QStringLiteral("color:#94a3b8; border:none; background:transparent;"));
    return l;
}

// ---- 底部提示条: showMsg 后 3s 自动消失 (网页 toast 的非阻塞替代) ----
class TipLabel : public QLabel
{
public:
    explicit TipLabel(QWidget *parent = nullptr) : QLabel(parent)
    {
        hide();
        setWordWrap(true);
    }

    void showMsg(const QString &text, bool ok)
    {
        setText(text);
        setStyleSheet(QStringLiteral("color:%1; font-weight:bold; border:none;")
                          .arg(ok ? QStringLiteral("#16a34a") : QStringLiteral("#dc2626")));
        show();
        m_timer.start(3000, this);
    }

protected:
    void timerEvent(QTimerEvent *e) override
    {
        if (e->timerId() == m_timer.timerId())
        {
            hide();
            m_timer.stop();
            return;
        }
        QLabel::timerEvent(e);
    }

private:
    QBasicTimer m_timer;
};

} // namespace ToolUi
