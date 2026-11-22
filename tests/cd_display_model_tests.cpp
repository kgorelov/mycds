#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "audio/audio_output.hpp"
#include "gui/cd_display_model.h"
#include "metadata/metadata_store.hpp"
#include "source/image_source.hpp"

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
    EXPECT_FALSE(model.is_cloud_active());
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

TEST(CdDisplayModelTests, StopFunctionality) {
    CdDisplayModel model;

    model.update(12.5);
    EXPECT_GT(model.track_elapsed_seconds(), 0.0);
    EXPECT_TRUE(model.is_playing());
    EXPECT_GT(model.left_level(), 0.0);

    model.stop();
    EXPECT_FALSE(model.is_playing());
    EXPECT_DOUBLE_EQ(model.track_elapsed_seconds(), 0.0);
    EXPECT_EQ(model.elapsed_minutes(), 0);
    EXPECT_EQ(model.elapsed_seconds(), 0);
    EXPECT_DOUBLE_EQ(model.left_level(), 0.0);
    EXPECT_DOUBLE_EQ(model.right_level(), 0.0);
}

TEST(CdDisplayModelTests, SeekingBehavior) {
    CdDisplayModel model;

    EXPECT_DOUBLE_EQ(model.track_elapsed_seconds(), 0.0);

    // Seek forward 15s
    model.seek_forward(15.0);
    EXPECT_NEAR(model.track_elapsed_seconds(), 15.0, 0.001);

    // Seek backward 5s
    model.seek_backward(5.0);
    EXPECT_NEAR(model.track_elapsed_seconds(), 10.0, 0.001);

    // Seek backward more than elapsed: clamps to 0.0
    model.seek_backward(20.0);
    EXPECT_DOUBLE_EQ(model.track_elapsed_seconds(), 0.0);

    // Seek past track duration advances to next track
    const int dur = model.track_duration_seconds();
    model.seek_forward(dur + 10.0);
    EXPECT_EQ(model.current_track_number(), 2);
    EXPECT_DOUBLE_EQ(model.track_elapsed_seconds(), 0.0);

    // From track 2 at 0:00, seek backward 5s rewinds into track 1
    model.seek_backward(5.0);
    EXPECT_EQ(model.current_track_number(), 1);
    EXPECT_NEAR(model.track_elapsed_seconds(), static_cast<double>(dur) - 5.0, 0.001);
}

TEST(CdDisplayModelTests, VolumeAndBalanceAdjustment) {
    CdDisplayModel model;

    EXPECT_DOUBLE_EQ(model.volume(), 0.8);
    EXPECT_DOUBLE_EQ(model.balance(), 0.0);

    model.set_volume(0.5);
    EXPECT_DOUBLE_EQ(model.volume(), 0.5);

    // Clamping volume
    model.set_volume(1.5);
    EXPECT_DOUBLE_EQ(model.volume(), 1.0);
    model.set_volume(-0.5);
    EXPECT_DOUBLE_EQ(model.volume(), 0.0);

    // Zero volume silences output levels
    model.update(0.1);
    EXPECT_DOUBLE_EQ(model.left_level(), 0.0);
    EXPECT_DOUBLE_EQ(model.right_level(), 0.0);

    // Balance
    model.set_volume(0.8);
    model.set_balance(-1.0); // Full Left
    EXPECT_DOUBLE_EQ(model.balance(), -1.0);
    model.update(0.1);
    EXPECT_GT(model.left_level(), 0.0);
    EXPECT_DOUBLE_EQ(model.right_level(), 0.0);

    model.set_balance(1.0); // Full Right
    EXPECT_DOUBLE_EQ(model.balance(), 1.0);
    // Allow VU ballistics decay time for left channel to settle to 0
    model.update(1.5);
    EXPECT_DOUBLE_EQ(model.left_level(), 0.0);
    EXPECT_GT(model.right_level(), 0.0);

    // Balance clamping
    model.set_balance(2.5);
    EXPECT_DOUBLE_EQ(model.balance(), 1.0);
    model.set_balance(-2.5);
    EXPECT_DOUBLE_EQ(model.balance(), -1.0);
}

