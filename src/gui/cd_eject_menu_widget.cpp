#include "cd_eject_menu_widget.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "cd_brushed_metal.h"
#include "cd_ui_style.h"
#include "eject_menu_state.h"
#include "domain/audio_format.hpp"
#include "source/image_source.hpp"
#include "source/physical_cd_source.hpp"

namespace mycds::gui {

namespace {

struct NavItem {
    EjectMenuScreen screen;
    const char* id;
    const char* label;
    const char* title;
};

constexpr NavItem kNavItems[] = {
    {EjectMenuScreen::Cd, "##NavCd", "CD", "LOAD FROM PHYSICAL CD DRIVE"},
    {EjectMenuScreen::ImageFile, "##NavImageFile", "Image/File", "LOAD DISC IMAGE (.CUE / .TOC)"},
    {EjectMenuScreen::Library, "##NavLibrary", "Library", "CD LIBRARY"},
    {EjectMenuScreen::DiskInfo, "##NavDiskInfo", "Disk Info", "DISC INFORMATION & ARTWORK"},
    {EjectMenuScreen::Settings, "##NavSettings", "Settings", "PROGRAM SETTINGS"},
};

const char* ScreenTitle(EjectMenuScreen screen) {
    for (const auto& item : kNavItems) {
        if (item.screen == screen) {
            return item.title;
        }
    }
    return "EJECT MENU";
}

void DrawCornerScrews(ImDrawList* draw_list, const ImVec2& min_pos, const ImVec2& max_pos) {
    DrawChassisScrew(draw_list, ImVec2(min_pos.x + 10.0f, min_pos.y + 10.0f));
    DrawChassisScrew(draw_list, ImVec2(max_pos.x - 10.0f, min_pos.y + 10.0f));
    DrawChassisScrew(draw_list, ImVec2(min_pos.x + 10.0f, max_pos.y - 10.0f));
    DrawChassisScrew(draw_list, ImVec2(max_pos.x - 10.0f, max_pos.y - 10.0f));
}

void LoadDiscSourceIntoModel(const mycds::DiscSource& source, CdDisplayModel& model) {
    const auto raw_tracks = source.getTracks();
    const auto disc_meta = source.getDiscMetadata();
    const auto track_meta = source.getTrackMetadata();

    std::vector<TrackInfo> gui_tracks;
    gui_tracks.reserve(raw_tracks.size());

    for (const auto& trk : raw_tracks) {
        const std::int64_t sectors = std::max<std::int64_t>(0, trk.endLba - trk.startLba);
        const int duration_sec = static_cast<int>(sectors / mycds::AudioFormat::kSectorsPerSecond);

        std::string title;
        for (const auto& tm : track_meta) {
            if (tm.trackNumber == trk.number && !tm.title.empty()) {
                title = tm.title;
                break;
            }
        }
        if (title.empty()) {
            title = (trk.type == mycds::TrackType::Data)
                ? ("Track " + std::to_string(trk.number) + " [Data]")
                : ("Track " + std::to_string(trk.number));
        }

        gui_tracks.push_back(TrackInfo{trk.number, duration_sec, std::move(title)});
    }

    if (gui_tracks.empty()) {
        return;
    }

    std::string artist = !disc_meta.performer.empty() ? disc_meta.performer : "Unknown Artist";
    std::string album = disc_meta.title;
    if (album.empty()) {
        const std::filesystem::path src_path(source.getSourcePath());
        album = src_path.has_stem() ? src_path.stem().string() : source.getSourceKind();
    }

    model.load_disc(artist, album, gui_tracks);
    model.set_has_cddb_data(!disc_meta.empty());
}

std::string FormatDurationMmSs(int total_seconds) {
    const int mins = std::max(0, total_seconds) / 60;
    const int secs = std::max(0, total_seconds) % 60;
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", mins, secs);
    return std::string(buf);
}

std::string FormatFileSize(std::uintmax_t bytes) {
    char buf[32];
    if (bytes < 1024) {
        std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    } else if (bytes < 1024 * 1024) {
        std::snprintf(buf, sizeof(buf), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    return std::string(buf);
}

void RenderCdLoadScreen(CdDisplayModel& model) {
    struct CdScreenState {
        bool initialized{false};
        std::vector<mycds::CdDriveInfo> drives{};
        int selected_index{0};
        std::string status_message{};
        bool status_is_error{false};
    };
    static CdScreenState state;

    auto refresh_drives = [&]() {
        state.drives = mycds::enumerate_cd_drives();
        if (state.selected_index >= static_cast<int>(state.drives.size())) {
            state.selected_index = 0;
        }
    };

    if (!state.initialized) {
        state.initialized = true;
        refresh_drives();
    }

    ImGui::TextColored(ImVec4(0.78f, 0.82f, 0.88f, 1.0f),
                       "Select an optical CD drive present in the system:");
    ImGui::Spacing();

    const ImVec2 refresh_pos = ImGui::GetCursorScreenPos();
    if (RenderHardwareTextButton("##RefreshDrives", "Refresh Drives", refresh_pos, ImVec2(170.0f, 34.0f))) {
        refresh_drives();
        state.status_message.clear();
        state.status_is_error = false;
    }
    ImGui::SetCursorScreenPos(ImVec2(refresh_pos.x, refresh_pos.y + 42.0f));
    ImGui::Dummy(ImVec2(1.0f, 4.0f));

    // Recessed drive list area
    const float list_height = std::max(140.0f, ImGui::GetContentRegionAvail().y - 90.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.07f, 0.08f, 0.90f));
    ImGui::BeginChild("##CdDriveList", ImVec2(0.0f, list_height), true);
    {
        if (state.drives.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.25f, 1.0f),
                               "No optical CD drives detected on this system.");
            ImGui::TextDisabled("Connect a drive or click 'Refresh Drives' to scan again.");
        } else {
            for (int i = 0; i < static_cast<int>(state.drives.size()); ++i) {
                const auto& drv = state.drives[static_cast<std::size_t>(i)];
                const bool is_selected = (state.selected_index == i);

                std::string label = drv.devicePath;
                if (!drv.vendor.empty() || !drv.model.empty()) {
                    label += "  [" + drv.vendor + (drv.vendor.empty() || drv.model.empty() ? "" : " ") + drv.model + "]";
                }

                if (ImGui::Selectable(label.c_str(), is_selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    state.selected_index = i;
                    state.status_message.clear();
                    state.status_is_error = false;
                }

                ImGui::SameLine();
                if (drv.hasDisc) {
                    const std::string dur = FormatDurationMmSs(drv.totalSeconds);
                    ImGui::TextColored(ImVec4(0.25f, 0.90f, 0.40f, 1.0f),
                                       "  [%s: %d tracks, %s]",
                                       drv.statusText.c_str(), drv.trackCount, dur.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.58f, 0.62f, 0.68f, 1.0f),
                                       "  [%s]", drv.statusText.c_str());
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing();
    if (!state.status_message.empty()) {
        const ImVec4 col = state.status_is_error
            ? ImVec4(1.0f, 0.38f, 0.35f, 1.0f)
            : ImVec4(0.25f, 0.90f, 0.45f, 1.0f);
        ImGui::TextColored(col, "%s", state.status_message.c_str());
    } else {
        ImGui::Dummy(ImVec2(1.0f, ImGui::GetTextLineHeight()));
    }
    ImGui::Spacing();

    const bool has_valid_drive = !state.drives.empty() &&
        state.selected_index >= 0 &&
        state.selected_index < static_cast<int>(state.drives.size());
    const bool can_load = has_valid_drive && state.drives[static_cast<std::size_t>(state.selected_index)].hasDisc;

    const ImVec2 btn_row_pos = ImGui::GetCursorScreenPos();
    if (RenderHardwareTextButton("##BtnLoadCd", "Load Disc", btn_row_pos, ImVec2(160.0f, 38.0f), false, can_load)) {
        const auto& drv = state.drives[static_cast<std::size_t>(state.selected_index)];
        try {
            mycds::PhysicalCdSource source(drv.devicePath);
            LoadDiscSourceIntoModel(source, model);
            state.status_message.clear();
            state.status_is_error = false;
        } catch (const std::exception& ex) {
            state.status_message = std::string("Failed to load CD: ") + ex.what();
            state.status_is_error = true;
        }
    }

    if (RenderHardwareTextButton("##BtnEjectTray", "Eject Tray",
                                 ImVec2(btn_row_pos.x + 176.0f, btn_row_pos.y),
                                 ImVec2(160.0f, 38.0f), false, has_valid_drive)) {
        const auto& drv = state.drives[static_cast<std::size_t>(state.selected_index)];
        model.stop();
        if (mycds::eject_cd_drive(drv.devicePath)) {
            state.status_message = "Ejected tray: " + drv.devicePath;
            state.status_is_error = false;
            refresh_drives();
        } else {
            state.status_message = "Could not eject tray: " + drv.devicePath;
            state.status_is_error = true;
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(btn_row_pos.x, btn_row_pos.y + 42.0f));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
}

void RenderImageFileScreen(CdDisplayModel& model, bool just_entered) {
    static FileBrowserState browser;
    static char path_buf[512] = "";
    static std::filesystem::path synced_dir;

    if (synced_dir != browser.current_dir()) {
        synced_dir = browser.current_dir();
        std::snprintf(path_buf, sizeof(path_buf), "%s", synced_dir.string().c_str());
    }

    auto try_load_selected = [&](const std::filesystem::path& file_path) {
        try {
            mycds::ImageSource source(file_path);
            LoadDiscSourceIntoModel(source, model);
            browser.clear_error_message();
        } catch (const std::exception& ex) {
            browser.set_error_message(std::string("Failed to load image: ") + ex.what());
        }
    };

    // Navigation bar: Home, Root, Up, Refresh, Show all files, and right-aligned Load Image
    const float avail_w = ImGui::GetContentRegionAvail().x;
    const ImVec2 nav_pos = ImGui::GetCursorScreenPos();
    if (RenderHardwareTextButton("##ImgHome", "Home", nav_pos, ImVec2(80.0f, 32.0f))) {
        if (const char* home = std::getenv("HOME")) {
            browser.navigate_to(home);
        }
        ImGui::SetWindowFocus("##ImageBrowserList");
    }
    if (RenderHardwareTextButton("##ImgRoot", "Root", ImVec2(nav_pos.x + 88.0f, nav_pos.y), ImVec2(80.0f, 32.0f))) {
        browser.navigate_to("/");
        ImGui::SetWindowFocus("##ImageBrowserList");
    }
    if (RenderHardwareTextButton("##ImgUp", "Up ..", ImVec2(nav_pos.x + 176.0f, nav_pos.y), ImVec2(80.0f, 32.0f))) {
        browser.navigate_up();
        ImGui::SetWindowFocus("##ImageBrowserList");
    }
    if (RenderHardwareTextButton("##ImgRefresh", "Refresh", ImVec2(nav_pos.x + 264.0f, nav_pos.y), ImVec2(92.0f, 32.0f))) {
        browser.refresh();
        ImGui::SetWindowFocus("##ImageBrowserList");
    }

    ImGui::SetCursorScreenPos(ImVec2(nav_pos.x + 368.0f, nav_pos.y + 5.0f));
    bool show_all = browser.show_all_files();
    if (ImGui::Checkbox("Show all files", &show_all)) {
        browser.set_show_all_files(show_all);
        ImGui::SetWindowFocus("##ImageBrowserList");
    }

    const bool can_load = !browser.selected_file().empty();
    constexpr float kLoadBtnW = 148.0f;
    const ImVec2 load_pos(nav_pos.x + std::max(510.0f, avail_w - kLoadBtnW), nav_pos.y);
    if (RenderHardwareTextButton("##BtnLoadImage", "Load Image", load_pos, ImVec2(kLoadBtnW, 32.0f), false, can_load)) {
        try_load_selected(browser.selected_file());
    }

    ImGui::SetCursorScreenPos(ImVec2(nav_pos.x, nav_pos.y + 42.0f));

    // Path input bar
    ImGui::SetNextItemWidth(std::max(180.0f, avail_w - 86.0f));
    if (ImGui::InputText("##BrowserPath", path_buf, sizeof(path_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
        browser.navigate_to(std::filesystem::path(path_buf));
        ImGui::SetWindowFocus("##ImageBrowserList");
    }
    ImGui::SameLine();
    if (ImGui::Button("Go", ImVec2(72.0f, 0.0f))) {
        browser.navigate_to(std::filesystem::path(path_buf));
        ImGui::SetWindowFocus("##ImageBrowserList");
    }

    // Active substring filter bar (shown when typing in the file browser)
    if (!browser.filter_query().empty()) {
        ImGui::TextColored(ImVec4(0.0f, 0.90f, 1.0f, 1.0f), "Filter: \"%s\"",
                           browser.filter_query().c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            browser.clear_filter_query();
            ImGui::SetWindowFocus("##ImageBrowserList");
        }
    } else {
        ImGui::Spacing();
    }

    // Directory & file list (slightly larger font + full keyboard navigation)
    const float list_height = std::max(160.0f, ImGui::GetContentRegionAvail().y - 34.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.07f, 0.08f, 0.90f));
    ImGui::BeginChild("##ImageBrowserList", ImVec2(0.0f, list_height), true,
                      ImGuiWindowFlags_NoNavInputs);
    {
        if (just_entered) {
            ImGui::SetWindowFocus();
        }

        ImGuiIO& io = ImGui::GetIO();
        bool scroll_to_selection = false;
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !io.WantTextInput) {
            if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                browser.navigate_up();
                scroll_to_selection = true;
            } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                browser.move_selection(-1);
                scroll_to_selection = true;
            } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                browser.move_selection(1);
                scroll_to_selection = true;
            } else if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
                browser.move_selection(-10);
                scroll_to_selection = true;
            } else if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
                browser.move_selection(10);
                scroll_to_selection = true;
            } else if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
                if (browser.selected_index().has_value()) {
                    if (browser.activate_entry(*browser.selected_index())) {
                        try_load_selected(browser.selected_file());
                    }
                    scroll_to_selection = true;
                }
            } else if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
                if (!browser.filter_query().empty()) {
                    std::string q = browser.filter_query();
                    q.pop_back();
                    browser.set_filter_query(q);
                } else {
                    browser.navigate_up();
                }
                scroll_to_selection = true;
            }

