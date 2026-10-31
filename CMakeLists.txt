cmake_minimum_required(VERSION 3.20)
set(PROJECT_NAME "BilibiliDown")

# ============================================================
# [1] Toolchain Setup (LLVM clang + lld, C++23, Qt6)
# ============================================================
# 面向 CLion 环境：默认使用 llvm-mingw (clang 22) + lld 链接器
# Qt 工具链: D:/MainAppx/Qt (6.11.1 / llvm-mingw_64)
set(TOOLCHAIN_BIN_DIR "D:/MainAppx/llvm/llvm-mingw-20260616-ucrt-x86_64/bin")
if(NOT CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER "${TOOLCHAIN_BIN_DIR}/clang++.exe")
    set(CMAKE_C_COMPILER    "${TOOLCHAIN_BIN_DIR}/clang.exe")
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# LLVM lld 链接器
set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=lld")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -fuse-ld=lld")

# C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt6 (llvm-mingw_64 构建, 与 clang 工具链 ABI 匹配)
if(NOT CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "D:/MainAppx/Qt/6.11.1/llvm-mingw_64")
endif()

project(${PROJECT_NAME} VERSION 1.0.0 LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui Network Concurrent)

# --- [2] 两个 API 链接库 ---
#   api : 外置库（HTTP/JSON/Crypto/Ffmpeg/Config 等基础能力）
#   code: 核心代码（B站解析/下载/合集合并）+ GUI
add_subdirectory(api)
add_subdirectory(code)

# --- [3] 生成 .qm 翻译文件 ---
find_program(LRELEASE_EXE lrelease HINTS "${Qt6_Core_DIR}/../bin")
file(GLOB TS_FILES "assets/i18n/*.ts")
set(GENERATED_QM_FILES "")
foreach(TS_FILE ${TS_FILES})
    get_filename_component(ABS_TS "${TS_FILE}" REALPATH)
    get_filename_component(DIR_NAME "${ABS_TS}" DIRECTORY)
    get_filename_component(FILE_WE "${ABS_TS}" NAME_WE)
    set(OUT_QM "${DIR_NAME}/${FILE_WE}.qm")
    add_custom_command(
            OUTPUT "${OUT_QM}"
            COMMAND ${LRELEASE_EXE} "${ABS_TS}"
            DEPENDS "${ABS_TS}"
            COMMENT "Compiling TS to QM: ${ABS_TS} -> ${OUT_QM}"
    )
    list(APPEND GENERATED_QM_FILES "${OUT_QM}")
endforeach()

# --- [4] 主可执行文件 ---
set(PROJECT_SOURCES
        main.cpp
        assets/res.qrc
        appicon.rc
        ${GENERATED_QM_FILES}
)
if(WIN32)
    # 控制台 + GUI 双模式: 调试期保留控制台输出
    add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})
else()
    add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# --- [5] Linking ---
target_link_libraries(${PROJECT_NAME} PRIVATE
        api
        code
        Qt6::Core
        Qt6::Widgets
        Qt6::Gui
        Qt6::Network
        Qt6::Concurrent
        advapi32
        shell32
)

if(WIN32)
    if(MSVC)
        target_link_options(${PROJECT_NAME} PRIVATE)
    else()
        # GUI 应用 (无控制台窗口); MinGW 运行时提供 WinMain->main 桥接
        target_link_options(${PROJECT_NAME} PRIVATE -mwindows)
    endif()
endif()

# --- [6] Deployment: windeployqt ---
find_program(WINDEPLOYQT windeployqt HINTS "${Qt6_Core_DIR}/../bin")
if(WINDEPLOYQT)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${WINDEPLOYQT} --no-translations "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.exe"
            COMMENT "Deploying Qt runtime dependencies..."
    )
endif()

# 部署 ffmpeg (音视频合并/合集合并依赖) 到 程序目录/bin/
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/bin"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/bin/ffmpeg.exe"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/bin/ffmpeg.exe"
        COMMENT "Deploying ffmpeg.exe..."
)

# 复制翻译文件到构建目录
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/i18n"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/i18n"
        COMMENT "Copying translation files..."
)
