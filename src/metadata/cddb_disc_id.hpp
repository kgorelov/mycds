#pragma once

#include "domain/disc_info.hpp"
#include "domain/track_info.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace mycds {

inline constexpr std::int64_t kCddbLeadInFrameOffset = 150;

struct CddbDiscQueryInfo {
    std::uint32_t discIdNumeric{};
    std::string discIdHex{};
    int trackCount{};
    std::vector<std::int64_t> trackFrameOffsets{};
    std::int64_t totalLengthSeconds{};
};

int cddb_sum_digits(std::int64_t value) noexcept;

CddbDiscQueryInfo calculate_cddb_disc_id(
    const DiscInfo& disc_info,
    const std::vector<TrackInfo>& tracks);

std::string format_cddb_query_command(const CddbDiscQueryInfo& query_info);

} // namespace mycds