TEST(CdDisplayModelTests, EjectMenuAndLoadDisc) {
    CdDisplayModel model;

    EXPECT_FALSE(model.is_eject_menu_open());
    EXPECT_EQ(model.active_eject_screen(), EjectMenuScreen::Cd);

    model.open_eject_menu();
    EXPECT_TRUE(model.is_eject_menu_open());

    model.set_active_eject_screen(EjectMenuScreen::ImageFile);
    EXPECT_EQ(model.active_eject_screen(), EjectMenuScreen::ImageFile);
    model.set_active_eject_screen(EjectMenuScreen::Library);
    EXPECT_EQ(model.active_eject_screen(), EjectMenuScreen::Library);
    model.set_active_eject_screen(EjectMenuScreen::DiskInfo);
    EXPECT_EQ(model.active_eject_screen(), EjectMenuScreen::DiskInfo);
    model.set_active_eject_screen(EjectMenuScreen::Settings);
    EXPECT_EQ(model.active_eject_screen(), EjectMenuScreen::Settings);
    model.set_active_eject_screen(EjectMenuScreen::About);
    EXPECT_EQ(model.active_eject_screen(), EjectMenuScreen::About);

    model.close_eject_menu();
    EXPECT_FALSE(model.is_eject_menu_open());

    const std::vector<TrackInfo> new_tracks = {
        {1, 180, "Come Together"},
        {2, 200, "Something"},
        {3, 210, "Maxwell's Silver Hammer"}
    };

    model.open_eject_menu();
    model.load_disc("Abbey Road", new_tracks);
    EXPECT_EQ(model.album_title(), "Abbey Road");
    EXPECT_EQ(model.total_tracks(), 3);
    EXPECT_EQ(model.current_track_number(), 1);
    EXPECT_EQ(model.tracks()[0].title, "Come Together");
    EXPECT_DOUBLE_EQ(model.track_elapsed_seconds(), 0.0);
    EXPECT_TRUE(model.is_playing());
    EXPECT_FALSE(model.is_eject_menu_open());
}

TEST(CdDisplayModelTests, RecordEmulationLifecycle) {
    CdDisplayModel model;

    EXPECT_FALSE(model.is_recording());

    const std::vector<TrackInfo> short_album = {
        {1, 100, "Track One"},
        {2, 100, "Track Two"},
        {3, 100, "Track Three"}
    };
    model.load_disc("Test Album", short_album);
    EXPECT_FALSE(model.is_recording());
    EXPECT_EQ(model.total_tracks(), 3);

    // Start recording emulation
    model.start_recording();
    EXPECT_TRUE(model.is_recording());
    EXPECT_FALSE(model.is_playing());
    EXPECT_EQ(model.current_track_number(), 1);
    EXPECT_DOUBLE_EQ(model.track_elapsed_seconds(), 0.0);

    // Fast seek speed is 350.0 audio seconds / real second.
    // 0.1 real seconds advances 35.0s of track 1
    model.update(0.1);
    EXPECT_TRUE(model.is_recording());
    EXPECT_EQ(model.current_track_number(), 1);
    EXPECT_NEAR(model.track_elapsed_seconds(), 35.0, 0.01);

    // Another 0.2s (total 0.3s -> 105s audio) completes track 1 (100s) and advances to track 2
    model.update(0.2);
    EXPECT_TRUE(model.is_recording());
    EXPECT_EQ(model.current_track_number(), 2);

    // Advance through track 2 (100s / 350.0 ~ 0.286s) into track 3
    model.update(0.35);
    EXPECT_TRUE(model.is_recording());
    EXPECT_EQ(model.current_track_number(), 3);

    // Advance past end of track 3: process finishes, resets to track 1 00:00, turns amber (not recording)
    model.update(0.4);
    EXPECT_FALSE(model.is_recording());
    EXPECT_EQ(model.current_track_number(), 1);
    EXPECT_DOUBLE_EQ(model.track_elapsed_seconds(), 0.0);
    EXPECT_EQ(model.elapsed_minutes(), 0);
    EXPECT_EQ(model.elapsed_seconds(), 0);
    EXPECT_FALSE(model.is_playing());
}

TEST(CdDisplayModelTests, RecordEmulationStopInterrupt) {
    CdDisplayModel model;
    model.start_recording();
    EXPECT_TRUE(model.is_recording());

    model.update(0.2);
    EXPECT_GT(model.track_elapsed_seconds(), 0.0);

    // Stop cancels recording and resets to track 1 00:00
    model.stop();
    EXPECT_FALSE(model.is_recording());
    EXPECT_EQ(model.current_track_number(), 1);
    EXPECT_DOUBLE_EQ(model.track_elapsed_seconds(), 0.0);
    EXPECT_FALSE(model.is_playing());
}

