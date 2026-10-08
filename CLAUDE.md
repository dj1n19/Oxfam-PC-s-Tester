# CLAUDE.md: Oxfam PC Tester

This file gives Claude Code the context needed to work on this repository.
Read it fully before changing anything. Update the "Current state" section when a milestone is finished.

## 1. Project description

A portable Qt6/C++ (Widgets) desktop application used by a technician at Oxfam
to check **refurbished desktop and laptop PCs** before they are sold in Oxfam shops.

- Targets: **Windows 10/11 and Linux**. Portable: no installer, no registry or system writes.
- Output: an **on-screen summary only**. No PDF, no print, no report export, no database.
- Goal: **as little human intervention as possible**. Automatic tests run first, interactive tests come last.
- Verdict shown to the technician (see `src/core/Verdict.h`):
  - any Fail -> "DO NOT SELL - needs repair"
  - any Error (and no Fail) -> "INCOMPLETE - a test could not run"
  - any Warn -> "SELL WITH NOTE - see warnings"
  - otherwise -> "OK FOR SALE"
- Licence: GPL-3.0.
- Repository: GitHub `dj1n19/Oxfam-PC-s-Tester`, working branch `dev`.

### Planned tests (phase 1)
Battery health, disk health (SMART), driver errors, keyboard, audio, camera,
Windows licence activation status. Plus basic system info.

### Possible later (phase 2, do NOT build now)
System updates, installation or update of an application bundle, delivery document form,
secure disk wipe (needs explicit confirmation before any erase).

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
`Qt6::Core` and `Qt6::Concurrent`. The executable `oxfam-tester` adds `Qt6::Widgets`.
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
- `TestRunner`: runs tests one at a time; `add()` always places automatic tests before interactive ones;
  a 60 s watchdog turns a silent automatic test into an Error; late or duplicate answers are ignored.
- `TestResult`: `status`, one-line `summary` (list), `details` (raw readings shown on row selection). **Always put raw tool output in `details`**: it is how problems are diagnosed on real machines.
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
- Windows PowerShell: always use `runPowerShell(script, timeoutMs)` (`src/tests/common/PowerShell.h`). It sends the script
  with `-EncodedCommand` (quotes are fine), makes errors fatal (`$ErrorActionPreference = 'Stop'`), hides progress and
  forces UTF-8 output (French Windows would otherwise send CP850 and break the JSON). Scripts end with `ConvertTo-Json -Compress`.
- Keep C++ sources ASCII-only (MSVC warning C4819 on non-UTF-8 code pages).
- `smartctl` is shipped as a **separate executable** in `tools/` (GPL), never linked. Call `smartctl --json -a <device>` and parse with `QJsonDocument`.
- Parse defensively: missing keys must produce an Error or Skipped, never a crash or a false Pass.

### C++ style
- C++20, `-Wall -Wextra -Wpedantic` (`/W4` on MSVC). **Zero warnings policy.**
- No raw `new`/`delete`. Use `std::unique_ptr` or Qt parent-child ownership.
- Don't block the UI thread. Don't create widgets from a worker thread.
- UI strings use `tr()`. Core strings are plain English for now (translation planned for M4).
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
./build/oxfam-tester
ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=offscreen ctest --test-dir build   # if no display
```

CI: `.github/workflows/build.yml`.
- `linux` job: Ubuntu 24.04, Qt 6.4 from apt, builds and runs ctest. **Do not use APIs newer than Qt 6.4** without checking.
- `windows` job: Qt 6.8 MSVC via `install-qt-action`, builds Release, runs `windeployqt` into `dist/`, uploads the portable folder as an artifact.

## 6. Repository layout

```
src/
  core/        TestResult.h ITest.h Verdict.h Thresholds.{h,cpp} TestRunner.{h,cpp}
  tests/
    battery/   BatteryInfo.h BatteryTest.{h,cpp} BatteryReport.cpp battery_linux.cpp battery_win.cpp
    sysinfo/   SystemInfo.h SystemInfoTest.{h,cpp} sysinfo_linux.cpp sysinfo_win.cpp
    disk/      DiskInfo.h Smartctl.cpp DiskTest.{h,cpp}   (no _win/_linux: smartctl is the same on both)
    drivers/   DriverInfo.h DriverParsing.cpp DriversTest.{h,cpp} drivers_linux.cpp drivers_win.cpp
    license/   LicenseInfo.h LicenseParsing.cpp LicenseTest.{h,cpp} license_linux.cpp license_win.cpp
    common/    PowerShell.h powershell_win.cpp   (Windows-only helper)
    keyboard/  KeyLayout.{h,cpp} keyboard_linux.cpp keyboard_win.cpp   (logic only)
    audio/     AudioCheck.{h,cpp}   (tone generation + result, logic only)
    (camera/ : to come)
  ui/          MainWindow.{h,cpp}
    interactive/ KeyboardTest.{h,cpp} KeyboardDialog.{h,cpp} AudioTest.{h,cpp} AudioDialog.{h,cpp}
  main.cpp
