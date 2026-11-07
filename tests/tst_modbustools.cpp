// ============================================================================
// tst_modbustools.cpp — Modbus 调试工具 (独立项目 modbustools): 显示格式纯逻辑 + 定义序列化 +
//   表格模型 + 主窗离屏冒烟 (不连串口, 只覆盖 UI 装配与定义增删/协议切换)
//   + 帧日志完整帧展示 (RTU ADU / TCP MBAP) + TCP 回环 TID 镜像
//   + 实机全功能自动化调试 (接入实机时执行, 否则跳过)
// ============================================================================

#include "AppSettings.h"
#include "ConnDialog.h"
#include "ConnectionManager.h"
#include "DataViewWidget.h"
#include "DefDialog.h"
#include "DeviceTreeWidget.h"
#include "FrameLogDialog.h"
#include "ModbusFormat.h"
#include "ModbusMasterTool.h"
#include "ModbusWorkspace.h"
#include "MasterClient.h"
#include "ScanDialog.h"
#include "SlavePoller.h"
#include "SlaveSetupDialog.h"

#include <QSerialPort>
#include <QSerialPortInfo>
#include "PollDefinition.h"
#include "PollTableModel.h"
#include "TestCenterCore.h"
#include "TestCenterPage.h"

#include <QtTest>

#include <QBrush>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>

#include <QComboBox>
#include <QFile>
#include <QFont>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QTableView>
#include <QTemporaryDir>

using namespace ModbusFormat;

class Tmm : public QObject
{
    Q_OBJECT

private slots:
    void formatDisplay()
    {
        const quint16 v = 1000;
        QCOMPARE(formatRegs(Fmt::U16, ByteOrder::ABCD, &v, 1), QStringLiteral("1000"));
        QCOMPARE(formatRegs(Fmt::Hex, ByteOrder::ABCD, &v, 1), QStringLiteral("03E8"));
        QCOMPARE(formatRegs(Fmt::Bin, ByteOrder::ABCD, &v, 1),
                 QStringLiteral("0000001111101000"));
        const quint16 neg = 0xFFFF;
        QCOMPARE(formatRegs(Fmt::I16, ByteOrder::ABCD, &neg, 1), QStringLiteral("-1"));
        const quint16 big = 40000; // 40000 按 16 位符号 = -25536
        QCOMPARE(formatRegs(Fmt::I16, ByteOrder::ABCD, &big, 1), QStringLiteral("-25536"));
        const quint16 ab = 0x4142;
        QCOMPARE(formatRegs(Fmt::AsciiHex, ByteOrder::ABCD, &ab, 1), QStringLiteral("AB"));
        const quint16 ctrl = 0x0101; // 不可打印字符 → '.'
        QCOMPARE(formatRegs(Fmt::AsciiHex, ByteOrder::ABCD, &ctrl, 1), QStringLiteral(".."));
        QVERIFY(formatRegs(Fmt::U16, ByteOrder::ABCD, nullptr, 0).isEmpty());
    }

    void parseInputs()
    {
        quint16 r = 0;
        QVERIFY(parseInput(Fmt::U16, ByteOrder::ABCD, QStringLiteral("1000"), &r, 1)
                && r == 1000);
        QVERIFY(parseInput(Fmt::U16, ByteOrder::ABCD, QStringLiteral("0x3E8"), &r, 1)
                && r == 1000);
        QVERIFY(parseInput(Fmt::U16, ByteOrder::ABCD, QStringLiteral("0b101"), &r, 1)
                && r == 5);
        QVERIFY(!parseInput(Fmt::U16, ByteOrder::ABCD, QStringLiteral("65536"), &r, 1));
        QVERIFY(!parseInput(Fmt::U16, ByteOrder::ABCD, QStringLiteral("12x3"), &r, 1));
        QVERIFY(parseInput(Fmt::I16, ByteOrder::ABCD, QStringLiteral("-1"), &r, 1)
                && r == 0xFFFF);
        QVERIFY(!parseInput(Fmt::I16, ByteOrder::ABCD, QStringLiteral("-40000"), &r, 1));
        QVERIFY(parseInput(Fmt::I16, ByteOrder::ABCD, QStringLiteral("65535"), &r, 1)
                && r == 0xFFFF);
        QVERIFY(parseInput(Fmt::Hex, ByteOrder::ABCD, QStringLiteral("3e8"), &r, 1)
                && r == 1000);
        QVERIFY(!parseInput(Fmt::Hex, ByteOrder::ABCD, QStringLiteral("12345"), &r, 1));
        QVERIFY(parseInput(Fmt::Bin, ByteOrder::ABCD, QStringLiteral("0b11"), &r, 1) && r == 3);
        QVERIFY(!parseInput(Fmt::Bin, ByteOrder::ABCD, QStringLiteral("102"), &r, 1));
        QVERIFY(parseInput(Fmt::AsciiHex, ByteOrder::ABCD, QStringLiteral("AB"), &r, 1)
                && r == 0x4142);
        QVERIFY(!parseInput(Fmt::AsciiHex, ByteOrder::ABCD, QStringLiteral("ABC"), &r, 1));
        QVERIFY(!parseInput(Fmt::U16, ByteOrder::ABCD, QString(), &r, 1));
    }

    void formatKeys()
    {
        QCOMPARE(fmtKey(Fmt::Hex), QStringLiteral("hex"));
        QCOMPARE(fmtFromKey(QStringLiteral("hex")), Fmt::Hex);
        QCOMPARE(fmtFromKey(QStringLiteral("bogus")), Fmt::U16); // 未知键落默认
        QCOMPARE(orderKey(ByteOrder::CDAB), QStringLiteral("CDAB"));
        QCOMPARE(orderFromKey(QStringLiteral("bogus")), ByteOrder::ABCD);
        QCOMPARE(fmtKey(Fmt::F32), QStringLiteral("f32"));
        QCOMPARE(fmtFromKey(QStringLiteral("f64")), Fmt::F64);
    }

    void regCounts()
    {
        QCOMPARE(regCount(Fmt::U16), 1);
        QCOMPARE(regCount(Fmt::AsciiHex), 1);
        QCOMPARE(regCount(Fmt::I32), 2);
        QCOMPARE(regCount(Fmt::F32), 2);
        QCOMPARE(regCount(Fmt::I64), 4);
        QCOMPARE(regCount(Fmt::F64), 4);
    }

    void wideFormatsByteOrders()
    {
        // 3.14f = 0x4048F5C3 → 字序 4 变体各占 2 寄存器
        const quint16 f32abcd[2] = {0x4048, 0xF5C3};
        const quint16 f32cdab[2] = {0xF5C3, 0x4048};
        const quint16 f32badc[2] = {0x4840, 0xC3F5};
        const quint16 f32dcba[2] = {0xC3F5, 0x4840};
        QCOMPARE(formatRegs(Fmt::F32, ByteOrder::ABCD, f32abcd, 2), QStringLiteral("3.14"));
        QCOMPARE(formatRegs(Fmt::F32, ByteOrder::CDAB, f32cdab, 2), QStringLiteral("3.14"));
        QCOMPARE(formatRegs(Fmt::F32, ByteOrder::BADC, f32badc, 2), QStringLiteral("3.14"));
        QCOMPARE(formatRegs(Fmt::F32, ByteOrder::DCBA, f32dcba, 2), QStringLiteral("3.14"));
        // -2 (32 位) = 0xFFFFFFFE
        const quint16 i32be[2] = {0xFFFF, 0xFFFE};
        QCOMPARE(formatRegs(Fmt::I32, ByteOrder::ABCD, i32be, 2), QStringLiteral("-2"));
        QCOMPARE(formatRegs(Fmt::U32, ByteOrder::ABCD, i32be, 2),
                 QStringLiteral("4294967294"));
        // 1.5 (64 位) = 0x3FF8000000000000
        const quint16 f64abcd[4] = {0x3FF8, 0x0000, 0x0000, 0x0000};
        const quint16 f64dcba[4] = {0x0000, 0x0000, 0x0000, 0xF83F};
        QCOMPARE(formatRegs(Fmt::F64, ByteOrder::ABCD, f64abcd, 4), QStringLiteral("1.5"));
        QCOMPARE(formatRegs(Fmt::F64, ByteOrder::DCBA, f64dcba, 4), QStringLiteral("1.5"));
        // 16 位格式不受字序影响
        const quint16 v = 1000;
        QCOMPARE(formatRegs(Fmt::U16, ByteOrder::DCBA, &v, 1), QStringLiteral("1000"));
    }

    void wideParseAndNumeric()
    {
        quint16 r[4] = {};
        QVERIFY(parseInput(Fmt::F32, ByteOrder::ABCD, QStringLiteral("3.14"), r, 2));
        QCOMPARE(r[0], quint16(0x4048));
        QCOMPARE(r[1], quint16(0xF5C3));
        QVERIFY(parseInput(Fmt::F32, ByteOrder::DCBA, QStringLiteral("3.14"), r, 2));
        QCOMPARE(r[0], quint16(0xC3F5));
        QCOMPARE(r[1], quint16(0x4840));
        QVERIFY(parseInput(Fmt::I32, ByteOrder::ABCD, QStringLiteral("-2"), r, 2));
        QCOMPARE(r[0], quint16(0xFFFF));
        QCOMPARE(r[1], quint16(0xFFFE));
        QVERIFY(parseInput(Fmt::U32, ByteOrder::ABCD, QStringLiteral("4294967295"), r, 2));
        QVERIFY(!parseInput(Fmt::U32, ByteOrder::ABCD, QStringLiteral("4294967296"), r, 2));
        QVERIFY(parseInput(Fmt::F64, ByteOrder::ABCD, QStringLiteral("1.5"), r, 4));
        QCOMPARE(r[0], quint16(0x3FF8));
        // 数值化 (变色/缩放基准)
        bool ok = false;
        const quint16 iv[2] = {0xFFFF, 0xFFFE};
        QCOMPARE(numericValue(Fmt::I32, ByteOrder::ABCD, iv, 2, &ok), -2.0);
        QVERIFY(ok);
        QCOMPARE(numericValue(Fmt::U16, ByteOrder::ABCD, iv + 1, 1, &ok), 65534.0);
        QVERIFY(ok);
        const quint16 fv[2] = {0x4048, 0xF5C3};
        QVERIFY(numericValue(Fmt::F32, ByteOrder::ABCD, fv, 2, &ok) > 3.13);
        QVERIFY(numericValue(Fmt::F32, ByteOrder::ABCD, fv, 2, &ok) < 3.15);
    }

    void definitionRoundtrip()
    {
        PollDefinition d;
        d.name = QStringLiteral("温度");
        d.slave = 7;
        d.fc = 4;
        d.addr = 0x01F4;
        d.qty = 20;
        d.scanRateMs = 250;
        d.enabled = false;
        d.autoStopOnError = true;
        d.hideNames = true;
        d.fmt = Fmt::Hex;
        d.order = ByteOrder::DCBA;
        d.nameCells = {QStringLiteral("a"), QStringLiteral("b")};
        const PollDefinition r = PollDefinition::fromJson(d.toJson());
        QCOMPARE(r.name, d.name);
        QCOMPARE(r.slave, d.slave);
        QCOMPARE(r.fc, d.fc);
        QCOMPARE(int(r.addr), int(d.addr));
        QCOMPARE(r.qty, d.qty);
        QCOMPARE(r.scanRateMs, d.scanRateMs);
        QCOMPARE(r.enabled, d.enabled);
        QCOMPARE(r.autoStopOnError, d.autoStopOnError);
        QCOMPARE(r.hideNames, d.hideNames);
        QVERIFY(r.fmt == Fmt::Hex);
        QVERIFY(r.order == ByteOrder::DCBA);
        QCOMPARE(r.nameCells.size(), 2);
    }

    void definitionDefaultsAndClamp()
    {
        // 全空对象: 全默认
        const PollDefinition r = PollDefinition::fromJson(QJsonObject());
        QCOMPARE(r.slave, 1);
        QCOMPARE(r.fc, 3);
        QCOMPARE(r.qty, 10);
        QCOMPARE(r.scanRateMs, 1000);
        QVERIFY(r.enabled);
        // 非法值钳制到合法域
        QJsonObject bad;
        bad.insert(QStringLiteral("fc"), 9);
        bad.insert(QStringLiteral("qty"), 99999);
        bad.insert(QStringLiteral("slave"), -5);
        bad.insert(QStringLiteral("rateMs"), -100);
        const PollDefinition b = PollDefinition::fromJson(bad);
        QCOMPARE(b.fc, 3);
        QCOMPARE(b.qty, 125); // 寄存器区上限
        QCOMPARE(b.slave, 0);
        QCOMPARE(b.scanRateMs, 0);
        // 写功能定义: maxQty 按功能码 (15 多写线圈 1968 / 05 单写强制 1)
        PollDefinition wd;
        wd.fc = 15;
        QCOMPARE(wd.maxQty(), 1968);
        wd.qty = 99999;
        wd.clamp();
        QCOMPARE(wd.qty, 1968);
        wd.fc = 5;
        wd.clamp();
        QCOMPARE(wd.qty, 1);
        QVERIFY(wd.isWriteFc());
        QCOMPARE(wd.maxQty(), 1);
    }

    void tableModel()
    {
        PollDefinition d;
        d.fc = 3;
        d.addr = 100;
        d.qty = 3;
        PollTableModel m;
        m.setDefinition(d);
        QCOMPARE(m.rowCount(), 3);
        QCOMPARE(m.columnCount(), 2); // Name / Value
        // 行头 = 地址 (Base0 偏移)
        QCOMPARE(m.headerData(0, Qt::Vertical, Qt::DisplayRole).toString(),
                 QStringLiteral("100"));
        QCOMPARE(m.headerData(2, Qt::Vertical, Qt::DisplayRole).toString(),
                 QStringLiteral("102"));
        // 读窗口: 值单元格不可内联编辑 (双击走写值对话框)
        QVERIFY(!(m.flags(m.index(0, PollTableModel::ColValue)) & Qt::ItemIsEditable));

        m.applyRegs(QVector<quint16>{1000, 65535, 5});
        QCOMPARE(m.data(m.index(0, PollTableModel::ColValue), Qt::DisplayRole).toString(),
                 QStringLiteral("1000"));
        QCOMPARE(m.data(m.index(1, PollTableModel::ColValue), Qt::DisplayRole).toString(),
                 QStringLiteral("65535"));
        QCOMPARE(m.data(m.index(0, PollTableModel::ColName), Qt::DisplayRole).toString(),
                 QString());

        // 别名 + 单元格内地址
        d.nameCells = {QStringLiteral("温度")};
        d.addrInCell = true;
        m.setDefinition(d);
        QCOMPARE(m.data(m.index(0, PollTableModel::ColName), Qt::DisplayRole).toString(),
                 QStringLiteral("100  温度"));
        d.addrInCell = false;
        m.setDefinition(d);
        QCOMPARE(m.data(m.index(0, PollTableModel::ColName), Qt::DisplayRole).toString(),
                 QStringLiteral("温度"));

        // 写窗口 (FC16): 值单元格内联编辑 → 网格更新 (发送由扫描/F6)
        d.fc = 16;
        d.qty = 2;
        m.setDefinition(d);
        QVERIFY(m.flags(m.index(0, PollTableModel::ColValue)) & Qt::ItemIsEditable);
        QVERIFY(m.setData(m.index(0, PollTableModel::ColValue), QStringLiteral("999"),
                          Qt::EditRole));
        QCOMPARE(m.regs().front(), quint16(999));
        // 非法输入拒绝
        QVERIFY(!m.setData(m.index(0, PollTableModel::ColValue), QStringLiteral("abc"),
                           Qt::EditRole));

        // PLC 行头 (FC03 → 4x)
        d.fc = 3;
        d.addr = 100;
        d.qty = 4;
        m.setDefinition(d);
        m.setPlcAddr(true);
        QCOMPARE(m.headerData(0, Qt::Vertical, Qt::DisplayRole).toString(),
                 QStringLiteral("4x00101"));
        QCOMPARE(m.headerData(3, Qt::Vertical, Qt::DisplayRole).toString(),
                 QStringLiteral("4x00104"));
        m.setPlcAddr(false);
        QCOMPARE(m.headerData(0, Qt::Vertical, Qt::DisplayRole).toString(),
                 QStringLiteral("100"));
    }

