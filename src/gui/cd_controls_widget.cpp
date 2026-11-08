#include "cd_controls_widget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "cd_brushed_metal.h"
#include "imgui.h"

namespace mycds::gui {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Color palette for hardware buttons and cyan backlight
constexpr ImU32 kColSocketBg = IM_COL32(14, 15, 18, 255);
constexpr ImU32 kColSocketInnerShadow = IM_COL32(6, 6, 8, 240);
constexpr ImU32 kColSocketBezelLight = IM_COL32(50, 53, 62, 140);

// Brushed metal button tones (distinctly lighter than the ~40 dark chassis faceplate)
constexpr ImU32 kColBtnMetalBase = IM_COL32(106, 110, 120, 255);
constexpr ImU32 kColBtnMetalHover = IM_COL32(126, 131, 142, 255);
constexpr ImU32 kColBtnMetalActive = IM_COL32(82, 85, 94, 255);

// 3D Machined bevel chamfers
constexpr ImU32 kColBevelTopLight = IM_COL32(220, 226, 242, 240);
constexpr ImU32 kColBevelLeftLight = IM_COL32(175, 180, 196, 200);
constexpr ImU32 kColBevelBottomShadow = IM_COL32(28, 30, 36, 255);
constexpr ImU32 kColBevelRightShadow = IM_COL32(46, 49, 58, 200);

// Inverted bevels for depressed button state
constexpr ImU32 kColBevelDepressedTop = IM_COL32(18, 19, 24, 255);
constexpr ImU32 kColBevelDepressedLeft = IM_COL32(28, 30, 38, 220);
constexpr ImU32 kColBevelDepressedBottom = IM_COL32(165, 172, 188, 180);
constexpr ImU32 kColBevelDepressedRight = IM_COL32(130, 136, 150, 150);

// Cyan-blue backlight colors
constexpr ImU32 kColCyanGlowCore = IM_COL32(210, 250, 255, 255);
constexpr ImU32 kColCyanBright = IM_COL32(0, 235, 255, 255);
constexpr ImU32 kColCyanMuted = IM_COL32(0, 160, 195, 180);
constexpr ImU32 kColCyanBloom = IM_COL32(0, 225, 255, 65);
constexpr ImU32 kColCyanBloomBright = IM_COL32(0, 240, 255, 110);
constexpr ImU32 kColEngravedShadow = IM_COL32(12, 18, 26, 160);

// Long press seeking state tracking
struct SeekPressTracker {
    double press_start_time{0.0};
    bool is_held{false};
    bool did_long_press{false};
};

static SeekPressTracker s_prev_tracker;
static SeekPressTracker s_next_tracker;

// Pseudo-random line generator for micro-brushed metal texture on buttons
float Hash1D(int x) {
    uint32_t n = static_cast<uint32_t>(x);
    n = (n << 13) ^ n;
    n = n * (n * n * 15731 + 789221) + 1376312589;
    return static_cast<float>(n & 0x7FFFFFFF) / 2147483648.0f;
}

void DrawButtonBrushedFace(ImDrawList* draw_list, const ImVec2& min_pos, const ImVec2& max_pos, bool hovered, bool pressed) {
    const ImU32 base_col = pressed ? kColBtnMetalActive : (hovered ? kColBtnMetalHover : kColBtnMetalBase);
    draw_list->AddRectFilled(min_pos, max_pos, base_col, 2.0f);

    // Subtle horizontal brushed hairlines
    const int start_y = static_cast<int>(min_pos.y + 1);
    const int end_y = static_cast<int>(max_pos.y - 1);
    for (int y = start_y; y < end_y; y += 2) {
        const float noise = Hash1D(y * 31 + 17) * 2.0f - 1.0f;
        const ImU32 line_col = (noise > 0.0f)
            ? IM_COL32(255, 255, 255, static_cast<int>(10.0f * noise))
            : IM_COL32(0, 0, 0, static_cast<int>(12.0f * (-noise)));
        draw_list->AddLine(ImVec2(min_pos.x + 2.0f, static_cast<float>(y)),
                           ImVec2(max_pos.x - 2.0f, static_cast<float>(y)),
                           line_col, 1.0f);
    }

    // Overhead ambient gradient
    draw_list->AddRectFilledMultiColor(
        min_pos, max_pos,
        IM_COL32(255, 255, 255, 22),
        IM_COL32(255, 255, 255, 22),
        IM_COL32(0, 0, 0, 30),
        IM_COL32(0, 0, 0, 30)
    );

    // Bevel edges
    if (!pressed) {
        draw_list->AddLine(ImVec2(min_pos.x, min_pos.y), ImVec2(max_pos.x, min_pos.y), kColBevelTopLight, 1.2f);
        draw_list->AddLine(ImVec2(min_pos.x, min_pos.y), ImVec2(min_pos.x, max_pos.y), kColBevelLeftLight, 1.0f);
        draw_list->AddLine(ImVec2(min_pos.x, max_pos.y - 1.0f), ImVec2(max_pos.x, max_pos.y - 1.0f), kColBevelBottomShadow, 1.2f);
        draw_list->AddLine(ImVec2(max_pos.x - 1.0f, min_pos.y), ImVec2(max_pos.x - 1.0f, max_pos.y), kColBevelRightShadow, 1.0f);
    } else {
        draw_list->AddLine(ImVec2(min_pos.x, min_pos.y), ImVec2(max_pos.x, min_pos.y), kColBevelDepressedTop, 1.2f);
        draw_list->AddLine(ImVec2(min_pos.x, min_pos.y), ImVec2(min_pos.x, max_pos.y), kColBevelDepressedLeft, 1.0f);
        draw_list->AddLine(ImVec2(min_pos.x, max_pos.y - 1.0f), ImVec2(max_pos.x, max_pos.y - 1.0f), kColBevelDepressedBottom, 1.2f);
        draw_list->AddLine(ImVec2(max_pos.x - 1.0f, min_pos.y), ImVec2(max_pos.x - 1.0f, max_pos.y), kColBevelDepressedRight, 1.0f);
    }
}

void DrawHardwareButtonSocket(ImDrawList* draw_list, const ImVec2& socket_min, const ImVec2& socket_max) {
    draw_list->AddRectFilled(socket_min, socket_max, kColSocketBg, 3.0f);
    draw_list->AddLine(ImVec2(socket_min.x, socket_min.y), ImVec2(socket_max.x, socket_min.y), kColSocketInnerShadow, 1.0f);
    draw_list->AddLine(ImVec2(socket_min.x, socket_min.y), ImVec2(socket_min.x, socket_max.y), kColSocketInnerShadow, 1.0f);
    draw_list->AddLine(ImVec2(socket_min.x, socket_max.y), ImVec2(socket_max.x, socket_max.y), kColSocketBezelLight, 1.0f);
    draw_list->AddLine(ImVec2(socket_max.x, socket_min.y), ImVec2(socket_max.x, socket_max.y), kColSocketBezelLight, 1.0f);
}

// -----------------------------------------------------------------------------
// Graphical Pictograms with Cyan-Blue Backlighting
// -----------------------------------------------------------------------------

void DrawPlayPausePictogram(ImDrawList* draw_list, const ImVec2& center, bool is_playing, bool hovered, bool pressed) {
    const float play_glow = (is_playing || pressed) ? 1.0f : (hovered ? 0.7f : 0.45f);
    const float pause_glow = (!is_playing || pressed) ? 1.0f : (hovered ? 0.7f : 0.45f);

    const ImU32 play_core = (play_glow >= 0.9f) ? kColCyanGlowCore : kColCyanMuted;
    const ImU32 play_bloom = (play_glow >= 0.9f) ? kColCyanBloomBright : kColCyanBloom;
    const ImU32 pause_core = (pause_glow >= 0.9f) ? kColCyanGlowCore : kColCyanMuted;
    const ImU32 pause_bloom = (pause_glow >= 0.9f) ? kColCyanBloomBright : kColCyanBloom;

    // Play triangle vertices on the left half
    const ImVec2 p1(center.x - 16.0f, center.y - 10.0f);
    const ImVec2 p2(center.x - 16.0f, center.y + 10.0f);
    const ImVec2 p3(center.x - 3.0f, center.y);

    // Laser-etched drop shadow
    draw_list->AddTriangleFilled(ImVec2(p1.x + 1.0f, p1.y + 1.0f),
                                 ImVec2(p2.x + 1.0f, p2.y + 1.0f),
                                 ImVec2(p3.x + 1.0f, p3.y + 1.0f), kColEngravedShadow);
    // Cyan backlight bloom and filled core
    draw_list->AddTriangle(p1, p2, p3, play_bloom, 2.5f);
    draw_list->AddTriangleFilled(p1, p2, p3, play_core);

    // Pause bars on the right half
    const ImVec2 b1_min(center.x + 3.0f, center.y - 10.0f);
    const ImVec2 b1_max(center.x + 7.0f, center.y + 10.0f);
    const ImVec2 b2_min(center.x + 11.0f, center.y - 10.0f);
    const ImVec2 b2_max(center.x + 15.0f, center.y + 10.0f);

    // Laser-etched drop shadow
    draw_list->AddRectFilled(ImVec2(b1_min.x + 1.0f, b1_min.y + 1.0f), ImVec2(b1_max.x + 1.0f, b1_max.y + 1.0f), kColEngravedShadow, 1.0f);
    draw_list->AddRectFilled(ImVec2(b2_min.x + 1.0f, b2_min.y + 1.0f), ImVec2(b2_max.x + 1.0f, b2_max.y + 1.0f), kColEngravedShadow, 1.0f);

    // Cyan backlight bloom and filled core
    draw_list->AddRect(b1_min, b1_max, pause_bloom, 1.0f, 0, 2.0f);
    draw_list->AddRect(b2_min, b2_max, pause_bloom, 1.0f, 0, 2.0f);
    draw_list->AddRectFilled(b1_min, b1_max, pause_core, 1.0f);
    draw_list->AddRectFilled(b2_min, b2_max, pause_core, 1.0f);
}

void DrawStopPictogram(ImDrawList* draw_list, const ImVec2& center, bool hovered, bool pressed) {
    const ImU32 core_col = pressed ? kColCyanGlowCore : (hovered ? kColCyanBright : kColCyanMuted);
    const ImU32 bloom_col = pressed ? kColCyanBloomBright : kColCyanBloom;

    const ImVec2 min_pos(center.x - 9.0f, center.y - 9.0f);
    const ImVec2 max_pos(center.x + 9.0f, center.y + 9.0f);

    // Laser-etched drop shadow
    draw_list->AddRectFilled(ImVec2(min_pos.x + 1.0f, min_pos.y + 1.0f),
                             ImVec2(max_pos.x + 1.0f, max_pos.y + 1.0f),
                             kColEngravedShadow, 1.5f);
    // Cyan bloom and core
    draw_list->AddRect(min_pos, max_pos, bloom_col, 1.5f, 0, 2.5f);
    draw_list->AddRectFilled(min_pos, max_pos, core_col, 1.5f);
}

void DrawPrevPictogram(ImDrawList* draw_list, const ImVec2& center, bool is_seeking, bool hovered, bool pressed) {
    ImU32 core_col = (pressed || is_seeking) ? kColCyanGlowCore : (hovered ? kColCyanBright : kColCyanMuted);
    ImU32 bloom_col = (pressed || is_seeking) ? kColCyanBloomBright : kColCyanBloom;

    if (is_seeking) {
        // Fast pulsing cyan glow during rewind seeking
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 22.0f);
        bloom_col = IM_COL32(0, 240, 255, static_cast<int>(120 + 130 * pulse));
    }