config/        thresholds.json keyboard_layout.json   (copied next to the executable)
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

**M3 in progress.** Done: Keyboard (verified on Arch), Audio (to verify). Next: Camera.
Qt Multimedia is a dependency of the **app only** (`oxfam-tester`), never of `oxcore`.
- Test: `KeyboardTest` (interactive, runs last): `config/keyboard_layout.json` (Belgian AZERTY labels, ISO 105, no numpad)
  lists keys by PC scan code set 1 (+0x100 for E0 keys). `canonicalScanCode()` converts `nativeScanCode()`: identity on
  Windows, xkb keycode -> set 1 on Linux (`scanCodeFromXkb`, unit tested). All required keys pressed = Pass (automatic),
  "A key does not work" = Fail (lists missing keys), "Skip" or closing the window = Skipped. Optional (dashed) keys may
  not exist or may be taken by the OS (Win key, PrtSc...). The dialog catches keys in `event()` so Tab/Esc are tested
  instead of moving focus or closing; its buttons are `Qt::NoFocus` so Space/Enter never click them.
  On GNOME/Wayland mute, volume and PrtSc are taken by the desktop and never reach the app (explained in the dialog).
- Test: `AudioTest` (interactive): `QAudioSink` on the default output, 48 kHz 16-bit stereo (`makeTone()` in oxcore).
  Tone on the LEFT only (440 Hz), then RIGHT only (660 Hz), 750 ms each; the technician answers Left / Right / Both / Nothing, without
  being told the side. Both correct = Pass; "Both" = Warn (mono speaker); wrong side = Fail (swapped); nothing = Fail.
  No output device or unsupported format = Error.
- CMake project, `oxcore` library + `oxfam-tester` app + `unittests`, CI workflow.
- Core: `ITest`, `TestRunner`, `TestResult`, `Thresholds`, `Verdict`.
- UI: `MainWindow` with START button, table (status/test/summary), details pane, verdict label.
- Test: `BatteryTest` (Linux reads `/sys/class/power_supply`; Windows runs `powercfg /batteryreport /xml` into a
  `QTemporaryDir`, parsed by `parseBatteryReport()` in `BatteryReport.cpp`, unit tested on Linux).
  WMI `BatteryStaticData` was dropped: "Generic Failure" on a ThinkPad 13 (Win10) and a Latitude 7420 (Win11), even as admin.
- Test: `SystemInfoTest`: vendor, model, serial, CPU, usable RAM, form factor from the SMBIOS chassis type
  (Linux `/sys/class/dmi/id` + `/proc`, Windows CIM via PowerShell). Runs first.
- Test: `DiskTest`: `smartctl --scan --json` then `smartctl --json -a -d <type> <dev>` per disk. One shared probe
  (`Smartctl.cpp`, same command line on both OSes). Checks SMART overall health, reallocated (ATA 5) and pending (ATA 197)
  sectors, NVMe `percentage_used`, temperature; power-on hours shown only. One row, worst disk wins; a disk without SMART
  (USB stick) is Skipped, but "no disk with SMART" is Error. smartctl is found in `<app>/tools/`, then `PATH`;
  Windows CI bundles it via Chocolatey into `dist/tools/`.
