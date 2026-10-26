#include "tests/keyboard/KeyLayout.h"

#include <windows.h>

// Qt on Windows already reports set 1 scan codes; only the way it marks
// extended keys changed between Qt versions (see scanCodeFromWindows).
int canonicalScanCode(unsigned nativeScanCode)
{
    return scanCodeFromWindows(nativeScanCode);
}

namespace {

// Reads WM_KEYDOWN/WM_KEYUP (and the SYS ones, sent with Alt) in the
// program's own message queue: no system-wide keyboard hook.
class RawKeyFilter : public QAbstractNativeEventFilter {
public:
    explicit RawKeyFilter(std::function<void(int)> onKey) : m_onKey(std::move(onKey)) {}

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* /*result*/) override
    {
        if (eventType != "windows_generic_MSG")
            return false;
        const auto* msg = static_cast<const MSG*>(message);
        switch (msg->message) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:      // Print Screen comes as a key up only
        case WM_SYSKEYUP:
            // Bits 16-23 = scan code, bit 24 = extended (E0) key: the same
            // value as Qt 6.7's nativeScanCode().
            m_onKey(scanCodeFromWindows(static_cast<unsigned>((msg->lParam >> 16) & 0x1FF)));
            break;
        default:
            break;
        }
        return false;   // Qt still gets the message (Tab, Esc... handled by the dialog)
    }

private:
    std::function<void(int)> m_onKey;
};

} // namespace

std::unique_ptr<QAbstractNativeEventFilter> makeRawKeyFilter(std::function<void(int code)> onKey)
{
    return std::make_unique<RawKeyFilter>(std::move(onKey));
}
