#pragma once

#include "cd_display_model.h"
#include "imgui.h"

namespace mycds::gui {

/// Renders the classic hardware CD player display widget.
/// Displays amber 7-segment track number and track time,
/// along with glowing green/red indicators for repeat, shuffle, cloud, and CDDB.
void RenderCdDisplay(CdDisplayModel& model, const ImVec2& size = ImVec2(0, 0));

} // namespace mycds::gui
