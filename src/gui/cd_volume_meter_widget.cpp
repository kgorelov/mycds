#include "cd_volume_meter_widget.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "cd_brushed_metal.h"

namespace mycds::gui {

namespace {

// Palette constants matching vintage audio hardware
constexpr ImU32 kChassisFrame = IM_COL32(28, 28, 32, 255);
constexpr ImU32 kBezelLight   = IM_COL32(50, 50, 54, 255);
constexpr ImU32 kBezelDark    = IM_COL32(18, 18, 20, 255);
constexpr ImU32 kGlassBg      = IM_COL32(10, 8, 7, 255);

constexpr ImU32 kAmberText    = IM_COL32(200, 140, 50, 220);
constexpr ImU32 kAmberLitCore = IM_COL32(255, 175, 20, 255);

// LED Color definitions
constexpr ImU32 kGreenLitCore   = IM_COL32(65, 255, 115, 255);
constexpr ImU32 kGreenLitGlow   = IM_COL32(35, 210, 75, 80);
constexpr ImU32 kGreenLitHi     = IM_COL32(190, 255, 215, 200);
constexpr ImU32 kGreenUnlit     = IM_COL32(12, 38, 16, 220);

constexpr ImU32 kYellowLitCore  = IM_COL32(255, 225, 25, 255);
constexpr ImU32 kYellowLitGlow  = IM_COL32(230, 180, 0, 80);
constexpr ImU32 kYellowLitHi    = IM_COL32(255, 250, 180, 200);
constexpr ImU32 kYellowUnlit    = IM_COL32(44, 38, 8, 220);

constexpr ImU32 kRedLitCore     = IM_COL32(255, 55, 45, 255);
constexpr ImU32 kRedLitGlow     = IM_COL32(230, 40, 30, 90);
constexpr ImU32 kRedLitHi       = IM_COL32(255, 175, 165, 200);
constexpr ImU32 kRedUnlit       = IM_COL32(48, 12, 10, 220);

constexpr int kTotalSegments = 24;
constexpr int kGreenSegments = 14;  // 0 .. 13
constexpr int kYellowSegments = 5;  // 14 .. 18
// Red segments: 19 .. 23 (5 segments)

enum class LedColorType {
    Green,
    Yellow,
    Red
};

LedColorType GetSegmentColor(int index) {
    if (index < kGreenSegments) {
        return LedColorType::Green;
    }
    if (index < kGreenSegments + kYellowSegments) {
        return LedColorType::Yellow;
    }
    return LedColorType::Red;
}

void DrawSquareLed(ImDrawList* draw_list, ImVec2 pos, float size,
                   LedColorType col_type, bool is_lit, bool is_peak) {
    const ImVec2 max_pt(pos.x + size, pos.y + size);

    // Dark well recess
    draw_list->AddRectFilled(pos, max_pt, IM_COL32(6, 6, 8, 255), 1.0f);

    ImU32 core_col = 0;
    ImU32 glow_col = 0;
    ImU32 hi_col = 0;
    ImU32 unlit_col = 0;

    switch (col_type) {
        case LedColorType::Green:
            core_col = kGreenLitCore;
            glow_col = kGreenLitGlow;
            hi_col = kGreenLitHi;
            unlit_col = kGreenUnlit;
            break;
        case LedColorType::Yellow:
            core_col = kYellowLitCore;
            glow_col = kYellowLitGlow;
            hi_col = kYellowLitHi;
            unlit_col = kYellowUnlit;
            break;
        case LedColorType::Red:
            core_col = kRedLitCore;
            glow_col = kRedLitGlow;
            hi_col = kRedLitHi;
            unlit_col = kRedUnlit;
            break;
    }

    if (is_lit) {
        // Outer halo glow
        draw_list->AddRect(ImVec2(pos.x - 2.5f, pos.y - 2.5f),
                           ImVec2(max_pt.x + 2.5f, max_pt.y + 2.5f),
                           glow_col, 2.0f, 0, 2.5f);

        // Core lit square LED
        draw_list->AddRectFilled(pos, max_pt, core_col, 1.0f);

        // Glossy lens highlight (upper third bar)
        draw_list->AddRectFilled(ImVec2(pos.x + 2.0f, pos.y + 2.0f),
                                 ImVec2(max_pt.x - 2.0f, pos.y + 5.0f),
                                 hi_col, 0.5f);

        // If it's a floating peak hold dot, highlight the border
        if (is_peak) {
            draw_list->AddRect(pos, max_pt, IM_COL32(255, 255, 255, 180), 1.0f, 0, 1.2f);
        }
    } else {
        // Unlit dark LED
        draw_list->AddRectFilled(pos, max_pt, unlit_col, 1.0f);
        draw_list->AddRect(pos, max_pt, IM_COL32(20, 20, 24, 180), 1.0f, 0, 1.0f);
    }
}

} // namespace

void RenderCdVolumeMeter(const CdDisplayModel& model, const ImVec2& user_size) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const float widget_width = (user_size.x >= 680.0f) ? user_size.x : 680.0f;
    const float widget_height = (user_size.y > 0.0f) ? user_size.y : 90.0f;

