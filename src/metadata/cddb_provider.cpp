#include "metadata/cddb_provider.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

#include <curl/curl.h>

namespace mycds {

namespace {

std::string trim_line(std::string_view sv) {
    while (!sv.empty() && (sv.back() == '\r' || sv.back() == '\n')) {
        sv.remove_suffix(1);
    }
    return std::string(sv);
}

std::string trim_whitespace(std::string_view sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
        sv.remove_suffix(1);
    }
    return std::string(sv);
}

std::string url_encode_cddb_param(std::string_view value) {
    std::ostringstream out;
    out << std::hex << std::uppercase << std::setfill('0');
    for (const unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out << static_cast<char>(c);
        } else if (c == ' ') {
            out << '+';
        } else {
            out << '%' << std::setw(2) << static_cast<int>(c);
        }
    }
    return out.str();
}

std::string unescape_xmcd_value(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\\' && i + 1 < raw.size()) {
            const char next = raw[i + 1];
            if (next == 'n') {
                out.push_back('\n');
                ++i;
                continue;
            }
            if (next == 't') {
                out.push_back('\t');
                ++i;
                continue;
            }
            if (next == '\\') {
                out.push_back('\\');
                ++i;
                continue;
            }
        }
        out.push_back(raw[i]);
    }
    return out;
}

std::string current_utc_iso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm utc_tm{};
    gmtime_r(&now_time, &utc_tm);
    std::ostringstream out;
    out << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

bool is_eight_hex_chars(std::string_view s) {
    if (s.size() != 8) {
        return false;
    }
    for (const unsigned char c : s) {
        if (!std::isxdigit(c)) {
            return false;
        }
    }
    return true;
}

MetadataCandidate parse_candidate_entry(std::string_view line, bool exact_match) {
    const std::string trimmed = trim_whitespace(line);
    std::istringstream iss(trimmed);
    std::string category;
    std::string disc_id;
    if (!(iss >> category >> disc_id)) {
        throw std::runtime_error("Malformed CDDB candidate line: '" + trimmed + "'");
    }
    if (!is_eight_hex_chars(disc_id)) {
        throw std::runtime_error("Invalid CDDB candidate disc ID '" + disc_id + "' in line: '" + trimmed + "'");
    }

    std::string rest;
    std::getline(iss, rest);
    rest = trim_whitespace(rest);
    if (rest.empty()) {
        throw std::runtime_error("Missing title in CDDB candidate line: '" + trimmed + "'");
    }

    const auto [performer, title] = split_cddb_artist_title(rest);
    return MetadataCandidate{
        .category = std::move(category),
        .discId = std::move(disc_id),
        .rawTitle = std::move(rest),
        .performer = performer,
        .title = title,
        .exactMatch = exact_match,
    };
}

std::size_t curl_write_callback(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    const std::size_t total = size * nmemb;
    auto* buffer = static_cast<std::string*>(userdata);
    buffer->append(ptr, total);
    return total;
}

} // namespace

std::pair<std::string, std::string> split_cddb_artist_title(std::string_view dtitle) {
    const std::string cleaned = trim_whitespace(dtitle);
    std::size_t sep_pos = cleaned.find(" / ");
    std::size_t sep_len = 3;
    if (sep_pos == std::string::npos) {
        sep_pos = cleaned.find(" /");
        sep_len = 2;
    }
    if (sep_pos == std::string::npos) {
        sep_pos = cleaned.find("/ ");
        sep_len = 2;
    }
    if (sep_pos == std::string::npos) {
        return {"", cleaned};
    }
    const std::string artist = trim_whitespace(std::string_view(cleaned).substr(0, sep_pos));
    const std::string title = trim_whitespace(std::string_view(cleaned).substr(sep_pos + sep_len));
    return {artist, title};
}

std::string build_cddb_cgi_url(
    const CddbClientConfig& config,
    std::string_view cddb_command) {
    if (config.endpointUrl.empty()) {
        throw std::invalid_argument("CDDB endpoint URL must not be empty");
    }
    const std::string hello = config.helloUser + " " + config.helloHost + " " +
                              config.clientName + " " + config.clientVersion;
    const char separator =
        (config.endpointUrl.find('?') == std::string::npos) ? '?' : '&';

    std::ostringstream url;
    url << config.endpointUrl << separator
        << "cmd=" << url_encode_cddb_param(cddb_command)
        << "&hello=" << url_encode_cddb_param(hello)
        << "&proto=" << config.protocolLevel;
    return url.str();
}

std::string CurlCddbHttpTransport::performGet(
    const std::string& url,
    long connect_timeout_ms,
    long request_timeout_ms) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize libcurl session");
    }

    std::string response_body;
    char error_buffer[CURL_ERROR_SIZE]{};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, request_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "MyCDs/0.1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);

    const CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        const std::string detail =
            (error_buffer[0] != '\0') ? error_buffer : curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        throw std::runtime_error("CDDB network request failed: " + detail);
    }

    long http_status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    curl_easy_cleanup(curl);

    if (http_status == 429) {
        throw std::runtime_error("CDDB service rate limit exceeded (HTTP 429)");
    }
    if (http_status < 200 || http_status >= 300) {
        throw std::runtime_error(
            "CDDB HTTP request failed with status code " + std::to_string(http_status));
    }

    return response_body;
}

