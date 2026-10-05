#pragma once
#include "nfsu/car_parts.h"
namespace nfsu {
// Applies the original FrontB FE_SKYLINE ordered preset through the native
// 0x4B26F0 lookup rules. Records refer to the supplied GLOBALB database.
bool resolve_frontend_skyline_parts(const std::vector<unsigned char>& front,
    const CarPartDatabase& database, InstalledPartSelection& selection,
    std::string& error);
}