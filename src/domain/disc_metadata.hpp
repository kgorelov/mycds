#pragma once

#include <optional>
#include <string>

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

    bool empty() const noexcept {
        return title.empty() && performer.empty() && songwriter.empty() &&
               catalog.empty() && genre.empty() && !releaseYear.has_value() &&
               databaseProvider.empty() && databaseDiscId.empty();
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

} // namespace mycds
