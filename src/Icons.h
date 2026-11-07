#pragma once

// ============================================================================
// Icons.h — 统一图标工具: QStyle 标准图标 + 程序绘制补齐 (无外部资源依赖)
// 用法: btn->setIcon(Icons::connect());
// ============================================================================

#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QStyle>
#include <QWidget>
#include <QApplication>

namespace Icons {

// ---- 标准 QIcon (fallback: QStyle standardIcon) ----
inline QIcon std(QStyle::StandardPixmap sp)
{
    return QApplication::style()->standardIcon(sp);
}

// ---- 程序绘制 (矢量 QPainter, 16x16 base, 自动高清缩放) ----
inline QIcon draw(const std::function<void(QPainter &, int)> &fn, int base = 24)
{
    QIcon icon;
    for (int sz : {16, 24, 32, 48})
    {
        qreal scale = static_cast<qreal>(sz) / base;
        QPixmap pm(sz, sz);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(scale, scale);
        fn(p, base);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

// ============================================================================
// 通信类
// ============================================================================
inline QIcon connect()
{
    // 插头 + 绿色圆点
    return draw([](QPainter &p, int s) {
        // 插头线
        p.setPen(QPen(QColor("#1976d2"), 2.2));
        p.setBrush(Qt::NoBrush);
        p.drawLine(s * 0.2, s * 0.5, s * 0.45, s * 0.5);
        // 插头体
        p.setBrush(QColor("#42a5f5"));
        p.drawRoundedRect(s * 0.4, s * 0.3, s * 0.25, s * 0.4, 3, 3);
        // 绿色连接指示
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#4caf50"));
        p.drawEllipse(QPointF(s * 0.78, s * 0.5), s * 0.13, s * 0.13);
    });
}

inline QIcon disconnect()
{
    // 插头 + 红色 X
    return draw([](QPainter &p, int s) {
        p.setPen(QPen(QColor("#1976d2"), 2.2));
        p.drawLine(s * 0.2, s * 0.5, s * 0.45, s * 0.5);
        p.setBrush(QColor("#90a4ae"));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(s * 0.4, s * 0.3, s * 0.25, s * 0.4, 3, 3);
        // 红色断开
        p.setPen(QPen(QColor("#ef5350"), 2.5));
        p.drawLine(s * 0.65, s * 0.3, s * 0.9, s * 0.7);
        p.drawLine(s * 0.9, s * 0.3, s * 0.65, s * 0.7);
    });
}

inline QIcon resume()
{
    // 环形箭头
    return draw([](QPainter &p, int s) {
        p.setPen(QPen(QColor("#ff9800"), 2.4));
        p.setBrush(Qt::NoBrush);
        p.drawArc(s * 0.15, s * 0.15, s * 0.7, s * 0.7, 30 * 16, 280 * 16);
        // 箭头
        QPolygonF tri;
        tri << QPointF(s * 0.85, s * 0.1) << QPointF(s * 0.98, s * 0.35) << QPointF(s * 0.72, s * 0.32);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#ff9800"));
        p.drawPolygon(tri);
    });
}

inline QIcon refresh()
{
    return std(QStyle::SP_BrowserReload);
}

// ============================================================================
// Modbus 主站工具类 (ModbusMasterTool 工具栏)
// ============================================================================
inline QIcon newWindow()
{
    // 文档页 + 绿色加号 (新建数据窗口)
    return draw([](QPainter &p, int s) {
        p.setPen(QPen(QColor("#546e7a"), 2));
        p.setBrush(QColor("#ffffff"));
        p.drawRect(QRectF(s * 0.12, s * 0.08, s * 0.56, s * 0.76));
        p.setPen(QPen(QColor("#90a4ae"), 1.6));
        for (int i = 0; i < 3; ++i)
            p.drawLine(s * 0.2, s * (0.2 + i * 0.16), s * 0.6, s * (0.2 + i * 0.16));
        p.setPen(QPen(QColor("#4caf50"), 2.8, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(s * 0.66, s * 0.7, s * 0.96, s * 0.7);
        p.drawLine(s * 0.81, s * 0.55, s * 0.81, s * 0.85);
    });
}

inline QIcon definition()
{
    // 寄存器表格 + 铅笔 (读/写定义)
    return draw([](QPainter &p, int s) {
        p.setPen(QPen(QColor("#1976d2"), 1.8));
        p.setBrush(QColor("#e3f2fd"));
        p.drawRect(QRectF(s * 0.08, s * 0.1, s * 0.6, s * 0.72));
        p.drawLine(s * 0.08, s * 0.3, s * 0.68, s * 0.3);
        p.drawLine(s * 0.08, s * 0.5, s * 0.68, s * 0.5);
        p.drawLine(s * 0.08, s * 0.7, s * 0.68, s * 0.7);
        p.drawLine(s * 0.28, s * 0.1, s * 0.28, s * 0.82);
        p.drawLine(s * 0.48, s * 0.1, s * 0.48, s * 0.82);
        // 铅笔 (斜置, 压在表格右下)
        p.setPen(QPen(QColor("#ff9800"), 4.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(s * 0.94, s * 0.38, s * 0.62, s * 0.7);
        p.setPen(QPen(QColor("#e65100"), 4.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(s * 0.62, s * 0.7, s * 0.52, s * 0.8);
    });
}

inline QIcon readOnce()
{
    // 单次读写: 环形箭头 + 数字 1
    return draw([](QPainter &p, int s) {
        p.setPen(QPen(QColor("#1976d2"), 2.4));
        p.setBrush(Qt::NoBrush);
        p.drawArc(s * 0.1, s * 0.1, s * 0.8, s * 0.8, 30 * 16, 300 * 16);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#1976d2"));
        QPolygonF tri;
        tri << QPointF(s * 0.86, s * 0.06) << QPointF(s * 1.0, s * 0.28)
            << QPointF(s * 0.72, s * 0.26);
        p.drawPolygon(tri);
        // 数字 1 (提笔 + 竖笔)
        p.setPen(QPen(QColor("#1565c0"), 2.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(s * 0.48, s * 0.36, s * 0.6, s * 0.28);
        p.drawLine(s * 0.6, s * 0.28, s * 0.6, s * 0.72);
    });
}

inline QIcon pause()
{
    // 扫描暂停: 双竖条
    return draw([](QPainter &p, int s) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#f59e0b"));
        p.drawRoundedRect(s * 0.22, s * 0.14, s * 0.2, s * 0.72, 2, 2);
        p.drawRoundedRect(s * 0.58, s * 0.14, s * 0.2, s * 0.72, 2, 2);
    });
}

// ============================================================================
// 数据/日志类
// ============================================================================
inline QIcon record()
{
    // 录制圆点
    return draw([](QPainter &p, int s) {
        p.setPen(QPen(QColor("#ef5350"), 2));
        p.setBrush(QColor("#ef5350"));
        p.drawEllipse(QPointF(s * 0.5, s * 0.5), s * 0.3, s * 0.3);
        // 装饰环
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor("#ffcdd2"), 1.5));
        p.drawEllipse(QPointF(s * 0.5, s * 0.5), s * 0.4, s * 0.4);
    });
}

inline QIcon database()
{
    // 数据库圆柱
    return draw([](QPainter &p, int s) {
        p.setPen(QPen(QColor("#7e57c2"), 2));
        p.setBrush(QColor("#b39ddb"));
        // 顶椭圆
        p.drawEllipse(QPointF(s * 0.5, s * 0.25), s * 0.3, s * 0.1);
        // 侧面
        p.drawLine(s * 0.2, s * 0.25, s * 0.2, s * 0.75);
        p.drawLine(s * 0.8, s * 0.25, s * 0.8, s * 0.75);
        // 中环
        p.drawArc(QRectF(s * 0.2, s * 0.4, s * 0.6, s * 0.2), 0, 180 * 16);
        // 底弧
        p.drawArc(QRectF(s * 0.2, s * 0.65, s * 0.6, s * 0.2), 0, 180 * 16);
    });
}

// ============================================================================
// 标定类
// ============================================================================
inline QIcon calibrate()
{
    // 天平/砝码
    return draw([](QPainter &p, int s) {
        // 天平横梁
        p.setPen(QPen(QColor("#5c6bc0"), 2.2));
        p.drawLine(s * 0.2, s * 0.25, s * 0.8, s * 0.25);
        // 立柱
        p.drawLine(s * 0.5, s * 0.25, s * 0.5, s * 0.8);
        // 底座
        p.setPen(QPen(QColor("#5c6bc0"), 3));
        p.drawLine(s * 0.3, s * 0.85, s * 0.7, s * 0.85);
        // 左盘
        p.setPen(QPen(QColor("#5c6bc0"), 1.8));
        p.drawArc(QRectF(s * 0.1, s * 0.25, s * 0.25, s * 0.25), 0, 180 * 16);
        // 右盘
        p.drawArc(QRectF(s * 0.65, s * 0.25, s * 0.25, s * 0.25), 0, 180 * 16);
    });
}

inline QIcon weight()
{
    // 经典砝码 (梯形 + 提环)
    return draw([](QPainter &p, int s) {
        // 提环
        p.setPen(QPen(QColor("#78909c"), 2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(s * 0.5, s * 0.18), s * 0.1, s * 0.1);
        // 砝码体 (梯形)
        QPolygonF body;
        body << QPointF(s * 0.25, s * 0.85) << QPointF(s * 0.75, s * 0.85)
             << QPointF(s * 0.65, s * 0.3) << QPointF(s * 0.35, s * 0.3);
        p.setPen(QPen(QColor("#546e7a"), 2));
        p.setBrush(QColor("#90a4ae"));
        p.drawPolygon(body);
    });
}

inline QIcon segments()
{
    // 多段折线 (校准曲线)
    return draw([](QPainter &p, int s) {
        p.setPen(QPen(QColor("#ab47bc"), 2.2));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(s * 0.1, s * 0.8), QPointF(s * 0.35, s * 0.5));
        p.drawLine(QPointF(s * 0.35, s * 0.5), QPointF(s * 0.65, s * 0.35));
        p.drawLine(QPointF(s * 0.65, s * 0.35), QPointF(s * 0.9, s * 0.15));
        // 节点
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#ab47bc"));
        for (auto pt : {QPointF(s * 0.35, s * 0.5), QPointF(s * 0.65, s * 0.35)})
            p.drawEllipse(pt, 2.5, 2.5);
        // 对角虚线 (理想)
        p.setPen(QPen(QColor("#ce93d8"), 1.2, Qt::DashLine));
        p.drawLine(QPointF(s * 0.1, s * 0.8), QPointF(s * 0.9, s * 0.15));
    });
}

inline QIcon home()
{
    return std(QStyle::SP_ComputerIcon);
}

inline QIcon settings()
{
    // 齿轮
    return draw([](QPainter &p, int s) {
        p.setPen(QPen(QColor("#607d8b"), 2));
        p.setBrush(QColor("#b0bec5"));
        p.drawEllipse(QPointF(s * 0.5, s * 0.5), s * 0.22, s * 0.22);
        // 齿
        p.setPen(QPen(QColor("#607d8b"), 3));
        for (int i = 0; i < 8; ++i)
        {
            qreal a = i * M_PI / 4;
            QPointF c1(s * 0.5 + cos(a) * s * 0.38, s * 0.5 + sin(a) * s * 0.38);
            QPointF c2(s * 0.5 + cos(a) * s * 0.28, s * 0.5 + sin(a) * s * 0.28);
            p.drawLine(c1, c2);
        }
    });
}

inline QIcon plugin()
{
    // 拼图块 (插件)
    return draw([](QPainter &p, int s) {
        const QColor body("#26a69a"), edge("#00695c");
        p.setPen(QPen(edge, 2));
        p.setBrush(body);
        // 主体
        p.drawRoundedRect(s * 0.12, s * 0.3, s * 0.58, s * 0.58, 4, 4);
        // 上侧凸起
        p.drawEllipse(QPointF(s * 0.41, s * 0.3), s * 0.11, s * 0.11);
        // 右侧凸起
        p.drawEllipse(QPointF(s * 0.7, s * 0.59), s * 0.11, s * 0.11);
        // 上沿凹槽 (挖空效果)
        p.setBrush(QColor("#e0f2f1"));
        p.drawEllipse(QPointF(s * 0.41, s * 0.3), s * 0.06, s * 0.06);
    });
}

inline QIcon serialPort()
{
    // 串口: 接口体 + 引脚 + 线缆
    return draw([](QPainter &p, int s) {
        p.setPen(QPen(QColor("#37474f"), 2));
        p.setBrush(QColor("#78909c"));
        p.drawRoundedRect(s * 0.18, s * 0.32, s * 0.64, s * 0.4, 3, 3);
        p.setPen(QPen(QColor("#263238"), 2.4));
        for (int i = 0; i < 4; ++i)
            p.drawLine(s * (0.3 + i * 0.14), s * 0.32, s * (0.3 + i * 0.14), s * 0.18);
        p.setPen(QPen(QColor("#37474f"), 2.4));
        p.drawLine(s * 0.5, s * 0.72, s * 0.5, s * 0.92);
    });
}

inline QIcon network()
{
    // 网络: 地球 (圆 + 经纬线)
    return draw([](QPainter &p, int s) {
        p.setPen(QPen(QColor("#1e88e5"), 2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(s * 0.5, s * 0.5), s * 0.36, s * 0.36);
        p.drawEllipse(QPointF(s * 0.5, s * 0.5), s * 0.15, s * 0.36);
        p.drawLine(QPointF(s * 0.14, s * 0.5), QPointF(s * 0.86, s * 0.5));
        p.drawLine(QPointF(s * 0.5, s * 0.14), QPointF(s * 0.5, s * 0.86));
    });
}

inline QIcon info()
{
    return std(QStyle::SP_MessageBoxInformation);
}

inline QIcon exit()
{
    return std(QStyle::SP_DialogCloseButton);
}

inline QIcon scale()
{
    // 电子秤 (矩形 + 显示屏)
    return draw([](QPainter &p, int s) {
        // 秤台
        p.setPen(QPen(QColor("#455a64"), 2));
        p.setBrush(QColor("#cfd8dc"));
        p.drawRoundedRect(s * 0.1, s * 0.3, s * 0.8, s * 0.6, 3, 3);
        // 显示屏
        p.setBrush(QColor("#00e676"));
        p.setPen(QPen(QColor("#26a69a"), 1.5));
        p.drawRoundedRect(s * 0.2, s * 0.35, s * 0.6, s * 0.15, 2, 2);
        // 数字线
        p.setPen(QPen(QColor("#004d40"), 2));
        p.drawLine(s * 0.3, s * 0.43, s * 0.5, s * 0.43);
        p.drawLine(s * 0.55, s * 0.43, s * 0.7, s * 0.43);
        // 支脚
        p.setPen(QPen(QColor("#455a64"), 2.5));
        p.drawLine(s * 0.3, s * 0.9, s * 0.25, s * 0.98);
        p.drawLine(s * 0.7, s * 0.9, s * 0.75, s * 0.98);
    });
}

// ============================================================================
// 工具箱类
// ============================================================================
inline QIcon grr()
{
    // GRR 量具研究: 靶心 (重复性/再现性) + 对钩 (合格判定)
    return draw([](QPainter &p, int s) {
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#6d28d9"), 2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(s * 0.42, s * 0.55), s * 0.30, s * 0.30);
        p.drawEllipse(QPointF(s * 0.42, s * 0.55), s * 0.15, s * 0.15);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#6d28d9"));
        p.drawEllipse(QPointF(s * 0.42, s * 0.55), s * 0.045, s * 0.045);
        p.setPen(QPen(QColor("#16a34a"), 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(QPointF(s * 0.64, s * 0.26), QPointF(s * 0.74, s * 0.16));
        p.drawLine(QPointF(s * 0.74, s * 0.16), QPointF(s * 0.92, s * 0.44));
    });
}

inline QIcon calc()
{
    // 计算器 (机身 + 显示屏 + 按键阵列)
    return draw([](QPainter &p, int s) {
        p.setPen(QPen(QColor("#1565c0"), 2));
        p.setBrush(QColor("#e3f2fd"));
        p.drawRoundedRect(s * 0.15, s * 0.05, s * 0.7, s * 0.9, 3, 3);
        // 显示屏
        p.setBrush(QColor("#00695c"));
        p.setPen(QPen(QColor("#004d40"), 1.2));
        p.drawRoundedRect(s * 0.25, s * 0.12, s * 0.5, s * 0.18, 1, 1);
        // 按键
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#1976d2"));
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                p.drawRoundedRect(s * (0.24 + c * 0.18), s * (0.40 + r * 0.17), s * 0.13,
                                  s * 0.12, 1, 1);
    });
}

inline QIcon book()
{
    // 打开的手册 (左右页 + 中缝)
    return draw([](QPainter &p, int s) {
        p.setPen(QPen(QColor("#5e35b1"), 2));
        p.setBrush(QColor("#ede7f6"));
        p.drawRoundedRect(s * 0.1, s * 0.15, s * 0.8, s * 0.7, 2, 2);
        // 中缝
        p.setPen(QPen(QColor("#5e35b1"), 1.6));
        p.drawLine(s * 0.5, s * 0.15, s * 0.5, s * 0.85);
        // 文字线
        p.setPen(QPen(QColor("#7e57c2"), 1.5));
        for (int i = 0; i < 3; ++i)
        {
            p.drawLine(s * 0.18, s * (0.30 + i * 0.15), s * 0.42, s * (0.30 + i * 0.15));
            p.drawLine(s * 0.58, s * (0.30 + i * 0.15), s * 0.82, s * (0.30 + i * 0.15));
        }
    });
}

} // namespace Icons
