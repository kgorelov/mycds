#include "source/cue_image_source.hpp"

#include "domain/audio_format.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace mycds {

namespace {

ParsedCueSheet parse_cue_file_or_throw_source_error(const std::filesystem::path& cue_path) {
    try {
        return parse_cue_file(cue_path);
    } catch (const DiscSourceError&) {
        throw;
    } catch (const std::exception& ex) {
        throw DiscSourceError(DiscSourceErrorKind::CorruptImage, ex.what());
    }
}

} // namespace

CueImageSource::CueImageSource(const std::filesystem::path& cue_path)
    : parsed_(parse_cue_file_or_throw_source_error(cue_path)) {}

std::string CueImageSource::getSourceKind() const {
    return parsed_.layout.sourceKind;
}

std::string CueImageSource::getSourcePath() const {
    return parsed_.layout.sourcePath;
}

DiscInfo CueImageSource::getDiscInfo() const {
    return parsed_.layout.discInfo;
}

std::vector<TrackInfo> CueImageSource::getTracks() const {
    return parsed_.layout.tracks;
}

DiscMetadata CueImageSource::getDiscMetadata() const {
    return parsed_.layout.discMetadata;
}

std::vector<TrackMetadata> CueImageSource::getTrackMetadata() const {
    return parsed_.layout.trackMetadata;
}

std::size_t CueImageSource::readAudioSectors(
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

    const auto& dinfo = parsed_.layout.discInfo;
    const std::int64_t endLba = startLba + static_cast<std::int64_t>(sectorCount);
    if (startLba < dinfo.startLba || endLba > dinfo.endLba) {
        throw DiscOutOfRangeError(
            "Requested LBA range [" + std::to_string(startLba) + ", " +
            std::to_string(endLba) + ") is outside CUE disc bounds [" +
            std::to_string(dinfo.startLba) + ", " + std::to_string(dinfo.endLba) + ")");
    }

    std::memset(destination.data(), 0, required_bytes);

    std::int64_t cursor_lba = startLba;
    std::size_t out_byte_offset = 0;

    while (cursor_lba < endLba) {
        const TrackInfo* found_track = nullptr;
        const CueTrackStorage* found_storage = nullptr;

        for (std::size_t i = 0; i < parsed_.layout.tracks.size(); ++i) {
            const auto& trk = parsed_.layout.tracks[i];
            if (cursor_lba >= trk.startLba && cursor_lba < trk.endLba) {
                found_track = &trk;
                found_storage = &parsed_.trackStorages[i];
                break;
            }
        }

        if (!found_track || !found_storage) {
            throw DiscSourceError(
                DiscSourceErrorKind::ReadFailed,
                "No track found covering LBA " + std::to_string(cursor_lba));
        }

        if (found_track->type != TrackType::Audio) {
            throw DiscSourceError(
                DiscSourceErrorKind::NonAudioTrack,
                "Cannot read audio sectors from non-audio track " +
                    std::to_string(found_track->number) + " at LBA " +
                    std::to_string(cursor_lba));
        }

        const std::int64_t syn_pre_end =
            found_storage->syntheticPregapStartLba + found_storage->syntheticPregapLength;
        if (found_storage->syntheticPregapLength > 0 && cursor_lba < syn_pre_end) {
            const std::int64_t run = std::min(endLba, syn_pre_end) - cursor_lba;
            out_byte_offset += static_cast<std::size_t>(run) * AudioFormat::kBytesPerSector;
            cursor_lba += run;
            continue;
        }

        const std::int64_t file_end =
            found_storage->fileBackedStartLba + found_storage->fileBackedLength;
        if (cursor_lba >= found_storage->fileBackedStartLba && cursor_lba < file_end) {
            const std::int64_t run = std::min(endLba, file_end) - cursor_lba;
            const std::int64_t sector_offset_in_track =
                cursor_lba - found_storage->fileBackedStartLba;
            const std::uint64_t byte_pos =
                found_storage->fileByteOffset +
                static_cast<std::uint64_t>(sector_offset_in_track) * AudioFormat::kBytesPerSector;
            const std::size_t bytes_to_read =
                static_cast<std::size_t>(run) * AudioFormat::kBytesPerSector;

            std::ifstream bin_in(found_storage->filePath, std::ios::binary);
            if (!bin_in) {
                throw DiscSourceError(
                    DiscSourceErrorKind::ReadFailed,
                    "Failed to open binary image file: '" +
                        found_storage->filePath.string() + "'");
            }
            bin_in.seekg(static_cast<std::streamoff>(byte_pos), std::ios::beg);
            if (!bin_in) {
                throw DiscSourceError(
                    DiscSourceErrorKind::ReadFailed,
                    "Failed to seek to byte offset " + std::to_string(byte_pos) +
                        " in '" + found_storage->filePath.string() + "'");
            }
            bin_in.read(
                reinterpret_cast<char*>(destination.data() + out_byte_offset),
                static_cast<std::streamsize>(bytes_to_read));
            if (static_cast<std::size_t>(bin_in.gcount()) != bytes_to_read) {
                throw DiscSourceError(
                    DiscSourceErrorKind::ReadFailed,
                    "Short read from binary image file '" +
                        found_storage->filePath.string() + "' at LBA " +
                        std::to_string(cursor_lba));
            }

            if (found_storage->swapAudioBytes) {
                swap_pcm16_byte_order(destination.subspan(out_byte_offset, bytes_to_read));
            }

            out_byte_offset += bytes_to_read;
            cursor_lba += run;
            continue;
        }

        const std::int64_t syn_post_end =
            found_storage->syntheticPostgapStartLba + found_storage->syntheticPostgapLength;
        if (found_storage->syntheticPostgapLength > 0 && cursor_lba < syn_post_end) {
            const std::int64_t run = std::min(endLba, syn_post_end) - cursor_lba;
            out_byte_offset += static_cast<std::size_t>(run) * AudioFormat::kBytesPerSector;
            cursor_lba += run;
            continue;
        }

        throw DiscSourceError(
            DiscSourceErrorKind::ReadFailed,
            "Internal layout error at LBA " + std::to_string(cursor_lba));
    }

    return required_bytes;
}

std::vector<std::byte> CueImageSource::readAudioSectors(
    std::int64_t startLba,
    std::uint32_t sectorCount) {
    if (sectorCount == 0) {
        return {};
    }
    std::vector<std::byte> output(
        static_cast<std::size_t>(sectorCount) * AudioFormat::kBytesPerSector);
    readAudioSectors(startLba, sectorCount, output);
    return output;
}

} // namespace mycds
