#include "cd_ui_style.h"

#include <cmath>

#include "cd_brushed_metal.h"

namespace mycds::gui {

void DrawHardwareButtonSocket(ImDrawList* draw_list, const ImVec2& socket_min, const ImVec2& socket_max) {
    if (!draw_list) {
        return;
    }
    draw_list->AddRectFilled(socket_min, socket_max, kColSocketBg, 3.5f);
    draw_list->AddLine(ImVec2(socket_min.x, socket_min.y), ImVec2(socket_max.x, socket_min.y), kColSocketInnerShadow, 1.0f);
    draw_list->AddLine(ImVec2(socket_min.x, socket_min.y), ImVec2(socket_min.x, socket_max.y), kColSocketInnerShadow, 1.0f);
    draw_list->AddLine(ImVec2(socket_min.x, socket_max.y), ImVec2(socket_max.x, socket_max.y), kColSocketBezelLight, 1.0f);
    draw_list->AddLine(ImVec2(socket_max.x, socket_min.y), ImVec2(socket_max.x, socket_max.y), kColSocketBezelLight, 1.0f);
}

void DrawButtonBrushedFace(ImDrawList* draw_list, const ImVec2& min_pos, const ImVec2& max_pos,
                           bool hovered, bool pressed) {
    if (!draw_list) {
        return;
    }
    const ImU32 base_col = pressed ? kColBtnMetalActive : (hovered ? kColBtnMetalHover : kColBtnMetalBase);

    // Solid titanium/gunmetal button face
    draw_list->AddRectFilled(min_pos, max_pos, base_col, 2.5f);

    // Realistic brushed titanium micro-hairlines: Sample pre-baked grain texture in 1 quad
    if (GetBrushedMetal().IsInitialized() && GetBrushedMetal().GetButtonTextureId() != 0) {
        const float w = max_pos.x - min_pos.x;
        const float h = max_pos.y - min_pos.y;
        constexpr float grain_size = 64.0f;
        const float u0 = std::floor(min_pos.x * 0.5f) / grain_size;
        const float v0 = std::floor(min_pos.y) / grain_size;
        const float u1 = u0 + (w / grain_size);
        const float v1 = v0 + (h / grain_size);

        draw_list->AddImageRounded(
            GetBrushedMetal().GetButtonTextureRef(),
            min_pos, max_pos,
            ImVec2(u0, v0), ImVec2(u1, v1),
            IM_COL32_WHITE, 2.5f
        );
    }

    // Overhead ambient gradient
    draw_list->AddRectFilledMultiColor(
        min_pos, max_pos,
        IM_COL32(255, 255, 255, 14),
        IM_COL32(255, 255, 255, 14),
        IM_COL32(0, 0, 0, 24),
        IM_COL32(0, 0, 0, 24)
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

void DrawChassisScrew(ImDrawList* draw_list, const ImVec2& pos) {
    if (!draw_list) {
        return;
    }
    draw_list->AddCircleFilled(pos, 3.0f, IM_COL32(40, 40, 44, 255));
    draw_list->AddCircle(pos, 3.0f, IM_COL32(70, 70, 76, 255), 10, 1.0f);
    draw_list->AddLine(ImVec2(pos.x - 1.8f, pos.y), ImVec2(pos.x + 1.8f, pos.y), IM_COL32(20, 20, 22, 255), 1.0f);
}

void DrawDiffusedTriangle(ImDrawList* draw_list, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3,
                          ImU32 core_col, bool is_bright) {
    if (!draw_list) {
        return;
    }

    const float cross = (p2.x - p1.x) * (p3.y - p2.y) - (p2.y - p1.y) * (p3.x - p2.x);
    const ImVec2 a = p1;
    const ImVec2 b = (cross < 0.0f) ? p3 : p2;
    const ImVec2 c = (cross < 0.0f) ? p2 : p3;

    draw_list->AddTriangleFilled(ImVec2(a.x + 1.0f, a.y + 1.2f),
                                 ImVec2(b.x + 1.0f, b.y + 1.2f),
                                 ImVec2(c.x + 1.0f, c.y + 1.2f),
                                 IM_COL32(10, 14, 20, 80));

    const ImU32 bloom_outer = is_bright ? IM_COL32(0, 220, 255, 35) : IM_COL32(0, 180, 220, 20);
    const ImU32 bloom_mid = is_bright ? IM_COL32(0, 230, 255, 75) : IM_COL32(0, 190, 230, 45);
    const ImU32 bloom_inner = is_bright ? IM_COL32(60, 240, 255, 140) : IM_COL32(20, 195, 230, 80);

    draw_list->AddTriangle(a, b, c, bloom_outer, 3.5f);
    draw_list->AddTriangle(a, b, c, bloom_mid, 2.0f);
    draw_list->AddTriangle(a, b, c, bloom_inner, 1.0f);

    draw_list->AddTriangleFilled(a, b, c, core_col);
}

void DrawDiffusedRect(ImDrawList* draw_list, const ImVec2& min_pos, const ImVec2& max_pos, float rounding,
                      ImU32 core_col, bool is_bright) {
    if (!draw_list) {
        return;
    }

    draw_list->AddRectFilled(ImVec2(min_pos.x + 1.0f, min_pos.y + 1.2f),
                             ImVec2(max_pos.x + 1.0f, max_pos.y + 1.2f),
                             IM_COL32(10, 14, 20, 80), rounding);

    const ImU32 bloom_outer = is_bright ? IM_COL32(0, 220, 255, 35) : IM_COL32(0, 180, 220, 20);
    const ImU32 bloom_mid = is_bright ? IM_COL32(0, 230, 255, 75) : IM_COL32(0, 190, 230, 45);
    const ImU32 bloom_inner = is_bright ? IM_COL32(60, 240, 255, 140) : IM_COL32(20, 195, 230, 80);

    draw_list->AddRect(min_pos, max_pos, bloom_outer, rounding, 0, 4.5f);
    draw_list->AddRect(min_pos, max_pos, bloom_mid, rounding, 0, 2.5f);
    draw_list->AddRect(min_pos, max_pos, bloom_inner, rounding, 0, 1.2f);

    draw_list->AddRectFilled(min_pos, max_pos, core_col, rounding);
}

void DrawDiffusedLine(ImDrawList* draw_list, const ImVec2& p1, const ImVec2& p2,
                      ImU32 core_col, float core_thickness, bool is_bright) {
    if (!draw_list) {
        return;
    }

    draw_list->AddLine(ImVec2(p1.x + 1.0f, p1.y + 1.2f), ImVec2(p2.x + 1.0f, p2.y + 1.2f),
                       IM_COL32(10, 14, 20, 80), core_thickness);

    const ImU32 bloom_outer = is_bright ? IM_COL32(0, 220, 255, 35) : IM_COL32(0, 180, 220, 20);
    const ImU32 bloom_mid = is_bright ? IM_COL32(0, 230, 255, 75) : IM_COL32(0, 190, 230, 45);

    draw_list->AddLine(p1, p2, bloom_outer, core_thickness + 3.8f);
    draw_list->AddLine(p1, p2, bloom_mid, core_thickness + 1.8f);
    draw_list->AddLine(p1, p2, core_col, core_thickness);
}

bool RenderHardwareTextButton(const char* str_id, const char* label,
                              const ImVec2& pos, const ImVec2& size,
                              bool is_active, bool enabled) {
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

    // Active indicator LED bar on the left edge of the button face
    if (is_active) {
        const ImVec2 bar_min(b_min.x + 6.0f, b_min.y + 8.0f);
        const ImVec2 bar_max(b_min.x + 10.0f, b_max.y - 8.0f);
        DrawDiffusedRect(draw_list, bar_min, bar_max, 1.5f, kColCyanGlowCore, true);
    }

    const ImU32 text_col = !enabled
        ? IM_COL32(90, 96, 106, 160)
        : ((is_active || pressed)
            ? kColCyanGlowCore
            : (hovered ? kColCyanBright : IM_COL32(110, 210, 235, 235)));

    ImFont* font = ImGui::GetFont();
    constexpr float kBtnFontSize = 18.0f;
    const ImVec2 text_size = font->CalcTextSizeA(kBtnFontSize, 10000.0f, 0.0f, label);
    const ImVec2 text_pos(
        b_min.x + (size.x - text_size.x) * 0.5f,
        b_min.y + (size.y - text_size.y) * 0.5f
    );

    // Subtle recessed drop shadow
    draw_list->AddText(font, kBtnFontSize, ImVec2(text_pos.x + 1.0f, text_pos.y + 1.2f),
                       IM_COL32(8, 10, 14, 180), label);

    // Soft cyan backlight bloom for active/hovered states
    if (enabled && (is_active || hovered || pressed)) {
        const ImU32 bloom_col = is_active ? IM_COL32(0, 230, 255, 70) : IM_COL32(0, 220, 255, 40);
        draw_list->AddText(font, kBtnFontSize, ImVec2(text_pos.x - 0.6f, text_pos.y), bloom_col, label);
        draw_list->AddText(font, kBtnFontSize, ImVec2(text_pos.x + 1.2f, text_pos.y), bloom_col, label);
    }

    // Bold stroke: double-pass with slight horizontal offset
    draw_list->AddText(font, kBtnFontSize, text_pos, text_col, label);
    draw_list->AddText(font, kBtnFontSize, ImVec2(text_pos.x + 0.65f, text_pos.y), text_col, label);

    return enabled && ImGui::IsItemClicked(ImGuiMouseButton_Left);
}

} // namespace mycds::gui
