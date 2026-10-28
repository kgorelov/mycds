#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mycds {

enum class TrackType {
    Audio,
    Data,
    Unknown
};

struct IndexEntry {
    int trackNumber{};
    int indexNumber{};
    std::int64_t lba{};
    bool explicitFromSource{true};
};

struct TrackInspection {
    int number{};
    TrackType type{TrackType::Unknown};
    std::int64_t startLba{};
    std::int64_t endLba{}; // Exclusive [startLba, endLba)
    std::int64_t index01Lba{};
    std::optional<std::int64_t> pregapStartLba{};
    std::optional<std::int64_t> pregapLength{};
    std::vector<IndexEntry> indexes{};
    std::string isrc{};
};

struct DiscInspection {
    std::string sourceKind{};
    std::string sourcePath{};
    int sessions{1};
    int firstTrack{};
    int lastTrack{};
    std::int64_t startLba{};
    std::int64_t endLba{}; // Exclusive [startLba, endLba)
    std::int64_t rawStartSector{};
    std::int64_t rawEndSector{};
    std::string mcn{};
    std::vector<TrackInspection> tracks{};
};

bool inspect_physical_cd(
    const std::string& device_path,
    DiscInspection& out_disc,
    std::string& out_error);

bool inspect_image(
    const std::string& image_path,
    DiscInspection& out_disc,
    std::string& out_error);

void print_disc_inspection(const DiscInspection& disc);

bool compare_disc_inspections(
    const DiscInspection& physical,
    const DiscInspection& image);

} // namespace mycds
