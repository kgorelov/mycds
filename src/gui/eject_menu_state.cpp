#include "eject_menu_state.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
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

    entries_.clear();
    entries_.reserve(dirs.size() + files.size());
    entries_.insert(entries_.end(),
                    std::make_move_iterator(dirs.begin()),
                    std::make_move_iterator(dirs.end()));
    entries_.insert(entries_.end(),
                    std::make_move_iterator(files.begin()),
                    std::make_move_iterator(files.end()));

    current_dir_ = target_dir;
    selected_index_.reset();
    selected_file_.clear();
    error_message_.clear();
    return true;
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

} // namespace mycds::gui
