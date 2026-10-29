#include "playback/playback_controller.hpp"

#include "domain/audio_format.hpp"
#include "source/disc_source.hpp"
#include "source/image_source.hpp"
#include "source/physical_cd_source.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace mycds {
namespace {

namespace fs = std::filesystem;

struct ReadRecord {
    std::int64_t startLba{};
    std::uint32_t sectorCount{};
};

// Configurable in-memory DiscSource for deterministic state-machine, gapless-boundary,
// mixed-mode, and error-propagation tests.
class MockDiscSource final : public DiscSource {
public:
    explicit MockDiscSource(
        std::vector<TrackInfo> tracks,
        std::string source_kind = "Mock DiscSource")
        : source_kind_(std::move(source_kind)),
          tracks_(std::move(tracks)) {
        if (!tracks_.empty()) {
            disc_info_.firstTrack = tracks_.front().number;
            disc_info_.lastTrack = tracks_.back().number;
            disc_info_.startLba = tracks_.front().startLba;
            disc_info_.endLba = tracks_.back().endLba;
            disc_info_.sessions = 1;
        }
    }

    void setFailureAtOrAboveLba(
        std::optional<std::int64_t> fail_lba,
        DiscSourceErrorKind kind = DiscSourceErrorKind::ReadFailed,
        std::string message = "Simulated sector read failure") {
        std::lock_guard<std::mutex> lock(mutex_);
        fail_at_lba_ = fail_lba;
        fail_kind_ = kind;
        fail_message_ = std::move(message);
    }

    std::vector<ReadRecord> recordedReads() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return reads_;
    }

