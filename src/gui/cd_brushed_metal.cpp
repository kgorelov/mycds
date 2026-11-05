#include "cd_brushed_metal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mycds::gui {

namespace {

CdBrushedMetal g_brushed_metal_instance;

constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kPi = 3.14159265358979323846f;

float Smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

CdBrushedMetal::~CdBrushedMetal() {
    Shutdown();
}

std::vector<RgbaPixel> CdBrushedMetal::GeneratePixels(int width, int height, uint32_t seed) {
    if (width <= 0 || height <= 0) {
        return {};
    }

    std::vector<RgbaPixel> pixels(width * height);

    uint32_t state = seed;
    auto lcg = [&state]() -> float {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state) / 4294967296.0f;
    };

    // 1. Periodic vertical row baseline roughness
    constexpr int kKy = 32;
    std::vector<float> grid_y(kKy);
    for (int k = 0; k < kKy; ++k) {
        grid_y[k] = (lcg() - 0.5f) * 11.0f;
    }

    std::vector<float> row_base(height);
    const float step_y = static_cast<float>(height) / static_cast<float>(kKy);
    for (int y = 0; y < height; ++y) {
        const float pos_y = static_cast<float>(y) / step_y;
        const int k0 = static_cast<int>(pos_y) % kKy;
        const int k1 = (k0 + 1) % kKy;
        const float t = pos_y - std::floor(pos_y);
        row_base[y] = (1.0f - Smoothstep(t)) * grid_y[k0] + Smoothstep(t) * grid_y[k1];
    }

    // 2. Grids for horizontal streaks of 3 scales (seamlessly periodic along X)
    constexpr int kK1 = 8;
    constexpr int kK2 = 16;
    constexpr int kK3 = 32;

    std::vector<float> g1(height * kK1);
    std::vector<float> g2(height * kK2);
    std::vector<float> g3(height * kK3);

    for (int y = 0; y < height; ++y) {
        for (int k = 0; k < kK1; ++k) {
            g1[y * kK1 + k] = (lcg() - 0.5f) * 10.0f;
        }
        for (int k = 0; k < kK2; ++k) {
            g2[y * kK2 + k] = (lcg() - 0.5f) * 7.5f;
        }
        for (int k = 0; k < kK3; ++k) {
            g3[y * kK3 + k] = (lcg() - 0.5f) * 5.0f;
        }
    }

    // 3. High-frequency micro-scratches buffer (filtered horizontally with periodic wrap)
    std::vector<float> micro(height * width);
    std::vector<float> row_scratch(width);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            row_scratch[x] = (lcg() - 0.5f) * 8.0f;
        }
        for (int x = 0; x < width; ++x) {
            const int prev_x = (x - 1 + width) % width;
            const int next_x = (x + 1) % width;
            micro[y * width + x] = 0.25f * row_scratch[prev_x] + 0.50f * row_scratch[x] + 0.25f * row_scratch[next_x];
        }
    }

    // 4. Anisotropic specular highlight across width (seamlessly periodic)
    std::vector<float> specular(width);
    for (int x = 0; x < width; ++x) {
        const float phi = (static_cast<float>(x) / static_cast<float>(width)) * kTwoPi;
        specular[x] = std::sin(phi - 0.4f) * 3.5f +
                      std::sin(2.0f * phi + 1.2f) * 4.0f +
                      std::cos(3.0f * phi) * 1.8f;
    }

    // 5. Evaluate all pixels
    constexpr float kBaseLum = 28.0f;
    for (int y = 0; y < height; ++y) {
        const float r_b = row_base[y];
        const float* row_g1 = &g1[y * kK1];
        const float* row_g2 = &g2[y * kK2];
        const float* row_g3 = &g3[y * kK3];
        const float* row_micro = &micro[y * width];
        RgbaPixel* out_row = &pixels[y * width];

        for (int x = 0; x < width; ++x) {
            auto sample_grid = [&](const float* g, int K) -> float {
                const float step = static_cast<float>(width) / static_cast<float>(K);
                const float pos = static_cast<float>(x) / step;
                const int k0 = static_cast<int>(pos) % K;
                const int k1 = (k0 + 1) % K;
                const float t = pos - std::floor(pos);
                const float w = Smoothstep(t);
                return (1.0f - w) * g[k0] + w * g[k1];
            };

            const float s = sample_grid(row_g1, kK1) + sample_grid(row_g2, kK2) + sample_grid(row_g3, kK3);
            float lum = kBaseLum + r_b + s + row_micro[x] + specular[x];
            lum = std::clamp(lum, 8.0f, 75.0f);

            // Slightly cool dark titanium/anthracite tint
            out_row[x].r = static_cast<uint8_t>(std::clamp(lum * 0.96f, 0.0f, 255.0f));
            out_row[x].g = static_cast<uint8_t>(std::clamp(lum * 0.98f, 0.0f, 255.0f));
            out_row[x].b = static_cast<uint8_t>(std::clamp(lum * 1.04f, 0.0f, 255.0f));
            out_row[x].a = 255;
        }
    }

    return pixels;
}

