cmake_minimum_required(VERSION 3.25)
project(gw_present_lab LANGUAGES CXX)

# A standalone lab for GUI.Forms handoff item 14 (Windows presentation
# surfaces). It links Direct3D 11, DXGI and DirectComposition to probe them;
# nothing here is part of GUI.Forms.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(present_lab
    src/main.cpp
    src/lab_common.cpp
    src/gpu.cpp
    src/route_gdi.cpp
    src/route_dxgi.cpp
    src/route_dcomp.cpp
    src/route_presentation.cpp
    src/route_winrt.cpp)
target_compile_definitions(present_lab PRIVATE UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00
    NTDDI_VERSION=0x0A00000F)
target_compile_options(present_lab PRIVATE -Wall -Wno-unused-parameter -Wno-nonportable-include-path)
target_link_options(present_lab PRIVATE -static)
target_link_libraries(present_lab PRIVATE d3d11 dxgi dcomp dwmapi uxtheme runtimeobject coremessaging gdi32 user32
    ole32)
