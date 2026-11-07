# ============================================================================
# make_dist.cmake — 组装发布目录 (build/dist)
#   windeployqt (Qt/插件/编译器运行时 DLL) + 应用 exe + 许可随附 (合规必需)
#   + 英文翻译
# 用法 (由 CMake dist 目标驱动, 勿手动调用):
#   cmake -DEXE=... -DWINDEPLOYQT=... -DSRC_DIR=... -DBIN_DIR=... -P make_dist.cmake
# ============================================================================

set(DIST_DIR "${BIN_DIR}/dist")
file(REMOVE_RECURSE "${DIST_DIR}")
file(MAKE_DIRECTORY "${DIST_DIR}")

file(COPY_FILE "${EXE}" "${DIST_DIR}/STModbusTools.exe")

# Qt DLL + 插件 (platforms/styles/imageformats/serialbus/tls...)
# + MinGW 编译器运行时 (libgcc/libstdc++/winpthread)
execute_process(
    COMMAND "${WINDEPLOYQT}" --release --compiler-runtime --dir "${DIST_DIR}"
            "${DIST_DIR}/STModbusTools.exe"
    RESULT_VARIABLE _rc
    COMMAND_ERROR_IS_FATAL ANY)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "windeployqt failed: ${_rc}")
endif()

# 许可随附: LGPL-3 合规要求分发物中包含许可文本与第三方声明
file(COPY "${SRC_DIR}/LICENSES" DESTINATION "${DIST_DIR}")

# 英文翻译
file(COPY "${BIN_DIR}/ModbusTools_en.qm" DESTINATION "${DIST_DIR}")

message(STATUS "dist assembled: ${DIST_DIR}")
