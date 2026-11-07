#pragma once

// ============================================================================
// ModbusFormat.h — 寄存器显示格式纯逻辑 (无 GUI, 测试直测)
//   对标 Modbus Poll Display 菜单 28 种: 16 位原生 5 种 +
//   32/64 位 × 4 字序 24 种 = 29 项 (含 AsciiHex)
//   字序 (以 32 位 ABCD 为例):
//     ABCD = 大端原序 / DCBA = 全字节反序 / BADC = 每字内字节交换 / CDAB = 字交换
// ============================================================================

#include <QString>
#include <QtGlobal>

namespace ModbusFormat {

enum class Fmt
{
    I16,      // 有符号十进制 (1 寄存器)
    U16,      // 无符号十进制
    Hex,      // 十六进制 (4 位大写)
    Bin,      // 二进制 (16 位)
    AsciiHex, // 两字节按 ASCII 字符显示 (不可打印作 '.')
    I32,      // 2 寄存器
    U32,
    F32,      // IEEE754 单精度
    I64,      // 4 寄存器
    U64,
    F64       // IEEE754 双精度
};

enum class ByteOrder
{
    ABCD,
    CDAB,
    BADC,
    DCBA
};

// JSON 存取键 (工作区文件用, 字符串稳定不随枚举顺序变化)
QString fmtKey(Fmt f);
Fmt fmtFromKey(const QString &key, Fmt fallback = Fmt::U16);
QString orderKey(ByteOrder o);
ByteOrder orderFromKey(const QString &key, ByteOrder fallback = ByteOrder::ABCD);

// 该格式占用的寄存器数: 16 位=1, 32 位=2, 64 位=4
int regCount(Fmt f);
// 整数格式 (I16/U16/Hex/Bin/AsciiHex/I32/U32/I64/U64) — 值名映射/缩放/变色适用域
bool isIntegerFmt(Fmt f);

// 把 regs[0..n-1] 按格式渲染为显示文本; n 不足 regCount 时返回空
QString formatRegs(Fmt f, ByteOrder o, const quint16 *regs, int n);

// 解析用户输入写回 regs[0..n-1]; 接受十进制 / 0x 十六进制 / 0b 二进制 /
// ASCII 文本 / 浮点 (浮点格式), 超范围或非法返回 false
bool parseInput(Fmt f, ByteOrder o, const QString &text, quint16 *regs, int n);

// 数值化 (条件变色/缩放的计算基础): 整数格式 = 整数值; 浮点格式 = 浮点值
double numericValue(Fmt f, ByteOrder o, const quint16 *regs, int n, bool *ok);

} // namespace ModbusFormat