- Test: `DriversTest`: Windows `Win32_PnPEntity` with `ConfigManagerErrorCode != 0` (PowerShell JSON parsed by
  `parsePnpEntities()`); code 45 "not connected" is ignored (previous owner's USB devices). Linux reads
  `/sys/bus/pci/devices` (no lspci) and flags only buyer-relevant classes without a driver (storage, network, display,
  multimedia, wireless, USB): many chipset functions normally have no Linux driver. A driver problem is **Warn**, not Fail.
- Test: `LicenseTest`: `SoftwareLicensingProduct` filtered on the Windows ApplicationID with a product key;
  `LicenseStatus == 1` is Pass, anything else (or no licence) is **Warn**. Reports whether a firmware OEM key exists
  (`OA3xOriginalProductKey`), turned into a boolean inside PowerShell: the key never reaches the program. Skipped on Linux.
- `sysinfo_win.cpp` and the powercfg `battery_win.cpp` verified on a ThinkPad 13 (Win10) and a Latitude 7420 (Win11;
  battery at 36 % correctly reported FAIL).

Known limitations:
- Only the first system battery is read (dual-battery ThinkPads under-report). Multi-battery: dropped, by decision.
- Battery cycle count `0` is treated as unknown.
- SystemInfo is ERROR without root on Linux: `product_serial` is root-only (expected until the M4 privilege flow).
- RAM shown is what the OS can use (a bit below the installed amount); exact installed RAM needs root/dmidecode.
- Placeholder serials ("To Be Filled By O.E.M.", "Default string") are shown as-is, not detected.
- DiskTest verified on Windows (admin). On Linux, smartctl is not bundled: system one only (M4).
- `license_win.cpp` and `runPowerShell()` (also used by SystemInfo and Drivers) verified on real Windows.
  `pnp_problems.json` and `license_*.json` are hand-made.
- Drivers on Linux ignores the kernel log (`journalctl -k -p err`): too noisy (ACPI BIOS errors on most laptops).
- DiskTest `smartctl_*.json` samples are hand-made from the smartctl 7.x format: replace them with real outputs.
- Disk test is ERROR without root (Linux) / administrator (Windows) until M4. smartctl is not installed on the dev machine yet.
- Disk thresholds (`thresholds.json`) are first guesses, to tune with colleagues (M5).
- `unittests/samples/powercfg_latitude7420.xml` is a real report trimmed to `<Batteries>` + `<RuntimeEstimates>`; `powercfg_desktop.xml` is still hand-made.
- **Keyboard scan codes on real Windows are untested** (from Microsoft/Qt docs). Labels are Belgian AZERTY only.
- Windows admin manifest and hidden console window not done (M4).
- **Linux root vs user session (M4):** SMART (Disk) needs root, but audio (and camera) need the user session:
  run with sudo, the app cannot reach PipeWire/PulseAudio and the Audio test reports ERROR. Planned fix: run the app
  as the normal user and elevate only the smartctl call (e.g. `pkexec smartctl ...`). Windows is not affected.

## 9. Roadmap

Part-time solo work. Every milestone must end with something usable.

| Milestone | Goal / done when... |
|---|---|
| **M0 Setup** | Builds on Arch and on Windows via CI. *(done)* |
| **M1 Vertical slice** | One real test (Battery) end to end: ITest -> TestRunner -> window -> verdict. Unit tests green. *(done; real-hardware check pending)* |
| **M2 Automatic tests** | SystemInfo (model, serial, CPU, RAM, form factor); **Disk** via bundled `smartctl --json` (overall health, reallocated/pending sectors, NVMe `percentage_used`, power-on hours, temperature); **Drivers** (Windows: `Win32_PnPEntity` with `ConfigManagerErrorCode != 0` via PowerShell; Linux: `lspci -k` / `dmesg` / `journalctl -k -p err`); **Windows licence status** (`SoftwareLicensingProduct.LicenseStatus == 1`, embedded OEM key presence; `Skipped` on Linux; never generate or store keys). Desktop vs laptop auto-detected (no battery -> Skipped). ~~Multi-battery support~~ (dropped). Start using it at work. *(done)* |
| **M3 Interactive tests** | Keyboard (on-screen layout lighting up keys, `nativeScanCode()` so AZERTY/QWERTY both work, optional Fn/media keys, JSON layout); Audio (left tone, right tone, "did you hear it?"); Camera (grab a frame, detect black frame, user confirms only when ambiguous). Qt Multimedia. Modal `QDialog` per interactive test, automatic tests first. |
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
- No PDF/print/report export, SQLite, history, CLI mode, plugin system, workflow engine or QML unless the developer explicitly asks.
- No blocking calls on the UI thread; no sleeping to wait for a result.
- No silent success: unknown, missing or unparsable data is `Error` or `Skipped`, never `Pass`.
- No large refactors while implementing a feature.
