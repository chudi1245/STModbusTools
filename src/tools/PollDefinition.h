#pragma once

// ============================================================================
// PollDefinition.h — Modbus 读/写定义纯数据 (工作区最小单元, 无 GUI)
//   语义对齐 Modbus Poll 的 Read/Write Definition (F8):
//     读功能 01/02/03/04 = 轮询读取窗口;
//     写功能 05/06/15/16 = 写窗口 — 网格数据可编辑, 按扫描率周期性写入
//     设备 (或禁用时 F6 写一次), "写功能写的是网格上显示的数据"
//   交互写 (双击单元格写对话框/批量写) 仅在读窗口可用;
//   附带显示侧属性: 缩放/条件变色规则/值名映射/单元格内地址
//   多段读取: 读窗口可定义多个不连续地址段 (同从站+同功能码),
//     segs 空 = 单段 (回落 addr/qty, 旧工作区兼容); 写窗口恒单段
//   行级显示覆盖: rowStyles 中的字段优先于窗口级默认 (分层继承)
// ============================================================================

#include "ModbusFormat.h"

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVector>

struct PollDefinition
{
    // 条件变色规则 (对标 Modbus Poll 两条规则): op 见 on()
    struct ColorRule
    {
        int op = 0;      // 0=关 1=>lo 2=<lo 3=区间内 4=区间外 5==lo
        double lo = 0;
        double hi = 0;
        bool on(double v) const
        {
            switch (op)
            {
            case 1: return v > lo;
            case 2: return v < lo;
            case 3: return v >= lo && v <= hi;
            case 4: return v < lo || v > hi;
            case 5: return v == lo;
            default: return false;
            }
        }
    };

    // 连续地址段 (读窗口多段; 每段 ≤ maxQty(fc), 各段间可不连续)
    struct Segment
    {
        quint16 addr = 0; // 协议地址 (base 0)
        int qty = 0;
        bool operator==(const Segment &o) const { return addr == o.addr && qty == o.qty; }
    };

    // 行级显示样式覆盖: has* = 该字段是否覆盖窗口级默认 (分层继承, 非
    // 快照) — 窗口级改动仍对未覆盖字段生效; "清除" = 移除条目
    struct RowStyle
    {
        bool hasFmt = false;
        ModbusFormat::Fmt fmt = ModbusFormat::Fmt::U16;
        ModbusFormat::ByteOrder order = ModbusFormat::ByteOrder::ABCD;
        bool hasScale = false;
        bool scaleOn = false;
        double scaleM = 1.0;
        double scaleX1 = 0.0;
        double scaleY1 = 0.0;
        bool hasRules = false;
        ColorRule rules[2];
        bool hasVNames = false;
        QMap<quint16, QString> valueNames;
        bool hasAffix = false;  // 值前缀/后缀覆盖 (常用于单位; 空串 = 显式无)
        QString valPrefix;
        QString valSuffix;
        bool hasFont = false;   // 字体覆盖 (大小 + 粗/斜/下划线一组)
        int fontPt = 9;
        bool bold = false;
        bool italic = false;
        bool underline = false;
        bool hasHeight = false; // 行高覆盖
        int rowHeight = 22;
        bool hasBg = false;     // 手动背景覆盖 ("#rrggbb"); 绝对优先于
        QString bg;             //   该行条件变色与段底色
    };

    QString name;                  // 窗口名 (标题栏显示)
    QString connectionId;          // 所属连接 (ConnectionManager id; 空 = 首个/活动连接, 旧工作区兼容)
    int slave = 1;                 // 从站 1-247; 0 仅广播写
    int fc = 3;                    // 读 1=线圈 2=离散输入 3=保持 4=输入; 写 5/6/15/16
    quint16 addr = 0;              // 协议地址 (base 0); 多段时 = segs[0].addr
    int qty = 10;                  // 数量 (随功能码钳制, 见 maxQty); 多段时 = segs[0].qty
    int scanRateMs = 1000;         // 扫描速率 0-3600000ms; 0 = 单次 (F6)
    bool enabled = true;           // 读/写启用 (Disabled 反义)
    bool autoStopOnError = false;  // Disable on error
    bool hideNames = false;        // 隐藏别名列
    bool addrInCell = false;       // 单元格内显示地址
    bool plcBase1 = false;         // 行头地址 PLC 风格 (Base 1 + 区前缀) (Name 列前缀)
    ModbusFormat::Fmt fmt = ModbusFormat::Fmt::U16;
    ModbusFormat::ByteOrder order = ModbusFormat::ByteOrder::ABCD;
    QVector<QString> nameCells;    // 别名 (Name Cells), 下标 = 行
    QMap<quint16, QString> valueNames; // 值名映射 (仅 16 位整数格式参与显示)
    QVector<Segment> segs;         // 多段 (≥2 段); 空 = 单段回落 addr/qty
    QMap<int, RowStyle> rowStyles; // 行级显示覆盖 (键 = 行号)

