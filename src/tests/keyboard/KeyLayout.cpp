#include "tests/keyboard/KeyLayout.h"

#include <algorithm>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

KeyLayout parseKeyLayout(const QByteArray& json)
{
    KeyLayout layout;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        layout.error = QStringLiteral("Invalid keyboard layout JSON: %1").arg(parseError.errorString());
        return layout;
    }
    const QJsonObject root = doc.object();
    layout.name = root["name"].toString();

    // { "rows": [ [ {"label": "Esc", "code": "0x01"}, {"gap": 1}, ... ], ... ] }
    for (const QJsonValue& rowValue : root["rows"].toArray()) {
        std::vector<Key> row;
        for (const QJsonValue& v : rowValue.toArray()) {
            const QJsonObject o = v.toObject();
            Key k;
            if (o.contains("gap")) {
                k.width = o["gap"].toDouble(1.0);
                row.push_back(k);
                continue;
            }
            bool ok = false;
            k.code = o["code"].toString().toInt(&ok, 16);   // "0x1E": hex is clearer than JSON numbers
            if (!ok || k.code <= 0) {
                layout.error = QStringLiteral("Key '%1' has no valid hex \"code\"").arg(o["label"].toString());
                return layout;
            }
            k.label = o["label"].toString();
            k.width = o["width"].toDouble(1.0);
            k.required = o["required"].toBool(true);
            row.push_back(k);
        }
        layout.rows.push_back(std::move(row));
    }
    if (layout.rows.empty())
        layout.error = QStringLiteral("Keyboard layout has no rows");
    return layout;
}

int scanCodeFromWindows(unsigned native)
{
    // Qt gives bits 16-27 of the WM_KEYDOWN lParam: scan code + extended flag
    // (0x100). Higher bits are reserved, drop them.
    return static_cast<int>(native & 0x1FF);
}

int scanCodeFromXkb(unsigned xkb)
{
    if (xkb < 8)
        return 0;
    const unsigned evdev = xkb - 8;   // X11/Wayland keycodes are evdev codes + 8

    // linux/input-event-codes.h -> scan code set 1 for the E0 ("extended") keys.
    switch (evdev) {
    case 69:  return 0x145;   // KEY_NUMLOCK (0x45 alone is Pause on Windows)
    case 96:  return 0x11C;   // KEY_KPENTER
    case 97:  return 0x11D;   // KEY_RIGHTCTRL
    case 98:  return 0x135;   // KEY_KPSLASH
    case 99:  return 0x137;   // KEY_SYSRQ (Print Screen)
    case 100: return 0x138;   // KEY_RIGHTALT (AltGr)
    case 102: return 0x147;   // KEY_HOME
    case 103: return 0x148;   // KEY_UP
    case 104: return 0x149;   // KEY_PAGEUP
    case 105: return 0x14B;   // KEY_LEFT
    case 106: return 0x14D;   // KEY_RIGHT
    case 107: return 0x14F;   // KEY_END
    case 108: return 0x150;   // KEY_DOWN
    case 109: return 0x151;   // KEY_PAGEDOWN
    case 110: return 0x152;   // KEY_INSERT
    case 111: return 0x153;   // KEY_DELETE
    case 113: return 0x120;   // KEY_MUTE
    case 114: return 0x12E;   // KEY_VOLUMEDOWN
    case 115: return 0x130;   // KEY_VOLUMEUP
    case 119: return 0x45;    // KEY_PAUSE
    case 125: return 0x15B;   // KEY_LEFTMETA (Windows key)
    case 126: return 0x15C;   // KEY_RIGHTMETA
    case 127: return 0x15D;   // KEY_COMPOSE (Menu key)
    default:
        break;
    }
    // Esc (1) ... F12 (88): evdev codes ARE the set 1 scan codes.
    return evdev >= 1 && evdev <= 88 ? static_cast<int>(evdev) : 0;
}

KeyTracker::KeyTracker(const KeyLayout& layout)
{
    for (const auto& row : layout.rows)
        for (const Key& k : row)
            if (k.code > 0) {
                m_keys.push_back(k);
                if (k.required)
                    m_required.insert(k.code);
            }
}

bool KeyTracker::press(int code)
{
    const bool known = std::any_of(m_keys.begin(), m_keys.end(),
                                   [code](const Key& k) { return k.code == code; });
    if (known)
        m_pressed.insert(code);
    else if (std::find(m_unknown.begin(), m_unknown.end(), code) == m_unknown.end())
        m_unknown.push_back(code);
    return known;
}

int KeyTracker::requiredPressedCount() const
{
    return static_cast<int>(std::count_if(m_required.begin(), m_required.end(),
                                          [this](int c) { return isPressed(c); }));
}

bool KeyTracker::allRequiredPressed() const
{
    return requiredPressedCount() == requiredCount();
}

QStringList KeyTracker::missingRequired() const
{
    QStringList labels;
    for (const Key& k : m_keys)
        if (k.required && !isPressed(k.code))
            labels << k.label;
    return labels;
}

TestResult KeyTracker::result(Outcome outcome) const
{
    TestResult r;
    QStringList unknown;
    for (int c : m_unknown)
        unknown << QStringLiteral("0x%1").arg(c, 0, 16);
    const int optionalPressed = static_cast<int>(m_pressed.size()) - requiredPressedCount();
    r.details = QStringLiteral("Required keys pressed: %1 / %2\nOptional keys pressed: %3\n"
                               "Not pressed: %4\nCodes not in the layout: %5")
                    .arg(requiredPressedCount()).arg(requiredCount()).arg(optionalPressed)
                    .arg(missingRequired().join(' '), unknown.isEmpty() ? QStringLiteral("none") : unknown.join(' '));

    switch (outcome) {
    case Outcome::AllPressed:
        r.status = Status::Pass;
        r.summary = QStringLiteral("All %1 keys work").arg(requiredCount());
        break;
    case Outcome::KeyBroken:
        r.status = Status::Fail;
        r.summary = QStringLiteral("Not working: %1").arg(missingRequired().join(' '));
        break;
    case Outcome::Skipped:
        r.status = Status::Skipped;
        r.summary = QStringLiteral("Skipped by the technician (%1 / %2 keys pressed)")
                        .arg(requiredPressedCount()).arg(requiredCount());
        break;
    }
    return r;
}
