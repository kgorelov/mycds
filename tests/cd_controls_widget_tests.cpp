#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

#include "gui/cd_controls_widget.h"
#include "gui/cd_cover_widget.h"
#include "gui/cd_display_model.h"
#include "gui/cd_display_widget.h"
#include "gui/cd_eject_menu_widget.h"
#include "gui/cd_layout.h"
#include "gui/eject_menu_state.h"
#include "imgui.h"
#include "source/physical_cd_source.hpp"

using namespace mycds::gui;

class CdControlsWidgetTest : public ::testing::Test {
protected:
    void SetUp() override {
        imgui_ctx_ = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1460.0f, 600.0f);
        io.DeltaTime = 1.0f / 60.0f;

        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }

    void TearDown() override {
        if (imgui_ctx_) {
            ImGui::DestroyContext(imgui_ctx_);
            imgui_ctx_ = nullptr;
        }
    }

    ImGuiContext* imgui_ctx_{nullptr};
};

TEST_F(CdControlsWidgetTest, InitialControlsState) {
    CdDisplayModel model;

    EXPECT_DOUBLE_EQ(model.volume(), 0.8);
    EXPECT_DOUBLE_EQ(model.balance(), 0.0);
    EXPECT_FALSE(model.is_eject_menu_open());
    EXPECT_TRUE(model.is_playing());
}

TEST_F(CdControlsWidgetTest, VolumeAdjustmentAndClamping) {
    CdDisplayModel model;

    model.set_volume(0.35);
    EXPECT_NEAR(model.volume(), 0.35, 0.001);

    model.set_volume(1.2);
    EXPECT_DOUBLE_EQ(model.volume(), 1.0);

    model.set_volume(-0.5);
    EXPECT_DOUBLE_EQ(model.volume(), 0.0);
}

TEST_F(CdControlsWidgetTest, BalanceAdjustmentAndClamping) {
    CdDisplayModel model;

    model.set_balance(0.75);
    EXPECT_NEAR(model.balance(), 0.75, 0.001);

    model.set_balance(2.0);
    EXPECT_DOUBLE_EQ(model.balance(), 1.0);

    model.set_balance(-3.0);
    EXPECT_DOUBLE_EQ(model.balance(), -1.0);
}

TEST_F(CdControlsWidgetTest, StopResetsPosition) {
    CdDisplayModel model;

    model.update(25.0);
    EXPECT_GT(model.track_elapsed_seconds(), 20.0);
    EXPECT_TRUE(model.is_playing());

    model.stop();
    EXPECT_FALSE(model.is_playing());
    EXPECT_DOUBLE_EQ(model.track_elapsed_seconds(), 0.0);
    EXPECT_EQ(model.elapsed_minutes(), 0);
    EXPECT_EQ(model.elapsed_seconds(), 0);
}

TEST_F(CdControlsWidgetTest, SeekingForwardAndBackward) {
    CdDisplayModel model;

    EXPECT_DOUBLE_EQ(model.track_elapsed_seconds(), 0.0);

    // Forward 30s
    model.seek_forward(30.0);
    EXPECT_NEAR(model.track_elapsed_seconds(), 30.0, 0.001);

    // Backward 10s
    model.seek_backward(10.0);
    EXPECT_NEAR(model.track_elapsed_seconds(), 20.0, 0.001);

    // Backward past start clamps to 0.0
    model.seek_backward(50.0);
    EXPECT_DOUBLE_EQ(model.track_elapsed_seconds(), 0.0);
}

TEST_F(CdControlsWidgetTest, LoadDiscUpdatesAlbumAndTracks) {
    CdDisplayModel model;

    const std::vector<TrackInfo> custom_tracks = {
        {1, 300, "Track One"},
        {2, 240, "Track Two"},
        {3, 180, "Track Three"}
    };

    model.open_eject_menu();
    EXPECT_TRUE(model.is_eject_menu_open());

    model.load_disc("Custom Master Disc", custom_tracks);
    EXPECT_EQ(model.album_title(), "Custom Master Disc");
    EXPECT_EQ(model.total_tracks(), 3);
    EXPECT_EQ(model.current_track_number(), 1);
    EXPECT_EQ(model.tracks()[0].title, "Track One");
    EXPECT_DOUBLE_EQ(model.track_elapsed_seconds(), 0.0);
    EXPECT_TRUE(model.is_playing());
    EXPECT_FALSE(model.is_eject_menu_open());
}

