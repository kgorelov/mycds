#pragma once

#include "source/disc_source.hpp"

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mycds {

struct CdDriveInfo {
    std::string devicePath{};
    std::string vendor{};
    std::string model{};
    bool hasDisc{false};
    int trackCount{0};
    int totalSeconds{0};
    std::string statusText{};
};

/// Enumerates optical CD/DVD/BD drives visible to libcdio on the system.
std::vector<CdDriveInfo> enumerate_cd_drives();

/// Attempts to eject the media tray for the specified optical drive device.
bool eject_cd_drive(const std::string& device_path);

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