TEST(CdDisplayModelTests, ArtistAndAlbumCddbData) {
    CdDisplayModel model;

    EXPECT_EQ(model.artist_name(), "Pink Floyd");
    EXPECT_EQ(model.album_title(), "The Dark Side of the Moon");

    // Before 3.0s uptime, CDDB data is inactive
    EXPECT_FALSE(model.has_cddb_data());
    EXPECT_EQ(model.track_display_title(0), "Track 1");
    EXPECT_EQ(model.track_display_title(1), "Track 2");

    // After 3.0s uptime with connectivity, CDDB data becomes active
    model.update(3.0);
    EXPECT_TRUE(model.has_cddb_data());
    EXPECT_EQ(model.track_display_title(0), "Speak to Me / Breathe");
    EXPECT_EQ(model.track_display_title(1), "On the Run");

    // Disconnecting connectivity disables CDDB data and reverts to generic track names
    model.set_connectivity(ConnectivityStatus::Disconnected);
    EXPECT_FALSE(model.has_cddb_data());
    EXPECT_EQ(model.track_display_title(0), "Track 1");

    model.set_connectivity(ConnectivityStatus::Connected);
    EXPECT_TRUE(model.has_cddb_data());
    EXPECT_EQ(model.track_display_title(0), "Speak to Me / Breathe");

    // Setting explicit has_cddb_data flag
    model.set_has_cddb_data(false);
    EXPECT_FALSE(model.has_cddb_data());
    model.set_has_cddb_data(true);
    EXPECT_TRUE(model.has_cddb_data());

    // Mutating artist and album title
    model.set_artist_name("Dire Straits");
    model.set_album_title("Brothers in Arms");
    EXPECT_EQ(model.artist_name(), "Dire Straits");
    EXPECT_EQ(model.album_title(), "Brothers in Arms");

    // Loading new disc with artist and tracks
    std::vector<TrackInfo> new_tracks = {
        {1, 312, "So Far Away"},
        {2, 514, "Money for Nothing"}
    };
    model.load_disc("Dire Straits", "Brothers in Arms", new_tracks);
    EXPECT_EQ(model.artist_name(), "Dire Straits");
    EXPECT_EQ(model.album_title(), "Brothers in Arms");
    EXPECT_EQ(model.total_tracks(), 2);
}

TEST(CdDisplayModelTests, RealPlaybackControllerIntegrationWithCueImage) {
    namespace fs = std::filesystem;
    const fs::path temp_dir = fs::temp_directory_path() / "mycds_display_model_real_test";
    std::error_code ec;
    fs::remove_all(temp_dir, ec);
    fs::create_directories(temp_dir, ec);

    const fs::path bin_path = temp_dir / "test.bin";
    const fs::path cue_path = temp_dir / "test.cue";
    const fs::path sidecar_path = temp_dir / "test.mycds.json";

    // 150 sectors = 2.0 seconds of 440 Hz sine wave (Track 1: 75 sectors, Track 2: 75 sectors)
    {
        std::ofstream bin_out(bin_path, std::ios::binary);
        const auto pcm = mycds::generate_sine_wave_pcm(440.0, 150, 0.5);
        bin_out.write(
            reinterpret_cast<const char*>(pcm.data()),
            static_cast<std::streamsize>(pcm.size()));
    }
    {
        std::ofstream cue_out(cue_path);
        cue_out << "PERFORMER \"Test Artist\"\n"
                << "TITLE \"Test Album\"\n"
                << "FILE \"test.bin\" BINARY\n"
                << "  TRACK 01 AUDIO\n"
                << "    TITLE \"First Cut\"\n"
                << "    INDEX 01 00:00:00\n"
                << "  TRACK 02 AUDIO\n"
                << "    TITLE \"Second Cut\"\n"
                << "    INDEX 01 00:01:00\n";
    }

    // Write a sidecar file to verify non-blocking local sidecar loading overrides/enriches metadata
    {
        mycds::MetadataRecord rec;
        rec.disc.performer = "Sidecar Artist";
        rec.disc.title = "Sidecar Album";
        rec.tracks = {
            {.trackNumber = 1, .title = "Sidecar One"},
            {.trackNumber = 2, .title = "Sidecar Two"},
        };
        mycds::MetadataStore::saveSidecar(sidecar_path, rec);
    }

    mycds::PlaybackControllerConfig cfg;
    cfg.audioConfig.ringBufferSectors = 60;
    cfg.audioConfig.preferredDriver = "dummy";
    cfg.readChunkSectors = 8;
    cfg.prebufferSectors = 8;

    CdDisplayModel model(cfg);
    EXPECT_FALSE(model.has_real_source());
    model.clear_disc();
    EXPECT_FALSE(model.has_disc());
    EXPECT_EQ(model.total_tracks(), 0);

    std::string err;
    ASSERT_TRUE(model.open_disc_source(mycds::open_image_source(cue_path.string()), &err)) << err;
    EXPECT_TRUE(model.has_real_source());
    EXPECT_TRUE(model.has_disc());
    EXPECT_EQ(model.total_tracks(), 2);
    EXPECT_EQ(model.artist_name(), "Sidecar Artist");
    EXPECT_EQ(model.album_title(), "Sidecar Album");
    EXPECT_TRUE(model.is_playing());
    EXPECT_EQ(model.current_track_number(), 1);

    // Advance uptime >= 3.0s for CDDB indicator and let real audio stream
    model.update(3.0);
    EXPECT_TRUE(model.has_cddb_data());
    EXPECT_EQ(model.track_display_title(0), "Sidecar One");
    EXPECT_EQ(model.track_display_title(1), "Sidecar Two");

    bool progressed = false;
    bool meter_active = false;
    for (int i = 0; i < 40; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        model.update(0.015);
        if (model.track_elapsed_seconds() > 0.02) {
            progressed = true;
        }
        if (model.left_level() > 0.05 && model.right_level() > 0.05) {
            meter_active = true;
        }
        if (progressed && meter_active) {
            break;
        }
    }
    EXPECT_TRUE(progressed);
    EXPECT_TRUE(meter_active);

    // Jump to track 2 and verify track number updates
    model.jump_to_track(2);
    model.update(0.02);
    EXPECT_EQ(model.current_track_number(), 2);

    // Pause zeroes meter levels immediately
    model.pause();
    EXPECT_FALSE(model.is_playing());
    EXPECT_DOUBLE_EQ(model.left_level(), 0.0);
    EXPECT_DOUBLE_EQ(model.right_level(), 0.0);

    // Stop resets to track 1 00:00
    model.stop();
    EXPECT_FALSE(model.is_playing());
    EXPECT_EQ(model.current_track_number(), 1);
    EXPECT_DOUBLE_EQ(model.track_elapsed_seconds(), 0.0);

    // Eject clears source and disc
    model.eject_disc();
    EXPECT_FALSE(model.has_real_source());
    EXPECT_FALSE(model.has_disc());
    EXPECT_EQ(model.total_tracks(), 0);

    fs::remove_all(temp_dir, ec);
}