TEST_F(CdControlsWidgetTest, RenderCdControlsHeadlessExecution) {
    CdDisplayModel model;

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(1460, 600));
    ImGui::Begin("TestWindow", nullptr, ImGuiWindowFlags_NoDecoration);

    // Render controls widget
    RenderCdControls(model, ImVec2(680.0f, 116.0f));

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ASSERT_NE(draw_list, nullptr);
    EXPECT_GT(draw_list->CmdBuffer.size(), 0u);
    EXPECT_GT(draw_list->VtxBuffer.size(), 0);

    ImGui::End();
    ImGui::Render();
}

TEST_F(CdControlsWidgetTest, RenderEjectMenuAllScreensHeadlessExecution) {
    CdDisplayModel model;
    model.open_eject_menu();

    const EjectMenuScreen screens[] = {
        EjectMenuScreen::Cd,
        EjectMenuScreen::ImageFile,
        EjectMenuScreen::Library,
        EjectMenuScreen::DiskInfo,
        EjectMenuScreen::Settings
    };

    for (EjectMenuScreen screen : screens) {
        model.set_active_eject_screen(screen);

        for (const ImVec2& menu_size : {ImVec2(1412.0f, 560.0f), ImVec2(640.0f, 560.0f)}) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(1460, 600));
            ImGui::Begin("TestWindow", nullptr, ImGuiWindowFlags_NoDecoration);

            RenderEjectMenu(model, menu_size);

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ASSERT_NE(draw_list, nullptr);
            EXPECT_GT(draw_list->CmdBuffer.size(), 0u);
            EXPECT_GT(draw_list->VtxBuffer.size(), 0);

            ImGui::End();
            ImGui::Render();
        }
    }
}

TEST_F(CdControlsWidgetTest, RenderCdControlsWhileRecording) {
    CdDisplayModel model;
    model.start_recording();
    EXPECT_TRUE(model.is_recording());

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(1460, 600));
    ImGui::Begin("TestWindow", nullptr, ImGuiWindowFlags_NoDecoration);

    RenderCdControls(model, ImVec2(680.0f, 116.0f));

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ASSERT_NE(draw_list, nullptr);
    EXPECT_GT(draw_list->CmdBuffer.size(), 0u);

    ImGui::End();
    ImGui::Render();
}

TEST_F(CdControlsWidgetTest, RenderCdDisplayInRecordingAndNormalModes) {
    CdDisplayModel model;

    // Normal amber mode
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(1460, 600));
    ImGui::Begin("TestWindowAmber", nullptr, ImGuiWindowFlags_NoDecoration);

    RenderCdDisplay(model, ImVec2(680.0f, 250.0f));

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ASSERT_NE(draw_list, nullptr);
    EXPECT_GT(draw_list->CmdBuffer.size(), 0u);

    ImGui::End();
    ImGui::Render();

    // Red recording mode
    model.start_recording();
    EXPECT_TRUE(model.is_recording());

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(1460, 600));
    ImGui::Begin("TestWindowRedRec", nullptr, ImGuiWindowFlags_NoDecoration);

    RenderCdDisplay(model, ImVec2(680.0f, 250.0f));

    draw_list = ImGui::GetWindowDrawList();
    ASSERT_NE(draw_list, nullptr);
    EXPECT_GT(draw_list->CmdBuffer.size(), 0u);

    ImGui::End();
    ImGui::Render();
}

