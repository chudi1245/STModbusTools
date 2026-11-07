#pragma once

// ============================================================================
// ToolFrames.h — Modbus 帧/CRC 计算共享工具 (工具箱各页共用)
//   RTU = PDU + CRC16(低字节在前); TCP = MBAP 头(随机事务ID) + PDU
// ============================================================================

#include <QByteArray>
#include <QString>

namespace ToolFrames {

quint16 crc16(const QByteArray &data);          // Modbus CRC16 (poly 0xA001, 初值 0xFFFF)
QByteArray rtuFrame(const QByteArray &pdu);     // PDU + CRC (低字节在前)
QByteArray tcpFrame(const QByteArray &pdu);     // MBAP(随机事务ID) + PDU

QByteArray u16be(quint16 v);                    // 2 字节大端
QByteArray i32be(qint32 v);                     // 4 字节大端 (补码)
QByteArray u32be(quint32 v);                    // 4 字节大端
QByteArray f32be(float v);                      // 4 字节大端 IEEE754

QString hexPlain(const QByteArray &frame);      // "0103000A0002C4.."
QString hexSpaced(const QByteArray &frame);     // "01 03 00 0A 00 02 .."
QString hexPrefixed(const QByteArray &frame);   // "0x01 0x03 0x00 0x0A .."

quint16 be16(const QByteArray &f, int off);     // 大端 u16
qint32 be32s(const QByteArray &f, int off);     // 大端有符号 i32
quint32 be32u(const QByteArray &f, int off);    // 大端无符号 u32

} // namespace ToolFrames
