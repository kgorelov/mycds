#include "metadata/metadata_workflow.hpp"

#include "inspect.hpp"
#include "metadata/cddb_disc_id.hpp"

#include <cctype>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace mycds {

namespace {

std::string trim_input(std::string_view sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
        sv.remove_suffix(1);
    }
    return std::string(sv);
}

std::filesystem::path resolve_cue_export_target(
    const DiscLayout& layout,
    const std::string& arg_path) {
    namespace fs = std::filesystem;
    if (!arg_path.empty()) {
        return fs::path(arg_path);
    }
    if (!layout.sourcePath.empty() && layout.sourcePath.rfind("/dev/", 0) != 0) {
        const fs::path src(layout.sourcePath);
        return src.parent_path() / (src.stem().string() + ".cue");
    }
    throw std::runtime_error("Please specify a target .cue file path for export-cue");
}

std::filesystem::path resolve_cue_template(
    const DiscLayout& layout,
    const std::filesystem::path& target_cue) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(target_cue, ec)) {
        return target_cue;
    }
    if (!layout.sourcePath.empty() && layout.sourcePath.rfind("/dev/", 0) != 0) {
        const fs::path src(layout.sourcePath);
        if (src.extension() == ".cue" || src.extension() == ".CUE") {
            return src;
        }
        const fs::path sibling_cue = src.parent_path() / (src.stem().string() + ".cue");
        if (fs::exists(sibling_cue, ec)) {
            return sibling_cue;
        }
    }
    return target_cue;
}

} // namespace

MetadataRecord extract_metadata_record(const DiscLayout& layout) {
    MetadataRecord rec{};
    rec.disc = layout.discMetadata;
    if (rec.disc.databaseDiscId.empty() && !layout.tracks.empty()) {
        try {
            rec.disc.databaseDiscId =
                calculate_cddb_disc_id(layout.discInfo, layout.tracks).discIdHex;
        } catch (...) {
        }
    }
    rec.tracks = layout.trackMetadata;
    if (rec.tracks.size() < layout.tracks.size()) {
        for (const auto& t : layout.tracks) {
            bool found = false;
            for (const auto& tm : rec.tracks) {
                if (tm.trackNumber == t.number) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                rec.tracks.push_back(TrackMetadata{.trackNumber = t.number});
            }
        }
    }
    return rec;
}

void print_metadata_summary(
    const DiscLayout& layout,
    std::ostream& out,
    const std::optional<MetadataValidationResult>& validation) {
    const auto& meta = layout.discMetadata;
    std::string computed_disc_id = meta.databaseDiscId;
    if (computed_disc_id.empty() && !layout.tracks.empty()) {
        try {
            computed_disc_id =
                calculate_cddb_disc_id(layout.discInfo, layout.tracks).discIdHex;
        } catch (...) {
        }
    }

    out << "------------------------------------------------------------------------------------\n";
    out << "Album Metadata Summary:\n";
    out << "  Performer     : " << (meta.performer.empty() ? "<unset>" : meta.performer) << "\n";
    out << "  Title         : " << (meta.title.empty() ? "<unset>" : meta.title) << "\n";
    if (!meta.songwriter.empty()) {
        out << "  Songwriter    : " << meta.songwriter << "\n";
    }
    out << "  Genre         : " << (meta.genre.empty() ? "<unset>" : meta.genre) << "\n";
    out << "  Year          : "
        << (meta.releaseYear.has_value() ? std::to_string(*meta.releaseYear) : "<unset>") << "\n";
    if (!meta.catalog.empty()) {
        out << "  Catalog       : " << meta.catalog << "\n";
    }
    out << "  CDDB Disc ID  : " << (computed_disc_id.empty() ? "<n/a>" : computed_disc_id) << "\n";
    if (!meta.databaseProvider.empty()) {
        out << "  Provider      : " << meta.databaseProvider << "\n";
    }
    if (!meta.externalRecordId.empty()) {
        out << "  External ID   : " << meta.externalRecordId << "\n";
    }
    if (!meta.lookupTimestamp.empty()) {
        out << "  Lookup Time   : " << meta.lookupTimestamp << "\n";
    }
    out << "  User Edited   : " << (meta.userCorrected ? "yes" : "no") << "\n";
    if (!meta.userNotes.empty()) {
        out << "  User Notes    : " << meta.userNotes << "\n";
    }

    if (validation.has_value()) {
        out << "  Validation    : " << match_quality_to_string(validation->quality)
            << (validation->valid ? " [VALID]" : " [INVALID]") << "\n";
        if (validation->offsetsAvailable) {
            out << "  Offset Delta  : max " << validation->maxFrameOffsetDelta
                << " frames, length delta " << validation->discLengthSecondsDelta << "s\n";
        }
        for (const auto& warn : validation->warnings) {
            out << "  [WARNING] " << warn << "\n";
        }
        for (const auto& err : validation->errors) {
            out << "  [ERROR]   " << err << "\n";
        }
    }

    out << "------------------------------------------------------------------------------------\n";
    out << "Tracks (" << layout.tracks.size() << "):\n";
    for (const auto& trk : layout.tracks) {
        const TrackMetadata* tm_ptr = nullptr;
        for (const auto& tm : layout.trackMetadata) {
            if (tm.trackNumber == trk.number) {
                tm_ptr = &tm;
                break;
            }
        }
        out << "  Track " << std::right << std::setw(2) << trk.number << ": ";
        if (tm_ptr && !tm_ptr->title.empty()) {
            if (!tm_ptr->performer.empty()) {
                out << tm_ptr->performer << " - ";
            }
            out << tm_ptr->title;
        } else {
            out << "<untitled>";
        }
        if (tm_ptr && !tm_ptr->isrc.empty()) {
            out << " [ISRC: " << tm_ptr->isrc << "]";
        }
        out << "\n";
    }
    out << "------------------------------------------------------------------------------------\n";
}

