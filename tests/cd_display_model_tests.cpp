#include <gtest/gtest.h>

#include "gui/cd_display_model.h"

using namespace mycds::gui;

TEST(CdDisplayModelTests, InitialState) {
    CdDisplayModel model;

    EXPECT_TRUE(model.is_playing());
    EXPECT_EQ(model.current_track_number(), 1);
    EXPECT_EQ(model.total_tracks(), 12);
    EXPECT_DOUBLE_EQ(model.track_elapsed_seconds(), 0.0);
    EXPECT_EQ(model.elapsed_minutes(), 0);
    EXPECT_EQ(model.elapsed_seconds(), 0);
    EXPECT_FALSE(model.is_shuffle());
    EXPECT_EQ(model.repeat_mode(), RepeatMode::Off);
    EXPECT_EQ(model.connectivity_status(), ConnectivityStatus::Connected);

    // Initial uptime < 3.0s means cloud and cddb indicators are disabled
    EXPECT_FALSE(model.is_cloud_active());
    EXPECT_FALSE(model.is_cddb_active());
}

TEST(CdDisplayModelTests, CloudAndCddbTimerAtThreeSeconds) {
    CdDisplayModel model;

    model.update(2.5);
    EXPECT_FALSE(model.is_cloud_active());
    EXPECT_FALSE(model.is_cddb_active());

    model.update(0.5); // total uptime = 3.0s
    EXPECT_TRUE(model.is_cloud_active());
    EXPECT_TRUE(model.is_cddb_active());
}

TEST(CdDisplayModelTests, TimeTickingWhenPlayingAndPaused) {
    CdDisplayModel model;

    model.update(15.2);
    EXPECT_NEAR(model.track_elapsed_seconds(), 15.2, 0.001);
    EXPECT_EQ(model.elapsed_minutes(), 0);
    EXPECT_EQ(model.elapsed_seconds(), 15);

    // Pause playback
    model.pause();
    EXPECT_FALSE(model.is_playing());

    model.update(10.0);
    EXPECT_NEAR(model.track_elapsed_seconds(), 15.2, 0.001);

    // Resume playback
    model.toggle_play_pause();
    EXPECT_TRUE(model.is_playing());

    model.update(60.0);
    EXPECT_NEAR(model.track_elapsed_seconds(), 75.2, 0.001);
    EXPECT_EQ(model.elapsed_minutes(), 1);
    EXPECT_EQ(model.elapsed_seconds(), 15);
}

TEST(CdDisplayModelTests, RepeatModeCycling) {
    CdDisplayModel model;

    EXPECT_EQ(model.repeat_mode(), RepeatMode::Off);

    model.toggle_repeat();
    EXPECT_EQ(model.repeat_mode(), RepeatMode::Disc);

    model.toggle_repeat();
    EXPECT_EQ(model.repeat_mode(), RepeatMode::Track);

    model.toggle_repeat();
    EXPECT_EQ(model.repeat_mode(), RepeatMode::Off);
}

TEST(CdDisplayModelTests, ShuffleToggle) {
    CdDisplayModel model;

    EXPECT_FALSE(model.is_shuffle());
    model.toggle_shuffle();
    EXPECT_TRUE(model.is_shuffle());
    model.toggle_shuffle();
    EXPECT_FALSE(model.is_shuffle());
}

TEST(CdDisplayModelTests, TrackNavigation) {
    CdDisplayModel model;

    EXPECT_EQ(model.current_track_number(), 1);

    model.next_track();
    EXPECT_EQ(model.current_track_number(), 2);
    EXPECT_DOUBLE_EQ(model.track_elapsed_seconds(), 0.0);

    // Previous track logic:
    // If elapsed time > 2s, should rewind to 0:00 on current track
    model.update(5.0);
    EXPECT_GT(model.track_elapsed_seconds(), 2.0);
    model.prev_track();
    EXPECT_EQ(model.current_track_number(), 2);
    EXPECT_DOUBLE_EQ(model.track_elapsed_seconds(), 0.0);

    // If elapsed time <= 2s, should go to previous track (1)
    model.prev_track();
    EXPECT_EQ(model.current_track_number(), 1);
}

TEST(CdDisplayModelTests, ConnectivityToggle) {
    CdDisplayModel model;

    EXPECT_EQ(model.connectivity_status(), ConnectivityStatus::Connected);
    model.toggle_connectivity();
    EXPECT_EQ(model.connectivity_status(), ConnectivityStatus::Disconnected);
    model.toggle_connectivity();
    EXPECT_EQ(model.connectivity_status(), ConnectivityStatus::Connected);
}

