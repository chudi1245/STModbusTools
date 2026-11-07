#pragma once

// ============================================================================
// LuaPorts.h — LuaHost 对外依赖的窄接口 (端口)
//   LuaHost (脚本宿主) 不依赖具体服务实现, 只依赖这里声明的最小能力面;
//   由宿主环境注入: GUI = MainWindow, 无头 = Headless, 测试 = 测试夹具。
//   方法签名与既有实现 (ModbusClient/Recorder) 完全一致, 实现类直接继承。
// ============================================================================

#include <QString>
#include <QVector>
#include <QtGlobal>
#include <functional>

// 寄存器读写 (标定协程 api.read*/write* 用)
class ILuaRegIo
{
public:
    virtual ~ILuaRegIo() = default;

    using ReadCb = std::function<void(bool ok, const QVector<quint16> &regs)>;
    using WriteCb = std::function<void(bool ok)>;

    // 回调契约与 ModbusClient 一致: 任何排队/在途请求保证回调一次,
    // 被丢弃/合并/断连清理时以 ok=false 通知
    virtual void readRegs(quint16 addr, int count, ReadCb cb) = 0;
    virtual void writeRegs(quint16 addr, const QVector<quint16> &values, WriteCb cb) = 0;
    // 插队写 (标定命令等关键操作)
    virtual void writeRegFront(quint16 addr, quint16 value, WriteCb cb) = 0;
    // 连接态 / 读在途判定 (轮询引擎防积压; HilLink 虚拟寄存器桥同语义)
    virtual bool isConnected() const = 0;
    virtual bool hasPendingRead() const = 0;
};

// 记录会话 (标定协程 api.record_* 用)
class ILuaRecord
{
public:
    virtual ~ILuaRecord() = default;

    virtual void startSession(const QString &note) = 0;
    virtual void stopSession() = 0;
    virtual bool recording() const = 0;
    virtual void recordEvent(const QString &msg) = 0;
};