    const ImVec2 screen_pos = ImGui::GetCursorScreenPos();
    const ImVec2 display_end(screen_pos.x + widget_width, screen_pos.y + widget_height);

    // Reserve layout space in ImGui
    ImGui::Dummy(ImVec2(widget_width, widget_height));

    // Outer Chassis Frame
    GetBrushedMetal().RenderPanelChassis(draw_list, screen_pos, display_end, 8.0f);

    // Corner screw accents
    auto draw_screw = [&](ImVec2 pos) {
        draw_list->AddCircleFilled(pos, 3.0f, IM_COL32(40, 40, 44, 255));
        draw_list->AddCircle(pos, 3.0f, IM_COL32(70, 70, 76, 255), 10, 1.0f);
        draw_list->AddLine(ImVec2(pos.x - 1.8f, pos.y), ImVec2(pos.x + 1.8f, pos.y), IM_COL32(20, 20, 22, 255), 1.0f);
    };
    draw_screw(ImVec2(screen_pos.x + 10.0f, screen_pos.y + 10.0f));
    draw_screw(ImVec2(display_end.x - 10.0f, screen_pos.y + 10.0f));
    draw_screw(ImVec2(screen_pos.x + 10.0f, display_end.y - 10.0f));
    draw_screw(ImVec2(display_end.x - 10.0f, display_end.y - 10.0f));

    // Inner Smoked Glass Window
    const ImVec2 glass_min(screen_pos.x + 22.0f, screen_pos.y + 8.0f);
    const ImVec2 glass_max(display_end.x - 22.0f, display_end.y - 8.0f);
    const float glass_h = glass_max.y - glass_min.y;

    draw_list->AddRectFilled(glass_min, glass_max, kGlassBg, 5.0f);
    draw_list->AddRect(glass_min, glass_max, kBezelDark, 5.0f, 0, 2.0f);
    draw_list->AddLine(ImVec2(glass_min.x + 1.0f, glass_max.y),
                       ImVec2(glass_max.x - 1.0f, glass_max.y),
                       IM_COL32(60, 60, 65, 180), 1.0f);

    // Glass glare reflection
    draw_list->AddRectFilledMultiColor(
        glass_min,
        ImVec2(glass_max.x, glass_min.y + glass_h * 0.40f),
        IM_COL32(255, 255, 255, 14),
        IM_COL32(255, 255, 255, 8),
        IM_COL32(255, 255, 255, 0),
        IM_COL32(255, 255, 255, 0)
    );

    // LED Layout geometry
    constexpr float kLedSize = 17.0f;
    constexpr float kLedGap  = 4.0f;
    constexpr float kLedRowW = kTotalSegments * kLedSize + (kTotalSegments - 1) * kLedGap; // ~500px

    const float led_start_x = glass_min.x + 52.0f;
    const float scale_y     = glass_min.y + 6.0f;
    const float row_y_left  = glass_min.y + 24.0f;
    const float row_y_right = glass_min.y + 48.0f;