bool run_interactive_metadata_lookup(
    DiscLayout& layout,
    MetadataProvider& provider,
    std::istream& in,
    std::ostream& out,
    const std::filesystem::path& sidecar_path,
    std::optional<std::size_t> preselected_one_based_index,
    bool auto_save) {
    try {
        const CddbDiscQueryInfo query_info =
            calculate_cddb_disc_id(layout.discInfo, layout.tracks);
        out << "[Metadata] Computed CDDB Disc ID: " << query_info.discIdHex
            << " (" << query_info.trackCount << " tracks, "
            << query_info.totalLengthSeconds << "s)\n";
        out << "[Metadata] Querying provider: " << provider.providerName() << "...\n";

        const std::vector<MetadataCandidate> candidates =
            provider.search(layout.discInfo, layout.tracks);

        if (candidates.empty()) {
            out << "[Metadata] No matches found for disc ID " << query_info.discIdHex
                << ". You can enter metadata manually with 'metadata edit'.\n";
            return false;
        }

        out << "[Metadata] Found " << candidates.size() << " candidate match(es):\n";
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            const auto& c = candidates[i];
            out << "  [" << (i + 1) << "] " << c.externalId() << " — " << c.rawTitle
                << (c.exactMatch ? " (exact)" : " (inexact)") << "\n";
        }

        std::size_t chosen_idx = 0;
        if (preselected_one_based_index.has_value()) {
            if (*preselected_one_based_index == 0 ||
                *preselected_one_based_index > candidates.size()) {
                out << "[Metadata] Invalid selection index " << *preselected_one_based_index
                    << " (valid range: 1.." << candidates.size() << ")\n";
                return false;
            }
            chosen_idx = *preselected_one_based_index - 1;
        } else if (candidates.size() == 1) {
            chosen_idx = 0;
            out << "[Metadata] Selecting single match [1]: "
                << candidates[0].externalId() << "\n";
        } else {
            out << "Select candidate [1-" << candidates.size()
                << ", or 0 to cancel, default=1]: ";
            out.flush();
            std::string choice_line;
            if (!std::getline(in, choice_line)) {
                chosen_idx = 0;
            } else {
                const std::string trimmed = trim_input(choice_line);
                if (trimmed.empty()) {
                    chosen_idx = 0;
                } else {
                    const int val = std::stoi(trimmed);
                    if (val == 0) {
                        out << "[Metadata] Lookup cancelled.\n";
                        return false;
                    }
                    if (val < 1 || static_cast<std::size_t>(val) > candidates.size()) {
                        out << "[Metadata] Selection out of range.\n";
                        return false;
                    }
                    chosen_idx = static_cast<std::size_t>(val - 1);
                }
            }
        }

        const MetadataCandidate& selected = candidates[chosen_idx];
        out << "[Metadata] Fetching full record for " << selected.externalId() << "...\n";
        const MetadataRecord record =
            provider.fetch(selected, layout.discInfo, layout.tracks);

        const MetadataValidationResult validation =
            MetadataMatcher::validate(layout.discInfo, layout.tracks, record);

        if (!validation.valid) {
            out << "[Metadata] Validation failed against local disc layout:\n";
            for (const auto& err : validation.errors) {
                out << "  [ERROR] " << err << "\n";
            }
            return false;
        }

        MetadataMatcher::applyToLayout(layout, record);
        print_metadata_summary(layout, out, validation);

        const std::filesystem::path target_sidecar =
            !sidecar_path.empty() ? sidecar_path : MetadataStore::defaultSidecarPath(layout);

        if (auto_save) {
            MetadataStore::saveSidecar(target_sidecar, extract_metadata_record(layout));
            out << "[Metadata] Saved sidecar metadata to " << target_sidecar.string() << "\n";
        }

        return true;
    } catch (const std::exception& ex) {
        out << "[Metadata] Lookup error: " << ex.what() << "\n";
        return false;
    }
}