    // 缩放 Scaling: Y = m(X − X1) + Y1 (仅整数格式; 对标 Modbus Poll Ctrl+Shift+S)
    bool scaleOn = false;
    double scaleM = 1.0;
    double scaleX1 = 0.0;
    double scaleY1 = 0.0;
    // 值前缀/后缀 (Affix): 包裹数值文本 (如 "12.5 V"); 值名映射不包裹;
    // 空串 = 无; 写窗口内联编辑输入时自动剥离后再解析
    QString valPrefix;
    QString valSuffix;

    ColorRule rules[2];            // 条件变色 ×2 (命中: 规则1 红 / 规则2 橙)

    // 外观 (窗口级默认): 字体/行高/背景可被 rowStyles 逐行覆盖;
    //   宽度 (列宽/行头宽) 按列共享, 仅窗口级, 无行级概念
    int fontPt = 9;             // 字体大小 (pt)
    bool fontBold = false;      // 粗体
    bool fontItalic = false;    // 斜体
    bool fontUnderline = false; // 下划线
    int rowHeight = 22;         // 默认行高 (px)
    QString bg;                 // 窗口底色 ("#rrggbb"; 空 = 主题默认)
    int nameColW = 200;         // 别名列宽 (px)
    int valueColW = 150;        // 值列宽 (px)
    int vHeaderW = 0;           // 地址行头宽 (px; 0 = 默认)

    bool isWriteFc() const { return fc == 5 || fc == 6 || fc == 15 || fc == 16; }
    bool isBitFc() const { return fc == 1 || fc == 2 || fc == 5 || fc == 15; }
    // 读窗口的只读数据区 (02 离散输入 / 04 输入寄存器); 写窗口全部可编辑
    bool readOnly() const { return !isWriteFc() && (fc == 2 || fc == 4); }
    bool singleWrite() const { return fc == 5 || fc == 6; }
    int maxQty() const
    {
        switch (fc)
        {
        case 1:
        case 2: return 2000;
        case 5:
        case 6: return 1;
        case 15: return 1968;
        case 16: return 123;
        default: return 125; // 03/04
        }
    }

    // 实际地址段: segs 非空且为读功能时用多段, 否则单段 (addr/qty)
    QVector<Segment> segments() const;
    // 各段数量和 (= 表格行数)
    int totalQty() const;
    // 行 → 协议地址 (越界 -1); 段内偏移单调
    int rowAddr(int row) const;
    // 协议地址 → 行 (不在任何段 -1); 用于写应答路由
    int rowForAddr(quint16 a) const;
    // 行 → 段下标 (仅多段读窗口; 单段/写功能/越界 -1); 供分段配色
    int segmentIndexAt(int row) const;

    // 数量与功能码的一致性钳制 (载入外部数据后必须调用);
    // 段数量 ≤ maxQty(fc), 总行数 ≤ 2000, 写功能强制单段
    void clamp()
    {
        switch (fc)
        {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 15:
        case 16: break;
        default: fc = 3; break;
        }
        slave = qBound(0, slave, 247);
        qty = qBound(1, qty, maxQty());
        scanRateMs = qBound(0, scanRateMs, 3600000);
        if (!isWriteFc())
        {
            for (Segment &s : segs)
            {
                s.qty = qBound(1, s.qty, maxQty());
            }
            // 总量上限: 防御外部数据撑爆网格 (qty 上限 × 段数不受控)
            int total = 0;
            QVector<Segment> kept;
            kept.reserve(segs.size());
            for (const Segment &s : segs)
            {
                if (total >= 2000)
                    break;
                Segment c = s;
                c.qty = qMin(c.qty, 2000 - total);
                kept.append(c);
                total += c.qty;
            }
            segs = kept;
        }
        else
        {
            segs.clear();
        }
        // 镜像 segs[0] → addr/qty (两套表示恒一致); 单段塌缩回落 addr/qty
        if (!segs.isEmpty())
        {
            addr = segs.first().addr;
            qty = segs.first().qty;
        }
        if (segs.size() <= 1)
            segs.clear();
        // 外观钳制 (越界回落; bg 合法性由渲染处按 QColor 有效性兜底)
        fontPt = qBound(6, fontPt, 48);
        rowHeight = qBound(14, rowHeight, 200);
        nameColW = qBound(40, nameColW, 4000);
        valueColW = qBound(40, valueColW, 4000);
        if (vHeaderW != 0)
            vHeaderW = qBound(64, vHeaderW, 400);
    }

    QJsonObject toJson() const;
    static PollDefinition fromJson(const QJsonObject &o); // 缺失/非法字段落默认
};