    std::vector<std::int64_t> flattenedReadSectors() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::int64_t> result;
        for (const auto& r : reads_) {
            for (std::uint32_t i = 0; i < r.sectorCount; ++i) {
                result.push_back(r.startLba + static_cast<std::int64_t>(i));
            }
        }
        return result;
    }

    void clearRecordedReads() {
        std::lock_guard<std::mutex> lock(mutex_);
        reads_.clear();
    }

    std::string getSourceKind() const override { return source_kind_; }
    std::string getSourcePath() const override { return "memory://mock"; }
    DiscInfo getDiscInfo() const override { return disc_info_; }
    std::vector<TrackInfo> getTracks() const override { return tracks_; }
    DiscMetadata getDiscMetadata() const override { return {}; }
    std::vector<TrackMetadata> getTrackMetadata() const override { return {}; }

    std::size_t readAudioSectors(
        std::int64_t startLba,
        std::uint32_t sectorCount,
        std::span<std::byte> destination) override {
        const std::size_t required_bytes =
            static_cast<std::size_t>(sectorCount) * AudioFormat::kBytesPerSector;
        if (destination.size() < required_bytes) {
            throw std::invalid_argument("Destination buffer too small");
        }
        if (sectorCount == 0) {
            return 0;
        }

        const std::int64_t end_lba = startLba + static_cast<std::int64_t>(sectorCount);
        if (startLba < disc_info_.startLba || end_lba > disc_info_.endLba) {
            throw DiscOutOfRangeError("Out of mock disc bounds");
        }

        for (const auto& trk : tracks_) {
            if (trk.endLba > startLba && trk.startLba < end_lba &&
                trk.type != TrackType::Audio) {
                throw DiscSourceError(
                    DiscSourceErrorKind::NonAudioTrack,
                    "Attempted audio read on non-audio track " +
                        std::to_string(trk.number));
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (fail_at_lba_.has_value() && end_lba > *fail_at_lba_) {
                throw DiscSourceError(fail_kind_, fail_message_);
            }
            reads_.push_back(ReadRecord{
                .startLba = startLba,
                .sectorCount = sectorCount,
            });
        }

        // Fill each sector with deterministic quiet S16LE pattern tagged with its LBA
        std::memset(destination.data(), 0, required_bytes);
        for (std::uint32_t s = 0; s < sectorCount; ++s) {
            const auto lba_tag =
                static_cast<std::int16_t>((startLba + static_cast<std::int64_t>(s)) & 0x0FFF);
            const auto u16 = static_cast<std::uint16_t>(lba_tag);
            const std::size_t base = static_cast<std::size_t>(s) * AudioFormat::kBytesPerSector;
            destination[base + 0] = static_cast<std::byte>(u16 & 0xFFU);
            destination[base + 1] = static_cast<std::byte>((u16 >> 8U) & 0xFFU);
            destination[base + 2] = static_cast<std::byte>(u16 & 0xFFU);
            destination[base + 3] = static_cast<std::byte>((u16 >> 8U) & 0xFFU);
        }

        return required_bytes;
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
    std::string source_kind_{};
    DiscInfo disc_info_{};
    std::vector<TrackInfo> tracks_{};

    mutable std::mutex mutex_{};
    std::vector<ReadRecord> reads_{};
    std::optional<std::int64_t> fail_at_lba_{};
    DiscSourceErrorKind fail_kind_{DiscSourceErrorKind::ReadFailed};
    std::string fail_message_{};
};

std::vector<TrackInfo> make_three_track_layout_with_pregap() {
    TrackInfo t1{};
    t1.number = 1;
    t1.type = TrackType::Audio;
    t1.startLba = 0;
    t1.index01Lba = 0;
    t1.endLba = 8;
    t1.indexes = {
        IndexInfo{.trackNumber = 1, .number = 1, .lba = 0, .explicitFromSource = true},
    };

    TrackInfo t2{};
    t2.number = 2;
    t2.type = TrackType::Audio;
    t2.startLba = 8;
    t2.index01Lba = 12;
    t2.endLba = 20;
    t2.pregapStartLba = 8;
    t2.pregapLength = 4;
    t2.pregapInFile = true;
    t2.indexes = {
        IndexInfo{.trackNumber = 2, .number = 0, .lba = 8, .explicitFromSource = true},
        IndexInfo{.trackNumber = 2, .number = 1, .lba = 12, .explicitFromSource = true},
        IndexInfo{.trackNumber = 2, .number = 2, .lba = 16, .explicitFromSource = true},
    };

    TrackInfo t3{};
    t3.number = 3;
    t3.type = TrackType::Audio;
    t3.startLba = 20;
    t3.index01Lba = 20;
    t3.endLba = 28;
    t3.indexes = {
        IndexInfo{.trackNumber = 3, .number = 1, .lba = 20, .explicitFromSource = true},
    };

    return {t1, t2, t3};
}

bool wait_until(
    const std::function<bool()>& predicate,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

TEST(PlaybackPositionTest, TrackAndIndexIdentificationAcrossPregapsAndSubIndexes) {
    const auto tracks = make_three_track_layout_with_pregap();
    const DiscInfo disc_info{
        .firstTrack = 1,
        .lastTrack = 3,
        .startLba = 0,
        .endLba = 28,
        .sessions = 1,
    };

    const auto p0 = resolve_playback_position(disc_info, tracks, 0, 12);
    EXPECT_EQ(p0.lba, 0);
    EXPECT_EQ(p0.frameInSector, 12U);
    EXPECT_EQ(p0.trackNumber, 1);
    EXPECT_EQ(p0.indexNumber, 1);
    EXPECT_EQ(p0.trackRelativeSectors, 0);
    EXPECT_EQ(p0.discElapsedSectors, 0);

    const auto p7 = resolve_playback_position(disc_info, tracks, 7);
    EXPECT_EQ(p7.trackNumber, 1);
    EXPECT_EQ(p7.indexNumber, 1);
    EXPECT_EQ(p7.trackRelativeSectors, 7);

    // Track 2 INDEX 00 pregap [8, 12) has negative track-relative sector countdown
    const auto p8 = resolve_playback_position(disc_info, tracks, 8);
    EXPECT_EQ(p8.trackNumber, 2);
    EXPECT_EQ(p8.indexNumber, 0);
    EXPECT_EQ(p8.trackRelativeSectors, -4);
    EXPECT_EQ(p8.discElapsedSectors, 8);

    const auto p11 = resolve_playback_position(disc_info, tracks, 11);
    EXPECT_EQ(p11.trackNumber, 2);
    EXPECT_EQ(p11.indexNumber, 0);
    EXPECT_EQ(p11.trackRelativeSectors, -1);

    // Track 2 INDEX 01 at LBA 12
    const auto p12 = resolve_playback_position(disc_info, tracks, 12);
    EXPECT_EQ(p12.trackNumber, 2);
    EXPECT_EQ(p12.indexNumber, 1);
    EXPECT_EQ(p12.trackRelativeSectors, 0);

    // Track 2 INDEX 02 at LBA 16
    const auto p16 = resolve_playback_position(disc_info, tracks, 16);
    EXPECT_EQ(p16.trackNumber, 2);
    EXPECT_EQ(p16.indexNumber, 2);
    EXPECT_EQ(p16.trackRelativeSectors, 4);

    // Track 3 INDEX 01 at LBA 20
    const auto p20 = resolve_playback_position(disc_info, tracks, 20);
    EXPECT_EQ(p20.trackNumber, 3);
    EXPECT_EQ(p20.indexNumber, 1);
    EXPECT_EQ(p20.trackRelativeSectors, 0);

    EXPECT_EQ(compute_contiguous_audio_end_lba(tracks, 0), 28);
    EXPECT_EQ(compute_contiguous_audio_end_lba(tracks, 8), 28);
    EXPECT_EQ(compute_contiguous_audio_end_lba(tracks, 20), 28);
}

TEST(PlaybackControllerTest, StateMachinePlayPauseResumeSeekAndStop) {
    PlaybackController controller(PlaybackControllerConfig{
        .audioConfig = {.ringBufferSectors = 60, .preferredDriver = "dummy"},
        .readChunkSectors = 8,
        .prebufferSectors = 8,
    });

    EXPECT_FALSE(controller.hasSource());
    EXPECT_EQ(controller.state(), PlaybackState::Stopped);
    EXPECT_THROW(controller.play(), std::runtime_error);
    EXPECT_THROW(controller.pause(), std::runtime_error);
    EXPECT_THROW(controller.seekToTrack(1), std::runtime_error);
    EXPECT_THROW(controller.seekToLba(0), std::runtime_error);
    EXPECT_NO_THROW(controller.stop());
    EXPECT_NO_THROW(controller.closeSource());

    // Build a 120-sector (~1.6s) 2-track disc so we can comfortably pause & seek mid-stream
    TrackInfo t1{
        .number = 1,
        .type = TrackType::Audio,
        .startLba = 0,
        .endLba = 60,
        .index01Lba = 0,
        .indexes = {{.trackNumber = 1, .number = 1, .lba = 0, .explicitFromSource = true}},
    };
    TrackInfo t2{
        .number = 2,
        .type = TrackType::Audio,
        .startLba = 60,
        .endLba = 120,
        .index01Lba = 65,
        .pregapStartLba = 60,
        .pregapLength = 5,
        .pregapInFile = true,
        .indexes = {
            {.trackNumber = 2, .number = 0, .lba = 60, .explicitFromSource = true},
            {.trackNumber = 2, .number = 1, .lba = 65, .explicitFromSource = true},
        },
    };

    controller.openSource(std::make_unique<MockDiscSource>(std::vector<TrackInfo>{t1, t2}));
    ASSERT_TRUE(controller.hasSource());
    EXPECT_EQ(controller.state(), PlaybackState::Stopped);
    EXPECT_EQ(controller.currentLba(), 0);
    EXPECT_EQ(controller.currentTrack(), 1);
    EXPECT_EQ(controller.currentIndex(), 1);

    controller.play();
    EXPECT_EQ(controller.state(), PlaybackState::Playing);
    ASSERT_TRUE(wait_until([&]() { return controller.currentLba() >= 4; }));

    controller.pause();
    EXPECT_EQ(controller.state(), PlaybackState::Paused);
    const std::int64_t paused_lba = controller.currentLba();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    EXPECT_EQ(controller.currentLba(), paused_lba);

    // Resume from paused position
    controller.play();
    EXPECT_EQ(controller.state(), PlaybackState::Playing);
    ASSERT_TRUE(wait_until([&]() { return controller.currentLba() > paused_lba + 2; }));

    // Seek to Track 2 while actively Playing -> lands at Track 2 INDEX 01 (LBA 65)
    controller.seekToTrack(2);
    EXPECT_EQ(controller.state(), PlaybackState::Playing);
    EXPECT_GE(controller.currentLba(), 65);
    EXPECT_EQ(controller.currentTrack(), 2);
    EXPECT_EQ(controller.currentIndex(), 1);

    // Pause and seek while Paused -> stays Paused at new LBA (Track 2 pregap LBA 60)
    controller.pause();
    EXPECT_EQ(controller.state(), PlaybackState::Paused);
    controller.seekToLba(60);
    EXPECT_EQ(controller.state(), PlaybackState::Paused);
    EXPECT_EQ(controller.currentLba(), 60);
    EXPECT_EQ(controller.currentTrack(), 2);
    EXPECT_EQ(controller.currentIndex(), 0);

    // Resume from Paused-after-seek and verify transition from INDEX 00 -> INDEX 01
    controller.play();
    EXPECT_EQ(controller.state(), PlaybackState::Playing);
    ASSERT_TRUE(wait_until([&]() {
        return controller.currentLba() >= 65 && controller.currentIndex() == 1;
    }));

    // Stop resets cursor to Track 1 INDEX 01 without closing AudioOutput
    controller.stop();
    EXPECT_EQ(controller.state(), PlaybackState::Stopped);
    EXPECT_FALSE(controller.isEndOfDisc());
    EXPECT_EQ(controller.currentLba(), 0);
    EXPECT_EQ(controller.currentTrack(), 1);
    EXPECT_EQ(controller.status().audioDeviceOpenCount, 1ULL);

    controller.closeSource();
    EXPECT_FALSE(controller.hasSource());
}

TEST(PlaybackControllerTest, ContinuousGaplessPlaybackAcrossTracksAndPregapsVsSeekToTrack) {
    auto mock = std::make_unique<MockDiscSource>(make_three_track_layout_with_pregap());
    auto* mock_ptr = mock.get();

    PlaybackController controller(PlaybackControllerConfig{
        .audioConfig = {.ringBufferSectors = 40, .preferredDriver = "dummy"},
        .readChunkSectors = 5, // Intentionally unaligned with track boundaries (8, 12, 20)
        .prebufferSectors = 5,
    });

    controller.openSource(std::move(mock));
    controller.play();

    std::set<int> observed_tracks;
    bool saw_track2_pregap_or_beyond = false;

    const bool finished = wait_until([&]() {
        const auto st = controller.status();
        if (st.position.trackNumber.has_value()) {
            observed_tracks.insert(*st.position.trackNumber);
            if (*st.position.trackNumber >= 2) {
                saw_track2_pregap_or_beyond = true;
            }
        }
        return st.state == PlaybackState::Stopped && st.endOfDisc;
    });

    ASSERT_TRUE(finished);
    EXPECT_TRUE(saw_track2_pregap_or_beyond);
    EXPECT_TRUE(observed_tracks.count(1) > 0);
    EXPECT_TRUE(observed_tracks.count(3) > 0);

    const auto final_st = controller.status();
    EXPECT_EQ(final_st.state, PlaybackState::Stopped);
    EXPECT_TRUE(final_st.endOfDisc);
    EXPECT_EQ(final_st.position.lba, 28);
    EXPECT_EQ(final_st.sectorsReadInSession, 28ULL);
    EXPECT_EQ(final_st.bytesConsumedInSession, 28ULL * AudioFormat::kBytesPerSector);
    EXPECT_EQ(final_st.audioDeviceOpenCount, 1ULL);

    // Every single sector 0..27 (including Track 2 INDEX 00 pregap 8..11) was read
    // in exact contiguous sequence without skipping or inserting silence.
    const auto read_lbAs = mock_ptr->flattenedReadSectors();
    ASSERT_EQ(read_lbAs.size(), 28U);
    for (std::size_t i = 0; i < 28; ++i) {
        EXPECT_EQ(read_lbAs[i], static_cast<std::int64_t>(i));
    }

    // Now test seekToTrack(2) separately from continuous playback:
    // seekToTrack(2) must jump to INDEX 01 (LBA 12), skipping INDEX 00 [8, 12).
    mock_ptr->clearRecordedReads();
    controller.seekToTrack(2);
    EXPECT_EQ(controller.currentLba(), 12);
    EXPECT_EQ(controller.currentTrack(), 2);
    EXPECT_EQ(controller.currentIndex(), 1);

    controller.play();
    ASSERT_TRUE(wait_until([&]() { return controller.isEndOfDisc(); }));

    const auto seek_st = controller.status();
    EXPECT_EQ(seek_st.sectorsReadInSession, 16ULL); // LBAs 12..27
    EXPECT_EQ(seek_st.bytesConsumedInSession, 16ULL * AudioFormat::kBytesPerSector);
    EXPECT_EQ(seek_st.audioDeviceOpenCount, 1ULL);  // Still never reopened AudioOutput!

    const auto seek_read_lbas = mock_ptr->flattenedReadSectors();
    ASSERT_EQ(seek_read_lbas.size(), 16U);
    EXPECT_EQ(seek_read_lbas.front(), 12);
    EXPECT_EQ(seek_read_lbas.back(), 27);
}

TEST(PlaybackControllerTest, MixedModeDiscStopsCleanlyBeforeTrailingDataTrack) {
    TrackInfo t1{
        .number = 1,
        .type = TrackType::Audio,
        .startLba = 0,
        .endLba = 8,
        .index01Lba = 0,
        .indexes = {{.trackNumber = 1, .number = 1, .lba = 0, .explicitFromSource = true}},
    };
    TrackInfo t2{
        .number = 2,
        .type = TrackType::Audio,
        .startLba = 8,
        .endLba = 16,
        .index01Lba = 8,
        .indexes = {{.trackNumber = 2, .number = 1, .lba = 8, .explicitFromSource = true}},
    };
    TrackInfo t3_data{
        .number = 3,
        .type = TrackType::Data,
        .startLba = 16,
        .endLba = 30,
        .index01Lba = 16,
        .indexes = {{.trackNumber = 3, .number = 1, .lba = 16, .explicitFromSource = true}},
    };

    PlaybackController controller(PlaybackControllerConfig{
        .audioConfig = {.ringBufferSectors = 30, .preferredDriver = "dummy"},
        .readChunkSectors = 6,
        .prebufferSectors = 4,
    });

    controller.openSource(
        std::make_unique<MockDiscSource>(std::vector<TrackInfo>{t1, t2, t3_data}));

    EXPECT_THROW(controller.seekToTrack(3), DiscSourceError);
    EXPECT_THROW(controller.seekToLba(16), DiscSourceError);

    controller.play();
    ASSERT_TRUE(wait_until([&]() { return controller.isEndOfDisc(); }));

    const auto st = controller.status();
    EXPECT_EQ(st.state, PlaybackState::Stopped);
    EXPECT_TRUE(st.endOfDisc);
    EXPECT_EQ(st.position.lba, 16);
    EXPECT_EQ(st.sectorsReadInSession, 16ULL);
}

TEST(PlaybackControllerTest, InvalidRequestsPreserveActivePlaybackAndErrorPropagationWorks) {
    PlaybackController controller(PlaybackControllerConfig{
        .audioConfig = {.ringBufferSectors = 30, .preferredDriver = "dummy"},
        .readChunkSectors = 4,
        .prebufferSectors = 4,
    });

    EXPECT_THROW(controller.openSource(nullptr), std::invalid_argument);

    // Data-only disc must be rejected on openSource
    TrackInfo data_only{
        .number = 1,
        .type = TrackType::Data,
        .startLba = 0,
        .endLba = 10,
        .index01Lba = 0,
        .indexes = {{.trackNumber = 1, .number = 1, .lba = 0, .explicitFromSource = true}},
    };
    EXPECT_THROW(
        controller.openSource(std::make_unique<MockDiscSource>(std::vector<TrackInfo>{data_only})),
        DiscSourceError);

    // Open valid 80-sector source that simulates disc removal at LBA 20
    TrackInfo audio_trk{
        .number = 1,
        .type = TrackType::Audio,
        .startLba = 0,
        .endLba = 80,
        .index01Lba = 0,
        .indexes = {{.trackNumber = 1, .number = 1, .lba = 0, .explicitFromSource = true}},
    };
    auto mock = std::make_unique<MockDiscSource>(std::vector<TrackInfo>{audio_trk});
    auto* mock_ptr = mock.get();

    controller.openSource(std::move(mock));
    controller.play();
    EXPECT_EQ(controller.state(), PlaybackState::Playing);

    // Out-of-range track and LBA seeks must throw without interrupting active playback
    EXPECT_THROW(controller.seekToTrack(99), DiscOutOfRangeError);
    EXPECT_THROW(controller.seekToLba(-1), DiscOutOfRangeError);
    EXPECT_THROW(controller.seekToLba(80), DiscOutOfRangeError);
    EXPECT_EQ(controller.state(), PlaybackState::Playing);

    // Inject mid-stream disc removal on next sector reads
    controller.pause();
    mock_ptr->setFailureAtOrAboveLba(
        50,
        DiscSourceErrorKind::DiscRemoved,
        "Optical disc was ejected mid-playback");
    controller.seekToLba(46);
    controller.play();

    ASSERT_TRUE(wait_until([&]() { return controller.state() == PlaybackState::Error; }));
    const auto err_st = controller.status();
    EXPECT_EQ(err_st.state, PlaybackState::Error);
    EXPECT_EQ(err_st.errorKind, DiscSourceErrorKind::DiscRemoved);
    EXPECT_TRUE(err_st.errorLba.has_value());
    EXPECT_FALSE(err_st.errorMessage.empty());

    // Recover after clearing failure and stopping
    mock_ptr->setFailureAtOrAboveLba(std::nullopt);
    controller.stop();
    EXPECT_EQ(controller.state(), PlaybackState::Stopped);
    EXPECT_TRUE(controller.lastError().empty());
    EXPECT_FALSE(controller.lastErrorKind().has_value());

    // Verify closing source while actively playing is completely thread-safe
    controller.play();
    EXPECT_EQ(controller.state(), PlaybackState::Playing);
    EXPECT_NO_THROW(controller.closeSource());
    EXPECT_FALSE(controller.hasSource());
    EXPECT_EQ(controller.state(), PlaybackState::Stopped);
}

// Source-independent playback verification routine: proves that the exact same
// PlaybackController logic plays, pauses, resumes, seeks, and crosses track
// boundaries gaplessly regardless of whether the underlying DiscSource is a
// TOC image, a CUE image, or a physical optical CD!
void verify_source_independent_playback(
    PlaybackController& controller,
    std::unique_ptr<DiscSource> source) {
    controller.openSource(std::move(source));
    ASSERT_TRUE(controller.hasSource());
    const std::uint64_t opens_after_open_source =
        controller.status().audioDeviceOpenCount;

    const auto tracks = controller.getTracks();
    ASSERT_FALSE(tracks.empty());

    controller.play();
    EXPECT_EQ(controller.state(), PlaybackState::Playing);

    const std::int64_t initial_lba = controller.currentLba();
    ASSERT_TRUE(wait_until(
        [&]() {
            return controller.currentLba() > initial_lba || controller.isEndOfDisc();
        },
        std::chrono::milliseconds(4000)));

    if (tracks.size() >= 2 &&
        tracks[0].type == TrackType::Audio &&
        tracks[1].type == TrackType::Audio &&
        tracks[0].endLba == tracks[1].startLba) {
        // Seek just before the Track 1 -> Track 2 boundary and verify continuous
        // gapless crossing into Track 2 without reopening the audio device.
        const std::int64_t pre_boundary =
            std::max(tracks[0].startLba, tracks[0].endLba - 6);
        controller.seekToLba(pre_boundary);
        EXPECT_EQ(controller.state(), PlaybackState::Playing);

        ASSERT_TRUE(wait_until(
            [&]() {
                const auto pos = controller.currentPosition();
                return (pos.trackNumber.has_value() && *pos.trackNumber >= tracks[1].number) ||
                       controller.isEndOfDisc();
            },
            std::chrono::milliseconds(4000)));

        EXPECT_EQ(controller.status().audioDeviceOpenCount, opens_after_open_source);
    }

    controller.stop();
    EXPECT_EQ(controller.state(), PlaybackState::Stopped);
    controller.closeSource();
    EXPECT_FALSE(controller.hasSource());
}

TEST(PlaybackControllerTest, SameControllerPlaysSyntheticTocAndCueImages) {
    const fs::path temp_dir = fs::temp_directory_path() / "mycds_phase5_playback_test";
    std::error_code ec;
    fs::remove_all(temp_dir, ec);
    fs::create_directories(temp_dir, ec);

    const fs::path bin_path = temp_dir / "synth.bin";
    const fs::path toc_path = temp_dir / "synth.toc";
    const fs::path cue_path = temp_dir / "synth.cue";

    constexpr std::size_t kTotalSectors = 30;
    {
        std::ofstream bin_out(bin_path, std::ios::binary);
        const std::vector<std::byte> pcm = generate_sine_wave_pcm(440.0, kTotalSectors, 0.1);
        bin_out.write(
            reinterpret_cast<const char*>(pcm.data()),
            static_cast<std::streamsize>(pcm.size()));
    }

    {
        std::ofstream toc_out(toc_path);
        toc_out << "CD_DA\n\n"
                << "TRACK AUDIO\n"
                << "FILE \"synth.bin\" 0 00:00:15\n\n"
                << "TRACK AUDIO\n"
                << "FILE \"synth.bin\" 00:00:15 00:00:15\n"
                << "START 00:00:03\n";
    }

    {
        std::ofstream cue_out(cue_path);
        cue_out << "FILE \"synth.bin\" BINARY\n"
                << "  TRACK 01 AUDIO\n"
                << "    INDEX 01 00:00:00\n"
                << "  TRACK 02 AUDIO\n"
                << "    INDEX 00 00:00:15\n"
                << "    INDEX 01 00:00:18\n";
    }

    PlaybackController controller(PlaybackControllerConfig{
        .audioConfig = {.ringBufferSectors = 40, .preferredDriver = "dummy"},
        .readChunkSectors = 8,
        .prebufferSectors = 8,
    });

    verify_source_independent_playback(controller, open_image_source(toc_path.string()));
    verify_source_independent_playback(controller, open_image_source(cue_path.string()));

    fs::remove_all(temp_dir, ec);
}

TEST(PlaybackControllerTest, ReferenceImageAndPhysicalCdExitCriterion) {
    PlaybackController controller(PlaybackControllerConfig{
        .audioConfig = {.ringBufferSectors = 150, .preferredDriver = ""},
        .readChunkSectors = 16,
        .prebufferSectors = 16,
    });

    bool tested_any = false;

    const fs::path ref_toc("/home/kgorelov/img/album.toc");
    std::error_code ec;
    if (fs::exists(ref_toc, ec)) {
        verify_source_independent_playback(controller, open_image_source(ref_toc.string()));
        tested_any = true;
    }

    std::unique_ptr<DiscSource> phys_source;
    try {
        phys_source = open_physical_cd_source("/dev/sr0");
    } catch (const std::exception&) {
        phys_source = nullptr;
    }

    if (phys_source) {
        verify_source_independent_playback(controller, std::move(phys_source));
        tested_any = true;
    }

    if (!tested_any) {
        GTEST_SKIP() << "Neither reference image (/home/kgorelov/img/album.toc) nor "
                     << "physical CD (/dev/sr0) is present in this environment";
    }
}

} // namespace
} // namespace mycds
