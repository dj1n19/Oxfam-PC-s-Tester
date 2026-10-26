# CLAUDE.md: Lazy Djin's Tester (Oxfam PC tester)

This file gives Claude Code the context needed to work on this repository.
Read it fully before changing anything. Update the "Current state" section when a milestone is finished.

## 1. Project description

**Lazy Djin's Tester** (name chosen by the developer; executable `lazy-djin-tester`): a portable Qt6/C++ (Widgets)
desktop application used by a technician at Oxfam
to check **refurbished desktop and laptop PCs** before they are sold in Oxfam shops.

- Targets: **Windows 10/11 and Linux**. Portable: no installer, no registry or system writes.
- Output: an **on-screen summary**, plus (button, never automatic) Oxfam's **delivery sheet** filled in place:
  `Fiche Pré Livraison.xlsx` / `Pre- LeveringsFiche.xlsx` on the Desktop (decided by the developer, Oct 2026).
  Plus, automatically after each run, a plain-text **run report** (every row's details) next to the program
  (developer's request 2026-10-15: for testing now, base of the phase 2 report). No PDF, no print, no database.
- Goal: **as little human intervention as possible**. Automatic tests run first, interactive tests come last.
- Verdict shown to the technician (see `src/core/Verdict.h`):
  - any Fail -> "DO NOT SELL - needs repair"
  - any Error (and no Fail) -> "INCOMPLETE - a test could not run"
  - any Warn -> "SELL WITH NOTE - see warnings"
  - otherwise -> "OK FOR SALE"
- Licence: GPL-3.0.
- Repository: GitHub `dj1n19/Oxfam-PC-s-Tester`, working branch `dev`.

### Planned tests (phase 1)
Battery health, disk health (SMART), driver errors, CPU temperature and throttling under load, keyboard, audio, camera,
Windows licence activation status. Plus basic system info.

### Possible later (phase 2, do NOT build now)
System updates, installation or update of an application bundle,
secure disk wipe (needs explicit confirmation before any erase).
(The delivery document was moved forward: see "Delivery sheet" in section 8.)

## 2. About the developer (important for how you work)

- Solo developer, **first real project**. C++ learned at school 19 (42 Network Belgium); the developer is also the Oxfam technician and the end user.
- Environment: **Arch Linux**, editor **Pulsar** (clangd via LSP), **CMake + Ninja**, terminal build. **No Qt Creator.**
- Windows cannot be run locally. It is built by GitHub Actions and tested by hand on a real Windows PC.

### How to work with this developer
- **Small, finished steps** (vertical slices). One feature across all layers before the next.
- **Explain the why** of non-obvious choices in a short comment or in your message. The developer wants to learn, not just receive code.
- **Do not over-engineer.** No abstraction until a second real use exists. No plugin system, no registry, no workflow engine, no DI framework.
- Prefer plain, readable C++20 over clever code. Follow the existing style.
- Ask before adding a dependency, a new library, a new top-level folder or a new pattern.
- Never claim something works if it was not built or run. Say what was and was not verified.

## 3. Architecture

Three layers. **The UI knows the Core, never the reverse.**

```
main.cpp                     composition root: the only place that knows every concrete test
 |- UI    src/ui             Qt Widgets, pure view, no test logic
 `- Core  src/core           ITest, TestRunner, TestResult, Thresholds, Verdict
          src/tests          diagnostic tests + platform code (*_linux.cpp / *_win.cpp)
```

CMake enforces this: the static library `oxcore` (core + tests) links only
`Qt6::Core` and `Qt6::Concurrent`. The executable `lazy-djin-tester` adds `Qt6::Widgets`.
Never add `Qt6::Widgets` or `Qt6::Gui` to `oxcore`.

### Patterns in use
| Pattern | Where |
|---|---|
| Strategy | every test implements `ITest`; the runner does not know what a test does |
| Observer | Qt signals/slots: `TestRunner` emits `testStarted`, `testFinished`, `allFinished` |
| Composition root | `main.cpp` creates the tests and injects `Thresholds` |
| Link-time platform selection | `BatteryInfo.h` declares `readBattery()`; CMake picks `battery_win.cpp` or `battery_linux.cpp` |
| Pure functions for logic | `BatteryTest::evaluate()`, `Thresholds::evaluate()`, `computeVerdict()` take data, return data, and are unit tested |
| Interactive test split (decided for M3) | logic in `src/tests/<name>/` (oxcore, unit tested, e.g. `KeyTracker`); the `ITest` + its `QDialog` in `src/ui/interactive/` (app target). The dialog only turns events into calls on the logic object and emits the `TestResult` it returns |

### Core contracts
- `Status`: `Pass | Warn | Fail | Skipped | Error`.
  - **Fail** = the test ran and the hardware is bad.
  - **Error** = the test could not run (no permission, tool missing, bad output, timeout).
  - **Never show green for an Error.** A missing threshold or unparsable output is an Error.
- `ITest` (QObject): `name()`, `needsUser()`, `run()`, signal `finished(const TestResult&)`.
  `run()` **must emit `finished` exactly once**, now or later.
- `TestRunner`: runs tests one at a time; `add()` places automatic tests, then interactive ones, then tests with
  `runsLast()` (only `CpuTempTest`: run during the interactive tests, its full load made the keyboard and audio
  dialogs freeze on real hardware, developer's report 2026-10-14). Each automatic test has its own
  watchdog, `ITest::timeoutMs()` (60 s; `CpuTempTest` 90 s), Error if silent; late or duplicate answers are ignored.
  `startOne(index)` runs one test alone (same path: a sequence from index to index, watchdog, `allFinished`).
- `TestResult`: `status`, one-line `summary` (list), `details` (raw readings shown on row selection). **Always put raw tool output in `details`**: it is how problems are diagnosed on real machines.
- **Error summaries tell the technician what to do.** If they can fix it (password cancelled, smartctl or a config
  file missing, camera busy): say how, e.g. "...: press START again and type it". If not: say what failed and end
  with "(see details)"; the verdict already says "check the ERROR rows, then press START again". When the probe knows
  a fixable cause, give it a small enum (see `SmartctlFailure`) or flag instead of comparing English error strings.
  Timeouts: `timedOut` flag (`PowerShellResult`, `CpuReading`) -> `slowWindowsSummary()`
  ("...select this row and press 'Run this test again'"). Windows PowerShell probes get 45 s: right after start-up
  the first WMI queries took > 20 s on 3 of 11 shop laptops (2026-10-15).
- `Thresholds`: `config/thresholds.json`, shape `{ "key": { "warn": X, "fail": Y } }`.
  Direction is deduced: `fail < warn` means lower is worse (battery %); otherwise higher is worse (bad sectors).
  `thresholds.json` is copied next to the executable by CMake and loaded from `applicationDirPath()`.

## 4. Conventions

### Platform code
- OS-specific code lives **only** in `*_win.cpp` / `*_linux.cpp`, selected in `CMakeLists.txt` with `if(WIN32)`.
- **No `#ifdef _WIN32`** in the code base. If you think you need one, add a function to the contract header and implement it per OS.
- Platform probes are **plain blocking functions** returning a struct (see `BatteryInfo`). The test calls them from a worker thread with `QtConcurrent::run` + `QFutureWatcher`. Probes must not touch shared state or any QWidget.

### External tools (smartctl, PowerShell, lspci, ...)
- Use `QProcess`, always with a **timeout**, check `exitStatus()` and `exitCode()`, never trust the output format blindly.
- **Windows: native API first, PowerShell/WMI being phased out** (developer's decision 2026-10-26): both are very slow
  right after start-up on the shop's PCs. Done: SystemInfo (SMBIOS via `GetSystemFirmwareTable`, registry, kernel32), Battery (no more
  powercfg: battery driver IOCTLs),
  Drivers (SetupAPI + `CM_Get_DevNode_Status`, links `setupapi` `cfgmgr32`), USB disks
  (`IOCTL_STORAGE_QUERY_PROPERTY`), licence (slc.dll + ACPI `MSDM` table size). To do: CPU (PDH `PdhAddEnglishCounter`, thermal zone counter kept
  for now, `GetSystemPowerStatus`). Code signing for Smart App Control: dropped by the developer.
- Windows PowerShell (until replaced): always use `runPowerShell(script, timeoutMs)` (`src/tests/common/PowerShell.h`). It sends the script
  with `-EncodedCommand` (quotes are fine), makes errors fatal (`$ErrorActionPreference = 'Stop'`), hides progress and
  forces UTF-8 output (French Windows would otherwise send CP850 and break the JSON). Scripts end with `ConvertTo-Json -Compress`.
  A long script that prints line by line (CPU test): `startPowerShell(QProcess&, script)`, same settings, no wait.
- Keep C++ sources ASCII-only (MSVC warning C4819 on non-UTF-8 code pages).
- `smartctl` is shipped as a **separate executable** in `tools/` (GPL), never linked. Call `smartctl --json -a <device>` and parse with `QJsonDocument`.
- Parse defensively: missing keys must produce an Error or Skipped, never a crash or a false Pass.

### C++ style
- C++20, `-Wall -Wextra -Wpedantic` (`/W4` on MSVC). **Zero warnings policy.**
- No raw `new`/`delete`. Use `std::unique_ptr` or Qt parent-child ownership.
- Don't block the UI thread. Don't create widgets from a worker thread.
- Qt Multimedia signals (`QVideoSink::videoFrameChanged`, `QCamera::errorOccurred`...) are emitted from inside the
  backend: connect them with `Qt::QueuedConnection` if the slot may stop or destroy the device. A direct call that
  stopped the camera crashed the FFmpeg backend (use of its freed buffers).
- **Translations (fr, nl; English is the source text):** everything the technician reads goes through `tr()`
  (in `Q_OBJECT` classes, static members too) or `QCoreApplication::translate("<TestClass>", "...")` in free
  functions (context = the test's class, so Linguist groups them). That covers test names, summaries, status labels,
  verdict, dialogs. **`details` stay English** (diagnostic, mixed with raw tool output). After changing texts:
  `cmake --build build --target update_translations` (lupdate), translate in `translations/*.ts` (Qt Linguist or by
  hand), rebuild (lrelease, `.qm` embedded as `:/i18n/`). Unit tests run without a translator: they see English.
  The language is picked in `src/ui/Language.cpp` (system fr/nl, else en) and changed live by the selector in
  `MainWindow` (disabled during a run; `retranslateUi()` on `QEvent::LanguageChange`).
- **Look:** dark "old terminal" theme (developer's choice): Fusion style + fixed palette + monospace font list
  (Consolas / DejaVu Sans Mono..., nothing shipped), set by `Theme::apply()` in `main.cpp`. **Every UI colour comes from
  `src/ui/Theme.h`**: no colour literals in other UI files. Status = coloured text + a code-drawn icon whose shape also
  differs (check, !, cross, ?, dash) for colour-blind technicians. Don't put a style sheet on QComboBox itself (Qt then
  draws its arrow natively: a white box).
- Headers use `#pragma once`. Include paths are relative to `src/` (e.g. `#include "core/ITest.h"`).
- Any header with `Q_OBJECT` and no matching `.cpp` must be listed in `add_library` / `add_executable` so AUTOMOC runs on it (see `ITest.h`).
- Golden samples go in `unittests/samples/` as files, not as raw string literals in `test_core.cpp`:
  XML raw strings there made moc output nothing (link error "undefined reference to vtable for TestCore").

### Git
- Small commits, imperative messages ("Add disk test"). Keep `main` buildable.
- Never commit build output. `.gitignore` already covers `build/` and `compile_commands.json`.

## 5. Build, run, test

```bash
# Arch packages
sudo pacman -S base-devel cmake ninja gdb clang git qt6-base qt6-multimedia qt6-tools smartmontools

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ln -sf build/compile_commands.json .          # clangd in Pulsar
./build/lazy-djin-tester
ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=offscreen ctest --test-dir build   # if no display
```

CI: `.github/workflows/build.yml`.
- `linux` job: Ubuntu 24.04, Qt 6.4 from apt, builds and runs ctest. **Do not use APIs newer than Qt 6.4** without checking.
- `windows` job: Qt 6.8 MSVC via `install-qt-action`, builds Release, runs `windeployqt` into `dist/`, uploads the portable folder as an artifact.

## 6. Repository layout

```
src/
  core/        TestResult.h ITest.h Verdict.h Thresholds.{h,cpp} TestRunner.{h,cpp} ProgramFolder.{h,cpp}
  tests/
    battery/   BatteryInfo.h BatteryTest.{h,cpp} battery_linux.cpp battery_win.cpp
    sysinfo/   SystemInfo.h SystemInfoTest.{h,cpp} Smbios.cpp (raw table parser) sysinfo_linux.cpp sysinfo_win.cpp
    disk/      DiskInfo.h Smartctl.cpp DiskTest.{h,cpp} smartctl_linux.cpp (pkexec) smartctl_win.cpp
    drivers/   DriverInfo.h DriverParsing.cpp DriversTest.{h,cpp} drivers_linux.cpp drivers_win.cpp
    license/   LicenseInfo.h LicenseTest.{h,cpp} license_linux.cpp license_win.cpp
    cputemp/   CpuTempInfo.h CpuTempParsing.cpp CpuTempTest.{h,cpp} cputemp_linux.cpp cputemp_win.cpp
    common/    PowerShell.h powershell_win.cpp   (Windows-only helper)
    keyboard/  KeyLayout.{h,cpp} keyboard_linux.cpp keyboard_win.cpp   (logic only)
    audio/     AudioCheck.{h,cpp}   (tone generation + result, logic only)
    camera/    FrameCheck.{h,cpp}   (frame statistics + result, logic only; grey bytes, no QImage)
  delivery/    XlsxEditor.{h,cpp} (in-place .xlsx cell editing, miniz)  DeliverySheet.{h,cpp} (which cell = what)
  report/      RunReport.{h,cpp} (run report text + file name: pure, unit tested; written by MainWindow)
  ui/          MainWindow.{h,cpp} Language.{h,cpp} Theme.{h,cpp} DeliveryAction.{h,cpp} (button flow: dialogs + files)
    icons/       djinn.png (32 px pixel-art master) -> make_icons.sh (ImageMagick, run by hand) -> djinn-<size>.png,
                 lazy-djin-tester.ico; app.rc (Windows .exe icon). Generated files are committed (CI has no ImageMagick).
    interactive/ KeyboardTest/Dialog, AudioTest/Dialog, CameraTest/Dialog (.h/.cpp each)
  main.cpp
config/        thresholds.json keyboard_layout.json   (copied next to the executable)
translations/  oxfam-tester_fr.ts oxfam-tester_nl.ts   (compiled into the executable; internal names such as
               these, `oxcore` and the repo keep "oxfam": only what users see was renamed)
third_party/   miniz/ (zip library, MIT, vendored unchanged; version + checksum in its README.md)
tools/         README.md only; CI puts external executables (smartctl.exe) in dist/tools/
unittests/     QtTest unit tests (test_core.cpp)   <- not the same as src/tests
  samples/     golden samples of real tool output (loaded with QFINDTESTDATA)
.github/workflows/build.yml
CMakeLists.txt
```
`src/tests/` = diagnostic tests that run on the PC under test. `unittests/` = developer tests for the code.

## 7. How to add a new diagnostic test (recipe)

1. Create `src/tests/<name>/`.
2. If the test needs OS data: write `<Name>Info.h` (struct + `readXxx()` declaration), then `<name>_linux.cpp` and `<name>_win.cpp`, and add both under the `if(WIN32)` block in `CMakeLists.txt`.
3. Write `<Name>Test` deriving from `ITest`. Keep a **static pure `evaluate(info, thresholds)`** returning `TestResult`.
4. Add the thresholds to `config/thresholds.json`.
5. Add unit tests for `evaluate()` in `unittests/test_core.cpp` (golden samples of real tool output when parsing JSON/text).
6. Register it in `main.cpp` with `runner.add(std::make_unique<...>(thresholds));`.
7. Build with zero warnings, run ctest, run the app. Test on a real machine, and for Windows via the CI artifact.

## 8. Current state

**M0 + M1 verified on Arch and in CI** (zero warnings, ctest green, Linux and Windows CI jobs green).
Still to check by hand: START on a real machine, and the Windows CI artifact on a real Windows PC.

**M2 done** (automatic tests): SystemInfo, Battery, Disk, Drivers, Windows licence, all verified on real Windows.
Multi-battery support was dropped by the developer (first battery only).

**M3 done** (interactive tests): Keyboard, Audio, Camera, verified on Arch.

**M4 in progress.** Done: Windows GUI subsystem (no console) + `requireAdministrator` manifest (to verify on Windows).
Linux privilege flow done (to verify): app runs as the normal user, only smartctl is elevated.
French/Dutch/English done (to verify on screen and on Windows): system language fr/nl, else English, plus a
language selector. **Dutch texts were written by Claude: have a Dutch speaker check them** (left "unfinished" in
the .ts so Linguist lists them). French texts to proofread by the developer.
Clear error messages done (actionable ERROR summaries, see the TestResult contract).
**Delivery sheet** done, verified by the developer in LibreOffice after a real run:
- Button "Fill the delivery sheet", enabled after a complete run. Finds the sheet on the Desktop
  (`QStandardPaths::DesktopLocation`, FR or NL file name), warns if an office lock file exists, writes C7 (date, text
  dd/MM/yyyy), C5 (model), and C/D of rows 12-16 (battery, disk, keyboard, audio, camera): PASS = box ticked + `note`
  (battery "85 %"), WARN = ticked + summary, SKIPPED = unticked + summary, FAIL/ERROR = not written (developer's choice). Manual rows/header are never touched.
- Model not in the "Models" tab (Lenovo reports "20HD..."): editable list dialog; a new model is appended to Models and
  a copy WITHOUT results is saved next to the program folder (`applicationDirPath()/..`: Documents in production) under
  the same name, the previous one kept as `<name> (backup).xlsx`. AppImage (M4): use `$APPIMAGE`'s folder instead.
- **Why in place:** QXlsx was tried on a copy and lost all 6 dropdowns and the page setup. `XlsxEditor` rewrites only
  the changed cells (found with QXmlStreamReader offsets, inline strings, `s=` style kept) and copies every other zip
  entry raw. The FR file is saved by OnlyOffice (dropdowns in x14 extLst), the NL one by LibreOffice (standard
  `dataValidations`): both are unit-test samples (`delivery_sheet_{fr,nl}.xlsx`, operator names anonymised).
- `TestResult::note` = the key value alone (battery %, "vendor model" from SystemInfo).
Dark terminal theme + status icons done (checked by rendering the window offscreen; not yet seen on Windows).
App icon done: the "cool djinn" (winking pixel-art djinn, developer's choice, no Oxfam logo): window/taskbar icon via
`QApplication::setWindowIcon` (`:/icons/`), .exe icon via `app.rc` (to verify on Windows), `djinn-256.png` ready for the AppImage.
**AppImage** done in CI (job `appimage`, artifact `lazy-djin-tester-linux`; to verify on a real Mint PC):
- Built on Ubuntu 22.04 with Qt 6.8 from qt.io (glibc 2.35: runs on Mint 21 and newer), linuxdeploy +
  linuxdeploy-plugin-qt (pinned, SHA-256 checked). Inside: `usr/bin/` = program, `thresholds.json`,
  `keyboard_layout.json`, `tools/smartctl` (static smartmontools 7.5 built in CI: Mint has no smartmontools).
- Custom `AppRun` (in the workflow) starts `usr/bin/lazy-djin-tester` by its real path: linuxdeploy's own AppRun goes
  through a symlink at the AppDir root and recent Qt then takes that folder as `applicationDirPath()` (no plugins,
  no config). Seen with Arch's Qt; a local build with system Qt + `NO_STRIP=1` is a quick way to test packaging.
- `smartctl_linux.cpp`: the AppImage mount is readable only by the user (not root), so before pkexec smartctl is
  copied to a `QTemporaryDir` when it lies under `$APPDIR`.
- Delivery sheet copy: next to the `.AppImage` file (`$APPIMAGE`), not `applicationDirPath()/..` (read-only mount):
  `folderAroundProgram()` (`core/ProgramFolder`), shared with the run report.
- Only the X11 (xcb) platform plugin is bundled (Mint Cinnamon = X11); under Wayland Qt falls back to XWayland.
**Run report** done (developer's choices: automatic, every test, one file per run, next to the program):
`MainWindow::onAllFinished()` (full run and each "Run this test again") writes
`folderAroundProgram()/yyyy-MM-dd HH-mm-ss <model>.txt`: date, model, verdict, then per row `==== [STATUS] name ====`,
summary, details. Status/name/summary/verdict in the UI language, details English. Text mode (CRLF on Windows),
UTF-8, QSaveFile, `\r\n` in raw output made `\n` first (else `\r\r\n`). Path or error shown in the status bar (no dialog: automatic). Verified by the developer after a
real run (2026-10-15): the file appears beside the program folder.
**Field day 2026-10-15** (23 run reports, Windows, from a USB stick: 11 Lenovo 20M6, Dell Latitude 7420/7430, HP ZBook
Fury 17 G7, Surface Pro 7): no crash, verdicts right. Found and fixed: PowerShell timeouts after start-up (above),
CPU first reading > 12 s (now 30 s), Surface audio float, 5 s freeze before the audio test (multimedia warm-up).
ACPI temperature never reacted on any PC (Lenovo `THM0_0` fixed 50-84 C, HP `CPUZ_0` 32 C): on Windows the CPU
speed is the real check (Dell 266 %, Lenovo 136-149 %, one Lenovo 119-124 %: dust?, HP 135 %). Lenovo driver WARN
"Synaptics WBDI - SGX" = fingerprint reader needing SGX in the BIOS. The reports hold real smartctl JSON of 6 NVMe
models (Intel SSDPEKKF256G8L, Samsung PM9A1/MZVLW256, WD SN810/SN730, SK Hynix): golden-sample candidates.
Next: test the AppImage on Mint (START with password prompt, audio, camera, delivery sheet), then tag v1.0.
Qt Multimedia is a dependency of the **app only** (`lazy-djin-tester`), never of `oxcore`.
Qt Multimedia is started at launch (`QMediaDevices::audioOutputs()` 300 ms after `window.show()` in `main.cpp`): its
first use loads the FFmpeg backend, ~5 s from a USB stick on the shop's Windows laptops, which froze the UI between
the keyboard and audio tests on the first run only (developer's report 2026-10-15; never on Arch). Fix not yet seen on Windows.
- Test: `KeyboardTest` (interactive, runs last): `config/keyboard_layout.json` (Belgian AZERTY labels, ISO 105, no numpad)
  lists keys by PC scan code set 1 (+0x100 for E0 keys). `canonicalScanCode()` converts `nativeScanCode()`: identity on
  Windows, xkb keycode -> set 1 on Linux (`scanCodeFromXkb`, unit tested). All required keys pressed = Pass (automatic),
  "A key does not work" = Fail (lists missing keys), "Skip" or closing the window = Skipped. Optional (dashed) keys may
  not exist or may be taken by the OS (Win key, PrtSc...). The dialog catches keys in `event()` so Tab/Esc are tested
  instead of moving focus or closing; its buttons are `Qt::NoFocus` so Space/Enter never click them.
  On GNOME/Wayland mute, volume and PrtSc are taken by the desktop and never reach the app (explained in the dialog).
- Test: `AudioTest` (interactive): `QAudioSink` on the default output, 48 kHz stereo, 16-bit, else 32-bit float
  (`makeTone(..., SampleType)` in oxcore; Surface Pro 7's Realtek SST refused 16-bit, 2026-10-15; fix not yet seen there).
  Tone on the LEFT only (440 Hz), then RIGHT only (660 Hz), 750 ms each; the technician answers Left / Right / Both / Nothing, without
  being told the side. Both correct = Pass; "Both" = Warn (mono speaker); wrong side = Fail (swapped); nothing = Fail.
  No output device or unsupported format = Error.
- Test: `CameraTest` (interactive, asks only when ambiguous): `QCamera` + `QMediaCaptureSession` -> `QVideoSink`
  (no MultimediaWidgets). Live preview in a QLabel; every 250 ms the frame is converted to Grayscale8 and
  `lumaStats()`/`classifyFrame()` (oxcore) give brightness and detail. ~1 s of real picture = automatic Pass.
  Black/uniform for 3 s = prompt (shutter? lens?) and the technician may click "Camera is broken" (Fail).
  No camera = Skipped (desktops); camera error or no frame in 6 s = Error.
- CMake project, `oxcore` library + `lazy-djin-tester` app + `unittests`, CI workflow.
- Core: `ITest`, `TestRunner`, `TestResult`, `Thresholds`, `Verdict`.
- UI: `MainWindow` with START button, "Run this test again" (selected row, enabled after a complete run so the
  verdict covers every row; verdict recomputed from each row's latest result), table (status/test/summary), details
  pane, verdict label (word wrap: the French verdict is wider than the window). Buttons enabled in `updateButtons()`.
- Test: `BatteryTest` (Linux reads `/sys/class/power_supply`; Windows asks the battery driver: SetupAPI lists the
  `GUID_DEVICE_BATTERY` interfaces, `IOCTL_BATTERY_QUERY_TAG` + `IOCTL_BATTERY_QUERY_INFORMATION` give design/full
  capacity and cycles; first system battery, UPS (short-term) left out; `BATTERY_UNKNOWN_CAPACITY` = 0 = Error.
  Since 2026-10-26, not yet run on Windows. `powercfg /batteryreport` was dropped: it also gathers weeks of usage
  history and timed out (30 s) on every shop laptop just after start-up).
  WMI `BatteryStaticData` was dropped: "Generic Failure" on a ThinkPad 13 (Win10) and a Latitude 7420 (Win11), even as admin.
- Test: `SystemInfoTest`: vendor, model, serial, CPU, RAM, form factor from the SMBIOS chassis type. Runs first.
  Linux: `/sys/class/dmi/id` + `/proc` (usable RAM). Windows (2026-10-26, no PowerShell; not yet run on Windows):
  raw SMBIOS table from `GetSystemFirmwareTable('RSMB')` parsed by `parseSmbios()` (unit tested on a hand-made
  table), CPU name from the registry (`QSettings` NativeFormat, read only), installed RAM from
  `GetPhysicallyInstalledSystemMemory` (fallback: usable RAM).
- Test: `DiskTest`: `smartctl --scan --json` then `smartctl --json -a -d <type> <dev>` per disk. One shared probe
  (`Smartctl.cpp`, same command line on both OSes). Checks SMART overall health, reallocated (ATA 5) and pending (ATA 197)
  sectors, NVMe `percentage_used`, temperature; power-on hours shown only. One row, worst disk wins; a disk without SMART
  (USB stick) is Skipped, but "no disk with SMART" is Error. **USB disks are left out** (developer's decision
  2026-10-15: the tester's own stick claimed "SMART OK, 0 C"): `usbDevices()` asks the OS for the bus (Linux:
  `/sys/block/<dev>` under `/usb`; Windows: `IOCTL_STORAGE_QUERY_PROPERTY` BusType on `\\.\PhysicalDriveN`, disk N =
  `/dev/sd`+letter N (smartctl names NVMe that way too on Windows), `physicalDriveNumber()` unit tested; not yet run on Windows); listed in the details; if the OS cannot tell, every disk is checked. smartctl is found in `<app>/tools/`, then `PATH`;
  Windows CI takes smartctl.exe 7.5 from the official installer (7-Zip, SHA-256 checked; Chocolatey failed with 406) into `dist/tools/`.
- Test: `DriversTest`: Windows (2026-10-26, no PowerShell; not yet run on Windows): SetupAPI lists the present devices
  (`DIGCF_PRESENT`: the previous owner's unplugged USB devices, WMI code 45, never come), `CM_Get_DevNode_Status`
  gives the problem code (= WMI `ConfigManagerErrorCode`); name = friendly name, else description, else device ID. Linux reads
  `/sys/bus/pci/devices` (no lspci) and flags only buyer-relevant classes without a driver (storage, network, display,
  multimedia, wireless, USB): many chipset functions normally have no Linux driver. A driver problem is **Warn**, not Fail.
- Test: `LicenseTest` (Windows, 2026-10-26, no PowerShell; not yet run on Windows): `SLGetLicensingStatusInformation`
  (slc.dll, links `slc`) for the Windows ApplicationID; every edition has an entry, those without a key say
  "unlicensed": the best status wins (licensed > grace > notification), its name/channel from
  `SLGetProductSkuInformation`. Licensed is Pass, anything else (or no licence) is **Warn**. Firmware OEM key = the
  ACPI `MSDM` table exists (only its SIZE is asked: the key never reaches the program). Skipped on Linux.
- Test: `CpuTempTest` (`runsLast()`, ~30 s, after the interactive tests: the technician waits for it): one idle reading, then every core busy (`QThread`s at low
  priority, so the readings still get CPU time) while sampling every ~1 s; judged on the peak (`cpu_temp_peak_c`).
  Linux: hwmon `coretemp`/`k10temp`/`zenpower` (label `Package id 0`, then `Tdie`, then `Tctl`, else hottest), then
  thermal zones `x86_pkg_temp`, `acpitz`. Windows: `MSAcpi_ThermalZoneTemperature` (tenths of kelvin, hottest zone,
  "Not supported" caught in PowerShell); no kernel driver (LibreHardwareMonitor/WinRing0 rejected: flagged by Defender).
  A sensor that rises < 1 C under load is not the CPU: Skipped instead of Pass (too hot still fails).
  Readings come from `sampleCpu(seconds, startLoad)`: reading at rest, then `startLoad()` starts the burn threads only
  if something is readable, then ~1 reading/s. Windows: ONE PowerShell loops and prints a JSON line per reading
  (`startPowerShell()`), started before the load: a new PowerShell per reading timed out (> 10 s to start) under full
  load on the Latitude 7420. **Speed (throttling)**, read with the temperature: % of base speed;
  Windows `Win32_PerfFormattedData_Counters_ProcessorInformation._Total.PercentProcessorPerformance` (CIM, not
  `Get-Counter`: counter names are translated on fr/nl Windows), Linux average `scaling_cur_freq` / `base_frequency`
  (intel_pstate only; AMD/acpi-cpufreq: no speed). Judged on the **median of the second half** of the load (single
  readings dip: 220 % with dips to 162 % on Arch), first 5 s left out, `cpu_speed_min_pct` (warn < 100, fail < 70).
  On battery (Windows `BatteryStatus == 1`; Linux charger input listed but offline) a slow CPU is Warn "plug in the
  charger", never Fail. Temperature and speed are judged apart, the worse wins (`worse()` in `TestResult.h`).
  Neither sensor nor speed = Skipped; then the CPU is not loaded at all.
- The old PowerShell `sysinfo_win.cpp` and the powercfg `battery_win.cpp` verified on a ThinkPad 13 (Win10) and a Latitude 7420 (Win11;
  battery at 36 % correctly reported FAIL).

Known limitations:
- Only the first system battery is read (dual-battery ThinkPads under-report). Multi-battery: dropped, by decision.
- Battery cycle count `0` is treated as unknown.
- The serial number is **optional** (developer's decision): on Linux, as a normal user, `product_serial` is root-only,
  so System info shows "serial unknown" and stays Pass.
- Linux RAM shown is what the OS can use (a bit below the installed amount); exact installed RAM needs root/dmidecode.
- Placeholder serials ("To Be Filled By O.E.M.", "Default string") are shown as-is, not detected.
- DiskTest verified on Windows (admin). On Linux the AppImage bundles a static smartctl (not yet run on real hardware).
- The old PowerShell `license_win.cpp` and `runPowerShell()` verified on real Windows.
- Drivers on Linux ignores the kernel log (`journalctl -k -p err`): too noisy (ACPI BIOS errors on most laptops).
- DiskTest: `smartctl_real_*.json` are real smartctl 7.5 outputs from 7 NVMe models + the tester's USB stick (Windows,
  2026-10-15, `serial_number` and `eui64.ext_id` anonymised, CRLF -> LF); the other `smartctl_*.json` (ATA, failing
  disks, permission denied) are still hand-made: replace them when real ones turn up. No real SATA disk sample yet.
- Disk test is ERROR without root (Linux) / administrator (Windows) until M4. smartctl is not installed on the dev machine yet.
- Disk thresholds (`thresholds.json`) are first guesses, to tune with colleagues (M5).
- Camera frame thresholds (detail < 6, dark < 20) are first guesses: tune on real webcams (M5).
- CPU temperature: verified on Arch (coretemp, 52 -> 72 C in 30 s, speed 220 % of base). `acpi_zones.json` is
  hand-made; `acpi_unsupported.json` matches the real Latitude 7420 (Win11) output byte for byte. Thresholds (warn 90, fail 97) are first guesses: thin laptops run hot by design (M5). The ACPI zone on
  Windows is a motherboard zone: expect many Skipped (Latitude 7420: "Not supported").
- CPU speed: the counters were read by hand on the Latitude 7420 (`cpu_win_latitude7420_*.json`: rest on battery =
  real output; load = real values in the script's shape). Under full load: 266 % falling to 241 % (charger) / 211 %
  (battery) as the turbo budget runs out: normal. The app's own probe verified on the Latitude 7420 (Win11, charger, 2026-10-14): 7 readings in 32.5 s
  (one per ~4.6 s under load, not per second), 266 % flat, PASS. On battery: 5 readings in 29.1 s, 266 % flat.
  266 % is the ceiling: Windows' "base" is `MaxClockSpeed` (1498 MHz on this i5-1145G7, not the 2.6 GHz of its name),
  and 2.66 x 1.498 = 4.0 GHz = its all-core turbo. The hand-read drop to 241/211 % comes when the turbo budget runs
  out after our 30 s with a fresh budget. Confirmed: 3rd load in a row on battery gave 266 266 210 106 82 86 ...
  (counter live, median ~83 % -> WARN "plug in the charger"). ThinkPad L380 (Win11): ACPI zone THM0_0 fixed at
  77.1 C under load -> temperature Skipped (first real case), speed 184 -> 150 % -> PASS. Both are unit tests
  (`cpuRealWindowsRuns`, sample `cpu_win_l380_rest.json` = real output). Limits 100/70 are
  guesses: no throttling PC seen yet (M5). A real Windows run (2026-10-14) gave an empty speed list: the
  first 5 *readings* were left out and a reading under load can take ~6 s. Now the first 5 *seconds* (`atMs` on each
  reading, pure `CpuTempTest::collect()`), the details give the reading count, and a missing speed is listed.
- Keyboard on real Windows: first run showed Qt 6.8 sends extended keys as `0xE0xx` (Qt 6.4: `0x1xx`); `scanCodeFromWindows()` now accepts both (fix not yet re-run on Windows). Labels are Belgian AZERTY only.
- Windows: the app is a GUI program (`WIN32`), so `qDebug()` output is not visible; put diagnostics in `details`.
- **Linux privileges:** run the app as the normal user (sudo breaks audio and camera: PipeWire/PulseAudio belong to the
  user session). `smartctl_linux.cpp` elevates only smartctl: if not root, ONE `pkexec /bin/sh -c <batch>` reads every
  disk (one password prompt, 50 s to type it before the 60 s watchdog). Device names/types are checked with
  `isSafeShellWord()` and single-quoted; outputs are split by the `OXFAM_SMARTCTL_EXIT=<code>` marker
  (`parseBatchOutput()`). pkexec exit 126/127 = password refused/cancelled -> Error. No pkexec = direct call.
  The technician must be at the PC when pressing START (password prompt). Windows: admin via manifest, direct calls.

## 9. Roadmap

Part-time solo work. Every milestone must end with something usable.

| Milestone | Goal / done when... |
|---|---|
| **M0 Setup** | Builds on Arch and on Windows via CI. *(done)* |
| **M1 Vertical slice** | One real test (Battery) end to end: ITest -> TestRunner -> window -> verdict. Unit tests green. *(done; real-hardware check pending)* |
| **M2 Automatic tests** | SystemInfo (model, serial, CPU, RAM, form factor); **Disk** via bundled `smartctl --json` (overall health, reallocated/pending sectors, NVMe `percentage_used`, power-on hours, temperature); **Drivers** (Windows: `Win32_PnPEntity` with `ConfigManagerErrorCode != 0` via PowerShell; Linux: `lspci -k` / `dmesg` / `journalctl -k -p err`); **Windows licence status** (`SoftwareLicensingProduct.LicenseStatus == 1`, embedded OEM key presence; `Skipped` on Linux; never generate or store keys). Desktop vs laptop auto-detected (no battery -> Skipped). ~~Multi-battery support~~ (dropped). Start using it at work. *(done)* |
| **M3 Interactive tests** *(done)* | Keyboard (on-screen layout lighting up keys, `nativeScanCode()` so AZERTY/QWERTY both work, optional Fn/media keys, JSON layout); Audio (left tone, right tone, "did you hear it?"); Camera (grab a frame, detect black frame, user confirms only when ambiguous). Qt Multimedia. Modal `QDialog` per interactive test, automatic tests first. |
| **M4 Polish and packaging** | Windows portable zip (`windeployqt`) and Linux AppImage; Windows `requireAdministrator` manifest and Linux privilege flow for SMART access (app as user, only `smartctl` via `pkexec`: root breaks audio/camera); hide the Windows console; French/English via `tr()` and Qt Linguist; clear error messages. Tag **v1.0**. |
| **M5 Field feedback** | Run on many real, ugly refurbished machines; adjust thresholds with colleagues; fix parsing quirks; keep golden samples of real outputs in `unittests/`. |
| **Phase 2** (later) | System updates, application bundle install (manifest + SHA-256), delivery-document form (on-screen/text only), secure disk wipe with explicit confirmation. Reuse the "something that runs and reports a result" idea; **do not build a workflow engine in advance.** |

## 10. Risks and rules of thumb

- **Hardware diversity** (SMART quirks, USB bridges, odd battery drivers): save raw tool output in `details`, add real samples as unit-test fixtures.
- **Privileges:** SMART and some WMI/driver queries need admin/root. Insufficient rights must give `Error` with a clear message, not `Pass`/`Fail`.
- **Hung hardware calls:** prefer out-of-process (`QProcess` with timeout) for risky calls. The runner watchdog is the last line of defence, not a substitute for timeouts.
- **Qt Multimedia backends differ between Windows and Linux:** isolate behind the test class and test on real hardware early.
- **Licensing:** the tool only reads licence status and the firmware-embedded OEM key. It never generates, ships or guesses keys. Oxfam should confirm which Microsoft refurbisher programme applies.
- **Data safety:** donated PCs may contain personal data. Any future wipe feature must require explicit confirmation naming the target disk, and must refuse the disk the tool itself runs from.
- **Widgets, not QML:** chosen for robustness on old PCs without GPU drivers and because the UI is a simple list plus dialogs. Keep logic out of UI classes so a QML front end remains possible.

## 11. Things NOT to do

- No `#ifdef` for platform selection.
- No `Qt6::Widgets`/`Gui` in `oxcore`; no UI code in `src/core` or `src/tests`.
- No PDF/print or report formats beyond the plain-text run report, SQLite, history, CLI mode, plugin system, workflow engine or QML unless the developer explicitly asks.
- No blocking calls on the UI thread; no sleeping to wait for a result.
- No silent success: unknown, missing or unparsable data is `Error` or `Skipped`, never `Pass`.
- No large refactors while implementing a feature.
