#include "cd_brushed_metal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mycds::gui {

namespace {

CdBrushedMetal g_brushed_metal_instance;

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

    const int half_y = (height + 1) / 2;
    const int half_x = (width + 1) / 2;

    // 1. Symmetric row baseline roughness
    std::vector<float> row_base(height);
    for (int y = 0; y < half_y; ++y) {
        const float val = (lcg() - 0.5f) * 9.0f;
        row_base[y] = val;
        row_base[height - 1 - y] = val;
    }

    // 2. Symmetric horizontal streak grids
    constexpr int kK1 = 4;
    constexpr int kK2 = 8;
    constexpr int kK3 = 16;

    std::vector<float> g1(half_y * kK1);
    std::vector<float> g2(half_y * kK2);
    std::vector<float> g3(half_y * kK3);

    for (int y = 0; y < half_y; ++y) {
        for (int k = 0; k < kK1; ++k) {
            g1[y * kK1 + k] = (lcg() - 0.5f) * 8.0f;
        }
        for (int k = 0; k < kK2; ++k) {
            g2[y * kK2 + k] = (lcg() - 0.5f) * 6.0f;
        }
        for (int k = 0; k < kK3; ++k) {
            g3[y * kK3 + k] = (lcg() - 0.5f) * 4.0f;
        }
    }

    // 3. Symmetric micro-grain buffer
    std::vector<float> micro_half(half_y * half_x);
    for (int y = 0; y < half_y; ++y) {
        std::vector<float> raw(half_x);
        for (int x = 0; x < half_x; ++x) {
            raw[x] = (lcg() - 0.5f) * 7.0f;
        }
        for (int x = 0; x < half_x; ++x) {
            const int prev_x = std::max(0, x - 1);
            const int next_x = std::min(half_x - 1, x + 1);
            micro_half[y * half_x + x] = 0.25f * raw[prev_x] + 0.50f * raw[x] + 0.25f * raw[next_x];
        }
    }

    // 4. Symmetric specular highlight
    std::vector<float> specular(width);
    for (int x = 0; x < half_x; ++x) {
        const float phi = (static_cast<float>(x) / static_cast<float>(half_x)) * kPi;
        const float val = std::sin(phi) * 3.0f + std::sin(2.0f * phi) * 1.5f;
        specular[x] = val;
        specular[width - 1 - x] = val;
    }

    // 5. Evaluate half-quadrant and mirror symmetrically to all 4 quadrants
    constexpr float kBaseLum = 28.0f;
    for (int y = 0; y < half_y; ++y) {
        const float r_b = row_base[y];
        const float* row_g1 = &g1[y * kK1];
        const float* row_g2 = &g2[y * kK2];
        const float* row_g3 = &g3[y * kK3];

        for (int x = 0; x < half_x; ++x) {
            auto sample_grid = [&](const float* g, int K) -> float {
                const float step = static_cast<float>(half_x) / static_cast<float>(K);
                const float pos = static_cast<float>(x) / step;
                const int k0 = std::clamp(static_cast<int>(pos), 0, K - 1);
                const int k1 = std::clamp(k0 + 1, 0, K - 1);
                const float t = pos - std::floor(pos);
                const float w = Smoothstep(t);
                return (1.0f - w) * g[k0] + w * g[k1];
            };

            const float s = sample_grid(row_g1, kK1) + sample_grid(row_g2, kK2) + sample_grid(row_g3, kK3);
            const float m = micro_half[y * half_x + x];
            float lum = kBaseLum + r_b + s + m + specular[x];
            lum = std::clamp(lum, 8.0f, 75.0f);

            RgbaPixel px;
            px.r = static_cast<uint8_t>(std::clamp(lum * 0.96f, 0.0f, 255.0f));
            px.g = static_cast<uint8_t>(std::clamp(lum * 0.98f, 0.0f, 255.0f));
            px.b = static_cast<uint8_t>(std::clamp(lum * 1.04f, 0.0f, 255.0f));
            px.a = 255;

            pixels[y * width + x] = px;
            pixels[y * width + (width - 1 - x)] = px;
            pixels[(height - 1 - y) * width + x] = px;
            pixels[(height - 1 - y) * width + (width - 1 - x)] = px;
        }
    }

