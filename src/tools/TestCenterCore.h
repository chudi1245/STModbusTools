#pragma once

// ============================================================================
// TestCenterCore.h — Test Center 纯逻辑 (帧构造/十进制转换/hex 解析/响应解析)
//   语义 = 旧 ModbusGenTool 全部生成校验 (kFcTable/字节数拦截/FF00 语义/
//   二补码展开/IEEE754/线圈打包) + Modbus Poll Test Center 的原始串收发;
//   「线级字节」= 从站地址+PDU (RTU ADU 去 CRC / TCP ADU 去 MBAP 后的共同体);
//   结构化生成直接填完整帧进发送框 (RTU 含 CRC16), stripForSend 发送/预览统一剥离
//   (兼容裸线级字节与完整帧两种输入), 离线预览用 ToolFrames::rtuFrame/tcpFrame 包回完整 ADU
// ============================================================================

#include "ToolFrames.h"

#include <QByteArray>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstring>
#include <limits>

namespace TestCenter {

// ---- 功能码表 (01-10 结构化生成 + 17 读/写多寄存器) ----
struct FcInfo
{
    quint8 code;
    const char *label;
    const char *desc;
};
inline const FcInfo kFcTable[] = {
    {0x01, "01 读线圈 (Read Coils)", "读取从机的线圈状态，每个线圈一个布尔值"},
    {0x02, "02 读离散输入 (Read Discrete Inputs)", "读取从机的离散量输入，通常用于开关量输入"},
    {0x03, "03 读保持寄存器 (Read Holding Registers)", "读取保持寄存器数据，常用于参数和配置"},
    {0x04, "04 读输入寄存器 (Read Input Registers)", "读取输入寄存器数据，通常为模拟量输入"},
    {0x05, "05 写单线圈 (Write Single Coil)", "写入单个线圈，FF00=ON，0000=OFF"},
    {0x06, "06 写单寄存器 (Write Single Register)", "写入单个保持寄存器的值"},
    {0x0F, "0F 写多线圈 (Write Multiple Coils)", "批量写入多个线圈状态"},
    {0x10, "10 写多寄存器 (Write Multiple Registers)", "批量写入多个保持寄存器的值"},
    {0x17, "17 读/写多寄存器 (Read/Write Multiple Registers)",
     "一次事务先写后读保持寄存器 (原子化参数更新+回读)"},
};

inline bool isWriteFc(quint8 fc)
{
    return fc == 0x05 || fc == 0x06 || fc == 0x0F || fc == 0x10;
}
inline bool isBitFc(quint8 fc)
{
    return fc == 0x01 || fc == 0x02 || fc == 0x05 || fc == 0x0F;
}

// ---- 结构化生成: 参数 → 线级字节 (从站+fc+data); err 非空 = 参数被拒绝 ----
struct BuildSpec
{
    int slave = 1;
    quint8 fc = 0x03;
    int addr = 0;
    int qty = 10;       // 读数量 / 单写值 (05/06) / 多写数量 (0F/10)
    QString writeHex;   // 0F/10/17 写数据 (hex 字节, 任意空白分隔)
    int addr2 = 0;      // 17: 写起始地址
    int qty2 = 0;       // 17: 写数量
};

inline QByteArray buildWire(const BuildSpec &s, QString *err)
{
    auto fail = [err](const QString &m) {
        if (err)
            *err = m;
        return QByteArray();
    };
    if (s.slave < 1 || s.slave > 247)
        return fail(QStringLiteral("从机地址必须在 1-247 范围内"));
    if (s.addr < 0 || s.addr > 65535)
        return fail(QStringLiteral("起始地址必须在 0-65535 范围内"));

    QByteArray p;
    p.append(char(s.slave));
    p.append(char(s.fc));
    p.append(char((s.addr >> 8) & 0xFF));
    p.append(char(s.addr & 0xFF));

    if (s.fc == 0x05 || s.fc == 0x06)
    {
        // 单写: qty 承载值语义 (05 线圈 FF00(65280)=ON/其余 OFF; 06 寄存器原值)
        if (s.qty < 0 || s.qty > 65535)
            return fail(QStringLiteral("写入值必须是 0-65535"));
        const int coil = (s.fc == 0x05) ? (s.qty == 0xFF00 ? 0xFF00 : 0x0000) : s.qty;
        p.append(char((coil >> 8) & 0xFF));
        p.append(char(coil & 0xFF));
        return p;
    }

    if (s.fc == 0x0F || s.fc == 0x10)
    {
        const int maxQty = isBitFc(s.fc) ? 1968 : 123;
        if (s.qty < 1 || s.qty > maxQty)
            return fail(QStringLiteral("数量必须在 1-%1 范围内").arg(maxQty));
        const QStringList parts =
            s.writeHex.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.isEmpty())
            return fail(QStringLiteral("请输入要写入的数据"));
        QByteArray data;
        for (const QString &w : parts)
        {
            bool hexOk = false;
            const uint b = w.toUInt(&hexOk, 16);
            if (!hexOk || b > 255 || w.size() % 2 != 0)
                return fail(QStringLiteral("写入数据格式错误，请输入有效的十六进制字节"));
            data.append(char(b));
        }
        // Modbus 规范: FC10 字节数 = 数量×2, FC0F = 数量/8 向上取整; 不匹配从机丢弃
        const int need = isBitFc(s.fc) ? (s.qty + 7) / 8 : s.qty * 2;
        if (data.size() != need)
            return fail(QStringLiteral("写入数据为 %1 字节，与数量 %2 不符 (功能码 0x%3 需要 %4 字节)")
                            .arg(data.size())
                            .arg(s.qty)
                            .arg(s.fc, 2, 16, QLatin1Char('0'))
                            .arg(need));
        p.append(char((s.qty >> 8) & 0xFF));
        p.append(char(s.qty & 0xFF));
        p.append(char(data.size() & 0xFF));
        p.append(data);
        return p;
    }

