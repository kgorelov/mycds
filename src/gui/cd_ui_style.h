#pragma once

#include "imgui.h"

namespace mycds::gui {

// Shared hardware UI color palette for sockets, brushed-metal buttons, bevels, and cyan backlight
inline constexpr ImU32 kColSocketBg = IM_COL32(14, 15, 18, 255);
inline constexpr ImU32 kColSocketInnerShadow = IM_COL32(6, 6, 8, 240);
inline constexpr ImU32 kColSocketBezelLight = IM_COL32(46, 49, 56, 140);

inline constexpr ImU32 kColBtnMetalBase = IM_COL32(74, 77, 84, 255);
inline constexpr ImU32 kColBtnMetalHover = IM_COL32(90, 94, 102, 255);
inline constexpr ImU32 kColBtnMetalActive = IM_COL32(56, 58, 64, 255);

inline constexpr ImU32 kColBevelTopLight = IM_COL32(165, 172, 188, 220);
inline constexpr ImU32 kColBevelLeftLight = IM_COL32(130, 136, 148, 180);
inline constexpr ImU32 kColBevelBottomShadow = IM_COL32(22, 24, 28, 255);
inline constexpr ImU32 kColBevelRightShadow = IM_COL32(36, 38, 44, 190);

inline constexpr ImU32 kColBevelDepressedTop = IM_COL32(16, 17, 20, 255);
inline constexpr ImU32 kColBevelDepressedLeft = IM_COL32(22, 24, 28, 220);
inline constexpr ImU32 kColBevelDepressedBottom = IM_COL32(135, 140, 155, 170);
inline constexpr ImU32 kColBevelDepressedRight = IM_COL32(110, 115, 126, 140);

inline constexpr ImU32 kColCyanGlowCore = IM_COL32(180, 248, 255, 230);
inline constexpr ImU32 kColCyanBright = IM_COL32(0, 230, 255, 210);
inline constexpr ImU32 kColCyanMuted = IM_COL32(0, 155, 190, 140);
inline constexpr ImU32 kColCyanBloomBase = IM_COL32(0, 230, 255, 160);

/// Draws a recessed socket frame behind a hardware button.
void DrawHardwareButtonSocket(ImDrawList* draw_list, const ImVec2& socket_min, const ImVec2& socket_max);

/// Draws a brushed-titanium hardware button face with 3D machined bevels.
void DrawButtonBrushedFace(ImDrawList* draw_list, const ImVec2& min_pos, const ImVec2& max_pos,
                           bool hovered, bool pressed);

/// Draws a small chassis screw/bolt matching the CD player panel corners.
void DrawChassisScrew(ImDrawList* draw_list, const ImVec2& pos);

/// Softly diffused cyan-backlit triangle primitive.
void DrawDiffusedTriangle(ImDrawList* draw_list, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3,
                          ImU32 core_col, bool is_bright);

/// Softly diffused cyan-backlit rectangle primitive (filled).
void DrawDiffusedRect(ImDrawList* draw_list, const ImVec2& min_pos, const ImVec2& max_pos, float rounding,
                      ImU32 core_col, bool is_bright);

/// Softly diffused cyan-backlit hollow rectangle outline primitive.
void DrawDiffusedRectOutline(ImDrawList* draw_list, const ImVec2& min_pos, const ImVec2& max_pos, float rounding,
                             ImU32 core_col, float core_thickness, bool is_bright);

/// Softly diffused cyan-backlit line primitive.
void DrawDiffusedLine(ImDrawList* draw_list, const ImVec2& p1, const ImVec2& p2,
                      ImU32 core_col, float core_thickness, bool is_bright);

/// Draws a cyan-backlit hardware button text label with recessed drop shadow,
/// optional glow bloom, and bold double-pass stroke at the given position.
void DrawHardwareButtonLabel(ImDrawList* draw_list, const char* label, const ImVec2& text_pos,
                             ImU32 text_col, bool is_active, bool is_bright,
                             float font_size = 18.0f);

/// Renders a tactile brushed-metal hardware button with a centered cyan-backlit label
/// and an optional active LED bar on the left edge. Returns true when clicked.
bool RenderHardwareTextButton(const char* str_id, const char* label,
                              const ImVec2& pos, const ImVec2& size,
                              bool is_active = false, bool enabled = true);

} // namespace mycds::gui
