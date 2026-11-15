#pragma once

#include "cd_display_model.h"
#include "imgui.h"

namespace mycds::gui {

/// Renders a standalone CD cover & album summary block underneath the main controls
/// when there is sufficient vertical space in the left column.
void RenderCdCoverWidget(CdDisplayModel& model, const ImVec2& size = ImVec2(680.0f, 180.0f));

} // namespace mycds::gui
