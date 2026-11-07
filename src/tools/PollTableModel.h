#pragma once

// ============================================================================
// PollTableModel.h — 单个读/写定义的数据表模型 (对齐 Modbus Poll 数据窗口)
//   列: [别名 Name][值 Value]; 行头 = 地址 (Base0 偏移 或 PLC 4x0001 风格);
//   "单元格内显示地址" = Name 列前缀地址文本;
//   显示 = ModbusFormat 渲染 + 值名映射 + 缩放 (整数格式) + 前缀/后缀 (单位)
//     + 条件变色;
//   外观 = 字体/背景 (窗口默认 + 行级覆盖; 行级手动背景绝对优先于
//   条件变色与段底色), 行高/列宽由视图按 PollDefinition 下发;
//   行级覆盖: rowStyles 中 has 置位的字段优先, 未覆盖字段回落窗口级默认;
//   多段: 行数 = totalQty, 行头/值按 rowAddr 映射, 宽格式不跨段渲染,
//     ≥2 段时整行 + 行头按段底色区分 (值列条件规则命中时规则色优先);
//   别名行内编辑在轮询期间保持可用 — 寄存器刷新只发值列 dataChanged;
//   写窗口: 值单元格内联编辑 (只改网格, 由扫描/F6 发送);
//   读窗口: 不允许内联编辑 — 双击由视图转发写值对话框 (DataViewWidget)
//   乐观上屏: 读窗口写应答 OK 后 setRegs; 写窗口网格即数据源
// ============================================================================

#include "PollDefinition.h"

#include <QAbstractTableModel>
#include <QFont>
#include <QVector>

class PollTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Col
    {
        ColName = 0,
        ColValue,
        ColCount
    };

    explicit PollTableModel(QObject *parent = nullptr);

    void setDefinition(const PollDefinition &d); // 行数/格式/可编辑性重建
    const PollDefinition &definition() const { return m_def; }
    void applyRegs(const QVector<quint16> &regs); // 读窗口: 轮询结果 → 整批刷新
    void setRegs(int row, const QVector<quint16> &vals); // 按段上屏/乐观上屏
    const QVector<quint16> &regs() const { return m_regs; }

    void setPlcAddr(bool on);   // 行头地址 PLC 风格 (Base 1 + 0x/1x/3x/4x 前缀)
    void setAddrInCell(bool on); // Name 列前缀地址

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &idx, int role) const override;
    QVariant headerData(int section, Qt::Orientation orient, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &idx) const override;
    bool setData(const QModelIndex &idx, const QVariant &value, int role) override;

    // 地址行头文本 (行头与"单元格内地址"共用; Base0 = 偏移, PLC = 4x00001 风格)
    QString addrText(int row) const;
    // 宽格式占用判定: 本行被上一行 32/64 位格式覆盖 → 值列显示 "-" 且禁止写入
    bool isCovered(int row) const;

signals:
    // 别名行内重命名 (Name 单元格编辑提交)
    void nameEdited(int row, const QString &text);

private:
    // 行有效显示参数 (rowStyles 覆盖 → 窗口级回落)
    struct EffDisp
    {
        ModbusFormat::Fmt fmt;
        ModbusFormat::ByteOrder order;
        bool scaleOn = false;
        double sm = 1.0, sx1 = 0.0, sy1 = 0.0;
        const PollDefinition::ColorRule *rules = nullptr; // null → m_def.rules
        const QMap<quint16, QString> *vnames = nullptr;   // null → m_def.valueNames
        const QString *pfx = nullptr; // 值前缀 (null → m_def.valPrefix)
        const QString *sfx = nullptr; // 值后缀 (null → m_def.valSuffix)
        int avail = 0;    // 该行到所在段末的寄存器数 (宽格式跨段/跨行拦截)
        bool covered = false; // 被上一行宽格式占用 (显示 "-" 且禁止写入)
    };
    EffDisp effBase(int row) const; // 仅解析格式/缩放覆盖 (不含 avail/占用)
    EffDisp eff(int row) const;     // effBase + avail + 宽格式占用判定
    QString valueText(int row) const;   // 含值名映射与缩放
    int hitRule(double v, int row) const; // 命中的规则序号 (0/1), 未命中 = -1
    // 行有效字体 (rowStyles.hasFont 覆盖 → 窗口级默认; 字体族跟随应用默认)
    QFont effectiveFont(int row) const;
    // 行级手动背景 (无效色 = 无覆盖); 绝对优先于条件变色与段底色
    QColor rowBg(int row) const;
    // 多段读窗口 (段底色只在 ≥2 段时启用, 单段/写窗口不着色)
    bool hasMultiSeg() const { return !m_def.segs.isEmpty() && !m_def.isWriteFc(); }

    PollDefinition m_def;
    QVector<quint16> m_regs; // 与行数对齐; 读回前保留旧值
    bool m_plcAddr = false;
    bool m_addrInCell = false;
};