TEST_F(CdControlsWidgetTest, LibraryScreenFocusesSearchAndSupportsUpDownEnter) {
    CdDisplayModel model;
    model.open_eject_menu();
    model.set_active_eject_screen(EjectMenuScreen::Library);

    auto render_frame = [&]() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(1460, 600));
        ImGui::Begin("TestWindowLib", nullptr, ImGuiWindowFlags_NoDecoration);
        RenderEjectMenu(model, ImVec2(1412.0f, 560.0f));
        ImGui::End();
        ImGui::Render();
    };

    // Enable keyboard navigation just like main.cpp does when the eject menu is open
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Frame 1: entering Library calls SetKeyboardFocusHere()
    render_frame();
    // Frame 2: ImGui resolves the nav focus request onto ##LibSearch
    render_frame();
    // Frame 3: NewFrame() updates io.WantTextInput == true for the active search InputText
    render_frame();
    EXPECT_TRUE(ImGui::GetIO().WantTextInput);

    // Frame 4: press Down arrow to move from index 0 (Dark Side) to index 1 (Abbey Road)
    // Search bar must retain text input focus!
    ImGui::GetIO().AddKeyEvent(ImGuiKey_DownArrow, true);
    render_frame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_DownArrow, false);
    render_frame();
    EXPECT_TRUE(ImGui::GetIO().WantTextInput);

    // Frame 6: press Enter to load the selected album
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
    render_frame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);

    EXPECT_EQ(model.album_title(), "Abbey Road");
    EXPECT_EQ(model.artist_name(), "The Beatles");
}

TEST(FileBrowserStateTests, FiltersCueAndTocFilesAndNavigatesDirectories) {
    const std::filesystem::path temp_root =
        std::filesystem::temp_directory_path() / "mycds_file_browser_test";
    std::error_code ec;
    std::filesystem::remove_all(temp_root, ec);
    std::filesystem::create_directories(temp_root / "subdir", ec);

    { std::ofstream(temp_root / "album.cue") << "FILE \"album.bin\" BINARY\n"; }
    { std::ofstream(temp_root / "disc.TOC") << "CD_DA\n"; }
    { std::ofstream(temp_root / "notes.txt") << "ignored\n"; }
    { std::ofstream(temp_root / "subdir" / "nested.cue") << "FILE \"n.bin\" BINARY\n"; }

    FileBrowserState browser(temp_root);
    EXPECT_EQ(browser.current_dir(), std::filesystem::weakly_canonical(temp_root));
    EXPECT_TRUE(browser.error_message().empty());

    // Should list 1 directory ("subdir") + 2 supported files ("album.cue", "disc.TOC")
    ASSERT_EQ(browser.entries().size(), 3u);
    EXPECT_TRUE(browser.entries()[0].isDirectory);
    EXPECT_EQ(browser.entries()[0].name, "subdir");
    EXPECT_FALSE(browser.entries()[1].isDirectory);
    EXPECT_EQ(browser.entries()[1].name, "album.cue");
    EXPECT_FALSE(browser.entries()[2].isDirectory);
    EXPECT_EQ(browser.entries()[2].name, "disc.TOC");

    // Selecting a file updates selected_file
    browser.select_entry(1);
    EXPECT_EQ(browser.selected_file().filename(), "album.cue");

    // Activating a directory enters it and does not trigger load
    EXPECT_FALSE(browser.activate_entry(0));
    EXPECT_EQ(browser.current_dir().filename(), "subdir");
    ASSERT_EQ(browser.entries().size(), 1u);
    EXPECT_EQ(browser.entries()[0].name, "nested.cue");

    // Activating a supported file returns true
    EXPECT_TRUE(browser.activate_entry(0));
    EXPECT_EQ(browser.selected_file().filename(), "nested.cue");

    // Navigate up returns to temp_root
    EXPECT_TRUE(browser.navigate_up());
    EXPECT_EQ(browser.current_dir(), std::filesystem::weakly_canonical(temp_root));

    // Enabling show_all_files includes notes.txt
    browser.set_show_all_files(true);
    EXPECT_EQ(browser.entries().size(), 4u);

    // Move selection with clamping
    browser.select_entry(0);
    EXPECT_EQ(browser.selected_index(), 0u);
    browser.move_selection(-5);
    EXPECT_EQ(browser.selected_index(), 0u);
    browser.move_selection(2);
    EXPECT_EQ(browser.selected_index(), 2u);
    EXPECT_EQ(browser.selected_file().filename(), "disc.TOC");
    browser.move_selection(100);
    EXPECT_EQ(browser.selected_index(), 3u);

    // Substring filtering
    browser.set_filter_query("ALB");
    ASSERT_EQ(browser.entries().size(), 1u);
    EXPECT_EQ(browser.entries()[0].name, "album.cue");
    EXPECT_EQ(browser.selected_file().filename(), "album.cue");

    browser.clear_filter_query();
    EXPECT_EQ(browser.entries().size(), 4u);

    std::filesystem::remove_all(temp_root, ec);
}