bool CdBrushedMetal::Initialize(int width, int height) {
    if (texture_id_ != 0) {
        Shutdown();
    }

    width_ = width;
    height_ = height;

    const std::vector<RgbaPixel> pixels = GeneratePixels(width_, height_);
    if (pixels.empty()) {
        return false;
    }

    glGenTextures(1, &texture_id_);
    if (texture_id_ == 0) {
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, texture_id_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

void CdBrushedMetal::Shutdown() {
    if (texture_id_ != 0) {
        glDeleteTextures(1, &texture_id_);
        texture_id_ = 0;
    }
    width_ = 0;
    height_ = 0;
}

ImTextureRef CdBrushedMetal::GetTextureRef() const {
    return ImTextureRef(reinterpret_cast<void*>(static_cast<intptr_t>(texture_id_)));
}

void CdBrushedMetal::RenderChassisBolt(ImDrawList* draw_list, const ImVec2& center, float radius) {
    if (!draw_list) {
        return;
    }

    // Outer recessed counterbore well
    draw_list->AddCircleFilled(center, radius + 2.0f, IM_COL32(14, 14, 17, 240));
    draw_list->AddCircle(center, radius + 2.0f, IM_COL32(38, 40, 46, 180), 24, 1.0f);

    // Milled outer bolt head
    draw_list->AddCircleFilled(center, radius, IM_COL32(48, 50, 56, 255));
    draw_list->AddCircle(center, radius, IM_COL32(75, 80, 92, 220), 24, 1.2f);
    draw_list->AddCircle(center, radius - 1.2f, IM_COL32(32, 33, 38, 180), 24, 1.0f);

    // Rim specular highlight (top-left) and shadow (bottom-right)
    draw_list->PathArcTo(center, radius - 0.5f, 3.4f, 5.2f, 12);
    draw_list->PathStroke(IM_COL32(160, 170, 190, 220), 0, 1.2f);

    draw_list->PathArcTo(center, radius - 0.5f, 0.3f, 2.1f, 12);
    draw_list->PathStroke(IM_COL32(18, 18, 22, 220), 0, 1.2f);

    // Recessed hex socket
    const float r_hex = radius * 0.48f;
    ImVec2 hex_pts[6];
    for (int i = 0; i < 6; ++i) {
        const float angle = static_cast<float>(i) * (kPi / 3.0f) + (kPi / 6.0f);
        hex_pts[i] = ImVec2(center.x + r_hex * std::cos(angle), center.y + r_hex * std::sin(angle));
    }
    draw_list->AddConvexPolyFilled(hex_pts, 6, IM_COL32(10, 10, 12, 255));
    draw_list->AddPolyline(hex_pts, 6, IM_COL32(24, 24, 28, 255), ImDrawFlags_Closed, 1.0f);
    draw_list->AddLine(hex_pts[0], hex_pts[1], IM_COL32(80, 85, 95, 160), 1.0f);
}

void CdBrushedMetal::RenderBackground(ImDrawList* draw_list, const ImVec2& screen_pos, const ImVec2& screen_size) {
    if (!draw_list) {
        return;
    }

    const ImVec2 max_pos(screen_pos.x + screen_size.x, screen_pos.y + screen_size.y);

    if (texture_id_ != 0 && width_ > 0 && height_ > 0) {
        const ImVec2 uv0(screen_pos.x / static_cast<float>(width_),
                         screen_pos.y / static_cast<float>(height_));
        const ImVec2 uv1(max_pos.x / static_cast<float>(width_),
                         max_pos.y / static_cast<float>(height_));
        draw_list->AddImage(GetTextureRef(), screen_pos, max_pos, uv0, uv1);
    } else {
        draw_list->AddRectFilled(screen_pos, max_pos, IM_COL32(28, 28, 32, 255));
    }

    // Overhead ambient light gradient across the faceplate
    draw_list->AddRectFilledMultiColor(
        screen_pos,
        max_pos,
        IM_COL32(255, 255, 255, 14),
        IM_COL32(255, 255, 255, 14),
        IM_COL32(0, 0, 0, 22),
        IM_COL32(0, 0, 0, 22)
    );

    // Machined top chamfer highlight lines (precision-cut aluminum faceplate top edge)
    draw_list->AddLine(ImVec2(screen_pos.x, screen_pos.y),
                       ImVec2(max_pos.x, screen_pos.y),
                       IM_COL32(110, 115, 130, 210), 1.2f);
    draw_list->AddLine(ImVec2(screen_pos.x, screen_pos.y + 1.2f),
                       ImVec2(max_pos.x, screen_pos.y + 1.2f),
                       IM_COL32(65, 70, 80, 140), 1.0f);

    // Bottom chassis shadow / contact lines
    draw_list->AddLine(ImVec2(screen_pos.x, max_pos.y - 1.0f),
                       ImVec2(max_pos.x, max_pos.y - 1.0f),
                       IM_COL32(10, 10, 12, 220), 1.0f);
    draw_list->AddLine(ImVec2(screen_pos.x, max_pos.y),
                       ImVec2(max_pos.x, max_pos.y),
                       IM_COL32(4, 4, 6, 255), 1.0f);

    // Left and right edge subtle depth shading
    draw_list->AddLine(ImVec2(screen_pos.x, screen_pos.y),
                       ImVec2(screen_pos.x, max_pos.y),
                       IM_COL32(70, 75, 85, 140), 1.0f);
    draw_list->AddLine(ImVec2(max_pos.x - 1.0f, screen_pos.y),
                       ImVec2(max_pos.x - 1.0f, max_pos.y),
                       IM_COL32(14, 14, 16, 200), 1.0f);

    // 4 Corner heavy-duty faceplate bolts (classic Hi-Fi rack/chassis mount bolts)
    constexpr float bolt_margin_x = 18.0f;
    constexpr float bolt_margin_y = 16.0f;
    RenderChassisBolt(draw_list, ImVec2(screen_pos.x + bolt_margin_x, screen_pos.y + bolt_margin_y), 6.5f);
    RenderChassisBolt(draw_list, ImVec2(max_pos.x - bolt_margin_x, screen_pos.y + bolt_margin_y), 6.5f);
    RenderChassisBolt(draw_list, ImVec2(screen_pos.x + bolt_margin_x, max_pos.y - bolt_margin_y), 6.5f);
    RenderChassisBolt(draw_list, ImVec2(max_pos.x - bolt_margin_x, max_pos.y - bolt_margin_y), 6.5f);
}

void CdBrushedMetal::RenderPanelChassis(ImDrawList* draw_list, const ImVec2& min_pos, const ImVec2& max_pos, float rounding) {
    if (!draw_list) {
        return;
    }

    if (texture_id_ != 0 && width_ > 0 && height_ > 0) {
        const ImVec2 uv0(min_pos.x / static_cast<float>(width_),
                         min_pos.y / static_cast<float>(height_));
        const ImVec2 uv1(max_pos.x / static_cast<float>(width_),
                         max_pos.y / static_cast<float>(height_));
        draw_list->AddImageRounded(GetTextureRef(), min_pos, max_pos, uv0, uv1, IM_COL32_WHITE, rounding);
    } else {
        draw_list->AddRectFilled(min_pos, max_pos, IM_COL32(28, 28, 32, 255), rounding);
    }

    // Subtle dark tint and recessed perimeter shadow so the panel has tactile depth on the faceplate
    draw_list->AddRectFilled(min_pos, max_pos, IM_COL32(0, 0, 0, 35), rounding);

    // Outer precision-milled metallic bezel
    draw_list->AddRect(min_pos, max_pos, IM_COL32(52, 54, 60, 255), rounding, 0, 1.5f);

    // Chamfer highlight on top edge of panel
    draw_list->AddLine(ImVec2(min_pos.x + rounding, min_pos.y),
                       ImVec2(max_pos.x - rounding, min_pos.y),
                       IM_COL32(80, 85, 96, 180), 1.0f);
}

CdBrushedMetal& GetBrushedMetal() {
    return g_brushed_metal_instance;
}

} // namespace mycds::gui
