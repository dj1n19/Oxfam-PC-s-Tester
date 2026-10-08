#pragma once
#include <set>
#include <vector>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include "core/TestResult.h"

// Keys are identified by their PHYSICAL position, never by the character
// they type: so an AZERTY and a QWERTY keyboard are tested the same way.
// "code" is the PC scan code (set 1), with 0x100 added for the E0-prefixed
// "extended" keys (arrows, right Ctrl, AltGr...). This is what Windows gives.

struct Key {
    QString label;
    int code = 0;          // 0 = gap (empty space in the drawing)
    double width = 1.0;    // in key units
    bool required = true;  // optional: may not exist (ANSI, laptops...) or be stolen by the OS
};

struct KeyLayout {
    QString name;
    std::vector<std::vector<Key>> rows;
    QString error;         // non-empty = unreadable layout file
};

// Reads config/keyboard_layout.json. Pure function, unit tested.
KeyLayout parseKeyLayout(const QByteArray& json);

// QKeyEvent::nativeScanCode() -> layout code. The OS-specific one is chosen
// by CMake (keyboard_linux.cpp / keyboard_win.cpp); both helpers below are
// plain functions so the unit tests can check the tables on Linux.
int canonicalScanCode(unsigned nativeScanCode);
int scanCodeFromXkb(unsigned xkbKeycode);       // Linux X11/Wayland: evdev code + 8
int scanCodeFromWindows(unsigned nativeScanCode);

// Which keys were pressed. Pure: the dialog only feeds it codes.
class KeyTracker {
public:
    explicit KeyTracker(const KeyLayout& layout);

    bool press(int code);              // true if the code is in the layout
    bool isPressed(int code) const { return m_pressed.count(code) > 0; }
    bool allRequiredPressed() const;
    int requiredCount() const { return static_cast<int>(m_required.size()); }
    int requiredPressedCount() const;
    QStringList missingRequired() const;   // labels, in layout order

    enum class Outcome { AllPressed, KeyBroken, Skipped };
    TestResult result(Outcome outcome) const;

private:
    std::vector<Key> m_keys;   // layout order, gaps removed
    std::set<int> m_required;
    std::set<int> m_pressed;
    std::vector<int> m_unknown;   // codes not in the layout (for details)
};
