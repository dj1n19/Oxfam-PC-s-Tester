#include "core/ProgramFolder.h"

#include <QDir>

#include <windows.h>

// Explorer ignores the leading dot: it needs the "hidden" attribute.
bool hideFile(const QString& path)
{
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    const DWORD attributes = GetFileAttributesW(native.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return false;
    return SetFileAttributesW(native.c_str(), attributes | FILE_ATTRIBUTE_HIDDEN) != 0;
}
