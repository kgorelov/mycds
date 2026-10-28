#pragma once

#include "domain/disc_info.hpp"
#include "domain/disc_metadata.hpp"
#include "domain/track_info.hpp"

#include <cstdint>
#include <filesystem>
#include <istream>
#include <string>
#include <vector>

namespace mycds {

enum class CueFileType {
    Binary,
    Motorola
};

struct CueTrackStorage {
    int trackNumber{};
    TrackType trackType{TrackType::Unknown};
    std::string modeString{};
    std::filesystem::path filePath{};
    CueFileType fileType{CueFileType::Binary};
    bool swapAudioBytes{false};
    std::uint32_t sectorByteSize{2352};

    std::int64_t syntheticPregapStartLba{0};
    std::int64_t syntheticPregapLength{0};

    std::int64_t fileBackedStartLba{0};
    std::int64_t fileBackedLength{0};
    std::uint64_t fileByteOffset{0};

    std::int64_t syntheticPostgapStartLba{0};
    std::int64_t syntheticPostgapLength{0};
};

struct ParsedCueSheet {
    DiscLayout layout{};
    std::vector<CueTrackStorage> trackStorages{};
};

ParsedCueSheet parse_cue_file(const std::filesystem::path& cue_path);

ParsedCueSheet parse_cue_stream(
    std::istream& input,
    const std::filesystem::path& base_dir,
    const std::string& source_path_label = "<stream>");

} // namespace mycds
