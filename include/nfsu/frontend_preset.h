#pragma once
#include "nfsu/car_parts.h"
#include "nfsu/skin_inputs.h"
namespace nfsu {
// Applies the original FrontB FE_SKYLINE ordered preset through the native
// 0x4B26F0 lookup rules. Records refer to the supplied GLOBALB database.
bool resolve_frontend_skyline_parts(const std::vector<unsigned char>& front,
    const CarPartDatabase& database, InstalledPartSelection& selection,
    std::string& error);
bool resolve_frontend_skyline_skin(const std::vector<unsigned char>& globals,
    const CarPartDatabase& database, const InstalledPartSelection& parts,
    BodySkinSelection& skin, std::string& error);
using FrontendSponsorHashes = std::array<std::array<std::uint32_t,8>,7>;
bool resolve_frontend_sponsors(const CarPartDatabase& database,
    const StockParts& parts, FrontendSponsorHashes& hashes, std::string& error);
}