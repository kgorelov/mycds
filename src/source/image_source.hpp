#pragma once

#include "source/disc_source.hpp"

#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace mycds {

// Unified disc-image adapter supporting BIN/TOC (via libMirage) and BIN/CUE images.
class ImageSource final : public DiscSource {
public:
    explicit ImageSource(const std::filesystem::path& image_path);
    ~ImageSource() override = default;

    ImageSource(const ImageSource&) = delete;
    ImageSource& operator=(const ImageSource&) = delete;
    ImageSource(ImageSource&&) noexcept = default;
    ImageSource& operator=(ImageSource&&) noexcept = default;

    std::string getSourceKind() const override;
    std::string getSourcePath() const override;

    DiscInfo getDiscInfo() const override;
    std::vector<TrackInfo> getTracks() const override;

    DiscMetadata getDiscMetadata() const override;
    std::vector<TrackMetadata> getTrackMetadata() const override;

    DiscLayout getDiscLayout() const override;

    std::vector<std::byte> readAudioSectors(
        std::int64_t startLba,
        std::uint32_t sectorCount) override;

    std::size_t readAudioSectors(
        std::int64_t startLba,
        std::uint32_t sectorCount,
        std::span<std::byte> destination) override;

private:
    std::unique_ptr<DiscSource> delegate_;
};

} // namespace mycds