    // Header Title (Top-Left)
    draw_list->AddText(ImVec2(glass_min.x + 8.0f, scale_y),
                       IM_COL32(160, 110, 50, 180), "LEVEL");

    // Scale Markings & Ticks above the LEDs
    struct ScalePoint {
        int segment;
        const char* label;
        bool is_ref_zero;
    };

    constexpr ScalePoint kScalePoints[] = {
        {0,  "-30", false},
        {3,  "-20", false},
        {6,  "-14", false},
        {9,  "-8",  false},
        {12, "-4",  false},
        {14, "-2",  false},
        {16, "-1",  false},
        {18, "0",   true},
        {20, "+2",  false},
        {22, "+4",  false},
        {23, "+6",  false}
    };

    for (const auto& sp : kScalePoints) {
        const float seg_x = led_start_x + sp.segment * (kLedSize + kLedGap);
        const float center_x = seg_x + kLedSize * 0.5f;

        const LedColorType col_type = GetSegmentColor(sp.segment);
        ImU32 tick_col = IM_COL32(110, 110, 116, 180);
        ImU32 text_col = IM_COL32(130, 130, 136, 200);

        if (sp.is_ref_zero) {
            tick_col = kAmberLitCore;
            text_col = kAmberLitCore;
        } else if (col_type == LedColorType::Yellow) {
            tick_col = IM_COL32(200, 180, 50, 180);
            text_col = IM_COL32(220, 200, 60, 210);
        } else if (col_type == LedColorType::Red) {
            tick_col = IM_COL32(220, 60, 50, 180);
            text_col = IM_COL32(230, 80, 70, 210);
        }

        // Tick mark
        const float tick_len = sp.is_ref_zero ? 4.5f : 3.0f;
        draw_list->AddLine(ImVec2(center_x, row_y_left - 2.0f - tick_len),
                           ImVec2(center_x, row_y_left - 2.0f),
                           tick_col, sp.is_ref_zero ? 1.5f : 1.0f);

        // Text label
        const ImVec2 txt_size = ImGui::CalcTextSize(sp.label);
        draw_list->AddText(ImVec2(center_x - txt_size.x * 0.5f, scale_y),
                           text_col, sp.label);
    }

    // "dB" unit label
    draw_list->AddText(ImVec2(led_start_x + kLedRowW + 6.0f, scale_y),
                       kAmberText, "dB");

    // Channel Badges (CH-L and CH-R)
    auto draw_channel_badge = [&](ImVec2 pos, const char* label) {
        const ImVec2 badge_max(pos.x + 32.0f, pos.y + kLedSize);
        draw_list->AddRectFilled(pos, badge_max, IM_COL32(18, 18, 22, 230), 3.0f);
        draw_list->AddRect(pos, badge_max, IM_COL32(40, 40, 48, 220), 3.0f, 0, 1.0f);

        const ImVec2 txt_sz = ImGui::CalcTextSize(label);
        const ImVec2 txt_pos(pos.x + (32.0f - txt_sz.x) * 0.5f,
                             pos.y + (kLedSize - txt_sz.y) * 0.5f);
        draw_list->AddText(txt_pos, kAmberText, label);
    };

    draw_channel_badge(ImVec2(glass_min.x + 12.0f, row_y_left), "L");
    draw_channel_badge(ImVec2(glass_min.x + 12.0f, row_y_right), "R");

    // Determine lit segments based on model state
    const bool is_playing = model.is_playing();

    int lit_left = 0;
    int lit_right = 0;
    int peak_left_idx = -1;
    int peak_right_idx = -1;

    if (is_playing) {
        const double l_val = model.left_level();
        const double r_val = model.right_level();
        const double l_pk  = model.left_peak();
        const double r_pk  = model.right_peak();

        lit_left = static_cast<int>(std::round(l_val * kTotalSegments));
        lit_right = static_cast<int>(std::round(r_val * kTotalSegments));

        lit_left = std::clamp(lit_left, 0, kTotalSegments);
        lit_right = std::clamp(lit_right, 0, kTotalSegments);

        if (l_pk > 0.02) {
            peak_left_idx = std::clamp(static_cast<int>(std::round(l_pk * (kTotalSegments - 1))),
                                       0, kTotalSegments - 1);
        }
        if (r_pk > 0.02) {
            peak_right_idx = std::clamp(static_cast<int>(std::round(r_pk * (kTotalSegments - 1))),
                                        0, kTotalSegments - 1);
        }
    }

