#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

#include "gui/cd_controls_widget.h"
#include "gui/cd_display_model.h"
#include "gui/cd_display_widget.h"
#include "gui/cd_eject_menu_widget.h"
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

    std::filesystem::remove_all(temp_root, ec);
}

TEST(CdDriveEnumerationTests, EnumerateDrivesDoesNotThrow) {
    const auto drives = mycds::enumerate_cd_drives();
    for (const auto& drv : drives) {
        EXPECT_FALSE(drv.devicePath.empty());
    }
}
