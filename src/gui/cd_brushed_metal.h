#pragma once

#include <cstdint>
#include <vector>

#include <SDL3/SDL_opengl.h>
#include "imgui.h"

namespace mycds::gui {

struct RgbaPixel {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
    uint8_t a{255};
};

class CdBrushedMetal {
public:
    static constexpr int kDefaultMinCapacityWidth = 2560;
    static constexpr int kDefaultMinCapacityHeight = 1440;

    CdBrushedMetal() = default;
    ~CdBrushedMetal();

    CdBrushedMetal(const CdBrushedMetal&) = delete;
    CdBrushedMetal& operator=(const CdBrushedMetal&) = delete;

    bool Initialize(int width, int height);
    void Shutdown();

    // Ensure texture has at least required_width x required_height capacity.
    // If current capacity is sufficient, returns true immediately with zero work.
    // Generates oversized texture (>= 2560x1440) so window resizing requires no regenerations.
    bool EnsureCapacity(int required_width, int required_height);

    bool UpdateSize(int required_width, int required_height) {
        return EnsureCapacity(required_width, required_height);
    }

    bool IsInitialized() const { return initialized_; }
    int GetWidth() const { return width_; }
    int GetHeight() const { return height_; }
    GLuint GetTextureId() const { return texture_id_; }
    ImTextureRef GetTextureRef() const;

    void RenderBackground(ImDrawList* draw_list, const ImVec2& screen_pos, const ImVec2& screen_size);
    void RenderPanelChassis(ImDrawList* draw_list, const ImVec2& min_pos, const ImVec2& max_pos, float rounding = 8.0f);
    void RenderChassisBolt(ImDrawList* draw_list, const ImVec2& center, float radius = 7.0f);

    static std::vector<RgbaPixel> GeneratePixels(int width, int height, uint32_t seed = 0x4D594344);

private:
    GLuint texture_id_{0};
    int width_{0};
    int height_{0};
    bool initialized_{false};
};

CdBrushedMetal& GetBrushedMetal();

} // namespace mycds::gui