TEST(FileBrowserStateTests, HidesDotfilesUnlessShowAllFilesEnabled) {
    const std::filesystem::path temp_root =
        std::filesystem::temp_directory_path() / "mycds_dotfiles_test";
    std::error_code ec;
    std::filesystem::remove_all(temp_root, ec);
    std::filesystem::create_directories(temp_root / "visible_dir", ec);
    std::filesystem::create_directories(temp_root / ".hidden_dir", ec);

    { std::ofstream(temp_root / "visible.cue") << "FILE \"v.bin\" BINARY\n"; }
    { std::ofstream(temp_root / ".hidden.cue") << "FILE \"h.bin\" BINARY\n"; }
    { std::ofstream(temp_root / ".hidden_notes.txt") << "secret\n"; }
    { std::ofstream(temp_root / "notes.txt") << "plain\n"; }

    FileBrowserState browser(temp_root);
    EXPECT_FALSE(browser.show_all_files());

    // By default, only visible_dir and visible.cue should appear
    ASSERT_EQ(browser.entries().size(), 2u);
    EXPECT_TRUE(browser.entries()[0].isDirectory);
    EXPECT_EQ(browser.entries()[0].name, "visible_dir");
    EXPECT_FALSE(browser.entries()[1].isDirectory);
    EXPECT_EQ(browser.entries()[1].name, "visible.cue");

    // Enabling "Show all files" reveals hidden directories, hidden files, and non-image files
    browser.set_show_all_files(true);
    ASSERT_EQ(browser.entries().size(), 6u);
    EXPECT_EQ(browser.entries()[0].name, ".hidden_dir");
    EXPECT_EQ(browser.entries()[1].name, "visible_dir");
    EXPECT_EQ(browser.entries()[2].name, ".hidden.cue");
    EXPECT_EQ(browser.entries()[3].name, ".hidden_notes.txt");
    EXPECT_EQ(browser.entries()[4].name, "notes.txt");
    EXPECT_EQ(browser.entries()[5].name, "visible.cue");

    // Disabling it again hides dotfiles and non-image files
    browser.set_show_all_files(false);
    ASSERT_EQ(browser.entries().size(), 2u);
    EXPECT_EQ(browser.entries()[0].name, "visible_dir");
    EXPECT_EQ(browser.entries()[1].name, "visible.cue");

    std::filesystem::remove_all(temp_root, ec);
}

TEST(CdDriveEnumerationTests, EnumerateDrivesDoesNotThrow) {
    const auto drives = mycds::enumerate_cd_drives();
    for (const auto& drv : drives) {
        EXPECT_FALSE(drv.devicePath.empty());
    }
}

TEST(LibraryCatalogStateTests, SearchFilterAndSortWorkDeterministically) {
    LibraryCatalogState lib;
    EXPECT_GE(lib.filtered_entries().size(), 5u);

    // Search by track title
    lib.set_search_query("Get Lucky");
    auto res = lib.filtered_entries();
    ASSERT_EQ(res.size(), 1u);
    EXPECT_EQ(res[0]->album, "Random Access Memories");

    // Search by artist + sort by year
    lib.set_search_query("Pink Floyd");
    lib.set_sort_mode(LibrarySortMode::YearNewest);
    res = lib.filtered_entries();
    ASSERT_EQ(res.size(), 2u);
    EXPECT_EQ(res[0]->year, 1975);
    EXPECT_EQ(res[1]->year, 1973);

    lib.set_sort_mode(LibrarySortMode::YearOldest);
    res = lib.filtered_entries();
    ASSERT_EQ(res.size(), 2u);
    EXPECT_EQ(res[0]->year, 1973);
    EXPECT_EQ(res[1]->year, 1975);

    // Clear search and filter by genre + sort by recently added
    lib.set_search_query("");
    lib.set_genre_filter("Rock");
    lib.set_sort_mode(LibrarySortMode::RecentlyAdded);
    res = lib.filtered_entries();
    ASSERT_EQ(res.size(), 2u);
    EXPECT_GE(res[0]->addedDate, res[1]->addedDate);
}

