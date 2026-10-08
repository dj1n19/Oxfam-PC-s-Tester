cmake_minimum_required(VERSION 3.21)
project(OxfamPcTester VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # for clangd (Pulsar)
set(CMAKE_AUTOMOC ON)                   # Qt's moc runs automatically

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Concurrent Multimedia)   # Multimedia: app only (audio test)

function(ox_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# oxcore: Core + diagnostic tests. Links QtCore and QtConcurrent ONLY,
# so it cannot use any widget: the "Core never knows the UI" rule is
# enforced by the build system.
# ---------------------------------------------------------------------------
add_library(oxcore STATIC
    src/core/TestResult.h
    src/core/ITest.h            # listed so that moc processes it
    src/core/Verdict.h
    src/core/Thresholds.h
    src/core/Thresholds.cpp
    src/core/TestRunner.h
    src/core/TestRunner.cpp
    src/tests/battery/BatteryInfo.h
    src/tests/battery/BatteryTest.h
    src/tests/battery/BatteryTest.cpp
    src/tests/battery/BatteryReport.cpp   # parses Windows powercfg XML; plain Qt, unit tested on Linux too
    src/tests/sysinfo/SystemInfo.h
    src/tests/sysinfo/SystemInfoTest.h
    src/tests/sysinfo/SystemInfoTest.cpp
    src/tests/disk/DiskInfo.h
    src/tests/disk/Smartctl.cpp           # same smartctl command line on both OSes: no _win/_linux
    src/tests/disk/DiskTest.h
    src/tests/disk/DiskTest.cpp
    src/tests/drivers/DriverInfo.h
    src/tests/drivers/DriverParsing.cpp   # pure helpers for both OSes, unit tested on Linux
    src/tests/drivers/DriversTest.h
    src/tests/drivers/DriversTest.cpp
    src/tests/license/LicenseInfo.h
    src/tests/license/LicenseParsing.cpp   # pure, unit tested on Linux
    src/tests/license/LicenseTest.h
    src/tests/license/LicenseTest.cpp
    src/tests/keyboard/KeyLayout.h
    src/tests/keyboard/KeyLayout.cpp      # layout, scan codes, result: logic of the keyboard test
    src/tests/audio/AudioCheck.h
    src/tests/audio/AudioCheck.cpp        # tone generation + result: logic of the audio test
)

# OS-specific implementations (readBattery(), readSystemInfo()...), chosen here. No #ifdef in code.
if(WIN32)
    target_sources(oxcore PRIVATE
        src/tests/battery/battery_win.cpp
        src/tests/sysinfo/sysinfo_win.cpp
        src/tests/drivers/drivers_win.cpp
        src/tests/license/license_win.cpp
        src/tests/common/PowerShell.h
        src/tests/common/powershell_win.cpp    # shared runPowerShell()
        src/tests/keyboard/keyboard_win.cpp)
else()
    target_sources(oxcore PRIVATE
        src/tests/battery/battery_linux.cpp
        src/tests/sysinfo/sysinfo_linux.cpp
        src/tests/drivers/drivers_linux.cpp
        src/tests/license/license_linux.cpp
        src/tests/keyboard/keyboard_linux.cpp)
endif()

target_include_directories(oxcore PUBLIC src)
target_link_libraries(oxcore PUBLIC Qt6::Core Qt6::Concurrent)
ox_enable_warnings(oxcore)

# ---------------------------------------------------------------------------
# The application (UI layer)
# ---------------------------------------------------------------------------
add_executable(oxfam-tester
    src/main.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    # Interactive tests: ITest + dialog here (they need widgets), logic in oxcore.
    src/ui/interactive/KeyboardTest.h
    src/ui/interactive/KeyboardTest.cpp
    src/ui/interactive/KeyboardDialog.h
    src/ui/interactive/KeyboardDialog.cpp
    src/ui/interactive/AudioTest.h
    src/ui/interactive/AudioTest.cpp
    src/ui/interactive/AudioDialog.h
    src/ui/interactive/AudioDialog.cpp
)
target_link_libraries(oxfam-tester PRIVATE oxcore Qt6::Widgets Qt6::Multimedia)
ox_enable_warnings(oxfam-tester)

# Config files are copied next to the executable (portable app).
# One command per file, with the full destination path: copying several
# files at once needs the folder to exist, and on a fresh Windows build
# build/Release does not exist yet when this runs.
add_custom_target(copy_config ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/config/thresholds.json
            $<TARGET_FILE_DIR:oxfam-tester>/thresholds.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/config/keyboard_layout.json
            $<TARGET_FILE_DIR:oxfam-tester>/keyboard_layout.json)
add_dependencies(oxfam-tester copy_config)

# ---------------------------------------------------------------------------
# Unit tests (ctest)
# ---------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(unittests)
endif()
