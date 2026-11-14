# MyCDs GUI Performance Profiling Analysis & Optimization Plan

## 1. Executive Summary

Empirical profiling of the `mycds` GUI application (`./build/mycds`) revealed an idle baseline CPU consumption of **~5.0% – 5.2%** of a CPU core on Linux x86_64 (`pidstat`: 4.8% user, 0.4% system).

All CPU consumption is concentrated in the main application thread (TID executing the SDL3/OpenGL event and rendering loop). Background Mesa OpenGL/Gallium driver threads (`disk$0`, `sh0`, `traceq0`, `gdrv0`) account for 0.0% CPU.

---

## 2. UI Update Frequency Analysis

The UI update rate is determined in [src/gui/main.cpp](file:///home/kgorelov/git/mycds/src/gui/main.cpp#L54-L125):
- `SDL_GL_SetSwapInterval(1)` enables vertical synchronization (VSync).
- The main `while (running)` loop continuously executes `SDL_PollEvent(&event)` without waiting or sleeping.
- On every loop iteration, a full Dear ImGui frame is created, simulated, tessellated, and submitted to OpenGL via `ImGui_ImplOpenGL3_RenderDrawData()` before blocking on `SDL_GL_SwapWindow(window)`.

**Current Behavior:**
- **Refresh rate:** Strictly locked to the display's VSync refresh rate (**60.05 Hz**, ~16.6 ms per frame on standard 60 Hz displays).
- **High-refresh displays:** On 120 Hz, 144 Hz, or 240 Hz monitors, the loop executes 120 to 240 times per second, scaling CPU consumption up to **10% – 20%**.
- **Uncapped fallback:** If VSync is unsupported or disabled by the compositor/driver, the loop runs uncapped, saturating an entire CPU core at 100%.

---

## 3. Profiling Findings & Hotspots

At 60 frames per second, 5.2% CPU corresponds to **~52 ms of CPU execution per second**, or **~0.87 ms per frame**.

Profiling via `gprofng` / `gp-collect-app` and static inspection highlights four primary contributors:

1. **Unconditional Re-rendering During Idle:**
   - Even when audio playback is stopped, no keys are pressed, and the cursor is stationary, the full widget hierarchy is rebuilt from scratch 60 times a second.
2. **Procedural Vector Drawing in Widgets:**
   - **Procedural Button Face Texture** ([src/gui/cd_controls_widget.cpp#L64-L80](file:///home/kgorelov/git/mycds/src/gui/cd_controls_widget.cpp#L64-L80)): `DrawButtonBrushedFace()` iterates over scanlines computing `Hash1D()` pseudo-random numbers and executing 15–30 `draw_list->AddLine()` calls per button every frame.
   - **Faceplate Background Tiling & Bolts** ([src/gui/cd_brushed_metal.cpp#L223-L297](file:///home/kgorelov/git/mycds/src/gui/cd_brushed_metal.cpp#L223-L297)): Every frame, `RenderBackground()` loops over the viewport width and height emitting multiple `draw_list->AddImage()` calls for 512x128 slices, multi-color gradients, chamfers, and 4 corner bolts with trigonometric hex socket projections and arcs.
   - **LED VU Meter & 7-Segment Displays** ([src/gui/cd_volume_meter_widget.cpp](file:///home/kgorelov/git/mycds/src/gui/cd_volume_meter_widget.cpp) & [src/gui/cd_display_widget.cpp](file:///home/kgorelov/git/mycds/src/gui/cd_display_widget.cpp)): 48 LEDs (2 channels x 24 segments) and multiple 7-segment digits continuously emit polylines, convex polygons, and glow outlines.
3. **ImDrawList Tessellation Overhead:**
   - Profiling functions `ImDrawList::AddConvexPolyFilled`, `AddPolyline`, `AddRectFilled`, and `PathFillConvex` occupy significant CPU time preparing vertex/index buffers.
4. **SDL / OS Driver Interactivity:**
   - `ImGui_ImplSDL3_NewFrame()` polls mouse state (`SDL_CaptureMouse`), display boundaries (`SDL_GetDisplayBounds`), and window geometry through X11/DRI3 kernel `ioctl` operations 60 times a second.

---

## 4. Prioritized Optimization Plan

### Phase 1: Event-Driven / Reactive Idle Loop [COMPLETED]
*Target: Reduce idle CPU consumption from ~5.2% to ~0.0%.*
*Result: Verified with `pidstat` — CPU dropped from **5.20%** down to **0.20%** (frequently 0.00% between blink intervals).*

- **Mechanism:** Replaced non-blocking `SDL_PollEvent(&event)` with intelligent `SDL_WaitEventTimeout(&event, wait_timeout_ms)` in [src/gui/main.cpp](file:///home/kgorelov/git/mycds/src/gui/main.cpp#L97-L175).
- **Logic:**
  - Evaluates whether continuous updates are needed (`is_active_playback`, `is_key_down`, `is_mouse_down`, or active ImGui interaction).
  - When idle (stopped/paused), computes the precise sleep duration until the next colon blink toggle (at phase 0.0 and 0.5), sleeping in the kernel for up to 500 ms.
  - Wakes up instantaneously (0 ms latency) on any SDL input event (mouse motion, key press, window expose).
  - Batches event processing and ensures a 3-frame settling tail to let transitions resolve smoothly.

### Phase 2: Frame Rate Limiting & Pacing [COMPLETED]
*Target: Prevent excessive CPU usage on high-refresh monitors (120Hz/144Hz/240Hz) and allow power-saving modes.*
*Result: Caps frame rate to 60 FPS maximum on high-refresh monitors, and supports custom `--fps <n>` (e.g. `--fps 30` drops active playback CPU to ~3.6%).*

- **Mechanism:** Implemented hardware-aware frame pacing in [src/gui/main.cpp](file:///home/kgorelov/git/mycds/src/gui/main.cpp#L104-L162).
- **Features:**
  - Detects native monitor display refresh rate via `SDL_GetCurrentDisplayMode()` and checks whether hardware VSync is active (`SDL_GL_GetSwapInterval()`).
  - Supports `--fps <n>` and `--fps=<n>` command line options (range: 10–240 FPS, default: 60 FPS).
  - Automatically activates frame sleep pacing only when required (`!vsync_active` or `target_fps < display_hz - 1.0f`), preventing VSync stutter on 60 Hz monitors while clamping CPU on 120 Hz, 144 Hz, and 240 Hz monitors.
  - Sleep intervals during active animation use `SDL_WaitEventTimeout()`, keeping user input response completely instantaneous (0 ms input lag).

### Phase 3: Background Faceplate Caching with Framebuffer Object (Medium Priority)
*Target: Reduce per-frame vertex count and draw call complexity by ~30–40%.*

- **Mechanism:** Render the brushed metal background, chamfer highlight lines, outer bezels, and 4 corner chassis bolts once into an offscreen OpenGL framebuffer (FBO) / texture.
- **Trigger:** Re-render the background texture only at startup and whenever the window is resized (`SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`).
- **Render Loop:** In [src/gui/cd_brushed_metal.cpp](file:///home/kgorelov/git/mycds/src/gui/cd_brushed_metal.cpp#L223), draw the entire faceplate using a single `draw_list->AddImage()` quad.

### Phase 4: Button Face Noise & Static Geometry Optimization (Low Priority)
*Target: Eliminate redundant CPU mathematical evaluations per frame.*

- In [src/gui/cd_controls_widget.cpp#L64](file:///home/kgorelov/git/mycds/src/gui/cd_controls_widget.cpp#L64), replace the procedural `for` loop executing `Hash1D()` on every scanline with:
  - A precomputed 1D or 2D noise lookup table (LUT).
  - Or a small pre-baked texture slice sampled for button surfaces.
- For unlit LED segments and static display bezels, avoid redundant multi-pass glow polylines when segments are dark.
- Pre-generate the static brushed metal procedural texture at compile time or initialize statically on first launch (as outlined in `todo.org`).
