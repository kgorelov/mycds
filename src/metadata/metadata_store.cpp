#include "metadata/metadata_store.hpp"

#include "metadata/cddb_disc_id.hpp"
#include "source/cue_parser.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace mycds {

namespace {

std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const unsigned char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    char buf[8]{};
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type{Type::Null};
    bool boolVal{false};
    std::int64_t intVal{0};
    std::string strVal{};
    std::vector<JsonValue> arrVal{};
    std::map<std::string, JsonValue> objVal{};

    const JsonValue* getField(const std::string& key) const {
        if (type != Type::Object) {
            return nullptr;
        }
        const auto it = objVal.find(key);
        return it != objVal.end() ? &it->second : nullptr;
    }

    std::string getString(const std::string& key, const std::string& def = "") const {
        const JsonValue* v = getField(key);
        if (v && v->type == Type::String) {
            return v->strVal;
        }
        return def;
    }

    bool getBool(const std::string& key, bool def = false) const {
        const JsonValue* v = getField(key);
        if (v && v->type == Type::Bool) {
            return v->boolVal;
        }
        return def;
    }

    std::optional<std::int64_t> getOptionalInt(const std::string& key) const {
        const JsonValue* v = getField(key);
        if (v && v->type == Type::Number) {
            return v->intVal;
        }
        return std::nullopt;
    }
};

class SimpleJsonParser {
public:
    explicit SimpleJsonParser(std::string_view input) : input_(input) {}

    JsonValue parse() {
        skipWhitespace();
        JsonValue val = parseValue();
        skipWhitespace();
        if (pos_ != input_.size()) {
            throw std::runtime_error("Unexpected trailing characters in sidecar JSON");
        }
        return val;
    }

private:
    void skipWhitespace() {
        while (pos_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
        }
    }

    char peek() const {
        if (pos_ >= input_.size()) {
            throw std::runtime_error("Unexpected end of sidecar JSON");
        }
        return input_[pos_];
    }

    char get() {
        const char c = peek();
        ++pos_;
        return c;
    }

    JsonValue parseValue() {
        skipWhitespace();
        const char c = peek();
        if (c == '{') {
            return parseObject();
        }
        if (c == '[') {
            return parseArray();
        }
        if (c == '"') {
            JsonValue v;
            v.type = JsonValue::Type::String;
            v.strVal = parseString();
            return v;
        }
        if (c == 't' || c == 'f') {
            return parseBool();
        }
        if (c == 'n') {
            return parseNull();
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            return parseNumber();
        }
        throw std::runtime_error(
            std::string("Unexpected character '") + c + "' in sidecar JSON");
    }

    std::string parseString() {
        if (get() != '"') {
            throw std::runtime_error("Expected '\"' in sidecar JSON");
        }
        std::string out;
        while (pos_ < input_.size()) {
            const char c = get();
            if (c == '"') {
                return out;
            }
            if (c == '\\') {
                const char esc = get();
                switch (esc) {
                    case '"':
                        out.push_back('"');
                        break;
                    case '\\':
                        out.push_back('\\');
                        break;
                    case '/':
                        out.push_back('/');
                        break;
                    case 'b':
                        out.push_back('\b');
                        break;
                    case 'f':
                        out.push_back('\f');
                        break;
                    case 'n':
                        out.push_back('\n');
                        break;
                    case 'r':
                        out.push_back('\r');
                        break;
                    case 't':
                        out.push_back('\t');
                        break;
                    case 'u': {
                        if (pos_ + 4 > input_.size()) {
                            throw std::runtime_error("Invalid \\u escape in sidecar JSON");
                        }
                        const std::string hex(input_.substr(pos_, 4));
                        pos_ += 4;
                        const unsigned int cp = std::stoul(hex, nullptr, 16);
                        out.push_back(static_cast<char>(cp & 0xFFU));
                        break;
                    }
                    default:
                        throw std::runtime_error("Invalid escape sequence in sidecar JSON");
                }
            } else {
                out.push_back(c);
            }
        }
        throw std::runtime_error("Unterminated string literal in sidecar JSON");
    }

