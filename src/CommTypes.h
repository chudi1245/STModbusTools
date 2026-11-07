#pragma once

// ============================================================================
// CommTypes.h — 通信事务/帧记录 (纯数据, 跨层共享)
//   ModbusClient 每个完成事务发一次 transactionDone (含失败/被丢弃);
//   每条线上字节 (请求/应答/错误) 发一次 frameLogged。
//   Qt SerialBus 不透传 RTU ADU 与 TCP MBAP 的编帧字节: 帧记录存 PDU + 从站地址
//   (+ TCP 事务 ID 镜像), 帧日志按传输模式补全为线上完整帧展示:
//     RTU → commRtuAdu: [地址][PDU][CRC] (CRC 按帧内容本地补算, 算法确定);
//     TCP → commTcpAdu: [事务ID][协议ID][长度][单元ID][PDU]
//         (TID 与 Qt 内部计数镜像: 每 0 起, 每次成功发出 +1, 超时重发不变);
//   CRC 错误本身体现在 TxnErr::Protocol 的统计里。
// ============================================================================

#include <QByteArray>
#include <QMetaType>
#include <QString>

// 事务错误分类 (通信体检统计口径)
enum class TxnErr
{
    Ok = 0,
    Timeout = 1,     // 应答超时
    Protocol = 2,    // 协议异常帧 (含 CRC 错)
    Connection = 3,  // 连接/发送失败
    Dropped = 4      // 被丢弃/合并/熔断清理 (未上线, elapsed=0)
};

struct TxnRecord
{
    quint64 txn = 0;
    bool isWrite = false;
    quint16 addr = 0;
    int count = 0;
    qint64 tMs = 0;          // 完成时刻 (epoch ms)
    double elapsedMs = 0;    // 发送→完成耗时 (未上线为 0)
    int err = static_cast<int>(TxnErr::Ok);
    QString note;            // 丢弃原因等 (可空)
};

struct FrameRecord
{
    qint64 tMs = 0;
    bool isTx = false;
    bool isErr = false;
    quint8 addr = 0;         // 从站地址 (TX=请求地址, RX=应答自报地址)
    bool isRtu = false;      // true = RTU 传输; false = TCP 传输 (MBAP 展示)
    quint16 mbapTid = 0;     // TCP 事务 ID 镜像 (仅 !isRtu 时用于 MBAP 展示)
    QByteArray pdu;          // 功能码+数据 (hex 展示; 无线上字节时为空)
    double elapsedMs = 0;
    QString note;            // 错误说明 (可空)
};

// QModbusPdu → 展示用 PDU 字节 (功能码 + data)
inline QByteArray commPduBytes(int functionCode, const QByteArray &data)
{
    QByteArray p;
    p.append(static_cast<char>(quint8(functionCode)));
    p.append(data);
    return p;
}

// Modbus RTU CRC16 (初值 0xFFFF, 多项式 0xA001 反射算法)
inline quint16 commCrc16(const QByteArray &data)
{
    quint16 crc = 0xFFFF;
    for (int i = 0; i < data.size(); ++i)
    {
        crc ^= quint8(data.at(i));
        for (int b = 0; b < 8; ++b)
            crc = (crc & 1) ? quint16((crc >> 1) ^ 0xA001) : quint16(crc >> 1);
    }
    return crc;
}

// RTU 完整 ADU: [从站地址][PDU][CRC 低字节][CRC 高字节] (RTU 低字节在前)
inline QByteArray commRtuAdu(quint8 addr, const QByteArray &pdu)
{
    QByteArray adu;
    adu.reserve(pdu.size() + 3);
    adu.append(static_cast<char>(addr));
    adu += pdu;
    const quint16 crc = commCrc16(adu);
    adu.append(static_cast<char>(quint8(crc & 0xFF)));
    adu.append(static_cast<char>(quint8(crc >> 8)));
    return adu;
}

// TCP 完整 ADU: [事务ID 2B][协议ID 2B=0][长度 2B=PDU+1][单元ID 1B][PDU]
// (全部大端; 长度含单元 ID 字节)
inline QByteArray commTcpAdu(quint16 tid, quint8 unitId, const QByteArray &pdu)
{
    QByteArray adu;
    adu.reserve(pdu.size() + 7);
    const quint16 len = quint16(pdu.size() + 1);
    adu.append(static_cast<char>(quint8(tid >> 8)));
    adu.append(static_cast<char>(quint8(tid & 0xFF)));
    adu.append('\x00');
    adu.append('\x00');
    adu.append(static_cast<char>(quint8(len >> 8)));
    adu.append(static_cast<char>(quint8(len & 0xFF)));
    adu.append(static_cast<char>(unitId));
    adu += pdu;
    return adu;
}

Q_DECLARE_METATYPE(TxnRecord)
Q_DECLARE_METATYPE(FrameRecord)
