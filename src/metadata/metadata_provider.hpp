#pragma once

#include "domain/disc_info.hpp"
#include "domain/disc_metadata.hpp"
#include "domain/track_info.hpp"

#include <string>
#include <vector>

namespace mycds {

struct MetadataCandidate {
    std::string category{};
    std::string discId{};
    std::string rawTitle{};
    std::string performer{};
    std::string title{};
    bool exactMatch{true};

    std::string externalId() const {
        if (category.empty()) {
            return discId;
        }
        return category + "/" + discId;
    }
};

class MetadataProvider {
public:
    virtual ~MetadataProvider() = default;

    virtual std::string providerName() const = 0;

    virtual std::vector<MetadataCandidate> search(
        const DiscInfo& disc_info,
        const std::vector<TrackInfo>& tracks) = 0;

    virtual MetadataRecord fetch(
        const MetadataCandidate& candidate,
        const DiscInfo& disc_info,
        const std::vector<TrackInfo>& tracks) = 0;
};

} // namespace mycds