TEST(DiskInfoStateTests, InlayCyclingAndEditorialSources) {
    DiskInfoState info;
    ASSERT_GE(info.inlay_slots().size(), 2u);
    EXPECT_EQ(info.active_inlay_index(), 0u);

    info.next_inlay();
    EXPECT_EQ(info.active_inlay_index(), 1u);

    info.prev_inlay();
    EXPECT_EQ(info.active_inlay_index(), 0u);

    info.prev_inlay();
    EXPECT_EQ(info.active_inlay_index(), info.inlay_slots().size() - 1);

    const auto sources = info.editorial_sources_for_album("Pink Floyd", "The Dark Side of the Moon");
    EXPECT_GE(sources.size(), 2u);
}

TEST(AppSettingsTests, PersistenceAndAutoPlayOnDiscLoad) {
    const std::filesystem::path settings_path =
        std::filesystem::temp_directory_path() / "mycds_settings_test" / "settings.json";
    std::error_code ec;
    std::filesystem::remove_all(settings_path.parent_path(), ec);

    AppSettings saved{false, false};
    EXPECT_TRUE(save_app_settings(settings_path, saved));

    const AppSettings loaded = load_app_settings(settings_path);
    EXPECT_FALSE(loaded.play_when_started);
    EXPECT_FALSE(loaded.auto_play_on_disc_load);

    CdDisplayModel model;
    model.set_app_settings(loaded);
    model.load_disc("Artist", "Album", {{1, 180, "Track 1"}});
    EXPECT_FALSE(model.is_playing());

    model.set_auto_play_on_disc_load(true);
    model.load_disc("Artist", "Album 2", {{1, 180, "Track 1"}});
    EXPECT_TRUE(model.is_playing());

    std::filesystem::remove_all(settings_path.parent_path(), ec);
}

TEST_F(CdControlsWidgetTest, RenderCdCoverWidgetHeadlessExecution) {
    CdDisplayModel model;

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(1460, 800));
    ImGui::Begin("TestWindowCover", nullptr, ImGuiWindowFlags_NoDecoration);

    RenderCdCoverWidget(model, ImVec2(680.0f, 180.0f));

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ASSERT_NE(draw_list, nullptr);
    EXPECT_GT(draw_list->CmdBuffer.size(), 0u);
    EXPECT_GT(draw_list->VtxBuffer.size(), 0);

    ImGui::End();
    ImGui::Render();
}

TEST(CdLayoutTests, MinimumWindowDimensionsCoverMandatoryWidgets) {
    EXPECT_EQ(compute_min_window_width(), 728);
    EXPECT_EQ(compute_min_window_height(), 504);

    AppSettings settings;
    const LayoutPlan plan = compute_layout_plan(24.0f, 20.0f, 680.0f, 464.0f, settings);
    EXPECT_FLOAT_EQ(plan.display_rect.width, 680.0f);
    EXPECT_FLOAT_EQ(plan.display_rect.height, 250.0f);
    EXPECT_FLOAT_EQ(plan.meter_rect.width, 680.0f);
    EXPECT_FLOAT_EQ(plan.meter_rect.height, 90.0f);
    EXPECT_FLOAT_EQ(plan.controls_rect.width, 680.0f);
    EXPECT_FLOAT_EQ(plan.controls_rect.height, 116.0f);
    EXPECT_FALSE(plan.show_playlist);
    EXPECT_FALSE(plan.show_cover);
}

