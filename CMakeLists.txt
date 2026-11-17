cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)
project(FoxKifuStudio LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")

add_executable(FoxKifuStudio WIN32
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

target_include_directories(FoxKifuStudio PRIVATE
    src
    ${IMGUI_DIR}
    third_party
)

target_compile_definitions(FoxKifuStudio PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
)

if (MSVC)
    target_compile_options(FoxKifuStudio PRIVATE /utf-8 /W3 /permissive-)
    target_link_options(FoxKifuStudio PRIVATE /MANIFEST:EMBED)
endif()

target_link_libraries(FoxKifuStudio PRIVATE
    d3d11
    dxgi
    d3dcompiler
    winhttp
    dwmapi
    shell32
    ole32
    comctl32
)

set_target_properties(FoxKifuStudio PROPERTIES
    OUTPUT_NAME "FoxKifuStudio"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

add_executable(FoxKifuBench
    src/bench.cpp
    src/http.cpp
    src/fox.cpp
    src/util.cpp
)
target_include_directories(FoxKifuBench PRIVATE
    src
    third_party
)
target_compile_definitions(FoxKifuBench PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
)
if (MSVC)
    target_compile_options(FoxKifuBench PRIVATE /utf-8 /W3 /permissive-)
endif()
target_link_libraries(FoxKifuBench PRIVATE winhttp)
set_target_properties(FoxKifuBench PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
