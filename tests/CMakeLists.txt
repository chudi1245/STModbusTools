# ============================================================================
# tests/ — Modbus 调试工具测试: Test Center 纯逻辑 + 显示格式 + 定义序列化 +
#   表格模型 + 主窗/Test Center 离屏冒烟 + 帧日志/TCP 回环 + 实机自动化 (可选)
#   ctest --test-dir build 即可运行
# ============================================================================
find_package(Qt6 REQUIRED COMPONENTS Test SerialBus SerialPort)

add_executable(tst_modbustools
    tst_modbustools.cpp
    ../src/AppSettings.cpp
    ../src/AppSettings.h
    ../src/CommStatsCollector.cpp
    ../src/CommStatsCollector.h
    ../src/CommTypes.h
    ../src/FrameLogDialog.cpp
    ../src/FrameLogDialog.h
    ../src/ModbusClient.cpp
    ../src/ModbusClient.h
    ../src/ModbusTransport.h
    ../src/tools/DataViewWidget.cpp
    ../src/tools/DataViewWidget.h
    ../src/tools/DefDialogs.h
    ../src/tools/ConnectionManager.cpp
    ../src/tools/ConnectionManager.h
    ../src/tools/DeviceTreeWidget.cpp
    ../src/tools/DeviceTreeWidget.h
    ../src/tools/MasterClient.cpp
    ../src/tools/MasterClient.h
    ../src/tools/ModbusFormat.cpp
    ../src/tools/ModbusFormat.h
    ../src/tools/ModbusMasterTool.cpp
    ../src/tools/ModbusMasterTool.h
    ../src/tools/PollDefinition.cpp
    ../src/tools/PollDefinition.h
    ../src/tools/PollTableModel.cpp
    ../src/tools/PollTableModel.h
    ../src/tools/ScanDialog.cpp
    ../src/tools/ScanDialog.h
    ../src/tools/SlavePoller.cpp
    ../src/tools/SlavePoller.h
    ../src/tools/SlaveSetupDialog.h
    ../src/tools/TestCenterCore.h
    ../src/tools/TestCenterPage.cpp
    ../src/tools/TestCenterPage.h
    ../src/tools/ToolFrames.cpp
    ../src/tools/ToolFrames.h)
target_include_directories(tst_modbustools PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
                                                   ${CMAKE_CURRENT_SOURCE_DIR}/../src/tools)
# 复用主程序的源文件 (ModbusMasterTool 用 MODBUSTOOLS_VERSION 拼标题/关于框)
target_compile_definitions(tst_modbustools PRIVATE
    MODBUSTOOLS_VERSION="${PROJECT_VERSION}")
target_link_libraries(tst_modbustools PRIVATE
    Qt6::Test Qt6::Widgets Qt6::SerialBus Qt6::SerialPort)
add_test(NAME modbustools COMMAND tst_modbustools)