    // Left chevron 1
    const ImVec2 p1(center.x - 2.0f, center.y - 9.0f);
    const ImVec2 p2(center.x - 2.0f, center.y + 9.0f);
    const ImVec2 p3(center.x - 12.0f, center.y);

    // Left chevron 2
    const ImVec2 q1(center.x + 9.0f, center.y - 9.0f);
    const ImVec2 q2(center.x + 9.0f, center.y + 9.0f);
    const ImVec2 q3(center.x - 1.0f, center.y);

    // End vertical bar
    const ImVec2 bar_min(center.x - 15.0f, center.y - 9.0f);
    const ImVec2 bar_max(center.x - 12.5f, center.y + 9.0f);

    // Engraved shadows
    draw_list->AddTriangleFilled(ImVec2(p1.x + 1, p1.y + 1), ImVec2(p2.x + 1, p2.y + 1), ImVec2(p3.x + 1, p3.y + 1), kColEngravedShadow);
    draw_list->AddTriangleFilled(ImVec2(q1.x + 1, q1.y + 1), ImVec2(q2.x + 1, q2.y + 1), ImVec2(q3.x + 1, q3.y + 1), kColEngravedShadow);
    draw_list->AddRectFilled(ImVec2(bar_min.x + 1, bar_min.y + 1), ImVec2(bar_max.x + 1, bar_max.y + 1), kColEngravedShadow, 0.5f);

    // Blooms
    draw_list->AddTriangle(p1, p2, p3, bloom_col, 2.0f);
    draw_list->AddTriangle(q1, q2, q3, bloom_col, 2.0f);
    draw_list->AddRect(bar_min, bar_max, bloom_col, 0.5f, 0, 1.5f);

