#include "source/cue_parser.hpp"

#include "domain/audio_format.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace mycds {

namespace {

std::string to_upper_ascii(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (unsigned char c : sv) {
        out.push_back(static_cast<char>(std::toupper(c)));
    }
    return out;
}

std::string trim_whitespace(std::string_view sv) {
    std::size_t first = 0;
    while (first < sv.size() && std::isspace(static_cast<unsigned char>(sv[first]))) {
        ++first;
    }
    std::size_t last = sv.size();
    while (last > first && std::isspace(static_cast<unsigned char>(sv[last - 1]))) {
        --last;
    }
    return std::string(sv.substr(first, last - first));
}

struct Token {
    std::string value{};
    bool quoted{false};
};

bool tokenize_cue_line(
    std::string_view line,
    std::vector<Token>& out_tokens,
    std::string& out_error) {
    out_tokens.clear();
    std::size_t i = 0;
    const std::size_t n = line.size();

    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
        }
        if (i >= n) {
            break;
        }

        if (line[i] == '"') {
            ++i;
            std::string val;
            bool closed = false;
            while (i < n) {
                if (line[i] == '\\' && i + 1 < n && (line[i + 1] == '"' || line[i + 1] == '\\')) {
                    val.push_back(line[i + 1]);
                    i += 2;
                } else if (line[i] == '"') {
                    closed = true;
                    ++i;
                    break;
                } else {
                    val.push_back(line[i]);
                    ++i;
                }
            }
            if (!closed) {
                out_error = "Unclosed quoted string";
                return false;
            }
            out_tokens.push_back(Token{.value = std::move(val), .quoted = true});
        } else {
            std::string val;
            while (i < n && !std::isspace(static_cast<unsigned char>(line[i]))) {
                if (line[i] == '"') {
                    out_error = "Unexpected quote inside unquoted token";
                    return false;
                }
                val.push_back(line[i]);
                ++i;
            }
            out_tokens.push_back(Token{.value = std::move(val), .quoted = false});
        }
    }

    return true;
}

bool is_valid_catalog(std::string_view s) {
    if (s.size() != 13) {
        return false;
    }
    for (unsigned char c : s) {
        if (!std::isdigit(c)) {
            return false;
        }
    }
    return true;
}

bool is_valid_isrc(std::string_view s) {
    if (s.size() != 12) {
        return false;
    }
    for (unsigned char c : s) {
        if (!std::isalnum(c)) {
            return false;
        }
    }
    return true;
}

std::optional<int> parse_strict_int(std::string_view s) {
    if (s.empty()) {
        return std::nullopt;
    }
    int val = 0;
    for (unsigned char c : s) {
        if (!std::isdigit(c)) {
            return std::nullopt;
        }
        val = val * 10 + (c - '0');
        if (val > 1000000) {
            return std::nullopt;
        }
    }
    return val;
}

struct RawCueIndex {
    int number{};
    std::int64_t fileSector{};
    int lineNumber{};
};

struct RawCueTrack {
    int number{};
    int lineNumber{};
    TrackType type{TrackType::Unknown};
    std::string modeString{};
    std::uint32_t sectorByteSize{2352};

    std::size_t fileGroupIndex{};
    std::string title{};
    std::string performer{};
    std::string songwriter{};
    std::string isrc{};

    std::optional<std::int64_t> pregapLength{};
    std::optional<std::int64_t> postgapLength{};
    std::vector<RawCueIndex> indices{};
};

struct RawCueFileGroup {
    std::string rawFilename{};
    std::filesystem::path resolvedPath{};
    CueFileType fileType{CueFileType::Binary};
    bool swapAudioBytes{false};
    int lineNumber{};
};

