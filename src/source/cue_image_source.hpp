#pragma once

#include "source/cue_parser.hpp"
#include "source/disc_source.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace mycds {

class CueImageSource final : public DiscSource {
public:
    explicit CueImageSource(const std::filesystem::path& cue_path);
    ~CueImageSource() override = default;

    std::string getSourceKind() const override;
    std::string getSourcePath() const override;

    DiscInfo getDiscInfo() const override;
    std::vector<TrackInfo> getTracks() const override;

    DiscMetadata getDiscMetadata() const override;
    std::vector<TrackMetadata> getTrackMetadata() const override;

    std::vector<std::byte> readAudioSectors(
        std::int64_t startLba,
        std::uint32_t sectorCount) override;

private:
    ParsedCueSheet parsed_;
};

} // namespace mycds
