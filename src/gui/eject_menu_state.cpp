#include "eject_menu_state.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace mycds::gui {

namespace {

std::string to_lower_ascii(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

std::filesystem::path default_start_directory() {
    std::error_code ec;
    if (const char* home = std::getenv("HOME")) {
        std::filesystem::path home_path(home);
        if (std::filesystem::exists(home_path, ec) && std::filesystem::is_directory(home_path, ec)) {
            return home_path;
        }
    }
    auto cwd = std::filesystem::current_path(ec);
    if (!ec && !cwd.empty()) {
        return cwd;
    }
    return std::filesystem::path("/");
}

} // namespace

FileBrowserState::FileBrowserState()
    : FileBrowserState(default_start_directory()) {}

FileBrowserState::FileBrowserState(const std::filesystem::path& initial_dir) {
    if (!navigate_to(initial_dir)) {
        navigate_to(default_start_directory());
    }
}

bool FileBrowserState::is_supported_image_extension(const std::filesystem::path& p) {
    const std::string ext = to_lower_ascii(p.extension().string());
    return ext == ".cue" || ext == ".toc";
}

bool FileBrowserState::navigate_to(const std::filesystem::path& dir) {
    std::error_code ec;
    if (dir.empty() || !std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
        error_message_ = "Directory not found: " + dir.string();
        return false;
    }

    const std::filesystem::path canonical_dir = std::filesystem::weakly_canonical(dir, ec);
    const std::filesystem::path target_dir = ec ? dir : canonical_dir;

    std::vector<FileBrowserEntry> dirs;
    std::vector<FileBrowserEntry> files;

    std::filesystem::directory_iterator it(
        target_dir,
        std::filesystem::directory_options::skip_permission_denied,
        ec);
    if (ec) {
        error_message_ = "Cannot read directory: " + ec.message();
        return false;
    }

    for (const auto& entry : it) {
        std::error_code entry_ec;
        const bool is_dir = entry.is_directory(entry_ec);
        if (entry_ec) {
            continue;
        }
        const std::string filename = entry.path().filename().string();
        if (filename.empty()) {
            continue;
        }
        if (!show_all_files_ && filename[0] == '.') {
            continue;
        }

        if (is_dir) {
            dirs.push_back(FileBrowserEntry{filename, entry.path(), true, 0});
        } else if (show_all_files_ || is_supported_image_extension(entry.path())) {
            const auto sz = entry.file_size(entry_ec);
            files.push_back(FileBrowserEntry{filename, entry.path(), false, entry_ec ? 0 : sz});
        }
    }

    const auto by_name_ci = [](const FileBrowserEntry& a, const FileBrowserEntry& b) {
        return to_lower_ascii(a.name) < to_lower_ascii(b.name);
    };
    std::sort(dirs.begin(), dirs.end(), by_name_ci);
    std::sort(files.begin(), files.end(), by_name_ci);

    all_entries_.clear();
    all_entries_.reserve(dirs.size() + files.size());
    all_entries_.insert(all_entries_.end(),
                        std::make_move_iterator(dirs.begin()),
                        std::make_move_iterator(dirs.end()));
    all_entries_.insert(all_entries_.end(),
                        std::make_move_iterator(files.begin()),
                        std::make_move_iterator(files.end()));

    current_dir_ = target_dir;
    filter_query_.clear();
    error_message_.clear();
    rebuild_filtered_entries();
    return true;
}

void FileBrowserState::rebuild_filtered_entries() {
    entries_.clear();
    if (filter_query_.empty()) {
        entries_ = all_entries_;
    } else {
        const std::string q_lower = to_lower_ascii(filter_query_);
        for (const auto& entry : all_entries_) {
            if (to_lower_ascii(entry.name).find(q_lower) != std::string::npos) {
                entries_.push_back(entry);
            }
        }
    }

    if (entries_.empty()) {
        selected_index_.reset();
        selected_file_.clear();
    } else {
        select_entry(0);
    }
}

bool FileBrowserState::navigate_up() {
    if (!current_dir_.has_parent_path()) {
        return false;
    }
    const auto parent = current_dir_.parent_path();
    if (parent == current_dir_) {
        return false;
    }
    return navigate_to(parent);
}

void FileBrowserState::refresh() {
    if (!current_dir_.empty()) {
        navigate_to(current_dir_);
    }
}

void FileBrowserState::select_entry(std::size_t index) {
    if (index >= entries_.size()) {
        return;
    }
    selected_index_ = index;
    if (!entries_[index].isDirectory) {
        selected_file_ = entries_[index].path;
        error_message_.clear();
    } else {
        selected_file_.clear();
    }
}

void FileBrowserState::move_selection(int delta) {
    if (entries_.empty()) {
        selected_index_.reset();
        selected_file_.clear();
        return;
    }
    const int current = selected_index_.has_value()
        ? static_cast<int>(*selected_index_)
        : 0;
    const int max_idx = static_cast<int>(entries_.size()) - 1;
    const int next = std::clamp(current + delta, 0, max_idx);
    select_entry(static_cast<std::size_t>(next));
}

bool FileBrowserState::activate_entry(std::size_t index) {
    if (index >= entries_.size()) {
        return false;
    }
    const FileBrowserEntry target = entries_[index];
    if (target.isDirectory) {
        navigate_to(target.path);
        return false;
    }
    selected_index_ = index;
    selected_file_ = target.path;
    error_message_.clear();
    return true;
}

void FileBrowserState::set_show_all_files(bool show_all) {
    if (show_all_files_ != show_all) {
        show_all_files_ = show_all;
        refresh();
    }
}

bool FileBrowserState::show_all_files() const {
    return show_all_files_;
}

void FileBrowserState::set_filter_query(const std::string& query) {
    if (filter_query_ != query) {
        filter_query_ = query;
        rebuild_filtered_entries();
    }
}

void FileBrowserState::clear_filter_query() {
    if (!filter_query_.empty()) {
        filter_query_.clear();
        rebuild_filtered_entries();
    }
}

const std::string& FileBrowserState::filter_query() const {
    return filter_query_;
}

const std::filesystem::path& FileBrowserState::current_dir() const {
    return current_dir_;
}

const std::vector<FileBrowserEntry>& FileBrowserState::entries() const {
    return entries_;
}

std::optional<std::size_t> FileBrowserState::selected_index() const {
    return selected_index_;
}

const std::filesystem::path& FileBrowserState::selected_file() const {
    return selected_file_;
}

const std::string& FileBrowserState::error_message() const {
    return error_message_;
}

void FileBrowserState::set_error_message(const std::string& msg) {
    error_message_ = msg;
}

void FileBrowserState::clear_error_message() {
    error_message_.clear();
}

// -----------------------------------------------------------------------------
// LibraryCatalogState
// -----------------------------------------------------------------------------

namespace {

std::vector<LibraryEntry> create_mock_library_entries() {
    return {
        {
            "Pink Floyd",
            "The Dark Side of the Moon",
            1973,
            "Progressive Rock",
            "2026-10-12",
            {
                {1, 222, "Speak to Me / Breathe"},
                {2, 255, "On the Run"},
                {3, 178, "Time"},
                {4, 321, "The Great Gig in the Sky"},
                {5, 192, "Money"},
                {6, 288, "Us and Them"},
                {7, 364, "Any Colour You Like"},
                {8, 213, "Brain Damage"},
                {9, 260, "Eclipse"}
            }
        },
        {
            "The Beatles",
            "Abbey Road",
            1969,
            "Rock",
            "2026-11-01",
            {
                {1, 260, "Come Together"},
                {2, 182, "Something"},
                {3, 207, "Maxwell's Silver Hammer"},
                {4, 196, "Oh! Darling"},
                {5, 171, "Octopus's Garden"},
                {6, 467, "I Want You (She's So Heavy)"},
                {7, 185, "Here Comes the Sun"},
                {8, 165, "Because"},
                {9, 243, "You Never Give Me Your Money"}
            }
        },
        {
            "Miles Davis",
            "Kind of Blue",
            1959,
            "Jazz",
            "2026-10-25",
            {
                {1, 562, "So What"},
                {2, 584, "Freddie Freeloader"},
                {3, 592, "Blue in Green"},
                {4, 693, "All Blues"},
                {5, 565, "Flamenco Sketches"}
            }
        },
        {
            "Daft Punk",
            "Random Access Memories",
            2013,
            "Electronic",
            "2026-11-10",
            {
                {1, 274, "Give Life Back to Music"},
                {2, 321, "The Game of Love"},
                {3, 544, "Giorgio by Moroder"},
                {4, 228, "Within"},
                {5, 335, "Instant Crush"},
                {6, 353, "Lose Yourself to Dance"},
                {7, 405, "Touch"},
                {8, 368, "Get Lucky"}
            }
        },
        {
            "Pink Floyd",
            "Wish You Were Here",
            1975,
            "Progressive Rock",
            "2026-09-18",
            {
                {1, 810, "Shine On You Crazy Diamond (Parts I-V)"},
                {2, 452, "Welcome to the Machine"},
                {3, 308, "Have a Cigar"},
                {4, 340, "Wish You Were Here"},
                {5, 748, "Shine On You Crazy Diamond (Parts VI-IX)"}
            }
        },
        {
            "Fleetwood Mac",
            "Rumours",
            1977,
            "Rock",
            "2026-11-14",
            {
                {1, 165, "Second Hand News"},
                {2, 257, "Dreams"},
                {3, 134, "Never Going Back Again"},
                {4, 208, "Don't Stop"},
                {5, 223, "Go Your Own Way"},
                {6, 200, "Songbird"},
                {7, 270, "The Chain"}
            }
        }
    };
}

int compute_relevance_score(const LibraryEntry& entry, const std::string& query_lower) {
    if (query_lower.empty()) {
        return 1;
    }

    int score = 0;
    const std::string artist_l = to_lower_ascii(entry.artist);
    const std::string album_l = to_lower_ascii(entry.album);

    if (artist_l == query_lower || album_l == query_lower) {
        score += 100;
    } else {
        if (artist_l.find(query_lower) != std::string::npos) {
            score += 40;
        }
        if (album_l.find(query_lower) != std::string::npos) {
            score += 50;
        }
    }

    for (const auto& trk : entry.tracks) {
        if (to_lower_ascii(trk.title).find(query_lower) != std::string::npos) {
            score += 20;
        }
    }
    return score;
}

} // namespace

LibraryCatalogState::LibraryCatalogState()
    : entries_(create_mock_library_entries()) {}

LibraryCatalogState::LibraryCatalogState(std::vector<LibraryEntry> entries)
    : entries_(std::move(entries)) {}

void LibraryCatalogState::set_search_query(const std::string& query) {
    search_query_ = query;
    selected_index_ = 0;
}

const std::string& LibraryCatalogState::search_query() const {
    return search_query_;
}

void LibraryCatalogState::set_genre_filter(const std::string& genre) {
    genre_filter_ = genre;
    selected_index_ = 0;
}

const std::string& LibraryCatalogState::genre_filter() const {
    return genre_filter_;
}

void LibraryCatalogState::set_sort_mode(LibrarySortMode mode) {
    sort_mode_ = mode;
    selected_index_ = 0;
}

LibrarySortMode LibraryCatalogState::sort_mode() const {
    return sort_mode_;
}

std::vector<std::string> LibraryCatalogState::available_genres() const {
    std::set<std::string> unique_genres;
    for (const auto& entry : entries_) {
        if (!entry.genre.empty()) {
            unique_genres.insert(entry.genre);
        }
    }
    return std::vector<std::string>(unique_genres.begin(), unique_genres.end());
}

std::vector<const LibraryEntry*> LibraryCatalogState::filtered_entries() const {
    const std::string query_lower = to_lower_ascii(search_query_);

    struct ScoredEntry {
        const LibraryEntry* entry;
        int score;
        std::size_t originalIndex;
    };

    std::vector<ScoredEntry> matches;
    matches.reserve(entries_.size());

    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& entry = entries_[i];
        if (!genre_filter_.empty() && entry.genre != genre_filter_) {
            continue;
        }
        const int score = compute_relevance_score(entry, query_lower);
        if (score <= 0) {
            continue;
        }
        matches.push_back(ScoredEntry{&entry, score, i});
    }

