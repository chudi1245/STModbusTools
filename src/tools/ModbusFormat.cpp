// ============================================================================
// ModbusFormat.cpp — 寄存器显示格式实现 (16 位原生 + 32/64 位×4 字序)
// ============================================================================

#include "ModbusFormat.h"

#include <cstring>

namespace {

struct KeyMap
{
    const char *key;
    int value;
};
// 表驱动键映射: 键即 JSON 字面量, 值为枚举序
constexpr KeyMap kFmtKeys[] = {
    {"i16", int(ModbusFormat::Fmt::I16)},     {"u16", int(ModbusFormat::Fmt::U16)},
    {"hex", int(ModbusFormat::Fmt::Hex)},     {"bin", int(ModbusFormat::Fmt::Bin)},
    {"ascii", int(ModbusFormat::Fmt::AsciiHex)},
    {"i32", int(ModbusFormat::Fmt::I32)},     {"u32", int(ModbusFormat::Fmt::U32)},
    {"f32", int(ModbusFormat::Fmt::F32)},     {"i64", int(ModbusFormat::Fmt::I64)},
    {"u64", int(ModbusFormat::Fmt::U64)},     {"f64", int(ModbusFormat::Fmt::F64)},
};
constexpr KeyMap kOrderKeys[] = {
    {"ABCD", int(ModbusFormat::ByteOrder::ABCD)}, {"CDAB", int(ModbusFormat::ByteOrder::CDAB)},
    {"BADC", int(ModbusFormat::ByteOrder::BADC)}, {"DCBA", int(ModbusFormat::ByteOrder::DCBA)},
};

template <int N>
int lookup(const KeyMap (&tab)[N], const QString &key, int fallback)
{
    for (const KeyMap &m : tab)
        if (key == QLatin1String(m.key))
            return m.value;
    return fallback;
}

// 寄存器序列 → 大端字节序列 (高字在前)
void regsToBytes(const quint16 *regs, int n, quint8 *out)
{
    for (int i = 0; i < n; ++i)
    {
        out[2 * i] = quint8(regs[i] >> 8);
        out[2 * i + 1] = quint8(regs[i]);
    }
}

// ByteOrder 变换 (均为对合变换: 同一排列应用两次还原)
void applyOrder(ModbusFormat::ByteOrder o, quint8 *b, int nWords)
{
    const int total = nWords * 2;
    switch (o)
    {
    case ModbusFormat::ByteOrder::ABCD:
        break;
    case ModbusFormat::ByteOrder::DCBA: // 全字节反序
        for (int i = 0; i < total / 2; ++i)
            qSwap(b[i], b[total - 1 - i]);
        break;
    case ModbusFormat::ByteOrder::BADC: // 每字内字节交换
        for (int i = 0; i < nWords; ++i)
            qSwap(b[2 * i], b[2 * i + 1]);
        break;
    case ModbusFormat::ByteOrder::CDAB: // 字交换 (整字对调, 两个字节一起换)
        for (int i = 0; i < nWords / 2; ++i)
        {
            qSwap(b[2 * i], b[2 * (nWords - 1 - i)]);
            qSwap(b[2 * i + 1], b[2 * (nWords - 1 - i) + 1]);
        }
        break;
    }
}

quint64 bytesToU64(const quint8 *b, int total)
{
    quint64 raw = 0;
    for (int i = 0; i < total; ++i)
        raw = (raw << 8) | b[i];
    return raw;
}

// regs (按当前字序) → 逻辑大端 u64 (低位补齐, 调用方按格式截取)
quint64 reorderBE(ModbusFormat::Fmt f, ModbusFormat::ByteOrder o, const quint16 *regs)
{
    const int n = ModbusFormat::regCount(f);
    quint8 b[8] = {};
    regsToBytes(regs, n, b);
    applyOrder(o, b, n);
    return bytesToU64(b, n * 2);
}

// 逻辑大端 u64 → regs (按当前字序) — parseInput 的落盘方向
void spreadBE(ModbusFormat::Fmt f, ModbusFormat::ByteOrder o, quint64 raw, quint16 *regs)
{
    const int n = ModbusFormat::regCount(f);
    const int total = n * 2;
    quint8 b[8] = {};
    for (int i = total - 1; i >= 0; --i)
    {
        b[i] = quint8(raw);
        raw >>= 8;
    }
    applyOrder(o, b, n);
    for (int i = 0; i < n; ++i)
        regs[i] = quint16((b[2 * i] << 8) | b[2 * i + 1]);
}

// 16 位原生格式渲染
QString fmt16(ModbusFormat::Fmt f, quint16 raw)
{
    switch (f)
    {
    case ModbusFormat::Fmt::I16:
        return QString::number(static_cast<qint16>(raw));
    case ModbusFormat::Fmt::U16:
        return QString::number(raw);
    case ModbusFormat::Fmt::Hex:
        return QString::number(raw, 16).rightJustified(4, QLatin1Char('0')).toUpper();
    case ModbusFormat::Fmt::Bin:
        return QString::number(raw, 2).rightJustified(16, QLatin1Char('0'));
    case ModbusFormat::Fmt::AsciiHex:
    {
        QString s;
        for (int b = 1; b >= 0; --b) // 高字节在前
        {
            const char c = char((raw >> (8 * b)) & 0xFF);
            s += (c >= 0x20 && c < 0x7F) ? QChar(c) : QChar::fromLatin1('.');
        }
        return s;
    }
    default:
        return QString();
    }
}

} // namespace