TEST(CdDisplayModelTests, RepeatOneKeepsPlayingCurrentTrack) {
    namespace fs = std::filesystem;
    const fs::path temp_dir = fs::temp_directory_path() / "mycds_display_model_repeat1_test";
    std::error_code ec;
    fs::remove_all(temp_dir, ec);
    fs::create_directories(temp_dir, ec);

    const fs::path bin_path = temp_dir / "repeat1.bin";
    const fs::path cue_path = temp_dir / "repeat1.cue";

    // 40 sectors total (~0.53s): Track 1 = 20 sectors (~0.27s), Track 2 = 20 sectors (~0.27s)
    {
        std::ofstream bin_out(bin_path, std::ios::binary);
        const auto pcm = mycds::generate_sine_wave_pcm(440.0, 40, 0.4);
        bin_out.write(
            reinterpret_cast<const char*>(pcm.data()),
            static_cast<std::streamsize>(pcm.size()));
    }
    {
        std::ofstream cue_out(cue_path);
        cue_out << "FILE \"repeat1.bin\" BINARY\n"
                << "  TRACK 01 AUDIO\n"
                << "    INDEX 01 00:00:00\n"
                << "  TRACK 02 AUDIO\n"
                << "    INDEX 01 00:00:20\n";
    }

    mycds::PlaybackControllerConfig cfg;
    cfg.audioConfig.ringBufferSectors = 30;
    cfg.audioConfig.preferredDriver = "dummy";
    cfg.readChunkSectors = 4;
    cfg.prebufferSectors = 4;

    CdDisplayModel model(cfg);
    model.set_repeat_mode(RepeatMode::Track);

    std::string err;
    ASSERT_TRUE(model.open_disc_source(mycds::open_image_source(cue_path.string()), &err)) << err;
    EXPECT_EQ(model.current_track_number(), 1);
    EXPECT_TRUE(model.is_playing());

    // Poll for 0.65s (> 2x Track 1 duration): Track 1 must loop and never advance to Track 2
    for (int i = 0; i < 45; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        model.update(0.015);
        EXPECT_EQ(model.current_track_number(), 1);
    }
    EXPECT_TRUE(model.is_playing());

    model.eject_disc();
    fs::remove_all(temp_dir, ec);
}