            // Substring filter typing via ImGuiKey (works even when SDL text input is inactive)
            if (!io.KeyCtrl && !io.KeyAlt && !io.KeySuper) {
                std::string q = browser.filter_query();
                bool added = false;
                for (int k = ImGuiKey_A; k <= ImGuiKey_Z; ++k) {
                    if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k))) {
                        const char base = io.KeyShift ? 'A' : 'a';
                        q.push_back(static_cast<char>(base + (k - ImGuiKey_A)));
                        added = true;
                    }
                }
                for (int k = ImGuiKey_0; k <= ImGuiKey_9; ++k) {
                    if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k))) {
                        q.push_back(static_cast<char>('0' + (k - ImGuiKey_0)));
                        added = true;
                    }
                }
                for (int k = ImGuiKey_Keypad0; k <= ImGuiKey_Keypad9; ++k) {
                    if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k))) {
                        q.push_back(static_cast<char>('0' + (k - ImGuiKey_Keypad0)));
                        added = true;
                    }
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
                    q.push_back(' ');
                    added = true;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Minus)) {
                    q.push_back(io.KeyShift ? '_' : '-');
                    added = true;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Period) || ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal)) {
                    q.push_back('.');
                    added = true;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Comma)) {
                    q.push_back(',');
                    added = true;
                }
                if (added) {
                    browser.set_filter_query(q);
                    scroll_to_selection = true;
                }
            }
        }

        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.18f);
        const auto& entries = browser.entries();
        if (entries.empty()) {
            ImGui::TextDisabled(browser.filter_query().empty()
                                    ? "(No matching .cue or .toc files in this directory)"
                                    : "(No entries match current filter)");
        } else if (ImGui::BeginTable("##FileBrowserTable", 3,
                                     ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 68.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 104.0f);
            ImGui::TableHeadersRow();

            for (std::size_t i = 0; i < entries.size(); ++i) {
                const auto& entry = entries[i];
                const bool is_selected = browser.selected_index().has_value() && (*browser.selected_index() == i);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (entry.isDirectory) {
                    ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f), "[DIR]");
                } else if (FileBrowserState::is_supported_image_extension(entry.path)) {
                    ImGui::TextColored(ImVec4(0.0f, 0.90f, 1.0f, 1.0f), "[IMG]");
                } else {
                    ImGui::TextDisabled("[FILE]");
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(entry.name.c_str(), is_selected,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    browser.select_entry(i);
                    ImGui::SetWindowFocus("##ImageBrowserList");
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        if (browser.activate_entry(i)) {
                            try_load_selected(browser.selected_file());
                        }
                    }
                }
                if (is_selected && scroll_to_selection) {
                    ImGui::SetScrollHereY(0.5f);
                }
                ImGui::PopID();

                ImGui::TableSetColumnIndex(2);
                if (entry.isDirectory) {
                    ImGui::TextDisabled("--");
                } else {
                    const std::string sz_str = FormatFileSize(entry.fileSize);
                    ImGui::TextUnformatted(sz_str.c_str());
                }
            }
            ImGui::EndTable();
        }
        ImGui::PopFont();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing();
    if (!browser.error_message().empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.35f, 1.0f), "%s", browser.error_message().c_str());
    } else if (!browser.selected_file().empty()) {
        ImGui::TextColored(ImVec4(0.0f, 0.90f, 1.0f, 1.0f), "Selected: %s",
                           browser.selected_file().filename().string().c_str());
    } else {
        ImGui::TextDisabled("Arrows/PgUp/PgDn to navigate, Enter to open/load, Backspace/Alt+Up for parent, type to filter.");
    }
}