std::vector<MetadataCandidate> parse_cddb_query_response(std::string_view response_body) {
    std::istringstream stream{std::string(response_body)};
    std::string raw_status_line;
    if (!std::getline(stream, raw_status_line)) {
        throw std::runtime_error("Empty response from CDDB service");
    }

    const std::string status_line = trim_whitespace(raw_status_line);
    if (status_line.size() < 3 ||
        !std::isdigit(static_cast<unsigned char>(status_line[0])) ||
        !std::isdigit(static_cast<unsigned char>(status_line[1])) ||
        !std::isdigit(static_cast<unsigned char>(status_line[2]))) {
        throw std::runtime_error("Malformed CDDB status line: '" + status_line + "'");
    }

    const int status_code = std::stoi(status_line.substr(0, 3));
    const std::string status_payload =
        status_line.size() > 3 ? trim_whitespace(status_line.substr(3)) : "";

    if (status_code == 202) {
        return {};
    }

    if (status_code == 200) {
        return {parse_candidate_entry(status_payload, true)};
    }

    if (status_code == 210 || status_code == 211) {
        const bool exact = (status_code == 210);
        std::vector<MetadataCandidate> candidates;
        std::string line;
        bool saw_terminator = false;
        while (std::getline(stream, line)) {
            const std::string cleaned = trim_whitespace(line);
            if (cleaned == ".") {
                saw_terminator = true;
                break;
            }
            if (cleaned.empty()) {
                continue;
            }
            candidates.push_back(parse_candidate_entry(cleaned, exact));
        }
        if (!saw_terminator && candidates.empty()) {
            throw std::runtime_error("Truncated CDDB multi-match response (missing '.' terminator)");
        }
        return candidates;
    }

    throw std::runtime_error(
        "CDDB query error (code " + std::to_string(status_code) + "): " + status_payload);
}