    void modelDisplayFeatures()
    {
        PollDefinition d;
        d.fc = 3;
        d.addr = 100;
        d.qty = 4;
        PollTableModel m;
        m.setDefinition(d);
        m.applyRegs(QVector<quint16>{0, 5, 1000, 65535});
        QCOMPARE(m.columnCount(), 2);

        // 值名映射 (16 位整数格式)
        d.valueNames.insert(0, QStringLiteral("停止"));
        d.valueNames.insert(5, QStringLiteral("运行"));
        m.setDefinition(d);
        QCOMPARE(m.data(m.index(0, PollTableModel::ColValue), Qt::DisplayRole).toString(),
                 QStringLiteral("停止"));
        QCOMPARE(m.data(m.index(1, PollTableModel::ColValue), Qt::DisplayRole).toString(),
                 QStringLiteral("运行"));
        QCOMPARE(m.data(m.index(2, PollTableModel::ColValue), Qt::DisplayRole).toString(),
                 QStringLiteral("1000"));

        // 缩放 Y = m(X−X1)+Y1: ×0.1 → 1000 显示 100
        d.scaleOn = true;
        d.scaleM = 0.1;
        m.setDefinition(d);
        QCOMPARE(m.data(m.index(2, PollTableModel::ColValue), Qt::DisplayRole).toString(),
                 QStringLiteral("100"));

        // 条件变色: 规则1 >500 → 红 (行2), 行1 不命中
        d.scaleOn = false;
        d.rules[0].op = 1;
        d.rules[0].lo = 500;
        m.setDefinition(d);
        QVERIFY(m.data(m.index(2, PollTableModel::ColValue), Qt::ForegroundRole).isValid());
        QVERIFY(!m.data(m.index(1, PollTableModel::ColValue), Qt::ForegroundRole).isValid());
        // 浮点格式不参与变色
        d.fmt = ModbusFormat::Fmt::F32;
        m.setDefinition(d);
        QVERIFY(!m.data(m.index(0, PollTableModel::ColValue), Qt::ForegroundRole).isValid());

        // 宽格式跨行: F32 占 2 寄存器, 被占用/不足的寄存器行 → 占位 "-"
        d.fmt = ModbusFormat::Fmt::F32;
        d.qty = 3; // 行0 跨寄存器 0,1 → 完整; 行1 被占用, 行2 只有 1 个寄存器
        m.setDefinition(d);
        m.applyRegs(QVector<quint16>{0x4048, 0xF5C3, 0x0000});
        QCOMPARE(m.data(m.index(0, PollTableModel::ColValue), Qt::DisplayRole).toString(),
                 QStringLiteral("3.14"));
        QVERIFY(m.isCovered(1)); // 行1 被行0 的 F32 占用
        QCOMPARE(m.data(m.index(1, PollTableModel::ColValue), Qt::DisplayRole).toString(),
                 QStringLiteral("-"));
        QCOMPARE(m.data(m.index(2, PollTableModel::ColValue), Qt::DisplayRole).toString(),
                 QStringLiteral("-"));
        // 宽格式写窗口编辑: 网格值按格式解析落盘 (发送由扫描/F6)
        m.setDefinition(PollDefinition{}); // 清规则
        PollDefinition w;
        w.fc = 16; // 写窗口才可内联编辑
        w.fmt = ModbusFormat::Fmt::F32;
        w.qty = 2;
        m.setDefinition(w);
        QVERIFY(m.setData(m.index(0, PollTableModel::ColValue), QStringLiteral("3.14"),
                          Qt::EditRole));
        QCOMPARE(m.regs(), (QVector<quint16>{0x4048, 0xF5C3}));
    }

    void definitionAdvancedRoundtrip()
    {
        PollDefinition d;
        d.scaleOn = true;
        d.scaleM = 0.1;
        d.scaleX1 = 2;
        d.scaleY1 = -5;
        d.rules[0].op = 4;
        d.rules[0].lo = 1;
        d.rules[0].hi = 9;
        d.rules[1].op = 2;
        d.rules[1].lo = -3.5;
        d.valueNames.insert(0, QStringLiteral("停止"));
        d.valueNames.insert(65535, QStringLiteral("满量程"));
        const PollDefinition r = PollDefinition::fromJson(d.toJson());
        QVERIFY(r.scaleOn);
        QCOMPARE(r.scaleM, 0.1);
        QCOMPARE(r.scaleX1, 2.0);
        QCOMPARE(r.scaleY1, -5.0);
        QCOMPARE(r.rules[0].op, 4);
        QCOMPARE(r.rules[0].lo, 1.0);
        QCOMPARE(r.rules[0].hi, 9.0);
        QCOMPARE(r.rules[1].op, 2);
        QCOMPARE(r.rules[1].lo, -3.5);
        QCOMPARE(r.valueNames.size(), 2);
        QCOMPARE(r.valueNames.value(0), QStringLiteral("停止"));
        QCOMPARE(r.valueNames.value(65535), QStringLiteral("满量程"));
    }

    void multiSegHelpers()
    {
        // 单段: segs 空 → 回落 addr/qty
        PollDefinition d;
        d.fc = 3;
        d.addr = 100;
        d.qty = 3;
        QCOMPARE(d.segments().size(), 1);
        QCOMPARE(d.totalQty(), 3);
        QCOMPARE(d.rowAddr(0), 100);
        QCOMPARE(d.rowAddr(2), 102);
        QCOMPARE(d.rowAddr(3), -1);
        QCOMPARE(d.rowForAddr(101), 1);
        QCOMPARE(d.rowForAddr(103), -1);

        // 多段: 100×3 + 200×2 + 300×4 → 行 0..8
        d.segs = {{100, 3}, {200, 2}, {300, 4}};
        d.clamp();
        QCOMPARE(d.segs.size(), 3);
        QCOMPARE(d.segments().size(), 3);
        QCOMPARE(d.totalQty(), 9);
        QCOMPARE(d.rowAddr(0), 100);
        QCOMPARE(d.rowAddr(2), 102);
        QCOMPARE(d.rowAddr(3), 200);
        QCOMPARE(d.rowAddr(4), 201);
        QCOMPARE(d.rowAddr(5), 300);
        QCOMPARE(d.rowAddr(8), 303);
        QCOMPARE(d.rowAddr(9), -1);
        QCOMPARE(d.rowForAddr(100), 0);
        QCOMPARE(d.rowForAddr(102), 2);
        QCOMPARE(d.rowForAddr(103), -1); // 段间空洞
        QCOMPARE(d.rowForAddr(200), 3);
        QCOMPARE(d.rowForAddr(303), 8);
        QCOMPARE(d.rowForAddr(304), -1);
        // 镜像: segs[0] → addr/qty (旧字段保持一致)
        QCOMPARE(int(d.addr), 100);
        QCOMPARE(d.qty, 3);

        // 钳制: 段数量超单帧上限
        d.segs = {{0, 9999}, {500, 1}};
        d.clamp();
        QCOMPARE(d.segs.size(), 2);
        QCOMPARE(d.segs[0].qty, 125);
        QCOMPARE(d.segs[1].qty, 1);

        // 钳制: 总行数上限 2000 (超限段被裁剪; 剩单段塌缩回 addr/qty 表示)
        PollDefinition c;
        c.fc = 1;
        c.segs = {{0, 2000}, {3000, 2000}, {6000, 2000}};
        c.clamp();
        QCOMPARE(c.totalQty(), 2000);
        QVERIFY(c.segs.isEmpty());
        QCOMPARE(int(c.addr), 0);
        QCOMPARE(c.qty, 2000);

        // 写功能强制单段
        PollDefinition w;
        w.fc = 16;
        w.addr = 10;
        w.qty = 5;
        w.segs = {{10, 2}, {20, 3}};
        w.clamp();
        QVERIFY(w.segs.isEmpty());
        QCOMPARE(w.totalQty(), 5);
        QCOMPARE(w.rowForAddr(12), 2);

        // 单段 segs 自动回落 addr/qty 表示
        PollDefinition s;
        s.fc = 3;
        s.addr = 7;
        s.qty = 2;
        s.segs = {{7, 2}};
        s.clamp();
        QVERIFY(s.segs.isEmpty());
        QCOMPARE(s.segments().size(), 1);
    }

    void segRowStyleRoundtrip()
    {
        PollDefinition d;
        d.segs = {{100, 3}, {200, 2}};
        PollDefinition::RowStyle st;
        st.hasFmt = true;
        st.fmt = Fmt::F32;
        st.order = ByteOrder::CDAB;
        st.hasScale = true;
        st.scaleOn = true;
        st.scaleM = 0.1;
        st.scaleX1 = 1;
        st.scaleY1 = 2;
        st.hasRules = true;
        st.rules[0].op = 3;
        st.rules[0].lo = 1;
        st.rules[0].hi = 5;
        st.rules[1].op = 1;
        st.rules[1].lo = 100;
        st.hasVNames = true;
        st.valueNames.insert(0, QStringLiteral("停止"));
        d.rowStyles.insert(1, st);
        d.rowStyles[4].hasFmt = true; // 部分覆盖: 只格式
        d.rowStyles[4].fmt = Fmt::Hex;
        d.rowStyles[4].order = ByteOrder::DCBA;

        const PollDefinition r = PollDefinition::fromJson(d.toJson());
        QCOMPARE(r.segs.size(), 2);
        QCOMPARE(int(r.segs[0].addr), 100);
        QCOMPARE(r.segs[0].qty, 3);
        QCOMPARE(int(r.segs[1].addr), 200);
        QCOMPARE(r.segs[1].qty, 2);
        QCOMPARE(r.rowStyles.size(), 2);
        const PollDefinition::RowStyle rt = r.rowStyles.value(1);
        QVERIFY(rt.hasFmt);
        QVERIFY(rt.fmt == Fmt::F32);
        QVERIFY(rt.order == ByteOrder::CDAB);
        QVERIFY(rt.hasScale);
        QVERIFY(rt.scaleOn);
        QCOMPARE(rt.scaleM, 0.1);
        QCOMPARE(rt.scaleX1, 1.0);
        QCOMPARE(rt.scaleY1, 2.0);
        QVERIFY(rt.hasRules);
        QCOMPARE(rt.rules[0].op, 3);
        QCOMPARE(rt.rules[0].lo, 1.0);
        QCOMPARE(rt.rules[1].op, 1);
        QVERIFY(rt.hasVNames);
        QCOMPARE(rt.valueNames.value(0), QStringLiteral("停止"));
        const PollDefinition::RowStyle rh = r.rowStyles.value(4);
        QVERIFY(rh.hasFmt);
        QVERIFY(rh.fmt == Fmt::Hex);
        QVERIFY(rh.order == ByteOrder::DCBA);
        QVERIFY(!rh.hasScale);
        QVERIFY(!rh.hasRules);
        QVERIFY(!rh.hasVNames);

        // v1 兼容: 旧文件无 segs/rowStyles 键 → 单段 + 无覆盖
        PollDefinition old;
        old.addr = 5;
        old.qty = 2;
        const PollDefinition ro = PollDefinition::fromJson(old.toJson());
        QVERIFY(ro.segs.isEmpty());
        QVERIFY(ro.rowStyles.isEmpty());
        QCOMPARE(ro.segments().size(), 1);
        QCOMPARE(ro.totalQty(), 2);
    }

    void lookRoundtrip()
    {
        // 窗口级外观 + 行级 字体/行高/背景 (含部分覆盖) 序列化往返
        PollDefinition d;
        d.fc = 3;
        d.addr = 0;
        d.qty = 3;
        d.fontPt = 14;
        d.fontBold = true;
        d.fontItalic = true;
        d.fontUnderline = false;
        d.rowHeight = 32;
        d.bg = QStringLiteral("#102a43");
        d.nameColW = 260;
        d.valueColW = 180;
        d.vHeaderW = 90;
        PollDefinition::RowStyle st;
        st.hasFont = true;
        st.fontPt = 20;
        st.bold = true;
        st.italic = false;
        st.underline = true;
        st.hasHeight = true;
        st.rowHeight = 40;
        st.hasBg = true;
        st.bg = QStringLiteral("#fde68a");
        d.rowStyles.insert(1, st);
        d.rowStyles[2].hasHeight = true; // 部分覆盖: 只行高
        d.rowStyles[2].rowHeight = 50;
        const PollDefinition r =
            PollDefinition::fromJson(QJsonDocument(d.toJson()).object());
        QCOMPARE(r.fontPt, 14);
        QVERIFY(r.fontBold && r.fontItalic && !r.fontUnderline);
        QCOMPARE(r.rowHeight, 32);
        QCOMPARE(r.bg, QStringLiteral("#102a43"));
        QCOMPARE(r.nameColW, 260);
        QCOMPARE(r.valueColW, 180);
        QCOMPARE(r.vHeaderW, 90);
        QCOMPARE(r.rowStyles.size(), 2);
        const PollDefinition::RowStyle rt = r.rowStyles.value(1);
        QVERIFY(rt.hasFont && rt.hasHeight && rt.hasBg);
        QCOMPARE(rt.fontPt, 20);
        QVERIFY(rt.bold && rt.underline && !rt.italic);
        QCOMPARE(rt.rowHeight, 40);
        QCOMPARE(rt.bg, QStringLiteral("#fde68a"));
        QVERIFY(r.rowStyles.value(2).hasHeight);
        QCOMPARE(r.rowStyles.value(2).rowHeight, 50);
        QVERIFY(!r.rowStyles.value(2).hasFont);

        // 旧文件 (无 look / 无新行级键) → 默认外观, 不产生覆盖位
        QJsonObject legacy;
        legacy.insert(QStringLiteral("fc"), 3);
        legacy.insert(QStringLiteral("qty"), 2);
        const PollDefinition o = PollDefinition::fromJson(legacy);
        QCOMPARE(o.fontPt, 9);
        QCOMPARE(o.rowHeight, 22);
        QVERIFY(o.bg.isEmpty());
        QCOMPARE(o.nameColW, 200);
        QCOMPARE(o.valueColW, 150);
        QCOMPARE(o.vHeaderW, 0);
        QVERIFY(o.rowStyles.isEmpty());

        // 非法背景色 → 丢弃; 越界外观值 → 钳制
        QJsonObject bad = QJsonDocument(d.toJson()).object();
        QJsonObject look = bad.value(QStringLiteral("look")).toObject();
        look.insert(QStringLiteral("bg"), QStringLiteral("not-a-color"));
        look.insert(QStringLiteral("fontPt"), 999);
        look.insert(QStringLiteral("rowH"), 1);
        bad.insert(QStringLiteral("look"), look);
        const PollDefinition b = PollDefinition::fromJson(bad);
        QVERIFY(b.bg.isEmpty());
        QCOMPARE(b.fontPt, 48);
        QCOMPARE(b.rowHeight, 14);
    }

    void modelLookRoles()
    {
        // 字体: 窗口默认 + 行级覆盖 (FontRole; 覆盖整组生效不混合窗口级)
        PollDefinition d;
        d.fc = 3;
        d.addr = 0;
        d.qty = 3;
        d.fmt = Fmt::U16;
        d.fontPt = 12;
        d.fontBold = true;
        PollTableModel m;
        m.setDefinition(d);
        m.applyRegs(QVector<quint16>{1, 2, 3});
        QCOMPARE(
            m.data(m.index(0, PollTableModel::ColValue), Qt::FontRole).value<QFont>().pointSize(),
            12);
        QVERIFY(
            m.data(m.index(0, PollTableModel::ColValue), Qt::FontRole).value<QFont>().bold());
        PollDefinition::RowStyle st;
        st.hasFont = true;
        st.fontPt = 20;
        st.italic = true;
        d.rowStyles.insert(1, st);
        m.setDefinition(d);
        const QFont rf =
            m.data(m.index(1, PollTableModel::ColName), Qt::FontRole).value<QFont>();
        QCOMPARE(rf.pointSize(), 20);
        QVERIFY(rf.italic());
        QVERIFY(!rf.bold());

        // 背景: 行级手动背景绝对优先 (压制段底色与报警色); 未设行报警照常
        PollDefinition s;
        s.fc = 3;
        s.addr = 0;
        s.qty = 4;
        s.fmt = Fmt::U16;
        s.rules[0].op = 1;
        s.rules[0].lo = 2; // > 2 命中红
        PollDefinition::Segment sg1;
        sg1.addr = 0;
        sg1.qty = 2;
        PollDefinition::Segment sg2;
        sg2.addr = 100;
        sg2.qty = 2;
        s.segs = {sg1, sg2}; // 2 段 → 段底色启用
        PollDefinition::RowStyle bgRow;
        bgRow.hasBg = true;
        bgRow.bg = QStringLiteral("#abcdef");
        s.rowStyles.insert(3, bgRow); // 行 3 值 4 本会命中报警, 但手动背景压制
        PollTableModel m2;
        m2.setDefinition(s);
        m2.applyRegs(QVector<quint16>{1, 4, 3, 4});
        QVERIFY(
            !m2.data(m2.index(3, PollTableModel::ColValue), Qt::ForegroundRole).isValid());
        QCOMPARE(m2.data(m2.index(3, PollTableModel::ColValue), Qt::BackgroundRole)
                     .value<QBrush>()
                     .color()
                     .name(),
            QStringLiteral("#abcdef"));
        // 行 1 (值 4, 命中报警) 无手动背景 → 报警前景
        QVERIFY(
            m2.data(m2.index(1, PollTableModel::ColValue), Qt::ForegroundRole).isValid());
        // 行 0 (值 1, 不命中): 段 0 底色兜底
        QVERIFY(
            m2.data(m2.index(0, PollTableModel::ColName), Qt::BackgroundRole).isValid());
        // 行头同链: 行 3 = 手动色
        QCOMPARE(m2.headerData(3, Qt::Vertical, Qt::BackgroundRole)
                     .value<QBrush>()
                     .color()
                     .name(),
            QStringLiteral("#abcdef"));

        // 窗口级底色: 单段窗口 (无段色/无手动/未命中) → bg 兜底
        PollDefinition w;
        w.fc = 3;
        w.addr = 0;
        w.qty = 2;
        w.fmt = Fmt::U16;
        w.bg = QStringLiteral("#123456");
        PollTableModel m3;
        m3.setDefinition(w);
        m3.applyRegs(QVector<quint16>{0, 0});
        QCOMPARE(m3.data(m3.index(0, PollTableModel::ColName), Qt::BackgroundRole)
                     .value<QBrush>()
                     .color()
                     .name(),
            QStringLiteral("#123456"));
    }