    if (s.fc == 0x17)
    {
        // 读/写多寄存器: [17][读地址][读数量][写地址][写数量][字节数][写数据]
        if (s.qty < 1 || s.qty > 125)
            return fail(QStringLiteral("读数量必须在 1-125 范围内"));
        if (s.qty2 < 1 || s.qty2 > 121)
            return fail(QStringLiteral("写数量必须在 1-121 范围内"));
        if (s.addr2 < 0 || s.addr2 > 65535)
            return fail(QStringLiteral("写地址必须在 0-65535 范围内"));
        const QStringList parts =
            s.writeHex.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        QByteArray data;
        for (const QString &w : parts)
        {
            bool hexOk = false;
            const uint b = w.toUInt(&hexOk, 16);
            if (!hexOk || b > 255 || w.size() % 2 != 0)
                return fail(QStringLiteral("写入数据格式错误，请输入有效的十六进制字节"));
            data.append(char(b));
        }
        if (data.size() != s.qty2 * 2)
            return fail(QStringLiteral("写入数据为 %1 字节，写数量 %2 需要 %3 字节")
                            .arg(data.size())
                            .arg(s.qty2)
                            .arg(s.qty2 * 2));
        p.append(char((s.qty >> 8) & 0xFF));
        p.append(char(s.qty & 0xFF));
        p.append(char((s.addr2 >> 8) & 0xFF));
        p.append(char(s.addr2 & 0xFF));
        p.append(char((s.qty2 >> 8) & 0xFF));
        p.append(char(s.qty2 & 0xFF));
        p.append(char(data.size() & 0xFF));
        p.append(data);
        return p;
    }

    // 读操作 (01/02/03/04)
    if (!isWriteFc(s.fc))
    {
        const int maxQty = isBitFc(s.fc) ? 2000 : 125;
        if (s.qty < 1 || s.qty > maxQty)
            return fail(QStringLiteral("数量必须在 1-%1 范围内").arg(maxQty));
        p.append(char((s.qty >> 8) & 0xFF));
        p.append(char(s.qty & 0xFF));
        return p;
    }
    return fail(QStringLiteral("不支持的功能码"));
}