void RenderLibraryScreen(CdDisplayModel& model) {
    static LibraryCatalogState library;
    static char search_buf[128] = "";

    ImGui::TextDisabled("Mock Library Catalog (search by artist, album, or track name):");
    ImGui::Spacing();

    // Search + Filter + Sort toolbar
    const float avail_w = ImGui::GetContentRegionAvail().x;
    const float search_w = std::max(180.0f, avail_w * 0.36f);
    ImGui::SetNextItemWidth(search_w);
    if (ImGui::InputTextWithHint("##LibSearch", "Search artist, album, or track...",
                                 search_buf, sizeof(search_buf))) {
        library.set_search_query(search_buf);
    }

    ImGui::SameLine();
    const auto genres = library.available_genres();
    const std::string& active_genre = library.genre_filter();
    const char* genre_preview = active_genre.empty() ? "All Genres" : active_genre.c_str();
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::BeginCombo("##LibGenreFilter", genre_preview)) {
        if (ImGui::Selectable("All Genres", active_genre.empty())) {
            library.set_genre_filter("");
        }
        for (const auto& g : genres) {
            const bool selected = (active_genre == g);
            if (ImGui::Selectable(g.c_str(), selected)) {
                library.set_genre_filter(g);
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    const auto years = library.available_years();
    const int active_year = library.year_filter();
    char year_preview[24];
    if (active_year <= 0) {
        std::snprintf(year_preview, sizeof(year_preview), "All Years");
    } else {
        std::snprintf(year_preview, sizeof(year_preview), "%d", active_year);
    }
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::BeginCombo("##LibYearFilter", year_preview)) {
        if (ImGui::Selectable("All Years", active_year == 0)) {
            library.set_year_filter(0);
        }
        for (int y : years) {
            char ybuf[16];
            std::snprintf(ybuf, sizeof(ybuf), "%d", y);
            if (ImGui::Selectable(ybuf, active_year == y)) {
                library.set_year_filter(y);
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    const char* sort_labels[] = {
        "Sort: Relevance",
        "Sort: Year (Newest)",
        "Sort: Year (Oldest)",
        "Sort: Recently Added"
    };
    const int sort_idx = static_cast<int>(library.sort_mode());
    ImGui::SetNextItemWidth(175.0f);
    if (ImGui::BeginCombo("##LibSortMode", sort_labels[sort_idx])) {
        for (int i = 0; i < 4; ++i) {
            if (ImGui::Selectable(sort_labels[i], sort_idx == i)) {
                library.set_sort_mode(static_cast<LibrarySortMode>(i));
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();

    const auto matches = library.filtered_entries();
    const float body_h = std::max(140.0f, ImGui::GetContentRegionAvail().y - 54.0f);
    const float left_w = std::max(260.0f, avail_w * 0.54f);

    // Left: matching albums list
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.07f, 0.08f, 0.90f));
    ImGui::BeginChild("##LibAlbumList", ImVec2(left_w, body_h), true);
    {
        if (matches.empty()) {
            ImGui::TextDisabled("No albums match your search/filter criteria.");
        } else {
            for (std::size_t i = 0; i < matches.size(); ++i) {
                const auto* entry = matches[i];
                const bool is_selected = (library.selected_index() == i);
                char row_buf[256];
                std::snprintf(row_buf, sizeof(row_buf), "%s - %s (%d) [%s]",
                              entry->artist.c_str(), entry->album.c_str(),
                              entry->year, entry->genre.c_str());
                if (ImGui::Selectable(row_buf, is_selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    library.set_selected_index(i);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        model.load_disc(entry->artist, entry->album, entry->tracks);
                        model.set_has_cddb_data(true);
                    }
                }
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right: selected album track preview
    const LibraryEntry* selected_entry = (!matches.empty() && library.selected_index() < matches.size())
        ? matches[library.selected_index()]
        : nullptr;

    ImGui::BeginChild("##LibTrackPreview", ImVec2(0.0f, body_h), true);
    {
        if (!selected_entry) {
            ImGui::TextDisabled("Select an album to preview tracks.");
        } else {
            ImGui::TextColored(ImVec4(0.0f, 0.90f, 1.0f, 1.0f), "%s", selected_entry->album.c_str());
            ImGui::TextColored(ImVec4(0.80f, 0.84f, 0.90f, 1.0f), "%s (%d)  |  Added: %s",
                               selected_entry->artist.c_str(), selected_entry->year,
                               selected_entry->addedDate.c_str());
            ImGui::Separator();
            ImGui::Spacing();
            for (const auto& trk : selected_entry->tracks) {
                const std::string dur = FormatDurationMmSs(trk.duration_seconds);
                ImGui::Text("%02d. %s  (%s)", trk.number, trk.title.c_str(), dur.c_str());
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing();
    const ImVec2 load_pos = ImGui::GetCursorScreenPos();
    if (RenderHardwareTextButton("##BtnLoadLibAlbum", "Load Album", load_pos, ImVec2(170.0f, 38.0f),
                                 false, selected_entry != nullptr)) {
        model.load_disc(selected_entry->artist, selected_entry->album, selected_entry->tracks);
        model.set_has_cddb_data(true);
    }
    ImGui::SetCursorScreenPos(ImVec2(load_pos.x, load_pos.y + 42.0f));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
}

void RenderDiskInfoScreen(CdDisplayModel& model) {
    static DiskInfoState info_state;

    const float avail_w = ImGui::GetContentRegionAvail().x;
    const float avail_h = ImGui::GetContentRegionAvail().y;
    const float left_col_w = std::max(280.0f, avail_w * 0.42f);

    // Left column: Large Multi-Inlay Cover Art Viewer + Album/Release Summary
    ImGui::BeginChild("##DiskInfoLeft", ImVec2(left_col_w, avail_h), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        const auto& slots = info_state.inlay_slots();
        const std::size_t inlay_idx = info_state.active_inlay_index();
        const InlayImageSlot& active_slot = slots[inlay_idx];

        // Album title & release year header
        ImGui::TextColored(ImVec4(0.0f, 0.90f, 1.0f, 1.0f), "%s", model.album_title().c_str());
        const auto release_year = info_state.release_year_for_album(model.artist_name(), model.album_title());
        if (release_year.has_value()) {
            ImGui::Text("Artist: %s  |  Released: %d", model.artist_name().c_str(), *release_year);
        } else {
            ImGui::Text("Artist: %s  |  Released: Unknown", model.artist_name().c_str());
        }
        ImGui::Spacing();

        // Large Cover / Inlay Artwork Card filling most of the left column without overflowing
        const float remaining_h = ImGui::GetContentRegionAvail().y;
        const float controls_reserve_h = 64.0f;
        const float art_w = std::max(180.0f, left_col_w - 8.0f);
        const float art_h = std::max(140.0f, remaining_h - controls_reserve_h);
        const ImVec2 art_min = ImGui::GetCursorScreenPos();
        const ImVec2 art_max(art_min.x + art_w, art_min.y + art_h);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        draw_list->AddRectFilled(art_min, art_max, IM_COL32(16, 18, 22, 255), 4.0f);
        draw_list->AddRect(art_min, art_max, IM_COL32(0, 180, 220, 140), 4.0f, 0, 1.5f);

        // Stylized CD jewel-case disc graphic scaled to the cover frame
        const float disc_radius = std::max(36.0f, std::min(art_w, art_h) * 0.34f);
        const ImVec2 disc_center(art_min.x + art_w * 0.5f, art_min.y + art_h * 0.5f);
        draw_list->AddCircle(disc_center, disc_radius, IM_COL32(0, 210, 240, 140), 48, 1.8f);
        draw_list->AddCircle(disc_center, disc_radius * 0.68f, IM_COL32(0, 160, 195, 70), 40, 1.0f);
        draw_list->AddCircle(disc_center, disc_radius * 0.30f, IM_COL32(0, 210, 240, 120), 28, 1.3f);
        draw_list->AddCircleFilled(disc_center, disc_radius * 0.14f, IM_COL32(12, 14, 18, 255));

        char slot_badge[64];
        std::snprintf(slot_badge, sizeof(slot_badge), "%zu / %zu: %s",
                      inlay_idx + 1, slots.size(), active_slot.label.c_str());
        draw_list->AddText(ImVec2(art_min.x + 12.0f, art_min.y + 10.0f), kColCyanBright, slot_badge);
        draw_list->AddText(ImVec2(art_min.x + 12.0f, art_max.y - 24.0f),
                           IM_COL32(170, 180, 195, 220), active_slot.subtitle.c_str());

        ImGui::Dummy(ImVec2(art_w, art_h + 6.0f));

        // Prev / Next inlay switcher buttons centered under cover
        const ImVec2 nav_pos = ImGui::GetCursorScreenPos();
        if (RenderHardwareTextButton("##InlayPrev", "< Prev", nav_pos, ImVec2(104.0f, 34.0f))) {
            info_state.prev_inlay();
        }
        if (RenderHardwareTextButton("##InlayNext", "Next >",
                                     ImVec2(nav_pos.x + 114.0f, nav_pos.y), ImVec2(104.0f, 34.0f))) {
            info_state.next_inlay();
        }
        ImGui::SetCursorScreenPos(ImVec2(nav_pos.x, nav_pos.y + 38.0f));
        ImGui::Dummy(ImVec2(1.0f, 1.0f));
    }
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 16.0f);

    // Right column: Two tabs ("Album info" and "Tracks info")
    ImGui::BeginChild("##DiskInfoRight", ImVec2(0.0f, avail_h), false);
    {
        const ImVec2 tab_pos = ImGui::GetCursorScreenPos();
        const std::size_t active_tab = info_state.active_source_index() % 2;

        if (RenderHardwareTextButton("##InfoTabAlbum", "Album info",
                                     tab_pos, ImVec2(160.0f, 36.0f), active_tab == 0)) {
            info_state.set_active_source_index(0);
        }
        if (RenderHardwareTextButton("##InfoTabTracks", "Tracks info",
                                     ImVec2(tab_pos.x + 170.0f, tab_pos.y), ImVec2(160.0f, 36.0f), active_tab == 1)) {
            info_state.set_active_source_index(1);
        }
        ImGui::SetCursorScreenPos(ImVec2(tab_pos.x, tab_pos.y + 46.0f));

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.07f, 0.08f, 0.90f));
        ImGui::BeginChild("##DiskInfoTabContent", ImVec2(0.0f, 0.0f), true);
        {
            if (active_tab == 0) {
                // Aggregated Album Info (Wikipedia, Music Magazines, Liner Notes)
                const auto sources = info_state.editorial_sources_for_album(
                    model.artist_name(), model.album_title());
                for (std::size_t i = 0; i < sources.size(); ++i) {
                    if (i > 0) {
                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();
                    }
                    ImGui::TextColored(ImVec4(0.0f, 0.90f, 1.0f, 1.0f), "%s",
                                       sources[i].sourceName.c_str());
                    ImGui::Spacing();
                    ImGui::TextWrapped("%s", sources[i].text.c_str());
                }
            } else {
                // Tracks Info tab
                const auto& tracks = model.tracks();
                int total_seconds = 0;
                for (const auto& trk : tracks) {
                    total_seconds += std::max(0, trk.duration_seconds);
                }
                const std::string total_dur = FormatDurationMmSs(total_seconds);
                ImGui::TextColored(ImVec4(0.0f, 0.90f, 1.0f, 1.0f),
                                   "Tracklist (%zu tracks, Total %s)", tracks.size(), total_dur.c_str());
                ImGui::Separator();
                ImGui::Spacing();

                if (ImGui::BeginTable("##DiskInfoTrackTable", 3,
                                      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
                    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 36.0f);
                    ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed, 72.0f);
                    ImGui::TableHeadersRow();

                    for (const auto& trk : tracks) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%02d", trk.number);

                        ImGui::TableSetColumnIndex(1);
                        const std::string title = trk.title.empty()
                            ? ("Track " + std::to_string(trk.number))
                            : trk.title;
                        ImGui::TextUnformatted(title.c_str());

                        ImGui::TableSetColumnIndex(2);
                        const std::string dur = FormatDurationMmSs(trk.duration_seconds);
                        ImGui::TextUnformatted(dur.c_str());
                    }
                    ImGui::EndTable();
                }
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
}

void RenderSettingsScreen(CdDisplayModel& model) {
    static std::string status_msg;

    ImGui::TextColored(ImVec4(0.78f, 0.82f, 0.88f, 1.0f),
                       "Configure startup and disc-loading playback behavior:");
    ImGui::Spacing();
    ImGui::Spacing();

    AppSettings settings = model.app_settings();
    bool changed = false;

    if (ImGui::Checkbox("Play when started", &settings.play_when_started)) {
        changed = true;
    }
    ImGui::TextDisabled("    Automatically begin playback when the application starts.");
    ImGui::Spacing();

    if (ImGui::Checkbox("Auto play on disc load", &settings.auto_play_on_disc_load)) {
        changed = true;
    }
    ImGui::TextDisabled("    Automatically start playing after loading a CD, image file, or library album.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.78f, 0.82f, 0.88f, 1.0f),
                       "Layout & Optional Widgets:");
    ImGui::Spacing();

    const char* layout_items[] = {"Dynamic (Auto)", "Horizontal", "Vertical"};
    int layout_idx = 0;
    switch (settings.layout_mode) {
        case LayoutMode::Dynamic:
            layout_idx = 0;
            break;
        case LayoutMode::Horizontal:
            layout_idx = 1;
            break;
        case LayoutMode::Vertical:
            layout_idx = 2;
            break;
    }
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::Combo("Layout", &layout_idx, layout_items, 3)) {
        if (layout_idx == 0) {
            settings.layout_mode = LayoutMode::Dynamic;
        } else if (layout_idx == 1) {
            settings.layout_mode = LayoutMode::Horizontal;
        } else {
            settings.layout_mode = LayoutMode::Vertical;
        }
        changed = true;
    }
    ImGui::TextDisabled("    Dynamic adapts between horizontal, vertical, and minimal based on window size.");
    ImGui::Spacing();

    if (ImGui::Checkbox("Show playlist", &settings.show_playlist)) {
        changed = true;
    }
    ImGui::TextDisabled("    Display the track playlist when enough room is available.");
    ImGui::Spacing();

    if (ImGui::Checkbox("Show CD cover", &settings.show_cd_cover)) {
        changed = true;
    }
    ImGui::TextDisabled("    Display the square CD cover under controls in horizontal layout when room allows.");
    ImGui::Spacing();

    if (changed) {
        model.set_app_settings(settings);
        const auto cfg_path = default_settings_path();
        if (save_app_settings(cfg_path, settings)) {
            status_msg = "Settings saved to " + cfg_path.string();
        } else {
            status_msg = "Settings updated in memory (could not write " + cfg_path.string() + ").";
        }
    }

    if (!status_msg.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.25f, 0.90f, 0.45f, 1.0f), "%s", status_msg.c_str());
    }
}

} // namespace

void RenderEjectMenu(CdDisplayModel& model, const ImVec2& size) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float total_w = (size.x > 0.0f) ? size.x : std::max(avail.x, 600.0f);
    const float total_h = (size.y > 0.0f) ? size.y : std::max(avail.y, 380.0f);

    const ImVec2 origin = ImGui::GetCursorScreenPos();

    constexpr float kSidebarWidth = 210.0f;
    constexpr float kPanelGap = 18.0f;
    constexpr float kInnerPadX = 16.0f;
    constexpr float kInnerPadY = 20.0f;
    constexpr float kBtnHeight = 42.0f;
    constexpr float kBtnGap = 10.0f;

    // -------------------------------------------------------------------------
    // Left Navigation Panel
    // -------------------------------------------------------------------------
    const ImVec2 sidebar_min = origin;
    const ImVec2 sidebar_max(origin.x + kSidebarWidth, origin.y + total_h);
    GetBrushedMetal().RenderPanelChassis(draw_list, sidebar_min, sidebar_max, 8.0f);
    DrawCornerScrews(draw_list, sidebar_min, sidebar_max);

    // Subtle silkscreen header label
    const char* menu_header = "EJECT MENU";
    const ImVec2 header_sz = ImGui::CalcTextSize(menu_header);
    draw_list->AddText(
        ImVec2(sidebar_min.x + (kSidebarWidth - header_sz.x) * 0.5f, sidebar_min.y + 12.0f),
        IM_COL32(140, 150, 165, 200),
        menu_header
    );

    const float btn_width = kSidebarWidth - 2.0f * kInnerPadX;
    float btn_y = sidebar_min.y + 38.0f;

    for (const auto& item : kNavItems) {
        const bool is_active = (model.active_eject_screen() == item.screen);
        if (RenderHardwareTextButton(item.id, item.label,
                                     ImVec2(sidebar_min.x + kInnerPadX, btn_y),
                                     ImVec2(btn_width, kBtnHeight),
                                     is_active)) {
            model.set_active_eject_screen(item.screen);
        }
        btn_y += kBtnHeight + kBtnGap;
    }

    // Separated Back button anchored at the bottom of the left panel
    const float back_y = sidebar_max.y - kInnerPadY - kBtnHeight;
    const float sep_y = back_y - 14.0f;
    draw_list->AddLine(
        ImVec2(sidebar_min.x + kInnerPadX, sep_y),
        ImVec2(sidebar_max.x - kInnerPadX, sep_y),
        IM_COL32(28, 30, 35, 255), 1.5f
    );
    draw_list->AddLine(
        ImVec2(sidebar_min.x + kInnerPadX, sep_y + 1.5f),
        ImVec2(sidebar_max.x - kInnerPadX, sep_y + 1.5f),
        IM_COL32(75, 80, 90, 140), 1.0f
    );

    if (RenderHardwareTextButton("##NavBack", "Back",
                                 ImVec2(sidebar_min.x + kInnerPadX, back_y),
                                 ImVec2(btn_width, kBtnHeight),
                                 false)) {
        model.close_eject_menu();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Return to CD Player (ESC)");
    }

    // -------------------------------------------------------------------------
    // Right Content Panel
    // -------------------------------------------------------------------------
    const ImVec2 content_min(sidebar_max.x + kPanelGap, origin.y);
    const ImVec2 content_max(origin.x + total_w, origin.y + total_h);
    GetBrushedMetal().RenderPanelChassis(draw_list, content_min, content_max, 8.0f);
    DrawCornerScrews(draw_list, content_min, content_max);

    const float content_w = std::max(100.0f, (content_max.x - content_min.x) - 40.0f);
    const float content_h = std::max(100.0f, (content_max.y - content_min.y) - 36.0f);

    ImGui::SetCursorScreenPos(ImVec2(content_min.x + 20.0f, content_min.y + 18.0f));
    ImGui::BeginChild("##EjectMenuContent", ImVec2(content_w, content_h), false,
                      ImGuiWindowFlags_NoBackground);
    {
        ImGui::TextColored(ImVec4(0.0f, 0.90f, 1.0f, 1.0f), "%s",
                           ScreenTitle(model.active_eject_screen()));
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        static EjectMenuScreen prev_screen = EjectMenuScreen::Cd;
        const bool just_entered_image =
            (model.active_eject_screen() == EjectMenuScreen::ImageFile) &&
            (ImGui::IsWindowAppearing() || prev_screen != EjectMenuScreen::ImageFile);
        prev_screen = model.active_eject_screen();

        switch (model.active_eject_screen()) {
            case EjectMenuScreen::Cd:
                RenderCdLoadScreen(model);
                break;
            case EjectMenuScreen::ImageFile:
                RenderImageFileScreen(model, just_entered_image);
                break;
            case EjectMenuScreen::Library:
                RenderLibraryScreen(model);
                break;
            case EjectMenuScreen::DiskInfo:
                RenderDiskInfoScreen(model);
                break;
            case EjectMenuScreen::Settings:
                RenderSettingsScreen(model);
                break;
        }
    }
    ImGui::EndChild();

    // Advance layout cursor cleanly
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(total_w, total_h));
}

} // namespace mycds::gui