    void dataViewLookGeometry()
    {
        // applyDefinition 下发 默认行高/行级行高/列宽; 拖拽回写 definition
        MasterClient mb;
        mb.setKind(MasterClient::Kind::Tcp);
        DataViewWidget w(&mb);
        QTableView *t = w.findChild<QTableView *>(QStringLiteral("mdef.table"));
        QVERIFY(t);

        PollDefinition d;
        d.fc = 3;
        d.addr = 0;
        d.qty = 3;
        d.fmt = Fmt::U16;
        d.rowHeight = 30;
        d.nameColW = 250;
        d.valueColW = 170;
        PollDefinition::RowStyle st;
        st.hasHeight = true;
        st.rowHeight = 48;
        d.rowStyles.insert(2, st);
        w.setDefinition(d);
        QCOMPARE(t->verticalHeader()->defaultSectionSize(), 30);
        QCOMPARE(t->rowHeight(2), 48);
        QCOMPARE(t->rowHeight(0), 30); // 未覆盖行 = 默认行高
        QCOMPARE(t->columnWidth(PollTableModel::ColName), 250);
        QCOMPARE(t->columnWidth(PollTableModel::ColValue), 170);

        // 模拟拖拽 (m_applyingDef=false 时 sectionResized 直连回写):
        // 列宽 → 窗口级字段; 行高 → 该行覆盖
        t->setColumnWidth(PollTableModel::ColName, 300);
        QCOMPARE(w.definition().nameColW, 300);
        t->setRowHeight(1, 55);
        QVERIFY(w.definition().rowStyles.value(1).hasHeight);
        QCOMPARE(w.definition().rowStyles.value(1).rowHeight, 55);

        // 双击边界自适应 (等价路径: resizeRowToContents → sectionResized 回写)
        t->setRowHeight(0, 60); // 先拉大
        QCOMPARE(w.definition().rowStyles.value(0).rowHeight, 60);
        t->resizeRowToContents(0); // 自适应回落 (< 60) → 回写新值
        QVERIFY(w.definition().rowStyles.value(0).rowHeight < 60);

        // 清掉行高覆盖 → 回落默认行高
        PollDefinition d2 = w.definition();
        d2.rowStyles.remove(1);
        w.setDefinition(d2);
        QCOMPARE(t->rowHeight(1), 30);
    }

    void valueAffix()
    {
        // 显示: 窗口级/行级 前缀/后缀 包裹数值; 值名映射不包裹; 与缩放叠加
        PollDefinition d;
        d.fc = 3;
        d.addr = 0;
        d.qty = 3;
        d.fmt = Fmt::U16;
        d.valPrefix = QStringLiteral("$");
        d.valSuffix = QStringLiteral(" V");
        PollDefinition::RowStyle st;
        st.hasAffix = true;
        st.valPrefix = QString();
        st.valSuffix = QStringLiteral(" kW"); // 行 1 覆盖 (含前导空格)
        d.rowStyles.insert(1, st);
        PollTableModel m;
        m.setDefinition(d);
        m.applyRegs(QVector<quint16>{255, 255, 10});
        QCOMPARE(m.data(m.index(0, PollTableModel::ColValue), Qt::DisplayRole).toString(),
                 QStringLiteral("$255 V"));
        QCOMPARE(m.data(m.index(1, PollTableModel::ColValue), Qt::DisplayRole).toString(),
                 QStringLiteral("255 kW"));
        // 缩放 ×0.1 + 前后缀: 行 0 寄存器 255 → 25.5 → "$25.5 V"
        d.scaleOn = true;
        d.scaleM = 0.1;
        m.setDefinition(d);
        QCOMPARE(m.data(m.index(0, PollTableModel::ColValue), Qt::DisplayRole).toString(),
                 QStringLiteral("$25.5 V"));
        // 值名映射命中 = 语义文本, 不包裹
        d.valueNames.insert(255, QStringLiteral("满"));
        d.scaleOn = false;
        m.setDefinition(d);
        QCOMPARE(m.data(m.index(0, PollTableModel::ColValue), Qt::DisplayRole).toString(),
                 QStringLiteral("满"));

        // 写窗口: 编辑输入剥离有效前缀/后缀后解析; 纯数值照旧
        PollDefinition w;
        w.fc = 16;
        w.addr = 0;
        w.qty = 2;
        w.fmt = Fmt::U16;
        w.valSuffix = QStringLiteral(" V");
        PollTableModel mw;
        mw.setDefinition(w);
        QVERIFY(mw.setData(mw.index(0, PollTableModel::ColValue),
                           QStringLiteral("999 V"), Qt::EditRole));
        QCOMPARE(mw.regs().value(0), quint16(999));
        QVERIFY(mw.setData(mw.index(1, PollTableModel::ColValue),
                           QStringLiteral("777"), Qt::EditRole));
        QCOMPARE(mw.regs().value(1), quint16(777));

        // 序列化: 窗口级 + 行级 (含"显式空覆盖") 往返; 旧文件默认无前后缀
        PollDefinition rt;
        rt.fc = 3;
        rt.qty = 2;
        rt.valPrefix = QStringLiteral("$");
        rt.valSuffix = QStringLiteral(" V");
        PollDefinition::RowStyle ra;
        ra.hasAffix = true;
        ra.valSuffix = QStringLiteral(" kW");
        rt.rowStyles.insert(0, ra);
        PollDefinition::RowStyle re; // 显式空覆盖 = 该行去掉单位
        re.hasAffix = true;
        rt.rowStyles.insert(1, re);
        const PollDefinition rr =
            PollDefinition::fromJson(QJsonDocument(rt.toJson()).object());
        QCOMPARE(rr.valPrefix, QStringLiteral("$"));
        QCOMPARE(rr.valSuffix, QStringLiteral(" V"));
        QCOMPARE(rr.rowStyles.size(), 2);
        QVERIFY(rr.rowStyles.value(0).hasAffix);
        QCOMPARE(rr.rowStyles.value(0).valSuffix, QStringLiteral(" kW"));
        QVERIFY(rr.rowStyles.value(0).valPrefix.isEmpty());
        QVERIFY(rr.rowStyles.value(1).hasAffix); // 空覆盖仍在
        QVERIFY(rr.rowStyles.value(1).valSuffix.isEmpty());

        QJsonObject legacy;
        legacy.insert(QStringLiteral("fc"), 3);
        legacy.insert(QStringLiteral("qty"), 2);
        const PollDefinition lo = PollDefinition::fromJson(legacy);
        QVERIFY(lo.valPrefix.isEmpty() && lo.valSuffix.isEmpty());
        QVERIFY(lo.rowStyles.isEmpty());
    }

    void multiSegTableModel()
    {
        PollDefinition d;
        d.fc = 3;
        d.segs = {{100, 3}, {200, 2}};
        PollTableModel m;
        m.setDefinition(d);
        QCOMPARE(m.rowCount(), 5);
        // 行头 = 各段实际地址
        QCOMPARE(m.addrText(0), QStringLiteral("100"));
        QCOMPARE(m.addrText(2), QStringLiteral("102"));
        QCOMPARE(m.addrText(3), QStringLiteral("200"));
        QCOMPARE(m.addrText(4), QStringLiteral("201"));
        QCOMPARE(m.headerData(4, Qt::Vertical, Qt::DisplayRole).toString(),
                 QStringLiteral("201"));
        m.setPlcAddr(true);
        QCOMPARE(m.headerData(3, Qt::Vertical, Qt::DisplayRole).toString(),
                 QStringLiteral("4x00201"));
        m.setPlcAddr(false);

        // 轮询刷新只发值列 dataChanged — 别名列不被波及 (行内编辑不被打断)
        int minCol = 99, maxCol = -1;
        connect(&m, &QAbstractItemModel::dataChanged, this,
                [&](const QModelIndex &tl, const QModelIndex &br) {
                    minCol = qMin(minCol, tl.column());
                    maxCol = qMax(maxCol, br.column());
                });

        // 数据按段上屏: 先整批, 再第 2 段 (行 3..4) setRegs
        m.applyRegs(QVector<quint16>{1, 2, 3, 0, 0});
        m.setRegs(3, QVector<quint16>{7, 8});
        QCOMPARE(m.regs(), (QVector<quint16>{1, 2, 3, 7, 8}));
        QCOMPARE(minCol, PollTableModel::ColValue);
        QCOMPARE(maxCol, PollTableModel::ColValue);

        // ---- 分段配色 (≥2 段整行 + 行头按段底色) ----
        QCOMPARE(d.segmentIndexAt(0), 0);
        QCOMPARE(d.segmentIndexAt(2), 0);
        QCOMPARE(d.segmentIndexAt(3), 1);
        QCOMPARE(d.segmentIndexAt(4), 1);
        QCOMPARE(d.segmentIndexAt(5), -1);
        d.fmt = Fmt::U16;
        m.setDefinition(d);
        m.applyRegs(QVector<quint16>{1, 2, 3, 4, 5});
        const QVariant name0 = m.data(m.index(0, PollTableModel::ColName), Qt::BackgroundRole);
        const QVariant val0 = m.data(m.index(0, PollTableModel::ColValue), Qt::BackgroundRole);
        const QVariant name3 = m.data(m.index(3, PollTableModel::ColName), Qt::BackgroundRole);
        QVERIFY(name0.isValid());
        QVERIFY(val0.isValid());
        QVERIFY(name3.isValid());
        QCOMPARE(name0.value<QBrush>(), val0.value<QBrush>()); // 同段行/值列同色
        QVERIFY(name0.value<QBrush>() != name3.value<QBrush>()); // 段间不同色
        QCOMPARE(m.headerData(0, Qt::Vertical, Qt::BackgroundRole).value<QBrush>(),
                 name0.value<QBrush>()); // 行头同段同色
        QVERIFY(m.headerData(3, Qt::Vertical, Qt::BackgroundRole).value<QBrush>()
                != name0.value<QBrush>());

        // 宽格式不跨段: F32 行 2 = 第 1 段末 (只剩 1 寄存器) → 占位 "-"
        d.fmt = Fmt::F32;
        m.setDefinition(d);
        m.applyRegs(QVector<quint16>{0x4048, 0xF5C3, 0, 0x4048, 0xF5C3});
        QCOMPARE(m.data(m.index(0, PollTableModel::ColValue), Qt::DisplayRole).toString(),
                 QStringLiteral("3.14"));
        QCOMPARE(m.data(m.index(2, PollTableModel::ColValue), Qt::DisplayRole).toString(),
                 QStringLiteral("-"));
        QCOMPARE(m.data(m.index(3, PollTableModel::ColValue), Qt::DisplayRole).toString(),
                 QStringLiteral("3.14"));

        // ---- 行级显示覆盖 ----
        PollDefinition d2;
        d2.fc = 3;
        d2.addr = 0;
        d2.qty = 2;
        d2.fmt = Fmt::U16;
        PollDefinition::RowStyle hexRow;
        hexRow.hasFmt = true;
        hexRow.fmt = Fmt::Hex;
        hexRow.order = ByteOrder::ABCD;
        d2.rowStyles.insert(0, hexRow);
        PollTableModel m2;
        m2.setDefinition(d2);
        m2.applyRegs(QVector<quint16>{0x00FF, 0x00FF});
        // 行 0 = Hex (覆盖), 行 1 = 窗口级 U16
        QCOMPARE(m2.data(m2.index(0, PollTableModel::ColValue), Qt::DisplayRole).toString(),
                 QStringLiteral("00FF"));
        QCOMPARE(m2.data(m2.index(1, PollTableModel::ColValue), Qt::DisplayRole).toString(),
                 QStringLiteral("255"));

        // 行级缩放覆盖 (窗口级关, 行 1 开 ×10)
        PollDefinition::RowStyle sc;
        sc.hasScale = true;
        sc.scaleOn = true;
        sc.scaleM = 10.0;
        d2.rowStyles[1] = sc;
        m2.setDefinition(d2);
        QCOMPARE(m2.data(m2.index(1, PollTableModel::ColValue), Qt::DisplayRole).toString(),
                 QStringLiteral("2550"));
        // 清除覆盖 → 回落窗口级
        d2.rowStyles.remove(1);
        m2.setDefinition(d2);
        QCOMPARE(m2.data(m2.index(1, PollTableModel::ColValue), Qt::DisplayRole).toString(),
                 QStringLiteral("255"));

        // 行级变色覆盖: 窗口级无规则, 行 1 规则 >100 → 红
        PollDefinition::RowStyle rl;
        rl.hasRules = true;
        rl.rules[0].op = 1;
        rl.rules[0].lo = 100;
        d2.rowStyles[1] = rl;
        m2.setDefinition(d2);
        QVERIFY(m2.data(m2.index(1, PollTableModel::ColValue), Qt::ForegroundRole).isValid());
        QVERIFY(!m2.data(m2.index(0, PollTableModel::ColValue), Qt::ForegroundRole)
                    .isValid());

        // 单段窗口 (无 segs) 不做段底色
        QVERIFY(!m2.data(m2.index(0, PollTableModel::ColName), Qt::BackgroundRole).isValid());

        // 行级值名覆盖: 行 0 映射 255→满, 行 1 不映射
        PollDefinition::RowStyle vn;
        vn.hasVNames = true;
        vn.valueNames.insert(255, QStringLiteral("满"));
        d2.rowStyles[0] = vn;
        m2.setDefinition(d2);
        QCOMPARE(m2.data(m2.index(0, PollTableModel::ColValue), Qt::DisplayRole).toString(),
                 QStringLiteral("满"));
        QCOMPARE(m2.data(m2.index(1, PollTableModel::ColValue), Qt::DisplayRole).toString(),
                 QStringLiteral("255"));

        // 写窗口行级格式覆盖: 编辑解析按行有效格式
        PollDefinition w2;
        w2.fc = 16;
        w2.qty = 2;
        w2.fmt = Fmt::U16;
        w2.rowStyles.insert(0, hexRow);
        PollTableModel m3;
        m3.setDefinition(w2);
        QVERIFY(m3.setData(m3.index(0, PollTableModel::ColValue), QStringLiteral("ABCD"),
                           Qt::EditRole));
        QVERIFY(!m3.setData(m3.index(1, PollTableModel::ColValue), QStringLiteral("ABCD"),
                            Qt::EditRole)); // 行 1 = U16, "ABCD" 非法
        QCOMPARE(m3.regs().value(0), quint16(0xABCD));
    }

    void defDialogSegments()
    {
        PollDefinition d;
        d.fc = 3;
        d.addr = 100;
        d.qty = 3;
        DefDialog::DefDialog dlg(nullptr, d);
        auto *tbl = dlg.findChild<QTableWidget *>(QStringLiteral("f8.segs"));
        QVERIFY(tbl);
        QCOMPARE(tbl->rowCount(), 1); // 预填首段
        QCOMPARE(tbl->item(0, 0)->text(), QStringLiteral("100"));
        // 追加第 2 段 → 双段定义
        tbl->insertRow(1);
        tbl->setItem(1, 0, new QTableWidgetItem(QStringLiteral("200")));
        tbl->setItem(1, 1, new QTableWidgetItem(QStringLiteral("2")));
        QString err;
        QVERIFY(dlg.apply(&err));
        QCOMPARE(d.segs.size(), 2);
        QCOMPARE(int(d.segs[0].addr), 100);
        QCOMPARE(d.segs[0].qty, 3);
        QCOMPARE(int(d.segs[1].addr), 200);
        QCOMPARE(d.segs[1].qty, 2);
        QCOMPARE(d.totalQty(), 5);
        QCOMPARE(d.rowAddr(3), 200);
        // 段地址非法 → 拒绝
        tbl->item(1, 0)->setText(QStringLiteral("abc"));
        QVERIFY(!dlg.apply(&err));
        QVERIFY(!err.isEmpty());
        // 段数量越上限 → 拒绝
        tbl->item(1, 0)->setText(QStringLiteral("200"));
        tbl->item(1, 1)->setText(QStringLiteral("9999"));
        QVERIFY(!dlg.apply(&err));
        // 切写功能 → 单地址/数量路径, segs 清空
        tbl->item(1, 1)->setText(QStringLiteral("2"));
        auto *fc = dlg.findChild<QComboBox *>(QStringLiteral("f8.fc"));
        QVERIFY(fc);
        const int i16 = fc->findData(16);
        QVERIFY(i16 >= 0);
        fc->setCurrentIndex(i16);
        QVERIFY(dlg.apply(&err));
        QVERIFY(d.segs.isEmpty());
        QVERIFY(d.isWriteFc());
    }

