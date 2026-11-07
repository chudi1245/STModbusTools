# AGENTS.md — ModbusTools 工作区说明

Modbus 主站调试工具（对标 Modbus Poll），自 WeightTool 拆分的独立 Qt 6 桌面程序。
注释、UI 文案、commit message 全部使用中文；commit 格式 `类型: 描述`（如 `修复: ...`）。

> 本文件主要面向 AI 编码协作与项目维护者；外部贡献者请先读
> [README.md](README.md)，其中「构建与测试」「新增源文件的硬性规则」同样适用。
> 「Windows 本机」等环境描述为维护者的开发环境，非贡献门槛。

## 构建与测试（Windows 本机）

- 工具链：Qt 6 @ `C:/devtools/qt`（MinGW 13.1 + Ninja，CMake ≥ 3.21），构建目录 `build/`（已配置为 Release）。
- 构建：`cmake --build build`；重配置：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`。
- 测试：`ctest --test-dir build`（离屏冒烟 + 回环 TCP 用例；实机用例接硬件才跑，否则自动 QSKIP，默认 COM30 RTU 可用环境变量覆盖）。
- TCP 测试模拟器：`python3 scripts/modbus_sim.py [--port 1502]`（纯标准库，不随产品分发）。
- 发布：Windows `cmake --build build --target all dist && cd build && cpack`（ZIP/NSIS）；macOS `cmake --build build --target dist`（DMG，arm64 brew Qt，最低 macOS 14）。

## 新增源文件的硬性规则

无共享库：`tst_modbustools` 直接重编译主程序源文件。新增 .h/.cpp 必须同时加入
**根 CMakeLists.txt** 的 `qt_add_executable(ModbusTools ...)` 和 **tests/CMakeLists.txt**
的 `add_executable(tst_modbustools ...)` 两处列表，否则要么链接失败要么测试覆盖不到。

## 目录与分层

- `src/` — 共享通信层与观感基座：`ModbusClient`（RTU/TCP 收发核心）、`CommTypes` /
  `ModbusTransport` / `CommStatsCollector` / `FrameLogDialog` / `LuaPorts.h`（寄存器读写
  窄接口，无 Lua 依赖）；`Theme.h` / `Icons.h` / `BtnStyles.h`；`AppSettings`（QSettings
  唯一权威）；`main.cpp`（浅色锁定 + Fusion + 全局 QSS，必须在窗口构造前设置）。
- `src/tools/` — 工具内核：`ModbusMasterTool`（主窗）、`ModbusWorkspace`（纯函数 JSON
  序列化，无 GUI，可测）、`ConnectionManager`（多连接注册表）、`SlavePoller`、
  `DeviceTreeWidget`、`DataViewWidget`、`PollDefinition`/`PollTableModel`、
  `TestCenterPage`/`TestCenterCore`、`ScanDialog`；多个对话框为纯头文件
  （`ConnDialog.h`、`DefDialog.h`、`SlaveSetupDialog.h` 等）。
- 头文件引用：`src/` 与 `src/tools/` 互相以**裸文件名** include（两个目录都在
  include path 上），不要写相对路径。
- `translations/`（en 译文）、`resources/app.qss`（全局样式）、`scripts/`（打包脚本
  与模拟器）、`宣传图片/`（宣传素材，非代码）。

## 关键约束

- **设置 key**：QSettings key 字符串只允许出现在 `AppSettings.h/.cpp`（org=ST072G，
  app=ModbusTools）；其他模块一律走类型化接口。含自 WeightTool 遗留设置的一次性迁移
  （`migrateLegacyIfNeeded`），改动 key schema 时注意兼容。
- **UI 设计令牌**：颜色/字号/间距一律引用 `Theme.h` 常量，禁止页面里写裸色值；
  字号一律 px；浅色主题锁定，勿加暗色分支。
- **i18n**：源语言中文，English 经 `translations/ModbusTools_en.ts`。改了 UI 文案后需
  跑 lupdate 更新 .ts 补译，再常规构建（lrelease 随构建执行，.qm 自动拷到输出目录）。
- **版本号**：`project(VERSION)` 是唯一来源，经 `MODBUSTOOLS_VERSION` 宏与 app.rc /
  Info.plist 注入；改版本只动根 CMakeLists 一处。
- **许可红线**：Qt 仅以 LGPL-3 动态链接（允许闭源商业分发），**禁止引入 GPL-only
  模块**（Qt Charts 曾因此移除）；分发物必须随附 `LICENSES/`。
- **工作区 JSON**：当前 v3 格式；加载必须向后兼容 v1/v2 及 WeightTool 时代文件
  （不校验 `app` 标签），改 schema 时保持兼容并补序列化测试。
- **Windows 资源**：`app.rc` 由 `app.rc.in` configure 生成且必须挂到目标源列表
  （曾因只生成未挂导致 exe 无图标、版本属性空白）。

## 代码索引

仓库根有 `.codegraph/` 索引，查找/理解代码优先用 CodeGraph（`codegraph explore` /
MCP 工具），再考虑 grep/读文件。
