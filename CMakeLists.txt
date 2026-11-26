cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)
project(FoxSgfsDownloadMax LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")

add_executable(FoxSgfsDownloadMax WIN32
    src/main.cpp
    src/http.cpp
    src/fox.cpp
    src/util.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_win32.cpp
    ${IMGUI_DIR}/imgui_impl_dx11.cpp
    assets/app.rc
)

target_include_directories(FoxSgfsDownloadMax PRIVATE
    src
    ${IMGUI_DIR}
    third_party
)

target_compile_definitions(FoxSgfsDownloadMax PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
)

if (MSVC)
    target_compile_options(FoxSgfsDownloadMax PRIVATE /utf-8 /W3 /permissive-)
    target_link_options(FoxSgfsDownloadMax PRIVATE /MANIFEST:EMBED)
endif()

target_link_libraries(FoxSgfsDownloadMax PRIVATE
    d3d11
    dxgi
    d3dcompiler
    winhttp
    dwmapi
    shell32
    ole32
    comctl32
)

set_target_properties(FoxSgfsDownloadMax PROPERTIES
    OUTPUT_NAME "FoxSgfsDownload-Max"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

add_executable(FoxSgfsDownloadMaxBench
    src/bench.cpp
    src/http.cpp
    src/fox.cpp
    src/util.cpp
)
target_include_directories(FoxSgfsDownloadMaxBench PRIVATE
    src
    third_party
)
target_compile_definitions(FoxSgfsDownloadMaxBench PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
)
if (MSVC)
    target_compile_options(FoxSgfsDownloadMaxBench PRIVATE /utf-8 /W3 /permissive-)
endif()
target_link_libraries(FoxSgfsDownloadMaxBench PRIVATE winhttp)
set_target_properties(FoxSgfsDownloadMaxBench PROPERTIES
    OUTPUT_NAME "FoxSgfsDownloadMaxBench"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)


