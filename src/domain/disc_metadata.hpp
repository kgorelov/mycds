#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mycds {

struct DiscMetadata {
    std::string title{};
    std::string performer{};
    std::string songwriter{};
    std::string catalog{};
    std::string genre{};
    std::optional<int> releaseYear{};
    std::string databaseProvider{};
    std::string databaseDiscId{};
    std::string externalRecordId{};
    std::string lookupTimestamp{};
    bool userCorrected{false};
    std::string userNotes{};
    std::string comment{};

    bool empty() const noexcept {
        return title.empty() && performer.empty() && songwriter.empty() &&
               catalog.empty() && genre.empty() && !releaseYear.has_value() &&
               databaseProvider.empty() && databaseDiscId.empty() &&
               externalRecordId.empty() && lookupTimestamp.empty() &&
               !userCorrected && userNotes.empty() && comment.empty();
    }
};

struct TrackMetadata {
    int trackNumber{};
    std::string title{};
    std::string performer{};
    std::string songwriter{};
    std::string isrc{};

    bool empty() const noexcept {
        return title.empty() && performer.empty() && songwriter.empty() && isrc.empty();
    }
};

struct MetadataRecord {
    DiscMetadata disc{};
    std::vector<TrackMetadata> tracks{};
    std::vector<std::int64_t> remoteTrackFrameOffsets{};
    std::optional<std::int64_t> remoteDiscLengthSeconds{};

    bool empty() const noexcept {
        if (!disc.empty()) {
            return false;
        }
        for (const auto& trk : tracks) {
            if (!trk.empty()) {
                return false;
            }
        }
        return true;
    }
};

} // namespace mycds