std::filesystem::path resolve_data_file_path(
    const std::filesystem::path& base_dir,
    const std::string& raw_filename) {
    std::string normalized = raw_filename;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    const std::filesystem::path rel_path(normalized);

    if (rel_path.is_absolute()) {
        return rel_path;
    }

    const std::filesystem::path direct = base_dir / rel_path;
    std::error_code ec;
    if (std::filesystem::exists(direct, ec)) {
        return direct;
    }

    // Fallback: check filename only in base_dir (including case-insensitive match)
    const std::filesystem::path filename_only = rel_path.filename();
    const std::filesystem::path in_base = base_dir / filename_only;
    if (std::filesystem::exists(in_base, ec)) {
        return in_base;
    }

    if (std::filesystem::is_directory(base_dir, ec)) {
        const std::string target_upper = to_upper_ascii(filename_only.string());
        for (const auto& entry : std::filesystem::directory_iterator(base_dir, ec)) {
            if (to_upper_ascii(entry.path().filename().string()) == target_upper) {
                return entry.path();
            }
        }
    }

    return direct;
}

bool is_cdrdao_big_endian_bin(
    const std::filesystem::path& base_dir,
    const std::filesystem::path& bin_path) {
    std::error_code ec;
    if (!std::filesystem::is_directory(base_dir, ec)) {
        return false;
    }

    const std::string bin_filename = bin_path.filename().string();
    if (bin_filename.empty()) {
        return false;
    }

    for (const auto& entry : std::filesystem::directory_iterator(base_dir, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        if (to_upper_ascii(entry.path().extension().string()) != ".TOC") {
            continue;
        }

        std::ifstream toc_in(entry.path());
        if (!toc_in) {
            continue;
        }
        std::string line;
        bool has_cd_header = false;
        while (std::getline(toc_in, line)) {
            const std::string trimmed = trim_whitespace(line);
            if (trimmed == "CD_DA" || trimmed == "CD_ROM" || trimmed == "CD_ROM_XA") {
                has_cd_header = true;
            }
            if (has_cd_header &&
                (trimmed.rfind("FILE ", 0) == 0 || trimmed.rfind("DATAFILE ", 0) == 0) &&
                trimmed.find("\"" + bin_filename + "\"") != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

void throw_cue_error(const std::string& label, int line_number, const std::string& msg) {
    std::ostringstream oss;
    oss << "CUE parse error in '" << label << "'";
    if (line_number > 0) {
        oss << " at line " << line_number;
    }
    oss << ": " << msg;
    throw std::runtime_error(oss.str());
}

} // namespace

ParsedCueSheet parse_cue_stream(
    std::istream& input,
    const std::filesystem::path& base_dir,
    const std::string& source_path_label) {
    DiscMetadata disc_metadata{};
    std::vector<RawCueFileGroup> file_groups;
    std::vector<RawCueTrack> raw_tracks;
    std::optional<std::size_t> active_file_group;

    std::string raw_line;
    int line_number = 0;

    while (std::getline(input, raw_line)) {
        ++line_number;

        // Strip UTF-8 BOM on first line if present
        if (line_number == 1 && raw_line.size() >= 3 &&
            static_cast<unsigned char>(raw_line[0]) == 0xEF &&
            static_cast<unsigned char>(raw_line[1]) == 0xBB &&
            static_cast<unsigned char>(raw_line[2]) == 0xBF) {
            raw_line.erase(0, 3);
        }

        // Strip trailing CR
        if (!raw_line.empty() && raw_line.back() == '\r') {
            raw_line.pop_back();
        }

        const std::string trimmed = trim_whitespace(raw_line);
        if (trimmed.empty()) {
            continue;
        }

        // Handle REM lines before general tokenization so arbitrary comments with quotes don't fail
        const std::string upper_Check = to_upper_ascii(trimmed);
        if (upper_Check == "REM" ||
            (upper_Check.rfind("REM", 0) == 0 &&
             std::isspace(static_cast<unsigned char>(trimmed[3])))) {
            const std::string rem_body = trim_whitespace(std::string_view(trimmed).substr(3));
            std::vector<Token> rem_tokens;
            std::string tok_err;
            if (tokenize_cue_line(rem_body, rem_tokens, tok_err) && rem_tokens.size() >= 2) {
                const std::string rem_key = to_upper_ascii(rem_tokens[0].value);
                if (rem_key == "GENRE" && raw_tracks.empty()) {
                    if (rem_tokens.size() == 2) {
                        disc_metadata.genre = rem_tokens[1].value;
                    } else {
                        disc_metadata.genre =
                            trim_whitespace(std::string_view(rem_body).substr(rem_tokens[0].value.size()));
                    }
                } else if ((rem_key == "DATE" || rem_key == "YEAR") && raw_tracks.empty()) {
                    if (const auto year = parse_strict_int(rem_tokens[1].value);
                        year.has_value() && *year >= 1000 && *year <= 9999) {
                        disc_metadata.releaseYear = *year;
                    }
                }
            }
            continue;
        }

        std::vector<Token> tokens;
        std::string tok_err;
        if (!tokenize_cue_line(trimmed, tokens, tok_err)) {
            throw_cue_error(source_path_label, line_number, tok_err);
        }
        if (tokens.empty()) {
            continue;
        }

        const std::string keyword = to_upper_ascii(tokens[0].value);

        if (keyword == "CATALOG") {
            if (tokens.size() != 2) {
                throw_cue_error(source_path_label, line_number, "CATALOG requires exactly 1 argument");
            }
            if (!raw_tracks.empty()) {
                throw_cue_error(source_path_label, line_number, "CATALOG must appear before any TRACK");
            }
            if (!is_valid_catalog(tokens[1].value)) {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "Invalid CATALOG '" + tokens[1].value + "' (expected 13 decimal digits)");
            }
            disc_metadata.catalog = tokens[1].value;
        } else if (keyword == "CDTEXTFILE") {
            if (tokens.size() != 2) {
                throw_cue_error(source_path_label, line_number, "CDTEXTFILE requires a filename");
            }
        } else if (keyword == "TITLE") {
            if (tokens.size() != 2) {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "TITLE requires a single (quoted) string argument");
            }
            if (raw_tracks.empty()) {
                disc_metadata.title = tokens[1].value;
            } else {
                raw_tracks.back().title = tokens[1].value;
            }
        } else if (keyword == "PERFORMER") {
            if (tokens.size() != 2) {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "PERFORMER requires a single (quoted) string argument");
            }
            if (raw_tracks.empty()) {
                disc_metadata.performer = tokens[1].value;
            } else {
                raw_tracks.back().performer = tokens[1].value;
            }
        } else if (keyword == "SONGWRITER") {
            if (tokens.size() != 2) {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "SONGWRITER requires a single (quoted) string argument");
            }
            if (raw_tracks.empty()) {
                disc_metadata.songwriter = tokens[1].value;
            } else {
                raw_tracks.back().songwriter = tokens[1].value;
            }
        } else if (keyword == "FILE") {
            if (tokens.size() != 3) {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "FILE directive requires <filename> and <filetype> (e.g. FILE \"album.bin\" BINARY)");
            }
            if (!raw_tracks.empty() && raw_tracks.back().indices.empty()) {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "Previous TRACK is missing an INDEX 01 before new FILE directive");
            }

            const std::string& filename = tokens[1].value;
            const std::string ftype_str = to_upper_ascii(tokens[2].value);

            if (filename.empty()) {
                throw_cue_error(source_path_label, line_number, "FILE filename cannot be empty");
            }

            CueFileType ftype = CueFileType::Binary;
            if (ftype_str == "BINARY") {
                ftype = CueFileType::Binary;
            } else if (ftype_str == "MOTOROLA") {
                ftype = CueFileType::Motorola;
            } else {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "Unsupported FILE type '" + tokens[2].value +
                        "' (only BINARY and MOTOROLA raw image files are supported)");
            }

            const std::filesystem::path resolved = resolve_data_file_path(base_dir, filename);
            const bool swap_bytes =
                (ftype == CueFileType::Motorola) ||
                (ftype == CueFileType::Binary && is_cdrdao_big_endian_bin(base_dir, resolved));

            file_groups.push_back(RawCueFileGroup{
                .rawFilename = filename,
                .resolvedPath = resolved,
                .fileType = ftype,
                .swapAudioBytes = swap_bytes,
                .lineNumber = line_number,
            });
            active_file_group = file_groups.size() - 1;
        } else if (keyword == "TRACK") {
            if (tokens.size() != 3) {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "TRACK directive requires <number> and <mode> (e.g. TRACK 01 AUDIO)");
            }
            if (!active_file_group.has_value()) {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "TRACK directive must appear after a FILE directive");
            }
            if (!raw_tracks.empty()) {
                bool prev_has_index01 = false;
                for (const auto& idx : raw_tracks.back().indices) {
                    if (idx.number == 1) {
                        prev_has_index01 = true;
                        break;
                    }
                }
                if (!prev_has_index01) {
                    throw_cue_error(
                        source_path_label,
                        raw_tracks.back().lineNumber,
                        "TRACK " + std::to_string(raw_tracks.back().number) +
                            " is missing required INDEX 01");
                }
            }

            const auto trk_num = parse_strict_int(tokens[1].value);
            if (!trk_num.has_value() || *trk_num < 1 || *trk_num > 99) {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "Invalid TRACK number '" + tokens[1].value + "' (expected 01..99)");
            }
            if (!raw_tracks.empty() && *trk_num != raw_tracks.back().number + 1) {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "Non-sequential TRACK number " + std::to_string(*trk_num) +
                        " after TRACK " + std::to_string(raw_tracks.back().number));
            }

            const std::string mode_str = to_upper_ascii(tokens[2].value);
            TrackType ttype = TrackType::Unknown;
            std::uint32_t sector_size = 2352;

            if (mode_str == "AUDIO") {
                ttype = TrackType::Audio;
                sector_size = 2352;
            } else if (mode_str == "MODE1/2048" || mode_str == "MODE2/2048") {
                ttype = TrackType::Data;
                sector_size = 2048;
            } else if (mode_str == "MODE1/2352" || mode_str == "MODE2/2352" || mode_str == "CDI/2352") {
                ttype = TrackType::Data;
                sector_size = 2352;
            } else if (mode_str == "MODE2/2336" || mode_str == "CDI/2336") {
                ttype = TrackType::Data;
                sector_size = 2336;
            } else if (mode_str == "MODE2/2324") {
                ttype = TrackType::Data;
                sector_size = 2324;
            } else {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "Unsupported TRACK mode '" + tokens[2].value + "'");
            }

            raw_tracks.push_back(RawCueTrack{
                .number = *trk_num,
                .lineNumber = line_number,
                .type = ttype,
                .modeString = mode_str,
                .sectorByteSize = sector_size,
                .fileGroupIndex = *active_file_group,
            });
        } else if (keyword == "ISRC") {
            if (raw_tracks.empty()) {
                throw_cue_error(source_path_label, line_number, "ISRC is only valid inside a TRACK");
            }
            if (tokens.size() != 2 || !is_valid_isrc(tokens[1].value)) {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "Invalid ISRC (expected 12 alphanumeric characters)");
            }
            raw_tracks.back().isrc = tokens[1].value;
        } else if (keyword == "FLAGS") {
            if (raw_tracks.empty()) {
                throw_cue_error(source_path_label, line_number, "FLAGS is only valid inside a TRACK");
            }
            if (tokens.size() < 2) {
                throw_cue_error(source_path_label, line_number, "FLAGS requires at least one flag");
            }
            for (std::size_t i = 1; i < tokens.size(); ++i) {
                const std::string flg = to_upper_ascii(tokens[i].value);
                if (flg != "DCP" && flg != "4CH" && flg != "PRE" && flg != "SCMS") {
                    throw_cue_error(
                        source_path_label,
                        line_number,
                        "Unsupported track flag '" + tokens[i].value + "'");
                }
            }
        } else if (keyword == "PREGAP") {
            if (raw_tracks.empty()) {
                throw_cue_error(source_path_label, line_number, "PREGAP is only valid inside a TRACK");
            }
            if (tokens.size() != 2) {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "PREGAP requires a timestamp argument MM:SS:FF");
            }
            auto& cur_trk = raw_tracks.back();
            if (!cur_trk.indices.empty()) {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "PREGAP must appear before any INDEX directive in a TRACK");
            }
            if (cur_trk.pregapLength.has_value()) {
                throw_cue_error(source_path_label, line_number, "Duplicate PREGAP in TRACK");
            }
            const auto sectors = parse_msf_to_sectors(tokens[1].value);
            if (!sectors.has_value() || *sectors <= 0) {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "Invalid PREGAP timestamp '" + tokens[1].value + "'");
            }
            cur_trk.pregapLength = *sectors;
        } else if (keyword == "POSTGAP") {
            if (raw_tracks.empty()) {
                throw_cue_error(source_path_label, line_number, "POSTGAP is only valid inside a TRACK");
            }
            if (tokens.size() != 2) {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "POSTGAP requires a timestamp argument MM:SS:FF");
            }
            auto& cur_trk = raw_tracks.back();
            if (cur_trk.indices.empty()) {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "POSTGAP must appear after INDEX directives in a TRACK");
            }
            if (cur_trk.postgapLength.has_value()) {
                throw_cue_error(source_path_label, line_number, "Duplicate POSTGAP in TRACK");
            }
            const auto sectors = parse_msf_to_sectors(tokens[1].value);
            if (!sectors.has_value() || *sectors <= 0) {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "Invalid POSTGAP timestamp '" + tokens[1].value + "'");
            }
            cur_trk.postgapLength = *sectors;
        } else if (keyword == "INDEX") {
            if (raw_tracks.empty()) {
                throw_cue_error(source_path_label, line_number, "INDEX is only valid inside a TRACK");
            }
            if (tokens.size() != 3) {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "INDEX directive requires <number> and <MM:SS:FF>");
            }
            auto& cur_trk = raw_tracks.back();
            if (cur_trk.postgapLength.has_value()) {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "INDEX cannot appear after POSTGAP in a TRACK");
            }

            const auto idx_num = parse_strict_int(tokens[1].value);
            if (!idx_num.has_value() || *idx_num < 0 || *idx_num > 99) {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "Invalid INDEX number '" + tokens[1].value + "' (expected 00..99)");
            }
            const auto sectors = parse_msf_to_sectors(tokens[2].value);
            if (!sectors.has_value()) {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "Invalid INDEX timestamp '" + tokens[2].value + "' (expected MM:SS:FF)");
            }

            if (*idx_num == 0 && cur_trk.pregapLength.has_value()) {
                throw_cue_error(
                    source_path_label,
                    line_number,
                    "TRACK cannot specify both PREGAP and INDEX 00");
            }

            if (cur_trk.indices.empty()) {
                if (*idx_num != 0 && *idx_num != 1) {
                    throw_cue_error(
                        source_path_label,
                        line_number,
                        "First INDEX of a TRACK must be 00 or 01");
                }
            } else {
                const auto& prev_idx = cur_trk.indices.back();
                if (*idx_num != prev_idx.number + 1) {
                    throw_cue_error(
                        source_path_label,
                        line_number,
                        "Non-sequential INDEX " + std::to_string(*idx_num) +
                            " after INDEX " + std::to_string(prev_idx.number));
                }
                if (*sectors < prev_idx.fileSector) {
                    throw_cue_error(
                        source_path_label,
                        line_number,
                        "INDEX timestamp " + tokens[2].value + " precedes previous INDEX in TRACK");
                }
            }

            cur_trk.indices.push_back(RawCueIndex{
                .number = *idx_num,
                .fileSector = *sectors,
                .lineNumber = line_number,
            });
        } else {
            throw_cue_error(
                source_path_label,
                line_number,
                "Unsupported or unrecognized CUE directive '" + tokens[0].value + "'");
        }
    }

    if (raw_tracks.empty()) {
        throw_cue_error(source_path_label, 0, "CUE sheet contains no TRACK definitions");
    }

    for (const auto& trk : raw_tracks) {
        bool has_idx01 = false;
        for (const auto& idx : trk.indices) {
            if (idx.number == 1) {
                has_idx01 = true;
                break;
            }
        }
        if (!has_idx01) {
            throw_cue_error(
                source_path_label,
                trk.lineNumber,
                "TRACK " + std::to_string(trk.number) + " is missing required INDEX 01");
        }
    }

    // Validate referenced image files and compute track storage & LBA layout
    ParsedCueSheet result{};
    result.layout.sourceKind = "CUE Sheet Image";
    result.layout.sourcePath = source_path_label;
    result.layout.discMetadata = disc_metadata;

    std::int64_t current_lba = 0;

    std::size_t trk_idx = 0;
    while (trk_idx < raw_tracks.size()) {
        const std::size_t fg_idx = raw_tracks[trk_idx].fileGroupIndex;
        const auto& fg = file_groups[fg_idx];

        std::size_t group_end = trk_idx + 1;
        while (group_end < raw_tracks.size() && raw_tracks[group_end].fileGroupIndex == fg_idx) {
            ++group_end;
        }

        std::error_code ec;
        if (!std::filesystem::exists(fg.resolvedPath, ec) ||
            !std::filesystem::is_regular_file(fg.resolvedPath, ec)) {
            throw_cue_error(
                source_path_label,
                fg.lineNumber,
                "Referenced binary image file not found: '" + fg.resolvedPath.string() + "'");
        }

        const std::uintmax_t file_size_u = std::filesystem::file_size(fg.resolvedPath, ec);
        if (ec || file_size_u == 0) {
            throw_cue_error(
                source_path_label,
                fg.lineNumber,
                "Referenced binary image file is empty or unreadable: '" +
                    fg.resolvedPath.string() + "'");
        }
        const std::uint64_t file_size = static_cast<std::uint64_t>(file_size_u);

        // Determine each track's starting file sector within this FILE group
        std::vector<std::int64_t> first_file_sectors(group_end - trk_idx, 0);
        for (std::size_t k = trk_idx; k < group_end; ++k) {
            const auto& rtrk = raw_tracks[k];
            const std::size_t rel = k - trk_idx;

            if (rel == 0) {
                // First track in a FILE starts at file sector 0 if its first index is 0,
                // or if it is Track 1 with an implicit pregap before INDEX 01.
                if (rtrk.indices.front().number == 0) {
                    if (rtrk.indices.front().fileSector != 0) {
                        throw_cue_error(
                            source_path_label,
                            rtrk.indices.front().lineNumber,
                            "First track in FILE must start at 00:00:00");
                    }
                    first_file_sectors[rel] = 0;
                } else {
                    // First index is INDEX 01
                    if (rtrk.pregapLength.has_value() && rtrk.indices.front().fileSector != 0) {
                        throw_cue_error(
                            source_path_label,
                            rtrk.indices.front().lineNumber,
                            "First track in FILE with PREGAP must have INDEX 01 at 00:00:00");
                    }
                    // Even if INDEX 01 > 00:00:00 without INDEX 00, sectors [0, index01) in
                    // the file belong to this first track as its file-backed pregap.
                    first_file_sectors[rel] = 0;
                }
            } else {
                first_file_sectors[rel] = rtrk.indices.front().fileSector;
                const std::int64_t prev_last_idx_sector = raw_tracks[k - 1].indices.back().fileSector;
                if (first_file_sectors[rel] <= first_file_sectors[rel - 1] ||
                    first_file_sectors[rel] <= prev_last_idx_sector) {
                    throw_cue_error(
                        source_path_label,
                        rtrk.indices.front().lineNumber,
                        "TRACK " + std::to_string(rtrk.number) +
                            " start timestamp does not advance past previous track");
                }
            }
        }

        std::uint64_t file_byte_cursor = 0;

        for (std::size_t k = trk_idx; k < group_end; ++k) {
            const auto& rtrk = raw_tracks[k];
            const std::size_t rel = k - trk_idx;
            const std::int64_t trk_first_file_sector = first_file_sectors[rel];

            std::int64_t file_backed_sectors = 0;
            if (k + 1 < group_end) {
                file_backed_sectors = first_file_sectors[rel + 1] - trk_first_file_sector;
                const std::uint64_t needed_bytes =
                    static_cast<std::uint64_t>(file_backed_sectors) * rtrk.sectorByteSize;
                if (file_byte_cursor + needed_bytes > file_size) {
                    throw_cue_error(
                        source_path_label,
                        rtrk.lineNumber,
                        "TRACK " + std::to_string(rtrk.number) +
                            " extends past the end of image file '" + fg.resolvedPath.string() + "'");
                }
            } else {
                if (file_byte_cursor >= file_size) {
                    throw_cue_error(
                        source_path_label,
                        rtrk.lineNumber,
                        "No data remaining in image file '" + fg.resolvedPath.string() +
                            "' for TRACK " + std::to_string(rtrk.number));
                }
                const std::uint64_t rem_bytes = file_size - file_byte_cursor;
                if (rem_bytes % rtrk.sectorByteSize != 0) {
                    throw_cue_error(
                        source_path_label,
                        rtrk.lineNumber,
                        "Image file '" + fg.resolvedPath.string() +
                            "' byte size is not aligned to sector size " +
                            std::to_string(rtrk.sectorByteSize));
                }
                file_backed_sectors = static_cast<std::int64_t>(rem_bytes / rtrk.sectorByteSize);
                const std::int64_t max_idx_rel =
                    rtrk.indices.back().fileSector - trk_first_file_sector;
                if (file_backed_sectors <= max_idx_rel) {
                    throw_cue_error(
                        source_path_label,
                        rtrk.indices.back().lineNumber,
                        "INDEX timestamp in TRACK " + std::to_string(rtrk.number) +
                            " exceeds image file length");
                }
            }

            const std::int64_t syn_pregap = rtrk.pregapLength.value_or(0);
            const std::int64_t syn_postgap = rtrk.postgapLength.value_or(0);

            const std::int64_t track_start_lba = current_lba;
            const std::int64_t file_backed_start_lba = track_start_lba + syn_pregap;
            const std::int64_t file_backed_end_lba = file_backed_start_lba + file_backed_sectors;
            const std::int64_t track_end_lba = file_backed_end_lba + syn_postgap;

            TrackInfo tinfo{};
            tinfo.number = rtrk.number;
            tinfo.type = rtrk.type;
            tinfo.startLba = track_start_lba;
            tinfo.endLba = track_end_lba;
            tinfo.postgapLength = syn_postgap;

            // Find INDEX 01 file sector
            std::int64_t idx01_file_sector = trk_first_file_sector;
            for (const auto& ridx : rtrk.indices) {
                if (ridx.number == 1) {
                    idx01_file_sector = ridx.fileSector;
                    break;
                }
            }

            tinfo.index01Lba =
                file_backed_start_lba + (idx01_file_sector - trk_first_file_sector);

            const std::int64_t total_pregap_len = tinfo.index01Lba - tinfo.startLba;
            tinfo.pregapLength = total_pregap_len;
            if (total_pregap_len > 0) {
                tinfo.pregapStartLba = tinfo.startLba;
                tinfo.pregapInFile = (syn_pregap == 0);
            }

            // Populate IndexInfo entries
            if (syn_pregap > 0) {
                tinfo.indexes.push_back(IndexInfo{
                    .trackNumber = rtrk.number,
                    .number = 0,
                    .lba = track_start_lba,
                    .explicitFromSource = true,
                });
            } else if (total_pregap_len > 0 && rtrk.indices.front().number != 0) {
                // First track with INDEX 01 > 00:00:00 has implicit INDEX 00 at 00:00:00
                tinfo.indexes.push_back(IndexInfo{
                    .trackNumber = rtrk.number,
                    .number = 0,
                    .lba = track_start_lba,
                    .explicitFromSource = false,
                });
            }

            for (const auto& ridx : rtrk.indices) {
                const std::int64_t abs_idx_lba =
                    file_backed_start_lba + (ridx.fileSector - trk_first_file_sector);
                tinfo.indexes.push_back(IndexInfo{
                    .trackNumber = rtrk.number,
                    .number = ridx.number,
                    .lba = abs_idx_lba,
                    .explicitFromSource = true,
                });
            }

            CueTrackStorage storage{};
            storage.trackNumber = rtrk.number;
            storage.trackType = rtrk.type;
            storage.modeString = rtrk.modeString;
            storage.filePath = fg.resolvedPath;
            storage.fileType = fg.fileType;
            storage.swapAudioBytes = fg.swapAudioBytes;
            storage.sectorByteSize = rtrk.sectorByteSize;
            storage.syntheticPregapStartLba = track_start_lba;
            storage.syntheticPregapLength = syn_pregap;
            storage.fileBackedStartLba = file_backed_start_lba;
            storage.fileBackedLength = file_backed_sectors;
            storage.fileByteOffset = file_byte_cursor;
            storage.syntheticPostgapStartLba = file_backed_end_lba;
            storage.syntheticPostgapLength = syn_postgap;

            TrackMetadata tmeta{};
            tmeta.trackNumber = rtrk.number;
            tmeta.title = rtrk.title;
            tmeta.performer = rtrk.performer;
            tmeta.songwriter = rtrk.songwriter;
            tmeta.isrc = rtrk.isrc;

            result.layout.tracks.push_back(std::move(tinfo));
            result.layout.trackMetadata.push_back(std::move(tmeta));
            result.trackStorages.push_back(std::move(storage));

            file_byte_cursor +=
                static_cast<std::uint64_t>(file_backed_sectors) * rtrk.sectorByteSize;
            current_lba = track_end_lba;
        }

        trk_idx = group_end;
    }

    result.layout.discInfo.firstTrack = result.layout.tracks.front().number;
    result.layout.discInfo.lastTrack = result.layout.tracks.back().number;
    result.layout.discInfo.startLba = result.layout.tracks.front().startLba;
    result.layout.discInfo.endLba = result.layout.tracks.back().endLba;
    result.layout.discInfo.sessions = 1;
    result.layout.discInfo.rawStartSector = result.layout.discInfo.startLba;
    result.layout.discInfo.rawEndSector = result.layout.discInfo.endLba;

    return result;
}

ParsedCueSheet parse_cue_file(const std::filesystem::path& cue_path) {
    std::error_code ec;
    if (!std::filesystem::exists(cue_path, ec) ||
        !std::filesystem::is_regular_file(cue_path, ec)) {
        throw std::runtime_error("Cannot open CUE file: '" + cue_path.string() + "'");
    }

    std::ifstream in(cue_path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Failed to open CUE file: '" + cue_path.string() + "'");
    }

    const std::filesystem::path base_dir =
        cue_path.has_parent_path() ? cue_path.parent_path() : std::filesystem::current_path();
    return parse_cue_stream(in, base_dir, cue_path.string());
}

} // namespace mycds
