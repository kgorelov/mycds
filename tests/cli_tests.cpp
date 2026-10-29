#include "cli/command_loop.hpp"

#include "audio/audio_output.hpp"
#include "domain/audio_format.hpp"
#include "playback/playback_controller.hpp"
#include "source/disc_source.hpp"

#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace mycds {
namespace {

namespace fs = std::filesystem;

class LifetimeTrackedDiscSource final : public DiscSource {
public:
    LifetimeTrackedDiscSource(
        std::vector<TrackInfo> tracks,
        std::atomic<int>& destroy_counter,
        std::optional<std::int64_t> fail_at_lba = std::nullopt)
        : tracks_(std::move(tracks)),
          destroy_counter_(destroy_counter),
          fail_at_lba_(fail_at_lba) {
        disc_info_.firstTrack = tracks_.front().number;
        disc_info_.lastTrack = tracks_.back().number;
        disc_info_.startLba = tracks_.front().startLba;
        disc_info_.endLba = tracks_.back().endLba;
        disc_info_.sessions = 1;

        disc_meta_.title = "Test Album";
        disc_meta_.performer = "Test Artist";
        for (const auto& t : tracks_) {
            track_meta_.push_back(TrackMetadata{
                .trackNumber = t.number,
                .title = "Song " + std::to_string(t.number),
            });
        }
    }

    ~LifetimeTrackedDiscSource() override {
        destroy_counter_.fetch_add(1, std::memory_order_relaxed);
    }

    std::string getSourceKind() const override { return "Tracked Test Source"; }
    std::string getSourcePath() const override { return "test://tracked"; }
    DiscInfo getDiscInfo() const override { return disc_info_; }
    std::vector<TrackInfo> getTracks() const override { return tracks_; }
    DiscMetadata getDiscMetadata() const override { return disc_meta_; }
    std::vector<TrackMetadata> getTrackMetadata() const override { return track_meta_; }

    std::size_t readAudioSectors(
        std::int64_t startLba,
        std::uint32_t sectorCount,
        std::span<std::byte> destination) override {
        const std::size_t bytes =
            static_cast<std::size_t>(sectorCount) * AudioFormat::kBytesPerSector;
        if (destination.size() < bytes) {
            throw std::invalid_argument("Buffer too small");
        }
        const std::int64_t end_lba = startLba + static_cast<std::int64_t>(sectorCount);
        if (startLba < disc_info_.startLba || end_lba > disc_info_.endLba) {
            throw DiscOutOfRangeError("LBA out of range");
        }
        for (const auto& t : tracks_) {
            if (t.endLba > startLba && t.startLba < end_lba && t.type != TrackType::Audio) {
                throw DiscSourceError(
                    DiscSourceErrorKind::NonAudioTrack,
                    "Non-audio track " + std::to_string(t.number));
            }
        }
        if (fail_at_lba_.has_value() && end_lba > *fail_at_lba_) {
            throw DiscSourceError(
                DiscSourceErrorKind::DiscRemoved,
                "Simulated optical disc removal");
        }
        std::memset(destination.data(), 0, bytes);
        return bytes;
    }