// ---- 十进制转换填充 (ModbusGenTool::convertDecimal 移植) ----
// fc: 05→FF00/0000, 06→单字 hex; 0F→位打包 (宽度=qty); 10/17→整数二补码展开
// (宽度=qty 寄存器) 或 IEEE754 float32 (小数, ≥2 寄存器, 余补 00)
inline QString convertDecimalToHex(quint8 fc, int qty, const QString &text, QString *err)
{
    auto fail = [err](const QString &m) -> QString {
        if (err)
            *err = m;
        return QString();
    };
    const QString t = text.trimmed();
    if (t.isEmpty())
        return fail(QStringLiteral("请输入十进制数值"));
    bool isInt = false;
    const qlonglong iv = t.toLongLong(&isInt);

    if (fc == 0x05 || fc == 0x06)
    {
        if (!isInt)
            return fail(QStringLiteral("功能码 0x%1 为单寄存器 (16位)，仅支持整数").arg(fc, 2, 16));
        if (fc == 0x05)
            return iv == 0 ? QStringLiteral("0000") : QStringLiteral("FF00");
        if (iv < -32768 || iv > 65535)
            return fail(QStringLiteral("寄存器值范围 -32768~65535"));
        return QString::number(static_cast<quint16>(iv), 16)
            .rightJustified(4, QLatin1Char('0'))
            .toUpper();
    }

    if (fc == 0x0F)
    {
        if (!isInt)
            return fail(QStringLiteral("线圈写入仅支持整数 (按位打包，bit0=第一个线圈)"));
        const int bytes = (qty + 7) / 8;
        if (bytes > 8)
            return fail(QStringLiteral("十进制转线圈最多支持 64 个"));
        quint64 u = static_cast<quint64>(iv); // 负数按二补码
        QStringList out;
        for (int i = 0; i < bytes; ++i)
        {
            out << QString::number(quint8(u & 0xFF), 16).rightJustified(2, QLatin1Char('0')).toUpper();
            u >>= 8;
        }
        return out.join(QLatin1Char(' '));
    }

    // 10/17: 寄存器序列
    if (!isInt)
    {
        if (qty < 2)
            return fail(QStringLiteral("浮点数需至少 2 个寄存器 (32位 IEEE754)"));
        const float fv = static_cast<float>(t.toDouble());
        quint32 bits = 0;
        static_assert(sizeof(bits) == sizeof(fv), "expect 32-bit float");
        memcpy(&bits, &fv, sizeof(bits));
        QStringList out;
        for (int i = 3; i >= 0; --i)
            out << QString::number(quint8((bits >> (8 * i)) & 0xFF), 16)
                       .rightJustified(2, QLatin1Char('0'))
                       .toUpper();
        for (int i = 4; i < qty * 2; ++i)
            out << QStringLiteral("00");
        return out.join(QLatin1Char(' '));
    }
    const int bytes = qty * 2;
    if (bytes > 8)
        return fail(QStringLiteral("十进制整数转换最多支持 4 个寄存器 (64位)"));
    const int w = bytes * 8;
    const qlonglong lo = (w >= 64) ? std::numeric_limits<qlonglong>::min() : -(1LL << (w - 1));
    const qlonglong hi = (w >= 64) ? std::numeric_limits<qlonglong>::max() : (1LL << w) - 1;
    if (iv < lo || iv > hi)
        return fail(QStringLiteral("数值超出 %1 个寄存器的范围 (%2~%3)").arg(qty).arg(lo).arg(hi));
    const quint64 u = static_cast<quint64>(iv);
    QStringList out;
    for (int i = bytes - 1; i >= 0; --i)
        out << QString::number(quint8((u >> (8 * i)) & 0xFF), 16).rightJustified(2, QLatin1Char('0')).toUpper();
    return out.join(QLatin1Char(' '));
}

// ---- 原始串解析: 容忍空格/逗号/分号/无间隔混排 + "//" 注释行 ----
inline QByteArray parseWireHex(const QString &text, QString *err)
{
    QStringList body;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines)
    {
        const int comment = line.indexOf(QStringLiteral("//"));
        if (comment == 0)
            continue; // 整行注释
        body << (comment > 0 ? line.left(comment) : line);
    }
    const QStringList words =
        body.join(QLatin1Char(' ')).split(QRegularExpression("[\\s,;]+"), Qt::SkipEmptyParts);
    QByteArray out;
    for (const QString &w : words)
    {
        if (w.size() % 2 != 0)
        {
            if (err)
                *err = QStringLiteral("无法解析 \"%1\" (十六进制必须成对)").arg(w);
            return QByteArray();
        }
        for (int i = 0; i < w.size(); i += 2) // 逐字节对 (容忍任意长度的无间隔串)
        {
            bool ok = false;
            const uint b = w.mid(i, 2).toUInt(&ok, 16);
            if (!ok)
            {
                if (err)
                    *err = QStringLiteral("无法解析 \"%1\" (不是十六进制)").arg(w);
                return QByteArray();
            }
            out.append(char(b));
        }
    }
    if (out.isEmpty())
    {
        if (err)
            *err = QStringLiteral("输入为空");
        return QByteArray();
    }
    return out;
}