    // Cores
    draw_list->AddTriangleFilled(p1, p2, p3, core_col);
    draw_list->AddTriangleFilled(q1, q2, q3, core_col);
    draw_list->AddRectFilled(bar_min, bar_max, core_col, 0.5f);
}

void DrawNextPictogram(ImDrawList* draw_list, const ImVec2& center, bool is_seeking, bool hovered, bool pressed) {
    ImU32 core_col = (pressed || is_seeking) ? kColCyanGlowCore : (hovered ? kColCyanBright : kColCyanMuted);
    ImU32 bloom_col = (pressed || is_seeking) ? kColCyanBloomBright : kColCyanBloom;

    if (is_seeking) {
        // Fast pulsing cyan glow during forward seeking
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 22.0f);
        bloom_col = IM_COL32(0, 240, 255, static_cast<int>(120 + 130 * pulse));
    }

    // Right chevron 1
    const ImVec2 p1(center.x - 9.0f, center.y - 9.0f);
    const ImVec2 p2(center.x - 9.0f, center.y + 9.0f);
    const ImVec2 p3(center.x + 1.0f, center.y);

    // Right chevron 2
    const ImVec2 q1(center.x + 2.0f, center.y - 9.0f);
    const ImVec2 q2(center.x + 2.0f, center.y + 9.0f);
    const ImVec2 q3(center.x + 12.0f, center.y);

    // End vertical bar
    const ImVec2 bar_min(center.x + 12.5f, center.y - 9.0f);
    const ImVec2 bar_max(center.x + 15.0f, center.y + 9.0f);

    // Engraved shadows
    draw_list->AddTriangleFilled(ImVec2(p1.x + 1, p1.y + 1), ImVec2(p2.x + 1, p2.y + 1), ImVec2(p3.x + 1, p3.y + 1), kColEngravedShadow);
    draw_list->AddTriangleFilled(ImVec2(q1.x + 1, q1.y + 1), ImVec2(q2.x + 1, q2.y + 1), ImVec2(q3.x + 1, q3.y + 1), kColEngravedShadow);
    draw_list->AddRectFilled(ImVec2(bar_min.x + 1, bar_min.y + 1), ImVec2(bar_max.x + 1, bar_max.y + 1), kColEngravedShadow, 0.5f);

    // Blooms
    draw_list->AddTriangle(p1, p2, p3, bloom_col, 2.0f);
    draw_list->AddTriangle(q1, q2, q3, bloom_col, 2.0f);
    draw_list->AddRect(bar_min, bar_max, bloom_col, 0.5f, 0, 1.5f);

    // Cores
    draw_list->AddTriangleFilled(p1, p2, p3, core_col);
    draw_list->AddTriangleFilled(q1, q2, q3, core_col);
    draw_list->AddRectFilled(bar_min, bar_max, core_col, 0.5f);
}

void DrawEjectPictogram(ImDrawList* draw_list, const ImVec2& center, bool hovered, bool pressed) {
    const ImU32 core_col = pressed ? kColCyanGlowCore : (hovered ? kColCyanBright : kColCyanMuted);
    const ImU32 bloom_col = pressed ? kColCyanBloomBright : kColCyanBloom;

    // Upward triangle
    const ImVec2 t1(center.x - 10.0f, center.y + 1.0f);
    const ImVec2 t2(center.x + 10.0f, center.y + 1.0f);
    const ImVec2 t3(center.x, center.y - 10.0f);

    // Horizontal tray bar
    const ImVec2 bar_min(center.x - 10.0f, center.y + 5.0f);
    const ImVec2 bar_max(center.x + 10.0f, center.y + 8.5f);

    // Shadow
    draw_list->AddTriangleFilled(ImVec2(t1.x + 1, t1.y + 1), ImVec2(t2.x + 1, t2.y + 1), ImVec2(t3.x + 1, t3.y + 1), kColEngravedShadow);
    draw_list->AddRectFilled(ImVec2(bar_min.x + 1, bar_min.y + 1), ImVec2(bar_max.x + 1, bar_max.y + 1), kColEngravedShadow, 0.5f);

    // Bloom
    draw_list->AddTriangle(t1, t2, t3, bloom_col, 2.0f);
    draw_list->AddRect(bar_min, bar_max, bloom_col, 0.5f, 0, 1.5f);

    // Core
    draw_list->AddTriangleFilled(t1, t2, t3, core_col);
    draw_list->AddRectFilled(bar_min, bar_max, core_col, 0.5f);
}

void DrawShufflePictogram(ImDrawList* draw_list, const ImVec2& center, bool is_active, bool hovered, bool pressed) {
    const ImU32 core_col = (is_active || pressed) ? kColCyanGlowCore : (hovered ? kColCyanBright : kColCyanMuted);
    const ImU32 bloom_col = (is_active || pressed) ? kColCyanBloomBright : kColCyanBloom;

    // Crossing arrow paths
    const float y1 = center.y - 4.5f;
    const float y2 = center.y + 4.5f;
    const float x_start = center.x - 16.0f;
    const float x_mid1 = center.x - 6.0f;
    const float x_mid2 = center.x + 2.0f;
    const float x_end = center.x + 10.0f;

    // Laser shadow lines
    draw_list->AddLine(ImVec2(x_start + 1, y1 + 1), ImVec2(x_mid1 + 1, y1 + 1), kColEngravedShadow, 2.0f);
    draw_list->AddLine(ImVec2(x_mid1 + 1, y1 + 1), ImVec2(x_mid2 + 1, y2 + 1), kColEngravedShadow, 2.0f);
    draw_list->AddLine(ImVec2(x_mid2 + 1, y2 + 1), ImVec2(x_end + 1, y2 + 1), kColEngravedShadow, 2.0f);

    draw_list->AddLine(ImVec2(x_start + 1, y2 + 1), ImVec2(x_mid1 + 1, y2 + 1), kColEngravedShadow, 2.0f);
    draw_list->AddLine(ImVec2(x_mid1 + 1, y2 + 1), ImVec2(x_mid2 + 1, y1 + 1), kColEngravedShadow, 2.0f);
    draw_list->AddLine(ImVec2(x_mid2 + 1, y1 + 1), ImVec2(x_end + 1, y1 + 1), kColEngravedShadow, 2.0f);

    // Bloom lines
    draw_list->AddLine(ImVec2(x_start, y1), ImVec2(x_mid1, y1), bloom_col, 3.0f);
    draw_list->AddLine(ImVec2(x_mid1, y1), ImVec2(x_mid2, y2), bloom_col, 3.0f);
    draw_list->AddLine(ImVec2(x_mid2, y2), ImVec2(x_end, y2), bloom_col, 3.0f);

    draw_list->AddLine(ImVec2(x_start, y2), ImVec2(x_mid1, y2), bloom_col, 3.0f);
    draw_list->AddLine(ImVec2(x_mid1, y2), ImVec2(x_mid2, y1), bloom_col, 3.0f);
    draw_list->AddLine(ImVec2(x_mid2, y1), ImVec2(x_end, y1), bloom_col, 3.0f);

    // Core lines
    draw_list->AddLine(ImVec2(x_start, y1), ImVec2(x_mid1, y1), core_col, 1.8f);
    draw_list->AddLine(ImVec2(x_mid1, y1), ImVec2(x_mid2, y2), core_col, 1.8f);
    draw_list->AddLine(ImVec2(x_mid2, y2), ImVec2(x_end, y2), core_col, 1.8f);

    draw_list->AddLine(ImVec2(x_start, y2), ImVec2(x_mid1, y2), core_col, 1.8f);
    draw_list->AddLine(ImVec2(x_mid1, y2), ImVec2(x_mid2, y1), core_col, 1.8f);
    draw_list->AddLine(ImVec2(x_mid2, y1), ImVec2(x_end, y1), core_col, 1.8f);

    // Arrow heads
    draw_list->AddTriangleFilled(ImVec2(x_end, y1 - 3.5f), ImVec2(x_end, y1 + 3.5f), ImVec2(x_end + 4.5f, y1), core_col);
    draw_list->AddTriangleFilled(ImVec2(x_end, y2 - 3.5f), ImVec2(x_end, y2 + 3.5f), ImVec2(x_end + 4.5f, y2), core_col);

    // Active status mini-LED indicator dot on right side
    const ImVec2 led_center(center.x + 20.0f, center.y);
    draw_list->AddCircleFilled(led_center, 3.0f, IM_COL32(18, 20, 24, 255));
    if (is_active) {
        draw_list->AddCircle(led_center, 4.0f, kColCyanBloomBright, 0, 2.0f);
        draw_list->AddCircleFilled(led_center, 2.2f, kColCyanBright);
    } else {
        draw_list->AddCircleFilled(led_center, 2.0f, IM_COL32(0, 50, 65, 200));
    }
}

