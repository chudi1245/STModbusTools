# ModbusTools — 第三方组件与许可证声明

本软件（ModbusTools，Modbus 调试工具）在运行时使用了以下第三方组件。
所有 Qt 模块均以**动态链接**方式使用（未修改 Qt 的任何部分）。

| 组件 | 版本 | 许可证 | 链接方式 | 随附文本 |
|---|---|---|---|---|
| Qt 6 Core / Gui / Widgets | 6.11.1 | LGPL-3.0-only（亦可选 GPL-2/3 或商业许可） | 动态 (Qt6*.dll) | `LGPL-3.0-only.txt` |
| Qt 6 Network（SerialBus 传递依赖） | 6.11.1 | LGPL-3.0-only | 动态 | `LGPL-3.0-only.txt` |
| Qt 6 SerialPort | 6.11.1 | LGPL-3.0-only | 动态 | `LGPL-3.0-only.txt` |
| Qt 6 SerialBus (Modbus) | 6.11.1 | LGPL-3.0-only | 动态 | `LGPL-3.0-only.txt` |
| GCC 运行时 (libgcc/libstdc++/winpthread) | 13.1 | GPL-3 + GCC Runtime Library Exception / 宽松许可 | 动态 DLL | `MinGW-Runtime-Notices.txt` |

> 本发布物不含 GPL-3.0-only 的 Qt 模块（趋势图功能与 Qt Charts 依赖已移除）。
> 全部 Qt 模块以 LGPL-3.0 动态链接（未修改 Qt 任何部分），据此允许闭源商业
> 分发，义务为：随附 LGPL-3.0 许可文本与本声明、不反编译限制条款、允许用户
> 重新链接 Qt（动态链接布局已满足）。`GPL-3.0-only.txt` 仅随 Windows 版保留，
> 供 GCC 运行时库的 GPL-3 + Runtime Library Exception 条款引用。

## Qt 官方源码与许可信息

- Qt 下载（含 LGPL 版源码）: https://www.qt.io/download/open-source
- Qt 各组件所用许可证官方清单（对应当前安装版本）:
  Qt 6.11.1 文档 `licenses-used-in-qt.html`
  （在线版: https://doc.qt.io/qt-6/licenses-used-in-qt.html）
- GNU LGPL-3.0 / GPL-3.0 文本: https://www.gnu.org/licenses/
