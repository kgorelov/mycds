#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "cd_display_model.h"

namespace mycds::gui {

struct FileBrowserEntry {
    std::string name{};
    std::filesystem::path path{};
    bool isDirectory{false};
    std::uintmax_t fileSize{0};
};

/// Pure C++ filesystem browser state for selecting .cue and .toc disc images.
class FileBrowserState {
public:
    FileBrowserState();
    explicit FileBrowserState(const std::filesystem::path& initial_dir);

    bool navigate_to(const std::filesystem::path& dir);
    bool navigate_up();
    void refresh();

    void select_entry(std::size_t index);
    void move_selection(int delta);
    /// Activates an entry (enters directory, or selects file and returns true if ready to load).
    bool activate_entry(std::size_t index);

    void set_show_all_files(bool show_all);
    [[nodiscard]] bool show_all_files() const;

    void set_filter_query(const std::string& query);
    void clear_filter_query();
    [[nodiscard]] const std::string& filter_query() const;

    [[nodiscard]] const std::filesystem::path& current_dir() const;
    [[nodiscard]] const std::vector<FileBrowserEntry>& entries() const;
    [[nodiscard]] std::optional<std::size_t> selected_index() const;
    [[nodiscard]] const std::filesystem::path& selected_file() const;
    [[nodiscard]] const std::string& error_message() const;
    void set_error_message(const std::string& msg);
    void clear_error_message();

    static bool is_supported_image_extension(const std::filesystem::path& p);

private:
    void rebuild_filtered_entries();

    std::filesystem::path current_dir_{};
    std::vector<FileBrowserEntry> all_entries_{};
    std::vector<FileBrowserEntry> entries_{};
    std::optional<std::size_t> selected_index_{};
    std::filesystem::path selected_file_{};
    bool show_all_files_{false};
    std::string filter_query_{};
    std::string error_message_{};
};

// -----------------------------------------------------------------------------
// Library Mock Catalog State
// -----------------------------------------------------------------------------

struct LibraryEntry {
    std::string artist{};
    std::string album{};
    int year{0};
    std::string genre{};
    std::string addedDate{}; // ISO YYYY-MM-DD
    std::vector<TrackInfo> tracks{};
};

enum class LibrarySortMode {
    Relevance,
    YearNewest,
    YearOldest,
    RecentlyAdded
};

/// Search, filter, and sort state for the mock CD Library screen.
class LibraryCatalogState {
public:
    LibraryCatalogState();
    explicit LibraryCatalogState(std::vector<LibraryEntry> entries);

    void set_search_query(const std::string& query);
    [[nodiscard]] const std::string& search_query() const;

    void set_genre_filter(const std::string& genre);
    [[nodiscard]] const std::string& genre_filter() const;

    void set_year_filter(int year);
    [[nodiscard]] int year_filter() const;

    void set_sort_mode(LibrarySortMode mode);
    [[nodiscard]] LibrarySortMode sort_mode() const;

    [[nodiscard]] std::vector<std::string> available_genres() const;
    [[nodiscard]] std::vector<int> available_years() const;
    [[nodiscard]] std::vector<const LibraryEntry*> filtered_entries() const;

    void set_selected_index(std::size_t index);
    [[nodiscard]] std::size_t selected_index() const;

private:
    std::vector<LibraryEntry> entries_{};
    std::string search_query_{};
    std::string genre_filter_{};
    int year_filter_{0};
    LibrarySortMode sort_mode_{LibrarySortMode::Relevance};
    std::size_t selected_index_{0};
};

// -----------------------------------------------------------------------------
// Disk Info & Inlay Gallery State
// -----------------------------------------------------------------------------

struct InlayImageSlot {
    std::string label{};
    std::string subtitle{};
};

struct EditorialSource {
    std::string sourceName{};
    std::string text{};
};

/// Holds cover/inlay slot selection and editorial source tabs for Disk Info.
class DiskInfoState {
public:
    DiskInfoState();

    void next_inlay();
    void prev_inlay();
    [[nodiscard]] std::size_t active_inlay_index() const;
    [[nodiscard]] const std::vector<InlayImageSlot>& inlay_slots() const;

    void set_active_source_index(std::size_t index);
    [[nodiscard]] std::size_t active_source_index() const;

    [[nodiscard]] std::optional<int> release_year_for_album(const std::string& artist,
                                                              const std::string& album) const;
    [[nodiscard]] std::vector<EditorialSource> editorial_sources_for_album(const std::string& artist,
                                                                           const std::string& album) const;

private:
    std::vector<InlayImageSlot> inlay_slots_{};
    std::size_t active_inlay_index_{0};
    std::size_t active_source_index_{0};
};

// -----------------------------------------------------------------------------
// Settings & UI State Persistence
// -----------------------------------------------------------------------------

struct PersistentUiState {
    EjectMenuScreen last_eject_screen{EjectMenuScreen::Cd};
    std::string last_visited_dir{};
    std::string last_cd_rom{};
};

std::filesystem::path default_settings_path();
AppSettings load_app_settings(const std::filesystem::path& path);
bool save_app_settings(const std::filesystem::path& path, const AppSettings& settings);

std::filesystem::path default_ui_state_path();
PersistentUiState load_ui_state(const std::filesystem::path& path);
bool save_ui_state(const std::filesystem::path& path, const PersistentUiState& state);

} // namespace mycds::gui