    std::stable_sort(matches.begin(), matches.end(),
                     [this](const ScoredEntry& a, const ScoredEntry& b) {
                         switch (sort_mode_) {
                             case LibrarySortMode::Relevance:
                                 if (a.score != b.score) {
                                     return a.score > b.score;
                                 }
                                 return a.originalIndex < b.originalIndex;
                             case LibrarySortMode::YearNewest:
                                 if (a.entry->year != b.entry->year) {
                                     return a.entry->year > b.entry->year;
                                 }
                                 return a.originalIndex < b.originalIndex;
                             case LibrarySortMode::YearOldest:
                                 if (a.entry->year != b.entry->year) {
                                     return a.entry->year < b.entry->year;
                                 }
                                 return a.originalIndex < b.originalIndex;
                             case LibrarySortMode::RecentlyAdded:
                                 if (a.entry->addedDate != b.entry->addedDate) {
                                     return a.entry->addedDate > b.entry->addedDate;
                                 }
                                 return a.originalIndex < b.originalIndex;
                         }
                         return a.originalIndex < b.originalIndex;
                     });

    std::vector<const LibraryEntry*> result;
    result.reserve(matches.size());
    for (const auto& m : matches) {
        result.push_back(m.entry);
    }
    return result;
}

void LibraryCatalogState::set_selected_index(std::size_t index) {
    selected_index_ = index;
}

