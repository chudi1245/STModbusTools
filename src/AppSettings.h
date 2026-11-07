#pragma once

// ============================================================================
// AppSettings.h — 应用设置统一访问 (ModbusTools 自己的 key schema 权威来源)
//   全部持久化经 QSettings (NativeFormat, org=ST072G app=ModbusTools)。
//   规则:
//     * key 字符串只允许出现在本文件 (及本 .cpp); 其他模块一律经类型化接口;
//     * 默认值与范围钳制集中在此, 读写两侧共用同一套;
//     * UI (ConnDialog 等) 只做编辑与保存调用, 不承载设置语义。
//   key 一览 (按域):
//     settings/lang                                   — 界面语言
//     mbtool/kind, mbtool/port, mbtool/baud,
//     mbtool/parity, mbtool/stop, mbtool/host,
//     mbtool/tcpport, mbtool/timeout, mbtool/retries,
//     mbtool/plc, mbtool/workspace                    — 连接参数 / 工作区
// ============================================================================

#include <QString>
#include <QtGlobal>

namespace AppSettings
{
// ---- 迁移 ----
// 本工具自称重测力调试软件 (WeightTool) 拆分而来: 首次启动且自身无
// mbtool/ 记忆时, 从主程序的 QSettings (org=ST072G app=称重测力调试软件)
// 一次性搬入 mbtool/* 与 settings/lang — 老用户的连接参数/工作区路径不丢。
void migrateLegacyIfNeeded();

// ---- 常规 ----
// ""=跟随系统, "zh", "en"
QString language();
void setLanguage(const QString &lang);

// ---- Modbus 调试工具连接 (存实际值而非下拉索引) ----
struct MbToolConn
{
    int kind = 0;         // 0=RTU, 1=TCP
    QString portName;     // 串口名 (空 = 枚举列表首个)
    int baud = 115200;    // 实际波特率值 (可自定义)
    int parity = 0;       // 0=无 1=偶 2=奇
    int stopBits = 1;     // 1 / 2
    QString host;         // TCP 主机
    int tcpPort = 502;
    int timeoutMs = 1000; // 响应超时
    int retries = 2;      // 重试次数
    bool plcAddr = false; // 地址列 PLC 风格 (Base 1 + 0x/1x/3x/4x 前缀)
};
MbToolConn mbToolConn();
void setMbToolConn(const MbToolConn &c);

// 最近工作区文件路径 (空 = 无记忆)
QString mbToolWorkspacePath();
void setMbToolWorkspacePath(const QString &path);
} // namespace AppSettings
