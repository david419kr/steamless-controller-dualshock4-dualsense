cmake_minimum_required(VERSION 3.20)
project(SteamlessController CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (MSVC)
    add_compile_options(/utf-8 /wd4828)
endif()

# Shared HID + Steam Controller library code
add_library(SteamController STATIC
    src/hid/HidDevice.cpp
    src/steam/SteamController.cpp
)
target_include_directories(SteamController PUBLIC src)
target_link_libraries(SteamController PUBLIC hid setupapi)
target_link_libraries(SteamController PUBLIC cfgmgr32)
if (MSVC)
    target_compile_options(SteamController PRIVATE /W4 /WX)
endif()

# Diagnostic probe tool (console)
add_executable(SteamProbe
    src/main.cpp
)
target_link_libraries(SteamProbe PRIVATE SteamController)
if (MSVC)
    target_compile_options(SteamProbe PRIVATE /W4 /WX)
    set_target_properties(SteamProbe PROPERTIES LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE")
endif()

# RawGameController diagnostic probe (WinRT)
add_executable(RawControllerProbe
    src/probe/RawControllerProbe.cpp
)
target_link_libraries(RawControllerProbe PRIVATE windowsapp)
if (MSVC)
    target_compile_options(RawControllerProbe PRIVATE /W4)
endif()

# System tray app (no console window)
add_executable(SteamlessController WIN32
    src/app/main.cpp
    src/app/TrayApp.cpp
    src/app/ControllerManager.cpp
    src/app/HidHideController.cpp
    src/app/VirtualController.cpp
    src/app/VirtualControllerTypes.cpp
    src/app/ViiperClient.cpp
    src/app/TrackpadMouse.cpp
    resources/app.rc
)
target_include_directories(SteamlessController PRIVATE resources)
target_link_libraries(SteamlessController PRIVATE SteamController ws2_32 comctl32 winhttp)
if (MSVC)
    target_compile_options(SteamlessController PRIVATE /W4 /WX)
endif()

find_program(POWERSHELL_EXECUTABLE NAMES powershell pwsh)
if (POWERSHELL_EXECUTABLE)
    add_custom_target(ViiperSidecar
        COMMAND ${POWERSHELL_EXECUTABLE}
            -NoProfile
            -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/tools/build-viiper.ps1"
            -OutputDir "$<TARGET_FILE_DIR:SteamlessController>"
        DEPENDS SteamlessController
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Building patched VIIPER sidecar"
        VERBATIM
    )

    add_custom_target(PackageInstaller
        COMMAND ${POWERSHELL_EXECUTABLE}
            -NoProfile
            -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/tools/build-installer.ps1"
            -SkipBuild
            -BuildDir "${CMAKE_BINARY_DIR}"
            -Configuration "$<CONFIG>"
        DEPENDS ViiperSidecar
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Building bundled installer"
        VERBATIM
    )
endif()

add_executable(SteamlessControllerTests
    src/tests/VirtualControllerTests.cpp
    src/app/VirtualControllerTypes.cpp
    src/app/ViiperClient.cpp
)
target_include_directories(SteamlessControllerTests PRIVATE src)
target_link_libraries(SteamlessControllerTests PRIVATE ws2_32)
if (MSVC)
    target_compile_options(SteamlessControllerTests PRIVATE /W4 /WX)
endif()