    JsonValue parseObject() {
        get(); // '{'
        JsonValue v;
        v.type = JsonValue::Type::Object;
        skipWhitespace();
        if (peek() == '}') {
            get();
            return v;
        }
        while (true) {
            skipWhitespace();
            const std::string key = parseString();
            skipWhitespace();
            if (get() != ':') {
                throw std::runtime_error("Expected ':' after object key in sidecar JSON");
            }
            v.objVal[key] = parseValue();
            skipWhitespace();
            const char sep = get();
            if (sep == '}') {
                break;
            }
            if (sep != ',') {
                throw std::runtime_error("Expected ',' or '}' in sidecar JSON object");
            }
        }
        return v;
    }

    JsonValue parseArray() {
        get(); // '['
        JsonValue v;
        v.type = JsonValue::Type::Array;
        skipWhitespace();
        if (peek() == ']') {
            get();
            return v;
        }
        while (true) {
            v.arrVal.push_back(parseValue());
            skipWhitespace();
            const char sep = get();
            if (sep == ']') {
                break;
            }
            if (sep != ',') {
                throw std::runtime_error("Expected ',' or ']' in sidecar JSON array");
            }
        }
        return v;
    }

    JsonValue parseBool() {
        if (input_.substr(pos_, 4) == "true") {
            pos_ += 4;
            JsonValue v;
            v.type = JsonValue::Type::Bool;
            v.boolVal = true;
            return v;
        }
        if (input_.substr(pos_, 5) == "false") {
            pos_ += 5;
            JsonValue v;
            v.type = JsonValue::Type::Bool;
            v.boolVal = false;
            return v;
        }
        throw std::runtime_error("Invalid boolean literal in sidecar JSON");
    }

    JsonValue parseNull() {
        if (input_.substr(pos_, 4) == "null") {
            pos_ += 4;
            return JsonValue{};
        }
        throw std::runtime_error("Invalid null literal in sidecar JSON");
    }

    JsonValue parseNumber() {
        const std::size_t start = pos_;
        if (input_[pos_] == '-') {
            ++pos_;
        }
        if (pos_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
            throw std::runtime_error("Invalid numeric literal in sidecar JSON");
        }
        while (pos_ < input_.size() &&
               std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
        }
        JsonValue v;
        v.type = JsonValue::Type::Number;
        v.intVal = std::stoll(std::string(input_.substr(start, pos_ - start)));
        return v;
    }

    std::string_view input_;
    std::size_t pos_{0};
};

std::string sanitize_cue_quoted(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        if (c == '"') {
            out.push_back('\'');
        } else if (c == '\r' || c == '\n') {
            out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string trim_ws(std::string_view sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
        sv.remove_suffix(1);
    }
    return std::string(sv);
}

std::string to_upper_ascii(std::string_view sv) {
    std::string out(sv);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return out;
}

bool is_cue_metadata_directive(std::string_view trimmed_line, bool in_track) {
    std::istringstream iss{std::string(trimmed_line)};
    std::string kw;
    if (!(iss >> kw)) {
        return false;
    }
    const std::string upper_kw = to_upper_ascii(kw);
    if (upper_kw == "TITLE" || upper_kw == "PERFORMER" || upper_kw == "SONGWRITER") {
        return true;
    }
    if (!in_track && upper_kw == "CATALOG") {
        return true;
    }
    if (in_track && upper_kw == "ISRC") {
        return true;
    }
    if (!in_track && upper_kw == "REM") {
        std::string sub_kw;
        if (iss >> sub_kw) {
            const std::string upper_sub = to_upper_ascii(sub_kw);
            if (upper_sub == "GENRE" || upper_sub == "DATE" || upper_sub == "DISCID") {
                return true;
            }
        }
    }
    return false;
}

void write_disc_metadata_directives(std::ostringstream& out, const DiscMetadata& meta) {
    if (!meta.genre.empty()) {
        out << "REM GENRE \"" << sanitize_cue_quoted(meta.genre) << "\"\n";
    }
    if (meta.releaseYear.has_value()) {
        out << "REM DATE " << *meta.releaseYear << "\n";
    }
    if (!meta.databaseDiscId.empty()) {
        out << "REM DISCID " << sanitize_cue_quoted(meta.databaseDiscId) << "\n";
    }
    if (!meta.catalog.empty()) {
        out << "CATALOG " << sanitize_cue_quoted(meta.catalog) << "\n";
    }
    if (!meta.performer.empty()) {
        out << "PERFORMER \"" << sanitize_cue_quoted(meta.performer) << "\"\n";
    }
    if (!meta.title.empty()) {
        out << "TITLE \"" << sanitize_cue_quoted(meta.title) << "\"\n";
    }
    if (!meta.songwriter.empty()) {
        out << "SONGWRITER \"" << sanitize_cue_quoted(meta.songwriter) << "\"\n";
    }
}

void write_track_metadata_directives(std::ostringstream& out, const TrackMetadata& tm) {
    if (!tm.title.empty()) {
        out << "    TITLE \"" << sanitize_cue_quoted(tm.title) << "\"\n";
    }
    if (!tm.performer.empty()) {
        out << "    PERFORMER \"" << sanitize_cue_quoted(tm.performer) << "\"\n";
    }
    if (!tm.songwriter.empty()) {
        out << "    SONGWRITER \"" << sanitize_cue_quoted(tm.songwriter) << "\"\n";
    }
    if (!tm.isrc.empty()) {
        out << "    ISRC " << sanitize_cue_quoted(tm.isrc) << "\n";
    }
}

} // namespace

