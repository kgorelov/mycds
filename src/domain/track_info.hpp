#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace mycds {

enum class TrackType {
    Audio,
    Data,
    Unknown
};

struct IndexInfo {
    int trackNumber{};
    int number{};
    std::int64_t lba{};
    bool explicitFromSource{true};
};

struct TrackInfo {
    int number{};
    TrackType type{TrackType::Unknown};

    std::int64_t startLba{};
    std::int64_t endLba{}; // Exclusive [startLba, endLba)
    std::int64_t index01Lba{};

    std::optional<std::int64_t> pregapStartLba{};
    std::optional<std::int64_t> pregapLength{};
    bool pregapInFile{false};
    std::int64_t postgapLength{0};

    std::vector<IndexInfo> indexes{};
};

inline const TrackInfo* find_track_at_lba(
    const std::vector<TrackInfo>& tracks,
    std::int64_t lba) noexcept {
    for (const auto& trk : tracks) {
        if (lba >= trk.startLba && lba < trk.endLba) {
            return &trk;
        }
    }
    return nullptr;
}

inline const TrackInfo* find_track_by_number(
    const std::vector<TrackInfo>& tracks,
    int track_number) noexcept {
    for (const auto& trk : tracks) {
        if (trk.number == track_number) {
            return &trk;
        }
    }
    return nullptr;
}

inline const IndexInfo* find_index_at_lba(
    const TrackInfo& track,
    std::int64_t lba) noexcept {
    if (lba < track.startLba || lba >= track.endLba) {
        return nullptr;
    }
    const IndexInfo* active = nullptr;
    for (const auto& idx : track.indexes) {
        if (idx.lba <= lba) {
            active = &idx;
        } else {
            break;
        }
    }
    return active;
}

} // namespace mycds
