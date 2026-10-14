# tools/

External programs shipped **next to** `lazy-djin-tester`, never linked into it.
The app looks here first (`<app folder>/tools/`), then in `PATH`.

No binaries are committed to git: CI puts them in `dist/tools/` (Windows) or `usr/bin/tools/` inside the AppImage (Linux).

## smartctl (smartmontools)

- Used by the Disk test: `smartctl --scan --json`, then `smartctl --json -a -d <type> <device>`.
- Licence: GPL-2.0-or-later. Source code: https://github.com/smartmontools/smartmontools/releases
  (version 7.5 on both OSes, also printed by `smartctl --version`).
- Windows: smartmontools 7.5, `bin/smartctl.exe` (64-bit) taken from the official installer
  (`smartmontools-7.5.win32-setup.exe`, SHA-256 checked) with 7-Zip in CI.
- Linux (AppImage): smartmontools 7.5 built from the official source tarball in CI (`appimage` job, SHA-256
  checked), linked **statically** so the one file also runs as root: pkexec cannot read inside the AppImage, so
  the Disk test copies it to a temporary folder first. Development builds use the system one
  (`sudo pacman -S smartmontools`).
- Needs root (Linux) or administrator (Windows) to read SMART data. Without it the Disk test reports ERROR.
