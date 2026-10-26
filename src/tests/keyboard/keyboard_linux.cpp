#include "tests/keyboard/KeyLayout.h"

// Qt on X11 and Wayland reports xkb keycodes (evdev code + 8).
int canonicalScanCode(unsigned nativeScanCode)
{
    return scanCodeFromXkb(nativeScanCode);
}

// Qt's key events already give every key on X11 and Wayland.
std::unique_ptr<QAbstractNativeEventFilter> makeRawKeyFilter(std::function<void(int code)> /*onKey*/)
{
    return nullptr;
}