TEST(CdDisplayModelTests, JumpToTrackAndPlaylistInfo) {
    CdDisplayModel model;

    EXPECT_EQ(model.total_tracks(), 12);
    EXPECT_FALSE(model.tracks()[0].title.empty());

    model.update(10.0);
    EXPECT_GT(model.track_elapsed_seconds(), 0.0);

    model.jump_to_track(5);
    EXPECT_EQ(model.current_track_number(), 5);
    EXPECT_DOUBLE_EQ(model.track_elapsed_seconds(), 0.0);

    model.jump_to_track(10);
    EXPECT_EQ(model.current_track_number(), 10);

    // Out of bounds jumps should be ignored
    model.jump_to_track(0);
    EXPECT_EQ(model.current_track_number(), 10);

    model.jump_to_track(99);
    EXPECT_EQ(model.current_track_number(), 10);
}

TEST(CdDisplayModelTests, VolumeLevelsInitialStateAndDynamicTicking) {
    CdDisplayModel model;

    EXPECT_DOUBLE_EQ(model.left_level(), 0.0);
    EXPECT_DOUBLE_EQ(model.right_level(), 0.0);
    EXPECT_DOUBLE_EQ(model.left_peak(), 0.0);
    EXPECT_DOUBLE_EQ(model.right_peak(), 0.0);

    model.update(0.1);
    EXPECT_GT(model.left_level(), 0.0);
    EXPECT_GT(model.right_level(), 0.0);
    EXPECT_LE(model.left_level(), 1.0);
    EXPECT_LE(model.right_level(), 1.0);
    EXPECT_GE(model.left_peak(), model.left_level());
    EXPECT_GE(model.right_peak(), model.right_level());
}

TEST(CdDisplayModelTests, VolumeLevelsEmptyWhenPaused) {
    CdDisplayModel model;

    model.update(0.5);
    EXPECT_GT(model.left_level(), 0.0);
    EXPECT_GT(model.right_level(), 0.0);

    // Pause playback: must become immediately empty (all segments unlit)
    model.pause();
    EXPECT_FALSE(model.is_playing());
    EXPECT_DOUBLE_EQ(model.left_level(), 0.0);
    EXPECT_DOUBLE_EQ(model.right_level(), 0.0);
    EXPECT_DOUBLE_EQ(model.left_peak(), 0.0);
    EXPECT_DOUBLE_EQ(model.right_peak(), 0.0);

    // Further updates while paused must keep levels at 0.0
    model.update(1.0);
    EXPECT_DOUBLE_EQ(model.left_level(), 0.0);
    EXPECT_DOUBLE_EQ(model.right_level(), 0.0);
    EXPECT_DOUBLE_EQ(model.left_peak(), 0.0);
    EXPECT_DOUBLE_EQ(model.right_peak(), 0.0);

    // Unpause / resume: levels should resume animating
    model.toggle_play_pause();
    EXPECT_TRUE(model.is_playing());
    model.update(0.1);
    EXPECT_GT(model.left_level(), 0.0);
    EXPECT_GT(model.right_level(), 0.0);

    // Toggle pause: must immediately clear levels
    model.toggle_play_pause();
    EXPECT_FALSE(model.is_playing());
    EXPECT_DOUBLE_EQ(model.left_level(), 0.0);
    EXPECT_DOUBLE_EQ(model.right_level(), 0.0);
}

TEST(CdDisplayModelTests, VolumeLevelsStereoVariationAndPeakHold) {
    CdDisplayModel model;

    // Run for several ticks and collect metrics
    bool saw_stereo_difference = false;
    for (int i = 0; i < 40; ++i) {
        model.update(0.05);
        EXPECT_GE(model.left_level(), 0.0);
        EXPECT_LE(model.left_level(), 1.0);
        EXPECT_GE(model.right_level(), 0.0);
        EXPECT_LE(model.right_level(), 1.0);
        EXPECT_GE(model.left_peak(), model.left_level());
        EXPECT_GE(model.right_peak(), model.right_level());

        if (std::abs(model.left_level() - model.right_level()) > 0.005) {
            saw_stereo_difference = true;
        }
    }

    EXPECT_TRUE(saw_stereo_difference);
}