void DrawRepeatPictogram(ImDrawList* draw_list, const ImVec2& center, RepeatMode mode, bool hovered, bool pressed) {
    const bool is_active = (mode != RepeatMode::Off);
    const ImU32 core_col = (is_active || pressed) ? kColCyanGlowCore : (hovered ? kColCyanBright : kColCyanMuted);
    const ImU32 bloom_col = (is_active || pressed) ? kColCyanBloomBright : kColCyanBloom;

    // Looping arrows rectangle
    const ImVec2 loop_min(center.x - 14.0f, center.y - 5.5f);
    const ImVec2 loop_max(center.x + 6.0f, center.y + 5.5f);

    draw_list->AddRect(ImVec2(loop_min.x + 1, loop_min.y + 1), ImVec2(loop_max.x + 1, loop_max.y + 1), kColEngravedShadow, 3.0f, 0, 1.8f);
    draw_list->AddRect(loop_min, loop_max, bloom_col, 3.0f, 0, 2.5f);
    draw_list->AddRect(loop_min, loop_max, core_col, 3.0f, 0, 1.6f);

    // Top arrow head pointing right
    draw_list->AddTriangleFilled(ImVec2(loop_max.x - 4.0f, loop_min.y - 3.0f),
                                 ImVec2(loop_max.x - 4.0f, loop_min.y + 3.0f),
                                 ImVec2(loop_max.x + 1.0f, loop_min.y), core_col);

    // Bottom arrow head pointing left
    draw_list->AddTriangleFilled(ImVec2(loop_min.x + 4.0f, loop_max.y - 3.0f),
                                 ImVec2(loop_min.x + 4.0f, loop_max.y + 3.0f),
                                 ImVec2(loop_min.x - 1.0f, loop_max.y), core_col);

    // Mode status text: "1" or "ALL" on right side
    const ImVec2 text_pos(center.x + 12.0f, center.y - 7.0f);
    if (mode == RepeatMode::Track) {
        draw_list->AddText(text_pos, kColCyanGlowCore, "1");
    } else if (mode == RepeatMode::Disc) {
        draw_list->AddText(text_pos, kColCyanBright, "A");
    } else {
        draw_list->AddText(text_pos, IM_COL32(60, 70, 80, 180), "-");
    }
}

// -----------------------------------------------------------------------------
// Rotary Hardware Knobs (Volume and Balance)
// -----------------------------------------------------------------------------

