#include "cd_eject_menu_widget.h"

#include <algorithm>
#include <cmath>
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
    {EjectMenuScreen::DiskInfo, "##NavDiskInfo", "Disc Info", "DISC INFORMATION & ARTWORK"},
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

std::string EllipsizeText(ImFont* font, float font_size, const std::string& text, float max_width) {
    if (!font || text.empty() || max_width <= 0.0f) {
        return text;
    }
    if (font->CalcTextSizeA(font_size, 10000.0f, 0.0f, text.c_str()).x <= max_width) {
        return text;
    }
    const char* ellipsis = "...";
    const float ellipsis_w = font->CalcTextSizeA(font_size, 10000.0f, 0.0f, ellipsis).x;
    if (ellipsis_w >= max_width) {
        return ellipsis;
    }

    std::size_t len = text.size();
    while (len > 0) {
        const std::string candidate = text.substr(0, len) + ellipsis;
        if (font->CalcTextSizeA(font_size, 10000.0f, 0.0f, candidate.c_str()).x <= max_width) {
            return candidate;
        }
        --len;
    }
    return ellipsis;
}

void DrawCenteredText(ImDrawList* draw_list, ImFont* font, float font_size,
                      float box_x, float box_w, float y, ImU32 col, const std::string& text) {
    if (!draw_list || !font || text.empty()) {
        return;
    }
    const std::string fitted = EllipsizeText(font, font_size, text, box_w);
    const ImVec2 sz = font->CalcTextSizeA(font_size, 10000.0f, 0.0f, fitted.c_str());
    const float x = box_x + std::max(0.0f, (box_w - sz.x) * 0.5f);
    draw_list->AddText(font, font_size, ImVec2(x, y), col, fitted.c_str());
}