std::filesystem::path MetadataStore::defaultSidecarPath(const DiscLayout& layout) {
    namespace fs = std::filesystem;
    if (!layout.sourcePath.empty() && layout.sourcePath.rfind("/dev/", 0) != 0) {
        const fs::path src(layout.sourcePath);
        return src.parent_path() / (src.stem().string() + ".mycds.json");
    }

    std::string disc_id = layout.discMetadata.databaseDiscId;
    if (disc_id.empty() && !layout.tracks.empty()) {
        try {
            disc_id = calculate_cddb_disc_id(layout.discInfo, layout.tracks).discIdHex;
        } catch (...) {
            disc_id = "unknown";
        }
    }
    return fs::current_path() / ("disc_" + disc_id + ".mycds.json");
}

bool MetadataStore::exists(const std::filesystem::path& sidecar_path) {
    std::error_code ec;
    return std::filesystem::exists(sidecar_path, ec) &&
           std::filesystem::is_regular_file(sidecar_path, ec);
}

std::string MetadataStore::serializeToJson(const MetadataRecord& record) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"schemaVersion\": 1,\n";
    out << "  \"disc\": {\n";
    out << "    \"title\": \"" << json_escape(record.disc.title) << "\",\n";
    out << "    \"performer\": \"" << json_escape(record.disc.performer) << "\",\n";
    out << "    \"songwriter\": \"" << json_escape(record.disc.songwriter) << "\",\n";
    out << "    \"catalog\": \"" << json_escape(record.disc.catalog) << "\",\n";
    out << "    \"genre\": \"" << json_escape(record.disc.genre) << "\",\n";
    if (record.disc.releaseYear.has_value()) {
        out << "    \"releaseYear\": " << *record.disc.releaseYear << ",\n";
    } else {
        out << "    \"releaseYear\": null,\n";
    }
    out << "    \"databaseProvider\": \"" << json_escape(record.disc.databaseProvider) << "\",\n";
    out << "    \"databaseDiscId\": \"" << json_escape(record.disc.databaseDiscId) << "\",\n";
    out << "    \"externalRecordId\": \"" << json_escape(record.disc.externalRecordId) << "\",\n";
    out << "    \"lookupTimestamp\": \"" << json_escape(record.disc.lookupTimestamp) << "\",\n";
    out << "    \"userCorrected\": " << (record.disc.userCorrected ? "true" : "false") << ",\n";
    out << "    \"userNotes\": \"" << json_escape(record.disc.userNotes) << "\",\n";
    out << "    \"comment\": \"" << json_escape(record.disc.comment) << "\"\n";
    out << "  },\n";

    out << "  \"remoteLayout\": {\n";
    if (record.remoteDiscLengthSeconds.has_value()) {
        out << "    \"discLengthSeconds\": " << *record.remoteDiscLengthSeconds << ",\n";
    } else {
        out << "    \"discLengthSeconds\": null,\n";
    }
    out << "    \"trackFrameOffsets\": [";
    for (std::size_t i = 0; i < record.remoteTrackFrameOffsets.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << record.remoteTrackFrameOffsets[i];
    }
    out << "]\n";
    out << "  },\n";

    out << "  \"tracks\": [\n";
    for (std::size_t i = 0; i < record.tracks.size(); ++i) {
        const auto& trk = record.tracks[i];
        out << "    {\n";
        out << "      \"trackNumber\": " << trk.trackNumber << ",\n";
        out << "      \"title\": \"" << json_escape(trk.title) << "\",\n";
        out << "      \"performer\": \"" << json_escape(trk.performer) << "\",\n";
        out << "      \"songwriter\": \"" << json_escape(trk.songwriter) << "\",\n";
        out << "      \"isrc\": \"" << json_escape(trk.isrc) << "\"\n";
        out << "    }" << (i + 1 < record.tracks.size() ? "," : "") << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    return out.str();
}

