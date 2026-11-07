cmake_minimum_required(VERSION 3.21)
project(modbustools VERSION 1.0.0 LANGUAGES C CXX)

# ============================================================================
# STModbusTools — Modbus 主站调试工具 (独立程序)
#   自 WeightTool (称重测力调试软件) 拆分: 原 src/tools 调试工具内核 +
#   共享通信层 (ModbusClient/CommStatsCollector/FrameLogDialog) 整体迁入。
#   功能对标 Modbus Poll: RTU/TCP 多从站轮询、定义表格、Test Center、
#   从站扫描、帧日志。
# ============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets SerialBus SerialPort LinguistTools)

qt_standard_project_setup()

if(WIN32)
    # app.rc 由模板生成: 注入 project(VERSION) 到 VERSIONINFO (资源管理器属性页展示)
    set(MBTOOLS_ICO "${CMAKE_CURRENT_SOURCE_DIR}/icon/modbustools.ico")
    configure_file(app.rc.in app.rc @ONLY)
else()
    set(MBTOOLS_ICO "")
endif()

qt_add_executable(STModbusTools
    resources.qrc
    # 入口 + 设置
    src/main.cpp
    src/AppSettings.h src/AppSettings.cpp
    # 共享通信层 (LuaPorts.h: ModbusClient 实现的寄存器读写窄接口, 无 Lua 依赖)
    src/CommTypes.h
    src/LuaPorts.h
    src/ModbusTransport.h
    src/ModbusClient.h src/ModbusClient.cpp
    src/CommStatsCollector.h src/CommStatsCollector.cpp
    src/FrameLogDialog.h src/FrameLogDialog.cpp
    # 观感基座
    src/Theme.h
    src/Icons.h
    src/BtnStyles.h
    # 调试工具内核
    src/tools/ToolUi.h
    src/tools/ToolFrames.h src/tools/ToolFrames.cpp
    src/tools/ModbusFormat.h src/tools/ModbusFormat.cpp
    src/tools/PollDefinition.h src/tools/PollDefinition.cpp
    src/tools/PollTableModel.h src/tools/PollTableModel.cpp
    src/tools/MasterClient.h src/tools/MasterClient.cpp
    src/tools/ConnectionManager.h src/tools/ConnectionManager.cpp
    src/tools/SlavePoller.h src/tools/SlavePoller.cpp
    src/tools/DataViewWidget.h src/tools/DataViewWidget.cpp
    src/tools/DefDialogs.h
    src/tools/TestCenterCore.h
    src/tools/ConnDialog.h
    src/tools/DefDialog.h
    src/tools/SlaveSetupDialog.h
    src/tools/DeviceTreeWidget.h src/tools/DeviceTreeWidget.cpp
    src/tools/ModbusWorkspace.h
    src/tools/TestCenterPage.h src/tools/TestCenterPage.cpp
    src/tools/ScanDialog.h src/tools/ScanDialog.cpp
    src/tools/ModbusMasterTool.h src/tools/ModbusMasterTool.cpp
)
if(WIN32)
    # 图标 + VERSIONINFO 随 exe (configure_file 生成于 build/app.rc);
    # 曾因只生成未挂目标, exe 无图标且属性页版本号空白
    target_sources(STModbusTools PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()
target_link_libraries(STModbusTools PRIVATE
    Qt6::Widgets
    Qt6::SerialBus
    Qt6::SerialPort
)

# src/ 与 src/tools/ 直接以文件名互相引用头文件
target_include_directories(STModbusTools PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tools)

# 版本号进 C++ (窗口标题抬头 / 关于框) — 与 app.rc VERSIONINFO 同源 project(VERSION)
target_compile_definitions(STModbusTools PRIVATE
    MODBUSTOOLS_VERSION="${PROJECT_VERSION}")

set_target_properties(STModbusTools PROPERTIES
    WIN32_EXECUTABLE ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# macOS 平台独有: .app bundle (Info.plist + icns) 与 dist 目标 (macdeployqt + DMG)
# brew Qt 为 arm64、最低 macOS 14 — 产物命名/LSMinimumSystemVersion 与之对齐
if(APPLE)
    set_target_properties(STModbusTools PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.st072g.modbustools"
        MACOSX_BUNDLE_BUNDLE_NAME "STModbusTools"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "modbustools.icns")

    # icns → bundle Contents/Resources
    set(MBTOOLS_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/icon/modbustools.icns")
    set_source_files_properties("${MBTOOLS_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(STModbusTools PRIVATE "${MBTOOLS_ICNS}")

    # dist: macdeployqt 把 Qt 框架/插件打进 bundle, ad-hoc 签名, 出 DMG。
    # 无 Developer ID: 接收方首次打开需右键 → 打开 (公证版发布另行接入)
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(NOT MACDEPLOYQT_EXECUTABLE)
        message(FATAL_ERROR "未找到 macdeployqt (Qt 安装的 bin 目录)")
    endif()
    # bundle 完整路径 (RUNTIME_OUTPUT_DIRECTORY/bin 下, 名字 = 目标名.app)。
    # 不用 $<TARGET_FILE>/../.. 拼接: macdeployqt 不规范化路径, 拼接串会被
    # 当成 bundle 路径原样使用, 导致 Contents/Contents 连锁失败
    set(MBTOOLS_BUNDLE "${CMAKE_BINARY_DIR}/bin/STModbusTools.app")

    # Info.plist 自行展开 + POST_BUILD 兜底拷贝: Qt/CMake 的 bundle 规则只在
    # 首次全量构建写 plist, bundle 目录被清后的增量重链不会重写 (codesign 也不拦),
    # dist 会把缺 plist 的 app 打进 DMG
    set(MACOSX_BUNDLE_EXECUTABLE_NAME "STModbusTools")
    set(MACOSX_BUNDLE_ICON_FILE "modbustools.icns")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.st072g.modbustools")
    set(MACOSX_BUNDLE_BUNDLE_NAME "STModbusTools")
    set(MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    set(MBTOOLS_PLIST "${CMAKE_CURRENT_BINARY_DIR}/STModbusTools_Info.plist")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in" "${MBTOOLS_PLIST}")
    add_custom_command(TARGET STModbusTools POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MBTOOLS_PLIST}"
                "${MBTOOLS_BUNDLE}/Contents/Info.plist")

    # 许可随附: LGPL-3 合规要求许可文本随分发物 (bundle → Contents/Resources)
    add_custom_command(TARGET STModbusTools POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/LICENSES
                $<TARGET_FILE_DIR:STModbusTools>/../Resources/LICENSES)

    add_custom_target(dist
        COMMAND ${CMAKE_COMMAND}
            -DBUNDLE=${MBTOOLS_BUNDLE}
            -DMACDEPLOYQT=${MACDEPLOYQT_EXECUTABLE}
            -DOUT_DMG=${CMAKE_BINARY_DIR}/STModbusTools-${PROJECT_VERSION}-macos-arm64.dmg
            -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/make_dist_mac.cmake
        DEPENDS STModbusTools
        COMMENT "Assembling STModbusTools.app (macdeployqt + ad-hoc codesign) → DMG")
endif()

# ---- 国际化: 源语言为中文, English 经 ModbusTools_en.qm 提供 ----
# 改 UI 文案后: lupdate src -ts translations/ModbusTools_en.ts 补译新串,
# 再 cmake --build build --target release_translations (常规构建亦会执行)
set(MBTOOLS_PAYLOAD_DIR "$<TARGET_FILE_DIR:STModbusTools>")
if(APPLE)
    string(APPEND MBTOOLS_PAYLOAD_DIR "/../Resources")
endif()
qt_add_translations(STModbusTools
    TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/translations/ModbusTools_en.ts
    QM_FILES_OUTPUT_VARIABLE QM_FILES
    LRELEASE_TARGET stmodbustools_lrelease)
# 新签名下宏不给主目标挂 lrelease 依赖, 翻译目标只进 all 列表 — 构建顺序存在
# 竞态 (POST_BUILD 拷贝 .qm 可能先于 lrelease 执行). 显式声明依赖消除竞态
add_dependencies(STModbusTools stmodbustools_lrelease)
add_custom_command(TARGET STModbusTools POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MBTOOLS_PAYLOAD_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${QM_FILES}
            ${MBTOOLS_PAYLOAD_DIR})

# ---- 发布目录组装 (build/dist): windeployqt + 许可随附 ----
# Qt LGPL/GPL 合规要求许可文本随分发物提供
# 仅 Windows 平台提供 dist 目标 (使用 windeployqt); macOS 用 macdeployqt 不在这里拼装
if(WIN32)
    if(TARGET Qt6::windeployqt)
        set(_WINDEPLOYQT $<TARGET_FILE:Qt6::windeployqt>)
    else()
        find_program(_WINDEPLOYQT windeployqt HINTS "${QT_HOST_PREFIX}/bin")
    endif()
    add_custom_target(dist
        COMMAND ${CMAKE_COMMAND}
            -DEXE=$<TARGET_FILE:STModbusTools>
            -DWINDEPLOYQT=${_WINDEPLOYQT}
            -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DBIN_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/make_dist.cmake
        DEPENDS STModbusTools
        COMMENT "Assembling distributable (windeployqt + LICENSES) → build/dist")
endif()

# Release 关闭控制台, Debug 保留便于排障
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(STModbusTools PROPERTIES WIN32_EXECUTABLE OFF)
endif()

# ============================================================================
# 安装/打包 (CPack, 仅 Windows): ZIP 与 NSIS 安装包
#   发布: cmake --build build --target all dist && cd build && cpack
# ============================================================================
if(WIN32)
    install(TARGETS STModbusTools RUNTIME DESTINATION .)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ModbusTools_en.qm" DESTINATION .)
    install(DIRECTORY LICENSES DESTINATION .)
    # Qt 运行时随装: 与 dist 目标同款 windeployqt 平铺布局 (Qt DLL/插件贴着 exe,
    # 含 MinGW 编译器运行时)
    install(CODE "
        execute_process(
            COMMAND \"${_WINDEPLOYQT}\" --release --compiler-runtime
                    --dir \${CMAKE_INSTALL_PREFIX} \${CMAKE_INSTALL_PREFIX}/STModbusTools.exe
            RESULT_VARIABLE _ws_rc
            COMMAND_ERROR_IS_FATAL ANY)")

    set(CPACK_PACKAGE_NAME "STModbusTools")
    set(CPACK_PACKAGE_VENDOR "ST072G")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "ST072G STModbusTools")
    set(CPACK_PACKAGE_FILE_NAME "STModbusTools-${PROJECT_VERSION}-win64")
    # 快捷方式/开始菜单显示名用软件名; exe 本体为 STModbusTools.exe
    set(CPACK_PACKAGE_EXECUTABLES "STModbusTools" "STModbusTools")
    set(CPACK_NSIS_DISPLAY_NAME "STModbusTools")
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "STModbusTools.exe")
    find_program(NSIS_PROGRAM makensis)
    if(NSIS_PROGRAM)
        set(CPACK_GENERATOR "ZIP;NSIS")
    else()
        message(STATUS "未找到 makensis: CPack 仅生成 ZIP")
        set(CPACK_GENERATOR "ZIP")
    endif()
    include(CPack)
endif()

enable_testing()
add_subdirectory(tests)
