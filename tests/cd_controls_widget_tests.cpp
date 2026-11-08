#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "gui/cd_controls_widget.h"
#include "gui/cd_display_model.h"
#include "imgui.h"

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
    EXPECT_FALSE(model.is_eject_dialog_open());
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

    model.open_eject_dialog();
    EXPECT_TRUE(model.is_eject_dialog_open());

    model.load_disc("Custom Master Disc", custom_tracks);
    EXPECT_EQ(model.album_title(), "Custom Master Disc");
    EXPECT_EQ(model.total_tracks(), 3);
    EXPECT_EQ(model.current_track_number(), 1);
    EXPECT_EQ(model.tracks()[0].title, "Track One");
    EXPECT_DOUBLE_EQ(model.track_elapsed_seconds(), 0.0);
    EXPECT_TRUE(model.is_playing());
    EXPECT_FALSE(model.is_eject_dialog_open());
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

TEST_F(CdControlsWidgetTest, RenderCdControlsWithEjectModalOpen) {
    CdDisplayModel model;
    model.open_eject_dialog();

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
