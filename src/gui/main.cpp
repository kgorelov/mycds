#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include "cd_brushed_metal.h"
#include "cd_controls_widget.h"
#include "cd_cover_widget.h"
#include "cd_display_model.h"
#include "cd_display_widget.h"
#include "cd_eject_menu_widget.h"
#include "cd_layout.h"
#include "cd_playlist_widget.h"
#include "cd_volume_meter_widget.h"
#include "eject_menu_state.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"
#include "source/disc_source.hpp"

namespace {

void print_gui_usage(std::string_view prog) {
    std::cout << "Usage: " << prog << " [options] [device_or_image]\n\n"
              << "Options:\n"
              << "  --device <path>         Open physical CD drive (e.g. /dev/sr0)\n"
              << "  --image <path>          Open disc image (.cue or .toc)\n"
              << "  --track, -t <num>       Jump to track number on startup\n"
              << "  --seek, -s <time>       Seek to time (SS or MM:SS) within track (with -t) or from disc start\n"
              << "  --audio-driver <name>   Preferred SDL3 audio driver (e.g. pipewire, alsa, dummy)\n"
              << "  --stopped, --paused     Start in stopped state\n"
              << "  --help, -h              Show this help message\n";
}

void persist_playback_state(const mycds::gui::CdDisplayModel& model) {
    mycds::gui::PersistentUiState st =
        mycds::gui::load_ui_state(mycds::gui::default_ui_state_path());
    if (model.has_real_source()) {
        st.last_source_path = model.source_path();
        st.last_source_is_device = model.source_is_device();
        st.last_track = std::max(1, model.current_track_number());
        st.last_position_seconds = std::max(0.0, model.track_elapsed_seconds());
    } else if (!model.has_disc()) {
        st.last_source_path.clear();
        st.last_source_is_device = false;
        st.last_track = 1;
        st.last_position_seconds = 0.0;
    }
    mycds::gui::save_ui_state(mycds::gui::default_ui_state_path(), st);
}

} // namespace

