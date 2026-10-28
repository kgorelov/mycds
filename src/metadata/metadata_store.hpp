#pragma once

#include "domain/disc_info.hpp"
#include "domain/disc_metadata.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace mycds {

class MetadataStore {
public:
    static std::filesystem::path defaultSidecarPath(const DiscLayout& layout);

    static bool exists(const std::filesystem::path& sidecar_path);

    static std::string serializeToJson(const MetadataRecord& record);
    static MetadataRecord deserializeFromJson(std::string_view json_text);

    static void saveSidecar(
        const std::filesystem::path& sidecar_path,
        const MetadataRecord& record);

    static MetadataRecord loadSidecar(const std::filesystem::path& sidecar_path);

    static std::string updateCueContentWithMetadata(
        std::string_view original_cue_content,
        const MetadataRecord& record);

    static void exportToCue(
        const std::filesystem::path& cue_path,
        const MetadataRecord& record,
        const std::filesystem::path& template_cue_path = {});
};

} // namespace mycds
