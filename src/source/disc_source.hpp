#pragma once

#include "domain/audio_format.hpp"
#include "domain/disc_info.hpp"
#include "domain/disc_metadata.hpp"
#include "domain/track_info.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace mycds {

enum class DiscSourceErrorKind {
    OpenFailed,
    NoDisc,
    UnsupportedLayout,
    CorruptImage,
    OutOfRangeLba,
    NonAudioTrack,
    ReadFailed,
    DiscRemoved
};

class DiscSourceError : public std::runtime_error {
public:
    DiscSourceError(DiscSourceErrorKind kind, const std::string& message)
        : std::runtime_error(message), kind_(kind) {}

    DiscSourceErrorKind kind() const noexcept { return kind_; }

private:
    DiscSourceErrorKind kind_;
};

class DiscOutOfRangeError : public std::out_of_range {
public:
    explicit DiscOutOfRangeError(const std::string& message)
        : std::out_of_range(message) {}
};

// Common DiscSource abstraction for physical CDs and disc images.
//
// Contract for readAudioSectors():
// 1. Requested range [startLba, startLba + sectorCount) is expressed in normalized
//    absolute LBAs (where Track 1 INDEX 01 on a standard Red Book CD is LBA 0).
// 2. Throws DiscOutOfRangeError (std::out_of_range) if the requested range falls
//    outside [discInfo.startLba, discInfo.endLba).
// 3. Throws DiscSourceError(NonAudioTrack) if any sector in the requested range
//    belongs to a non-audio track (e.g. Mode 1 / Mode 2 data track).
// 4. Returns exactly sectorCount * 2352 bytes of 16-bit signed little-endian
//    stereo interleaved PCM (44,100 Hz, 2 channels) or throws on read failure.
// 5. Stateless with respect to caller position: reading does not mutate a shared
//    cursor visible to other logical operations.
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

    virtual std::size_t readAudioSectors(
        std::int64_t startLba,
        std::uint32_t sectorCount,
        std::span<std::byte> destination) {
        const std::size_t required_bytes =
            static_cast<std::size_t>(sectorCount) * AudioFormat::kBytesPerSector;
        if (destination.size() < required_bytes) {
            throw std::invalid_argument(
                "Destination buffer too small for requested sectorCount");
        }
        if (sectorCount == 0) {
            return 0;
        }
        const std::vector<std::byte> data = readAudioSectors(startLba, sectorCount);
        std::memcpy(destination.data(), data.data(), required_bytes);
        return required_bytes;
    }
};

std::unique_ptr<DiscSource> open_physical_cd_source(const std::string& device_path);
std::unique_ptr<DiscSource> open_image_source(const std::string& image_path);

} // namespace mycds