void RenderHardwareKnob(
    const char* str_id,
    const char* label,
    float* value,
    float min_val,
    float max_val,
    float default_val,
    const ImVec2& center,
    float radius,
    bool is_balance
) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const ImVec2 widget_min(center.x - radius - 16.0f, center.y - radius - 14.0f);
    const ImVec2 widget_max(center.x + radius + 16.0f, center.y + radius + 26.0f);

    ImGui::SetCursorScreenPos(widget_min);
    ImGui::InvisibleButton(str_id, ImVec2(widget_max.x - widget_min.x, widget_max.y - widget_min.y));
    const bool is_hovered = ImGui::IsItemHovered();
    const bool is_active = ImGui::IsItemActive();

    // Interaction handling: drag, mouse wheel, double click reset
    if (is_active) {
        const float sensitivity = io.KeyShift ? 0.001f : 0.005f;
        const float delta = (-io.MouseDelta.y + io.MouseDelta.x) * sensitivity * (max_val - min_val);
        *value = std::clamp(*value + delta, min_val, max_val);
    }
    if (is_hovered && std::abs(io.MouseWheel) > 0.01f) {
        const float step = (max_val - min_val) * 0.04f * io.MouseWheel;
        *value = std::clamp(*value + step, min_val, max_val);
    }
    if (is_hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        *value = default_val;
    }

    // Normalized value [0.0, 1.0]
    const float norm = (*value - min_val) / (max_val - min_val);

    // Angle range: -135 degrees to +135 degrees (270 degree sweep, with 0 pointing straight up)
    constexpr float kMinAngle = -0.75f * kPi;
    constexpr float kMaxAngle = 0.75f * kPi;
    const float current_angle = kMinAngle + norm * (kMaxAngle - kMinAngle);

    // 1. Perimeter calibration ticks arc
    constexpr int kTotalTicks = 11;
    for (int i = 0; i < kTotalTicks; ++i) {
        const float t = static_cast<float>(i) / (kTotalTicks - 1);
        const float tick_angle = kMinAngle + t * (kMaxAngle - kMinAngle);
        const float cos_a = std::cos(tick_angle - 0.5f * kPi);
        const float sin_a = std::sin(tick_angle - 0.5f * kPi);

        const float r1 = radius + 4.5f;
        const float r2 = radius + ((i == 0 || i == kTotalTicks - 1 || (is_balance && i == (kTotalTicks / 2))) ? 8.5f : 6.5f);

        const ImVec2 p1(center.x + r1 * cos_a, center.y + r1 * sin_a);
        const ImVec2 p2(center.x + r2 * cos_a, center.y + r2 * sin_a);

        bool tick_lit = false;
        if (is_balance) {
            // For balance: center tick (detent) is lit if centered; otherwise lit towards current side
            if (i == (kTotalTicks / 2)) {
                tick_lit = (std::abs(*value) < 0.05f);
            } else if (*value < 0.0f) {
                tick_lit = (t <= 0.5f && tick_angle >= current_angle);
            } else {
                tick_lit = (t >= 0.5f && tick_angle <= current_angle);
            }
        } else {
            // For volume: ticks up to current level are lit
            tick_lit = (tick_angle <= current_angle + 0.02f);
        }

        const ImU32 tick_col = tick_lit
            ? ((is_balance && i == (kTotalTicks / 2)) ? kColCyanGlowCore : kColCyanBright)
            : IM_COL32(50, 54, 62, 200);

        if (tick_lit) {
            draw_list->AddLine(p1, p2, kColCyanBloom, 2.5f);
        }
        draw_list->AddLine(p1, p2, tick_col, (i == 0 || i == kTotalTicks - 1) ? 1.8f : 1.2f);
    }

    // 2. Knob base socket and drop shadow
    draw_list->AddCircleFilled(ImVec2(center.x + 1.5f, center.y + 2.0f), radius + 1.5f, IM_COL32(0, 0, 0, 160));
    draw_list->AddCircleFilled(center, radius + 1.0f, IM_COL32(18, 19, 22, 255));

    // 3. Knurled outer rim (36 radial teeth around perimeter)
    constexpr int kKnurlCount = 36;
    for (int k = 0; k < kKnurlCount; ++k) {
        const float angle = (static_cast<float>(k) / kKnurlCount) * 2.0f * kPi;
        const float ca = std::cos(angle);
        const float sa = std::sin(angle);
        const ImVec2 p1(center.x + (radius - 3.0f) * ca, center.y + (radius - 3.0f) * sa);
        const ImVec2 p2(center.x + radius * ca, center.y + radius * sa);
        const ImU32 knurl_col = (angle < kPi) ? IM_COL32(140, 145, 156, 200) : IM_COL32(35, 37, 44, 220);
        draw_list->AddLine(p1, p2, knurl_col, 1.2f);
    }

    // 4. Circular brushed metal face
    draw_list->AddCircleFilled(center, radius - 3.0f, is_hovered ? IM_COL32(110, 114, 124, 255) : IM_COL32(96, 99, 108, 255));

    // Concentric machined groove rings
    draw_list->AddCircle(center, radius - 5.5f, IM_COL32(70, 73, 80, 160), 0, 1.0f);
    draw_list->AddCircle(center, radius - 8.5f, IM_COL32(130, 135, 146, 120), 0, 1.0f);
    draw_list->AddCircle(center, radius - 11.5f, IM_COL32(65, 68, 75, 180), 0, 1.0f);

    // Overhead ambient light specular reflection (top edge highlight, bottom shadow)
    draw_list->AddCircle(center, radius - 3.0f, kColBevelTopLight, 0, 1.2f);

    // 5. Center recessed cap with milled bevel
    const float cap_r = radius * 0.46f;
    draw_list->AddCircleFilled(center, cap_r, IM_COL32(32, 33, 38, 255));
    draw_list->AddCircle(center, cap_r, IM_COL32(18, 19, 22, 255), 0, 1.2f);
    draw_list->AddCircle(center, cap_r - 1.0f, IM_COL32(68, 72, 82, 180), 0, 1.0f);

    // 6. Cyan backlit indicator pointer notch
    const float ptr_ca = std::cos(current_angle - 0.5f * kPi);
    const float ptr_sa = std::sin(current_angle - 0.5f * kPi);
    const ImVec2 ptr_start(center.x + (cap_r + 1.0f) * ptr_ca, center.y + (cap_r + 1.0f) * ptr_sa);
    const ImVec2 ptr_end(center.x + (radius - 3.5f) * ptr_ca, center.y + (radius - 3.5f) * ptr_sa);

    // Pointer bloom and core
    draw_list->AddLine(ptr_start, ptr_end, kColCyanBloomBright, 3.5f);
    draw_list->AddLine(ptr_start, ptr_end, kColCyanGlowCore, 1.8f);

    // 7. Labels underneath knob
    char val_buf[32];
    if (is_balance) {
        if (std::abs(*value) < 0.05f) {
            std::snprintf(val_buf, sizeof(val_buf), "CENTER");
        } else if (*value < 0.0f) {
            std::snprintf(val_buf, sizeof(val_buf), "L %d%%", static_cast<int>(std::abs(*value) * 100.0f));
        } else {
            std::snprintf(val_buf, sizeof(val_buf), "R %d%%", static_cast<int>(*value * 100.0f));
        }
    } else {
        std::snprintf(val_buf, sizeof(val_buf), "%d%%", static_cast<int>(*value * 100.0f));
    }

    const ImVec2 label_size = ImGui::CalcTextSize(label);
    const ImVec2 val_size = ImGui::CalcTextSize(val_buf);

    const ImVec2 label_pos(center.x - label_size.x * 0.5f, center.y + radius + 7.0f);
    const ImVec2 val_pos(center.x - val_size.x * 0.5f, center.y + radius + 18.0f);

    draw_list->AddText(label_pos, IM_COL32(185, 190, 205, 230), label);
    draw_list->AddText(val_pos, (is_hovered || is_active) ? kColCyanBright : IM_COL32(120, 130, 145, 210), val_buf);

    if (is_hovered) {
        ImGui::SetTooltip("%s: %s\n(Drag or scroll to adjust, double-click to reset)", label, val_buf);
    }
}

// -----------------------------------------------------------------------------
// Eject / Load Modal Dialog
// -----------------------------------------------------------------------------

