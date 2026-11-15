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

void RenderImageFileScreen(CdDisplayModel& model) {
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

    // Navigation bar: Home, Root, Up, Refresh
    const ImVec2 nav_pos = ImGui::GetCursorScreenPos();
    if (RenderHardwareTextButton("##ImgHome", "Home", nav_pos, ImVec2(84.0f, 32.0f))) {
        if (const char* home = std::getenv("HOME")) {
            browser.navigate_to(home);
        }
    }
    if (RenderHardwareTextButton("##ImgRoot", "Root", ImVec2(nav_pos.x + 92.0f, nav_pos.y), ImVec2(84.0f, 32.0f))) {
        browser.navigate_to("/");
    }
    if (RenderHardwareTextButton("##ImgUp", "Up ..", ImVec2(nav_pos.x + 184.0f, nav_pos.y), ImVec2(84.0f, 32.0f))) {
        browser.navigate_up();
    }
    if (RenderHardwareTextButton("##ImgRefresh", "Refresh", ImVec2(nav_pos.x + 276.0f, nav_pos.y), ImVec2(100.0f, 32.0f))) {
        browser.refresh();
    }

    ImGui::SetCursorScreenPos(ImVec2(nav_pos.x + 390.0f, nav_pos.y + 5.0f));
    bool show_all = browser.show_all_files();
    if (ImGui::Checkbox("Show all files", &show_all)) {
        browser.set_show_all_files(show_all);
    }

    ImGui::SetCursorScreenPos(ImVec2(nav_pos.x, nav_pos.y + 42.0f));

    // Path input bar
    const float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(std::max(180.0f, avail_w - 86.0f));
    if (ImGui::InputText("##BrowserPath", path_buf, sizeof(path_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
        browser.navigate_to(std::filesystem::path(path_buf));
    }
    ImGui::SameLine();
    if (ImGui::Button("Go", ImVec2(72.0f, 0.0f))) {
        browser.navigate_to(std::filesystem::path(path_buf));
    }

    ImGui::Spacing();

    // Directory & file list
    const float list_height = std::max(140.0f, ImGui::GetContentRegionAvail().y - 96.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.07f, 0.08f, 0.90f));
    ImGui::BeginChild("##ImageBrowserList", ImVec2(0.0f, list_height), true);
    {
        const auto& entries = browser.entries();
        if (entries.empty()) {
            ImGui::TextDisabled("(No matching .cue or .toc files in this directory)");
        } else if (ImGui::BeginTable("##FileBrowserTable", 3,
                                     ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 64.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 96.0f);
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
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        if (browser.activate_entry(i)) {
                            try_load_selected(browser.selected_file());
                        }
                    }
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
        ImGui::TextDisabled("Select a .cue or .toc file (double-click directories to open).");
    }
    ImGui::Spacing();

    const bool can_load = !browser.selected_file().empty();
    const ImVec2 load_pos = ImGui::GetCursorScreenPos();
    if (RenderHardwareTextButton("##BtnLoadImage", "Load Image", load_pos, ImVec2(170.0f, 38.0f), false, can_load)) {
        try_load_selected(browser.selected_file());
    }
    ImGui::SetCursorScreenPos(ImVec2(load_pos.x, load_pos.y + 42.0f));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
}

void RenderLibraryScreen(CdDisplayModel& /*model*/) {
    ImGui::TextColored(ImVec4(0.75f, 0.78f, 0.84f, 1.0f),
                       "Search and filter discs in your library by artist, album, track, genre, or year.");
    ImGui::Spacing();
    ImGui::TextDisabled("Mock library catalog, search, filters, and sorting will be added in Phase 3.");
}

void RenderDiskInfoScreen(CdDisplayModel& model) {
    ImGui::TextColored(ImVec4(0.0f, 0.90f, 1.0f, 1.0f), "%s - %s",
                       model.artist_name().c_str(), model.album_title().c_str());
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.75f, 0.78f, 0.84f, 1.0f),
                       "Loaded tracks: %d", model.total_tracks());
    ImGui::Spacing();
    ImGui::TextDisabled("Cover/inlay image gallery and band/album notes will be added in Phase 3.");
}

void RenderSettingsScreen(CdDisplayModel& /*model*/) {
    ImGui::TextColored(ImVec4(0.75f, 0.78f, 0.84f, 1.0f),
                       "Configure startup and disc-loading playback preferences.");
    ImGui::Spacing();
    ImGui::TextDisabled("Settings toggles (Play when started, Auto play on disc load) will be added in Phase 3.");
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

        switch (model.active_eject_screen()) {
            case EjectMenuScreen::Cd:
                RenderCdLoadScreen(model);
                break;
            case EjectMenuScreen::ImageFile:
                RenderImageFileScreen(model);
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
