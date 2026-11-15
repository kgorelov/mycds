#include <algorithm>
#include <cmath>
#include <iostream>
#include <string_view>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include "cd_brushed_metal.h"
#include "cd_controls_widget.h"
#include "cd_cover_widget.h"
#include "cd_display_model.h"
#include "cd_display_widget.h"
#include "cd_eject_menu_widget.h"
#include "cd_playlist_widget.h"
#include "cd_volume_meter_widget.h"
#include "eject_menu_state.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"

int main(int argc, char* argv[]) {

    // Disable X11 compositor bypass by default (improves desktop integration on KDE KWin and X11 compositors)
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << '\n';
        return 1;
    }

    // OpenGL 3.0 Core profile and GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    const SDL_WindowFlags window_flags = SDL_WINDOW_OPENGL |
                                         SDL_WINDOW_RESIZABLE |
                                         SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* window = SDL_CreateWindow("MyCDs", 1460, 532, window_flags);
    if (!window) {
        std::cerr << "Failed to create SDL window: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        std::cerr << "Failed to create OpenGL context: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Embedded scalable vector font
    io.Fonts->AddFontDefaultVector();

    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    if (!ImGui_ImplSDL3_InitForOpenGL(window, gl_context)) {
        std::cerr << "Failed to initialize ImGui SDL3 backend\n";
        ImGui::DestroyContext();
        SDL_GL_DestroyContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!ImGui_ImplOpenGL3_Init(glsl_version)) {
        std::cerr << "Failed to initialize ImGui OpenGL3 backend\n";
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_GL_DestroyContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!mycds::gui::GetBrushedMetal().Initialize(mycds::gui::CdBrushedMetal::kDefaultTileSizeWidth,
                                                  mycds::gui::CdBrushedMetal::kDefaultTileSizeHeight)) {
        std::cerr << "Warning: Failed to initialize brushed metal texture\n";
    }

    mycds::gui::CdDisplayModel display_model;
    const mycds::gui::AppSettings startup_settings =
        mycds::gui::load_app_settings(mycds::gui::default_settings_path());
    display_model.set_app_settings(startup_settings);
    if (!startup_settings.play_when_started) {
        display_model.stop();
    }
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--stopped" || arg == "--paused") {
            display_model.stop();
        }
    }

    bool running = true;
    int current_swap_interval = 1;
    SDL_GL_SetSwapInterval(1);

    bool had_active_item_last_frame = false;
    Uint64 last_user_activity_ticks = SDL_GetTicks();
    constexpr Uint64 kInteractiveGracePeriodMs = 250;

    auto process_event = [&](const SDL_Event& ev) {
        ImGui_ImplSDL3_ProcessEvent(&ev);

        if (ev.type == SDL_EVENT_QUIT) {
            running = false;
        }
        if (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
            ev.window.windowID == SDL_GetWindowID(window)) {
            running = false;
        }
        if ((ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || ev.type == SDL_EVENT_WINDOW_RESIZED) &&
            ev.window.windowID == SDL_GetWindowID(window)) {
            mycds::gui::GetBrushedMetal().InvalidateCache();
        }
        if (ev.type == SDL_EVENT_KEY_DOWN) {
            if (ev.key.key == SDLK_F10 || ev.key.scancode == SDL_SCANCODE_F10) {
                running = false;
            }
            if (!display_model.is_eject_menu_open() && !ImGui::GetIO().WantTextInput &&
                (ev.key.key == SDLK_Q || ev.key.key == 'Q' || ev.key.scancode == SDL_SCANCODE_Q)) {
                running = false;
            }
        }
    };

    while (running) {
        const Uint64 now_ticks = SDL_GetTicks();
        const bool user_recently_active = (now_ticks - last_user_activity_ticks < kInteractiveGracePeriodMs);
        const bool is_active_playback = display_model.is_playing() || display_model.is_recording();

        SDL_Event event;
        bool had_events = false;

        // When in idle mode (stopped/paused) and user is not actively interacting:
        // Sleep in kernel waiting for events or the next colon blink toggle (phase 0.0 and 0.5)
        if (!user_recently_active && !is_active_playback) {
            const double uptime = display_model.uptime_seconds();
            const double phase = std::fmod(uptime, 1.0);
            const double time_to_next_blink = (phase < 0.5) ? (0.5 - phase) : (1.0 - phase);
            int wait_ms = static_cast<int>(std::ceil(time_to_next_blink * 1000.0)) + 10;
            wait_ms = std::clamp(wait_ms, 10, 500);

            if (SDL_WaitEventTimeout(&event, wait_ms)) {
                had_events = true;
                process_event(event);
            }
        }

        // Drain ALL pending events immediately (highest priority user input processing)
        while (SDL_PollEvent(&event)) {
            had_events = true;
            process_event(event);
        }

        if (had_events) {
            last_user_activity_ticks = SDL_GetTicks();
        }

        if (!running) {
            break;
        }

        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(50);
            continue;
        }

        // Dynamic VSync swap pacing:
        // - Interactive mode: interval 1 (full monitor refresh, e.g. 60 FPS) for instant, lag-free UI.
        // - Hands-off playback mode: interval 2 (e.g. 30 FPS) for smooth retro LED meters at low CPU.
        const bool is_interactive = had_events ||
                                    (SDL_GetTicks() - last_user_activity_ticks < kInteractiveGracePeriodMs) ||
                                    had_active_item_last_frame;

        const int desired_interval = is_interactive ? 1 : (is_active_playback ? 2 : 1);
        if (desired_interval != current_swap_interval) {
            SDL_GL_SetSwapInterval(desired_interval);
            current_swap_interval = desired_interval;
        }

        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(50);
            continue;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Keyboard controls
        if (ImGui::IsKeyPressed(ImGuiKey_F10) ||
            (!display_model.is_eject_menu_open() && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Q))) {
            running = false;
        }
        if (display_model.is_eject_menu_open()) {
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                display_model.close_eject_menu();
            }
        } else if (!io.WantTextInput) {
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
            if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
                display_model.toggle_play_pause();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_X)) {
                display_model.stop();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_E)) {
                display_model.open_eject_menu();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                display_model.stop();
                display_model.open_eject_menu();
            }
            const double seek_dt = std::min(static_cast<double>(io.DeltaTime), 0.05);
            if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
                display_model.seek_backward(10.0 * seek_dt);
            }
            if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
                display_model.seek_forward(10.0 * seek_dt);
            }
            const bool vol_up = ImGui::IsKeyDown(ImGuiKey_UpArrow);
            const bool vol_down = ImGui::IsKeyDown(ImGuiKey_DownArrow);
            static double vol_hold_seconds = 0.0;
            if (vol_up != vol_down) {
                const bool initial_press =
                    (vol_up && ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) ||
                    (vol_down && ImGui::IsKeyPressed(ImGuiKey_DownArrow, false));
                vol_hold_seconds += seek_dt;
                const double accel = std::min(1.0, vol_hold_seconds / 0.9);
                const double rate = 0.15 + 1.35 * accel * accel;
                const double step = initial_press ? std::max(0.01, rate * seek_dt) : (rate * seek_dt);
                const double dir = vol_up ? 1.0 : -1.0;
                display_model.set_volume(display_model.volume() + dir * step);
            } else {
                vol_hold_seconds = 0.0;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_S)) {
                display_model.toggle_shuffle();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_R)) {
                display_model.toggle_repeat();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_N) || ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
                display_model.next_track();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_P) || ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
                display_model.prev_track();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_C)) {
                display_model.toggle_connectivity();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_D)) {
                display_model.start_recording();
            }

            // Direct track selection (1-9 for tracks 1-9, 0 for track 10)
            if (ImGui::IsKeyPressed(ImGuiKey_1) || ImGui::IsKeyPressed(ImGuiKey_Keypad1)) {
                display_model.jump_to_track(1);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_2) || ImGui::IsKeyPressed(ImGuiKey_Keypad2)) {
                display_model.jump_to_track(2);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_3) || ImGui::IsKeyPressed(ImGuiKey_Keypad3)) {
                display_model.jump_to_track(3);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_4) || ImGui::IsKeyPressed(ImGuiKey_Keypad4)) {
                display_model.jump_to_track(4);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_5) || ImGui::IsKeyPressed(ImGuiKey_Keypad5)) {
                display_model.jump_to_track(5);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_6) || ImGui::IsKeyPressed(ImGuiKey_Keypad6)) {
                display_model.jump_to_track(6);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_7) || ImGui::IsKeyPressed(ImGuiKey_Keypad7)) {
                display_model.jump_to_track(7);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_8) || ImGui::IsKeyPressed(ImGuiKey_Keypad8)) {
                display_model.jump_to_track(8);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_9) || ImGui::IsKeyPressed(ImGuiKey_Keypad9)) {
                display_model.jump_to_track(9);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_0) || ImGui::IsKeyPressed(ImGuiKey_Keypad0)) {
                display_model.jump_to_track(10);
            }
        }

        // Update simulation time
        display_model.update(static_cast<double>(io.DeltaTime));

        // Display root window
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGuiWindowFlags host_window_flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoScrollbar;
        if (!display_model.is_eject_menu_open()) {
            host_window_flags |= ImGuiWindowFlags_NoNavInputs;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 20.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

        ImGui::Begin("MyCDsMainWindow", nullptr, host_window_flags);

        // Dark brushed metal faceplate background (rendered with small symmetric tiles)
        mycds::gui::GetBrushedMetal().RenderBackground(
            ImGui::GetWindowDrawList(),
            viewport->WorkPos,
            viewport->WorkSize
        );

        if (display_model.is_eject_menu_open()) {
            mycds::gui::RenderEjectMenu(display_model, ImGui::GetContentRegionAvail());
        } else {
            constexpr float kLeftColumnWidth = 680.0f;
            constexpr float kDisplayHeight = 250.0f;
            constexpr float kMeterHeight = 90.0f;
            constexpr float kControlsHeight = 116.0f;
            constexpr float kColumnGap = 24.0f;
            constexpr float kMinCoverHeight = 140.0f;

            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const float start_y = ImGui::GetCursorScreenPos().y;

            // Left Column: Fixed width clinging to the left edge of the window
            ImGui::BeginGroup();
            {
                // Phase 2.1: CD Player Display Widget
                mycds::gui::RenderCdDisplay(display_model, ImVec2(kLeftColumnWidth, kDisplayHeight));

                // Phase 2.3: LED volume indicator
                mycds::gui::RenderCdVolumeMeter(display_model, ImVec2(kLeftColumnWidth, kMeterHeight));

                // Phase 2.4: Controls Widget (Hardware buttons, backlit cyan pictograms, rotary knobs)
                mycds::gui::RenderCdControls(display_model, ImVec2(kLeftColumnWidth, kControlsHeight));

                // Optional CD Cover block underneath controls when vertical space allows
                const float used_left_h = ImGui::GetCursorScreenPos().y - start_y;
                const float remaining_left_h = avail.y - used_left_h;
                if (remaining_left_h >= kMinCoverHeight) {
                    mycds::gui::RenderCdCoverWidget(display_model, ImVec2(kLeftColumnWidth, remaining_left_h));
                }
            }
            ImGui::EndGroup();

            ImGui::SameLine(0.0f, kColumnGap);

            // Right Column: Dynamic Playlist spanning to the right and bottom of the window
            ImGui::BeginGroup();
            {
                const float actual_playlist_w = std::max(320.0f, avail.x - kLeftColumnWidth - kColumnGap);
                const float actual_playlist_h = std::max(240.0f, avail.y);
                mycds::gui::RenderCdPlaylist(display_model, ImVec2(actual_playlist_w, actual_playlist_h));
            }
            ImGui::EndGroup();
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        had_active_item_last_frame = ImGui::IsAnyItemActive();

        // Rendering
        ImGui::Render();
        int fb_width = 0;
        int fb_height = 0;
        SDL_GetWindowSizeInPixels(window, &fb_width, &fb_height);
        glViewport(0, 0, fb_width, fb_height);
        glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // Cleanup
    mycds::gui::GetBrushedMetal().Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