TEST(CdLayoutTests, DynamicLayoutTransitionsAcrossSizes) {
    AppSettings settings;
    settings.layout_mode = LayoutMode::Dynamic;

    // Default wide & short window (1460x532 -> 1412x492 content): Horizontal + playlist, no cover
    {
        const LayoutPlan plan = compute_layout_plan(0.0f, 0.0f, 1412.0f, 492.0f, settings);
        EXPECT_EQ(plan.resolved_layout, ResolvedLayout::Horizontal);
        EXPECT_TRUE(plan.show_playlist);
        EXPECT_GE(plan.playlist_rect.width, LayoutConstants::kMinPlaylistWidth);
        EXPECT_FALSE(plan.show_cover);
    }

    // Wide & tall window (1460x720 -> 1412x680 content): Horizontal + playlist + fixed-width cover canvas
    {
        const LayoutPlan plan = compute_layout_plan(0.0f, 0.0f, 1412.0f, 680.0f, settings);
        EXPECT_EQ(plan.resolved_layout, ResolvedLayout::Horizontal);
        EXPECT_TRUE(plan.show_playlist);
        EXPECT_TRUE(plan.show_cover);
        EXPECT_FLOAT_EQ(plan.cover_rect.x, plan.display_rect.x);
        EXPECT_FLOAT_EQ(plan.cover_rect.width, LayoutConstants::kDeckWidth);
        EXPECT_GE(plan.cover_rect.height, LayoutConstants::kMinCoverHeight);
    }

    // Narrow & tall window (800x800 -> 752x760 content): Vertical + playlist below deck, no cover
    {
        const LayoutPlan plan = compute_layout_plan(0.0f, 0.0f, 752.0f, 760.0f, settings);
        EXPECT_EQ(plan.resolved_layout, ResolvedLayout::Vertical);
        EXPECT_TRUE(plan.show_playlist);
        EXPECT_GE(plan.playlist_rect.y, plan.controls_rect.y + plan.controls_rect.height);
        EXPECT_GE(plan.playlist_rect.height, LayoutConstants::kMinPlaylistHeight);
        EXPECT_FALSE(plan.show_cover);
    }
}

TEST(CdLayoutTests, ExplicitHorizontalAndVerticalAndVisibilityFlags) {
    AppSettings settings;

    // Explicit Horizontal in narrow & tall window: stays Horizontal, hides playlist, shows cover
    settings.layout_mode = LayoutMode::Horizontal;
    {
        const LayoutPlan plan = compute_layout_plan(0.0f, 0.0f, 752.0f, 760.0f, settings);
        EXPECT_EQ(plan.resolved_layout, ResolvedLayout::Horizontal);
        EXPECT_FALSE(plan.show_playlist);
        EXPECT_TRUE(plan.show_cover);
    }

    // Explicit Vertical in wide & tall window: stays Vertical, places playlist below, hides cover
    settings.layout_mode = LayoutMode::Vertical;
    {
        const LayoutPlan plan = compute_layout_plan(0.0f, 0.0f, 1412.0f, 760.0f, settings);
        EXPECT_EQ(plan.resolved_layout, ResolvedLayout::Vertical);
        EXPECT_TRUE(plan.show_playlist);
        EXPECT_FLOAT_EQ(plan.playlist_rect.width, LayoutConstants::kDeckWidth);
        EXPECT_FALSE(plan.show_cover);
    }

    // Visibility flags disable optional widgets even when space permits
    settings.layout_mode = LayoutMode::Dynamic;
    settings.show_playlist = false;
    settings.show_cd_cover = false;
    {
        const LayoutPlan plan = compute_layout_plan(0.0f, 0.0f, 1412.0f, 760.0f, settings);
        EXPECT_FALSE(plan.show_playlist);
        EXPECT_FALSE(plan.show_cover);
    }
}