std::size_t LibraryCatalogState::selected_index() const {
    return selected_index_;
}

// -----------------------------------------------------------------------------
// DiskInfoState
// -----------------------------------------------------------------------------

DiskInfoState::DiskInfoState()
    : inlay_slots_{
          {"Front Cover", "Primary Jewel Case Booklet Front"},
          {"Back Inlay", "Tray Card & Tracklist Artwork"},
          {"Booklet Spread", "Inside Liner Notes & Credits"},
          {"CD Disc Art", "Compact Disc Silkscreen Label"}
      } {}

void DiskInfoState::next_inlay() {
    if (!inlay_slots_.empty()) {
        active_inlay_index_ = (active_inlay_index_ + 1) % inlay_slots_.size();
    }
}

void DiskInfoState::prev_inlay() {
    if (!inlay_slots_.empty()) {
        active_inlay_index_ = (active_inlay_index_ + inlay_slots_.size() - 1) % inlay_slots_.size();
    }
}

std::size_t DiskInfoState::active_inlay_index() const {
    return active_inlay_index_;
}

const std::vector<InlayImageSlot>& DiskInfoState::inlay_slots() const {
    return inlay_slots_;
}

void DiskInfoState::set_active_source_index(std::size_t index) {
    active_source_index_ = index;
}