// ---- Add Check (RTU, 对标 Test Center 的 Add Check): 线级字节尾追加 CRC16 (低字节在前) ----
inline QByteArray addCheckRtu(const QByteArray &wire)
{
    const quint16 crc = ToolFrames::crc16(wire);
    QByteArray out = wire;
    out.append(char(crc & 0xFF)); // 低字节在前
    out.append(char((crc >> 8) & 0xFF));
    return out;
}

// ---- 发送前剥离: 输入可能是裸线级字节, 也可能带了 CRC (RTU) 或 MBAP 头 (TCP) ----
// 返回发送用的线级字节; note 描述剥离动作 (可空)
inline QByteArray stripForSend(bool tcp, const QByteArray &wire, QString *note)
{
    auto say = [note](const char *m) {
        if (note)
            *note = QString::fromUtf8(m);
    };
    if (!tcp)
    {
        // rtuFrame 尾部 = CRC 低字节在前
        if (wire.size() >= 4)
        {
            const quint16 want = ToolFrames::crc16(wire.left(wire.size() - 2));
            const quint16 tail = quint16(quint8(wire.at(wire.size() - 2))
                                         | (quint8(wire.at(wire.size() - 1)) << 8));
            if (want == tail)
            {
                say("已剥离尾部 CRC (发送仍由 Qt 重新校验)");
                return wire.left(wire.size() - 2);
            }
        }
        say("");
        return wire;
    }
    // TCP: [事务2][协议2][长度2][单元1][PDU], 长度 = 其后字节数 (单元+PDU)
    if (wire.size() >= 8 && ToolFrames::be16(wire, 2) == 0
        && ToolFrames::be16(wire, 4) == wire.size() - 6)
    {
        say("已剥离 MBAP 头 (单元标识符作为从站地址)");
        return wire.mid(6);
    }
    say("");
    return wire;
}

// 帧解析字段 (请求/响应共用): name/value 对
struct Field
{
    QString name;
    QString value;
};

// 请求帧字段解析 (线级字节 = 从机+PDU; 离线预览与指令解析共用)
inline QVector<Field> analyzeRequest(const QByteArray &wire)
{
    QVector<Field> rows;
    if (wire.isEmpty())
        return rows;
    const auto hex2 = [](quint8 b) {
        return QString::number(b, 16).rightJustified(2, QLatin1Char('0')).toUpper();
    };
    const auto u16at = [&wire](int off) -> int {
        if (off + 1 >= wire.size())
            return -1;
        return (quint8(wire.at(off)) << 8) | quint8(wire.at(off + 1));
    };
    rows.append({QStringLiteral("从机地址"),
                 QStringLiteral("0x%1 (%2)").arg(hex2(quint8(wire.at(0))))
                     .arg(int(quint8(wire.at(0))))});
    if (wire.size() < 2)
        return rows;
    const quint8 fc = quint8(wire.at(1));
    rows.append({QStringLiteral("功能码"), QStringLiteral("0x%1").arg(hex2(fc))});
    rows.append({QStringLiteral("起始地址"), QString::number(u16at(2))});
    if (fc == 0x05 || fc == 0x06)
        rows.append({QStringLiteral("写入值"),
                     QStringLiteral("0x%1").arg(u16at(4), 4, 16, QLatin1Char('0'))});
    else if (fc == 0x0F || fc == 0x10)
    {
        rows.append({QStringLiteral("数量"), QString::number(u16at(4))});
        const int bc = wire.size() > 6 ? quint8(wire.at(6)) : 0;
        rows.append({QStringLiteral("字节数"), QString::number(bc)});
        QStringList data;
        for (int i = 7; i < wire.size(); ++i)
            data << hex2(quint8(wire.at(i)));
        rows.append({QStringLiteral("数据"), data.join(QLatin1Char(' '))});
    }
    else if (fc == 0x17)
    {
        rows.append({QStringLiteral("读数量"), QString::number(u16at(4))});
        rows.append({QStringLiteral("写地址"), QString::number(u16at(6))});
        rows.append({QStringLiteral("写数量"), QString::number(u16at(8))});
        const int bc = wire.size() > 10 ? quint8(wire.at(10)) : 0;
        rows.append({QStringLiteral("字节数"), QString::number(bc)});
        QStringList data;
        for (int i = 11; i < wire.size(); ++i)
            data << hex2(quint8(wire.at(i)));
        rows.append({QStringLiteral("数据"), data.join(QLatin1Char(' '))});
    }
    else
        rows.append({QStringLiteral("数量"), QString::number(u16at(4))});
    return rows;
}

