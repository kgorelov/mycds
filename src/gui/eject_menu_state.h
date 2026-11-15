#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

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
    /// Activates an entry (enters directory, or selects file and returns true if ready to load).
    bool activate_entry(std::size_t index);

    void set_show_all_files(bool show_all);
    [[nodiscard]] bool show_all_files() const;

    [[nodiscard]] const std::filesystem::path& current_dir() const;
    [[nodiscard]] const std::vector<FileBrowserEntry>& entries() const;
    [[nodiscard]] std::optional<std::size_t> selected_index() const;
    [[nodiscard]] const std::filesystem::path& selected_file() const;
    [[nodiscard]] const std::string& error_message() const;
    void set_error_message(const std::string& msg);
    void clear_error_message();

    static bool is_supported_image_extension(const std::filesystem::path& p);

private:
    std::filesystem::path current_dir_{};
    std::vector<FileBrowserEntry> entries_{};
    std::optional<std::size_t> selected_index_{};
    std::filesystem::path selected_file_{};
    bool show_all_files_{false};
    std::string error_message_{};
};

} // namespace mycds::gui
