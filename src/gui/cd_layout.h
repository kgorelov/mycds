#pragma once

#include "cd_display_model.h"

namespace mycds::gui {

struct LayoutRect {
    float x{0.0f};
    float y{0.0f};
    float width{0.0f};
    float height{0.0f};
};

struct LayoutConstants {
    static constexpr float kDeckWidth = 680.0f;
    static constexpr float kDisplayHeight = 250.0f;
    static constexpr float kMeterHeight = 90.0f;
    static constexpr float kControlsHeight = 116.0f;

    static constexpr float kWindowPaddingX = 24.0f;
    static constexpr float kWindowPaddingY = 20.0f;
    static constexpr float kColumnGap = 24.0f;
    static constexpr float kRowGap = 16.0f;
    static constexpr float kDefaultItemSpacingY = 4.0f;

    static constexpr float kMinPlaylistWidth = 420.0f;
    static constexpr float kMinPlaylistHeight = 220.0f;

    static constexpr float kMinCoverWidth = 160.0f;
    static constexpr float kMinCoverHeight = 160.0f;
    static constexpr float kMinCoverAspectRatio = 0.5f;
    static constexpr float kMaxCoverAspectRatio = 1.5f;
};

enum class ResolvedLayout {
    Horizontal,
    Vertical
};

struct LayoutPlan {
    ResolvedLayout resolved_layout{ResolvedLayout::Horizontal};
    LayoutRect display_rect{};
    LayoutRect meter_rect{};
    LayoutRect controls_rect{};

    bool show_playlist{false};
    LayoutRect playlist_rect{};

    bool show_cover{false};
    LayoutRect cover_rect{};
};

/// Total height of the mandatory deck (display + volume meter + controls + vertical gaps).
[[nodiscard]] float compute_deck_height(float item_spacing_y = LayoutConstants::kDefaultItemSpacingY);

/// Minimum window width required so mandatory widgets stay visible.
[[nodiscard]] int compute_min_window_width();

/// Minimum window height required so mandatory widgets stay visible.
[[nodiscard]] int compute_min_window_height(float item_spacing_y = LayoutConstants::kDefaultItemSpacingY);

/// Compute widget positions, sizes, and optional widget visibility for the given content region.
[[nodiscard]] LayoutPlan compute_layout_plan(
    float origin_x,
    float origin_y,
    float avail_width,
    float avail_height,
    const AppSettings& settings,
    float item_spacing_y = LayoutConstants::kDefaultItemSpacingY);

} // namespace mycds::gui
