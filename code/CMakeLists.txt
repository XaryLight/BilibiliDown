# code 库: B站核心逻辑(解析/下载/合集合并) + Qt GUI
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(
        Qt6
        REQUIRED
        COMPONENTS
        Gui
        Core
        Widgets
        Network
        Concurrent
)

# 自动收集 code 目录及其子目录下的所有源文件
file(GLOB_RECURSE CODE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.ui"
)

# 编译为名为 "code" 的静态库
add_library(code STATIC ${CMAKE_CURRENT_SOURCE_DIR} ${CODE_SOURCES})

target_include_directories(code
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/model
        ${CMAKE_CURRENT_SOURCE_DIR}/parser
        ${CMAKE_CURRENT_SOURCE_DIR}/downloader
        ${CMAKE_CURRENT_SOURCE_DIR}/merge
        ${CMAKE_CURRENT_SOURCE_DIR}/util
        ${CMAKE_CURRENT_SOURCE_DIR}/ui
)

target_link_libraries(code PUBLIC
        api
        Qt6::Core
        Qt6::Widgets
        Qt6::Gui
        Qt6::Network
        Qt6::Concurrent
)