MetadataRecord parse_xmcd_record(
    std::string_view response_body,
    const std::string& category,
    const std::string& record_disc_id,
    const std::string& computed_disc_id,
    const std::string& provider_name) {
    std::istringstream stream{std::string(response_body)};
    std::string first_line;
    if (!std::getline(stream, first_line)) {
        throw std::runtime_error("Empty CDDB read response");
    }

    const std::string trimmed_first = trim_whitespace(first_line);
    if (!trimmed_first.empty() && trimmed_first[0] != '#') {
        if (trimmed_first.size() < 3 ||
            !std::isdigit(static_cast<unsigned char>(trimmed_first[0])) ||
            !std::isdigit(static_cast<unsigned char>(trimmed_first[1])) ||
            !std::isdigit(static_cast<unsigned char>(trimmed_first[2]))) {
            throw std::runtime_error("Malformed CDDB read status line: '" + trimmed_first + "'");
        }
        const int code = std::stoi(trimmed_first.substr(0, 3));
        if (code != 210) {
            throw std::runtime_error("CDDB read failed: " + trimmed_first);
        }
    } else {
        stream.clear();
        stream.seekg(0);
    }

    MetadataRecord record{};
    record.disc.databaseProvider = provider_name;
    record.disc.databaseDiscId = !computed_disc_id.empty() ? computed_disc_id : record_disc_id;
    record.disc.externalRecordId =
        category.empty() ? record_disc_id : (category + "/" + record_disc_id);
    record.disc.lookupTimestamp = current_utc_iso8601();

    bool in_offset_section = false;
    bool saw_xmcd_field = false;
    std::string dtitle_accum;
    std::string dyear_accum;
    std::string dgenre_accum;
    std::string extd_accum;
    std::string discid_field;
    std::map<int, std::string> ttitle_map;
    std::map<int, std::string> extt_map;

    std::string raw_line;
    while (std::getline(stream, raw_line)) {
        const std::string line = trim_line(raw_line);
        const std::string trimmed = trim_whitespace(line);
        if (trimmed == ".") {
            break;
        }
        if (trimmed.empty()) {
            continue;
        }

        if (trimmed[0] == '#') {
            const std::string comment_text = trim_whitespace(trimmed.substr(1));
            if (comment_text.find("Track frame offsets") != std::string::npos) {
                in_offset_section = true;
                continue;
            }
            if (comment_text.rfind("Disc length:", 0) == 0) {
                in_offset_section = false;
                const std::string len_part = trim_whitespace(comment_text.substr(12));
                std::istringstream len_iss(len_part);
                std::int64_t seconds = 0;
                if (len_iss >> seconds && seconds > 0) {
                    record.remoteDiscLengthSeconds = seconds;
                }
                continue;
            }
            if (in_offset_section) {
                if (!comment_text.empty() &&
                    std::all_of(comment_text.begin(), comment_text.end(), [](unsigned char c) {
                        return std::isdigit(c) != 0;
                    })) {
                    record.remoteTrackFrameOffsets.push_back(std::stoll(comment_text));
                } else if (!comment_text.empty()) {
                    in_offset_section = false;
                }
            }
            continue;
        }

        in_offset_section = false;
        const std::size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }

        saw_xmcd_field = true;
        const std::string key = trim_whitespace(std::string_view(line).substr(0, eq_pos));
        const std::string value = unescape_xmcd_value(std::string_view(line).substr(eq_pos + 1));

        if (key == "DISCID") {
            if (discid_field.empty()) {
                discid_field = trim_whitespace(value);
            }
        } else if (key == "DTITLE") {
            dtitle_accum += value;
        } else if (key == "DYEAR") {
            dyear_accum += trim_whitespace(value);
        } else if (key == "DGENRE") {
            dgenre_accum += value;
        } else if (key == "EXTD") {
            extd_accum += value;
        } else if (key.rfind("TTITLE", 0) == 0 && key.size() > 6) {
            const std::string idx_str = key.substr(6);
            if (std::all_of(idx_str.begin(), idx_str.end(), [](unsigned char c) {
                    return std::isdigit(c) != 0;
                })) {
                const int track_idx = std::stoi(idx_str);
                ttitle_map[track_idx] += value;
            }
        } else if (key.rfind("EXTT", 0) == 0 && key.size() > 4) {
            const std::string idx_str = key.substr(4);
            if (std::all_of(idx_str.begin(), idx_str.end(), [](unsigned char c) {
                    return std::isdigit(c) != 0;
                })) {
                const int track_idx = std::stoi(idx_str);
                extt_map[track_idx] += value;
            }
        }
    }

    if (!saw_xmcd_field) {
        throw std::runtime_error("Malformed XMCD record: no key=value fields found");
    }

    if (record.disc.databaseDiscId.empty() && !discid_field.empty()) {
        const std::size_t comma = discid_field.find(',');
        record.disc.databaseDiscId =
            trim_whitespace(comma == std::string::npos ? discid_field : discid_field.substr(0, comma));
    }

    const auto [album_artist, album_title] = split_cddb_artist_title(dtitle_accum);
    record.disc.performer = album_artist;
    record.disc.title = album_title;
    record.disc.genre = trim_whitespace(dgenre_accum);
    record.disc.comment = trim_whitespace(extd_accum);

    if (!dyear_accum.empty()) {
        try {
            const int yr = std::stoi(dyear_accum);
            if (yr > 0) {
                record.disc.releaseYear = yr;
            }
        } catch (...) {
        }
    }

    const bool is_various_artists =
        (record.disc.performer == "Various" || record.disc.performer == "Various Artists" ||
         record.disc.performer == "various" || record.disc.performer == "VA");

    for (const auto& [zero_based_idx, raw_track_title] : ttitle_map) {
        TrackMetadata tm{};
        tm.trackNumber = zero_based_idx + 1;
        const std::string cleaned_ttitle = trim_whitespace(raw_track_title);
        if (is_various_artists && cleaned_ttitle.find(" / ") != std::string::npos) {
            const auto [trk_artist, trk_title] = split_cddb_artist_title(cleaned_ttitle);
            tm.performer = trk_artist;
            tm.title = trk_title;
        } else {
            tm.title = cleaned_ttitle;
        }
        record.tracks.push_back(std::move(tm));
    }

    return record;
}

CddbCompatibleProvider::CddbCompatibleProvider(
    CddbClientConfig config,
    std::shared_ptr<CddbHttpTransport> transport)
    : config_(std::move(config)),
      transport_(transport ? std::move(transport) : std::make_shared<CurlCddbHttpTransport>()) {}

std::string CddbCompatibleProvider::providerName() const {
    return "gnudb (CDDB proto " + std::to_string(config_.protocolLevel) + ")";
}

std::vector<MetadataCandidate> CddbCompatibleProvider::search(
    const DiscInfo& disc_info,
    const std::vector<TrackInfo>& tracks) {
    const CddbDiscQueryInfo query_info = calculate_cddb_disc_id(disc_info, tracks);
    const std::string cmd = format_cddb_query_command(query_info);
    const std::string url = build_cddb_cgi_url(config_, cmd);
    const std::string body =
        transport_->performGet(url, config_.connectTimeoutMs, config_.requestTimeoutMs);
    return parse_cddb_query_response(body);
}

MetadataRecord CddbCompatibleProvider::fetch(
    const MetadataCandidate& candidate,
    const DiscInfo& disc_info,
    const std::vector<TrackInfo>& tracks) {
    if (candidate.category.empty() || candidate.discId.empty()) {
        throw std::invalid_argument("MetadataCandidate category and discId must not be empty");
    }
    const CddbDiscQueryInfo query_info = calculate_cddb_disc_id(disc_info, tracks);
    const std::string cmd = "cddb read " + candidate.category + " " + candidate.discId;
    const std::string url = build_cddb_cgi_url(config_, cmd);
    const std::string body =
        transport_->performGet(url, config_.connectTimeoutMs, config_.requestTimeoutMs);
    return parse_xmcd_record(
        body,
        candidate.category,
        candidate.discId,
        query_info.discIdHex,
        providerName());
}

} // namespace mycds