    return pixels;
}

bool CdBrushedMetal::Initialize(int tile_size) {
    return Initialize(tile_size, tile_size);
}

bool CdBrushedMetal::Initialize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }

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
    if (texture_id_ != 0) {
        glBindTexture(GL_TEXTURE_2D, texture_id_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    initialized_ = true;
    return true;
}

bool CdBrushedMetal::EnsureCapacity(int required_width, int required_height) {
    if (required_width <= 0 || required_height <= 0) {
        return false;
    }
    if (initialized_) {
        return true;
    }
    return Initialize(kDefaultTileSize, kDefaultTileSize);
}

void CdBrushedMetal::Shutdown() {
    if (texture_id_ != 0) {
        glDeleteTextures(1, &texture_id_);
        texture_id_ = 0;
    }
    initialized_ = false;
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

    // Recessed hex socket with precalculated unit offsets
    const float r_hex = radius * 0.48f;
    static const ImVec2 kHexUnitOffsets[6] = {
        ImVec2(std::cos(0.0f * (kPi / 3.0f) + (kPi / 6.0f)), std::sin(0.0f * (kPi / 3.0f) + (kPi / 6.0f))),
        ImVec2(std::cos(1.0f * (kPi / 3.0f) + (kPi / 6.0f)), std::sin(1.0f * (kPi / 3.0f) + (kPi / 6.0f))),
        ImVec2(std::cos(2.0f * (kPi / 3.0f) + (kPi / 6.0f)), std::sin(2.0f * (kPi / 3.0f) + (kPi / 6.0f))),
        ImVec2(std::cos(3.0f * (kPi / 3.0f) + (kPi / 6.0f)), std::sin(3.0f * (kPi / 3.0f) + (kPi / 6.0f))),
        ImVec2(std::cos(4.0f * (kPi / 3.0f) + (kPi / 6.0f)), std::sin(4.0f * (kPi / 3.0f) + (kPi / 6.0f))),
        ImVec2(std::cos(5.0f * (kPi / 3.0f) + (kPi / 6.0f)), std::sin(5.0f * (kPi / 3.0f) + (kPi / 6.0f)))
    };

    ImVec2 hex_pts[6];
    for (int i = 0; i < 6; ++i) {
        hex_pts[i] = ImVec2(center.x + r_hex * kHexUnitOffsets[i].x, center.y + r_hex * kHexUnitOffsets[i].y);
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
        const float tile_w = static_cast<float>(width_);
        const float tile_h = static_cast<float>(height_);

        // Tile the small symmetric texture seamlessly across the window
        for (float y = screen_pos.y; y < max_pos.y; y += tile_h) {
            const float h = std::min(tile_h, max_pos.y - y);
            const float uv1_y = h / tile_h;

            for (float x = screen_pos.x; x < max_pos.x; x += tile_w) {
                const float w = std::min(tile_w, max_pos.x - x);
                const float uv1_x = w / tile_w;

                draw_list->AddImage(
                    GetTextureRef(),
                    ImVec2(x, y),
                    ImVec2(x + w, y + h),
                    ImVec2(0.0f, 0.0f),
                    ImVec2(uv1_x, uv1_y)
                );
            }
        }
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

    // The brushed metal background spans the entire window faceplate. The panel modules
    // sit seamlessly on this surface; a subtle recess tint and milled bezel create
    // authentic physical modular depth without redundant texture redraws or state changes.
    draw_list->AddRectFilled(min_pos, max_pos, IM_COL32(0, 0, 0, 32), rounding);

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
