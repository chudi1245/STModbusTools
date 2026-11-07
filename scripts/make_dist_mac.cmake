# ============================================================================
# make_dist_mac.cmake — 组装 macOS 发布 bundle
#   macdeployqt (Qt 框架/插件入 bundle) → 清除不可用可选插件 → 深签名
#   (内→外: 框架/插件无 entitlements, 主程序可选带沙盒 entitlements;
#    codesign 校验通过才继续) → hdiutil 出 DMG
# 用法 (由 CMake dist / dist-mas 目标驱动, 勿手动调用):
#   cmake -DBUNDLE=<STModbusTools.app> -DMACDEPLOYQT=<路径> -DOUT_DMG=<dmg>
#         [-DENTITLEMENTS=<plist>] [-DIDENTITY=<签名身份>] -P make_dist_mac.cmake
# 签名级别: 默认 ad-hoc (无证书)。ENTITLEMENTS 提供时主程序带沙盒
#   entitlements (Mac App Store / 本地沙盒验证用)。IDENTITY 用于正式
#   MAS 提交 ("3rd Party Mac Developer Application: …"), MAS 签名按苹果
#   要求不带安全时间戳 (--timestamp=none)。免 Gatekeeper 提示的
#   Developer ID 分发走 dist 目标 (无沙盒) + 公证, 另行接入。
# ============================================================================

if(NOT BUNDLE OR NOT MACDEPLOYQT OR NOT OUT_DMG)
    message(FATAL_ERROR "需要 -DBUNDLE=<.app> -DMACDEPLOYQT=<路径> -DOUT_DMG=<dmg>")
endif()

if(NOT IDENTITY)
    set(IDENTITY "-") # ad-hoc
endif()
if(IDENTITY STREQUAL "-")
    set(_TS_ARGS "") # ad-hoc 无时间戳概念
else()
    set(_TS_ARGS "--timestamp=none") # 苹果要求: MAS 分发签名不带安全时间戳
endif()

# ---- 1) Qt 框架/插件进 bundle ----
execute_process(
    COMMAND "${MACDEPLOYQT}" "${BUNDLE}" -always-overwrite
    RESULT_VARIABLE _rc
    COMMAND_ERROR_IS_FATAL ANY)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "macdeployqt failed: ${_rc}")
endif()

# ---- 2) 删除依赖未被部署框架的可选插件 (保留会让 Qt 插件扫描告警/签名存疑) ----
#   应用图标全部 QPainter 自绘 (src/Icons.h), 无 SVG/PDF/虚拟键盘依赖
set(_DROP_PLUGINS
    iconengines/libqsvgicon.dylib
    imageformats/libqpdf.dylib
    platforminputcontexts/libqtvirtualkeyboardplugin.dylib)
foreach(_p ${_DROP_PLUGINS})
    if(EXISTS "${BUNDLE}/Contents/PlugIns/${_p}")
        file(REMOVE "${BUNDLE}/Contents/PlugIns/${_p}")
        message(STATUS "dropped optional plugin: ${_p}")
    endif()
endforeach()

# ---- 2b) 清除孤儿框架 ----
# 虚拟键盘插件是唯一引用 Qml 系的; 删插件后它们只是死重。
# 注意 QtDBus 不能删: QtGui 硬依赖它。
set(_DROP_FRAMEWORKS
    QtQml QtQmlMeta QtQmlModels QtQmlWorkerScript QtQuick)
foreach(_f ${_DROP_FRAMEWORKS})
    if(EXISTS "${BUNDLE}/Contents/Frameworks/${_f}.framework")
        file(REMOVE_RECURSE "${BUNDLE}/Contents/Frameworks/${_f}.framework")
        message(STATUS "dropped orphan framework: ${_f}")
    endif()
endforeach()

# ---- 3) 部署完整性自检 ----
# 增量重链会把主程序 Qt 依赖退回 /opt/homebrew 绝对路径 (双 Qt 同进程, 启动即
# SIGABRT); macdeployqt 失败或被跳过时在这里拦截
set(_exe "STModbusTools")
execute_process(
    COMMAND otool -L "${BUNDLE}/Contents/MacOS/${_exe}"
    OUTPUT_VARIABLE _otool_out)
foreach(_bad "/opt/homebrew" "/usr/local")
    if(_otool_out MATCHES "${_bad}")
        message(FATAL_ERROR "主程序仍依赖 ${_bad} — macdeployqt 未生效, bundle 会混入两套 Qt")
    endif()
endforeach()

# ---- 4) 深签名 (内→外) + 校验 ----
# arm64 macOS 不执行无签名的 Mach-O; macdeployqt 的签名顺序对第三方 dylib
# (brotli 等) 会留下失效子签名, 这里整体重签兜底。
# --deep + --entitlements 会把 entitlements 撒到嵌套代码上 (框架不该带),
# 故手工按 内层框架 → 插件 → 主程序 顺序签, 只有主程序带 entitlements。
file(GLOB _frameworks "${BUNDLE}/Contents/Frameworks/*.framework")
file(GLOB _helpers "${BUNDLE}/Contents/Frameworks/*.dylib")
file(GLOB_RECURSE _plugins "${BUNDLE}/Contents/PlugIns/*.dylib")
foreach(_nested IN LISTS _frameworks _helpers _plugins)
    execute_process(
        COMMAND codesign --force --sign "${IDENTITY}" ${_TS_ARGS} "${_nested}"
        RESULT_VARIABLE _rc
        COMMAND_ERROR_IS_FATAL ANY)
endforeach()

set(_APP_SIGN_ARGS --force --sign "${IDENTITY}" ${_TS_ARGS})
if(ENTITLEMENTS)
    list(APPEND _APP_SIGN_ARGS --entitlements "${ENTITLEMENTS}")
endif()
execute_process(
    COMMAND codesign ${_APP_SIGN_ARGS} "${BUNDLE}"
    RESULT_VARIABLE _rc
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(
    COMMAND codesign --verify --deep --strict "${BUNDLE}"
    RESULT_VARIABLE _rc
    COMMAND_ERROR_IS_FATAL ANY)
if(ENTITLEMENTS)
    execute_process(
        COMMAND codesign --display --entitlements :- "${BUNDLE}"
        OUTPUT_VARIABLE _ent_out)
    if(NOT _ent_out MATCHES "com.apple.security.app-sandbox")
        message(FATAL_ERROR "签名未携带 app-sandbox entitlements: ${ENTITLEMENTS}")
    endif()
    message(STATUS "codesign ok (sandbox entitlements): ${BUNDLE}")
else()
    message(STATUS "codesign ok (no entitlements): ${BUNDLE}")
endif()

# ---- 5) DMG (UDZO 压缩; 已存在则覆盖) ----
execute_process(
    COMMAND hdiutil create -volname "STModbusTools"
            -srcfolder "${BUNDLE}" -ov -format UDZO "${OUT_DMG}"
    RESULT_VARIABLE _rc
    COMMAND_ERROR_IS_FATAL ANY)

message(STATUS "dist assembled: ${OUT_DMG}")
