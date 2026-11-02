#include "cd_display_widget.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace mycds::gui {

namespace {

// Palette constants
constexpr ImU32 kAmberLitCore = IM_COL32(255, 175, 20, 255);
constexpr ImU32 kAmberLitGlow = IM_COL32(255, 130, 0, 75);
constexpr ImU32 kAmberUnlit   = IM_COL32(46, 22, 6, 210);

constexpr ImU32 kGreenLitCore = IM_COL32(65, 255, 115, 255);
constexpr ImU32 kGreenLitGlow = IM_COL32(35, 210, 75, 75);
constexpr ImU32 kGreenUnlit   = IM_COL32(14, 42, 18, 220);

constexpr ImU32 kRedLitCore   = IM_COL32(255, 60, 50, 255);
constexpr ImU32 kRedLitGlow   = IM_COL32(230, 40, 30, 85);

constexpr ImU32 kGlassBg      = IM_COL32(10, 8, 7, 255);
constexpr ImU32 kBezelDark    = IM_COL32(18, 18, 20, 255);
constexpr ImU32 kBezelLight   = IM_COL32(50, 50, 54, 255);
constexpr ImU32 kChassisFrame = IM_COL32(28, 28, 32, 255);

// 7-segment bitmasks (Bits: 0:A, 1:B, 2:C, 3:D, 4:E, 5:F, 6:G)
uint8_t GetSegmentMask(char c) {
    switch (c) {
        case '0': return 0x3F; // A, B, C, D, E, F
        case '1': return 0x06; // B, C
        case '2': return 0x5B; // A, B, D, E, G
        case '3': return 0x4F; // A, B, C, D, G
        case '4': return 0x66; // B, C, F, G
        case '5': return 0x6D; // A, C, D, F, G
        case '6': return 0x7D; // A, C, D, E, F, G
        case '7': return 0x07; // A, B, C
        case '8': return 0x7F; // A, B, C, D, E, F, G
        case '9': return 0x6F; // A, B, C, D, F, G
        case '-': return 0x40; // G
        default:  return 0x00;
    }
}

void DrawSlantedPoly(ImDrawList* draw_list, const ImVec2* pts, int count,
                     ImU32 fill_col, ImU32 glow_col, bool is_lit) {
    if (is_lit) {
        draw_list->AddPolyline(pts, count, glow_col, ImDrawFlags_Closed, 3.5f);
        draw_list->AddConvexPolyFilled(pts, count, fill_col);
    } else {
        draw_list->AddConvexPolyFilled(pts, count, fill_col);
    }
}

void DrawSevenSegmentDigit(ImDrawList* draw_list, ImVec2 origin, ImVec2 size,
                           char c, float slant_factor = 0.08f) {
    const uint8_t mask = GetSegmentMask(c);
    const float w = size.x;
    const float h = size.y;
    const float t = w * 0.15f;
    const float g = w * 0.04f;
    const float b = t * 0.5f;
    const float ym = h * 0.5f;

    auto slant = [&](float lx, float ly) -> ImVec2 {
        return ImVec2(origin.x + lx + (h - ly) * slant_factor, origin.y + ly);
    };

    // Segment A (Top)
    {
        const bool lit = (mask & 0x01) != 0;
        const ImVec2 pts[4] = {
            slant(g + b, g),
            slant(w - g - b, g),
            slant(w - g - t, g + t),
            slant(g + t, g + t)
        };
        DrawSlantedPoly(draw_list, pts, 4,
                        lit ? kAmberLitCore : kAmberUnlit,
                        kAmberLitGlow, lit);
    }

    // Segment B (Top-Right)
    {
        const bool lit = (mask & 0x02) != 0;
        const ImVec2 pts[4] = {
            slant(w - g - t, g + t + b),
            slant(w - g, g + b),
            slant(w - g, ym - g * 0.5f - b),
            slant(w - g - t, ym - g * 0.5f - t * 0.5f)
        };
        DrawSlantedPoly(draw_list, pts, 4,
                        lit ? kAmberLitCore : kAmberUnlit,
                        kAmberLitGlow, lit);
    }

    // Segment C (Bottom-Right)
    {
        const bool lit = (mask & 0x04) != 0;
        const ImVec2 pts[4] = {
            slant(w - g - t, ym + g * 0.5f + t * 0.5f),
            slant(w - g, ym + g * 0.5f + b),
            slant(w - g, h - g - b),
            slant(w - g - t, h - g - t - b)
        };
        DrawSlantedPoly(draw_list, pts, 4,
                        lit ? kAmberLitCore : kAmberUnlit,
                        kAmberLitGlow, lit);
    }

    // Segment D (Bottom)
    {
        const bool lit = (mask & 0x08) != 0;
        const ImVec2 pts[4] = {
            slant(g + t, h - g - t),
            slant(w - g - t, h - g - t),
            slant(w - g - b, h - g),
            slant(g + b, h - g)
        };
        DrawSlantedPoly(draw_list, pts, 4,
                        lit ? kAmberLitCore : kAmberUnlit,
                        kAmberLitGlow, lit);
    }

    // Segment E (Bottom-Left)
    {
        const bool lit = (mask & 0x10) != 0;
        const ImVec2 pts[4] = {
            slant(g, ym + g * 0.5f + b),
            slant(g + t, ym + g * 0.5f + t * 0.5f),
            slant(g + t, h - g - t - b),
            slant(g, h - g - b)
        };
        DrawSlantedPoly(draw_list, pts, 4,
                        lit ? kAmberLitCore : kAmberUnlit,
                        kAmberLitGlow, lit);
    }

    // Segment F (Top-Left)
    {
        const bool lit = (mask & 0x20) != 0;
        const ImVec2 pts[4] = {
            slant(g, g + b),
            slant(g + t, g + t + b),
            slant(g + t, ym - g * 0.5f - t * 0.5f),
            slant(g, ym - g * 0.5f - b)
        };
        DrawSlantedPoly(draw_list, pts, 4,
                        lit ? kAmberLitCore : kAmberUnlit,
                        kAmberLitGlow, lit);
    }

    // Segment G (Middle)
    {
        const bool lit = (mask & 0x40) != 0;
        const ImVec2 pts[6] = {
            slant(g + b, ym),
            slant(g + t, ym - t * 0.5f),
            slant(w - g - t, ym - t * 0.5f),
            slant(w - g - b, ym),
            slant(w - g - t, ym + t * 0.5f),
            slant(g + t, ym + t * 0.5f)
        };
        DrawSlantedPoly(draw_list, pts, 6,
                        lit ? kAmberLitCore : kAmberUnlit,
                        kAmberLitGlow, lit);
    }
}

void DrawColon(ImDrawList* draw_list, ImVec2 origin, ImVec2 size, bool lit,
               float slant_factor = 0.08f) {
    const float w = size.x;
    const float h = size.y;
    const float r = w * 0.22f;
    const float ym = h * 0.5f;

    auto slant = [&](float lx, float ly) -> ImVec2 {
        return ImVec2(origin.x + lx + (h - ly) * slant_factor, origin.y + ly);
    };

    const ImVec2 c1 = slant(w * 0.5f, ym - h * 0.18f);
    const ImVec2 c2 = slant(w * 0.5f, ym + h * 0.18f);

    if (lit) {
        draw_list->AddCircleFilled(c1, r + 2.0f, kAmberLitGlow);
        draw_list->AddCircleFilled(c2, r + 2.0f, kAmberLitGlow);
        draw_list->AddCircleFilled(c1, r, kAmberLitCore);
        draw_list->AddCircleFilled(c2, r, kAmberLitCore);
    } else {
        draw_list->AddCircleFilled(c1, r, kAmberUnlit);
        draw_list->AddCircleFilled(c2, r, kAmberUnlit);
    }
}

// Indicator icon renderers
void DrawRepeatGraphic(ImDrawList* draw_list, ImVec2 center, float size, ImU32 col, ImU32 glow_col, bool lit) {
    const float r = size * 0.42f;
    const float th = 2.0f;

    if (lit) {
        draw_list->AddCircle(center, r, glow_col, 20, th + 2.0f);
    }
    draw_list->AddCircle(center, r, col, 20, th);

    // Arrowhead top-right
    const ImVec2 a1(center.x + r - 3.0f, center.y - 4.0f);
    const ImVec2 a2(center.x + r + 3.0f, center.y);
    const ImVec2 a3(center.x + r - 3.0f, center.y + 4.0f);
    draw_list->AddTriangleFilled(a1, a2, a3, col);

    // Arrowhead bottom-left
    const ImVec2 b1(center.x - r + 3.0f, center.y + 4.0f);
    const ImVec2 b2(center.x - r - 3.0f, center.y);
    const ImVec2 b3(center.x - r + 3.0f, center.y - 4.0f);
    draw_list->AddTriangleFilled(b1, b2, b3, col);
}

void DrawShuffleGraphic(ImDrawList* draw_list, ImVec2 origin, float size, ImU32 col, ImU32 glow_col, bool lit) {
    const float w = size * 1.1f;
    const float h = size * 0.75f;
    const float th = 2.0f;

    // Line 1: top-left to bottom-right
    const ImVec2 p1_0(origin.x, origin.y + 2.0f);
    const ImVec2 p1_1(origin.x + w * 0.35f, origin.y + 2.0f);
    const ImVec2 p1_2(origin.x + w * 0.65f, origin.y + h - 2.0f);
    const ImVec2 p1_3(origin.x + w - 4.0f, origin.y + h - 2.0f);

    // Line 2: bottom-left to top-right
    const ImVec2 p2_0(origin.x, origin.y + h - 2.0f);
    const ImVec2 p2_1(origin.x + w * 0.35f, origin.y + h - 2.0f);
    const ImVec2 p2_2(origin.x + w * 0.65f, origin.y + 2.0f);
    const ImVec2 p2_3(origin.x + w - 4.0f, origin.y + 2.0f);

    if (lit) {
        draw_list->AddLine(p1_0, p1_1, glow_col, th + 2.0f);
        draw_list->AddLine(p1_1, p1_2, glow_col, th + 2.0f);
        draw_list->AddLine(p1_2, p1_3, glow_col, th + 2.0f);

        draw_list->AddLine(p2_0, p2_1, glow_col, th + 2.0f);
        draw_list->AddLine(p2_1, p2_2, glow_col, th + 2.0f);
        draw_list->AddLine(p2_2, p2_3, glow_col, th + 2.0f);
    }

    draw_list->AddLine(p1_0, p1_1, col, th);
    draw_list->AddLine(p1_1, p1_2, col, th);
    draw_list->AddLine(p1_2, p1_3, col, th);

    draw_list->AddLine(p2_0, p2_1, col, th);
    draw_list->AddLine(p2_1, p2_2, col, th);
    draw_list->AddLine(p2_2, p2_3, col, th);

    // Top arrow tip
    draw_list->AddTriangleFilled(ImVec2(origin.x + w - 6.0f, origin.y - 2.0f),
                                 ImVec2(origin.x + w, origin.y + 2.0f),
                                 ImVec2(origin.x + w - 6.0f, origin.y + 6.0f), col);

    // Bottom arrow tip
    draw_list->AddTriangleFilled(ImVec2(origin.x + w - 6.0f, origin.y + h - 6.0f),
                                 ImVec2(origin.x + w, origin.y + h - 2.0f),
                                 ImVec2(origin.x + w - 6.0f, origin.y + h + 2.0f), col);
}

void DrawCloudGraphic(ImDrawList* draw_list, ImVec2 center, float size, ImU32 col, ImU32 glow_col, bool lit) {
    const float y_base = center.y + size * 0.28f;
    const float r_left = size * 0.22f;
    const float r_mid = size * 0.35f;
    const float r_right = size * 0.24f;

    const ImVec2 c_left(center.x - size * 0.28f, y_base - r_left);
    const ImVec2 c_mid(center.x, y_base - r_mid * 1.15f);
    const ImVec2 c_right(center.x + size * 0.28f, y_base - r_right);

    if (lit) {
        draw_list->AddCircleFilled(c_mid, r_mid + 2.0f, glow_col);
        draw_list->AddCircleFilled(c_left, r_left + 2.0f, glow_col);
        draw_list->AddCircleFilled(c_right, r_right + 2.0f, glow_col);
        draw_list->AddRectFilled(ImVec2(c_left.x - 2.0f, y_base - r_left),
                                 ImVec2(c_right.x + 2.0f, y_base + 2.0f), glow_col);
    }

    draw_list->AddCircleFilled(c_mid, r_mid, col);
    draw_list->AddCircleFilled(c_left, r_left, col);
    draw_list->AddCircleFilled(c_right, r_right, col);
    draw_list->AddRectFilled(ImVec2(c_left.x, y_base - r_left),
                             ImVec2(c_right.x, y_base), col);
}

void DrawCddbGraphic(ImDrawList* draw_list, ImVec2 center, float size, ImU32 col, ImU32 glow_col, bool lit) {
    const float r_outer = size * 0.44f;
    const float r_inner = size * 0.16f;
    const float r_hub   = size * 0.25f;

    if (lit) {
        draw_list->AddCircle(center, r_outer, glow_col, 24, 3.5f);
    }
    draw_list->AddCircle(center, r_outer, col, 24, 1.8f);
    draw_list->AddCircle(center, r_hub, col, 20, 1.2f);
    draw_list->AddCircleFilled(center, r_inner, col);

    // Subtle radial light reflection line
    const ImVec2 line_p1(center.x - r_outer * 0.7f, center.y - r_outer * 0.7f);
    const ImVec2 line_p2(center.x - r_hub * 0.7f, center.y - r_hub * 0.7f);
    draw_list->AddLine(line_p1, line_p2, col, 1.2f);
}

} // namespace