// ---- 异常码中文表 (0x81-0x8B 低 4 位 01-0B) ----
inline QString exceptionText(quint8 code)
{
    switch (code)
    {
    case 0x01: return QStringLiteral("非法功能");
    case 0x02: return QStringLiteral("非法数据地址");
    case 0x03: return QStringLiteral("非法数据值");
    case 0x04: return QStringLiteral("从站设备故障");
    case 0x05: return QStringLiteral("确认 (ACK, 从站处理中)");
    case 0x06: return QStringLiteral("从站设备忙");
    case 0x08: return QStringLiteral("存储奇偶校验错误");
    case 0x0A: return QStringLiteral("网关不可达");
    case 0x0B: return QStringLiteral("网关目标设备无响应");
    default: return QStringLiteral("未知异常码");
    }
}

// 响应字段解析 (供 UI 表格)
inline QVector<Field> analyzeResponse(const QByteArray &pdu)
{
    QVector<Field> rows;
    if (pdu.isEmpty())
    {
        rows.append({QStringLiteral("响应"), QStringLiteral("— (无线上字节)")});
        return rows;
    }
    const auto hex2 = [](quint8 b) {
        return QString::number(b, 16).rightJustified(2, QLatin1Char('0')).toUpper();
    };
    const quint8 fc = quint8(pdu.at(0));
    if (fc & 0x80)
    {
        rows.append({QStringLiteral("异常功能码"),
                     QStringLiteral("0x%1 (请求 0x%2)").arg(hex2(fc)).arg(hex2(fc & 0x7F))});
        const quint8 code = pdu.size() > 1 ? quint8(pdu.at(1)) : 0;
        rows.append({QStringLiteral("异常码"),
                     QStringLiteral("0x%1 — %2").arg(hex2(code), exceptionText(code))});
        return rows;
    }
    rows.append({QStringLiteral("功能码"), QStringLiteral("0x%1").arg(hex2(fc))});
    const auto u16at = [&pdu](int off) -> int {
        if (off + 1 >= pdu.size())
            return -1;
        return (quint8(pdu.at(off)) << 8) | quint8(pdu.at(off + 1));
    };
    switch (fc)
    {
    case 0x01:
    case 0x02:
    {
        const int bc = pdu.size() > 1 ? quint8(pdu.at(1)) : 0;
        rows.append({QStringLiteral("字节数"), QString::number(bc)});
        QStringList bits;
        for (int i = 0; i < bc && 2 + i < pdu.size(); ++i)
            for (int b = 0; b < 8; ++b)
                bits << ((quint8(pdu.at(2 + i)) >> b) & 1 ? QStringLiteral("ON")
                                                          : QStringLiteral("OFF"));
        rows.append({QStringLiteral("线圈/位 (LSB 在前)"), bits.join(QLatin1Char(','))});
        break;
    }
    case 0x03:
    case 0x04:
    case 0x17:
    {
        const int bc = pdu.size() > 1 ? quint8(pdu.at(1)) : 0;
        rows.append({QStringLiteral("字节数"), QString::number(bc)});
        QStringList regs;
        for (int off = 2; off + 1 < pdu.size(); off += 2)
        {
            const int v = u16at(off);
            regs << QStringLiteral("0x%1(%2)")
                        .arg(v, 4, 16, QLatin1Char('0'))
                        .arg(v > 32767 ? v - 65536 : v);
        }
        rows.append({QStringLiteral("寄存器 (hex(有符号))"), regs.join(QLatin1Char(' '))});
        break;
    }
    case 0x05:
    case 0x06:
        rows.append({QStringLiteral("地址"), QString::number(u16at(1))});
        rows.append({QStringLiteral("值"),
                     QStringLiteral("0x%1").arg(u16at(3), 4, 16, QLatin1Char('0'))});
        break;
    case 0x0F:
    case 0x10:
        rows.append({QStringLiteral("地址"), QString::number(u16at(1))});
        rows.append({QStringLiteral("数量"), QString::number(u16at(3))});
        break;
    default:
    {
        QStringList data;
        for (int i = 1; i < pdu.size(); ++i)
            data << hex2(quint8(pdu.at(i)));
        rows.append({QStringLiteral("数据"), data.join(QLatin1Char(' '))});
        break;
    }
    }
    return rows;
}

} // namespace TestCenter
