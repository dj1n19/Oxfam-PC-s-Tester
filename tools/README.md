# tools/

External programs shipped **next to** `oxfam-tester`, never linked into it.
The app looks here first (`<app folder>/tools/`), then in `PATH`.

No binaries are committed to git: the Windows CI job puts them in `dist/tools/`.

## smartctl (smartmontools)

- Used by the Disk test: `smartctl --scan --json`, then `smartctl --json -a -d <type> <device>`.
- Licence: GPL-2.0-or-later. Source code: https://github.com/smartmontools/smartmontools/releases
  (the exact version is printed in the CI log, step "Bundle smartctl", and by `smartctl --version`).
- Windows: installed in CI with `choco install smartmontools`, `smartctl.exe` copied here.
- Linux: not bundled yet, the system one is used (`sudo pacman -S smartmontools`). AppImage bundling: M4.
- Needs root (Linux) or administrator (Windows) to read SMART data. Without it the Disk test reports ERROR.
