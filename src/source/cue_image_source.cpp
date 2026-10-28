#include "source/cue_image_source.hpp"

#include "domain/audio_format.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace mycds {

CueImageSource::CueImageSource(const std::filesystem::path& cue_path)
    : parsed_(parse_cue_file(cue_path)) {}

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

std::vector<std::byte> CueImageSource::readAudioSectors(
    std::int64_t startLba,
    std::uint32_t sectorCount) {
    if (sectorCount == 0) {
        return {};
    }

    const auto& dinfo = parsed_.layout.discInfo;
    const std::int64_t endLba = startLba + static_cast<std::int64_t>(sectorCount);
    if (startLba < dinfo.startLba || endLba > dinfo.endLba) {
        throw std::out_of_range(
            "Requested LBA range [" + std::to_string(startLba) + ", " +
            std::to_string(endLba) + ") is outside CUE disc bounds [" +
            std::to_string(dinfo.startLba) + ", " + std::to_string(dinfo.endLba) + ")");
    }

    std::vector<std::byte> output(
        static_cast<std::size_t>(sectorCount) * AudioFormat::kBytesPerSector,
        std::byte{0});

    std::int64_t cursor_lba = startLba;
    std::size_t out_byte_offset = 0;

    while (cursor_lba < endLba) {
        // Find the track containing cursor_lba
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
            throw std::runtime_error(
                "No track found covering LBA " + std::to_string(cursor_lba));
        }

        if (found_track->type != TrackType::Audio) {
            throw std::runtime_error(
                "Cannot read audio sectors from non-audio track " +
                std::to_string(found_track->number) + " at LBA " +
                std::to_string(cursor_lba));
        }

        // Check if cursor_lba is inside synthetic pregap
        const std::int64_t syn_pre_end =
            found_storage->syntheticPregapStartLba + found_storage->syntheticPregapLength;
        if (found_storage->syntheticPregapLength > 0 && cursor_lba < syn_pre_end) {
            const std::int64_t run = std::min(endLba, syn_pre_end) - cursor_lba;
            out_byte_offset += static_cast<std::size_t>(run) * AudioFormat::kBytesPerSector;
            cursor_lba += run;
            continue;
        }

        // Check if cursor_lba is inside file-backed region
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
                throw std::runtime_error(
                    "Failed to open binary image file: '" +
                    found_storage->filePath.string() + "'");
            }
            bin_in.seekg(static_cast<std::streamoff>(byte_pos), std::ios::beg);
            if (!bin_in) {
                throw std::runtime_error(
                    "Failed to seek to byte offset " + std::to_string(byte_pos) +
                    " in '" + found_storage->filePath.string() + "'");
            }
            bin_in.read(
                reinterpret_cast<char*>(output.data() + out_byte_offset),
                static_cast<std::streamsize>(bytes_to_read));
            if (static_cast<std::size_t>(bin_in.gcount()) != bytes_to_read) {
                throw std::runtime_error(
                    "Short read from binary image file '" +
                    found_storage->filePath.string() + "' at LBA " +
                    std::to_string(cursor_lba));
            }

            if (found_storage->swapAudioBytes) {
                for (std::size_t b = 0; b < bytes_to_read; b += 2) {
                    std::swap(
                        output[out_byte_offset + b],
                        output[out_byte_offset + b + 1]);
                }
            }

            out_byte_offset += bytes_to_read;
            cursor_lba += run;
            continue;
        }

        // Check if cursor_lba is inside synthetic postgap
        const std::int64_t syn_post_end =
            found_storage->syntheticPostgapStartLba + found_storage->syntheticPostgapLength;
        if (found_storage->syntheticPostgapLength > 0 && cursor_lba < syn_post_end) {
            const std::int64_t run = std::min(endLba, syn_post_end) - cursor_lba;
            out_byte_offset += static_cast<std::size_t>(run) * AudioFormat::kBytesPerSector;
            cursor_lba += run;
            continue;
        }

        throw std::runtime_error(
            "Internal layout error at LBA " + std::to_string(cursor_lba));
    }

    return output;
}

} // namespace mycds
