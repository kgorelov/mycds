#pragma once

#include <cstdint>
#include <vector>

#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
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
    static constexpr int kDefaultTileSizeWidth = 512;
    static constexpr int kDefaultTileSizeHeight = 128;

    CdBrushedMetal() = default;
    ~CdBrushedMetal();

    CdBrushedMetal(const CdBrushedMetal&) = delete;
    CdBrushedMetal& operator=(const CdBrushedMetal&) = delete;

    bool Initialize(int width, int height);
    void Shutdown();

    // With 128x128 symmetric tile rendering, no re-generation is needed when resizing.
    // EnsureCapacity/UpdateSize ensure initialization and return true immediately.
    bool EnsureCapacity(int required_width = kDefaultTileSizeWidth, int required_height = kDefaultTileSizeHeight);

    bool UpdateSize(int required_width, int required_height) {
        return EnsureCapacity(required_width, required_height);
    }

    bool IsInitialized() const { return initialized_; }
    int GetWidth() const { return width_; }
    int GetHeight() const { return height_; }
    GLuint GetTextureId() const { return texture_id_; }
    ImTextureRef GetTextureRef() const;

    GLuint GetButtonTextureId() const { return button_texture_id_; }
    ImTextureRef GetButtonTextureRef() const;

    // FBO Caching API
    void InvalidateCache();
    bool RebuildCache(int width, int height);
    GLuint GetCachedTextureId() const { return cached_texture_id_; }
    GLuint GetFboId() const { return fbo_id_; }
    int GetCachedWidth() const { return cached_width_; }
    int GetCachedHeight() const { return cached_height_; }
    bool IsCacheValid() const { return cached_texture_id_ != 0 && !cache_dirty_; }
    ImTextureRef GetCachedTextureRef() const;

    void RenderBackground(ImDrawList* draw_list, const ImVec2& screen_pos, const ImVec2& screen_size);
    void RenderBackgroundDirect(ImDrawList* draw_list, const ImVec2& screen_pos, const ImVec2& screen_size);
    void RenderPanelChassis(ImDrawList* draw_list, const ImVec2& min_pos, const ImVec2& max_pos, float rounding = 8.0f);
    void RenderChassisBolt(ImDrawList* draw_list, const ImVec2& center, float radius = 7.0f);

    static std::vector<RgbaPixel> GeneratePixels(int width, int height, uint32_t seed = 0x4D594344);
    static std::vector<RgbaPixel> GenerateButtonGrainPixels(int width = 64, int height = 64, uint32_t seed = 0x42544E31);

private:
    void DestroyFbo();

    GLuint texture_id_{0};
    GLuint button_texture_id_{0};
    int width_{0};
    int height_{0};
    bool initialized_{false};

    GLuint fbo_id_{0};
    GLuint cached_texture_id_{0};
    int cached_width_{0};
    int cached_height_{0};
    bool cache_dirty_{true};
};

CdBrushedMetal& GetBrushedMetal();

} // namespace mycds::gui