    void csvExport()
    {
        PollDefinition d;
        d.fc = 3;
        d.addr = 100;
        d.qty = 2;
        d.nameCells = {QStringLiteral("温度"), QString()};
        PollTableModel m;
        m.setDefinition(d);
        m.applyRegs(QVector<quint16>{1000, 5});
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("out.csv"));
        QVERIFY(DataViewWidget::exportCsv(path, &m));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray all = f.readAll();
        QVERIFY(all.startsWith("\xEF\xBB\xBF"));
        const QString text = QString::fromUtf8(all);
        QVERIFY(text.contains(QStringLiteral("address,value,name")));
        QVERIFY(text.contains(QStringLiteral("100,\"1000\",\"温度\"")));
        QVERIFY(text.contains(QStringLiteral("101,\"5\",\"\"")));
    }

    // ---------- Test Center (旧 ModbusGenTool 断言迁移 + 新增) ----------
    // 迁移自 tst_modbusgen: 连续生成防崩溃 (QFormLayout::takeRow 回归) +
    // 十进制(含浮点)转换 + 精确帧断言; objectName 前缀 modbusGen.* → tc.*

    TestCenterPage *makeTc(QSpinBox **slave, QSpinBox **addr, QSpinBox **qty,
                           QComboBox **fc, QPlainTextEdit **writeData,
                           QLineEdit **dec, QPushButton **genBtn,
                           QPushButton **decBtn, QLabel **adu)
    {
        auto *mb = new MasterClient(this);
        auto *tc = new TestCenterPage(mb); // 无 QWidget 父 (Tmm 非 QWidget), 用例末 delete
        *slave = tc->findChild<QSpinBox *>(QStringLiteral("tc.slave"));
        *addr = tc->findChild<QSpinBox *>(QStringLiteral("tc.addr"));
        *qty = tc->findChild<QSpinBox *>(QStringLiteral("tc.qty"));
        *fc = tc->findChild<QComboBox *>(QStringLiteral("tc.fc"));
        *writeData = tc->findChild<QPlainTextEdit *>(QStringLiteral("tc.writeData"));
        *dec = tc->findChild<QLineEdit *>(QStringLiteral("tc.dec"));
        *genBtn = tc->findChild<QPushButton *>(QStringLiteral("tc.genBtn"));
        *decBtn = tc->findChild<QPushButton *>(QStringLiteral("tc.decBtn"));
        *adu = tc->findChild<QLabel *>(QStringLiteral("tc.aduHex"));
        return tc;
    }

    void testCenterGenerateFrames()
    {
        QSpinBox *slave = nullptr, *addr = nullptr, *qty = nullptr;
        QComboBox *fc = nullptr;
        QPlainTextEdit *writeData = nullptr;
        QLineEdit *dec = nullptr;
        QPushButton *genBtn = nullptr, *decBtn = nullptr;
        QLabel *adu = nullptr;
        auto *tc = makeTc(&slave, &addr, &qty, &fc, &writeData, &dec, &genBtn, &decBtn, &adu);
        auto setFc = [fc](int code) {
            const int i = fc->findData(code);
            QVERIFY(i >= 0);
            fc->setCurrentIndex(i);
        };
        // FC10 × 5 次连续生成: 曾因 QFormLayout 清空崩溃, 此处覆盖多次重填路径
        setFc(0x10);
        slave->setValue(1);
        addr->setValue(0);
        qty->setValue(2);
        writeData->setPlainText(QStringLiteral("00 00 00 00"));
        for (int i = 0; i < 5; ++i)
        {
            genBtn->click();
            qApp->processEvents();
        }
        QCOMPARE(adu->text(), QStringLiteral("0110000000020400000000F3AF"));
        // 浮点转换 + 生成 (float32 占 2 寄存器, 余补 00)
        qty->setValue(2);
        dec->setText(QStringLiteral("3.14"));
        decBtn->click();
        QCOMPARE(writeData->toPlainText(), QStringLiteral("40 48 F5 C3"));
        qty->setValue(3);
        decBtn->click();
        QCOMPARE(writeData->toPlainText(), QStringLiteral("40 48 F5 C3 00 00"));
        genBtn->click();
        // 生成直接填完整帧 (从机 + PDU + CRC16, 低字节在前)
        auto *wireEdit = tc->findChild<QPlainTextEdit *>(QStringLiteral("tc.wireEdit"));
        QString perr;
        const QByteArray body =
            TestCenter::parseWireHex(QStringLiteral("01 10 00 00 00 03 06 40 48 F5 C3 00 00"), &perr);
        QVERIFY(!body.isEmpty());
        const QByteArray framed = TestCenter::addCheckRtu(body);
        QCOMPARE(wireEdit->toPlainText(), ToolFrames::hexSpaced(framed));
        // 预览不双包: 发送框已含 CRC → 完整报文与发送框一致 (无第二个 CRC)
        QCOMPARE(adu->text(), ToolFrames::hexPlain(framed));
        // 追加 CRC 幂等: 已含正确 CRC 时不再追加
        auto *crcBtn = tc->findChild<QPushButton *>(QStringLiteral("tc.crcBtn"));
        QVERIFY(crcBtn);
        crcBtn->click();
        qApp->processEvents();
        QCOMPARE(wireEdit->toPlainText(), ToolFrames::hexSpaced(framed));
        delete tc;
    }

    void testCenterDecimalAndFrames()
    {
        // 迁移自 tst_modbusgen: 纯逻辑断言 (convertDecimalToHex + buildWire)
        QString err;
        QCOMPARE(TestCenter::convertDecimalToHex(0x10, 2, QStringLiteral("1000"), &err),
                 QStringLiteral("00 00 03 E8"));
        QCOMPARE(TestCenter::convertDecimalToHex(0x10, 1, QStringLiteral("-1"), &err),
                 QStringLiteral("FF FF"));
        QCOMPARE(TestCenter::convertDecimalToHex(0x10, 2, QStringLiteral("-1"), &err),
                 QStringLiteral("FF FF FF FF"));
        QCOMPARE(TestCenter::convertDecimalToHex(0x10, 3, QStringLiteral("1000"), &err),
                 QStringLiteral("00 00 00 00 03 E8"));
        QCOMPARE(TestCenter::convertDecimalToHex(0x0F, 10, QStringLiteral("1023"), &err),
                 QStringLiteral("FF 03")); // 10 线圈全 ON = 0x03FF, 首字节 bit0=线圈0
        QCOMPARE(TestCenter::convertDecimalToHex(0x06, 1, QStringLiteral("255"), &err),
                 QStringLiteral("00FF"));
        QCOMPARE(TestCenter::convertDecimalToHex(0x05, 1, QStringLiteral("1"), &err),
                 QStringLiteral("FF00"));
        QCOMPARE(TestCenter::convertDecimalToHex(0x05, 1, QStringLiteral("0"), &err),
                 QStringLiteral("0000"));
        // 字节数与数量不符: 拒绝 (旧版会生成非法帧)
        TestCenter::BuildSpec bad;
        bad.fc = 0x10;
        bad.qty = 2;
        bad.writeHex = QStringLiteral("00");
        QVERIFY(TestCenter::buildWire(bad, &err).isEmpty());
        QVERIFY(!err.isEmpty());
        // FC17 读/写多寄存器精确帧
        TestCenter::BuildSpec s17;
        s17.fc = 0x17;
        s17.addr = 0;
        s17.qty = 2;
        s17.addr2 = 10;
        s17.qty2 = 2;
        s17.writeHex = QStringLiteral("00 0A 00 14");
        const QByteArray w17 = TestCenter::buildWire(s17, &err);
        QCOMPARE(QString::fromLatin1(w17.toHex().toUpper()),
                 QStringLiteral("011700000002000A000204000A0014"));
    }

    void testCenterCoreParsing()
    {
        // 原始串解析: 空格/逗号/无间隔混排 + 注释行
        QString err;
        QCOMPARE(TestCenter::parseWireHex(QStringLiteral("01 03 00 0A"), &err).size(), 4);
        QCOMPARE(TestCenter::parseWireHex(QStringLiteral("0103000A"), &err).size(), 4);
        QCOMPARE(TestCenter::parseWireHex(QStringLiteral("01,03;00 0A"), &err).size(), 4);
        QCOMPARE(TestCenter::parseWireHex(QStringLiteral("// 注释\n01 03"), &err).size(), 2);
        QCOMPARE(TestCenter::parseWireHex(QStringLiteral("01 03 // 尾注释"), &err).size(), 2);
        QCOMPARE(TestCenter::parseWireHex(QStringLiteral("01 030 // 尾注释"), &err).size(), 0);
        QVERIFY(TestCenter::parseWireHex(QStringLiteral("01 030"), &err).isEmpty()); // 奇数
        QVERIFY(TestCenter::parseWireHex(QString(), &err).isEmpty());
        // Add Check: CRC 低字节在前 (旧断言帧 0110…F3AF 的尾巴)
        const QByteArray wire = TestCenter::parseWireHex(QStringLiteral("0110000000020400000000"), &err);
        const QByteArray framed = TestCenter::addCheckRtu(wire);
        QCOMPARE(ToolFrames::hexPlain(framed), QStringLiteral("0110000000020400000000F3AF"));
        // 发送前剥离: 带 CRC → 剥; MBAP → 剥; 裸帧 → 原样
        QString note;
        QCOMPARE(TestCenter::stripForSend(false, framed, &note), wire);
        QVERIFY(!note.isEmpty());
        const QByteArray tcpAdu = ToolFrames::tcpFrame(wire);
        QCOMPARE(TestCenter::stripForSend(true, tcpAdu, &note), wire);
        QCOMPARE(TestCenter::stripForSend(false, wire, &note), wire);
        // 响应解析: 正常读 + 异常帧中文
        const QByteArray resp = TestCenter::parseWireHex(QStringLiteral("03 02 00 64"), &err);
        const QVector<TestCenter::Field> rows = TestCenter::analyzeResponse(resp);
        QVERIFY(rows.size() >= 3);
        QCOMPARE(rows.at(2).value, QStringLiteral("0x0064(100)"));
        const QByteArray exc = TestCenter::parseWireHex(QStringLiteral("83 02"), &err);
        const QVector<TestCenter::Field> erows = TestCenter::analyzeResponse(exc);
        QVERIFY(erows.at(1).value.contains(QStringLiteral("非法数据地址")));
        QCOMPARE(TestCenter::exceptionText(0x06), QStringLiteral("从站设备忙"));
    }

    void plcAddressParse()
    {
        auto p = DefDialog::parsePlcAddress(QStringLiteral("100"));
        QVERIFY(p.ok);
        QCOMPARE(p.region, -1); // 协议地址直输
        QCOMPARE(p.addr, 100);
        p = DefDialog::parsePlcAddress(QStringLiteral("0"));
        QVERIFY(p.ok);
        QCOMPARE(p.addr, 0);
        p = DefDialog::parsePlcAddress(QStringLiteral("40001")); // 纯数字 = 协议地址
        QVERIFY(p.ok);
        QCOMPARE(p.region, -1);
        QCOMPARE(p.addr, 40001);
        p = DefDialog::parsePlcAddress(QStringLiteral("160")); // 常见寄存器地址不受段语义误伤
        QCOMPARE(p.region, -1);
        QCOMPARE(p.addr, 160);
        p = DefDialog::parsePlcAddress(QStringLiteral("400001")); // 6 位保持段首
        QVERIFY(p.ok);
        QCOMPARE(p.region, 4);
        QCOMPARE(p.addr, 0);
        p = DefDialog::parsePlcAddress(QStringLiteral("400010"));
        QCOMPARE(p.region, 4);
        QCOMPARE(p.addr, 9);
        p = DefDialog::parsePlcAddress(QStringLiteral("100001")); // 6 位离散段首
        QCOMPARE(p.region, 1);
        p = DefDialog::parsePlcAddress(QStringLiteral("300001")); // 6 位输入段首
        QCOMPARE(p.region, 3);
        p = DefDialog::parsePlcAddress(QStringLiteral("410001")); // 6 位扩展
        QVERIFY(p.ok);
        QCOMPARE(p.region, 4);
        QCOMPARE(p.addr, 10000);
        p = DefDialog::parsePlcAddress(QStringLiteral("4x00001")); // 前缀风格
        QVERIFY(p.ok);
        QCOMPARE(p.region, 4);
        QCOMPARE(p.addr, 0);
        p = DefDialog::parsePlcAddress(QStringLiteral("0x1"));
        QCOMPARE(p.region, 0);
        QCOMPARE(p.addr, 0);
        QVERIFY(!DefDialog::parsePlcAddress(QStringLiteral("abc")).ok);
        QVERIFY(!DefDialog::parsePlcAddress(QStringLiteral("65536")).ok);
        QVERIFY(!DefDialog::parsePlcAddress(QStringLiteral("99999")).ok);
        QVERIFY(!DefDialog::parsePlcAddress(QString()).ok);
        // 区域默认功能码
        QCOMPARE(DefDialog::regionDefaultFc(4), 3);
        QCOMPARE(DefDialog::regionDefaultFc(1), 2);
    }

    void defDialogApply()
    {
        PollDefinition d; // 默认值
        DefDialog::DefDialog dlg(nullptr, d);
        auto *slave = dlg.findChild<QSpinBox *>(QStringLiteral("f8.slave"));
        auto *fc = dlg.findChild<QComboBox *>(QStringLiteral("f8.fc"));
        auto *addr = dlg.findChild<QLineEdit *>(QStringLiteral("f8.addr"));
        auto *qty = dlg.findChild<QSpinBox *>(QStringLiteral("f8.qty"));
        auto *rate = dlg.findChild<QSpinBox *>(QStringLiteral("f8.rate"));
        auto *disabled = dlg.findChild<QCheckBox *>(QStringLiteral("f8.disabled"));
        auto *plc = dlg.findChild<QCheckBox *>(QStringLiteral("f8.plcBase1"));
        QVERIFY(slave && fc && addr && qty && rate && disabled && plc);
        slave->setValue(5);
        fc->setCurrentIndex(fc->findData(16));
        addr->setText(QStringLiteral("400010")); // 6 位 PLC 段 → 保持 9
        qty->setValue(8);
        rate->setValue(250);
        disabled->setChecked(true);
        plc->setChecked(true);
        QVERIFY(dlg.apply());
        QCOMPARE(d.slave, 5);
        QCOMPARE(d.fc, 16);
        QCOMPARE(int(d.addr), 9);
        QCOMPARE(d.qty, 8);
        QCOMPARE(d.scanRateMs, 250);
        QVERIFY(!d.enabled);
        QVERIFY(d.plcBase1);
        QVERIFY(d.isWriteFc());
        // 非法地址拒绝 (无模态弹窗, 错误文本带回)
        QString err;
        addr->setText(QStringLiteral("abc"));
        QVERIFY(!dlg.apply(&err));
        QVERIFY(!err.isEmpty());
    }

    void frameLogDisplay()
    {
        // 帧日志展示为线上完整帧: RTU = [地址][PDU][CRC], TCP = [MBAP][PDU]
        FrameLogDialog dlg;
        FrameRecord rtu;
        rtu.isTx = true;
        rtu.isRtu = true;
        rtu.addr = 1;
        rtu.pdu = commPduBytes(3, QByteArrayLiteral("\x00\x00\x00\x0A"));
        dlg.appendFrame(rtu);
        FrameRecord tcp;
        tcp.isTx = false;
        tcp.isRtu = false;
        tcp.mbapTid = 0x1234;
        tcp.addr = 5;
        tcp.pdu = commPduBytes(3, QByteArrayLiteral("\x00\x00\x00\x02"));
        dlg.appendFrame(tcp);
        auto *t = dlg.findChild<QTableWidget *>();
        QVERIFY(t);
        QCOMPARE(t->item(0, 2)->text(),
                 QString::fromLatin1(commRtuAdu(1, rtu.pdu).toHex(' ').toUpper()));
        QCOMPARE(t->item(1, 2)->text(),
                 QString::fromLatin1(commTcpAdu(0x1234, 5, tcp.pdu).toHex(' ').toUpper()));
        // 无线上字节 (超时等) → 占位文本, 不补 CRC
        FrameRecord lost;
        lost.isTx = false;
        lost.isRtu = true;
        dlg.appendFrame(lost);
        QVERIFY(t->item(2, 2)->text().contains(QStringLiteral("无线上字节")));
    }

    void tcpFrameEcho()
    {
        // 本机回环 Modbus TCP 从站: 验证帧日志为完整 MBAP 帧, TID 镜像 Qt 内部
        // 计数 (首帧 0, 每请求 +1), 且每请求恰 1 TX + 1 RX (无重复接线)
        QTcpServer srv;
        QVERIFY(srv.listen(QHostAddress::LocalHost));
        QByteArray rxBuf;
        QList<QByteArray> reqAdus;
        QObject::connect(&srv, &QTcpServer::newConnection, &srv, [&] {
            while (QTcpSocket *c = srv.nextPendingConnection())
            {
                // socket 按值捕获 (循环变量即焚); 缓冲区按引用共享
                QObject::connect(c, &QTcpSocket::readyRead, c, [&rxBuf, &reqAdus, c] {
                    rxBuf += c->readAll();
                    forever
                    {
                        if (rxBuf.size() < 7)
                            return;
                        const int len = (quint8(rxBuf.at(4)) << 8) | quint8(rxBuf.at(5));
                        if (rxBuf.size() < 6 + len)
                            return;
                        const QByteArray adu = rxBuf.left(6 + len);
                        rxBuf.remove(0, 6 + len);
                        reqAdus.append(adu);
                        // FC03 请求 [03][addr][qty] → 应答 [03][2*qty][递增值]
                        const QByteArray pdu = adu.mid(7);
                        if (quint8(pdu.at(0)) != 3 || pdu.size() != 5)
                            return;
                        const int addr = (quint8(pdu.at(1)) << 8) | quint8(pdu.at(2));
                        const int qty = (quint8(pdu.at(3)) << 8) | quint8(pdu.at(4));
                        QByteArray respPdu;
                        respPdu.append(char(0x03));
                        respPdu.append(char(qty * 2));
                        for (int i = 0; i < qty; ++i)
                        {
                            const int v = addr + i;
                            respPdu.append(char(v >> 8));
                            respPdu.append(char(v & 0xFF));
                        }
                        QByteArray resp = adu.left(2); // TID 原样回显
                        resp.append('\x00').append('\x00'); // 协议 ID
                        resp.append(char((respPdu.size() + 1) >> 8));
                        resp.append(char((respPdu.size() + 1) & 0xFF));
                        resp.append(adu.mid(6, 1)); // 单元 ID 原样回显
                        resp += respPdu;
                        c->write(resp);
                    }
                });
            }
        });

        MasterClient mb;
        mb.setKind(MasterClient::Kind::Tcp);
        mb.setTcpParams(QStringLiteral("127.0.0.1"), srv.serverPort());
        mb.setTimeoutMs(1000);
        mb.setRetries(0);
        QVERIFY(mb.connectDevice());
        QTRY_VERIFY(mb.isConnected());

        QList<FrameRecord> frames;
        QObject::connect(&mb, &MasterClient::frameLogged, &mb,
                         [&](const FrameRecord &fr) { frames.append(fr); });

        QSignalSpy spy(&mb, &MasterClient::readDone);
        QVERIFY(mb.read(3, 1, 100, 2));
        QVERIFY(spy.wait(2000));
        QCOMPARE(spy.first().at(3).toBool(), true);
        const QVector<quint16> got = spy.first().at(4).value<QVector<quint16>>();
        QCOMPARE(got, (QVector<quint16> {100, 101}));
        QSignalSpy spy2(&mb, &MasterClient::readDone);
        QVERIFY(mb.read(3, 1, 200, 1));
        QVERIFY(spy2.wait(2000));
        QCOMPARE(spy2.first().at(3).toBool(), true);

        QCOMPARE(frames.size(), 4);
        for (int i = 0; i < 4; ++i)
            QCOMPARE(frames[i].isTx, i % 2 == 0); // 方向交替: 每请求 1 TX + 1 RX
        for (int i = 0; i < 4; ++i)
            QCOMPARE(frames[i].mbapTid, quint16(i / 2)); // TID 0,0,1,1 (RX 复用请求号)
        QVERIFY(!frames[0].isRtu);
        QCOMPARE(frames[0].pdu, QByteArray::fromHex("0300640002"));
        QCOMPARE(frames[1].pdu, QByteArray::fromHex("030400640065"));
        // 从站实际收到 2 条 ADU, TID 与帧日志镜像一致 (0, 1)
        QCOMPARE(reqAdus.size(), 2);
        QCOMPARE(int(quint8(reqAdus[0].at(0))), 0x00);
        QCOMPARE(int(quint8(reqAdus[0].at(1))), 0x00);
        QCOMPARE(int(quint8(reqAdus[1].at(0))), 0x00);
        QCOMPARE(int(quint8(reqAdus[1].at(1))), 0x01);
        // 槽内消化延迟删除 (setKind 换传输的旧客户端) 与 socket 收尾:
        // 避免析构时序泄入下一槽 (QTest 槽间仅隐式处理队列)
        mb.disconnectDevice();
        QTest::qWait(200);
    }

    void scanDialogSmoke()
    {
        // 四页装配 + TCP 发现页按本机网段预填 + 端口默认 502;
        // 构造/析构 (未扫描直接关闭) 不得改写共享连接参数 — 曾因恢复
        // 未初始化的 saved 超时/重试 0 值导致后续全部请求秒超时 (回归点)
        MasterClient mb;
        mb.setTimeoutMs(1234);
        mb.setRetries(2);
        {
            ScanDialog dlg(&mb);
            auto *tabs = dlg.findChild<QTabWidget *>();
            QVERIFY(tabs);
            QCOMPARE(tabs->count(), 4);
            QVERIFY(dlg.findChild<QPushButton *>(QStringLiteral("scan.startSlave")));
            QVERIFY(dlg.findChild<QPushButton *>(QStringLiteral("scan.startAddr")));
            QVERIFY(dlg.findChild<QPushButton *>(QStringLiteral("scan.startSerial")));
            QVERIFY(dlg.findChild<QPushButton *>(QStringLiteral("scan.startTcp")));
            QVERIFY(dlg.findChild<QTableWidget *>(QStringLiteral("scan.serialTable")));
            QVERIFY(dlg.findChild<QTableWidget *>(QStringLiteral("scan.tcpTable")));
            auto *port = dlg.findChild<QSpinBox *>(QStringLiteral("scan.tcpPort"));
            QVERIFY(port);
            QCOMPARE(port->value(), 502);
            auto *toMs = dlg.findChild<QSpinBox *>(QStringLiteral("scan.probeTimeoutMs"));
            QVERIFY(toMs);
            QCOMPARE(toMs->value(), 100); // 无应答超时默认 100ms (快速扫描)
            const QStringList parts =
                dlg.findChild<QLineEdit *>(QStringLiteral("scan.tcpFrom"))->text().split('.');
            QCOMPARE(parts.size(), 4);
            QCOMPARE(parts.last(), QStringLiteral("1")); // 网段末位 .1 预填
        }
        QCOMPARE(mb.timeoutMs(), 1234);
        QCOMPARE(mb.retries(), 2);
    }

    void tcpDiscoverLoopback()
    {
        // 回环 Modbus/TCP 从站 (任意单元 ID 均应答 FC03) → TCP 发现全流程:
        // 端口探测命中 → 逐单元探测入表 → 完成恢复共享连接参数与传输类型,
        // 双击结果发 tcpPicked (参数回填由主窗接线, 此处验证载荷)
        QTcpServer srv;
        QVERIFY(srv.listen(QHostAddress::LocalHost));
        QObject::connect(&srv, &QTcpServer::newConnection, &srv, [&] {
            while (QTcpSocket *c = srv.nextPendingConnection())
            {
                // 每连接独立缓冲 (shared_ptr 随 lambda 存活)
                QObject::connect(c, &QTcpSocket::readyRead, c,
                                 [buf = std::make_shared<QByteArray>(), c] {
                                     *buf += c->readAll();
                                     forever
                                     {
                                         if (buf->size() < 7)
                                             return;
                                         const int len =
                                             (quint8(buf->at(4)) << 8) | quint8(buf->at(5));
                                         if (buf->size() < 6 + len)
                                             return;
                                         const QByteArray adu = buf->left(6 + len);
                                         buf->remove(0, 6 + len);
                                         const QByteArray pdu = adu.mid(7);
                                         if (quint8(pdu.at(0)) != 3 || pdu.size() != 5)
                                             return;
                                         const int qty =
                                             (quint8(pdu.at(3)) << 8) | quint8(pdu.at(4));
                                         QByteArray respPdu;
                                         respPdu.append(char(0x03));
                                         respPdu.append(char(qty * 2));
                                         for (int i = 0; i < qty; ++i)
                                             respPdu.append('\x00').append('\x07');
                                         QByteArray resp = adu.left(2); // TID 原样回显
                                         resp.append('\x00').append('\x00');
                                         resp.append(char((respPdu.size() + 1) >> 8));
                                         resp.append(char((respPdu.size() + 1) & 0xFF));
                                         resp.append(adu.mid(6, 1)); // 单元 ID 原样回显
                                         resp += respPdu;
                                         c->write(resp);
                                     }
                                 });
            }
        });

        MasterClient mb; // 未连接起步: TCP 发现不要求先连接
        mb.setTimeoutMs(1000);
        ScanDialog dlg(&mb);
        auto *from = dlg.findChild<QLineEdit *>(QStringLiteral("scan.tcpFrom"));
        auto *to = dlg.findChild<QLineEdit *>(QStringLiteral("scan.tcpTo"));
        auto *port = dlg.findChild<QSpinBox *>(QStringLiteral("scan.tcpPort"));
        auto *unitTo = dlg.findChild<QSpinBox *>(QStringLiteral("scan.tcpUnitTo"));
        auto *stopFirst = dlg.findChild<QCheckBox *>(QStringLiteral("scan.tcpStopFirst"));
        auto *start = dlg.findChild<QPushButton *>(QStringLiteral("scan.startTcp"));
        QVERIFY(from && to && port && unitTo && stopFirst && start);
        from->setText(QStringLiteral("127.0.0.1"));
        to->setText(QStringLiteral("127.0.0.1"));
        port->setValue(srv.serverPort());
        unitTo->setValue(3);
        stopFirst->setChecked(false); // 扫全单元段: 验证 1..3 全部入表

        QSignalSpy busySpy(&dlg, &ScanDialog::scanBusyChanged);
        start->click();
        QCOMPARE(busySpy.count(), 1);
        QCOMPARE(busySpy.first().first().toBool(), true); // 开始: 主窗应暂停轮询
        QCOMPARE(start->isEnabled(), false);
        auto *progress = dlg.findChild<QLabel *>(QStringLiteral("scan.progress"));
        QVERIFY(progress);
        QTRY_VERIFY_WITH_TIMEOUT(progress->text().contains(QStringLiteral("完成")),
                                 15000);
        QCOMPARE(busySpy.count(), 2);
        QCOMPARE(busySpy.last().first().toBool(), false); // 结束: 恢复轮询态

        auto *table = dlg.findChild<QTableWidget *>(QStringLiteral("scan.tcpTable"));
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 3);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("127.0.0.1"));
        QCOMPARE(table->item(0, 2)->text(), QStringLiteral("1"));
        QCOMPARE(table->item(2, 2)->text(), QStringLiteral("3"));

        // 共享连接参数/传输类型已恢复 (扫描中临时切到 TCP + 300ms 短超时)
        QCOMPARE(mb.kind(), MasterClient::Kind::SerialRtu);
        QCOMPARE(mb.timeoutMs(), 1000);
        QCOMPARE(mb.retries(), 2);

        // 双击结果 → tcpPicked 载荷 (host/port/unit)
        QSignalSpy pickSpy(&dlg, &ScanDialog::tcpPicked);
        emit table->cellDoubleClicked(0, 0);
        QCOMPARE(pickSpy.count(), 1);
        QCOMPARE(pickSpy.first().at(0).toString(), QStringLiteral("127.0.0.1"));
        QCOMPARE(pickSpy.first().at(1).toInt(), srv.serverPort());
        QCOMPARE(pickSpy.first().at(2).toInt(), 1);

        mb.disconnectDevice();
        QTest::qWait(200); // 消化 socket 收尾, 避免泄入下一槽
    }

    // 本机回环 FC03 从站: 应答值 = 地址 + i (便于按段核对上屏数据)
    static void startF03LoopbackSlave(QTcpServer *srv, QList<QByteArray> *reqAdus)
    {
        QVERIFY(srv->listen(QHostAddress::LocalHost));
        QObject::connect(srv, &QTcpServer::newConnection, srv, [srv, reqAdus] {
            while (QTcpSocket *c = srv->nextPendingConnection())
            {
                QObject::connect(c, &QTcpSocket::readyRead, c, [reqAdus, c] {
                    QByteArray rxBuf = c->property("rx").toByteArray();
                    rxBuf += c->readAll();
                    forever
                    {
                        if (rxBuf.size() < 7)
                            break;
                        const int len = (quint8(rxBuf.at(4)) << 8) | quint8(rxBuf.at(5));
                        if (rxBuf.size() < 6 + len)
                            break;
                        const QByteArray adu = rxBuf.left(6 + len);
                        rxBuf.remove(0, 6 + len);
                        reqAdus->append(adu);
                        const QByteArray pdu = adu.mid(7);
                        if (quint8(pdu.at(0)) != 3 || pdu.size() != 5)
                            continue;
                        const int addr = (quint8(pdu.at(1)) << 8) | quint8(pdu.at(2));
                        const int qty = (quint8(pdu.at(3)) << 8) | quint8(pdu.at(4));
                        QByteArray respPdu;
                        respPdu.append(char(0x03));
                        respPdu.append(char(qty * 2));
                        for (int i = 0; i < qty; ++i)
                        {
                            const int v = addr + i;
                            respPdu.append(char(v >> 8));
                            respPdu.append(char(v & 0xFF));
                        }
                        QByteArray resp = adu.left(2); // TID 原样回显
                        resp.append('\x00').append('\x00');
                        resp.append(char((respPdu.size() + 1) >> 8));
                        resp.append(char((respPdu.size() + 1) & 0xFF));
                        resp.append(adu.mid(6, 1));
                        resp += respPdu;
                        c->write(resp);
                    }
                    c->setProperty("rx", rxBuf);
                });
            }
        });
    }

    void multiSegPollLoopback()
    {
        // 多段轮询链: 一个扫描周期 = 顺序读完全部段, 段应答链发下一段,
        // 末段完成后 statusMessage(OK) — 各段数据落在正确行位
        QTcpServer srv;
        QList<QByteArray> reqAdus;
        startF03LoopbackSlave(&srv, &reqAdus);

        MasterClient mb;
        mb.setKind(MasterClient::Kind::Tcp);
        mb.setTcpParams(QStringLiteral("127.0.0.1"), srv.serverPort());
        mb.setTimeoutMs(1000);
        mb.setRetries(0);
        QVERIFY(mb.connectDevice());
        QTRY_VERIFY(mb.isConnected());

        DataViewWidget w(&mb);
        PollDefinition d;
        d.fc = 3;
        d.scanRateMs = 0; // 不起定时器, F6 手动触发
        d.segs = {{100, 3}, {200, 2}};
        w.setDefinition(d);
        w.setConnected(true); // 连接态由主窗下发 (onConnectionChanged)
        QCOMPARE(w.definition().totalQty(), 5);

        QSignalSpy status(&w, &DataViewWidget::statusMessage);
        w.readWriteOnce();
        QTRY_COMPARE(status.size(), 1); // 周期完成时一次 OK 状态
        QCOMPARE(status.takeFirst().at(1).toBool(), true);
        // 从站应答值 = 地址+i: 段1 → 行0..2 = 100..102, 段2 → 行3..4 = 200..201
        auto *model = w.findChild<PollTableModel *>();
        QVERIFY(model);
        QCOMPARE(model->regs(), (QVector<quint16> {100, 101, 102, 200, 201}));
        // 两段 = 2 条 TX 请求 (链式, 无重复)
        QCOMPARE(reqAdus.size(), 2);
        QCOMPARE(reqAdus[0].mid(7), QByteArray::fromHex("0300640003"));
        QCOMPARE(reqAdus[1].mid(7), QByteArray::fromHex("0300C80002"));

        mb.disconnectDevice();
        QTest::qWait(200);
    }

    void masterClientConnectGuard()
    {
        // 契约: 空串口名 → connectDevice 同步失败且失败原因可读
        // (主窗据此把"没反应"转成显式引导/报错)
        MasterClient mb;
        mb.setSerialParams(QString(), 115200, QSerialPort::NoParity,
                           QSerialPort::OneStop);
        QVERIFY(!mb.isConnected());
        const bool opened = mb.connectDevice();
        if (opened)
        {
            // 某些后端异步失败: 等一轮事件循环后必须回到未连接且带原因
            QTest::qWait(300);
            QVERIFY(!mb.isConnected());
            QVERIFY(!mb.lastError().isEmpty());
        }
        else
        {
            QVERIFY(!mb.lastError().isEmpty());
        }
        QVERIFY(!mb.isConnected());
    }

    void realDeviceSmoke()
    {
        // ====================================================================
        // 实机全功能自动化调试 — 标准 1-16 通道变送器 (6 通道) @ COM30 (RTU)
        // 寄存器表: doc/标准变送器/Modbus寄存器分布表.md
        //   0=从站地址 1=波特率码(4800:0..115200:5) 3=协议 5=通道数 7-8=版本
        //   9/10=稳定/在线位图 11+2(n-1)=实时值(4B) 43/75+2(n-1)=峰/谷值(4B)
        //   107=标定通道 108=砝码值 110/111=灵敏度/满量程 113=标定命令(1零点/2增益/4清零/8灵敏度)
        //   117=写锁 194-199=多段标定 200+(n-1)*100=每通道参数区 (236=滤波系数)
        // 实机配置 19200 8N2 从站 1, 可用环境变量覆盖:
        //   HOSTTOOL_REAL_PORT / HOSTTOOL_REAL_BAUD /
        //   HOSTTOOL_REAL_PARITY(N|E|O) / HOSTTOOL_REAL_STOP(1|2) /
        //   HOSTTOOL_REAL_SLAVE
        // 无实机时优雅跳过; 全程帧入 frames, 末尾统一做帧口径断言
        // ====================================================================
        const QString portName = qEnvironmentVariable("HOSTTOOL_REAL_PORT",
                                                      QStringLiteral("COM30"));
        bool present = false;
        const auto ports = QSerialPortInfo::availablePorts();
        for (const auto &p : ports)
            if (p.portName() == portName)
                present = true;
        if (!present)
            QSKIP(qPrintable(QStringLiteral("实机 %1 未接入 (检测到 %2 个串口), 跳过")
                                 .arg(portName)
                                 .arg(ports.size())));

        const int baudEnv = qEnvironmentVariableIntValue("HOSTTOOL_REAL_BAUD");
        const qint32 baud = baudEnv > 0 ? qint32(baudEnv) : 19200;
        const QString parityEnv = qEnvironmentVariable("HOSTTOOL_REAL_PARITY",
                                                       QStringLiteral("N"));
        const QSerialPort::Parity parity =
                parityEnv == QLatin1String("E") ? QSerialPort::EvenParity
              : parityEnv == QLatin1String("O") ? QSerialPort::OddParity
                                                : QSerialPort::NoParity;
        const QString stopEnv = qEnvironmentVariable("HOSTTOOL_REAL_STOP",
                                                     QStringLiteral("2"));
        const QSerialPort::StopBits stop =
                stopEnv == QLatin1String("1") ? QSerialPort::OneStop
                                              : QSerialPort::TwoStop;
        const int slaveEnv = qEnvironmentVariableIntValue("HOSTTOOL_REAL_SLAVE");
        const int slave = slaveEnv > 0 ? slaveEnv : 1;

        MasterClient mb;
        mb.setSerialParams(portName, baud, parity, stop);
        mb.setTimeoutMs(1000);
        mb.setRetries(1);
        QVERIFY2(mb.connectDevice(), qPrintable(mb.lastError()));
        QTRY_VERIFY(mb.isConnected());

        QList<FrameRecord> frames;
        QObject::connect(&mb, &MasterClient::frameLogged, &mb,
                         [&](const FrameRecord &fr) { frames.append(fr); });

        // 同步读/写辅助: 恰一次完成信号 (失败/异常路径也会发)
        auto syncRead = [&](quint16 addr, int qty, bool *ok,
                            QVector<quint16> *regs = nullptr,
                            QString *err = nullptr) -> bool {
            QSignalSpy spy(&mb, &MasterClient::readDone);
            if (!mb.read(3, slave, addr, quint16(qty)))
                return false;
            if (!spy.wait(3000))
                return false;
            const auto a = spy.first();
            if (ok)
                *ok = a.at(3).toBool();
            if (regs)
                *regs = a.at(4).value<QVector<quint16>>();
            if (err)
                *err = a.at(5).toString();
            return true;
        };
        auto syncWrite = [&](quint16 addr, const QVector<quint16> &vals, bool *ok,
                             bool forceMulti = false) -> bool {
            QSignalSpy spy(&mb, &MasterClient::writeDone);
            if (!mb.writeRegs(slave, addr, vals, forceMulti))
                return false;
            if (!spy.wait(3000))
                return false;
            if (ok)
                *ok = spy.first().at(3).toBool();
            return true;
        };
        auto i32 = [](const QVector<quint16> &r, int off) {
            return qint32((quint32(r.value(off)) << 16) | quint32(r.value(off + 1)));
        };

        // ---- ① 系统区 (0..10): 从站地址/波特率码/协议/通道数/版本/状态位图 ----
        quint16 chCount = 0;
        quint16 stableBits = 0;
        quint16 onlineBits = 0;
        {
            bool ok = false;
            QVector<quint16> w;
            QVERIFY(syncRead(0, 11, &ok, &w));
            QVERIFY2(ok, "系统区 (0..10) 读失败");
            QCOMPARE(w.size(), 11);
            QCOMPARE(int(w[0]), slave); // 从机地址
            const QHash<qint32, quint16> baudCode = {
                { 4800, 0 }, { 9600, 1 }, { 19200, 2 },
                { 38400, 3 }, { 57600, 4 }, { 115200, 5 }
            };
            if (baudCode.contains(baud))
                QCOMPARE(int(w[1]), int(baudCode[baud])); // 波特率码与实连一致
            QCOMPARE(int(w[3]), 0);                       // 协议 = Modbus RTU
            chCount = w[5];
            QVERIFY2(chCount >= 1 && chCount <= 16, "通道数寄存器 (5) 越界");
            stableBits = w[9];
            onlineBits = w[10];
            const quint32 ver = (quint32(w[7]) << 16) | quint32(w[8]);
            qInfo() << "[① 系统] 从站" << w[0] << "波特率码" << w[1] << "协议" << w[3]
                    << "通道数" << chCount << "版本 0x" << Qt::hex << ver
                    << "稳定位图 0x" << stableBits << "在线位图 0x" << onlineBits
                    << Qt::dec;
        }

        // ---- ② 状态位图 (9, 10): 记录不硬断言 — 稳定/在线是随实况变化的物理状态
        //        (未接传感器通道的稳定位为固件缺省行为, 在线通道随信号波动) ----
        {
            const quint16 mask = quint16((1u << chCount) - 1);
            qInfo() << "[② 状态] 稳定位图 0x" << Qt::hex << stableBits << "在线位图 0x"
                    << onlineBits << "低" << chCount << "位掩码 0x" << mask
                    << "— 仅记录 (接入通道数与信号状态由现场决定)" << Qt::dec;
        }

        // ---- ③ 16 路实时值 (11..42, 每通道 4B) ----
        QVector<qint32> rt(chCount);
        {
            bool ok = false;
            QVector<quint16> w;
            QVERIFY(syncRead(11, 32, &ok, &w));
            QVERIFY2(ok, "实时值区 (11..42) 读失败");
            QCOMPARE(w.size(), 32);
            for (int ch = 0; ch < int(chCount); ++ch)
                rt[ch] = i32(w, 2 * ch);
            QStringList vs;
            for (qint32 v : rt)
                vs << QString::number(v);
            qInfo() << "[③ 实时值] 1.." << chCount << "通道:" << vs.join(QStringLiteral(", "));
        }

        // ---- ④ 峰谷值区 (43..106): 16 峰 + 16 谷 (各 4B) ----
        {
            bool ok = false;
            QVector<quint16> w;
            QVERIFY(syncRead(43, 64, &ok, &w));
            QVERIFY2(ok, "峰谷值区 (43..106) 读失败");
            QCOMPARE(w.size(), 64);
            QStringList ps;
            for (int ch = 0; ch < int(chCount); ++ch)
                ps << QString::number(i32(w, 2 * ch));
            qInfo() << "[④ 峰值] 1.." << chCount << "通道:" << ps.join(QStringLiteral(", "));
        }

        // ---- ⑤ 通道 1 参数区 (200..267): 全读 + 关键参数合法性 ----
        quint16 filterCur = 0;
        QVector<quint16> limitCur;
        {
            bool ok = false;
            QVector<quint16> w;
            QVERIFY(syncRead(200, 68, &ok, &w));
            QVERIFY2(ok, "通道 1 参数区 (200..267) 读失败");
            QCOMPARE(w.size(), 68);
            const int off = 0; // 200 + (1-1)*100
            const quint16 dot = w.value(201 - 200);
            const quint16 div = w.value(202 - 200);
            QVERIFY2(dot <= 5, "小数点 (201) 越界");
            QVERIFY2(div == 1 || div == 2 || div == 5 || div == 10 || div == 20
                         || div == 50,
                     "分度值 (202) 非法");
            filterCur = w.value(236 - 200);
            limitCur = w.mid(260 - 200, 6);
            qInfo() << "[⑤ ch1 参数] 小数点" << dot << "分度值" << div
                    << "AD速度码" << w.value(234 - 200) << "滤波系数" << filterCur
                    << "单位码" << w.value(259 - 200) << "下/中/上限"
                    << i32(w, 260 - 200) << "/" << i32(w, 262 - 200) << "/"
                    << i32(w, 264 - 200);
        }

        // ---- ⑥ 标定流程 (注1): 选通道 → 清零 → 零点确认 → 峰/谷清零 ----
        {
            bool ok = false;
            // 107 = 1 (操作通道 1)
            QVERIFY(syncWrite(107, {1}, &ok));
            QVERIFY2(ok, "标定通道选择 (107=1) 被拒");
            QTest::qWait(100);
            // 113 = 4 (清零范围内清重量值, 标准置零)
            QVERIFY(syncWrite(113, {4}, &ok));
            QVERIFY2(ok, "清零命令 (113=4) 被拒");
            QTest::qWait(100);
            // 113 = 1 (零点确认 — 空秤标准操作, 刷新零点标定)
            QVERIFY(syncWrite(113, {1}, &ok));
            QVERIFY2(ok, "零点确认 (113=1) 被拒");
            QTest::qWait(150);
            // 零点后实时值应回零附近 (清零范围内)
            bool rok = false;
            QVector<quint16> w;
            QVERIFY(syncRead(11, 2, &rok, &w));
            QVERIFY2(rok, "零点后实时值读失败");
            const qint32 z = i32(w, 0);
            qInfo() << "[⑥ 标定] 清零+零点确认完成, 通道 1 实时值 =" << z;
            // 峰/谷清零: 211-212 峰值 ← 0, 213-214 谷值 ← 0 (FC10 双寄存器)
            QVERIFY(syncWrite(211, {0, 0}, &ok, /*forceMulti*/ true));
            QVERIFY2(ok, "峰值清零 (211-212=0) 被拒");
            QVERIFY(syncWrite(213, {0, 0}, &ok, /*forceMulti*/ true));
            QVERIFY2(ok, "谷值清零 (213-214=0) 被拒");
            QVERIFY(syncRead(211, 4, &rok, &w));
            QVERIFY2(rok && i32(w, 0) == 0 && i32(w, 2) == 0, "峰/谷清零后未归零");
            qInfo() << "[⑥ 标定] 峰/谷清零完成并回读归零";
        }

        // ---- ⑦ 写锁 (117) + FC06 单寄存器写: 滤波系数 (236) 改值→回读→还原 ----
        {
            bool ok = false;
            quint16 lockOrig = 0;
            QVERIFY(syncRead(117, 1, &ok, nullptr));
            QVERIFY(syncWrite(117, {1}, &ok)); // 解锁 (掉电不记忆, 末尾还原)
            QVERIFY2(ok, "写锁置 1 被拒");
            const quint16 alt = (filterCur == 0) ? quint16(1) : quint16(0);
            QVERIFY(syncWrite(236, {alt}, &ok)); // FC06 单写
            QVERIFY2(ok, "滤波系数 FC06 写被拒");
            bool rok = false;
            QVector<quint16> rb;
            QVERIFY(syncRead(236, 1, &rok, &rb));
            QVERIFY2(rok && rb.value(0) == alt, "滤波系数 FC06 写后回读不一致");
            QVERIFY(syncWrite(236, {filterCur}, &ok));
            QVERIFY(syncRead(236, 1, &rok, &rb));
            QVERIFY2(rok && rb.value(0) == filterCur, "滤波系数还原失败");
            QVERIFY(syncWrite(117, {lockOrig}, &ok)); // 写锁还原
            qInfo() << "[⑦ 写路径] 写锁+FC06 单写/回读/还原通过 (滤波系数" << filterCur
                    << "→" << alt << "→" << filterCur << ", 写锁还原" << lockOrig << ")";
        }

        // ---- ⑧ FC10 多寄存器写: 下限值 (260-261, 4B) 改值→回读→还原 ----
        {
            const quint16 lo = limitCur.value(0);
            const quint16 hi = limitCur.value(1);
            const qint32 cur = (qint32(quint32(lo) << 16)) | quint32(hi);
            const qint32 altRaw = (cur == 0) ? qint32(100) : cur - 1;
            const QVector<quint16> alt = { quint16((altRaw >> 16) & 0xFFFF),
                                           quint16(altRaw & 0xFFFF) };
            bool ok = false;
            QVERIFY(syncWrite(260, alt, &ok, /*forceMulti*/ true));
            QVERIFY2(ok, "下限值 FC10 写被拒");
            bool rok = false;
            QVector<quint16> rb;
            QVERIFY(syncRead(260, 2, &rok, &rb));
            QVERIFY2(rok && rb.value(0) == alt[0] && rb.value(1) == alt[1],
                     "下限值 FC10 写后回读不一致");
            QVERIFY(syncWrite(260, {lo, hi}, &ok, true));
            QVERIFY(syncRead(260, 2, &rok, &rb));
            QVERIFY2(rok && rb.value(0) == lo && rb.value(1) == hi, "下限值还原失败");
            qInfo() << "[⑧ 写路径] FC10 多写/回读/还原通过 (下限值" << cur << "→"
                    << altRaw << "→" << cur << ")";
        }

        // ---- ⑨ 多段标定区 (194..200): 可读 + 段号/生效命令寄存器可写回原值 ----
        {
            bool ok = false;
            QVector<quint16> w;
            QVERIFY(syncRead(194, 6, &ok, &w));
            QVERIFY2(ok, "多段标定区 (194..199) 读失败");
            qInfo() << "[⑨ 多段] 段号" << w.value(0) << "段重量" << i32(w, 1)
                    << "段mV" << i32(w, 3) << "生效命令" << w.value(5);
        }

        // ---- ⑩ 通道 2 参数区 (300 + ): 独立参数区按通道平移可读 ----
        {
            bool ok = false;
            QVector<quint16> w;
            QVERIFY(syncRead(300, 5, &ok, &w));
            QVERIFY2(ok, "通道 2 参数区 (300..304) 读失败");
            qInfo() << "[⑩ ch2] 测量状态" << w.value(0) << "小数点" << w.value(1)
                    << "分度值" << w.value(2) << "实时值" << i32(w, 3);
        }

        // ---- ⑪ 异常路径: 越界地址读 → 设备侧异常 (本固件回错 CRC 垃圾帧), 链路保持健康 ----
        {
            bool ok = false;
            QString err;
            QVERIFY(syncRead(60000, 1, &ok, nullptr, &err));
            QVERIFY2(!ok, "越界地址竟读成功");
            const FrameRecord &rx = frames[frames.size() - 1];
            QVERIFY2(!rx.isTx && rx.isErr, "越界读缺少错误 RX 帧记录");
            if (rx.pdu.size() == 2 && (quint8(rx.pdu.at(0)) & 0x80))
                qInfo() << "[⑪ 异常] 越界读被设备以标准异常帧拒绝 (异常码 0x" << Qt::hex
                        << quint8(rx.pdu.at(1)) << ")" << Qt::dec;
            else
                qInfo() << "[⑪ 异常] 越界读: 设备回错 CRC 垃圾帧被 Qt 丢弃 →"
                        << err << "(本固件对未映射地址不作标准异常应答, 记录)";
            QVERIFY(syncRead(0, 1, &ok));
            QVERIFY2(ok, "异常帧后链路不健康");
        }

        // ---- ⑫ 超时路径: 不存在的从站 247 (短超时, 无线上字节占位帧) ----
        {
            mb.setTimeoutMs(300);
            QSignalSpy spy(&mb, &MasterClient::readDone);
            QVERIFY(mb.read(3, 247, 0, 1));
            QVERIFY2(spy.wait(1500), "死从站读未按时返回超时结果");
            const bool ok = spy.first().at(3).toBool();
            const QString err = spy.first().at(5).toString();
            QVERIFY2(!ok && !err.isEmpty(), "死从站读未报错");
            QVERIFY2(frames.size() >= 2, "超时路径缺帧记录");
            const FrameRecord &rx = frames[frames.size() - 1];
            QVERIFY2(!rx.isTx && rx.isErr && rx.pdu.isEmpty(),
                     "超时 RX 帧应为无线上字节占位");
            mb.setTimeoutMs(1000);
            qInfo() << "[⑫ 超时] 死从站超时路径正常:" << err;
        }

        // ---- ⑬ 帧口径: TX/RX 1:1 无重复, RTU 完整 ADU + CRC 自洽 ----
        {
            int nTx = 0;
            int nRx = 0;
            for (int i = 0; i < frames.size(); ++i)
            {
                const FrameRecord &fr = frames[i];
                if (fr.isTx)
                    ++nTx;
                else
                    ++nRx;
                QVERIFY2(fr.isRtu, "RTU 会话出现 TCP 帧");
                // 相邻重复 TX 帧 = 重复接线特征 (同方向同 PDU 连发两条)
                if (i > 0 && fr.isTx && frames[i - 1].isTx
                    && fr.pdu == frames[i - 1].pdu)
                    QFAIL("检出相邻重复 TX 帧 (重复接线)");
            }
            QCOMPARE(nTx, nRx);
            for (const auto &fr : frames)
            {
                if (fr.pdu.isEmpty())
                    continue;
                const QByteArray adu = commRtuAdu(fr.addr, fr.pdu);
                QCOMPARE(int(quint8(adu.at(0))), int(fr.addr));
                const quint16 crc = commCrc16(adu.left(adu.size() - 2));
                QCOMPARE(int(quint8(adu.at(adu.size() - 2))), int(crc & 0xFF));
                QCOMPARE(int(quint8(adu.at(adu.size() - 1))), int(crc >> 8));
            }
            qInfo() << "[⑬ 帧口径]" << frames.size() << "帧 (TX" << nTx << "/RX" << nRx
                    << "), RTU 完整 ADU + CRC 自洽, 无重复帧";
        }

        // 串口断开收尾在槽内落地, 避免拆链时序泄入下一槽 (同 tcpFrameEcho)
        mb.disconnectDevice();
        QTest::qWait(150);
    }

    void realToolUi()
    {
        // ====================================================================
        // 软件 UI 级实机测试 — 以实机为参照, 驱动真正的 ModbusMasterTool 界面:
        //   连接 (act.connect) → 数据窗口定义下发 (setDefinition) → F6 读一次
        //   → 扫描轮询上屏 → 帧日志窗口验证完整 RTU 帧 (地址+CRC, 方向交替,
        //   CRC 复核) → 界面截图存证 (ui_shots/)
        // 设备缺失时跳过; 串口参数环境变量同 realDeviceSmoke
        // ====================================================================
        const QString portName = qEnvironmentVariable("HOSTTOOL_REAL_PORT",
                                                      QStringLiteral("COM30"));
        bool present = false;
        const auto ports = QSerialPortInfo::availablePorts();
        for (const auto &p : ports)
            if (p.portName() == portName)
                present = true;
        if (!present)
            QSKIP(qPrintable(QStringLiteral("实机 %1 未接入, 跳过 UI 级测试")
                                 .arg(portName)));

        // 种连接配置 (在构造工具前; 工具 ctor 即应用)
        QCoreApplication::setOrganizationName(QStringLiteral("tst"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_modbustools"));
        {
            AppSettings::MbToolConn c;
            c.kind = 0;
            c.portName = portName;
            c.baud = 19200;
            c.parity = 0;
            c.stopBits = 2;
            c.timeoutMs = 1000;
            c.retries = 1;
            AppSettings::setMbToolConn(c);
        }

        ModbusMasterTool w;
        w.show();
        auto *mb = w.findChild<MasterClient *>();
        QVERIFY(mb);
        auto *aConn = w.findChild<QAction *>(QStringLiteral("act.connect"));
        auto *aF6 = w.findChild<QAction *>(QStringLiteral("act.f6"));
        auto *aDis = w.findChild<QAction *>(QStringLiteral("act.disconnect"));
        QVERIFY(aConn && aF6 && aDis);

        // ---- ① 帧日志先开 (缓冲从打开时刻起收) ----
        QMetaObject::invokeMethod(&w, "showFrameLog");
        auto *dlg = w.findChild<FrameLogDialog *>();
        QVERIFY(dlg);
        auto *logTable = dlg->findChild<QTableWidget *>();
        QVERIFY(logTable);

        // ---- ② 连接实机 ----
        aConn->trigger();
        QTRY_VERIFY(mb->isConnected());
        // 已连接: [断开]/[读写一次]可用, [连接]置灰 (工具栏状态可视化)
        QVERIFY(!aConn->isEnabled());
        QVERIFY(aDis->isEnabled());
        QVERIFY(aF6->isEnabled());
        qInfo() << "[工具·连接]" << portName << "@19200 连接成功 (act.connect)";

        // ---- ③ 数据窗口: 定义 CH1 参数区 200..214 → F6 读一次 → 上屏 ----
        auto *dv = w.findChild<DataViewWidget *>();
        QVERIFY(dv);
        PollDefinition d;
        d.name = QStringLiteral("CH1 参数区");
        d.fc = 3;
        d.slave = 1;
        d.addr = 200;
        d.qty = 15;
        d.scanRateMs = 200;
        d.enabled = true;
        dv->setDefinition(d);
        aF6->trigger();
        // 等应答上屏: 行 2 = 寄存器 202 分度值, 实机读回应为 1
        QTRY_COMPARE(dv->definition().fc, 3);
        QTRY_VERIFY2(
            dv->findChild<QTableView *>() != nullptr, "数据表格缺失");
        bool got = false;
        const int tries = 50;
        for (int i = 0; i < tries && !got; ++i)
        {
            QTest::qWait(100);
            got = dv->findChild<QTableView *>()
                      ->model()
                      ->index(2, PollTableModel::ColValue)
                      .data(Qt::DisplayRole)
                      .isValid();
        }
        QVERIFY2(got, "F6 读一次后表格无数据上屏");
        qInfo() << "[工具·F6] 数据窗口读一次上屏成功 (200..214, 15 寄存器)";

        // ---- ④ 扫描轮询: 等多拍, 值持续刷新 ----
        QTest::qWait(700);
        qInfo() << "[工具·轮询] 200ms 周期扫描运行中";

        // ---- ⑤ 帧日志: 完整 RTU 帧 (地址+功能码+数据+CRC), 方向交替 ----
        QTRY_VERIFY(logTable->rowCount() >= 4);
        const QString tx = logTable->item(0, 2)->text();
        const QStringList b = tx.split(QLatin1Char(' '));
        QCOMPARE(b.size(), 8); // 01 03 00 C8 00 0F crc crc — 从站地址与 CRC 都在
        QCOMPARE(b.at(0), QStringLiteral("01")); // 从站地址
        QCOMPARE(b.at(1), QStringLiteral("03")); // 功能码
        QCOMPARE(b.at(2), QStringLiteral("00"));
        QCOMPARE(b.at(3), QStringLiteral("C8")); // 地址 200
        QCOMPARE(b.at(4), QStringLiteral("00"));
        QCOMPARE(b.at(5), QStringLiteral("0F")); // 数量 15
        QByteArray shown;
        for (const QString &hex : b)
            shown.append(char(hex.toUShort(nullptr, 16)));
        const quint16 crc = commCrc16(shown.left(6));
        QCOMPARE(int(quint8(shown.at(6))), int(crc & 0xFF));
        QCOMPARE(int(quint8(shown.at(7))), int(crc >> 8));
        QCOMPARE(logTable->item(0, 1)->text(), QStringLiteral("TX"));
        for (int i = 1; i < logTable->rowCount(); ++i)
            QVERIFY2(logTable->item(i, 1)->text()
                         != logTable->item(i - 1, 1)->text(),
                     "帧日志方向未交替 (存在重复 TX/RX)");
        qInfo() << "[工具·帧日志] 完整 RTU 帧展示正确:" << tx;

        // ---- ⑥ 截图存证 (帧日志 + 工具主窗) ----
        const QString shotDir =
                QCoreApplication::applicationDirPath() + QStringLiteral("/ui_shots");
        QDir().mkpath(shotDir);
        QVERIFY(dlg->grab().save(shotDir + QStringLiteral("/framelog_rtu.png")));
        QVERIFY(w.grab().save(shotDir + QStringLiteral("/tool_main.png")));
        qInfo() << "[工具·截图] 已保存:" << shotDir;

        // ---- ⑦ 断开收尾 (槽内落地拆链时序) ----
        aDis->trigger();
        QTest::qWait(250);
    }

    void modelNameEdit()
    {
        PollDefinition d;
        d.fc = 3;
        d.addr = 100;
        d.qty = 2;
        PollTableModel m;
        m.setDefinition(d);
        // 别名列可直接行内重命名 (对齐 Modbus Poll Name Cells)
        QVERIFY(m.flags(m.index(0, PollTableModel::ColName)) & Qt::ItemIsEditable);
        QSignalSpy spy(&m, &PollTableModel::nameEdited);
        QVERIFY(m.setData(m.index(0, PollTableModel::ColName), QStringLiteral("温度"),
                          Qt::EditRole));
        QCOMPARE(m.data(m.index(0, PollTableModel::ColName), Qt::DisplayRole).toString(),
                 QStringLiteral("温度"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 0);
        QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("温度"));
        // 第二行: 跳行命名 (nameCells 自动补位)
        QVERIFY(m.setData(m.index(1, PollTableModel::ColName), QStringLiteral("压力"),
                          Qt::EditRole));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(m.data(m.index(0, PollTableModel::ColName), Qt::DisplayRole).toString(),
                 QStringLiteral("温度"));
    }

    void testCenterMdiCycle()
    {
        // Test Center 是 QMdiSubWindow 单例 (关闭仅隐藏, 重开复用) —
        // 回归: 关闭→重开 / 最大化→关闭→重开 后内容几何被破坏 (用户报"内容空白")
        QCoreApplication::setOrganizationName(QStringLiteral("tst"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_modbustools"));
        ModbusMasterTool w;
        auto *mdi = w.findChild<QMdiArea *>();
        QVERIFY(mdi);
        QAction *tcAct = nullptr; // Test Center 菜单项 (无 objectName, 按文本定位)
        for (QAction *ma : w.menuBar()->actions())
            if (QMenu *m = ma->menu())
                for (QAction *a : m->actions())
                    if (a->text() == QStringLiteral("Test Center"))
                        tcAct = a;
        QVERIFY(tcAct);
        tcAct->trigger();
        qApp->processEvents();
        QCOMPARE(mdi->subWindowList().size(), 2); // 启动窗口 + Test Center
        QMdiSubWindow *sub = mdi->subWindowList().last();
        auto *tc = w.findChild<TestCenterPage *>();
        QVERIFY(tc && sub->widget() == tc);
        auto checkContent = [tc, sub]() -> QString {
            if (sub->isHidden())
                return QStringLiteral("subwindow hidden");
            if (tc->isHidden())
                return QStringLiteral("page hidden");
            if (tc->width() < 200 || tc->height() < 200)
                return QStringLiteral("page %1x%2").arg(tc->width()).arg(tc->height());
            auto *wire = tc->findChild<QPlainTextEdit *>(QStringLiteral("tc.wireEdit"));
            if (!wire || wire->isHidden() || wire->height() < 20)
                return QStringLiteral("wireEdit gone/small");
            return QString();
        };
        QVERIFY(checkContent().isEmpty());

        // 路径 A: 关闭 (仅隐藏) → 重开
        // (QMdiSubWindow 关闭会对内容页显式 hide — 重开时不恢复即为空白窗体)
        sub->close();
        qApp->processEvents();
        tcAct->trigger();
        qApp->processEvents();
        QVERIFY2(checkContent().isEmpty(), qPrintable(checkContent()));

        // 路径 B: 最大化 → 关闭 → 重开 (经典空白触发路径)
        sub->showMaximized();
        qApp->processEvents();
        sub->close();
        qApp->processEvents();
        tcAct->trigger();
        qApp->processEvents();
        QVERIFY2(checkContent().isEmpty(), qPrintable(checkContent()));

        // 路径 C: 最大化态直接重开动作 (重复触发不崩/不破坏)
        tcAct->trigger();
        qApp->processEvents();
        QVERIFY2(checkContent().isEmpty(), qPrintable(checkContent()));
    }

    void toolSmoke()
    {
        // connectDevice 在无连接配置时会弹模态连接设置 (引导), 同步失败再弹警告框;
        // 无头冒烟先种一个立即被拒的 TCP 目标, 走异步失败路径 (只改状态栏文字)
        // 测试进程未设组织/应用名, QSettings 默认落点无效 — 显式指定, 种入才生效
        QCoreApplication::setOrganizationName(QStringLiteral("tst"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_modbustools"));
        {
            AppSettings::MbToolConn c;
            c.kind = 1;
            c.host = QStringLiteral("127.0.0.1");
            c.tcpPort = 9; // discard 端口, 本机无监听 → 连接拒绝
            c.timeoutMs = 200;
            AppSettings::setMbToolConn(c);
        }
        ModbusMasterTool w;
        auto *mdi = w.findChild<QMdiArea *>();
        QVERIFY(mdi);
        QCOMPARE(mdi->subWindowList().size(), 1); // 启动即有一个数据窗口
        auto *aNew = w.findChild<QAction *>(QStringLiteral("act.newWindow"));
        auto *aF6 = w.findChild<QAction *>(QStringLiteral("act.f6"));
        auto *aPause = w.findChild<QAction *>(QStringLiteral("act.pause"));
        auto *aConn = w.findChild<QAction *>(QStringLiteral("act.connect"));
        auto *aDis = w.findChild<QAction *>(QStringLiteral("act.disconnect"));
        QVERIFY(aNew && aF6 && aPause && aConn && aDis);
        // 初始 (未连接): 仅[连接]可用; 断开/读写一次/扫描暂停置灰
        QVERIFY(aConn->isEnabled());
        QVERIFY(!aDis->isEnabled());
        QVERIFY(!aF6->isEnabled());
        QVERIFY(!aPause->isEnabled());
        aNew->trigger();
        aNew->trigger();
        QCOMPARE(mdi->subWindowList().size(), 3);
        // 关闭活动数据窗口 = 删除定义
        QMdiSubWindow *act = mdi->activeSubWindow();
        QVERIFY(act);
        act->close();
        qApp->processEvents();
        QCOMPARE(mdi->subWindowList().size(), 2);
        // 窗口菜单反复开合 (回归: 动态项回收曾用 at(4) 越界 → 野指针崩溃/死循环)
        QMenu *winMenu = nullptr;
        for (QAction *ma : w.menuBar()->actions())
            if (QMenu *m = ma->menu(); m && m->title().contains(QStringLiteral("窗口")))
                winMenu = m;
        QVERIFY(winMenu);
        for (int round = 0; round < 5; ++round)
        {
            winMenu->popup(QPoint(50, 50)); // popup 触发 aboutToShow 重建
            qApp->processEvents();
            QCOMPARE(winMenu->actions().size(),
                     3 + mdi->subWindowList().size()); // 3 固定项 + 每窗口一项
            winMenu->hide();
            qApp->processEvents();
        }
        // 扫描暂停/恢复 + 连接失败路径 (无串口环境) 不崩溃
        aPause->setChecked(true);
        aPause->setChecked(false);
        // 兜底: 冒烟中若仍弹模态 (同步失败警告等), 定时关闭保持无人值守
        for (int i = 1; i <= 3; ++i)
            QTimer::singleShot(50 * i, qApp, [] {
                if (QWidget *m = QApplication::activeModalWidget())
                    m->close();
            });
        aConn->trigger();
        // 任意状态 [连接]/[断开] 不允许同时置灰: 连接在途时 [断开] 即取消通道
        QVERIFY(aConn->isEnabled() || aDis->isEnabled());
        // 异步失败 (127.0.0.1:9 拒绝) 收敛后 [连接] 必须恢复可用 —
        // 回归: 曾因 updateConnectUi 先于 m_connecting 复位而永久置灰
        // (拒绝收敛受 TCP 栈/内部重连影响可至数秒, 放宽等待上限)
        QTRY_VERIFY_WITH_TIMEOUT(aConn->isEnabled(), 15000);
        QVERIFY(!aDis->isEnabled()); // 未连接且不在途: 仅 [断开] 置灰
        // 连接中窗体: 尝试发起过 (惰性创建), 失败收敛后已关闭;
        // 状态栏右下角连接参数窗格: 未连接不显示
        auto *connDlg = w.findChild<QProgressDialog *>(QStringLiteral("master.connDlg"));
        QVERIFY(connDlg);
        QVERIFY(!connDlg->isVisible());
        auto *connPane = w.findChild<QLabel *>(QStringLiteral("master.connPane"));
        QVERIFY(connPane);
        QVERIFY(!connPane->isVisible());
        aDis->trigger();
        aF6->trigger(); // 未连接: 状态栏提示
        qApp->processEvents();
    }

    void workspaceRoundtrip()
    {
        // v3: 多连接 + 从机列表 + 活动连接 + defs 归属 (connectionId 持久化)
        ModbusWorkspace::Data w;
        {
            ModbusWorkspace::ConnEntry c1;
            c1.id = QStringLiteral("c1");
            c1.params.kind = 0;
            c1.params.portName = QStringLiteral("COM3");
            c1.params.baud = 9600;
            c1.slaves = {1, 3, 5};
            w.conns.append(c1);
            ModbusWorkspace::ConnEntry c2;
            c2.id = QStringLiteral("f2ab01cd");
            c2.params.kind = 1;
            c2.params.host = QStringLiteral("10.0.0.5");
            c2.params.tcpPort = 1502;
            c2.params.timeoutMs = 500;
            c2.params.retries = 1;
            w.conns.append(c2);
            w.activeId = c2.id;
        }
        PollDefinition d;
        d.name = QStringLiteral("压力");
        d.connectionId = QStringLiteral("f2ab01cd");
        d.slave = 5;
        d.fc = 4;
        d.addr = 100;
        d.qty = 8;
        d.scanRateMs = 250;
        d.enabled = false;
        d.fmt = ModbusFormat::Fmt::F32;
        d.order = ModbusFormat::ByteOrder::CDAB;
        w.defs.append(d);
        const QByteArray json = ModbusWorkspace::toJson(w);
        ModbusWorkspace::Data r;
        QVERIFY(ModbusWorkspace::fromJson(json, &r));
        QCOMPARE(r.conns.size(), 2);
        QCOMPARE(r.conns.at(0).id, QStringLiteral("c1"));
        QCOMPARE(r.conns.at(0).params.kind, 0);
        QCOMPARE(r.conns.at(0).params.portName, QStringLiteral("COM3"));
        QCOMPARE(r.conns.at(0).params.baud, 9600);
        QCOMPARE(r.conns.at(0).slaves, QList<int>({1, 3, 5}));
        QCOMPARE(r.conns.at(1).id, QStringLiteral("f2ab01cd"));
        QCOMPARE(r.conns.at(1).params.host, QStringLiteral("10.0.0.5"));
        QCOMPARE(r.conns.at(1).params.tcpPort, 1502);
        QCOMPARE(r.conns.at(1).params.timeoutMs, 500);
        QCOMPARE(r.conns.at(1).params.retries, 1);
        QCOMPARE(r.activeId, QStringLiteral("f2ab01cd"));
        QCOMPARE(r.defs.size(), 1);
        QCOMPARE(r.defs.first().name, QStringLiteral("压力"));
        QCOMPARE(r.defs.first().connectionId, QStringLiteral("f2ab01cd"));
        QCOMPARE(r.defs.first().fc, 4);
        QVERIFY(r.defs.first().fmt == ModbusFormat::Fmt::F32);
        QVERIFY(r.defs.first().order == ModbusFormat::ByteOrder::CDAB);
        // 非法/空输入拒绝
        QVERIFY(!ModbusWorkspace::fromJson(QByteArray("not json"), &r));
        QVERIFY(!ModbusWorkspace::fromJson(QByteArray(), &r));
        // 垃圾字段落默认
        QVERIFY(r.conns.isEmpty());

        // v2 旧工作区 (单 conn 对象, defs 无 connId) → 包装为 conns[0],
        // 载入方按"空 id = 首个连接"绑定, 行为与单连接时代一致
        const QByteArray v2json =
            QStringLiteral(R"({
                "app": "ST072G-ModbusTools", "version": 2,
                "conn": { "kind": 1, "port": "ttyUSB0", "baud": 9600,
                          "parity": 2, "stop": 2, "host": "10.0.0.5",
                          "tcpPort": 1502, "timeout": 500, "retries": 1 },
                "defs": [ { "name": "压力", "slave": 5, "fc": 4, "addr": 100,
                            "qty": 8, "rateMs": 250, "enabled": false } ]
            })").toUtf8();
        ModbusWorkspace::Data v2;
        QVERIFY(ModbusWorkspace::fromJson(v2json, &v2));
        QCOMPARE(v2.conns.size(), 1);
        QCOMPARE(v2.conns.first().params.kind, 1);
        QCOMPARE(v2.conns.first().params.portName, QStringLiteral("ttyUSB0"));
        QCOMPARE(v2.conns.first().params.host, QStringLiteral("10.0.0.5"));
        QVERIFY(v2.conns.first().slaves.isEmpty());
        QCOMPARE(v2.defs.size(), 1);
        QVERIFY(v2.defs.first().connectionId.isEmpty()); // 空 = 绑首个连接
        QCOMPARE(v2.defs.first().slave, 5);
    }

    // ====================================================================
    // 多机连接: 从机地址解析 / 连接注册表 / 从机在线轮询 / 设备树 / 双连接回环
    // ====================================================================
    void slaveSetupParse()
    {
        // 解析: 逗号/中文逗号/分号/空格分隔 + a-b 范围 + 升序去重
        QList<int> out;
        QVERIFY(SlaveSetupDialog::parseAddresses(QStringLiteral("1,3,5-8"), &out));
        QCOMPARE(out, QList<int>({1, 3, 5, 6, 7, 8}));
        QVERIFY(SlaveSetupDialog::parseAddresses(QStringLiteral("5, 1；3"), &out));
        QCOMPARE(out, QList<int>({1, 3, 5}));
        QVERIFY(SlaveSetupDialog::parseAddresses(QStringLiteral("8-5"), &out)); // 倒序范围归正
        QCOMPARE(out, QList<int>({5, 6, 7, 8}));
        QVERIFY(SlaveSetupDialog::parseAddresses(QStringLiteral("300, 0"), &out)); // 钳制 1..247
        QCOMPARE(out, QList<int>({1, 247})); // 升序输出
        QVERIFY(!SlaveSetupDialog::parseAddresses(QStringLiteral("abc"), &out)); // 非数字
        QVERIFY(!SlaveSetupDialog::parseAddresses(QStringLiteral("1-2-3"), &out)); // 双杠
        QVERIFY(!SlaveSetupDialog::parseAddresses(QString(), &out)); // 空
        // 回显: 连续段压缩
        QCOMPARE(SlaveSetupDialog::formatAddresses({1, 3, 5, 6, 7, 8}),
                 QStringLiteral("1,3,5-8"));
        QCOMPARE(SlaveSetupDialog::formatAddresses({2}), QStringLiteral("2"));
    }

    void connectionManagerBasics()
    {
        // 增删/活动移交/重复串口拒绝/标签/从机列表 + clientAdded 信号
        ConnectionManager mgr;
        QSignalSpy addedSpy(&mgr, &ConnectionManager::clientAdded);
        QSignalSpy listSpy(&mgr, &ConnectionManager::listChanged);
        QSignalSpy activeSpy(&mgr, &ConnectionManager::activeChanged);

        AppSettings::MbToolConn serial;
        serial.kind = 0;
        serial.portName = QStringLiteral("COM3");
        const QString id1 = mgr.addConnection(serial);
        QVERIFY(!id1.isEmpty());
        QCOMPARE(addedSpy.size(), 1);
        QCOMPARE(listSpy.size(), 1);
        QCOMPARE(mgr.activeId(), id1); // 首个连接自动成为活动
        QCOMPARE(mgr.label(id1), QStringLiteral("COM3"));
        QVERIFY(mgr.client(id1) && mgr.poller(id1));

        // 同名串口拒绝 (Windows 独占); TCP 重复 host:port 允许
        QVERIFY(mgr.addConnection(serial).isEmpty());
        QCOMPARE(mgr.count(), 1);
        AppSettings::MbToolConn tcp2;
        tcp2.kind = 1;
        tcp2.host = QStringLiteral("10.0.0.5");
        tcp2.tcpPort = 502;
        const QString id2 = mgr.addConnection(tcp2, QStringLiteral("forced99"));
        QVERIFY(!id2.isEmpty());
        QCOMPARE(mgr.count(), 2);
        QCOMPARE(mgr.label(id2), QStringLiteral("10.0.0.5:502"));

        // 活动切换
        mgr.setActiveId(id2);
        QCOMPARE(mgr.activeId(), id2);
        QCOMPARE(activeSpy.size(), 2); // 首个自动活动 + 手动切换
        mgr.setActiveId(QStringLiteral("nope")); // 无效 id 忽略
        QCOMPARE(mgr.activeId(), id2);

        // 从机列表: 同步进 poller + 持久化读取
        QSignalSpy slavesSpy(&mgr, &ConnectionManager::slavesChanged);
        mgr.setSlaves(id1, {3, 1, 2, 2});
        QCOMPARE(mgr.slaves(id1), QList<int>({1, 2, 3}));
        QCOMPARE(mgr.poller(id1)->addresses(), QList<int>({1, 2, 3}));
        QCOMPARE(slavesSpy.size(), 1);

        // 参数编辑: 改串口名与其他连接冲突拒绝, 正常修改生效
        AppSettings::MbToolConn clash = tcp2;
        clash.kind = 0;
        clash.portName = QStringLiteral("COM3");
        QVERIFY(!mgr.updateConnection(id2, clash));
        AppSettings::MbToolConn renamed = serial;
        renamed.baud = 19200;
        QVERIFY(mgr.updateConnection(id1, renamed));
        QCOMPARE(mgr.entry(id1).params.baud, 19200);

        // 移除活动连接 → 活动移交剩余首个
        mgr.removeConnection(id2);
        QCOMPARE(mgr.count(), 1);
        QCOMPARE(mgr.activeId(), id1);
        QVERIFY(!mgr.client(id2)); // 已销毁句柄查空
        mgr.removeConnection(id1);
        QCOMPARE(mgr.count(), 0);
        QVERIFY(mgr.activeId().isEmpty()); // 全部移除 → 活动清空
    }

    void slavePollerLoopback()
    {
        // 从机轮转探测: 依地址 1,2,3 顺序轮询; 在线判定 (应答=在线);
        // 不应答地址判离线; 断线全离线; 暂停挂起
        QTcpServer srv;
        QList<QByteArray> reqAdus;
        startF03LoopbackSlave(&srv, &reqAdus);

        MasterClient mb;
        mb.setKind(MasterClient::Kind::Tcp);
        mb.setTcpParams(QStringLiteral("127.0.0.1"), srv.serverPort());
        mb.setTimeoutMs(150);
        mb.setRetries(0);
        QVERIFY(mb.connectDevice());
        QTRY_VERIFY(mb.isConnected());

        SlavePoller poller(&mb);
        QSignalSpy onlineSpy(&poller, &SlavePoller::slaveOnlineChanged);
        poller.setIntervalMs(30);
        poller.setAddresses({1, 2, 3});
        // 三台全在线 (回环从机应答任意单元)
        QTRY_COMPARE_WITH_TIMEOUT(onlineSpy.count(), 3, 5000);
        QVERIFY(poller.isOnline(1) && poller.isOnline(2) && poller.isOnline(3));
        // 轮转顺序: 前三发按单元 1,2,3 依序 (地址=MBAP 第 7 字节)
        QTRY_VERIFY(reqAdus.size() >= 3);
        QCOMPARE(int(quint8(reqAdus.at(0).at(6))), 1);
        QCOMPARE(int(quint8(reqAdus.at(1).at(6))), 2);
        QCOMPARE(int(quint8(reqAdus.at(2).at(6))), 3);
        // 探测请求 = FC03 读地址 0 ×1
        QCOMPARE(reqAdus.at(0).mid(7), QByteArray::fromHex("0300000001"));

        // 暂停: 先等在途探测落地, 再确认不再产生新探测
        poller.setPaused(true);
        QTest::qWait(80);
        const int frozen = reqAdus.size();
        QTest::qWait(250);
        QCOMPARE(reqAdus.size(), frozen);
        poller.setPaused(false);

        // 断线 → 全部从机转离线
        mb.disconnectDevice();
        QTRY_VERIFY(onlineSpy.count() >= 6); // 3 上线 + 3 下线
        QVERIFY(!poller.isOnline(1) && !poller.isOnline(2) && !poller.isOnline(3));
        QTest::qWait(150); // 消化 socket 收尾
    }

    void slavePollerOfflineDetect()
    {
        // 无应答地址判离线: 回环从机只应答单元 1/3, 单元 2 静默
        QTcpServer srv;
        QVERIFY(srv.listen(QHostAddress::LocalHost));
        QObject::connect(&srv, &QTcpServer::newConnection, &srv, [&srv] {
            while (QTcpSocket *c = srv.nextPendingConnection())
            {
                QObject::connect(c, &QTcpSocket::readyRead, c, [c] {
                    QByteArray buf = c->property("rx").toByteArray();
                    buf += c->readAll();
                    forever
                    {
                        if (buf.size() < 7)
                            break;
                        const int len = (quint8(buf.at(4)) << 8) | quint8(buf.at(5));
                        if (buf.size() < 6 + len)
                            break;
                        const QByteArray adu = buf.left(6 + len);
                        buf.remove(0, 6 + len);
                        const int unit = quint8(adu.at(6));
                        if (unit != 1 && unit != 3)
                            continue; // 单元 2: 静默 (超时判离线)
                        const QByteArray pdu = adu.mid(7);
                        if (quint8(pdu.at(0)) != 3 || pdu.size() != 5)
                            continue;
                        QByteArray respPdu;
                        respPdu.append(char(0x03)).append(char(2)).append('\x00').append('\x07');
                        QByteArray resp = adu.left(2);
                        resp.append('\x00').append('\x00')
                            .append(char((respPdu.size() + 1) >> 8))
                            .append(char((respPdu.size() + 1) & 0xFF))
                            .append(adu.mid(6, 1));
                        resp += respPdu;
                        c->write(resp);
                    }
                    c->setProperty("rx", buf);
                });
            }
        });

        MasterClient mb;
        mb.setKind(MasterClient::Kind::Tcp);
        mb.setTcpParams(QStringLiteral("127.0.0.1"), srv.serverPort());
        mb.setTimeoutMs(150);
        mb.setRetries(0);
        QVERIFY(mb.connectDevice());
        QTRY_VERIFY(mb.isConnected());

        SlavePoller poller(&mb);
        poller.setIntervalMs(30);
        poller.setAddresses({1, 2, 3});
        QTRY_VERIFY_WITH_TIMEOUT(poller.isOnline(1) && poller.isOnline(3), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!poller.isOnline(2), 5000); // 超时后判离线
        mb.disconnectDevice();
        QTest::qWait(150);
    }

    void deviceTreeSmoke()
    {
        // 树装配: 连接节点/从机子节点/信息列; 状态点经 stateChanged 更新;
        // 选中从机 → selectedSlaveAddress; TCP 不挂从机节点 (refresh 层面)
        ConnectionManager mgr;
        AppSettings::MbToolConn serial;
        serial.kind = 0;
        serial.portName = QStringLiteral("COM3");
        serial.baud = 115200;
        const QString id1 = mgr.addConnection(serial);
        AppSettings::MbToolConn tcp;
        tcp.kind = 1;
        tcp.host = QStringLiteral("192.168.1.10");
        const QString id2 = mgr.addConnection(tcp);
        mgr.setSlaves(id1, {1, 5, 9});
        DeviceTreeWidget tree(&mgr);
        tree.resize(300, 400);
        QCOMPARE(tree.topLevelItemCount(), 2);
        QTreeWidgetItem *n1 = tree.topLevelItem(0);
        QVERIFY(n1);
        QCOMPARE(n1->text(0), QStringLiteral("COM3"));
        QVERIFY(n1->text(1).contains(QStringLiteral("115200")));
        QCOMPARE(n1->childCount(), 3); // 从机子节点
        QCOMPARE(n1->child(1)->text(0), QStringLiteral("从机 5"));
        QVERIFY(!n1->icon(0).isNull()); // 状态点 (红, 未连接)
        QTreeWidgetItem *n2 = tree.topLevelItem(1);
        QCOMPARE(n2->text(0), QStringLiteral("192.168.1.10:502"));
        QCOMPARE(n2->childCount(), 0); // TCP 无从机节点
        // 活动连接加粗 (id1 为活动)
        QVERIFY(n1->font(0).bold());
        QVERIFY(!n2->font(0).bold());
        // 选中从机 → 查询接口
        tree.setCurrentItem(n1->child(2));
        QCOMPARE(tree.selectedConnectionId(), id1);
        QCOMPARE(tree.selectedSlaveAddress(), 9);
        tree.setCurrentItem(n2);
        QCOMPARE(tree.selectedSlaveAddress(), -1);
        // 点击从机 → activePick (所属连接)
        QSignalSpy pickSpy(&tree, &DeviceTreeWidget::activePickRequested);
        emit tree.itemClicked(n1->child(0), 0);
        QCOMPARE(pickSpy.count(), 1);
        QCOMPARE(pickSpy.first().first().toString(), id1);
        // 双击从机 → slaveActivated
        QSignalSpy actSpy(&tree, &DeviceTreeWidget::slaveActivated);
        emit tree.itemDoubleClicked(n1->child(0), 0);
        QCOMPARE(actSpy.count(), 1);
        QCOMPARE(actSpy.first().at(1).toInt(), 1);
        // 空白处右键入口信号 (菜单 exec 模态, 冒烟只验信号契约)
        QSignalSpy addSerialSpy(&tree, &DeviceTreeWidget::addSerialRequested);
        QSignalSpy addTcpSpy(&tree, &DeviceTreeWidget::addTcpRequested);
        emit tree.addSerialRequested();
        emit tree.addTcpRequested();
        QCOMPARE(addSerialSpy.count(), 1);
        QCOMPARE(addTcpSpy.count(), 1);
        // 从机在线/探测结果更新 (manager 转发路径)
        mgr.poller(id1)->setAddresses({1, 5, 9});
        emit mgr.slaveOnlineChanged(id1, 5, true); // 直接驱动转发信号
        qApp->processEvents();
        mgr.setSlaves(id1, {1, 5, 9}); // 触发 refresh 重建后状态保持
        qApp->processEvents();
    }

    void multiConnectionLoopback()
    {
        // 双连接并存: 两个回环 server, manager 两台 TCP 连接, 两个数据窗口
        // 各绑各的连接同时轮询 — 互不串扰 (client 信号按实例隔离)
        QTcpServer srvA, srvB;
        QList<QByteArray> adusA, adusB;
        startF03LoopbackSlave(&srvA, &adusA);
        startF03LoopbackSlave(&srvB, &adusB);

        ConnectionManager mgr;
        AppSettings::MbToolConn pa;
        pa.kind = 1;
        pa.host = QStringLiteral("127.0.0.1");
        pa.tcpPort = srvA.serverPort();
        pa.timeoutMs = 1000;
        const QString idA = mgr.addConnection(pa);
        AppSettings::MbToolConn pb = pa;
        pb.tcpPort = srvB.serverPort();
        const QString idB = mgr.addConnection(pb);
        QVERIFY(!idA.isEmpty() && !idB.isEmpty() && idA != idB);
        QVERIFY(mgr.connectOne(idA));
        QVERIFY(mgr.connectOne(idB));
        QTRY_VERIFY(mgr.client(idA)->isConnected());
        QTRY_VERIFY(mgr.client(idB)->isConnected());

        // 两个数据窗口各绑各的连接 (手动触发一次读取)
        DataViewWidget wA(mgr.client(idA));
        PollDefinition dA;
        dA.fc = 3;
        dA.scanRateMs = 0;
        dA.addr = 100;
        dA.qty = 2;
        wA.setDefinition(dA);
        DataViewWidget wB(mgr.client(idB));
        PollDefinition dB;
        dB.fc = 3;
        dB.scanRateMs = 0;
        dB.addr = 200;
        dB.qty = 2;
        wB.setDefinition(dB);
        QSignalSpy spyA(&wA, &DataViewWidget::statusMessage);
        QSignalSpy spyB(&wB, &DataViewWidget::statusMessage);
        wA.readWriteOnce();
        wB.readWriteOnce();
        QTRY_COMPARE(spyA.size(), 1);
        QTRY_COMPARE(spyB.size(), 1);
        QCOMPARE(spyA.first().at(1).toBool(), true);
        QCOMPARE(spyB.first().at(1).toBool(), true);
        // 各自读各自的目标段 (A→100, B→200), 请求落在各自 server
        QCOMPARE(adusA.size(), 1);
        QCOMPARE(adusA.at(0).mid(7), QByteArray::fromHex("0300640002"));
        QCOMPARE(adusB.size(), 1);
        QCOMPARE(adusB.at(0).mid(7), QByteArray::fromHex("0300C80002"));

        // stateChanged 按连接 id 区分
        QSignalSpy stateSpy(&mgr, &ConnectionManager::stateChanged);
        mgr.disconnectOne(idA);
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                for (const auto &args : stateSpy)
                    if (args.at(0).toString() == idA && args.at(1).toBool() == false)
                        return true;
                return false;
            }(),
            5000);
        QVERIFY(mgr.client(idB)->isConnected()); // B 不受影响

        mgr.disconnectOne(idB);
        QTest::qWait(200); // 消化收尾
    }
};

QTEST_MAIN(Tmm)
#include "tst_modbustools.moc"
