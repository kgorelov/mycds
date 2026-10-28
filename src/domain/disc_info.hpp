#pragma once

#include "domain/disc_metadata.hpp"
#include "domain/track_info.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace mycds {

struct DiscInfo {
    int firstTrack{};
    int lastTrack{};

    std::int64_t startLba{};
    std::int64_t endLba{}; // Exclusive [startLba, endLba)

    int sessions{1};
    std::int64_t rawStartSector{};
    std::int64_t rawEndSector{};
};

struct DiscLayout {
    std::string sourceKind{};
    std::string sourcePath{};
    DiscInfo discInfo{};
    std::vector<TrackInfo> tracks{};
    DiscMetadata discMetadata{};
    std::vector<TrackMetadata> trackMetadata{};
};

} // namespace mycds
