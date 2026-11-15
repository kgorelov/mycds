#include "cd_cover_widget.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "cd_brushed_metal.h"
#include "cd_ui_style.h"

namespace mycds::gui {

void RenderCdCoverWidget(CdDisplayModel& model, const ImVec2& size) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    const float panel_w = (size.x > 0.0f) ? size.x : 680.0f;
    const float panel_h = (size.y > 0.0f) ? size.y : 180.0f;

    const ImVec2 panel_min = cursor_pos;
    const ImVec2 panel_max(cursor_pos.x + panel_w, cursor_pos.y + panel_h);

    GetBrushedMetal().RenderPanelChassis(draw_list, panel_min, panel_max, 8.0f);
    DrawChassisScrew(draw_list, ImVec2(panel_min.x + 10.0f, panel_min.y + 10.0f));
    DrawChassisScrew(draw_list, ImVec2(panel_max.x - 10.0f, panel_min.y + 10.0f));
    DrawChassisScrew(draw_list, ImVec2(panel_min.x + 10.0f, panel_max.y - 10.0f));
    DrawChassisScrew(draw_list, ImVec2(panel_max.x - 10.0f, panel_max.y - 10.0f));

    // Reserve a compact 2-line footer at the bottom for album info + Disk Info shortcut
    constexpr float kPadX = 20.0f;
    constexpr float kPadTop = 16.0f;
    constexpr float kPadBottom = 14.0f;
    constexpr float kFooterH = 40.0f;
    constexpr float kGap = 8.0f;

    const float art_w = std::max(120.0f, panel_w - 2.0f * kPadX);
    const float art_h = std::max(60.0f, panel_h - kPadTop - kPadBottom - kFooterH - kGap);
    const ImVec2 art_min(panel_min.x + kPadX, panel_min.y + kPadTop);
    const ImVec2 art_max(art_min.x + art_w, art_min.y + art_h);

    draw_list->AddRectFilled(art_min, art_max, IM_COL32(12, 14, 18, 255), 4.0f);
    draw_list->AddRect(art_min, art_max, IM_COL32(0, 180, 220, 140), 4.0f, 0, 1.4f);

    const float disc_r = std::max(22.0f, std::min(art_w, art_h) * 0.36f);
    const ImVec2 disc_center(art_min.x + art_w * 0.5f, art_min.y + art_h * 0.5f);
    draw_list->AddCircle(disc_center, disc_r, IM_COL32(0, 210, 240, 150), 48, 1.8f);
    draw_list->AddCircle(disc_center, disc_r * 0.65f, IM_COL32(0, 160, 195, 75), 36, 1.0f);
    draw_list->AddCircle(disc_center, disc_r * 0.28f, IM_COL32(0, 210, 240, 120), 28, 1.2f);
    draw_list->AddCircleFilled(disc_center, disc_r * 0.13f, IM_COL32(12, 14, 18, 255));

    // Compact 2-line album info footer underneath the cover
    const float footer_y = art_max.y + kGap;
    ImGui::SetCursorScreenPos(ImVec2(art_min.x, footer_y));
    ImGui::BeginChild("##CdCoverFooter", ImVec2(art_w, kFooterH), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    {
        ImGui::TextColored(ImVec4(0.0f, 0.90f, 1.0f, 1.0f), "%s", model.album_title().c_str());
        ImGui::TextColored(ImVec4(0.75f, 0.79f, 0.85f, 1.0f), "%s  |  %d tracks",
                           model.artist_name().c_str(), model.total_tracks());

        ImGui::SetCursorPos(ImVec2(std::max(0.0f, art_w - 118.0f), 8.0f));
        if (ImGui::SmallButton("Disk Info...")) {
            model.set_active_eject_screen(EjectMenuScreen::DiskInfo);
            model.open_eject_menu();
        }
    }
    ImGui::EndChild();

    ImGui::SetCursorScreenPos(panel_min);
    ImGui::Dummy(ImVec2(panel_w, panel_h));
}

} // namespace mycds::gui