std::size_t DiskInfoState::active_source_index() const {
    return active_source_index_;
}

std::optional<int> DiskInfoState::release_year_for_album(const std::string& artist,
                                                         const std::string& album) const {
    const std::string artist_l = to_lower_ascii(artist);
    const std::string album_l = to_lower_ascii(album);

    for (const auto& entry : create_mock_library_entries()) {
        if (to_lower_ascii(entry.album) == album_l ||
            (album_l.find(to_lower_ascii(entry.album)) != std::string::npos &&
             artist_l.find(to_lower_ascii(entry.artist)) != std::string::npos)) {
            return entry.year;
        }
    }
    return std::nullopt;
}

std::vector<EditorialSource> DiskInfoState::editorial_sources_for_album(const std::string& artist,
                                                                        const std::string& album) const {
    const std::string album_l = to_lower_ascii(album);
    if (album_l.find("dark side of the moon") != std::string::npos) {
        return {
            {
                "Wikipedia",
                "The Dark Side of the Moon is the eighth studio album by the English rock band Pink Floyd, "
                "released on 1 March 1973 by Harvest Records. Developed during live performances before recording "
                "began, it was conceived as a concept album focusing on the pressures faced by the band and "
                "modern life, featuring multitrack tape loops, analog synthesizers, and seamless transitions."
            },
            {
                "Music Magazines",
                "Rolling Stone & Classic Rock Retrospective:\n\n"
                "Engineered by Alan Parsons at Abbey Road Studios, the album set a benchmark for studio "
                "production and hi-fi demonstration discs. Its continuous suite structure rewards uninterrupted "
                "CD playback from 'Speak to Me' through the final heartbeat of 'Eclipse'."
            },
            {
                "Liner Notes",
                "Recorded at Abbey Road Studios, London between June 1972 and January 1973.\n"
                "Produced by Pink Floyd. Engineered by Alan Parsons.\n"
                "Sleeve design by Hipgnosis and George Hardie."
            }
        };
    }

    if (album_l.find("abbey road") != std::string::npos) {
        return {
            {
                "Wikipedia",
                "Abbey Road is the eleventh studio album by the English rock band the Beatles, released on "
                "26 September 1969. It was the last album the band recorded together, notable for its "
                "symphonic Side Two medley and George Martin's orchestral arrangements."
            },
            {
                "Music Magazines",
                "Mojo / Sound On Sound Feature:\n\n"
                "Recorded on a solid-state EMI TG12345 console, Abbey Road marked a dramatic shift toward "
                "richer, warmer stereo imaging and prominent Moog synthesizer textures."
            },
            {
                "Liner Notes",
                "Produced by George Martin. Recorded at EMI Studios, Abbey Road, London (1969)."
            }
        };
    }

    return {
        {
            "Wikipedia",
            "Album: " + album + "\nArtist: " + artist + "\n\n"
            "No cached Wikipedia article is available for this disc yet. "
            "Online metadata and editorial fetching will populate this section when connected."
        },
        {
            "Music Magazines",
            "No magazine reviews or retrospective articles are currently cached for \"" + album + "\"."
        },
        {
            "Liner Notes",
            "Disc loaded in player: " + artist + " - " + album + "."
        }
    };
}