int main(int argc, char* argv[]) {
    std::optional<std::string> cli_source_path;
    bool cli_source_is_device = false;
    bool cli_source_type_explicit = false;
    bool force_stopped = false;
    std::string audio_driver;
    std::optional<int> cli_track;
    std::optional<double> cli_seek_seconds;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            print_gui_usage(argc > 0 && argv[0] ? argv[0] : "mycds");
            return 0;
        }
        if (arg == "--stopped" || arg == "--paused") {
            force_stopped = true;
        } else if (arg == "--device" || arg == "--image") {
            if (i + 1 >= argc) {
                std::cerr << "Error: '" << arg << "' requires a path argument.\n";
                return 1;
            }
            cli_source_is_device = (arg == "--device");
            cli_source_type_explicit = true;
            cli_source_path = argv[++i];
        } else if (arg == "--track" || arg == "-t") {
            if (i + 1 >= argc) {
                std::cerr << "Error: '" << arg << "' requires a track number.\n";
                return 1;
            }
            try {
                std::size_t idx = 0;
                const std::string val_str(argv[++i]);
                const int trk = std::stoi(val_str, &idx);
                if (idx != val_str.size() || trk < 1) {
                    std::cerr << "Error: Invalid track number '" << val_str << "'.\n";
                    return 1;
                }
                cli_track = trk;
            } catch (...) {
                std::cerr << "Error: Invalid track number.\n";
                return 1;
            }
        } else if (arg == "--seek" || arg == "-s") {
            if (i + 1 >= argc) {
                std::cerr << "Error: '" << arg << "' requires a time argument.\n";
                return 1;
            }
            const std::string_view val_sv(argv[++i]);
            const auto parsed = mycds::gui::parse_seek_time_seconds(val_sv);
            if (!parsed.has_value()) {
                std::cerr << "Error: Invalid seek time '" << val_sv
                          << "' (expected seconds or MM:SS).\n";
                return 1;
            }
            cli_seek_seconds = *parsed;
        } else if (arg == "--audio-driver") {
            if (i + 1 >= argc) {
                std::cerr << "Error: '--audio-driver' requires a driver name.\n";
                return 1;
            }
            audio_driver = argv[++i];
        } else if (!arg.empty() && arg[0] != '-') {
            cli_source_path = std::string(arg);
            cli_source_type_explicit = false;
        } else {
            std::cerr << "Error: Unknown argument '" << arg << "'.\n";
            print_gui_usage(argc > 0 && argv[0] ? argv[0] : "mycds");
            return 1;
        }
    }

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
    SDL_SetWindowMinimumSize(
        window,
        mycds::gui::compute_min_window_width(),
        mycds::gui::compute_min_window_height());

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

    mycds::PlaybackControllerConfig controller_config;
    controller_config.audioConfig.preferredDriver = audio_driver;

    mycds::gui::CdDisplayModel display_model(controller_config);
    display_model.clear_disc();

    const mycds::gui::AppSettings startup_settings =
        mycds::gui::load_app_settings(mycds::gui::default_settings_path());
    display_model.set_app_settings(startup_settings);
    const mycds::gui::PersistentUiState startup_ui_state =
        mycds::gui::load_ui_state(mycds::gui::default_ui_state_path());
    display_model.set_active_eject_screen(startup_ui_state.last_eject_screen);

    bool cli_load_failed = false;
    bool resumed_from_state = false;
    if (cli_source_path.has_value()) {
        const bool is_dev = cli_source_type_explicit
            ? cli_source_is_device
            : (cli_source_path->rfind("/dev/", 0) == 0);
        try {
            auto src = is_dev
                ? mycds::open_physical_cd_source(*cli_source_path)
                : mycds::open_image_source(*cli_source_path);
            std::string err;
            if (!display_model.open_disc_source(std::move(src), &err)) {
                std::cerr << "Failed to open '" << *cli_source_path << "': " << err << '\n';
                cli_load_failed = true;
            }
        } catch (const std::exception& ex) {
            std::cerr << "Failed to open '" << *cli_source_path << "': " << ex.what() << '\n';
            cli_load_failed = true;
        }
    } else if (!startup_ui_state.last_source_path.empty()) {
        try {
            auto src = startup_ui_state.last_source_is_device
                ? mycds::open_physical_cd_source(startup_ui_state.last_source_path)
                : mycds::open_image_source(startup_ui_state.last_source_path);
            std::string err;
            if (display_model.open_disc_source(std::move(src), &err)) {
                resumed_from_state = true;
            } else {
                std::cerr << "Warning: Could not restore previous disc '"
                          << startup_ui_state.last_source_path << "': " << err << '\n';
            }
        } catch (const std::exception& ex) {
            std::cerr << "Warning: Could not restore previous disc '"
                      << startup_ui_state.last_source_path << "': " << ex.what() << '\n';
        }
    }

    if (cli_load_failed) {
        display_model.eject_disc();
        mycds::gui::GetBrushedMetal().Shutdown();
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_GL_DestroyContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (display_model.has_real_source()) {
        const bool should_play = startup_settings.play_when_started && !force_stopped;
        if (!should_play) {
            display_model.stop();
        }
        if (cli_track.has_value()) {
            display_model.seek_to_track_and_seconds(*cli_track, cli_seek_seconds.value_or(0.0));
        } else if (cli_seek_seconds.has_value()) {
            display_model.seek_to_disc_seconds(*cli_seek_seconds);
        } else if (resumed_from_state) {
            display_model.seek_to_track_and_seconds(
                startup_ui_state.last_track,
                startup_ui_state.last_position_seconds);
        }
        if (should_play && !display_model.is_playing()) {
            display_model.play();
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
            const bool fast_rew = ImGui::IsKeyDown(ImGuiKey_LeftBracket) ||
                                  (io.KeyShift && ImGui::IsKeyDown(ImGuiKey_LeftArrow));
            const bool fast_ff = ImGui::IsKeyDown(ImGuiKey_RightBracket) ||
                                 (io.KeyShift && ImGui::IsKeyDown(ImGuiKey_RightArrow));
            if (fast_rew) {
                display_model.seek_backward(60.0 * seek_dt);
            } else if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
                display_model.seek_backward(20.0 * seek_dt);
            }
            if (fast_ff) {
                display_model.seek_forward(60.0 * seek_dt);
            } else if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
                display_model.seek_forward(20.0 * seek_dt);
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

        // Periodically persist active disc/track/position (every 5s)
        static Uint64 last_persist_ticks = SDL_GetTicks();
        if (now_ticks - last_persist_ticks >= 5000) {
            last_persist_ticks = now_ticks;
            persist_playback_state(display_model);
        }

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

        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(mycds::gui::LayoutConstants::kWindowPaddingX,
                   mycds::gui::LayoutConstants::kWindowPaddingY));
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
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const mycds::gui::LayoutPlan plan = mycds::gui::compute_layout_plan(
                origin.x,
                origin.y,
                avail.x,
                avail.y,
                display_model.app_settings(),
                ImGui::GetStyle().ItemSpacing.y);

            // Mandatory widgets: always visible
            ImGui::SetCursorScreenPos(ImVec2(plan.display_rect.x, plan.display_rect.y));
            mycds::gui::RenderCdDisplay(
                display_model,
                ImVec2(plan.display_rect.width, plan.display_rect.height));

            ImGui::SetCursorScreenPos(ImVec2(plan.meter_rect.x, plan.meter_rect.y));
            mycds::gui::RenderCdVolumeMeter(
                display_model,
                ImVec2(plan.meter_rect.width, plan.meter_rect.height));

            ImGui::SetCursorScreenPos(ImVec2(plan.controls_rect.x, plan.controls_rect.y));
            mycds::gui::RenderCdControls(
                display_model,
                ImVec2(plan.controls_rect.width, plan.controls_rect.height));

            // Optional CD cover widget
            if (plan.show_cover) {
                ImGui::SetCursorScreenPos(ImVec2(plan.cover_rect.x, plan.cover_rect.y));
                mycds::gui::RenderCdCoverWidget(
                    display_model,
                    ImVec2(plan.cover_rect.width, plan.cover_rect.height));
            }

            // Optional playlist widget
            if (plan.show_playlist) {
                ImGui::SetCursorScreenPos(ImVec2(plan.playlist_rect.x, plan.playlist_rect.y));
                mycds::gui::RenderCdPlaylist(
                    display_model,
                    ImVec2(plan.playlist_rect.width, plan.playlist_rect.height));
            }
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
    persist_playback_state(display_model);
    display_model.eject_disc();
    mycds::gui::GetBrushedMetal().Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