void RenderEjectLoadModal(CdDisplayModel& model) {
    if (model.is_eject_dialog_open()) {
        ImGui::OpenPopup("Load / Eject Disc");
    }

    ImGui::SetNextWindowSize(ImVec2(540.0f, 380.0f), ImGuiCond_Appearing);

    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.12f, 0.12f, 0.14f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.38f, 0.42f, 0.50f, 0.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

    bool dialog_open = true;
    if (ImGui::BeginPopupModal("Load / Eject Disc", &dialog_open, ImGuiWindowFlags_NoResize)) {
        ImGui::TextColored(ImVec4(0.0f, 0.92f, 1.0f, 1.0f), "Hi-Fi Optical Transport & Media Loader");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::BeginTabBar("LoadOptionsTabBar")) {
            // Tab 1: Physical CD Drive
            if (ImGui::BeginTabItem("Physical CD")) {
                ImGui::Spacing();
                ImGui::Text("Optical Drive:");
                static int selected_drive = 0;
                const char* drives[] = {"/dev/sr0 [HL-DT-ST BD-RE WH16NS40]", "/dev/cdrom [Generic CD/DVD-ROM]"};
                ImGui::Combo("##DriveSelector", &selected_drive, drives, IM_ARRAYSIZE(drives));

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "[DISC READY] Audio CD detected (12 tracks, 47:32)");
                ImGui::Text("TOC Status: Valid Lead-in / CD-DA Audio Stream");
                ImGui::Spacing();

                if (ImGui::Button("Load Disc", ImVec2(130.0f, 32.0f))) {
                    const std::vector<TrackInfo> cd_tracks = {
                        {1, 222, "Speak to Me / Breathe"},
                        {2, 255, "On the Run"},
                        {3, 178, "Time"},
                        {4, 321, "The Great Gig in the Sky"},
                        {5, 192, "Money"},
                        {6, 288, "Us and Them"},
                        {7, 364, "Any Colour You Like"},
                        {8, 213, "Brain Damage"},
                        {9, 260, "Eclipse"},
                        {10, 165, "Shine On You Crazy Diamond (Part I)"},
                        {11, 310, "Welcome to the Machine"},
                        {12, 230, "Wish You Were Here"}
                    };
                    model.load_disc("The Dark Side of the Moon (CD-DA)", cd_tracks);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Eject Tray", ImVec2(120.0f, 32.0f))) {
                    model.stop();
                }

                ImGui::EndTabItem();
            }

            // Tab 2: Disk Image File (.cue, .iso, .toc, .bin)
            if (ImGui::BeginTabItem("Image from Disk")) {
                ImGui::Spacing();
                static char image_path[256] = "/home/music/images/AbbeyRoad.cue";
                ImGui::Text("Disc Image File (.cue, .iso, .toc, .bin, .nrg):");
                ImGui::InputText("##ImagePath", image_path, sizeof(image_path));

                ImGui::Spacing();
                ImGui::Text("Quick Select Sample Images:");
                if (ImGui::Button("Dark Side of the Moon (.cue)")) {
                    std::snprintf(image_path, sizeof(image_path), "/music/PinkFloyd_DarkSide.cue");
                }
                ImGui::SameLine();
                if (ImGui::Button("Abbey Road (.cue)")) {
                    std::snprintf(image_path, sizeof(image_path), "/music/TheBeatles_AbbeyRoad.cue");
                }
                if (ImGui::Button("Wish You Were Here (.toc)")) {
                    std::snprintf(image_path, sizeof(image_path), "/music/PinkFloyd_WYWH.toc");
                }
                ImGui::SameLine();
                if (ImGui::Button("Kind of Blue (.bin)")) {
                    std::snprintf(image_path, sizeof(image_path), "/music/MilesDavis_KindBlue.bin");
                }

                ImGui::Spacing();
                if (ImGui::Button("Load Image File", ImVec2(150.0f, 32.0f))) {
                    const std::vector<TrackInfo> image_tracks = {
                        {1, 260, "Come Together"},
                        {2, 182, "Something"},
                        {3, 207, "Maxwell's Silver Hammer"},
                        {4, 196, "Oh! Darling"},
                        {5, 171, "Octopus's Garden"},
                        {6, 467, "I Want You (She's So Heavy)"},
                        {7, 185, "Here Comes the Sun"},
                        {8, 165, "Because"},
                        {9, 243, "You Never Give Me Your Money"},
                        {10, 146, "Sun King"},
                        {11, 66, "Mean Mr. Mustard"},
                        {12, 73, "Polythene Pam"},
                        {13, 91, "She Came In Through the Bathroom Window"},
                        {14, 91, "Golden Slumbers"},
                        {15, 96, "Carry That Weight"},
                        {16, 141, "The End"},
                        {17, 23, "Her Majesty"}
                    };
                    model.load_disc("The Beatles - Abbey Road (CUE/FLAC)", image_tracks);
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndTabItem();
            }

            // Tab 3: CD Library
            if (ImGui::BeginTabItem("CD Library")) {
                ImGui::Spacing();
                static char search_buf[64] = "";
                ImGui::InputTextWithHint("##LibSearch", "Search library albums or artists...", search_buf, sizeof(search_buf));

                static int selected_album = 0;
                const char* library_albums[] = {
                    "Pink Floyd - The Dark Side of the Moon (1973) [12 Tracks]",
                    "The Beatles - Abbey Road (1969) [17 Tracks]",
                    "Pink Floyd - Wish You Were Here (1975) [5 Tracks]",
                    "Miles Davis - Kind of Blue (1959) [5 Tracks]",
                    "Daft Punk - Random Access Memories (2013) [13 Tracks]",
                    "Fleetwood Mac - Rumours (1977) [11 Tracks]"
                };

                ImGui::Spacing();
                ImGui::ListBox("##LibraryList", &selected_album, library_albums, IM_ARRAYSIZE(library_albums), 5);

                ImGui::Spacing();
                if (ImGui::Button("Load Selected Album", ImVec2(170.0f, 32.0f))) {
                    if (selected_album == 1) {
                        const std::vector<TrackInfo> abbey_road = {
                            {1, 260, "Come Together"},
                            {2, 182, "Something"},
                            {3, 207, "Maxwell's Silver Hammer"},
                            {4, 196, "Oh! Darling"},
                            {5, 171, "Octopus's Garden"},
                            {6, 467, "I Want You (She's So Heavy)"},
                            {7, 185, "Here Comes the Sun"},
                            {8, 165, "Because"},
                            {9, 243, "You Never Give Me Your Money"}
                        };
                        model.load_disc("The Beatles - Abbey Road", abbey_road);
                    } else if (selected_album == 2) {
                        const std::vector<TrackInfo> wywh = {
                            {1, 810, "Shine On You Crazy Diamond (Parts I-V)"},
                            {2, 452, "Welcome to the Machine"},
                            {3, 308, "Have a Cigar"},
                            {4, 340, "Wish You Were Here"},
                            {5, 748, "Shine On You Crazy Diamond (Parts VI-IX)"}
                        };
                        model.load_disc("Pink Floyd - Wish You Were Here", wywh);
                    } else if (selected_album == 3) {
                        const std::vector<TrackInfo> kind_of_blue = {
                            {1, 562, "So What"},
                            {2, 584, "Freddie Freeloader"},
                            {3, 592, "Blue in Green"},
                            {4, 693, "All Blues"},
                            {5, 565, "Flamenco Sketches"}
                        };
                        model.load_disc("Miles Davis - Kind of Blue", kind_of_blue);
                    } else if (selected_album == 4) {
                        const std::vector<TrackInfo> ram = {
                            {1, 274, "Give Life Back to Music"},
                            {2, 321, "The Game of Love"},
                            {3, 544, "Giorgio by Moroder"},
                            {4, 228, "Within"},
                            {5, 335, "Instant Crush"},
                            {6, 353, "Lose Yourself to Dance"},
                            {7, 405, "Touch"},
                            {8, 368, "Get Lucky"}
                        };
                        model.load_disc("Daft Punk - Random Access Memories", ram);
                    } else {
                        const std::vector<TrackInfo> default_tracks = {
                            {1, 222, "Speak to Me / Breathe"},
                            {2, 255, "On the Run"},
                            {3, 178, "Time"},
                            {4, 321, "The Great Gig in the Sky"},
                            {5, 192, "Money"},
                            {6, 288, "Us and Them"},
                            {7, 364, "Any Colour You Like"},
                            {8, 213, "Brain Damage"},
                            {9, 260, "Eclipse"}
                        };
                        model.load_disc("The Dark Side of the Moon", default_tracks);
                    }
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(100.0f, 28.0f))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    } else {
        model.close_eject_dialog();
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

} // namespace

void RenderCdControls(CdDisplayModel& model, const ImVec2& size) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();

    const ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    const float panel_width = (size.x > 0.0f) ? size.x : 680.0f;
    const float panel_height = (size.y > 0.0f) ? size.y : 116.0f;

    const ImVec2 panel_min = cursor_pos;
    const ImVec2 panel_max(cursor_pos.x + panel_width, cursor_pos.y + panel_height);

    // Chassis bezel framing matching Phase 2.1 display and 2.3 volume meter
    GetBrushedMetal().RenderPanelChassis(draw_list, panel_min, panel_max, 6.0f);

    // Layout geometry
    constexpr float kNormalBtnSize = 56.0f;
    constexpr float kHalfBtnWidth = 56.0f;
    constexpr float kHalfBtnHeight = 25.0f;
    constexpr float kBtnGap = 8.0f;
    constexpr float kKnobRadius = 23.0f;

    const float btn_start_y = panel_min.y + (panel_height - kNormalBtnSize) * 0.5f;
    float current_x = panel_min.x + 18.0f;

    // Helper lambda for rendering a standard hardware button
    auto RenderHardwareButton = [&](const char* id, const ImVec2& pos, const ImVec2& btn_sz, auto draw_pictogram) {
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton(id, btn_sz);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();

        const ImVec2 socket_min(pos.x - 2.0f, pos.y - 2.0f);
        const ImVec2 socket_max(pos.x + btn_sz.x + 2.0f, pos.y + btn_sz.y + 2.0f);
        DrawHardwareButtonSocket(draw_list, socket_min, socket_max);

        const ImVec2 offset = active ? ImVec2(1.5f, 1.5f) : ImVec2(0.0f, 0.0f);
        const ImVec2 b_min(pos.x + offset.x, pos.y + offset.y);
        const ImVec2 b_max(pos.x + btn_sz.x + offset.x, pos.y + btn_sz.y + offset.y);

        DrawButtonBrushedFace(draw_list, b_min, b_max, hovered, active);

        const ImVec2 icon_center((b_min.x + b_max.x) * 0.5f, (b_min.y + b_max.y) * 0.5f);
        draw_pictogram(icon_center, hovered, active);

        return ImGui::IsItemClicked(ImGuiMouseButton_Left);
    };

    // 1. Eject (load) button
    const ImVec2 eject_pos(current_x, btn_start_y);
    if (RenderHardwareButton("##BtnEject", eject_pos, ImVec2(kNormalBtnSize, kNormalBtnSize),
                            [&](const ImVec2& c, bool h, bool a) {
                                DrawEjectPictogram(draw_list, c, h, a);
                            })) {
        model.open_eject_dialog();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Eject / Load Disc (Physical CD, Disk Image, or Library)");
    }
    current_x += kNormalBtnSize + kBtnGap;

    // 2. Prev track button (seek backward on long-press)
    const ImVec2 prev_pos(current_x, btn_start_y);
    ImGui::SetCursorScreenPos(prev_pos);
    ImGui::InvisibleButton("##BtnPrev", ImVec2(kNormalBtnSize, kNormalBtnSize));
    const bool prev_hovered = ImGui::IsItemHovered();
    const bool prev_active = ImGui::IsItemActive();

    // Long press tracking for Prev
    if (prev_active) {
        if (!s_prev_tracker.is_held) {
            s_prev_tracker.is_held = true;
            s_prev_tracker.press_start_time = ImGui::GetTime();
            s_prev_tracker.did_long_press = false;
        } else {
            const double held_time = ImGui::GetTime() - s_prev_tracker.press_start_time;
            if (held_time > 0.38) {
                s_prev_tracker.did_long_press = true;
                // Fast seek backward: 10 seconds per second
                model.seek_backward(10.0 * static_cast<double>(io.DeltaTime));
            }
        }
    } else {
        if (s_prev_tracker.is_held) {
            if (!s_prev_tracker.did_long_press) {
                // Short click triggered prev track
                model.prev_track();
            }
            s_prev_tracker.is_held = false;
            s_prev_tracker.did_long_press = false;
        }
    }

    const ImVec2 prev_sock_min(prev_pos.x - 2.0f, prev_pos.y - 2.0f);
    const ImVec2 prev_sock_max(prev_pos.x + kNormalBtnSize + 2.0f, prev_pos.y + kNormalBtnSize + 2.0f);
    DrawHardwareButtonSocket(draw_list, prev_sock_min, prev_sock_max);

    const ImVec2 prev_offset = prev_active ? ImVec2(1.5f, 1.5f) : ImVec2(0.0f, 0.0f);
    const ImVec2 p_min(prev_pos.x + prev_offset.x, prev_pos.y + prev_offset.y);
    const ImVec2 p_max(prev_pos.x + kNormalBtnSize + prev_offset.x, prev_pos.y + kNormalBtnSize + prev_offset.y);
    DrawButtonBrushedFace(draw_list, p_min, p_max, prev_hovered, prev_active);

    const ImVec2 prev_center((p_min.x + p_max.x) * 0.5f, (p_min.y + p_max.y) * 0.5f);
    DrawPrevPictogram(draw_list, prev_center, s_prev_tracker.did_long_press, prev_hovered, prev_active);
    if (prev_hovered) {
        ImGui::SetTooltip("Previous Track (Hold to seek backward)");
    }
    current_x += kNormalBtnSize + kBtnGap;

    // 3. Play / Pause button (> ||)
    const ImVec2 play_pos(current_x, btn_start_y);
    if (RenderHardwareButton("##BtnPlayPause", play_pos, ImVec2(kNormalBtnSize, kNormalBtnSize),
                            [&](const ImVec2& c, bool h, bool a) {
                                DrawPlayPausePictogram(draw_list, c, model.is_playing(), h, a);
                            })) {
        model.toggle_play_pause();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s (Space)", model.is_playing() ? "Pause" : "Play");
    }
    current_x += kNormalBtnSize + kBtnGap;

    // 4. Stop button
    const ImVec2 stop_pos(current_x, btn_start_y);
    if (RenderHardwareButton("##BtnStop", stop_pos, ImVec2(kNormalBtnSize, kNormalBtnSize),
                            [&](const ImVec2& c, bool h, bool a) {
                                DrawStopPictogram(draw_list, c, h, a);
                            })) {
        model.stop();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Stop Playback");
    }
    current_x += kNormalBtnSize + kBtnGap;

    // 5. Next track button (seek forward on long-press)
    const ImVec2 next_pos(current_x, btn_start_y);
    ImGui::SetCursorScreenPos(next_pos);
    ImGui::InvisibleButton("##BtnNext", ImVec2(kNormalBtnSize, kNormalBtnSize));
    const bool next_hovered = ImGui::IsItemHovered();
    const bool next_active = ImGui::IsItemActive();

    // Long press tracking for Next
    if (next_active) {
        if (!s_next_tracker.is_held) {
            s_next_tracker.is_held = true;
            s_next_tracker.press_start_time = ImGui::GetTime();
            s_next_tracker.did_long_press = false;
        } else {
            const double held_time = ImGui::GetTime() - s_next_tracker.press_start_time;
            if (held_time > 0.38) {
                s_next_tracker.did_long_press = true;
                // Fast seek forward: 10 seconds per second
                model.seek_forward(10.0 * static_cast<double>(io.DeltaTime));
            }
        }
    } else {
        if (s_next_tracker.is_held) {
            if (!s_next_tracker.did_long_press) {
                // Short click triggered next track
                model.next_track();
            }
            s_next_tracker.is_held = false;
            s_next_tracker.did_long_press = false;
        }
    }

    const ImVec2 next_sock_min(next_pos.x - 2.0f, next_pos.y - 2.0f);
    const ImVec2 next_sock_max(next_pos.x + kNormalBtnSize + 2.0f, next_pos.y + kNormalBtnSize + 2.0f);
    DrawHardwareButtonSocket(draw_list, next_sock_min, next_sock_max);

    const ImVec2 next_offset = next_active ? ImVec2(1.5f, 1.5f) : ImVec2(0.0f, 0.0f);
    const ImVec2 n_min(next_pos.x + next_offset.x, next_pos.y + next_offset.y);
    const ImVec2 n_max(next_pos.x + kNormalBtnSize + next_offset.x, next_pos.y + kNormalBtnSize + next_offset.y);
    DrawButtonBrushedFace(draw_list, n_min, n_max, next_hovered, next_active);

    const ImVec2 next_center((n_min.x + n_max.x) * 0.5f, (n_min.y + n_max.y) * 0.5f);
    DrawNextPictogram(draw_list, next_center, s_next_tracker.did_long_press, next_hovered, next_active);
    if (next_hovered) {
        ImGui::SetTooltip("Next Track (Hold to seek forward)");
    }
    current_x += kNormalBtnSize + 12.0f;

    // 6. Two smaller half-size buttons one on top of the other: Shuffle and Repeat
    const ImVec2 shuffle_pos(current_x, btn_start_y);
    const ImVec2 repeat_pos(current_x, btn_start_y + kHalfBtnHeight + 6.0f);

    // Shuffle button
    if (RenderHardwareButton("##BtnShuffle", shuffle_pos, ImVec2(kHalfBtnWidth, kHalfBtnHeight),
                            [&](const ImVec2& c, bool h, bool a) {
                                DrawShufflePictogram(draw_list, c, model.is_shuffle(), h, a);
                            })) {
        model.toggle_shuffle();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Shuffle Mode [S]: %s", model.is_shuffle() ? "ON" : "OFF");
    }

    // Repeat button
    if (RenderHardwareButton("##BtnRepeat", repeat_pos, ImVec2(kHalfBtnWidth, kHalfBtnHeight),
                            [&](const ImVec2& c, bool h, bool a) {
                                DrawRepeatPictogram(draw_list, c, model.repeat_mode(), h, a);
                            })) {
        model.toggle_repeat();
    }
    if (ImGui::IsItemHovered()) {
        const char* rep_str = (model.repeat_mode() == RepeatMode::Off)
            ? "OFF" : ((model.repeat_mode() == RepeatMode::Disc) ? "DISC" : "TRACK");
        ImGui::SetTooltip("Repeat Mode [R]: %s", rep_str);
    }
    current_x += kHalfBtnWidth + 20.0f;

    // Vertical precision divider line separating buttons and knobs
    const float div_x = current_x - 4.0f;
    draw_list->AddLine(ImVec2(div_x, panel_min.y + 16.0f), ImVec2(div_x, panel_max.y - 16.0f), IM_COL32(20, 22, 26, 255), 1.0f);
    draw_list->AddLine(ImVec2(div_x + 1.0f, panel_min.y + 16.0f), ImVec2(div_x + 1.0f, panel_max.y - 16.0f), IM_COL32(75, 80, 92, 140), 1.0f);
    current_x += 16.0f;

    // 7. Two round knobs next to each other: Volume and Balance
    const float knob_center_y = panel_min.y + 44.0f;

    // Volume Knob
    const ImVec2 vol_center(current_x + kKnobRadius + 14.0f, knob_center_y);
    float vol_val = static_cast<float>(model.volume());
    RenderHardwareKnob("##KnobVolume", "VOLUME", &vol_val, 0.0f, 1.0f, 0.8f, vol_center, kKnobRadius, false);
    if (static_cast<double>(vol_val) != model.volume()) {
        model.set_volume(static_cast<double>(vol_val));
    }
    current_x += (kKnobRadius * 2.0f) + 48.0f;

    // Balance Knob
    const ImVec2 bal_center(current_x + kKnobRadius + 14.0f, knob_center_y);
    float bal_val = static_cast<float>(model.balance());
    RenderHardwareKnob("##KnobBalance", "BALANCE", &bal_val, -1.0f, 1.0f, 0.0f, bal_center, kKnobRadius, true);
    if (static_cast<double>(bal_val) != model.balance()) {
        model.set_balance(static_cast<double>(bal_val));
    }

    // Modal Eject/Load dialog
    RenderEjectLoadModal(model);

    // Advance cursor past the controls widget
    ImGui::SetCursorScreenPos(ImVec2(panel_min.x, panel_max.y + 8.0f));
    ImGui::Dummy(ImVec2(panel_width, 0.0f));
}

} // namespace mycds::gui