MetadataRecord MetadataStore::deserializeFromJson(std::string_view json_text) {
    SimpleJsonParser parser(json_text);
    const JsonValue root = parser.parse();
    if (root.type != JsonValue::Type::Object) {
        throw std::runtime_error("Sidecar JSON root must be an object");
    }

    MetadataRecord record{};
    if (const JsonValue* disc_obj = root.getField("disc")) {
        if (disc_obj->type != JsonValue::Type::Object) {
            throw std::runtime_error("Sidecar JSON 'disc' field must be an object");
        }
        record.disc.title = disc_obj->getString("title");
        record.disc.performer = disc_obj->getString("performer");
        record.disc.songwriter = disc_obj->getString("songwriter");
        record.disc.catalog = disc_obj->getString("catalog");
        record.disc.genre = disc_obj->getString("genre");
        if (const auto yr = disc_obj->getOptionalInt("releaseYear")) {
            record.disc.releaseYear = static_cast<int>(*yr);
        }
        record.disc.databaseProvider = disc_obj->getString("databaseProvider");
        record.disc.databaseDiscId = disc_obj->getString("databaseDiscId");
        record.disc.externalRecordId = disc_obj->getString("externalRecordId");
        record.disc.lookupTimestamp = disc_obj->getString("lookupTimestamp");
        record.disc.userCorrected = disc_obj->getBool("userCorrected", false);
        record.disc.userNotes = disc_obj->getString("userNotes");
        record.disc.comment = disc_obj->getString("comment");
    }

    if (const JsonValue* remote_obj = root.getField("remoteLayout")) {
        if (remote_obj->type == JsonValue::Type::Object) {
            record.remoteDiscLengthSeconds = remote_obj->getOptionalInt("discLengthSeconds");
            if (const JsonValue* offsets_arr = remote_obj->getField("trackFrameOffsets")) {
                if (offsets_arr->type == JsonValue::Type::Array) {
                    for (const auto& elem : offsets_arr->arrVal) {
                        if (elem.type == JsonValue::Type::Number) {
                            record.remoteTrackFrameOffsets.push_back(elem.intVal);
                        }
                    }
                }
            }
        }
    }

    if (const JsonValue* tracks_arr = root.getField("tracks")) {
        if (tracks_arr->type != JsonValue::Type::Array) {
            throw std::runtime_error("Sidecar JSON 'tracks' field must be an array");
        }
        for (const auto& item : tracks_arr->arrVal) {
            if (item.type != JsonValue::Type::Object) {
                throw std::runtime_error("Sidecar JSON track entry must be an object");
            }
            TrackMetadata tm{};
            if (const auto num = item.getOptionalInt("trackNumber")) {
                tm.trackNumber = static_cast<int>(*num);
            }
            tm.title = item.getString("title");
            tm.performer = item.getString("performer");
            tm.songwriter = item.getString("songwriter");
            tm.isrc = item.getString("isrc");
            record.tracks.push_back(std::move(tm));
        }
    }

    return record;
}

void MetadataStore::saveSidecar(
    const std::filesystem::path& sidecar_path,
    const MetadataRecord& record) {
    if (sidecar_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(sidecar_path.parent_path(), ec);
    }

    std::ofstream out(sidecar_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error(
            "Failed to open sidecar file for writing: " + sidecar_path.string());
    }
    const std::string json_text = serializeToJson(record);
    out.write(json_text.data(), static_cast<std::streamsize>(json_text.size()));
    if (!out.good()) {
        throw std::runtime_error(
            "Failed to write sidecar metadata file: " + sidecar_path.string());
    }
}

