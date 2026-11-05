#include <iostream>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include "cd_brushed_metal.h"
#include "cd_display_model.h"
#include "cd_display_widget.h"
#include "cd_playlist_widget.h"
#include "cd_volume_meter_widget.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

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
    SDL_Window* window = SDL_CreateWindow("MyCDs", 1460, 560, window_flags);
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

    if (!mycds::gui::GetBrushedMetal().Initialize(1024, 512)) {
        std::cerr << "Warning: Failed to initialize brushed metal texture\n";
    }

    mycds::gui::CdDisplayModel display_model;

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(window)) {
                running = false;
            }
            if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_Q || event.key.key == 'Q' ||
                    event.key.scancode == SDL_SCANCODE_Q ||
                    event.key.key == SDLK_F10 || event.key.scancode == SDL_SCANCODE_F10) {
                    running = false;
                }
            }
        }

        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Keyboard controls
        if (ImGui::IsKeyPressed(ImGuiKey_Q) || ImGui::IsKeyPressed(ImGuiKey_F10)) {
            running = false;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
            display_model.toggle_play_pause();
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

        // Update simulation time
        display_model.update(static_cast<double>(io.DeltaTime));

        // Display root window
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        constexpr ImGuiWindowFlags host_window_flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 20.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

        ImGui::Begin("MyCDsMainWindow", nullptr, host_window_flags);

        // Dark brushed metal faceplate background
        mycds::gui::GetBrushedMetal().RenderBackground(
            ImGui::GetWindowDrawList(),
            viewport->WorkPos,
            viewport->WorkSize
        );

        constexpr float widget_w = 680.0f;
        constexpr float widget_h = 250.0f;
        constexpr float playlist_w = 680.0f;
        constexpr float playlist_h = 480.0f;

        const float avail_w = ImGui::GetContentRegionAvail().x;
        const float total_panels_w = widget_w + 24.0f + playlist_w;
        const float indent_x = (avail_w > total_panels_w) ? ((avail_w - total_panels_w) * 0.5f) : 0.0f;

        if (indent_x > 0.0f) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent_x);
        }

        // Left Column: Main CD Player Display + Future Widgets Space + Transport Controls
        ImGui::BeginGroup();
        {
            // Phase 2.1: CD Player Display Widget
            mycds::gui::RenderCdDisplay(display_model, ImVec2(widget_w, widget_h));

            // Phase 2.3: LED volume indicator
            mycds::gui::RenderCdVolumeMeter(display_model, ImVec2(widget_w, 90.0f));

            // Transport Controls Bar
            const ImVec2 btn_size(90.0f, 34.0f);

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.21f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.27f, 0.31f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.13f, 0.13f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.38f, 0.40f, 0.46f, 0.75f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

            if (ImGui::Button("|<< Prev", btn_size)) {
                display_model.prev_track();
            }
            ImGui::SameLine();

            const char* play_label = display_model.is_playing() ? "|| Pause" : "> Play";
            if (ImGui::Button(play_label, btn_size)) {
                display_model.toggle_play_pause();
            }
            ImGui::SameLine();

            if (ImGui::Button("Next >>|", btn_size)) {
                display_model.next_track();
            }
            ImGui::SameLine();

            const char* shuf_label = display_model.is_shuffle() ? "Shuffle [ON]" : "Shuffle [OFF]";
            if (ImGui::Button(shuf_label, ImVec2(110.0f, 34.0f))) {
                display_model.toggle_shuffle();
            }
            ImGui::SameLine();

            const char* rep_label = (display_model.repeat_mode() == mycds::gui::RepeatMode::Off)
                                        ? "Repeat [OFF]"
                                        : ((display_model.repeat_mode() == mycds::gui::RepeatMode::Disc)
                                               ? "Repeat [DISC]"
                                               : "Repeat [1]");
            if (ImGui::Button(rep_label, ImVec2(120.0f, 34.0f))) {
                display_model.toggle_repeat();
            }
            ImGui::SameLine();

            const bool conn = (display_model.connectivity_status() == mycds::gui::ConnectivityStatus::Connected);
            const char* net_label = conn ? "Net: Online" : "Net: Offline";
            if (ImGui::Button(net_label, ImVec2(115.0f, 34.0f))) {
                display_model.toggle_connectivity();
            }

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
        }
        ImGui::EndGroup();

        ImGui::SameLine(0.0f, 24.0f);

        // Right Column: Phase 2.2 CD Playlist Widget
        ImGui::BeginGroup();
        {
            const float actual_playlist_w = std::max(playlist_w, avail_w - widget_w - 24.0f - indent_x);
            mycds::gui::RenderCdPlaylist(display_model, ImVec2(actual_playlist_w, playlist_h));
        }
        ImGui::EndGroup();

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

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
