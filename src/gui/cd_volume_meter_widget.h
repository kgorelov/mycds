#pragma once

#include "cd_display_model.h"
#include "imgui.h"

namespace mycds::gui {

/// Renders vintage square-LED volume/VU meter widget.
/// Features dual-channel (L/R) green, yellow, and red square LEDs with peak hold,
/// styled like vintage tape/cassette recorder level indicators.
void RenderCdVolumeMeter(const CdDisplayModel& model, const ImVec2& size = ImVec2(0, 0));

} // namespace mycds::gui