namespace ModbusFormat {

QString fmtKey(Fmt f)
{
    for (const KeyMap &m : kFmtKeys)
        if (int(f) == m.value)
            return QLatin1String(m.key);
    return QStringLiteral("u16");
}

Fmt fmtFromKey(const QString &key, Fmt fallback)
{
    return Fmt(lookup(kFmtKeys, key, int(fallback)));
}

QString orderKey(ByteOrder o)
{
    for (const KeyMap &m : kOrderKeys)
        if (int(o) == m.value)
            return QLatin1String(m.key);
    return QStringLiteral("ABCD");
}

ByteOrder orderFromKey(const QString &key, ByteOrder fallback)
{
    return ByteOrder(lookup(kOrderKeys, key, int(fallback)));
}

int regCount(Fmt f)
{
    switch (f)
    {
    case Fmt::I32:
    case Fmt::U32:
    case Fmt::F32:
        return 2;
    case Fmt::I64:
    case Fmt::U64:
    case Fmt::F64:
        return 4;
    default:
        return 1;
    }
}

bool isIntegerFmt(Fmt f)
{
    return f != Fmt::F32 && f != Fmt::F64;
}

QString formatRegs(Fmt f, ByteOrder o, const quint16 *regs, int n)
{
    if (!regs || n < regCount(f))
        return QString();
    if (regCount(f) == 1)
        return fmt16(f, regs[0]);

    const quint64 raw = reorderBE(f, o, regs);
    switch (f)
    {
    case Fmt::I32:
    {
        const quint32 u = quint32(raw);
        qint32 s;
        std::memcpy(&s, &u, sizeof(s)); // 避免 C++17 下实现定义的整型收缩转换
        return QString::number(s);
    }
    case Fmt::U32:
        return QString::number(quint32(raw));
    case Fmt::F32:
    {
        const quint32 u = quint32(raw);
        float fv;
        std::memcpy(&fv, &u, sizeof(fv));
        return QString::number(fv, 'g', 7);
    }
    case Fmt::I64:
    {
        qint64 s;
        std::memcpy(&s, &raw, sizeof(s));
        return QString::number(s);
    }
    case Fmt::U64:
        return QString::number(raw);
    case Fmt::F64:
    {
        double dv;
        std::memcpy(&dv, &raw, sizeof(dv));
        return QString::number(dv, 'g', 15);
    }
    default:
        return QString();
    }
}

bool parseInput(Fmt f, ByteOrder o, const QString &text, quint16 *regs, int n)
{
    if (!regs || n < regCount(f))
        return false;
    const QString t = text.trimmed();
    if (t.isEmpty())
        return false;
    bool ok = false;

    // ---- 16 位原生格式 ----
    if (regCount(f) == 1)
    {
        quint16 r0 = 0;
        switch (f)
        {
        case Fmt::I16:
        {
            // 接受 -32768..65535 (65535 按 0xFFFF 字面写入, 显示时再按符号解读)
            const qlonglong v = t.toLongLong(&ok);
            if (!ok || v < -32768 || v > 65535)
                return false;
            r0 = quint16(v & 0xFFFF); // 负数按二补码字面写入
            break;
        }
        case Fmt::U16:
        {
            if (t.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
            {
                const uint v = t.mid(2).toUInt(&ok, 16);
                if (!ok || v > 0xFFFF)
                    return false;
                r0 = quint16(v);
            }
            else if (t.startsWith(QStringLiteral("0b"), Qt::CaseInsensitive))
            {
                const QString b = t.mid(2);
                if (b.isEmpty() || b.size() > 16)
                    return false;
                for (const QChar c : b)
                    if (c != QLatin1Char('0') && c != QLatin1Char('1'))
                        return false;
                r0 = quint16(b.toUInt(nullptr, 2));
            }
            else
            {
                const uint v = t.toUInt(&ok, 10);
                if (!ok || v > 65535)
                    return false;
                r0 = quint16(v);
            }
            break;
        }
        case Fmt::Hex:
        {
            QString h = t;
            if (h.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
                h.remove(0, 2);
            if (h.isEmpty() || h.size() > 4)
                return false;
            const uint v = h.toUInt(&ok, 16);
            if (!ok)
                return false;
            r0 = quint16(v);
            break;
        }
        case Fmt::Bin:
        {
            QString b = t;
            if (b.startsWith(QStringLiteral("0b"), Qt::CaseInsensitive))
                b.remove(0, 2);
            if (b.isEmpty() || b.size() > 16)
                return false;
            for (const QChar c : b)
                if (c != QLatin1Char('0') && c != QLatin1Char('1'))
                    return false;
            r0 = quint16(b.toUInt(nullptr, 2));
            break;
        }
        case Fmt::AsciiHex:
        {
            if (t.size() != 2)
                return false;
            const int hi = t.at(0).toLatin1();
            const int lo = t.at(1).toLatin1();
            if (hi <= 0 || hi > 0xFF || lo <= 0 || lo > 0xFF)
                return false;
            r0 = quint16((quint8(hi) << 8) | quint8(lo));
            break;
        }
        default:
            return false;
        }
        regs[0] = r0;
        return true;
    }

    // ---- 32/64 位宽格式 ----
    switch (f)
    {
    case Fmt::I32:
    {
        const qlonglong v = t.toLongLong(&ok);
        if (!ok || v < -(1LL << 31) || v > 0xFFFFFFFFLL)
            return false;
        spreadBE(f, o, quint64(v & 0xFFFFFFFFLL), regs);
        return true;
    }
    case Fmt::U32:
    {
        const qulonglong v = t.toULongLong(&ok);
        if (!ok || v > 0xFFFFFFFFLL)
            return false;
        spreadBE(f, o, quint64(v), regs);
        return true;
    }
    case Fmt::F32:
    {
        const double dv = t.toDouble(&ok);
        if (!ok)
            return false;
        const float fv = float(dv);
        quint32 bits;
        std::memcpy(&bits, &fv, sizeof(bits));
        spreadBE(f, o, bits, regs);
        return true;
    }
    case Fmt::I64:
    {
        const qlonglong v = t.toLongLong(&ok);
        if (!ok)
            return false;
        spreadBE(f, o, quint64(v), regs); // 负数按二补码
        return true;
    }
    case Fmt::U64:
    {
        const qulonglong v = t.toULongLong(&ok);
        if (!ok)
            return false;
        spreadBE(f, o, quint64(v), regs);
        return true;
    }
    case Fmt::F64:
    {
        const double dv = t.toDouble(&ok);
        if (!ok)
            return false;
        quint64 bits;
        std::memcpy(&bits, &dv, sizeof(bits));
        spreadBE(f, o, bits, regs);
        return true;
    }
    default:
        return false;
    }
}

double numericValue(Fmt f, ByteOrder o, const quint16 *regs, int n, bool *ok)
{
    if (ok)
        *ok = false;
    if (!regs || n < regCount(f))
        return 0;
    if (ok)
        *ok = true;
    if (regCount(f) == 1)
    {
        switch (f)
        {
        case Fmt::I16:
            return double(qint16(regs[0]));
        default:
            return double(regs[0]); // U16/Hex/Bin/AsciiHex 按无符号字面值
        }
    }
    const quint64 raw = reorderBE(f, o, regs);
    switch (f)
    {
    case Fmt::I32:
    {
        const quint32 u = quint32(raw);
        qint32 s;
        std::memcpy(&s, &u, sizeof(s));
        return double(s);
    }
    case Fmt::U32:
        return double(quint32(raw));
    case Fmt::F32:
    {
        const quint32 u = quint32(raw);
        float fv;
        std::memcpy(&fv, &u, sizeof(fv));
        return double(fv);
    }
    case Fmt::I64:
    {
        qint64 s;
        std::memcpy(&s, &raw, sizeof(s));
        return double(s);
    }
    case Fmt::U64:
        return double(raw);
    case Fmt::F64:
    {
        double dv;
        std::memcpy(&dv, &raw, sizeof(dv));
        return dv;
    }
    default:
        break;
    }
    if (ok)
        *ok = false;
    return 0;
}

} // namespace ModbusFormat