    // Render Left Channel Square LEDs
    for (int i = 0; i < kTotalSegments; ++i) {
        const ImVec2 led_pos(led_start_x + i * (kLedSize + kLedGap), row_y_left);
        const LedColorType col = GetSegmentColor(i);
        const bool is_bar_lit = (i < lit_left);
        const bool is_pk = (i == peak_left_idx);
        const bool is_lit = is_bar_lit || is_pk;

        DrawSquareLed(draw_list, led_pos, kLedSize, col, is_lit, is_pk && !is_bar_lit);
    }

    // Render Right Channel Square LEDs
    for (int i = 0; i < kTotalSegments; ++i) {
        const ImVec2 led_pos(led_start_x + i * (kLedSize + kLedGap), row_y_right);
        const LedColorType col = GetSegmentColor(i);
        const bool is_bar_lit = (i < lit_right);
        const bool is_pk = (i == peak_right_idx);
        const bool is_lit = is_bar_lit || is_pk;

        DrawSquareLed(draw_list, led_pos, kLedSize, col, is_lit, is_pk && !is_bar_lit);
    }

    // Peak Overload Indicators (PEAK L and PEAK R) on the right side
    const float peak_box_x = led_start_x + kLedRowW + 30.0f;
    const float peak_box_w = 48.0f;

    auto draw_peak_indicator = [&](float y_pos, bool is_peak_active, const char* label) {
        const ImVec2 box_min(peak_box_x, y_pos);
        const ImVec2 box_max(peak_box_x + peak_box_w, y_pos + kLedSize);

        if (is_peak_active) {
            // Glowing red peak indicator
            draw_list->AddRect(ImVec2(box_min.x - 2.0f, box_min.y - 2.0f),
                               ImVec2(box_max.x + 2.0f, box_max.y + 2.0f),
                               kRedLitGlow, 3.0f, 0, 2.0f);
            draw_list->AddRectFilled(box_min, box_max, kRedLitCore, 2.0f);
            draw_list->AddRect(box_min, box_max, IM_COL32(255, 180, 170, 255), 2.0f, 0, 1.0f);

            const ImVec2 sz = ImGui::CalcTextSize(label);
            draw_list->AddText(ImVec2(box_min.x + (peak_box_w - sz.x) * 0.5f,
                                      box_min.y + (kLedSize - sz.y) * 0.5f),
                               IM_COL32(20, 10, 10, 255), label);
        } else {
            // Dark unlit peak indicator
            draw_list->AddRectFilled(box_min, box_max, IM_COL32(24, 10, 10, 220), 2.0f);
            draw_list->AddRect(box_min, box_max, IM_COL32(40, 16, 16, 220), 2.0f, 0, 1.0f);

            const ImVec2 sz = ImGui::CalcTextSize(label);
            draw_list->AddText(ImVec2(box_min.x + (peak_box_w - sz.x) * 0.5f,
                                      box_min.y + (kLedSize - sz.y) * 0.5f),
                               IM_COL32(80, 30, 28, 200), label);
        }
    };

    const bool l_clip = is_playing && (lit_left >= (kTotalSegments - 1) || peak_left_idx >= (kTotalSegments - 1));
    const bool r_clip = is_playing && (lit_right >= (kTotalSegments - 1) || peak_right_idx >= (kTotalSegments - 1));

    draw_peak_indicator(row_y_left, l_clip, "PEAK");
    draw_peak_indicator(row_y_right, r_clip, "PEAK");

    // Restore cursor position directly beneath the volume meter widget
    ImGui::SetCursorScreenPos(ImVec2(screen_pos.x, display_end.y + 12.0f));
}

} // namespace mycds::gui
