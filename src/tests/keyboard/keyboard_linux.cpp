#include "tests/keyboard/KeyLayout.h"

// Qt on X11 and Wayland reports xkb keycodes (evdev code + 8).
int canonicalScanCode(unsigned nativeScanCode)
{
    return scanCodeFromXkb(nativeScanCode);
}
