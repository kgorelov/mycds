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

} // namespace mycds