void run_interactive_metadata_edit(
    DiscLayout& layout,
    std::istream& in,
    std::ostream& out) {
    if (layout.trackMetadata.size() < layout.tracks.size()) {
        layout.trackMetadata = extract_metadata_record(layout).tracks;
    }

    auto prompt_field = [&](const std::string& label, std::string& current_val) {
        out << label << " [" << current_val << "]: ";
        out.flush();
        std::string line;
        if (std::getline(in, line)) {
            const std::string trimmed = trim_input(line);
            if (!trimmed.empty()) {
                current_val = trimmed;
                layout.discMetadata.userCorrected = true;
            }
        }
    };

    out << "--- Editing Album Metadata (press Enter to keep current value) ---\n";
    prompt_field("Album Performer", layout.discMetadata.performer);
    prompt_field("Album Title", layout.discMetadata.title);
    prompt_field("Genre", layout.discMetadata.genre);

    const std::string current_year =
        layout.discMetadata.releaseYear.has_value()
            ? std::to_string(*layout.discMetadata.releaseYear)
            : "";
    std::string new_year = current_year;
    prompt_field("Release Year", new_year);
    if (new_year != current_year && !new_year.empty()) {
        try {
            layout.discMetadata.releaseYear = std::stoi(new_year);
        } catch (...) {
        }
    }

    prompt_field("User Notes", layout.discMetadata.userNotes);

    for (auto& tm : layout.trackMetadata) {
        prompt_field(
            "Track " + std::to_string(tm.trackNumber) + " Title",
            tm.title);
    }

    if (layout.discMetadata.databaseDiscId.empty() && !layout.tracks.empty()) {
        try {
            layout.discMetadata.databaseDiscId =
                calculate_cddb_disc_id(layout.discInfo, layout.tracks).discIdHex;
        } catch (...) {
        }
    }
}

int run_metadata_repl(
    DiscLayout& layout,
    MetadataProvider& provider,
    std::istream& in,
    std::ostream& out,
    const std::filesystem::path& custom_sidecar_path) {
    out << "Interactive Console Ready. Type 'help' for available commands, 'quit' to exit.\n";

    std::string line;
    while (true) {
        out << "mycds> ";
        out.flush();
        if (!std::getline(in, line)) {
            out << "\n";
            break;
        }

        const std::string trimmed = trim_input(line);
        if (trimmed.empty()) {
            continue;
        }

        std::istringstream iss(trimmed);
        std::string cmd;
        iss >> cmd;

        if (cmd == "quit" || cmd == "exit") {
            break;
        }
        if (cmd == "help") {
            out << "Commands:\n"
                << "  info                         Show full disc layout and metadata\n"
                << "  tracks                       Show track list and titles\n"
                << "  metadata lookup [index]      Query CDDB provider and select candidate\n"
                << "  metadata show                Display current metadata summary\n"
                << "  metadata edit                Interactively edit album and track metadata\n"
                << "  metadata save [path]         Save metadata to sidecar JSON file\n"
                << "  metadata export-cue [path]   Export metadata to CUE file without altering layout\n"
                << "  quit                         Exit interactive console\n";
            continue;
        }
        if (cmd == "info" || cmd == "tracks") {
            print_disc_inspection(layout);
            continue;
        }
        if (cmd == "metadata") {
            std::string subcmd;
            iss >> subcmd;
            if (subcmd == "lookup") {
                std::optional<std::size_t> idx;
                std::size_t parsed_idx = 0;
                if (iss >> parsed_idx) {
                    idx = parsed_idx;
                }
                run_interactive_metadata_lookup(
                    layout, provider, in, out, custom_sidecar_path, idx, false);
            } else if (subcmd == "show") {
                print_metadata_summary(layout, out);
            } else if (subcmd == "edit") {
                run_interactive_metadata_edit(layout, in, out);
                print_metadata_summary(layout, out);
            } else if (subcmd == "save") {
                std::string path_arg;
                std::getline(iss, path_arg);
                path_arg = trim_input(path_arg);
                const std::filesystem::path target =
                    !path_arg.empty() ? std::filesystem::path(path_arg)
                    : !custom_sidecar_path.empty() ? custom_sidecar_path
                    : MetadataStore::defaultSidecarPath(layout);
                try {
                    MetadataStore::saveSidecar(target, extract_metadata_record(layout));
                    out << "[Metadata] Saved sidecar metadata to " << target.string() << "\n";
                } catch (const std::exception& ex) {
                    out << "[Metadata] Save error: " << ex.what() << "\n";
                }
            } else if (subcmd == "export-cue") {
                std::string path_arg;
                std::getline(iss, path_arg);
                path_arg = trim_input(path_arg);
                try {
                    const auto target_cue = resolve_cue_export_target(layout, path_arg);
                    const auto template_cue = resolve_cue_template(layout, target_cue);
                    MetadataStore::exportToCue(
                        target_cue, extract_metadata_record(layout), template_cue);
                    out << "[Metadata] Exported metadata to CUE file: "
                        << target_cue.string() << "\n";
                } catch (const std::exception& ex) {
                    out << "[Metadata] CUE export error: " << ex.what() << "\n";
                }
            } else {
                out << "Unknown metadata subcommand '" << subcmd
                    << "'. Try: lookup, show, edit, save, export-cue\n";
            }
            continue;
        }

        out << "Unknown command: '" << cmd << "'. Type 'help' for available commands.\n";
    }

    return 0;
}

} // namespace mycds
