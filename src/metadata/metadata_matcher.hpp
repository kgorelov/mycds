#pragma once

#include "domain/disc_info.hpp"
#include "domain/disc_metadata.hpp"
#include "domain/track_info.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace mycds {

enum class MatchQuality {
    ExactOffsetMatch,
    CloseOffsetMatch,
    MetadataOnlyMatch,
    OffsetMismatch,
    LayoutMismatch
};

std::string match_quality_to_string(MatchQuality quality);

struct MetadataValidationResult {
    bool valid{false};
    MatchQuality quality{MatchQuality::LayoutMismatch};
    bool trackCountMatches{false};
    bool trackOrderValid{false};
    bool allTrackTitlesPresent{false};
    bool offsetsAvailable{false};
    std::int64_t maxFrameOffsetDelta{0};
    std::int64_t discLengthSecondsDelta{0};
    std::vector<std::string> warnings{};
    std::vector<std::string> errors{};
};

class MetadataMatcher {
public:
    static MetadataValidationResult validate(
        const DiscInfo& local_disc,
        const std::vector<TrackInfo>& local_tracks,
        const MetadataRecord& record);

    static void applyToLayout(
        DiscLayout& layout,
        const MetadataRecord& record);
};

} // namespace mycds