// -----------------------------------------------------------------------------
// Settings & UI State Persistence
// -----------------------------------------------------------------------------

namespace {

std::filesystem::path config_file_path(const char* filename, const char* fallback_name) {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && xdg[0] != '\0') {
        return std::filesystem::path(xdg) / "mycds" / filename;
    }
    if (const char* home = std::getenv("HOME"); home && home[0] != '\0') {
        return std::filesystem::path(home) / ".config" / "mycds" / filename;
    }
    return std::filesystem::path(fallback_name);
}

std::string escape_json_string(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char ch : input) {
        switch (ch) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
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
                out.push_back(ch);
                break;
        }
    }
    return out;
}

std::string parse_json_string_key(const std::string& content, const std::string& key,
                                  const std::string& default_val) {
    const auto pos = content.find("\"" + key + "\"");
    if (pos == std::string::npos) {
        return default_val;
    }
    const auto colon = content.find(':', pos);
    if (colon == std::string::npos) {
        return default_val;
    }
    const auto first_quote = content.find('"', colon + 1);
    if (first_quote == std::string::npos) {
        return default_val;
    }

    std::string result;
    for (std::size_t i = first_quote + 1; i < content.size(); ++i) {
        const char ch = content[i];
        if (ch == '\\' && i + 1 < content.size()) {
            const char next = content[++i];
            switch (next) {
                case '\\':
                    result.push_back('\\');
                    break;
                case '"':
                    result.push_back('"');
                    break;
                case 'n':
                    result.push_back('\n');
                    break;
                case 'r':
                    result.push_back('\r');
                    break;
                case 't':
                    result.push_back('\t');
                    break;
                default:
                    result.push_back(next);
                    break;
            }
        } else if (ch == '"') {
            return result;
        } else {
            result.push_back(ch);
        }
    }
    return default_val;
}

} // namespace

