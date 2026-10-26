#include "tests/sysinfo/SystemInfo.h"

#include <QList>

// SMBIOS layout (DMTF DSP0134): a list of structures, each one
//   - a 4-byte header: type, length of the formatted area, handle (2 bytes),
//   - the rest of the formatted area (fixed fields; text fields hold a string
//     NUMBER, 1-based, 0 = no string),
//   - the strings, each ending with '\0', the whole set ending with an extra
//     '\0' (so "\0\0" when the structure has no strings at all).
// Type 127 marks the end of the table.
SmbiosIds parseSmbios(const QByteArray& table)
{
    SmbiosIds ids;
    bool hasChassis = false;
    qsizetype pos = 0;
    while (pos + 4 <= table.size()) {
        const int type = quint8(table[pos]);
        const int length = quint8(table[pos + 1]);
        if (length < 4 || pos + length > table.size())
            break;
        // The first "\0\0" after the formatted area ends the string set.
        const qsizetype end = table.indexOf(QByteArray("\0\0", 2), pos + length);
        if (end < 0)
            break;
        const QByteArray fields = table.mid(pos, length);
        const QList<QByteArray> strings = table.mid(pos + length, end - (pos + length)).split('\0');

        // The string whose number is stored at this offset of the formatted area.
        const auto text = [&](int offset) -> QString {
            if (offset >= length)
                return {};
            const int n = quint8(fields[offset]);
            if (n < 1 || n > strings.size())
                return {};
            return QString::fromLatin1(strings[n - 1]).trimmed();
        };

        if (type == 1 && !ids.hasSystem) {
            ids.hasSystem = true;
            ids.vendor = text(0x04);
            ids.model  = text(0x05);
            ids.serial = text(0x07);
        } else if (type == 3 && !hasChassis && length > 0x05) {
            hasChassis = true;
            ids.chassisType = quint8(fields[0x05]) & 0x7F;   // bit 7 = "has a lock"
        } else if (type == 127) {
            break;
        }
        pos = end + 2;
    }
    return ids;
}