TEST(AppSettingsTests, LayoutAndVisibilityPersistence) {
    const std::filesystem::path settings_path =
        std::filesystem::temp_directory_path() / "mycds_layout_settings_test" / "settings.json";
    std::error_code ec;
    std::filesystem::remove_all(settings_path.parent_path(), ec);

    AppSettings saved;
    saved.layout_mode = LayoutMode::Vertical;
    saved.show_playlist = false;
    saved.show_cd_cover = true;
    saved.file_browser_view = FileBrowserViewMode::Icons;
    saved.library_view = LibraryViewMode::Covers;
    EXPECT_TRUE(save_app_settings(settings_path, saved));

    const AppSettings loaded = load_app_settings(settings_path);
    EXPECT_EQ(loaded.layout_mode, LayoutMode::Vertical);
    EXPECT_FALSE(loaded.show_playlist);
    EXPECT_TRUE(loaded.show_cd_cover);
    EXPECT_EQ(loaded.file_browser_view, FileBrowserViewMode::Icons);
    EXPECT_EQ(loaded.library_view, LibraryViewMode::Covers);

    std::filesystem::remove_all(settings_path.parent_path(), ec);
}

TEST_F(CdControlsWidgetTest, RenderImageFileScreenBothViewModesHeadless) {
    CdDisplayModel model;
    model.open_eject_menu();
    model.set_active_eject_screen(EjectMenuScreen::ImageFile);

    for (FileBrowserViewMode mode : {FileBrowserViewMode::List, FileBrowserViewMode::Icons}) {
        AppSettings settings = model.app_settings();
        settings.file_browser_view = mode;
        model.set_app_settings(settings);

        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(1460, 600));
        ImGui::Begin("TestWindow", nullptr, ImGuiWindowFlags_NoDecoration);

        RenderEjectMenu(model, ImVec2(1412.0f, 560.0f));

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ASSERT_NE(draw_list, nullptr);
        EXPECT_GT(draw_list->CmdBuffer.size(), 0u);
        EXPECT_GT(draw_list->VtxBuffer.size(), 0);

        ImGui::End();
        ImGui::Render();
    }
}

TEST_F(CdControlsWidgetTest, RenderLibraryScreenBothViewModesHeadless) {
    CdDisplayModel model;
    model.open_eject_menu();
    model.set_active_eject_screen(EjectMenuScreen::Library);

    for (LibraryViewMode mode : {LibraryViewMode::List, LibraryViewMode::Covers}) {
        AppSettings settings = model.app_settings();
        settings.library_view = mode;
        model.set_app_settings(settings);

        for (const ImVec2& menu_size : {ImVec2(1412.0f, 560.0f), ImVec2(640.0f, 560.0f)}) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(1460, 600));
            ImGui::Begin("TestWindowLibView", nullptr, ImGuiWindowFlags_NoDecoration);

            RenderEjectMenu(model, menu_size);

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ASSERT_NE(draw_list, nullptr);
            EXPECT_GT(draw_list->CmdBuffer.size(), 0u);
            EXPECT_GT(draw_list->VtxBuffer.size(), 0);

            ImGui::End();
            ImGui::Render();
        }
    }
}

TEST(UiStateTests, PersistenceRoundTrip) {
    const std::filesystem::path state_path =
        std::filesystem::temp_directory_path() / "mycds_ui_state_test" / "state.json";
    std::error_code ec;
    std::filesystem::remove_all(state_path.parent_path(), ec);

    PersistentUiState saved;
    saved.last_eject_screen = EjectMenuScreen::ImageFile;
    saved.last_visited_dir = "/tmp/music/Pink Floyd \"Wish\"";
    saved.last_cd_rom = "/dev/sr1";
    EXPECT_TRUE(save_ui_state(state_path, saved));

    const PersistentUiState loaded = load_ui_state(state_path);
    EXPECT_EQ(loaded.last_eject_screen, EjectMenuScreen::ImageFile);
    EXPECT_EQ(loaded.last_visited_dir, "/tmp/music/Pink Floyd \"Wish\"");
    EXPECT_EQ(loaded.last_cd_rom, "/dev/sr1");

    std::filesystem::remove_all(state_path.parent_path(), ec);
}