std::filesystem::path default_settings_path() {
    return config_file_path("settings.json", "mycds_settings.json");
}

std::filesystem::path default_ui_state_path() {
    return config_file_path("state.json", "mycds_state.json");
}

AppSettings load_app_settings(const std::filesystem::path& path) {
    AppSettings settings;
    std::ifstream in(path);
    if (!in.is_open()) {
        return settings;
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string content = ss.str();

    auto parse_bool_key = [&](const std::string& key, bool default_val) {
        const auto pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) {
            return default_val;
        }
        const auto colon = content.find(':', pos);
        if (colon == std::string::npos) {
            return default_val;
        }
        const auto true_pos = content.find("true", colon);
        const auto false_pos = content.find("false", colon);
        const auto newline_pos = content.find('\n', colon);
        if (true_pos != std::string::npos && (newline_pos == std::string::npos || true_pos < newline_pos)) {
            if (false_pos == std::string::npos || true_pos < false_pos) {
                return true;
            }
        }
        if (false_pos != std::string::npos && (newline_pos == std::string::npos || false_pos < newline_pos)) {
            return false;
        }
        return default_val;
    };

    settings.play_when_started = parse_bool_key("play_when_started", settings.play_when_started);
    settings.auto_play_on_disc_load = parse_bool_key("auto_play_on_disc_load", settings.auto_play_on_disc_load);
    settings.show_playlist = parse_bool_key("show_playlist", settings.show_playlist);
    settings.show_cd_cover = parse_bool_key("show_cd_cover", settings.show_cd_cover);

    auto parse_layout_mode = [&](LayoutMode default_val) {
        const auto pos = content.find("\"layout_mode\"");
        if (pos == std::string::npos) {
            return default_val;
        }
        const auto colon = content.find(':', pos);
        if (colon == std::string::npos) {
            return default_val;
        }
        const auto newline_pos = content.find('\n', colon);
        const auto line_end = (newline_pos == std::string::npos) ? content.size() : newline_pos;
        const std::string line = content.substr(colon, line_end - colon);
        if (line.find("\"horizontal\"") != std::string::npos) {
            return LayoutMode::Horizontal;
        }
        if (line.find("\"vertical\"") != std::string::npos) {
            return LayoutMode::Vertical;
        }
        if (line.find("\"dynamic\"") != std::string::npos) {
            return LayoutMode::Dynamic;
        }
        return default_val;
    };
    settings.layout_mode = parse_layout_mode(settings.layout_mode);

    auto parse_file_browser_view = [&](FileBrowserViewMode default_val) {
        const auto pos = content.find("\"file_browser_view\"");
        if (pos == std::string::npos) {
            return default_val;
        }
        const auto colon = content.find(':', pos);
        if (colon == std::string::npos) {
            return default_val;
        }
        const auto newline_pos = content.find('\n', colon);
        const auto line_end = (newline_pos == std::string::npos) ? content.size() : newline_pos;
        const std::string line = content.substr(colon, line_end - colon);
        if (line.find("\"icons\"") != std::string::npos) {
            return FileBrowserViewMode::Icons;
        }
        if (line.find("\"list\"") != std::string::npos) {
            return FileBrowserViewMode::List;
        }
        return default_val;
    };
    settings.file_browser_view = parse_file_browser_view(settings.file_browser_view);

    auto parse_library_view = [&](LibraryViewMode default_val) {
        const auto pos = content.find("\"library_view\"");
        if (pos == std::string::npos) {
            return default_val;
        }
        const auto colon = content.find(':', pos);
        if (colon == std::string::npos) {
            return default_val;
        }
        const auto newline_pos = content.find('\n', colon);
        const auto line_end = (newline_pos == std::string::npos) ? content.size() : newline_pos;
        const std::string line = content.substr(colon, line_end - colon);
        if (line.find("\"covers\"") != std::string::npos) {
            return LibraryViewMode::Covers;
        }
        if (line.find("\"list\"") != std::string::npos) {
            return LibraryViewMode::List;
        }
        return default_val;
    };
    settings.library_view = parse_library_view(settings.library_view);
    return settings;
}

