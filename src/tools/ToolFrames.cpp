// ============================================================================
// ToolFrames.cpp — Modbus 帧/CRC 计算共享工具
// ============================================================================

#include "ToolFrames.h"

#include <QRandomGenerator>

#include <cstring>

namespace ToolFrames {

quint16 crc16(const QByteArray &data)
{
    quint16 crc = 0xFFFF;
    for (char byte : data)
    {
        crc ^= static_cast<quint8>(byte);
        for (int j = 0; j < 8; ++j)
        {
            if (crc & 0x0001)
            {
                crc >>= 1;
                crc ^= 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}

QByteArray rtuFrame(const QByteArray &pdu)
{
    QByteArray f = pdu;
    const quint16 crc = crc16(pdu);
    f.append(static_cast<char>(crc & 0xFF));
    f.append(static_cast<char>((crc >> 8) & 0xFF));
    return f;
}

QByteArray tcpFrame(const QByteArray &pdu)
{
    const quint16 txId = static_cast<quint16>(QRandomGenerator::global()->bounded(65536));
    QByteArray f;
    f.append(static_cast<char>((txId >> 8) & 0xFF));
    f.append(static_cast<char>(txId & 0xFF));
    f.append(char(0x00));
    f.append(char(0x00));
    f.append(static_cast<char>((pdu.size() >> 8) & 0xFF));
    f.append(static_cast<char>(pdu.size() & 0xFF));
    f.append(pdu);
    return f;
}

QByteArray u16be(quint16 v)
{
    QByteArray f;
    f.append(static_cast<char>((v >> 8) & 0xFF));
    f.append(static_cast<char>(v & 0xFF));
    return f;
}

QByteArray i32be(qint32 v)
{
    QByteArray f;
    f.append(static_cast<char>((v >> 24) & 0xFF));
    f.append(static_cast<char>((v >> 16) & 0xFF));
    f.append(static_cast<char>((v >> 8) & 0xFF));
    f.append(static_cast<char>(v & 0xFF));
    return f;
}

QByteArray u32be(quint32 v) { return i32be(static_cast<qint32>(v)); }

QByteArray f32be(float v)
{
    quint32 u = 0;
    static_assert(sizeof u == sizeof v, "float must be 32-bit");
    memcpy(&u, &v, sizeof u);
    return i32be(static_cast<qint32>(u));
}

static QString hexify(const QByteArray &frame, const char *sep)
{
    QString out;
    const int n = frame.size();
    out.reserve(n * (2 + (sep[0] ? 1 : 0)));
    for (int i = 0; i < n; ++i)
    {
        if (i) out += QLatin1String(sep);
        out += QString::number(static_cast<quint8>(frame.at(i)), 16).rightJustified(2, QLatin1Char('0')).toUpper();
    }
    return out;
}

QString hexPlain(const QByteArray &frame) { return hexify(frame, ""); }
QString hexSpaced(const QByteArray &frame) { return hexify(frame, " "); }
QString hexPrefixed(const QByteArray &frame)
{
    QString out;
    const int n = frame.size();
    for (int i = 0; i < n; ++i)
    {
        if (i) out += QLatin1Char(' ');
        out += QStringLiteral("0x")
               + QString::number(static_cast<quint8>(frame.at(i)), 16)
                     .rightJustified(2, QLatin1Char('0'))
                     .toUpper();
    }
    return out;
}

quint16 be16(const QByteArray &f, int off)
{
    if (off + 1 >= f.size()) return 0;
    return static_cast<quint16>(((static_cast<quint8>(f.at(off)) << 8) | static_cast<quint8>(f.at(off + 1))) & 0xFFFF);
}

qint32 be32s(const QByteArray &f, int off)
{
    if (off + 3 >= f.size()) return 0;
    const quint32 u = (static_cast<quint32>(static_cast<quint8>(f.at(off))) << 24)
                      | (static_cast<quint32>(static_cast<quint8>(f.at(off + 1))) << 16)
                      | (static_cast<quint32>(static_cast<quint8>(f.at(off + 2))) << 8)
                      | static_cast<quint32>(static_cast<quint8>(f.at(off + 3)));
    return static_cast<qint32>(u);
}

quint32 be32u(const QByteArray &f, int off) { return static_cast<quint32>(be32s(f, off)); }

} // namespace ToolFrames
