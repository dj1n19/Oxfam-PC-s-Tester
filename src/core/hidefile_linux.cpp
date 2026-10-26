#include "core/ProgramFolder.h"

// A name starting with a dot is already hidden on Linux.
bool hideFile(const QString& /*path*/)
{
    return true;
}
