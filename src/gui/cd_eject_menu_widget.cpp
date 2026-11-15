#include "cd_eject_menu_widget.h"

#include <algorithm>

#include "cd_brushed_metal.h"
#include "cd_ui_style.h"

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

void RenderCdLoadScreen(CdDisplayModel& /*model*/) {
    ImGui::TextColored(ImVec4(0.75f, 0.78f, 0.84f, 1.0f),
                       "Select an optical CD drive present in the system to inspect or load.");
    ImGui::Spacing();
    ImGui::TextDisabled("Physical CD drive enumeration and loading will be wired in Phase 2.");
}

void RenderImageFileScreen(CdDisplayModel& /*model*/) {
    ImGui::TextColored(ImVec4(0.75f, 0.78f, 0.84f, 1.0f),
                       "Browse the file system and choose a .cue or .toc disc image to load.");
    ImGui::Spacing();
    ImGui::TextDisabled("In-app file browser and ImageSource loading will be wired in Phase 2.");
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
