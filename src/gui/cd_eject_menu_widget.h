#pragma once

#include "cd_display_model.h"
#include "imgui.h"

namespace mycds::gui {

/// Renders the full-window Eject Menu screen with a left navigation panel
/// (CD, Image/File, Library, Disc Info, Settings, and bottom-anchored Back)
/// and a right-hand content panel for the active screen.
void RenderEjectMenu(CdDisplayModel& model, const ImVec2& size = ImVec2(0.0f, 0.0f));

} // namespace mycds::gui
