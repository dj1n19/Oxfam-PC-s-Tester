#include "tests/license/LicenseInfo.h"

// Linux has no activation: the test reports Skipped.
LicenseInfo readLicense()
{
    LicenseInfo info;
    info.applicable = false;
    return info;
}
