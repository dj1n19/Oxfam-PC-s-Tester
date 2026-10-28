cmake_minimum_required(VERSION 3.21)
project(OxfamPcTester VERSION 0.1.0 LANGUAGES C CXX)   # C: miniz

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # for clangd (Pulsar)
set(CMAKE_AUTOMOC ON)                   # Qt's moc runs automatically

# Shown in the window title and in every run report, so a report always says
# which build made it. CI passes the tag name (v1.0.0-beta.1) or dev-<commit>.
set(APP_VERSION "dev" CACHE STRING "Version shown to the technician")

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Concurrent Multimedia   # Multimedia: app only (audio, camera)
                                     LinguistTools)                        # lupdate/lrelease (translations)

function(ox_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# miniz: third-party zip library in C (third_party/miniz/README.md), used to
# edit the delivery sheet. Not our code and never edited: built without our
# warning flags, and SYSTEM so its header gives no warnings in our files.
# ---------------------------------------------------------------------------
add_library(miniz STATIC third_party/miniz/miniz.c third_party/miniz/miniz.h)
target_include_directories(miniz SYSTEM PUBLIC third_party/miniz)
if(MSVC)
    target_compile_options(miniz PRIVATE /w)
else()
    target_compile_options(miniz PRIVATE -w)
endif()

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
    src/core/TestChoice.h
    src/core/TestChoice.cpp               # config/tests.json: tests ticked at start
    src/core/TestRunner.h
    src/core/TestRunner.cpp
    src/core/ProgramFolder.h
    src/core/ProgramFolder.cpp            # the root folder: app/, config/, logs/, delivery-sheets/
    src/tests/battery/BatteryInfo.h
    src/tests/battery/BatteryTest.h
    src/tests/battery/BatteryTest.cpp
    src/tests/battery/ChargeTest.h
    src/tests/battery/ChargeTest.cpp
    src/tests/wireless/WirelessInfo.h
    src/tests/wireless/WifiTest.h
    src/tests/wireless/WifiTest.cpp
    src/tests/wireless/BluetoothTest.h
    src/tests/wireless/BluetoothTest.cpp
    src/tests/sysinfo/SystemInfo.h
    src/tests/sysinfo/SystemInfoTest.h
    src/tests/sysinfo/SystemInfoTest.cpp
    src/tests/sysinfo/Smbios.cpp          # parses the raw SMBIOS table (Windows); plain Qt, unit tested
    src/tests/sysinfo/SystemInfoParsing.cpp   # Windows version name, /proc/cpuinfo cores; unit tested
    src/tests/disk/DiskInfo.h
    src/tests/disk/Smartctl.cpp           # shared: scan, direct calls, pkexec batch helpers
    src/tests/disk/DiskTest.h
    src/tests/disk/DiskTest.cpp
    src/tests/drivers/DriverInfo.h
    src/tests/drivers/DriverParsing.cpp   # pure helpers for both OSes, unit tested on Linux
    src/tests/drivers/DriversTest.h
    src/tests/drivers/DriversTest.cpp
    src/tests/license/LicenseInfo.h
    src/tests/license/LicenseTest.h
    src/tests/license/LicenseTest.cpp
    src/tests/cputemp/CpuTempInfo.h
    src/tests/cputemp/CpuTempParsing.cpp  # parses the Windows ACPI zones JSON; pure, unit tested on Linux
    src/tests/cputemp/CpuTempTest.h
    src/tests/cputemp/CpuTempTest.cpp
    src/tests/keyboard/KeyLayout.h
    src/tests/keyboard/KeyLayout.cpp      # layout, scan codes, result: logic of the keyboard test
    src/tests/audio/AudioCheck.h
    src/tests/audio/AudioCheck.cpp        # tone generation + result: logic of the audio test
    src/tests/camera/FrameCheck.h
    src/tests/camera/FrameCheck.cpp       # frame statistics + result: logic of the camera test
    src/delivery/XlsxEditor.h
    src/delivery/XlsxEditor.cpp           # edits .xlsx cells in place (miniz)
    src/delivery/DeliverySheet.h
    src/delivery/DeliverySheet.cpp        # which cells of Oxfam's delivery sheet hold what
    src/report/RunReport.h
    src/report/RunReport.cpp              # every row's details in a text file after each run
)

# OS-specific implementations (readBattery(), readSystemInfo()...), chosen here. No #ifdef in code.
if(WIN32)
    target_sources(oxcore PRIVATE
        src/core/hidefile_win.cpp
        src/tests/battery/battery_win.cpp
        src/tests/sysinfo/sysinfo_win.cpp
        src/tests/drivers/drivers_win.cpp
        src/tests/license/license_win.cpp
        src/tests/cputemp/cputemp_win.cpp
        src/tests/keyboard/keyboard_win.cpp
        src/tests/wireless/wireless_win.cpp
        src/tests/disk/smartctl_win.cpp)
    # Windows system libraries (part of Windows, nothing to ship):
    # setupapi + cfgmgr32 = the device list and problem codes (drivers_win.cpp),
    # slc = the licensing service (license_win.cpp), pdh = performance counters (cputemp_win.cpp),
    # wlanapi = the Wi-Fi service, bthprops = the Bluetooth stack (wireless_win.cpp).
    target_link_libraries(oxcore PRIVATE setupapi cfgmgr32 slc pdh wlanapi bthprops)