void RenderCdDisplay(CdDisplayModel& model, const ImVec2& user_size) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const float widget_width = (user_size.x > 0.0f) ? user_size.x : 660.0f;
    const float widget_height = (user_size.y > 0.0f) ? user_size.y : 250.0f;

    const ImVec2 screen_pos = ImGui::GetCursorScreenPos();
    const ImVec2 display_end(screen_pos.x + widget_width, screen_pos.y + widget_height);

    // Reserve layout space in ImGui
    ImGui::Dummy(ImVec2(widget_width, widget_height));

    // Outer Chassis Frame (Brushed metal / chassis appearance)
    draw_list->AddRectFilled(screen_pos, display_end, kChassisFrame, 10.0f);
    draw_list->AddRect(screen_pos, display_end, kBezelLight, 10.0f, 0, 1.5f);

    // Screw accents in corners
    auto draw_screw = [&](ImVec2 pos) {
        draw_list->AddCircleFilled(pos, 3.5f, IM_COL32(40, 40, 44, 255));
        draw_list->AddCircle(pos, 3.5f, IM_COL32(70, 70, 76, 255), 12, 1.0f);
        draw_list->AddLine(ImVec2(pos.x - 2.0f, pos.y), ImVec2(pos.x + 2.0f, pos.y), IM_COL32(20, 20, 22, 255), 1.0f);
    };
    draw_screw(ImVec2(screen_pos.x + 12.0f, screen_pos.y + 12.0f));
    draw_screw(ImVec2(display_end.x - 12.0f, screen_pos.y + 12.0f));
    draw_screw(ImVec2(screen_pos.x + 12.0f, display_end.y - 12.0f));
    draw_screw(ImVec2(display_end.x - 12.0f, display_end.y - 12.0f));

    // Inner Smoked Glass Display Area
    const ImVec2 glass_min(screen_pos.x + 24.0f, screen_pos.y + 22.0f);
    const ImVec2 glass_max(display_end.x - 24.0f, display_end.y - 22.0f);
    const float glass_w = glass_max.x - glass_min.x;
    const float glass_h = glass_max.y - glass_min.y;

    // Recessed bezel shadow and border
    draw_list->AddRectFilled(glass_min, glass_max, kGlassBg, 6.0f);
    draw_list->AddRect(glass_min, glass_max, kBezelDark, 6.0f, 0, 2.5f);
    draw_list->AddLine(ImVec2(glass_min.x + 1.0f, glass_max.y),
                       ImVec2(glass_max.x - 1.0f, glass_max.y),
                       IM_COL32(60, 60, 65, 180), 1.0f);

    // Subtle glass reflection sheen across top half
    draw_list->AddRectFilledMultiColor(
        glass_min,
        ImVec2(glass_max.x, glass_min.y + glass_h * 0.45f),
        IM_COL32(255, 255, 255, 16),
        IM_COL32(255, 255, 255, 10),
        IM_COL32(255, 255, 255, 0),
        IM_COL32(255, 255, 255, 0)
    );

    // Upper Silkscreen / VFD Status Row
    const float header_y = glass_min.y + 12.0f;

    // Left brand header
    draw_list->AddText(ImVec2(glass_min.x + 20.0f, header_y),
                       IM_COL32(160, 110, 50, 180), "COMPACT DISC DIGITAL AUDIO");

    // Play / Pause amber status indicator
    const bool is_play = model.is_playing();
    const ImU32 play_col = is_play ? kAmberLitCore : kAmberUnlit;
    const ImU32 pause_col = !is_play ? kAmberLitCore : kAmberUnlit;

    if (is_play) {
        draw_list->AddText(ImVec2(glass_min.x + 280.0f, header_y), kAmberLitGlow, "> PLAY");
    }
    draw_list->AddText(ImVec2(glass_min.x + 280.0f, header_y), play_col, "> PLAY");

    if (!is_play) {
        draw_list->AddText(ImVec2(glass_min.x + 345.0f, header_y), kAmberLitGlow, "|| PAUSE");
    }
    draw_list->AddText(ImVec2(glass_min.x + 345.0f, header_y), pause_col, "|| PAUSE");

    // Track and Time Headers above 7-segment displays
    const float seg_y = glass_min.y + 36.0f;
    const float seg_digit_h = 76.0f;
    const float seg_digit_w = 40.0f;
    const float seg_digit_gap = 10.0f;

    // "TRACK" label
    draw_list->AddText(ImVec2(glass_min.x + 40.0f, seg_y - 2.0f),
                       IM_COL32(180, 130, 60, 200), "TRACK");

    // "TOTAL" label
    char total_tracks_buf[16];
    std::snprintf(total_tracks_buf, sizeof(total_tracks_buf), "/ %02d", model.total_tracks());
    draw_list->AddText(ImVec2(glass_min.x + 145.0f, seg_y + 40.0f),
                       IM_COL32(150, 100, 40, 190), total_tracks_buf);

    // "MIN" and "SEC" labels
    draw_list->AddText(ImVec2(glass_min.x + 295.0f, seg_y - 2.0f),
                       IM_COL32(180, 130, 60, 200), "MIN");
    draw_list->AddText(ImVec2(glass_min.x + 440.0f, seg_y - 2.0f),
                       IM_COL32(180, 130, 60, 200), "SEC");

    // 7-Segment Values
    char track_buf[8];
    std::snprintf(track_buf, sizeof(track_buf), "%02d", model.current_track_number());

    char min_buf[8];
    std::snprintf(min_buf, sizeof(min_buf), "%02d", model.elapsed_minutes());

    char sec_buf[8];
    std::snprintf(sec_buf, sizeof(sec_buf), "%02d", model.elapsed_seconds());

    const float digits_start_y = seg_y + 14.0f;

    // Track Digits (2 digits)
    float cur_x = glass_min.x + 35.0f;
    DrawSevenSegmentDigit(draw_list, ImVec2(cur_x, digits_start_y),
                          ImVec2(seg_digit_w, seg_digit_h), track_buf[0]);
    cur_x += seg_digit_w + seg_digit_gap;
    DrawSevenSegmentDigit(draw_list, ImVec2(cur_x, digits_start_y),
                          ImVec2(seg_digit_w, seg_digit_h), track_buf[1]);

    // Time Digits (Minutes : Seconds)
    cur_x = glass_min.x + 265.0f;
    DrawSevenSegmentDigit(draw_list, ImVec2(cur_x, digits_start_y),
                          ImVec2(seg_digit_w, seg_digit_h), min_buf[0]);
    cur_x += seg_digit_w + seg_digit_gap;
    DrawSevenSegmentDigit(draw_list, ImVec2(cur_x, digits_start_y),
                          ImVec2(seg_digit_w, seg_digit_h), min_buf[1]);
    cur_x += seg_digit_w + 6.0f;

    // Colon between Min and Sec: blinks when paused, solid when playing
    bool colon_lit = true;
    if (!model.is_playing()) {
        const double phase = std::fmod(model.uptime_seconds(), 1.0);
        colon_lit = (phase < 0.5);
    }
    const float colon_w = 16.0f;
    DrawColon(draw_list, ImVec2(cur_x, digits_start_y),
              ImVec2(colon_w, seg_digit_h), colon_lit);
    cur_x += colon_w + 6.0f;

    DrawSevenSegmentDigit(draw_list, ImVec2(cur_x, digits_start_y),
                          ImVec2(seg_digit_w, seg_digit_h), sec_buf[0]);
    cur_x += seg_digit_w + seg_digit_gap;
    DrawSevenSegmentDigit(draw_list, ImVec2(cur_x, digits_start_y),
                          ImVec2(seg_digit_w, seg_digit_h), sec_buf[1]);

    // Indicators Row Underneath
    // Indicators: "repeat (track/disk)", "shuffle", "cloud", "CDDB"
    const float indicator_y = digits_start_y + seg_digit_h + 16.0f;
    const float ind_tile_h = 34.0f;
    const float ind_tile_w = (glass_w - 50.0f) / 4.0f;

    const bool cloud_active = model.is_cloud_active();
    const bool cddb_active = model.is_cddb_active();
    const bool is_connected = (model.connectivity_status() == ConnectivityStatus::Connected);

    // Indicator 1: REPEAT (track / disk)
    {
        const ImVec2 tile_pos(glass_min.x + 10.0f + 0 * (ind_tile_w + 10.0f), indicator_y);
        const bool repeat_on = (model.repeat_mode() != RepeatMode::Off);
        const ImU32 col_core = repeat_on ? kGreenLitCore : kGreenUnlit;
        const ImU32 col_glow = repeat_on ? kGreenLitGlow : 0;
        const float text_y = tile_pos.y + (ind_tile_h - ImGui::GetTextLineHeight()) * 0.5f;

        // Background slot
        draw_list->AddRectFilled(tile_pos, ImVec2(tile_pos.x + ind_tile_w, tile_pos.y + ind_tile_h),
                                 IM_COL32(8, 16, 10, 200), 4.0f);
        draw_list->AddRect(tile_pos, ImVec2(tile_pos.x + ind_tile_w, tile_pos.y + ind_tile_h),
                           repeat_on ? IM_COL32(30, 90, 45, 180) : IM_COL32(18, 30, 20, 180), 4.0f);

        // Graphic icon
        DrawRepeatGraphic(draw_list, ImVec2(tile_pos.x + 20.0f, tile_pos.y + ind_tile_h * 0.5f),
                          20.0f, col_core, col_glow, repeat_on);

        // Label
        std::string label = "REPEAT";
        if (model.repeat_mode() == RepeatMode::Disc) {
            label += " DISC";
        } else if (model.repeat_mode() == RepeatMode::Track) {
            label += " 1";
        }
        if (repeat_on) {
            draw_list->AddText(ImVec2(tile_pos.x + 36.0f, text_y), col_glow, label.c_str());
        }
        draw_list->AddText(ImVec2(tile_pos.x + 36.0f, text_y), col_core, label.c_str());

        // Mouse click support
        ImGui::SetCursorScreenPos(tile_pos);
        if (ImGui::InvisibleButton("RepeatIndicatorBtn", ImVec2(ind_tile_w, ind_tile_h))) {
            model.toggle_repeat();
        }
        if (ImGui::IsItemHovered()) {
            draw_list->AddRect(tile_pos, ImVec2(tile_pos.x + ind_tile_w, tile_pos.y + ind_tile_h),
                               IM_COL32(120, 255, 150, 120), 4.0f);
        }
    }

    // Indicator 2: SHUFFLE
    {
        const ImVec2 tile_pos(glass_min.x + 10.0f + 1 * (ind_tile_w + 10.0f), indicator_y);
        const bool shuffle_on = model.is_shuffle();
        const ImU32 col_core = shuffle_on ? kGreenLitCore : kGreenUnlit;
        const ImU32 col_glow = shuffle_on ? kGreenLitGlow : 0;
        const float text_y = tile_pos.y + (ind_tile_h - ImGui::GetTextLineHeight()) * 0.5f;

        draw_list->AddRectFilled(tile_pos, ImVec2(tile_pos.x + ind_tile_w, tile_pos.y + ind_tile_h),
                                 IM_COL32(8, 16, 10, 200), 4.0f);
        draw_list->AddRect(tile_pos, ImVec2(tile_pos.x + ind_tile_w, tile_pos.y + ind_tile_h),
                           shuffle_on ? IM_COL32(30, 90, 45, 180) : IM_COL32(18, 30, 20, 180), 4.0f);

        DrawShuffleGraphic(draw_list, ImVec2(tile_pos.x + 12.0f, tile_pos.y + 9.0f),
                           18.0f, col_core, col_glow, shuffle_on);

        if (shuffle_on) {
            draw_list->AddText(ImVec2(tile_pos.x + 40.0f, text_y), col_glow, "SHUFFLE");
        }
        draw_list->AddText(ImVec2(tile_pos.x + 40.0f, text_y), col_core, "SHUFFLE");

        ImGui::SetCursorScreenPos(tile_pos);
        if (ImGui::InvisibleButton("ShuffleIndicatorBtn", ImVec2(ind_tile_w, ind_tile_h))) {
            model.toggle_shuffle();
        }
        if (ImGui::IsItemHovered()) {
            draw_list->AddRect(tile_pos, ImVec2(tile_pos.x + ind_tile_w, tile_pos.y + ind_tile_h),
                               IM_COL32(120, 255, 150, 120), 4.0f);
        }
    }

    // Indicator 3: CLOUD
    {
        const ImVec2 tile_pos(glass_min.x + 10.0f + 2 * (ind_tile_w + 10.0f), indicator_y);
        ImU32 col_core = kGreenUnlit;
        ImU32 col_glow = 0;
        bool is_lit = false;
        const float text_y = tile_pos.y + (ind_tile_h - ImGui::GetTextLineHeight()) * 0.5f;

        if (cloud_active) {
            is_lit = true;
            if (is_connected) {
                col_core = kGreenLitCore;
                col_glow = kGreenLitGlow;
            } else {
                col_core = kRedLitCore;
                col_glow = kRedLitGlow;
            }
        }

        draw_list->AddRectFilled(tile_pos, ImVec2(tile_pos.x + ind_tile_w, tile_pos.y + ind_tile_h),
                                 IM_COL32(8, 16, 10, 200), 4.0f);
        draw_list->AddRect(tile_pos, ImVec2(tile_pos.x + ind_tile_w, tile_pos.y + ind_tile_h),
                           is_lit ? (is_connected ? IM_COL32(30, 90, 45, 180) : IM_COL32(110, 30, 25, 180))
                                  : IM_COL32(18, 30, 20, 180), 4.0f);

        DrawCloudGraphic(draw_list, ImVec2(tile_pos.x + 22.0f, tile_pos.y + ind_tile_h * 0.5f),
                         20.0f, col_core, col_glow, is_lit);

        if (is_lit) {
            draw_list->AddText(ImVec2(tile_pos.x + 44.0f, text_y), col_glow, "CLOUD");
        }
        draw_list->AddText(ImVec2(tile_pos.x + 44.0f, text_y), col_core, "CLOUD");

        ImGui::SetCursorScreenPos(tile_pos);
        if (ImGui::InvisibleButton("CloudIndicatorBtn", ImVec2(ind_tile_w, ind_tile_h))) {
            model.toggle_connectivity();
        }
        if (ImGui::IsItemHovered()) {
            draw_list->AddRect(tile_pos, ImVec2(tile_pos.x + ind_tile_w, tile_pos.y + ind_tile_h),
                               IM_COL32(120, 255, 150, 120), 4.0f);
        }
    }

    // Indicator 4: CDDB
    {
        const ImVec2 tile_pos(glass_min.x + 10.0f + 3 * (ind_tile_w + 10.0f), indicator_y);
        ImU32 col_core = kGreenUnlit;
        ImU32 col_glow = 0;
        bool is_lit = false;
        const float text_y = tile_pos.y + (ind_tile_h - ImGui::GetTextLineHeight()) * 0.5f;

        if (cddb_active) {
            is_lit = true;
            if (is_connected) {
                col_core = kGreenLitCore;
                col_glow = kGreenLitGlow;
            } else {
                col_core = kRedLitCore;
                col_glow = kRedLitGlow;
            }
        }

        draw_list->AddRectFilled(tile_pos, ImVec2(tile_pos.x + ind_tile_w, tile_pos.y + ind_tile_h),
                                 IM_COL32(8, 16, 10, 200), 4.0f);
        draw_list->AddRect(tile_pos, ImVec2(tile_pos.x + ind_tile_w, tile_pos.y + ind_tile_h),
                           is_lit ? (is_connected ? IM_COL32(30, 90, 45, 180) : IM_COL32(110, 30, 25, 180))
                                  : IM_COL32(18, 30, 20, 180), 4.0f);

        DrawCddbGraphic(draw_list, ImVec2(tile_pos.x + 22.0f, tile_pos.y + ind_tile_h * 0.5f),
                        20.0f, col_core, col_glow, is_lit);

        if (is_lit) {
            draw_list->AddText(ImVec2(tile_pos.x + 44.0f, text_y), col_glow, "CDDB");
        }
        draw_list->AddText(ImVec2(tile_pos.x + 44.0f, text_y), col_core, "CDDB");

        ImGui::SetCursorScreenPos(tile_pos);
        if (ImGui::InvisibleButton("CddbIndicatorBtn", ImVec2(ind_tile_w, ind_tile_h))) {
            model.toggle_connectivity();
        }
        if (ImGui::IsItemHovered()) {
            draw_list->AddRect(tile_pos, ImVec2(tile_pos.x + ind_tile_w, tile_pos.y + ind_tile_h),
                               IM_COL32(120, 255, 150, 120), 4.0f);
        }
    }

    // Restore cursor position to right below the display widget
    ImGui::SetCursorScreenPos(ImVec2(screen_pos.x, display_end.y + 14.0f));
}

} // namespace mycds::gui
