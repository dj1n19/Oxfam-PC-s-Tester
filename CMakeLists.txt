cmake_minimum_required(VERSION 3.21)
project(OxfamPcTester VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # for clangd (Pulsar)
set(CMAKE_AUTOMOC ON)                   # Qt's moc runs automatically

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Concurrent)

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
)

# OS-specific implementations (readBattery(), readSystemInfo()...), chosen here. No #ifdef in code.
if(WIN32)
    target_sources(oxcore PRIVATE
        src/tests/battery/battery_win.cpp
        src/tests/sysinfo/sysinfo_win.cpp
        src/tests/drivers/drivers_win.cpp
        src/tests/license/license_win.cpp
        src/tests/common/PowerShell.h
        src/tests/common/powershell_win.cpp)   # shared runPowerShell()
else()
    target_sources(oxcore PRIVATE
        src/tests/battery/battery_linux.cpp
        src/tests/sysinfo/sysinfo_linux.cpp
        src/tests/drivers/drivers_linux.cpp
        src/tests/license/license_linux.cpp)
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
)
target_link_libraries(oxfam-tester PRIVATE oxcore Qt6::Widgets)
ox_enable_warnings(oxfam-tester)

# thresholds.json is copied next to the executable (portable app)
add_custom_target(copy_config ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/config/thresholds.json
            $<TARGET_FILE_DIR:oxfam-tester>/thresholds.json)
add_dependencies(oxfam-tester copy_config)

# ---------------------------------------------------------------------------
# Unit tests (ctest)
# ---------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(unittests)
endif()