MetadataRecord MetadataStore::loadSidecar(const std::filesystem::path& sidecar_path) {
    std::ifstream in(sidecar_path, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error(
            "Failed to open sidecar metadata file: " + sidecar_path.string());
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return deserializeFromJson(ss.str());
}

std::string MetadataStore::updateCueContentWithMetadata(
    std::string_view original_cue_content,
    const MetadataRecord& record) {
    std::istringstream stream{std::string(original_cue_content)};
    std::ostringstream out;

    bool in_track = false;
    bool disc_meta_written = false;
    int current_track_num = 0;

    auto find_track_meta = [&](int trk_num) -> const TrackMetadata* {
        for (const auto& tm : record.tracks) {
            if (tm.trackNumber == trk_num) {
                return &tm;
            }
        }
        return nullptr;
    };

    std::string raw_line;
    while (std::getline(stream, raw_line)) {
        if (!raw_line.empty() && raw_line.back() == '\r') {
            raw_line.pop_back();
        }
        if (raw_line.size() >= 3 &&
            static_cast<unsigned char>(raw_line[0]) == 0xEF &&
            static_cast<unsigned char>(raw_line[1]) == 0xBB &&
            static_cast<unsigned char>(raw_line[2]) == 0xBF) {
            raw_line.erase(0, 3);
        }

        const std::string trimmed = trim_ws(raw_line);
        if (trimmed.empty()) {
            out << "\n";
            continue;
        }

        std::istringstream iss(trimmed);
        std::string kw;
        iss >> kw;
        const std::string upper_kw = to_upper_ascii(kw);

        if (upper_kw == "FILE" && !in_track && !disc_meta_written) {
            write_disc_metadata_directives(out, record.disc);
            disc_meta_written = true;
            out << raw_line << "\n";
            continue;
        }

        if (upper_kw == "TRACK") {
            if (!in_track && !disc_meta_written) {
                write_disc_metadata_directives(out, record.disc);
                disc_meta_written = true;
            }
            in_track = true;
            std::string trk_num_str;
            if (iss >> trk_num_str) {
                try {
                    current_track_num = std::stoi(trk_num_str);
                } catch (...) {
                    current_track_num = 0;
                }
            }
            out << raw_line << "\n";
            if (const TrackMetadata* tm = find_track_meta(current_track_num)) {
                write_track_metadata_directives(out, *tm);
            }
            continue;
        }

        if (is_cue_metadata_directive(trimmed, in_track)) {
            continue;
        }

        out << raw_line << "\n";
    }

    return out.str();
}

void MetadataStore::exportToCue(
    const std::filesystem::path& cue_path,
    const MetadataRecord& record,
    const std::filesystem::path& template_cue_path) {
    const std::filesystem::path source_cue =
        !template_cue_path.empty() ? template_cue_path : cue_path;

    std::ifstream in(source_cue, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error(
            "Cannot export metadata to CUE: template CUE file not found at " +
            source_cue.string());
    }
    std::ostringstream original_buf;
    original_buf << in.rdbuf();
    in.close();

    const std::string original_content = original_buf.str();
    const std::string updated_content =
        updateCueContentWithMetadata(original_content, record);

    auto extract_layout_directives = [](std::string_view cue_text) {
        std::vector<std::string> directives;
        std::istringstream ss{std::string(cue_text)};
        std::string line;
        bool in_trk = false;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const std::string trimmed = trim_ws(line);
            if (trimmed.empty()) {
                continue;
            }
            std::istringstream iss(trimmed);
            std::string kw;
            iss >> kw;
            if (to_upper_ascii(kw) == "TRACK") {
                in_trk = true;
            }
            if (!is_cue_metadata_directive(trimmed, in_trk)) {
                directives.push_back(trimmed);
            }
        }
        return directives;
    };

    if (extract_layout_directives(original_content) !=
        extract_layout_directives(updated_content)) {
        throw std::runtime_error(
            "Internal error: CUE export altered non-metadata layout directives");
    }

    if (cue_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(cue_path.parent_path(), ec);
    }

    std::ofstream out(cue_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error(
            "Failed to open CUE file for writing: " + cue_path.string());
    }
    out.write(updated_content.data(), static_cast<std::streamsize>(updated_content.size()));
    if (!out.good()) {
        throw std::runtime_error("Failed to write updated CUE file: " + cue_path.string());
    }
}

} // namespace mycds