    std::vector<std::byte> readAudioSectors(
        std::int64_t startLba,
        std::uint32_t sectorCount) override {
        std::vector<std::byte> buf(
            static_cast<std::size_t>(sectorCount) * AudioFormat::kBytesPerSector);
        readAudioSectors(startLba, sectorCount, buf);
        return buf;
    }

private:
    DiscInfo disc_info_{};
    std::vector<TrackInfo> tracks_{};
    DiscMetadata disc_meta_{};
    std::vector<TrackMetadata> track_meta_{};
    std::atomic<int>& destroy_counter_;
    std::optional<std::int64_t> fail_at_lba_{};
};

std::vector<TrackInfo> make_cli_test_tracks() {
    TrackInfo t1{
        .number = 1,
        .type = TrackType::Audio,
        .startLba = 0,
        .endLba = 30,
        .index01Lba = 0,
        .indexes = {{.trackNumber = 1, .number = 1, .lba = 0, .explicitFromSource = true}},
    };
    TrackInfo t2{
        .number = 2,
        .type = TrackType::Audio,
        .startLba = 30,
        .endLba = 60,
        .index01Lba = 35,
        .pregapStartLba = 30,
        .pregapLength = 5,
        .pregapInFile = true,
        .indexes = {
            {.trackNumber = 2, .number = 0, .lba = 30, .explicitFromSource = true},
            {.trackNumber = 2, .number = 1, .lba = 35, .explicitFromSource = true},
        },
    };
    TrackInfo t3_data{
        .number = 3,
        .type = TrackType::Data,
        .startLba = 60,
        .endLba = 80,
        .index01Lba = 60,
        .indexes = {{.trackNumber = 3, .number = 1, .lba = 60, .explicitFromSource = true}},
    };
    return {t1, t2, t3_data};
}

TEST(CliStartupTest, StartupFailuresReturnNonZeroWithReadableDiagnostics) {
    auto invoke = [](std::vector<const char*> args, std::string& out_str, std::string& err_str) {
        std::istringstream in("quit\n");
        std::ostringstream out;
        std::ostringstream err;
        const int rc = run_cdplayer_cli(
            static_cast<int>(args.size()), args.data(), in, out, err, nullptr);
        out_str = out.str();
        err_str = err.str();
        return rc;
    };

    std::string out_s;
    std::string err_s;

    // 1. No arguments
    EXPECT_NE(invoke({"cdplayer"}, out_s, err_s), 0);
    EXPECT_NE(err_s.find("Error:"), std::string::npos);

    // 2. Help flag succeeds with exit code 0
    EXPECT_EQ(invoke({"cdplayer", "--help"}, out_s, err_s), 0);
    EXPECT_NE(out_s.find("Usage:"), std::string::npos);

    // 3. Incomplete flags
    EXPECT_NE(invoke({"cdplayer", "--device"}, out_s, err_s), 0);
    EXPECT_NE(err_s.find("--device"), std::string::npos);
    EXPECT_NE(invoke({"cdplayer", "--image"}, out_s, err_s), 0);
    EXPECT_NE(err_s.find("--image"), std::string::npos);

    // 4. Conflicting multiple sources without --inspect or --compare
    EXPECT_NE(
        invoke({"cdplayer", "--device", "/dev/sr0", "--image", "foo.toc"}, out_s, err_s),
        0);
    EXPECT_NE(err_s.find("Multiple sources"), std::string::npos);

    // 5. Nonexistent image produces readable formatted diagnostic and non-zero exit code
    EXPECT_NE(
        invoke({"cdplayer", "--image", "/nonexistent/dir/missing_album.toc"}, out_s, err_s),
        0);
    EXPECT_NE(err_s.find("Error:"), std::string::npos);

    // 6. Nonexistent optical drive produces readable [Open Failed] diagnostic
    EXPECT_NE(
        invoke({"cdplayer", "--device", "/dev/nonexistent_cd_drive_999"}, out_s, err_s),
        0);
    EXPECT_NE(err_s.find("[Open Failed]"), std::string::npos);
}

TEST(CommandLoopTest, SupportsCompleteRequiredCommandSetAndReadableDiagnostics) {
    std::atomic<int> destroyed{0};
    PlaybackController controller(PlaybackControllerConfig{
        .audioConfig = {.ringBufferSectors = 60, .preferredDriver = "dummy"},
        .readChunkSectors = 8,
        .prebufferSectors = 8,
    });
    controller.openSource(
        std::make_unique<LifetimeTrackedDiscSource>(make_cli_test_tracks(), destroyed));

    std::istringstream in(
        "help\n"
        "info\n"
        "tracks\n"
        "position\n"
        "play\n"
        "position\n"
        "pause\n"
        "position\n"
        "seek-track 2\n"
        "position\n"
        "seek-lba 30\n"
        "position\n"
        "stop\n"
        "position\n"
        "seek-track\n"
        "seek-track 99\n"
        "seek-track 3\n"
        "seek-lba -5\n"
        "seek-lba 65\n"
        "unknown-cmd\n"
        "quit\n");
    std::ostringstream out;
    std::ostringstream err;

    CommandLoop loop(controller, in, out, err);
    const int rc = loop.run();
    EXPECT_EQ(rc, 0);

    // Verify shutdown and resource cleanup on quit
    EXPECT_FALSE(controller.hasSource());
    EXPECT_EQ(controller.state(), PlaybackState::Stopped);
    EXPECT_EQ(destroyed.load(), 1);

    const std::string stdout_text = out.str();
    const std::string stderr_text = err.str();

    // info output
    EXPECT_NE(stdout_text.find("Source Type : Tracked Test Source"), std::string::npos);
    EXPECT_NE(stdout_text.find("Test Album"), std::string::npos);
    EXPECT_NE(stdout_text.find("Test Artist"), std::string::npos);

    // tracks output (numbers, types, ranges, pregaps, indexes, titles)
    EXPECT_NE(stdout_text.find("I00:30, I01:35"), std::string::npos);
    EXPECT_NE(stdout_text.find("Data"), std::string::npos);
    EXPECT_NE(stdout_text.find("\"Song 2\""), std::string::npos);

    // play, pause, seek-track, seek-lba, position, stop outputs
    EXPECT_NE(stdout_text.find("State: Playing"), std::string::npos);
    EXPECT_NE(stdout_text.find("Paused at LBA"), std::string::npos);
    EXPECT_NE(stdout_text.find("Seeked to Track 2 (INDEX 01) at LBA 35"), std::string::npos);
    EXPECT_NE(
        stdout_text.find("Seeked to LBA 30 (Track 2, INDEX 00, track time -00:00:05)"),
        std::string::npos);
    EXPECT_NE(stdout_text.find("Stopped. Position reset to LBA 0"), std::string::npos);

    // Readable diagnostics on invalid commands, out-of-range seeks, and data tracks
    EXPECT_NE(stderr_text.find("Usage: seek-track <number>"), std::string::npos);
    EXPECT_NE(stderr_text.find("[Out of Range]"), std::string::npos);
    EXPECT_NE(stderr_text.find("[Non-Audio Track]"), std::string::npos);
    EXPECT_NE(stderr_text.find("Unknown command 'unknown-cmd'"), std::string::npos);
}

TEST(CommandLoopTest, CleansUpActivePlaybackOnEofSignalAndSourceReadError) {
    // 1. EOF while actively Playing cleans up PlaybackController and DiscSource
    {
        std::atomic<int> destroyed{0};
        PlaybackController controller(PlaybackControllerConfig{
            .audioConfig = {.ringBufferSectors = 40, .preferredDriver = "dummy"},
            .readChunkSectors = 8,
            .prebufferSectors = 8,
        });
        controller.openSource(
            std::make_unique<LifetimeTrackedDiscSource>(make_cli_test_tracks(), destroyed));

        std::istringstream in("play\n"); // Hits EOF while Playing!
        std::ostringstream out;
        std::ostringstream err;

        CommandLoop loop(controller, in, out, err);
        EXPECT_EQ(loop.run(), 0);
        EXPECT_FALSE(controller.hasSource());
        EXPECT_EQ(destroyed.load(), 1);
    }

    // 2. Interrupt signal (SIGINT) exits with 128 + SIGINT and cleans up resources
    {
        std::atomic<int> destroyed{0};
        volatile std::sig_atomic_t sig_flag = SIGINT;
        PlaybackController controller(PlaybackControllerConfig{
            .audioConfig = {.ringBufferSectors = 40, .preferredDriver = "dummy"},
            .readChunkSectors = 8,
            .prebufferSectors = 8,
        });
        controller.openSource(
            std::make_unique<LifetimeTrackedDiscSource>(make_cli_test_tracks(), destroyed));

        std::istringstream in("position\n");
        std::ostringstream out;
        std::ostringstream err;

        CommandLoop loop(
            controller,
            in,
            out,
            err,
            CommandLoopOptions{
                .autoPlayOnStart = true,
                .interruptSignal = &sig_flag,
            });
        EXPECT_EQ(loop.run(), 128 + SIGINT);
        EXPECT_FALSE(controller.hasSource());
        EXPECT_EQ(destroyed.load(), 1);
    }

    // 3. Disc removal error surfaces readable [Disc Removed] diagnostic in CLI
    {
        std::atomic<int> destroyed{0};
        PlaybackController controller(PlaybackControllerConfig{
            .audioConfig = {.ringBufferSectors = 40, .preferredDriver = "dummy"},
            .readChunkSectors = 4,
            .prebufferSectors = 4,
        });
        controller.openSource(
            std::make_unique<LifetimeTrackedDiscSource>(make_cli_test_tracks(), destroyed, 0));

        std::istringstream in("play\nposition\nquit\n");
        std::ostringstream out;
        std::ostringstream err;

        CommandLoop loop(controller, in, out, err);
        EXPECT_EQ(loop.run(), 0);
        EXPECT_NE(err.str().find("[Disc Removed]"), std::string::npos);
        EXPECT_NE(out.str().find("State      : Error"), std::string::npos);
        EXPECT_EQ(destroyed.load(), 1);
    }
}

TEST(CliEndToEndTest, ImageSourceSelectionThroughCommandLineAndInteractiveSession) {
    const fs::path temp_dir = fs::temp_directory_path() / "mycds_phase6_cli_test";
    std::error_code ec;
    fs::remove_all(temp_dir, ec);
    fs::create_directories(temp_dir, ec);

    const fs::path bin_path = temp_dir / "cli_album.bin";
    const fs::path toc_path = temp_dir / "cli_album.toc";
    const fs::path cue_path = temp_dir / "cli_album.cue";

    constexpr std::uint32_t kSectors = 40;
    {
        std::ofstream bin_out(bin_path, std::ios::binary);
        const auto pcm = generate_sine_wave_pcm(440.0, kSectors, 0.1);
        bin_out.write(
            reinterpret_cast<const char*>(pcm.data()),
            static_cast<std::streamsize>(pcm.size()));
    }
    {
        std::ofstream toc_out(toc_path);
        toc_out << "CD_DA\n\n"
                << "TRACK AUDIO\n"
                << "FILE \"cli_album.bin\" 0 00:00:20\n\n"
                << "TRACK AUDIO\n"
                << "FILE \"cli_album.bin\" 00:00:20 00:00:20\n"
                << "START 00:00:04\n";
    }
    {
        std::ofstream cue_out(cue_path);
        cue_out << "PERFORMER \"CLI Performer\"\n"
                << "TITLE \"CLI Album\"\n"
                << "FILE \"cli_album.bin\" BINARY\n"
                << "  TRACK 01 AUDIO\n"
                << "    TITLE \"Opening\"\n"
                << "    INDEX 01 00:00:00\n"
                << "  TRACK 02 AUDIO\n"
                << "    TITLE \"Finale\"\n"
                << "    INDEX 00 00:00:20\n"
                << "    INDEX 01 00:00:24\n";
    }

    for (const fs::path& img_path : {toc_path, cue_path}) {
        const std::string path_str = img_path.string();
        std::vector<const char*> argv = {
            "cdplayer",
            "--image",
            path_str.c_str(),
            "--audio-driver",
            "dummy",
        };
        std::istringstream in(
            "info\n"
            "tracks\n"
            "seek-track 2\n"
            "play\n"
            "position\n"
            "pause\n"
            "stop\n"
            "quit\n");
        std::ostringstream out;
        std::ostringstream err;

        const int rc = run_cdplayer_cli(
            static_cast<int>(argv.size()), argv.data(), in, out, err, nullptr);
        EXPECT_EQ(rc, 0) << err.str();
        EXPECT_TRUE(err.str().empty()) << err.str();
        EXPECT_NE(out.str().find("LBA Range   : [0, 40)"), std::string::npos);
        EXPECT_NE(out.str().find("Seeked to Track 2 (INDEX 01) at LBA 24"), std::string::npos);
    }

    fs::remove_all(temp_dir, ec);
}

} // namespace
} // namespace mycds