else()
    target_sources(oxcore PRIVATE
        src/core/hidefile_linux.cpp
        src/tests/battery/battery_linux.cpp
        src/tests/sysinfo/sysinfo_linux.cpp
        src/tests/drivers/drivers_linux.cpp
        src/tests/license/license_linux.cpp
        src/tests/cputemp/cputemp_linux.cpp
        src/tests/keyboard/keyboard_linux.cpp
        src/tests/wireless/wireless_linux.cpp
        src/tests/disk/smartctl_linux.cpp)     # pkexec when not root
endif()

target_include_directories(oxcore PUBLIC src)
target_link_libraries(oxcore PUBLIC Qt6::Core Qt6::Concurrent PRIVATE miniz)
target_compile_definitions(oxcore PUBLIC OX_APP_VERSION="${APP_VERSION}")
ox_enable_warnings(oxcore)

# ---------------------------------------------------------------------------
# The application (UI layer)
# ---------------------------------------------------------------------------
# WIN32: a Windows GUI program (no console window). Ignored on Linux.
add_executable(lazy-djin-tester WIN32
    src/main.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/Language.h
    src/ui/Language.cpp
    src/ui/Theme.h
    src/ui/Theme.cpp                      # dark "terminal" look: every UI colour comes from here
    src/ui/DeliveryAction.h
    src/ui/DeliveryAction.cpp             # "Fill the delivery sheet" button: dialogs + files
    # Interactive tests: ITest + dialog here (they need widgets), logic in oxcore.
    src/ui/interactive/KeyboardTest.h
    src/ui/interactive/KeyboardTest.cpp
    src/ui/interactive/KeyboardDialog.h
    src/ui/interactive/KeyboardDialog.cpp
    src/ui/interactive/AudioTest.h
    src/ui/interactive/AudioTest.cpp
    src/ui/interactive/AudioDialog.h
    src/ui/interactive/AudioDialog.cpp
    src/ui/interactive/CameraTest.h
    src/ui/interactive/CameraTest.cpp
    src/ui/interactive/CameraDialog.h
    src/ui/interactive/CameraDialog.cpp
)
target_link_libraries(lazy-djin-tester PRIVATE oxcore Qt6::Widgets Qt6::Multimedia)

# App icon (the djinn), generated from src/ui/icons/djinn.png by make_icons.sh.
# Window/taskbar icon: PNGs embedded as :/icons/... (QIcon picks the best size).
# .exe icon in Explorer: Windows reads it from a resource compiled from app.rc.
qt_add_resources(lazy-djin-tester "icons"
    PREFIX /icons
    BASE src/ui/icons
    FILES
        src/ui/icons/djinn-16.png
        src/ui/icons/djinn-24.png
        src/ui/icons/djinn.png
        src/ui/icons/djinn-48.png
        src/ui/icons/djinn-64.png
        src/ui/icons/djinn-128.png
        src/ui/icons/djinn-256.png)
if(WIN32)
    target_sources(lazy-djin-tester PRIVATE src/ui/icons/app.rc)
endif()

# Translations. English is the text in the code; fr and nl are in translations/.
# lrelease compiles each .ts into a .qm, embedded in the executable as the
# resource :/i18n/oxfam-tester_<lang>.qm (portable: no extra file to ship).
# After changing texts in the code: cmake --build build --target update_translations
# (runs lupdate on the sources below), then translate the new texts in the .ts files.
get_target_property(OXCORE_SOURCES oxcore SOURCES)
get_target_property(APP_SOURCES lazy-djin-tester SOURCES)
qt_add_translations(lazy-djin-tester
    TS_FILES
        translations/oxfam-tester_fr.ts
        translations/oxfam-tester_nl.ts
    SOURCES ${OXCORE_SOURCES} ${APP_SOURCES}
    INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/src   # includes are relative to src/
    LUPDATE_OPTIONS -no-obsolete                  # drop texts no longer in the code
    RESOURCE_PREFIX /i18n)

# Windows asks for administrator rights at start (UAC prompt): SMART, WMI
# licence and driver queries need them. Embedded in the .exe by the linker.
if(MSVC)
    target_link_options(lazy-djin-tester PRIVATE "/MANIFESTUAC:level='requireAdministrator'")
endif()
ox_enable_warnings(lazy-djin-tester)

# Config files are NOT copied next to the executable: the program reads
# <root>/config/, root = the folder above the program's folder. From build/
# (Linux, Ninja) that is the repository itself, so config/ is used as is.
# The CI package puts them in "Lazy Djin's Tester/config/".

# ---------------------------------------------------------------------------
# Unit tests (ctest)
# ---------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(unittests)
endif()
