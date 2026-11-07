#pragma once

// ============================================================================
// ModbusTransport.h — Qt SerialBus 客户端工厂 (RTU 串口 / Modbus TCP)
//   协议栈 (RTU 帧/CRC/超时/重试/TCP) 完全由 Qt 实现, 本头只做实例选择与
//   连接参数装配 — ModbusClient (主界面) 与 MasterClient (调试工具) 共用,
//   两处各自管理调度策略, 传输装配语义只有这一份
// ============================================================================

#include <QModbusClient>
#include <QModbusRtuSerialClient>
#include <QModbusTcpClient>
#include <QNetworkProxy>
#include <QSerialPort>
#include <QVariant>

namespace ModbusTransport {

enum class Kind
{
    SerialRtu,
    Tcp
};

inline QModbusClient *createClient(Kind k, QObject *parent)
{
    if (k == Kind::Tcp)
    {
        // Modbus/TCP 是局域网明文直连, 不允许走代理: 本机开了系统代理时
        // (v2rayN 等, HTTP 代理), Qt 启动即收编为 applicationProxy, 而
        // 明文 QTcpSocket 不支持经 HTTP 代理 — 直接报
        // "The proxy type is invalid for this operation", 连接发不出去;
        // 且 applicationProxy 不看 ProxyOverride 绕过列表, 192.168.* 照样被劫持。
        // 全进程生效: 工程内 Qt 网络栈只有此处消费 (更新走原生 WinSparkle)。
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    }
    QModbusClient *mb = (k == Kind::Tcp)
                            ? static_cast<QModbusClient *>(new QModbusTcpClient(parent))
                            : static_cast<QModbusClient *>(new QModbusRtuSerialClient(parent));
    mb->setTimeout(300);
    mb->setNumberOfRetries(2);
    return mb;
}

inline void applySerialParams(QModbusClient *mb, const QString &portName, qint32 baud,
                              QSerialPort::Parity parity,
                              QSerialPort::StopBits stopBits = QSerialPort::OneStop)
{
    mb->setConnectionParameter(QModbusDevice::SerialPortNameParameter, portName);
    mb->setConnectionParameter(QModbusDevice::SerialBaudRateParameter, baud);
    mb->setConnectionParameter(QModbusDevice::SerialParityParameter, static_cast<int>(parity));
    mb->setConnectionParameter(QModbusDevice::SerialDataBitsParameter,
                               static_cast<int>(QSerialPort::Data8));
    mb->setConnectionParameter(QModbusDevice::SerialStopBitsParameter,
                               static_cast<int>(stopBits));
}

inline void applyTcpParams(QModbusClient *mb, const QString &host, quint16 port)
{
    mb->setConnectionParameter(QModbusDevice::NetworkAddressParameter, host);
    mb->setConnectionParameter(QModbusDevice::NetworkPortParameter, port);
}

} // namespace ModbusTransport
