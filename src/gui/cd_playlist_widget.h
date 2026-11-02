#pragma once

#include "cd_display_model.h"
#include "imgui.h"

namespace mycds::gui {

/// Renders the CD playlist / track list widget in matching hardware CD display style.
/// Amber orange list items, active track glowing green, interactive selection.
void RenderCdPlaylist(CdDisplayModel& model, const ImVec2& size = ImVec2(0, 0));

} // namespace mycds::gui