bool save_app_settings(const std::filesystem::path& path, const AppSettings& settings) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return false;
        }
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    const char* layout_str = "dynamic";
    switch (settings.layout_mode) {
        case LayoutMode::Dynamic:
            layout_str = "dynamic";
            break;
        case LayoutMode::Horizontal:
            layout_str = "horizontal";
            break;
        case LayoutMode::Vertical:
            layout_str = "vertical";
            break;
    }

    const char* browser_view_str =
        (settings.file_browser_view == FileBrowserViewMode::Icons) ? "icons" : "list";
    const char* library_view_str =
        (settings.library_view == LibraryViewMode::Covers) ? "covers" : "list";

    out << "{\n"
        << "  \"play_when_started\": " << (settings.play_when_started ? "true" : "false") << ",\n"
        << "  \"auto_play_on_disc_load\": " << (settings.auto_play_on_disc_load ? "true" : "false") << ",\n"
        << "  \"layout_mode\": \"" << layout_str << "\",\n"
        << "  \"show_playlist\": " << (settings.show_playlist ? "true" : "false") << ",\n"
        << "  \"show_cd_cover\": " << (settings.show_cd_cover ? "true" : "false") << ",\n"
        << "  \"file_browser_view\": \"" << browser_view_str << "\",\n"
        << "  \"library_view\": \"" << library_view_str << "\"\n"
        << "}\n";
    return out.good();
}

PersistentUiState load_ui_state(const std::filesystem::path& path) {
    PersistentUiState state;
    std::ifstream in(path);
    if (!in.is_open()) {
        return state;
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string content = ss.str();

    const std::string screen_str = parse_json_string_key(content, "last_eject_screen", "cd");
    if (screen_str == "image_file") {
        state.last_eject_screen = EjectMenuScreen::ImageFile;
    } else if (screen_str == "library") {
        state.last_eject_screen = EjectMenuScreen::Library;
    } else if (screen_str == "disk_info") {
        state.last_eject_screen = EjectMenuScreen::DiskInfo;
    } else if (screen_str == "settings") {
        state.last_eject_screen = EjectMenuScreen::Settings;
    } else {
        state.last_eject_screen = EjectMenuScreen::Cd;
    }

    state.last_visited_dir = parse_json_string_key(content, "last_visited_dir", "");
    state.last_cd_rom = parse_json_string_key(content, "last_cd_rom", "");
    return state;
}

bool save_ui_state(const std::filesystem::path& path, const PersistentUiState& state) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return false;
        }
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    const char* screen_str = "cd";
    switch (state.last_eject_screen) {
        case EjectMenuScreen::Cd:
            screen_str = "cd";
            break;
        case EjectMenuScreen::ImageFile:
            screen_str = "image_file";
            break;
        case EjectMenuScreen::Library:
            screen_str = "library";
            break;
        case EjectMenuScreen::DiskInfo:
            screen_str = "disk_info";
            break;
        case EjectMenuScreen::Settings:
            screen_str = "settings";
            break;
    }

    out << "{\n"
        << "  \"last_eject_screen\": \"" << screen_str << "\",\n"
        << "  \"last_visited_dir\": \"" << escape_json_string(state.last_visited_dir) << "\",\n"
        << "  \"last_cd_rom\": \"" << escape_json_string(state.last_cd_rom) << "\"\n"
        << "}\n";
    return out.good();
}

} // namespace mycds::gui
