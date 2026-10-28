#pragma once

#include "source/disc_source.hpp"

#include <memory>
#include <span>
#include <string>

namespace mycds {

class PhysicalCdSource final : public DiscSource {
public:
    explicit PhysicalCdSource(const std::string& device_path);
    ~PhysicalCdSource() override;

    PhysicalCdSource(const PhysicalCdSource&) = delete;
    PhysicalCdSource& operator=(const PhysicalCdSource&) = delete;
    PhysicalCdSource(PhysicalCdSource&&) noexcept = default;
    PhysicalCdSource& operator=(PhysicalCdSource&&) noexcept = default;

    std::string getSourceKind() const override;
    std::string getSourcePath() const override;

    DiscInfo getDiscInfo() const override;
    std::vector<TrackInfo> getTracks() const override;

    DiscMetadata getDiscMetadata() const override;
    std::vector<TrackMetadata> getTrackMetadata() const override;

    std::vector<std::byte> readAudioSectors(
        std::int64_t startLba,
        std::uint32_t sectorCount) override;

    std::size_t readAudioSectors(
        std::int64_t startLba,
        std::uint32_t sectorCount,
        std::span<std::byte> destination) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mycds
