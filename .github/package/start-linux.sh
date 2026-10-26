#!/bin/sh
# Started by "Lazy Djin's Tester (Linux).desktop", one folder up.
# GitHub artifacts (zip) lose the "executable" right: give it back, then
# start the AppImage. On a stick that cannot keep it (exFAT), chmod fails
# but the stick is mounted with every file executable anyway.
HERE="$(dirname "$(readlink -f "$0")")"
APP="$HERE/lazy-djin-tester-x86_64.AppImage"
chmod +x "$APP" 2>/dev/null
exec "$APP" "$@"
