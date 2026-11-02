#include "cd_playlist_widget.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace mycds::gui {

namespace {

constexpr ImU32 kAmberLitCore = IM_COL32(255, 175, 20, 255);
constexpr ImU32 kAmberLitGlow = IM_COL32(255, 130, 0, 75);
constexpr ImU32 kAmberMuted   = IM_COL32(180, 120, 45, 200);

constexpr ImU32 kGreenLitCore = IM_COL32(65, 255, 115, 255);
constexpr ImU32 kGreenLitGlow = IM_COL32(35, 210, 75, 75);

constexpr ImU32 kGlassBg      = IM_COL32(10, 8, 7, 255);
constexpr ImU32 kBezelDark    = IM_COL32(18, 18, 20, 255);
constexpr ImU32 kBezelLight   = IM_COL32(50, 50, 54, 255);
constexpr ImU32 kChassisFrame = IM_COL32(28, 28, 32, 255);

} // namespace

void RenderCdPlaylist(CdDisplayModel& model, const ImVec2& user_size) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const float widget_width = (user_size.x >= 680.0f) ? user_size.x : 680.0f;
    const float widget_height = (user_size.y > 0.0f) ? user_size.y : 380.0f;

    const ImVec2 screen_pos = ImGui::GetCursorScreenPos();
    const ImVec2 display_end(screen_pos.x + widget_width, screen_pos.y + widget_height);

    // Reserve layout space in ImGui
    ImGui::Dummy(ImVec2(widget_width, widget_height));

    // Outer Chassis Frame
    draw_list->AddRectFilled(screen_pos, display_end, kChassisFrame, 10.0f);
    draw_list->AddRect(screen_pos, display_end, kBezelLight, 10.0f, 0, 1.5f);

    // Corner screw accents
    auto draw_screw = [&](ImVec2 pos) {
        draw_list->AddCircleFilled(pos, 3.5f, IM_COL32(40, 40, 44, 255));
        draw_list->AddCircle(pos, 3.5f, IM_COL32(70, 70, 76, 255), 12, 1.0f);
        draw_list->AddLine(ImVec2(pos.x - 2.0f, pos.y), ImVec2(pos.x + 2.0f, pos.y), IM_COL32(20, 20, 22, 255), 1.0f);
    };
    draw_screw(ImVec2(screen_pos.x + 12.0f, screen_pos.y + 12.0f));
    draw_screw(ImVec2(display_end.x - 12.0f, screen_pos.y + 12.0f));
    draw_screw(ImVec2(screen_pos.x + 12.0f, display_end.y - 12.0f));
    draw_screw(ImVec2(display_end.x - 12.0f, display_end.y - 12.0f));

    // Inner Smoked Glass Window
    const ImVec2 glass_min(screen_pos.x + 24.0f, screen_pos.y + 20.0f);
    const ImVec2 glass_max(display_end.x - 24.0f, display_end.y - 20.0f);
    const float glass_w = glass_max.x - glass_min.x;
    const float glass_h = glass_max.y - glass_min.y;

    draw_list->AddRectFilled(glass_min, glass_max, kGlassBg, 6.0f);
    draw_list->AddRect(glass_min, glass_max, kBezelDark, 6.0f, 0, 2.5f);
    draw_list->AddLine(ImVec2(glass_min.x + 1.0f, glass_max.y),
                       ImVec2(glass_max.x - 1.0f, glass_max.y),
                       IM_COL32(60, 60, 65, 180), 1.0f);

    // Glass glare reflection
    draw_list->AddRectFilledMultiColor(
        glass_min,
        ImVec2(glass_max.x, glass_min.y + glass_h * 0.35f),
        IM_COL32(255, 255, 255, 14),
        IM_COL32(255, 255, 255, 8),
        IM_COL32(255, 255, 255, 0),
        IM_COL32(255, 255, 255, 0)
    );

    // Header Row
    const float header_y = glass_min.y + 10.0f;
    draw_list->AddText(ImVec2(glass_min.x + 16.0f, header_y),
                       IM_COL32(160, 110, 50, 180), "COMPACT DISC DIGITAL AUDIO");

    draw_list->AddText(ImVec2(glass_min.x + 250.0f, header_y),
                       kAmberLitCore, "TRACK PLAYLIST");

    // Total tracks and duration summary
    int total_duration = 0;
    for (const auto& trk : model.tracks()) {
        total_duration += trk.duration_seconds;
    }
    const int total_min = total_duration / 60;
    const int total_sec = total_duration % 60;
    char disc_info_buf[32];
    std::snprintf(disc_info_buf, sizeof(disc_info_buf), "%d TRACKS  [%02d:%02d]",
                  model.total_tracks(), total_min, total_sec);
    const float info_w = ImGui::CalcTextSize(disc_info_buf).x;
    draw_list->AddText(ImVec2(glass_max.x - info_w - 16.0f, header_y),
                       kAmberMuted, disc_info_buf);

    // Column Headers
    const float col_header_y = header_y + 22.0f;
    draw_list->AddLine(ImVec2(glass_min.x + 12.0f, col_header_y - 2.0f),
                       ImVec2(glass_max.x - 12.0f, col_header_y - 2.0f),
                       IM_COL32(50, 40, 30, 200), 1.0f);

    const float col_track_x = glass_min.x + 20.0f;
    const float col_title_x = glass_min.x + 85.0f;
    const float col_time_x  = glass_max.x - 85.0f;

    draw_list->AddText(ImVec2(col_track_x, col_header_y), kAmberMuted, "TRK");
    draw_list->AddText(ImVec2(col_title_x, col_header_y), kAmberMuted, "TITLE");
    draw_list->AddText(ImVec2(col_time_x, col_header_y),  kAmberMuted, "TIME");

    draw_list->AddLine(ImVec2(glass_min.x + 12.0f, col_header_y + 18.0f),
                       ImVec2(glass_max.x - 12.0f, col_header_y + 18.0f),
                       IM_COL32(60, 48, 34, 255), 1.0f);

    // Child Window for Scrollable Sparse Track List
    const float list_start_y = col_header_y + 24.0f;
    const float list_h = glass_max.y - list_start_y - 8.0f;
    const float list_w = glass_w - 24.0f;

    ImGui::SetCursorScreenPos(ImVec2(glass_min.x + 12.0f, list_start_y));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.04f, 0.04f, 0.05f, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.6f, 0.35f, 0.08f, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.9f, 0.55f, 0.12f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(1.0f, 0.65f, 0.15f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 3.0f));

    ImGui::BeginChild("TrackListChild", ImVec2(list_w, list_h), false,
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysVerticalScrollbar);

    ImDrawList* child_draw_list = ImGui::GetWindowDrawList();
    const int current_track_num = model.current_track_number();

    for (const auto& track : model.tracks()) {
        const bool is_active = (track.number == current_track_num);
        const float item_h = 24.0f;
        const ImVec2 row_pos = ImGui::GetCursorScreenPos();
        const ImVec2 row_size(list_w - 18.0f, item_h);
        const ImVec2 row_end(row_pos.x + row_size.x, row_pos.y + row_size.y);

        // Invisible button for row interaction / clicking
        char btn_id[32];
        std::snprintf(btn_id, sizeof(btn_id), "TrackRow_%d", track.number);
        if (ImGui::InvisibleButton(btn_id, row_size)) {
            model.jump_to_track(track.number);
        }
        const bool hovered = ImGui::IsItemHovered();

        // Row background rendering
        if (is_active) {
            child_draw_list->AddRectFilled(row_pos, row_end, IM_COL32(30, 160, 65, 45), 4.0f);
            child_draw_list->AddRect(row_pos, row_end, IM_COL32(50, 220, 95, 120), 4.0f, 0, 1.2f);
        } else if (hovered) {
            child_draw_list->AddRectFilled(row_pos, row_end, IM_COL32(255, 175, 20, 30), 4.0f);
        }

        const ImU32 text_col = is_active ? kGreenLitCore : kAmberLitCore;
        const float text_y = row_pos.y + 4.0f;

        // Active Track Arrow indicator
        if (is_active) {
            const ImVec2 t1(row_pos.x + 4.0f, text_y + 2.0f);
            const ImVec2 t2(row_pos.x + 11.0f, text_y + 7.0f);
            const ImVec2 t3(row_pos.x + 4.0f, text_y + 12.0f);
            child_draw_list->AddTriangleFilled(t1, t2, t3, kGreenLitCore);
            child_draw_list->AddTriangleFilled(t1, t2, t3, kGreenLitGlow);
        }

        // Track number (e.g. "01")
        char num_buf[8];
        std::snprintf(num_buf, sizeof(num_buf), "%02d", track.number);
        child_draw_list->AddText(ImVec2(row_pos.x + 18.0f, text_y), text_col, num_buf);

        // Track title
        child_draw_list->AddText(ImVec2(row_pos.x + 75.0f, text_y), text_col, track.title.c_str());

        // Track duration
        char time_buf[16];
        const int m = track.duration_seconds / 60;
        const int s = track.duration_seconds % 60;
        std::snprintf(time_buf, sizeof(time_buf), "%02d:%02d", m, s);
        const float time_w = ImGui::CalcTextSize(time_buf).x;
        child_draw_list->AddText(ImVec2(row_end.x - time_w - 6.0f, text_y), text_col, time_buf);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(5);

    // Restore cursor position below the widget and submit Dummy to define parent boundary
    ImGui::SetCursorScreenPos(ImVec2(screen_pos.x, display_end.y + 14.0f));
    ImGui::Dummy(ImVec2(widget_width, 0.0f));
}

} // namespace mycds::gui