void DrawCdRomDriveIcon(ImDrawList* draw_list, const ImVec2& center,
                        bool has_disc, bool is_selected, bool hovered) {
    if (!draw_list) {
        return;
    }

    // Large optical disc positioned above/entering the drive bezel
    const ImVec2 disc_center(center.x, center.y - 10.0f);
    constexpr float kDiscRadius = 38.0f;

    if (has_disc) {
        const ImU32 disc_fill = is_selected
            ? IM_COL32(36, 56, 68, 245)
            : (hovered ? IM_COL32(44, 54, 66, 240) : IM_COL32(36, 42, 52, 235));
        const ImU32 disc_rim = is_selected ? kColCyanGlowCore : (hovered ? kColCyanBright : IM_COL32(0, 195, 230, 210));

        draw_list->AddCircleFilled(disc_center, kDiscRadius, disc_fill, 48);

        // Specular sheen lines across the optical surface
        const ImU32 sheen_col = is_selected ? IM_COL32(160, 245, 255, 70) : IM_COL32(140, 225, 245, 45);
        draw_list->AddLine(ImVec2(disc_center.x - 24.0f, disc_center.y - 22.0f),
                           ImVec2(disc_center.x - 8.0f, disc_center.y - 7.0f), sheen_col, 5.0f);
        draw_list->AddLine(ImVec2(disc_center.x + 8.0f, disc_center.y + 7.0f),
                           ImVec2(disc_center.x + 24.0f, disc_center.y + 22.0f), sheen_col, 5.0f);

        draw_list->AddCircle(disc_center, kDiscRadius, disc_rim, 48, is_selected ? 2.0f : 1.6f);
        draw_list->AddCircle(disc_center, kDiscRadius * 0.68f, IM_COL32(0, 185, 220, 85), 36, 1.0f);
        draw_list->AddCircle(disc_center, kDiscRadius * 0.34f, IM_COL32(130, 215, 235, 160), 28, 1.2f);
    } else {
        // Dimmed/ghosted disc silhouette for empty drives
        draw_list->AddCircleFilled(disc_center, kDiscRadius, IM_COL32(22, 25, 30, 185), 48);
        draw_list->AddCircle(disc_center, kDiscRadius, IM_COL32(82, 90, 104, 130), 48, 1.4f);
        draw_list->AddCircle(disc_center, kDiscRadius * 0.68f, IM_COL32(65, 72, 84, 70), 36, 1.0f);
        draw_list->AddCircle(disc_center, kDiscRadius * 0.34f, IM_COL32(82, 90, 104, 110), 28, 1.1f);
    }

    // Center spindle hole
    draw_list->AddCircleFilled(disc_center, kDiscRadius * 0.14f, kColSocketBg, 20);
    draw_list->AddCircle(disc_center, kDiscRadius * 0.14f,
                         has_disc ? IM_COL32(130, 220, 240, 180) : IM_COL32(85, 92, 104, 140), 20, 1.1f);

    // Optical drive front bezel / tray unit overlapping the bottom of the disc
    const ImVec2 drive_min(center.x - 46.0f, center.y + 20.0f);
    const ImVec2 drive_max(center.x + 46.0f, center.y + 44.0f);
    DrawHardwareButtonSocket(draw_list,
                             ImVec2(drive_min.x - 2.0f, drive_min.y - 2.0f),
                             ImVec2(drive_max.x + 2.0f, drive_max.y + 2.0f));
    DrawButtonBrushedFace(draw_list, drive_min, drive_max, hovered, false);

    // Recessed disc tray slot
    draw_list->AddRectFilled(ImVec2(drive_min.x + 8.0f, drive_min.y + 6.0f),
                             ImVec2(drive_max.x - 8.0f, drive_min.y + 10.0f),
                             IM_COL32(10, 12, 15, 255), 1.5f);

    // Drive status LED on bottom-left of bezel
    const ImVec2 led_pos(drive_min.x + 13.0f, drive_max.y - 7.0f);
    if (has_disc) {
        draw_list->AddCircleFilled(led_pos, 4.5f, IM_COL32(45, 230, 100, 65), 12);
        draw_list->AddCircleFilled(led_pos, 2.3f, IM_COL32(70, 245, 120, 255), 12);
    } else {
        draw_list->AddCircleFilled(led_pos, 2.2f, IM_COL32(95, 102, 114, 190), 12);
    }

    // Small eject button detail on bottom-right of bezel
    const ImVec2 ej_min(drive_max.x - 19.0f, drive_max.y - 9.0f);
    const ImVec2 ej_max(drive_max.x - 9.0f, drive_max.y - 5.0f);
    draw_list->AddRectFilled(ej_min, ej_max, IM_COL32(44, 48, 56, 255), 1.0f);
    draw_list->AddRect(ej_min, ej_max, IM_COL32(120, 128, 142, 160), 1.0f);
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

    auto load_drive = [&](const mycds::CdDriveInfo& drv) {
        try {
            mycds::PhysicalCdSource source(drv.devicePath);
            LoadDiscSourceIntoModel(source, model);
            state.status_message.clear();
            state.status_is_error = false;
        } catch (const std::exception& ex) {
            state.status_message = std::string("Failed to load CD: ") + ex.what();
            state.status_is_error = true;
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

    // Recessed drive icons grid area
    const float list_height = std::max(180.0f, ImGui::GetContentRegionAvail().y - 90.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.07f, 0.08f, 0.90f));
    ImGui::BeginChild("##CdDriveList", ImVec2(0.0f, list_height), true);
    {
        if (state.drives.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.25f, 1.0f),
                               "No optical CD drives detected on this system.");
            ImGui::TextDisabled("Connect a drive or click 'Refresh Drives' to scan again.");
        } else {
            constexpr float kCardWidth = 192.0f;
            constexpr float kCardHeight = 186.0f;
            constexpr float kCardGap = 16.0f;
            constexpr float kGridPad = 12.0f;

            const float avail_w = ImGui::GetContentRegionAvail().x;
            const int cols = std::max(
                1,
                static_cast<int>((avail_w - 2.0f * kGridPad + kCardGap) / (kCardWidth + kCardGap))
            );
            const ImVec2 grid_origin = ImGui::GetCursorPos();
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImFont* font = ImGui::GetFont();
            const float base_font_sz = ImGui::GetFontSize();

            for (int i = 0; i < static_cast<int>(state.drives.size()); ++i) {
                const auto& drv = state.drives[static_cast<std::size_t>(i)];
                const bool is_selected = (state.selected_index == i);
                const int col = i % cols;
                const int row = i / cols;

                ImGui::SetCursorPos(ImVec2(
                    grid_origin.x + kGridPad + static_cast<float>(col) * (kCardWidth + kCardGap),
                    grid_origin.y + kGridPad + static_cast<float>(row) * (kCardHeight + kCardGap)
                ));

                const ImVec2 card_min = ImGui::GetCursorScreenPos();
                const ImVec2 card_max(card_min.x + kCardWidth, card_min.y + kCardHeight);

                ImGui::PushID(i);
                ImGui::InvisibleButton("##DriveIconCard", ImVec2(kCardWidth, kCardHeight));
                const bool hovered = ImGui::IsItemHovered();
                const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                const bool double_clicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                ImGui::PopID();

                if (clicked) {
                    state.selected_index = i;
                    state.status_message.clear();
                    state.status_is_error = false;
                }
                if (double_clicked && drv.hasDisc) {
                    state.selected_index = i;
                    load_drive(drv);
                }

                // Card background and selection frame
                const ImU32 card_bg = is_selected
                    ? IM_COL32(20, 30, 38, 240)
                    : (hovered ? IM_COL32(24, 28, 34, 230) : IM_COL32(16, 18, 22, 220));
                draw_list->AddRectFilled(card_min, card_max, card_bg, 6.0f);

                if (is_selected) {
                    DrawDiffusedRectOutline(draw_list, card_min, card_max, 6.0f, kColCyanBright, 1.5f, true);
                } else {
                    const ImU32 border_col = hovered ? IM_COL32(95, 115, 135, 180) : IM_COL32(48, 52, 60, 160);
                    draw_list->AddRect(card_min, card_max, border_col, 6.0f, 0, 1.0f);
                }

                // Big CD-ROM icon in upper section of card
                const ImVec2 icon_center(card_min.x + kCardWidth * 0.5f, card_min.y + 62.0f);
                DrawCdRomDriveIcon(draw_list, icon_center, drv.hasDisc, is_selected, hovered);

                // Device name and details underneath the icon
                const float text_box_x = card_min.x + 10.0f;
                const float text_box_w = kCardWidth - 20.0f;

                const ImU32 dev_col = is_selected ? kColCyanGlowCore : IM_COL32(226, 232, 242, 255);
                DrawCenteredText(draw_list, font, base_font_sz * 1.08f,
                                 text_box_x, text_box_w, card_min.y + 116.0f,
                                 dev_col, drv.devicePath);

                std::string hw_name = drv.vendor;
                if (!drv.model.empty()) {
                    if (!hw_name.empty()) {
                        hw_name += ' ';
                    }
                    hw_name += drv.model;
                }
                if (!hw_name.empty()) {
                    DrawCenteredText(draw_list, font, base_font_sz * 0.90f,
                                     text_box_x, text_box_w, card_min.y + 138.0f,
                                     IM_COL32(150, 160, 175, 230), hw_name);
                }

                if (drv.hasDisc) {
                    const std::string dur = FormatDurationMmSs(drv.totalSeconds);
                    char disc_summary[64];
                    std::snprintf(disc_summary, sizeof(disc_summary), "%d tracks  %s",
                                  drv.trackCount, dur.c_str());
                    DrawCenteredText(draw_list, font, base_font_sz * 0.92f,
                                     text_box_x, text_box_w, card_min.y + 159.0f,
                                     IM_COL32(64, 230, 102, 255), disc_summary);
                } else {
                    DrawCenteredText(draw_list, font, base_font_sz * 0.90f,
                                     text_box_x, text_box_w, card_min.y + 159.0f,
                                     IM_COL32(130, 138, 150, 220), drv.statusText);
                }

                if (hovered) {
                    std::string tip = drv.devicePath;
                    if (!hw_name.empty()) {
                        tip += "\n" + hw_name;
                    }
                    tip += "\nStatus: " + drv.statusText;
                    if (drv.hasDisc) {
                        tip += " (" + std::to_string(drv.trackCount) + " tracks, " +
                               FormatDurationMmSs(drv.totalSeconds) + ")";
                    }
                    ImGui::SetTooltip("%s", tip.c_str());
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
        load_drive(state.drives[static_cast<std::size_t>(state.selected_index)]);
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

enum class NavButtonIcon {
    Home,
    Root,
    Up,
    Refresh,
    ViewList,
    ViewIcons
};

void DrawNavButtonIcon(ImDrawList* draw_list, const ImVec2& center, NavButtonIcon icon,
                       ImU32 col, bool is_bright) {
    if (!draw_list) {
        return;
    }
    const float t = 1.8f;
    switch (icon) {
        case NavButtonIcon::Home: {
            // Roof chevron + house body
            DrawDiffusedLine(draw_list, ImVec2(center.x - 8.5f, center.y - 0.5f),
                             ImVec2(center.x, center.y - 8.0f), col, t, is_bright);
            DrawDiffusedLine(draw_list, ImVec2(center.x, center.y - 8.0f),
                             ImVec2(center.x + 8.5f, center.y - 0.5f), col, t, is_bright);
            DrawDiffusedRectOutline(draw_list, ImVec2(center.x - 5.5f, center.y - 0.5f),
                                    ImVec2(center.x + 5.5f, center.y + 7.5f), 1.0f, col, 1.5f, is_bright);
            break;
        }
        case NavButtonIcon::Root: {
            // Forward slash "/" with subtle base bar
            DrawDiffusedLine(draw_list, ImVec2(center.x - 4.0f, center.y + 7.5f),
                             ImVec2(center.x + 4.5f, center.y - 7.5f), col, 2.2f, is_bright);
            DrawDiffusedLine(draw_list, ImVec2(center.x - 7.0f, center.y + 7.5f),
                             ImVec2(center.x + 7.0f, center.y + 7.5f), col, 1.3f, is_bright);
            break;
        }
        case NavButtonIcon::Up: {
            // Upward arrow
            DrawDiffusedLine(draw_list, ImVec2(center.x, center.y + 7.0f),
                             ImVec2(center.x, center.y - 5.5f), col, t, is_bright);
            DrawDiffusedLine(draw_list, ImVec2(center.x - 5.5f, center.y - 0.5f),
                             ImVec2(center.x, center.y - 6.5f), col, t, is_bright);
            DrawDiffusedLine(draw_list, ImVec2(center.x + 5.5f, center.y - 0.5f),
                             ImVec2(center.x, center.y - 6.5f), col, t, is_bright);
            break;
        }
        case NavButtonIcon::Refresh: {
            // Circular arc + arrowhead
            constexpr float kR = 6.8f;
            constexpr float kStart = 0.35f;
            constexpr float kEnd = 5.15f;
            draw_list->PathClear();
            draw_list->PathArcTo(center, kR, kStart, kEnd, 18);
            draw_list->PathStroke(col, ImDrawFlags_None, 1.8f);
            const ImVec2 tip(center.x + std::cos(kEnd) * kR, center.y + std::sin(kEnd) * kR);
            DrawDiffusedTriangle(draw_list,
                                 ImVec2(tip.x + 3.5f, tip.y + 1.0f),
                                 ImVec2(tip.x - 2.5f, tip.y - 3.5f),
                                 ImVec2(tip.x - 2.0f, tip.y + 3.5f),
                                 col, is_bright);
            break;
        }
        case NavButtonIcon::ViewList: {
            for (int row = -1; row <= 1; ++row) {
                const float y = center.y + static_cast<float>(row) * 5.0f;
                draw_list->AddRectFilled(ImVec2(center.x - 8.0f, y - 1.2f),
                                         ImVec2(center.x - 5.5f, y + 1.2f), col);
                DrawDiffusedLine(draw_list, ImVec2(center.x - 3.0f, y),
                                 ImVec2(center.x + 8.0f, y), col, 1.6f, is_bright);
            }
            break;
        }
        case NavButtonIcon::ViewIcons: {
            const float s = 5.2f;
            const float g = 2.0f;
            for (int r = 0; r < 2; ++r) {
                for (int c = 0; c < 2; ++c) {
                    const float x0 = center.x - s - g * 0.5f + static_cast<float>(c) * (s + g);
                    const float y0 = center.y - s - g * 0.5f + static_cast<float>(r) * (s + g);
                    DrawDiffusedRectOutline(draw_list, ImVec2(x0, y0), ImVec2(x0 + s, y0 + s),
                                            1.0f, col, 1.4f, is_bright);
                }
            }
            break;
        }
    }
}

bool RenderHardwareIconButton(const char* str_id, const ImVec2& pos, const ImVec2& size,
                              NavButtonIcon icon, const char* tooltip,
                              bool is_active = false, bool enabled = true) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(str_id, size);
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool pressed = enabled && ImGui::IsItemActive();

    const ImVec2 socket_min(pos.x - 2.0f, pos.y - 2.0f);
    const ImVec2 socket_max(pos.x + size.x + 2.0f, pos.y + size.y + 2.0f);
    DrawHardwareButtonSocket(draw_list, socket_min, socket_max);

    const ImVec2 offset = pressed ? ImVec2(1.5f, 1.5f) : ImVec2(0.0f, 0.0f);
    const ImVec2 b_min(pos.x + offset.x, pos.y + offset.y);
    const ImVec2 b_max(pos.x + size.x + offset.x, pos.y + size.y + offset.y);
    DrawButtonBrushedFace(draw_list, b_min, b_max, hovered, pressed);

    const ImU32 icon_col = !enabled
        ? IM_COL32(90, 96, 106, 160)
        : ((is_active || pressed)
            ? kColCyanGlowCore
            : (hovered ? kColCyanBright : IM_COL32(110, 210, 235, 235)));
    const ImVec2 center((b_min.x + b_max.x) * 0.5f, (b_min.y + b_max.y) * 0.5f);
    DrawNavButtonIcon(draw_list, center, icon, icon_col, enabled && (is_active || hovered || pressed));

    if (tooltip && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return enabled && ImGui::IsItemClicked(ImGuiMouseButton_Left);
}

void DrawBrowserEntryIcon(ImDrawList* draw_list, const ImVec2& center,
                          bool is_parent, bool is_dir, bool is_image,
                          bool is_selected, bool hovered) {
    if (!draw_list) {
        return;
    }

    if (is_parent || is_dir) {
        const ImU32 fill_col = is_parent
            ? IM_COL32(36, 52, 66, 235)
            : IM_COL32(62, 50, 24, 235);
        const ImU32 rim_col = is_parent
            ? (is_selected ? kColCyanGlowCore : IM_COL32(100, 200, 235, 220))
            : (is_selected ? kColCyanGlowCore : (hovered ? IM_COL32(250, 205, 95, 240) : IM_COL32(230, 180, 72, 220)));

        // Folder tab + body
        const ImVec2 tab_min(center.x - 24.0f, center.y - 18.0f);
        const ImVec2 tab_max(center.x - 4.0f, center.y - 11.0f);
        draw_list->AddRectFilled(tab_min, tab_max, fill_col, 3.0f, ImDrawFlags_RoundCornersTop);
        draw_list->AddRect(tab_min, tab_max, rim_col, 3.0f, ImDrawFlags_RoundCornersTop, 1.4f);

        const ImVec2 body_min(center.x - 24.0f, center.y - 12.0f);
        const ImVec2 body_max(center.x + 24.0f, center.y + 18.0f);
        draw_list->AddRectFilled(body_min, body_max, fill_col, 4.0f);
        draw_list->AddRect(body_min, body_max, rim_col, 4.0f, 0, is_selected ? 2.0f : 1.5f);

        if (is_parent) {
            // Upward arrow inside parent folder
            DrawDiffusedLine(draw_list, ImVec2(center.x, center.y + 10.0f),
                             ImVec2(center.x, center.y - 3.0f), rim_col, 2.0f, is_selected);
            DrawDiffusedLine(draw_list, ImVec2(center.x - 6.0f, center.y + 2.0f),
                             ImVec2(center.x, center.y - 4.0f), rim_col, 2.0f, is_selected);
            DrawDiffusedLine(draw_list, ImVec2(center.x + 6.0f, center.y + 2.0f),
                             ImVec2(center.x, center.y - 4.0f), rim_col, 2.0f, is_selected);
        }
        return;
    }

    if (is_image) {
        // Optical disc icon for .cue / .toc images
        constexpr float kR = 22.0f;
        const ImU32 rim = is_selected ? kColCyanGlowCore : (hovered ? kColCyanBright : IM_COL32(0, 200, 235, 220));
        draw_list->AddCircleFilled(center, kR, IM_COL32(30, 48, 60, 240), 36);
        draw_list->AddLine(ImVec2(center.x - 13.0f, center.y - 12.0f),
                           ImVec2(center.x - 4.0f, center.y - 4.0f), IM_COL32(150, 240, 255, 65), 3.5f);
        draw_list->AddLine(ImVec2(center.x + 4.0f, center.y + 4.0f),
                           ImVec2(center.x + 13.0f, center.y + 12.0f), IM_COL32(150, 240, 255, 65), 3.5f);
        draw_list->AddCircle(center, kR, rim, 36, is_selected ? 2.0f : 1.6f);
        draw_list->AddCircle(center, kR * 0.65f, IM_COL32(0, 180, 215, 90), 28, 1.0f);
        draw_list->AddCircle(center, kR * 0.32f, IM_COL32(140, 220, 240, 170), 20, 1.1f);
        draw_list->AddCircleFilled(center, kR * 0.14f, kColSocketBg, 16);
        return;
    }

    // Generic file document icon with folded corner
    const ImVec2 doc_min(center.x - 16.0f, center.y - 21.0f);
    const ImVec2 doc_max(center.x + 16.0f, center.y + 21.0f);
    const ImU32 rim = is_selected ? kColCyanGlowCore : IM_COL32(130, 140, 155, 190);
    draw_list->AddRectFilled(doc_min, doc_max, IM_COL32(28, 32, 38, 225), 3.0f);
    draw_list->AddRect(doc_min, doc_max, rim, 3.0f, 0, is_selected ? 1.8f : 1.3f);
    draw_list->AddLine(ImVec2(doc_min.x + 6.0f, center.y - 6.0f),
                     ImVec2(doc_max.x - 6.0f, center.y - 6.0f), IM_COL32(120, 130, 145, 150), 1.4f);
    draw_list->AddLine(ImVec2(doc_min.x + 6.0f, center.y + 1.0f),
                     ImVec2(doc_max.x - 6.0f, center.y + 1.0f), IM_COL32(120, 130, 145, 150), 1.4f);
    draw_list->AddLine(ImVec2(doc_min.x + 6.0f, center.y + 8.0f),
                     ImVec2(doc_max.x - 10.0f, center.y + 8.0f), IM_COL32(120, 130, 145, 150), 1.4f);
}

void RenderImageFileScreen(CdDisplayModel& model, bool just_entered) {
    static FileBrowserState browser;
    static char path_buf[512] = "";
    static std::filesystem::path synced_dir;
    static bool parent_selected = false;

    if (synced_dir != browser.current_dir()) {
        synced_dir = browser.current_dir();
        std::snprintf(path_buf, sizeof(path_buf), "%s", synced_dir.string().c_str());
        parent_selected = false;
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

    const bool has_parent = browser.current_dir().has_parent_path() &&
        browser.current_dir().parent_path() != browser.current_dir();
    AppSettings settings = model.app_settings();
    const bool is_icon_view = (settings.file_browser_view == FileBrowserViewMode::Icons);

    // Navigation bar: Load Image (first on left), Home, Root, Up, Refresh, View Toggle, Show all files
    const float avail_w = ImGui::GetContentRegionAvail().x;
    const ImVec2 nav_pos = ImGui::GetCursorScreenPos();
    const bool can_load = !browser.selected_file().empty();
    constexpr float kLoadBtnW = 142.0f;
    constexpr float kIconBtnW = 36.0f;
    constexpr float kIconBtnH = 32.0f;
    constexpr float kIconGap = 6.0f;

    float cur_x = nav_pos.x;
    if (RenderHardwareTextButton("##BtnLoadImage", "Load Image",
                                 ImVec2(cur_x, nav_pos.y), ImVec2(kLoadBtnW, kIconBtnH),
                                 false, can_load)) {
        try_load_selected(browser.selected_file());
    }
    cur_x += kLoadBtnW + 12.0f;

    if (RenderHardwareIconButton("##ImgHome", ImVec2(cur_x, nav_pos.y),
                                 ImVec2(kIconBtnW, kIconBtnH), NavButtonIcon::Home, "Home directory")) {
        if (const char* home = std::getenv("HOME")) {
            browser.navigate_to(home);
            parent_selected = false;
        }
        ImGui::SetWindowFocus("##ImageBrowserList");
    }
    cur_x += kIconBtnW + kIconGap;

    if (RenderHardwareIconButton("##ImgRoot", ImVec2(cur_x, nav_pos.y),
                                 ImVec2(kIconBtnW, kIconBtnH), NavButtonIcon::Root, "Root (/)")) {
        browser.navigate_to("/");
        parent_selected = false;
        ImGui::SetWindowFocus("##ImageBrowserList");
    }
    cur_x += kIconBtnW + kIconGap;

    if (RenderHardwareIconButton("##ImgUp", ImVec2(cur_x, nav_pos.y),
                                 ImVec2(kIconBtnW, kIconBtnH), NavButtonIcon::Up,
                                 "Parent directory (..)", false, has_parent)) {
        browser.navigate_up();
        parent_selected = false;
        ImGui::SetWindowFocus("##ImageBrowserList");
    }
    cur_x += kIconBtnW + kIconGap;

    if (RenderHardwareIconButton("##ImgRefresh", ImVec2(cur_x, nav_pos.y),
                                 ImVec2(kIconBtnW, kIconBtnH), NavButtonIcon::Refresh, "Refresh")) {
        browser.refresh();
        parent_selected = false;
        ImGui::SetWindowFocus("##ImageBrowserList");
    }
    cur_x += kIconBtnW + 12.0f;

    const char* view_tip = is_icon_view
        ? "View: Icons (click for List)"
        : "View: List (click for Icons)";
    if (RenderHardwareIconButton("##ImgViewToggle", ImVec2(cur_x, nav_pos.y),
                                 ImVec2(kIconBtnW, kIconBtnH),
                                 is_icon_view ? NavButtonIcon::ViewIcons : NavButtonIcon::ViewList,
                                 view_tip, is_icon_view)) {
        settings.file_browser_view = is_icon_view ? FileBrowserViewMode::List : FileBrowserViewMode::Icons;
        model.set_app_settings(settings);
        save_app_settings(default_settings_path(), settings);
        ImGui::SetWindowFocus("##ImageBrowserList");
    }
    cur_x += kIconBtnW + 14.0f;

    ImGui::SetCursorScreenPos(ImVec2(cur_x, nav_pos.y + 5.0f));
    bool show_all = browser.show_all_files();
    if (ImGui::Checkbox("Show all files", &show_all)) {
        browser.set_show_all_files(show_all);
        parent_selected = false;
        ImGui::SetWindowFocus("##ImageBrowserList");
    }

    ImGui::SetCursorScreenPos(ImVec2(nav_pos.x, nav_pos.y + 42.0f));

    // Path input bar
    ImGui::SetNextItemWidth(std::max(180.0f, avail_w - 86.0f));
    if (ImGui::InputText("##BrowserPath", path_buf, sizeof(path_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
        browser.navigate_to(std::filesystem::path(path_buf));
        parent_selected = false;
        ImGui::SetWindowFocus("##ImageBrowserList");
    }
    ImGui::SameLine();
    if (ImGui::Button("Go", ImVec2(72.0f, 0.0f))) {
        browser.navigate_to(std::filesystem::path(path_buf));
        parent_selected = false;
        ImGui::SetWindowFocus("##ImageBrowserList");
    }

    // Active substring filter bar (shown when typing in the file browser)
    if (!browser.filter_query().empty()) {
        ImGui::TextColored(ImVec4(0.0f, 0.90f, 1.0f, 1.0f), "Filter: \"%s\"",
                           browser.filter_query().c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            browser.clear_filter_query();
            parent_selected = false;
            ImGui::SetWindowFocus("##ImageBrowserList");
        }
    } else {
        ImGui::Spacing();
    }

    // Directory & file list / icon grid (larger font + full keyboard navigation)
    const float list_height = std::max(160.0f, ImGui::GetContentRegionAvail().y - 34.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.07f, 0.08f, 0.90f));
    ImGui::BeginChild("##ImageBrowserList", ImVec2(0.0f, list_height), true,
                      ImGuiWindowFlags_NoNavInputs);
    {
        if (just_entered) {
            ImGui::SetWindowFocus();
        }

        constexpr float kCardW = 148.0f;
        constexpr float kCardH = 126.0f;
        constexpr float kCardGap = 12.0f;
        constexpr float kGridPad = 12.0f;
        const int icon_cols = std::max(
            1,
            static_cast<int>((ImGui::GetContentRegionAvail().x - 2.0f * kGridPad + kCardGap) / (kCardW + kCardGap))
        );

        const auto& entries = browser.entries();
        ImGuiIO& io = ImGui::GetIO();
        bool scroll_to_selection = false;
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !io.WantTextInput) {
            auto move_step = [&](int step) {
                if (entries.empty()) {
                    if (has_parent) {
                        parent_selected = true;
                    }
                    return;
                }
                if (parent_selected) {
                    if (step > 0) {
                        parent_selected = false;
                        const int target = std::clamp(step - 1, 0, static_cast<int>(entries.size()) - 1);
                        browser.select_entry(static_cast<std::size_t>(target));
                    }
                } else {
                    const int cur = browser.selected_index().has_value()
                        ? static_cast<int>(*browser.selected_index())
                        : 0;
                    if (cur + step < 0 && has_parent) {
                        parent_selected = true;
                    } else {
                        browser.move_selection(step);
                    }
                }
                scroll_to_selection = true;
            };

            if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                browser.navigate_up();
                parent_selected = false;
                scroll_to_selection = true;
            } else if (is_icon_view && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
                move_step(-1);
            } else if (is_icon_view && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
                move_step(1);
            } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                move_step(is_icon_view ? -icon_cols : -1);
            } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                move_step(is_icon_view ? icon_cols : 1);
            } else if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
                move_step(is_icon_view ? -(icon_cols * 3) : -10);
            } else if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
                move_step(is_icon_view ? (icon_cols * 3) : 10);
            } else if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
                if (parent_selected) {
                    browser.navigate_up();
                    parent_selected = false;
                } else if (browser.selected_index().has_value()) {
                    if (browser.activate_entry(*browser.selected_index())) {
                        try_load_selected(browser.selected_file());
                    }
                    parent_selected = false;
                }
                scroll_to_selection = true;
            } else if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
                if (!browser.filter_query().empty()) {
                    std::string q = browser.filter_query();
                    q.pop_back();
                    browser.set_filter_query(q);
                } else {
                    browser.navigate_up();
                }
                parent_selected = false;
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
                    parent_selected = false;
                    scroll_to_selection = true;
                }
            }
        }

        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.38f);
        if (entries.empty() && !has_parent) {
            ImGui::TextDisabled(browser.filter_query().empty()
                                    ? "(No matching .cue or .toc files in this directory)"
                                    : "(No entries match current filter)");
        } else if (is_icon_view) {
            const ImVec2 grid_origin = ImGui::GetCursorPos();
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImFont* font = ImGui::GetFont();
            const float font_sz = ImGui::GetFontSize();
            const int total_items = (has_parent ? 1 : 0) + static_cast<int>(entries.size());

            for (int item_idx = 0; item_idx < total_items; ++item_idx) {
                const bool is_parent_item = has_parent && (item_idx == 0);
                const std::size_t entry_idx = is_parent_item
                    ? 0
                    : static_cast<std::size_t>(item_idx - (has_parent ? 1 : 0));
                const int col = item_idx % icon_cols;
                const int row = item_idx / icon_cols;

                ImGui::SetCursorPos(ImVec2(
                    grid_origin.x + kGridPad + static_cast<float>(col) * (kCardW + kCardGap),
                    grid_origin.y + kGridPad + static_cast<float>(row) * (kCardH + kCardGap)
                ));
                const ImVec2 card_min = ImGui::GetCursorScreenPos();
                const ImVec2 card_max(card_min.x + kCardW, card_min.y + kCardH);

                const bool is_selected = is_parent_item
                    ? parent_selected
                    : (!parent_selected && browser.selected_index().has_value() && (*browser.selected_index() == entry_idx));

                ImGui::PushID(item_idx);
                ImGui::InvisibleButton("##BrowserIconCard", ImVec2(kCardW, kCardH));
                const bool hovered = ImGui::IsItemHovered();
                const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                const bool double_clicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                ImGui::PopID();

                if (clicked) {
                    ImGui::SetWindowFocus("##ImageBrowserList");
                    if (is_parent_item) {
                        parent_selected = true;
                    } else {
                        parent_selected = false;
                        browser.select_entry(entry_idx);
                    }
                }
                if (double_clicked) {
                    if (is_parent_item) {
                        browser.navigate_up();
                        parent_selected = false;
                    } else if (browser.activate_entry(entry_idx)) {
                        try_load_selected(browser.selected_file());
                    }
                }
                if (is_selected && scroll_to_selection) {
                    ImGui::SetScrollHereY(0.5f);
                }

                const ImU32 card_bg = is_selected
                    ? IM_COL32(20, 30, 38, 240)
                    : (hovered ? IM_COL32(24, 28, 34, 230) : IM_COL32(16, 18, 22, 210));
                draw_list->AddRectFilled(card_min, card_max, card_bg, 6.0f);
                if (is_selected) {
                    DrawDiffusedRectOutline(draw_list, card_min, card_max, 6.0f, kColCyanBright, 1.5f, true);
                } else {
                    draw_list->AddRect(card_min, card_max,
                                       hovered ? IM_COL32(95, 115, 135, 180) : IM_COL32(46, 50, 58, 150),
                                       6.0f, 0, 1.0f);
                }

                const bool is_dir = is_parent_item || entries[entry_idx].isDirectory;
                const bool is_img = !is_parent_item && !is_dir &&
                    FileBrowserState::is_supported_image_extension(entries[entry_idx].path);

                DrawBrowserEntryIcon(draw_list,
                                     ImVec2(card_min.x + kCardW * 0.5f, card_min.y + 42.0f),
                                     is_parent_item, is_dir, is_img, is_selected, hovered);

                const std::string& label = is_parent_item ? std::string("..") : entries[entry_idx].name;
                const ImU32 name_col = is_selected
                    ? kColCyanGlowCore
                    : (is_img ? IM_COL32(140, 235, 255, 255) : IM_COL32(224, 230, 240, 255));
                DrawCenteredText(draw_list, font, font_sz,
                                 card_min.x + 8.0f, kCardW - 16.0f, card_min.y + 78.0f,
                                 name_col, label);

                const std::string sub = is_parent_item
                    ? "Parent Dir"
                    : (is_dir ? "Folder" : FormatFileSize(entries[entry_idx].fileSize));
                DrawCenteredText(draw_list, font, font_sz * 0.78f,
                                 card_min.x + 8.0f, kCardW - 16.0f, card_min.y + 101.0f,
                                 IM_COL32(140, 150, 165, 215), sub);

                if (hovered) {
                    ImGui::SetTooltip("%s", label.c_str());
                }
            }
        } else if (ImGui::BeginTable("##FileBrowserTable", 3,
                                     ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 76.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 114.0f);
            ImGui::TableHeadersRow();

            if (has_parent) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), "[DIR]");

                ImGui::TableSetColumnIndex(1);
                if (ImGui::Selectable("..", parent_selected,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    parent_selected = true;
                    ImGui::SetWindowFocus("##ImageBrowserList");
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        browser.navigate_up();
                        parent_selected = false;
                    }
                }
                if (parent_selected && scroll_to_selection) {
                    ImGui::SetScrollHereY(0.5f);
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("--");
            }

            for (std::size_t i = 0; i < entries.size(); ++i) {
                const auto& entry = entries[i];
                const bool is_selected = !parent_selected &&
                    browser.selected_index().has_value() && (*browser.selected_index() == i);

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
                    parent_selected = false;
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

    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.25f);
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
    ImGui::PopFont();
}

void RenderDiskInfoScreen(CdDisplayModel& model) {
    static DiskInfoState info_state;

    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.25f);
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
        draw_list->AddText(ImVec2(art_min.x + 12.0f, art_max.y - 28.0f),
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
    ImGui::PopFont();
}

void RenderSettingsScreen(CdDisplayModel& model) {
    static std::string status_msg;

    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.25f);
    ImGui::BeginChild("##SettingsScroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_NoBackground);
    {
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

    const char* browser_view_items[] = {"List", "Icons"};
    int browser_view_idx = (settings.file_browser_view == FileBrowserViewMode::Icons) ? 1 : 0;
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::Combo("Image/File browser view", &browser_view_idx, browser_view_items, 2)) {
        settings.file_browser_view = (browser_view_idx == 1)
            ? FileBrowserViewMode::Icons
            : FileBrowserViewMode::List;
        changed = true;
    }
    ImGui::TextDisabled("    Default display mode (List or Icons) for the Image/File browser.");
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
    ImGui::EndChild();
    ImGui::PopFont();
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
