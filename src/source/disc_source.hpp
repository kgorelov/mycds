#pragma once

#include "domain/disc_info.hpp"
#include "domain/disc_metadata.hpp"
#include "domain/track_info.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mycds {

class DiscSource {
public:
    virtual ~DiscSource() = default;

    virtual std::string getSourceKind() const = 0;
    virtual std::string getSourcePath() const = 0;

    virtual DiscInfo getDiscInfo() const = 0;
    virtual std::vector<TrackInfo> getTracks() const = 0;

    virtual DiscMetadata getDiscMetadata() const = 0;
    virtual std::vector<TrackMetadata> getTrackMetadata() const = 0;

    virtual DiscLayout getDiscLayout() const {
        return DiscLayout{
            .sourceKind = getSourceKind(),
            .sourcePath = getSourcePath(),
            .discInfo = getDiscInfo(),
            .tracks = getTracks(),
            .discMetadata = getDiscMetadata(),
            .trackMetadata = getTrackMetadata(),
        };
    }

    virtual std::vector<std::byte> readAudioSectors(
        std::int64_t startLba,
        std::uint32_t sectorCount) = 0;
};

std::unique_ptr<DiscSource> open_physical_cd_source(const std::string& device_path);
std::unique_ptr<DiscSource> open_image_source(const std::string& image_path);

} // namespace mycds
