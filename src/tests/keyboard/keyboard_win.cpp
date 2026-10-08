#include "tests/keyboard/KeyLayout.h"

// Qt on Windows already reports set 1 scan codes (+ 0x100 when extended).
int canonicalScanCode(unsigned nativeScanCode)
{
    return scanCodeFromWindows(nativeScanCode);
}
