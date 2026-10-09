#include "tests/keyboard/KeyLayout.h"

// Qt on Windows already reports set 1 scan codes; only the way it marks
// extended keys changed between Qt versions (see scanCodeFromWindows).
int canonicalScanCode(unsigned nativeScanCode)
{
    return scanCodeFromWindows(nativeScanCode);
}
