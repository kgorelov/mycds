#include "cd_layout.h"

#include <algorithm>
#include <cmath>

namespace mycds::gui {

float compute_deck_height(float item_spacing_y) {
    const float spacing = std::max(0.0f, item_spacing_y);
    return LayoutConstants::kDisplayHeight +
           LayoutConstants::kMeterHeight +
           LayoutConstants::kControlsHeight +
           2.0f * spacing;
}

int compute_min_window_width() {
    return static_cast<int>(std::ceil(
        LayoutConstants::kDeckWidth + 2.0f * LayoutConstants::kWindowPaddingX));
}

int compute_min_window_height(float item_spacing_y) {
    return static_cast<int>(std::ceil(
        compute_deck_height(item_spacing_y) + 2.0f * LayoutConstants::kWindowPaddingY));
}

LayoutPlan compute_layout_plan(
    float origin_x,
    float origin_y,
    float avail_width,
    float avail_height,
    const AppSettings& settings,
    float item_spacing_y) {
    LayoutPlan plan;
    const float spacing = std::max(0.0f, item_spacing_y);
    const float deck_h = compute_deck_height(spacing);

    // Mandatory widgets always stay visible at their fixed deck dimensions.
    plan.display_rect = LayoutRect{
        origin_x,
        origin_y,
        LayoutConstants::kDeckWidth,
        LayoutConstants::kDisplayHeight
    };
    plan.meter_rect = LayoutRect{
        origin_x,
        origin_y + LayoutConstants::kDisplayHeight + spacing,
        LayoutConstants::kDeckWidth,
        LayoutConstants::kMeterHeight
    };
    plan.controls_rect = LayoutRect{
        origin_x,
        plan.meter_rect.y + LayoutConstants::kMeterHeight + spacing,
        LayoutConstants::kDeckWidth,
        LayoutConstants::kControlsHeight
    };

    const float avail_playlist_w_horiz = avail_width - LayoutConstants::kDeckWidth - LayoutConstants::kColumnGap;
    const bool can_fit_horizontal_playlist =
        (avail_playlist_w_horiz >= LayoutConstants::kMinPlaylistWidth) &&
        (avail_height >= LayoutConstants::kMinPlaylistHeight);

    const float avail_playlist_h_vert = avail_height - deck_h - LayoutConstants::kRowGap;
    const bool can_fit_vertical_playlist =
        (avail_width >= LayoutConstants::kMinPlaylistWidth) &&
        (avail_playlist_h_vert >= LayoutConstants::kMinPlaylistHeight);

    switch (settings.layout_mode) {
        case LayoutMode::Horizontal:
            plan.resolved_layout = ResolvedLayout::Horizontal;
            break;
        case LayoutMode::Vertical:
            plan.resolved_layout = ResolvedLayout::Vertical;
            break;
        case LayoutMode::Dynamic:
        default:
            if (!settings.show_playlist) {
                plan.resolved_layout = ResolvedLayout::Horizontal;
            } else if (can_fit_horizontal_playlist) {
                plan.resolved_layout = ResolvedLayout::Horizontal;
            } else if (can_fit_vertical_playlist) {
                plan.resolved_layout = ResolvedLayout::Vertical;
            } else {
                plan.resolved_layout = ResolvedLayout::Horizontal;
            }
            break;
    }

    if (settings.show_playlist) {
        if (plan.resolved_layout == ResolvedLayout::Horizontal) {
            if (can_fit_horizontal_playlist) {
                plan.show_playlist = true;
                plan.playlist_rect = LayoutRect{
                    origin_x + LayoutConstants::kDeckWidth + LayoutConstants::kColumnGap,
                    origin_y,
                    avail_playlist_w_horiz,
                    std::max(avail_height, LayoutConstants::kMinPlaylistHeight)
                };
            }
        } else {
            if (can_fit_vertical_playlist) {
                plan.show_playlist = true;
                plan.playlist_rect = LayoutRect{
                    origin_x,
                    origin_y + deck_h + LayoutConstants::kRowGap,
                    LayoutConstants::kDeckWidth,
                    avail_playlist_h_vert
                };
            }
        }
    }

    if (settings.show_cd_cover && plan.resolved_layout == ResolvedLayout::Horizontal) {
        const float remaining_h = avail_height - deck_h - LayoutConstants::kRowGap;
        if (remaining_h >= LayoutConstants::kMinCoverHeight) {
            plan.show_cover = true;
            plan.cover_rect = LayoutRect{
                origin_x,
                origin_y + deck_h + LayoutConstants::kRowGap,
                LayoutConstants::kDeckWidth,
                remaining_h
            };
        }
    }

    return plan;
}

} // namespace mycds::gui
