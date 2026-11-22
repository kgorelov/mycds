#pragma once

#include "cd_display_model.h"
#include "imgui.h"

namespace mycds::gui {

/// Renders the hardware-style Hi-Fi CD player controls widget (Phase 2.4).
/// Features:
///   - Brushed metal square buttons lighter than the chassis background.
///   - Backlit cyan-blue graphical pictograms.
///   - Tactile hardware look and feel (recessed socket slots, 3D chamfers, pressed state).
///   - Normal square buttons: Play/Pause (> ||), Stop, Prev (seek backward on hold),
///     Next (seek forward on hold), Record (classic red circle), and Eject (load dialog).
///   - Two half-size stacked buttons: Shuffle and Repeat.
///   - Two precision-machined rotary knobs: Volume and Balance with cyan-lit pointers,
///     knurled grips, arc ticks, and drag/scroll interaction.
///   - Eject dialog allowing selection of physical CD, disk image (.cue, .iso, etc.),
///     or album from the CD library.
void RenderCdControls(CdDisplayModel& model, const ImVec2& size = ImVec2(0, 0));

} // namespace mycds::gui
