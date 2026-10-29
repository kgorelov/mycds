#include "audio/audio_output.hpp"
#include "audio/pcm_ring_buffer.hpp"
#include "cli/command_loop.hpp"
#include "domain/audio_format.hpp"
#include "metadata/cddb_disc_id.hpp"
#include "playback/playback_controller.hpp"
#include "source/cue_image_source.hpp"
#include "source/disc_source.hpp"
#include "source/image_source.hpp"
#include "source/physical_cd_source.hpp"
#include "source/toc_image_source.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <numbers>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace mycds {
namespace {

class TempTestDir {
public:
    explicit TempTestDir(const std::string& prefix) {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                (prefix + "_" + std::to_string(tick));
        std::filesystem::create_directories(path_);
    }

    ~TempTestDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

std::int16_t read_s16le(const std::byte* ptr) noexcept {
    const auto lo = static_cast<std::uint16_t>(std::to_integer<unsigned char>(ptr[0]));
    const auto hi = static_cast<std::uint16_t>(std::to_integer<unsigned char>(ptr[1]));
    return static_cast<std::int16_t>(lo | static_cast<std::uint16_t>(hi << 8U));
}

void write_s16le(std::byte* ptr, std::int16_t sample) noexcept {
    const auto u = static_cast<std::uint16_t>(sample);
    ptr[0] = static_cast<std::byte>(u & 0xFFU);
    ptr[1] = static_cast<std::byte>((u >> 8U) & 0xFFU);
}

void write_s16be(std::byte* ptr, std::int16_t sample) noexcept {
    const auto u = static_cast<std::uint16_t>(sample);
    ptr[0] = static_cast<std::byte>((u >> 8U) & 0xFFU);
    ptr[1] = static_cast<std::byte>(u & 0xFFU);
}

// Generates a continuous phase-coherent stereo sine wave across total_sectors
// (Left = +sin, Right = -sin) in both canonical S16LE (for CUE BINARY and reference
// comparison) and big-endian S16BE (for cdrdao TOC DATAFILE raw audio).
struct SyntheticContinuousSineDisc {
    std::vector<std::byte> s16lePcm;
    std::vector<std::byte> s16bePcm;
};

SyntheticContinuousSineDisc generate_continuous_sine_disc(
    std::uint32_t total_sectors,
    double frequency_hz = 440.0,
    double amplitude = 0.4) {
    const std::size_t total_frames =
        static_cast<std::size_t>(total_sectors) * AudioFormat::kFramesPerSector;
    const std::size_t total_bytes =
        static_cast<std::size_t>(total_sectors) * AudioFormat::kBytesPerSector;

    SyntheticContinuousSineDisc out;
    out.s16lePcm.resize(total_bytes);
    out.s16bePcm.resize(total_bytes);

    const double two_pi_f_over_sr =
        (2.0 * std::numbers::pi * frequency_hz) /
        static_cast<double>(AudioFormat::kSampleRate);

    for (std::size_t frame = 0; frame < total_frames; ++frame) {
        const double val = std::sin(static_cast<double>(frame) * two_pi_f_over_sr) *
                           amplitude * 32767.0;
        const auto left = static_cast<std::int16_t>(std::lrint(val));
        const auto right = static_cast<std::int16_t>(-left);

        const std::size_t byte_offset = frame * AudioFormat::kBytesPerSampleFrame;
        write_s16le(out.s16lePcm.data() + byte_offset, left);
        write_s16le(out.s16lePcm.data() + byte_offset + 2, right);

        write_s16be(out.s16bePcm.data() + byte_offset, left);
        write_s16be(out.s16bePcm.data() + byte_offset + 2, right);
    }
    return out;
}

void write_binary_file(
    const std::filesystem::path& path,
    std::span<const std::byte> bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open()) << "Failed to create " << path;
    out.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(out.good());
}

void write_text_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open()) << "Failed to create " << path;
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    ASSERT_TRUE(out.good());
}

PlaybackControllerConfig make_fast_dummy_config() {
    return PlaybackControllerConfig{
        .audioConfig = AudioOutputConfig{
            .ringBufferSectors = 60,
            .preferredDriver = "dummy",
        },
        .readChunkSectors = 8,
        .prebufferSectors = 12,
    };
}

template <typename Predicate>
bool wait_until(Predicate pred, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return pred();
}

// Controllable DiscSource simulating mid-playback optical disc ejection.
class RemovableCdMockSource final : public DiscSource {
public:
    RemovableCdMockSource(std::int64_t total_sectors, std::int64_t eject_at_lba)
        : total_sectors_(total_sectors),
          eject_at_lba_(eject_at_lba),
          pcm_(generate_sine_wave_pcm(440.0, static_cast<std::uint32_t>(total_sectors))) {}

    std::string getSourceKind() const override { return "Simulated Optical CD"; }
    std::string getSourcePath() const override { return "/dev/sr0"; }

    DiscInfo getDiscInfo() const override {
        return DiscInfo{
            .firstTrack = 1,
            .lastTrack = 2,
            .startLba = 0,
            .endLba = total_sectors_,
            .sessions = 1,
            .rawStartSector = 0,
            .rawEndSector = total_sectors_,
        };
    }

    std::vector<TrackInfo> getTracks() const override {
        const std::int64_t split = total_sectors_ / 2;
        return {
            TrackInfo{
                .number = 1,
                .type = TrackType::Audio,
                .startLba = 0,
                .endLba = split,
                .index01Lba = 0,
                .indexes = {IndexInfo{.trackNumber = 1, .number = 1, .lba = 0, .explicitFromSource = true}},
            },
            TrackInfo{
                .number = 2,
                .type = TrackType::Audio,
                .startLba = split,
                .endLba = total_sectors_,
                .index01Lba = split,
                .indexes = {IndexInfo{.trackNumber = 2, .number = 1, .lba = split, .explicitFromSource = true}},
            },
        };
    }

    DiscMetadata getDiscMetadata() const override { return {}; }
    std::vector<TrackMetadata> getTrackMetadata() const override { return {}; }

    std::size_t readAudioSectors(
        std::int64_t startLba,
        std::uint32_t sectorCount,
        std::span<std::byte> destination) override {
        const std::size_t bytes =
            static_cast<std::size_t>(sectorCount) * AudioFormat::kBytesPerSector;
        if (destination.size() < bytes) {
            throw std::invalid_argument("Destination buffer too small");
        }
        if (sectorCount == 0) {
            return 0;
        }
        if (startLba < 0 || startLba > total_sectors_ ||
            static_cast<std::int64_t>(sectorCount) > (total_sectors_ - startLba)) {
            throw DiscOutOfRangeError("LBA out of range");
        }
        const std::int64_t endLba = startLba + static_cast<std::int64_t>(sectorCount);
        const std::int64_t eject_lba = eject_at_lba_.load(std::memory_order_acquire);
        if (eject_lba >= 0 && endLba > eject_lba) {
            throw DiscSourceError(
                DiscSourceErrorKind::DiscRemoved,
                "Optical disc removed from /dev/sr0 while reading LBA " +
                    std::to_string(std::max(startLba, eject_lba)));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const std::size_t src_offset =
            static_cast<std::size_t>(startLba) * AudioFormat::kBytesPerSector;
        std::memcpy(destination.data(), pcm_.data() + src_offset, bytes);
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
    std::int64_t total_sectors_;
    std::atomic<std::int64_t> eject_at_lba_;
    std::vector<std::byte> pcm_;
};

// Task 1 & Acceptance Criteria 12.3:
// Phase-coherent gapless playback across adjacent tracks, INDEX 00 pregap, and INDEX 02
// sub-index in both TOC and CUE images without artificial silence or audio device reopens.
TEST(ValidationHardeningTests, GaplessPlaybackContinuousPhaseCoherentSineAcrossTocAndCue) {
    TempTestDir temp("mycds_phase7_gapless");

    constexpr std::uint32_t kTotalSectors = 90;
    const auto sine_disc = generate_continuous_sine_disc(kTotalSectors, 440.0, 0.45);

    const auto toc_bin_path = temp.path() / "gapless_toc.bin";
    const auto cue_bin_path = temp.path() / "gapless_cue.bin";
    write_binary_file(toc_bin_path, sine_disc.s16bePcm);
    write_binary_file(cue_bin_path, sine_disc.s16lePcm);

    // Track 1: [0, 30), INDEX 01 = 0
    // Track 2: [30, 65), INDEX 00 = 30 (8 sectors), INDEX 01 = 38, INDEX 02 = 52
    // Track 3: [65, 90), INDEX 01 = 65
    const auto toc_path = temp.path() / "gapless.toc";
    write_text_file(
        toc_path,
        "CD_DA\n"
        "\n"
        "TRACK AUDIO\n"
        "DATAFILE \"gapless_toc.bin\" 00:00:30\n"
        "\n"
        "TRACK AUDIO\n"
        "DATAFILE \"gapless_toc.bin\" #70560 00:00:35\n"
        "START 00:00:08\n"
        "INDEX 00:00:14\n"
        "\n"
        "TRACK AUDIO\n"
        "DATAFILE \"gapless_toc.bin\" #152880 00:00:25\n");

    const auto cue_path = temp.path() / "gapless.cue";
    write_text_file(
        cue_path,
        "FILE \"gapless_cue.bin\" BINARY\n"
        "  TRACK 01 AUDIO\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 00 00:00:30\n"
        "    INDEX 01 00:00:38\n"
        "    INDEX 02 00:00:52\n"
        "  TRACK 03 AUDIO\n"
        "    INDEX 01 00:00:65\n");

    // 1. Verify exact sample-by-sample continuity across all track and index boundaries
    // for arbitrary multi-sector cross-track read chunks.
    for (const auto& img_path : {toc_path, cue_path}) {
        auto src = open_image_source(img_path.string());
        ASSERT_NE(src, nullptr);

        const auto dinfo = src->getDiscInfo();
        const auto tracks = src->getTracks();
        ASSERT_EQ(tracks.size(), 3U);
        EXPECT_EQ(dinfo.startLba, 0);
        EXPECT_EQ(dinfo.endLba, kTotalSectors);

        EXPECT_EQ(tracks[0].startLba, 0);
        EXPECT_EQ(tracks[0].index01Lba, 0);
        EXPECT_EQ(tracks[0].endLba, 30);

        EXPECT_EQ(tracks[1].startLba, 30);
        EXPECT_EQ(tracks[1].index01Lba, 38);
        EXPECT_EQ(tracks[1].endLba, 65);
        EXPECT_EQ(tracks[1].pregapLength, 8);

        EXPECT_EQ(tracks[2].startLba, 65);
        EXPECT_EQ(tracks[2].index01Lba, 65);
        EXPECT_EQ(tracks[2].endLba, 90);

        // Verify position resolution at every boundary LBA.
        const auto p29 = resolve_playback_position(dinfo, tracks, 29);
        EXPECT_EQ(p29.trackNumber, 1);
        EXPECT_EQ(p29.indexNumber, 1);
        EXPECT_EQ(p29.trackRelativeSectors, 29);

        const auto p30 = resolve_playback_position(dinfo, tracks, 30);
        EXPECT_EQ(p30.trackNumber, 2);
        EXPECT_EQ(p30.indexNumber, 0);
        EXPECT_EQ(p30.trackRelativeSectors, -8);

        const auto p38 = resolve_playback_position(dinfo, tracks, 38);
        EXPECT_EQ(p38.trackNumber, 2);
        EXPECT_EQ(p38.indexNumber, 1);
        EXPECT_EQ(p38.trackRelativeSectors, 0);

        const auto p52 = resolve_playback_position(dinfo, tracks, 52);
        EXPECT_EQ(p52.trackNumber, 2);
        EXPECT_EQ(p52.indexNumber, 2);
        EXPECT_EQ(p52.trackRelativeSectors, 14);

        const auto p65 = resolve_playback_position(dinfo, tracks, 65);
        EXPECT_EQ(p65.trackNumber, 3);
        EXPECT_EQ(p65.indexNumber, 1);
        EXPECT_EQ(p65.trackRelativeSectors, 0);

        // Read in prime-sized 13-sector chunks across all boundaries and compare with
        // the uninterrupted reference sine wave (zero silence, zero phase glitch).
        std::vector<std::byte> reconstructed;
        reconstructed.reserve(sine_disc.s16lePcm.size());
        for (std::uint32_t lba = 0; lba < kTotalSectors;) {
            const std::uint32_t chunk = std::min<std::uint32_t>(13U, kTotalSectors - lba);
            const auto bytes = src->readAudioSectors(lba, chunk);
            reconstructed.insert(reconstructed.end(), bytes.begin(), bytes.end());
            lba += chunk;
        }
        ASSERT_EQ(reconstructed.size(), sine_disc.s16lePcm.size());
        EXPECT_EQ(
            std::memcmp(
                reconstructed.data(),
                sine_disc.s16lePcm.data(),
                sine_disc.s16lePcm.size()),
            0)
            << "PCM mismatch across track boundary for " << img_path;
    }

    // 2. Stream both TOC and CUE end-to-end through the same PlaybackController and
    // verify zero underruns, zero device reopens across track boundaries, and exact byte count.
    PlaybackController controller(make_fast_dummy_config());
    for (const auto& img_path : {toc_path, cue_path}) {
        controller.openSource(open_image_source(img_path.string()));
        const std::uint64_t opens_at_start = controller.status().audioDeviceOpenCount;

        bool saw_track_1 = false;
        bool saw_track_2 = false;
        bool saw_track_3 = false;

        controller.play();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            const auto st = controller.status();
            if (st.position.trackNumber == 1) {
                saw_track_1 = true;
            } else if (st.position.trackNumber == 2) {
                saw_track_2 = true;
            } else if (st.position.trackNumber == 3) {
                saw_track_3 = true;
            }
            EXPECT_EQ(st.audioDeviceOpenCount, opens_at_start);
            if (st.endOfDisc && st.state == PlaybackState::Stopped) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }

        const auto final_status = controller.status();
        EXPECT_TRUE(final_status.endOfDisc);
        EXPECT_EQ(final_status.state, PlaybackState::Stopped);
        EXPECT_EQ(final_status.audioDeviceOpenCount, opens_at_start);
        EXPECT_EQ(final_status.underrunCount, 0ULL);
        EXPECT_EQ(final_status.sectorsReadInSession, kTotalSectors);
        EXPECT_EQ(
            final_status.bytesConsumedInSession,
            static_cast<std::uint64_t>(kTotalSectors) * AudioFormat::kBytesPerSector);
        EXPECT_TRUE(saw_track_1);
        EXPECT_TRUE(saw_track_2);
        EXPECT_TRUE(saw_track_3);
    }
}

// Tasks 2 & 3 & Acceptance Criteria 12.4:
// Compare source metadata (DiscInfo, TrackInfo, CDDB Disc ID) and audio samples across
// reference TOC, CUE, and physical CD (/dev/sr0 if available with the matching disc).
TEST(ValidationHardeningTests, ReferenceSourcesMetadataAndSampleConsistency) {
    const std::filesystem::path toc_path = "/home/kgorelov/img/album.toc";
    const std::filesystem::path cue_path = "/home/kgorelov/img/album.cue";
    if (!std::filesystem::exists(toc_path) || !std::filesystem::exists(cue_path)) {
        GTEST_SKIP() << "Reference images not present in /home/kgorelov/img; skipping.";
    }

    TocImageSource toc_src(toc_path);
    CueImageSource cue_src(cue_path);

    const auto toc_info = toc_src.getDiscInfo();
    const auto cue_info = cue_src.getDiscInfo();
    const auto toc_tracks = toc_src.getTracks();
    const auto cue_tracks = cue_src.getTracks();

    EXPECT_EQ(toc_info.sessions, cue_info.sessions);
    EXPECT_EQ(toc_info.firstTrack, cue_info.firstTrack);
    EXPECT_EQ(toc_info.lastTrack, cue_info.lastTrack);
    EXPECT_EQ(toc_info.startLba, 0);
    EXPECT_EQ(cue_info.startLba, 0);
    EXPECT_EQ(toc_info.endLba, cue_info.endLba);
    ASSERT_EQ(toc_tracks.size(), cue_tracks.size());
    ASSERT_FALSE(toc_tracks.empty());

    for (std::size_t i = 0; i < toc_tracks.size(); ++i) {
        EXPECT_EQ(toc_tracks[i].number, cue_tracks[i].number) << "Track index " << i;
        EXPECT_EQ(toc_tracks[i].type, cue_tracks[i].type) << "Track index " << i;
        EXPECT_EQ(toc_tracks[i].startLba, cue_tracks[i].startLba) << "Track index " << i;
        EXPECT_EQ(toc_tracks[i].index01Lba, cue_tracks[i].index01Lba) << "Track index " << i;
        EXPECT_EQ(toc_tracks[i].endLba, cue_tracks[i].endLba) << "Track index " << i;
        EXPECT_EQ(toc_tracks[i].pregapLength, cue_tracks[i].pregapLength) << "Track index " << i;
        ASSERT_EQ(toc_tracks[i].indexes.size(), cue_tracks[i].indexes.size()) << "Track index " << i;
        for (std::size_t j = 0; j < toc_tracks[i].indexes.size(); ++j) {
            EXPECT_EQ(toc_tracks[i].indexes[j].number, cue_tracks[i].indexes[j].number);
            EXPECT_EQ(toc_tracks[i].indexes[j].lba, cue_tracks[i].indexes[j].lba);
        }
    }

    const auto toc_cddb = calculate_cddb_disc_id(toc_info, toc_tracks);
    const auto cue_cddb = calculate_cddb_disc_id(cue_info, cue_tracks);
    EXPECT_EQ(toc_cddb.discIdHex, cue_cddb.discIdHex);
    EXPECT_EQ(toc_cddb.trackFrameOffsets, cue_cddb.trackFrameOffsets);
    EXPECT_EQ(toc_cddb.totalLengthSeconds, cue_cddb.totalLengthSeconds);

    // Compare canonical S16LE PCM samples across TOC and CUE at start, Track 2->3
    // pregap/INDEX 01 boundary, and final sectors of the disc.
    const std::vector<std::pair<std::int64_t, std::uint32_t>> sample_windows = {
        {0, 8},
        {std::min<std::int64_t>(50084, toc_info.endLba - 12), 12},
        {toc_info.endLba - 8, 8},
    };
    for (const auto& [lba, count] : sample_windows) {
        if (lba < 0 || lba + count > toc_info.endLba) {
            continue;
        }
        const auto toc_pcm = toc_src.readAudioSectors(lba, count);
        const auto cue_pcm = cue_src.readAudioSectors(lba, count);
        ASSERT_EQ(toc_pcm.size(), cue_pcm.size());
        EXPECT_EQ(std::memcmp(toc_pcm.data(), cue_pcm.data(), toc_pcm.size()), 0)
            << "TOC vs CUE PCM mismatch at LBA " << lba;
    }

    // Optional physical CD consistency check if /dev/sr0 holds the same reference disc.
    if (!std::filesystem::exists("/dev/sr0")) {
        return;
    }
    std::unique_ptr<DiscSource> cd_src;
    try {
        cd_src = open_physical_cd_source("/dev/sr0");
    } catch (const DiscSourceError&) {
        return;
    }

    const auto cd_info = cd_src->getDiscInfo();
    const auto cd_tracks = cd_src->getTracks();
    const auto cd_cddb = calculate_cddb_disc_id(cd_info, cd_tracks);
    if (cd_cddb.discIdHex != toc_cddb.discIdHex) {
        return;
    }

    EXPECT_EQ(cd_info.firstTrack, toc_info.firstTrack);
    EXPECT_EQ(cd_info.lastTrack, toc_info.lastTrack);
    EXPECT_EQ(cd_info.startLba, toc_info.startLba);
    EXPECT_EQ(cd_info.endLba, toc_info.endLba);
    ASSERT_EQ(cd_tracks.size(), toc_tracks.size());

    for (std::size_t i = 0; i < cd_tracks.size(); ++i) {
        EXPECT_EQ(cd_tracks[i].number, toc_tracks[i].number);
        EXPECT_EQ(cd_tracks[i].type, toc_tracks[i].type);
        // Lead-In TOC stores INDEX 01 start addresses for every track.
        EXPECT_EQ(cd_tracks[i].index01Lba, toc_tracks[i].index01Lba)
            << "INDEX 01 mismatch on track " << cd_tracks[i].number;
        // Physical Lead-In TOC does not encode Q-subchannel INDEX 00 split points, so
        // physical track[i].endLba equals next track's index01Lba (or disc endLba).
        const std::int64_t expected_cd_end =
            (i + 1 < toc_tracks.size()) ? toc_tracks[i + 1].index01Lba : toc_info.endLba;
        EXPECT_EQ(cd_tracks[i].endLba, expected_cd_end)
            << "End LBA mismatch on track " << cd_tracks[i].number;
    }

    // Compare physical CD audio samples against the ripped image in an active musical
    // region (LBA 500..510), accounting for constant hardware optical drive read offset.
    constexpr std::int64_t kProbeLba = 500;
    constexpr std::uint32_t kProbeSectors = 10;
    if (cd_info.endLba >= kProbeLba + kProbeSectors + 4) {
        const auto cd_pcm = cd_src->readAudioSectors(kProbeLba, kProbeSectors);
        const auto img_window = toc_src.readAudioSectors(kProbeLba - 4, kProbeSectors + 8);
        ASSERT_EQ(cd_pcm.size(), kProbeSectors * AudioFormat::kBytesPerSector);

        const std::size_t cd_frames = cd_pcm.size() / AudioFormat::kBytesPerSampleFrame;
        const std::size_t img_frames = img_window.size() / AudioFormat::kBytesPerSampleFrame;

        // Verify non-silent musical signal on physical CD.
        std::uint64_t abs_sum = 0;
        for (std::size_t f = 0; f < cd_frames; ++f) {
            const std::int16_t l = read_s16le(cd_pcm.data() + f * 4);
            const std::int16_t r = read_s16le(cd_pcm.data() + f * 4 + 2);
            abs_sum += static_cast<std::uint64_t>(std::abs(static_cast<int>(l))) +
                       static_cast<std::uint64_t>(std::abs(static_cast<int>(r)));
        }
        EXPECT_GT(abs_sum, 0ULL);

        // Search within +/- 4 sectors (+/- 2352 stereo frames) for exact byte-for-byte
        // PCM frame alignment between /dev/sr0 and the ripped image.
        constexpr std::size_t kMatchSpanFrames = 588 * 4;
        constexpr std::size_t kCdAnchorFrame = 588 * 2;
        bool found_exact_offset_match = false;
        for (std::size_t img_off = 0; img_off + kMatchSpanFrames <= img_frames; ++img_off) {
            if (std::memcmp(
                    cd_pcm.data() + kCdAnchorFrame * 4,
                    img_window.data() + img_off * 4,
                    kMatchSpanFrames * 4) == 0) {
                found_exact_offset_match = true;
                break;
            }
        }
        EXPECT_TRUE(found_exact_offset_match)
            << "Physical CD samples around LBA " << kProbeLba
            << " did not match ripped TOC image within +/- 2352 sample drive offset window";
    }
}

// Task 4 & Acceptance Criteria 12.1 & 12.5:
// Invalid images, truncated backing files, non-audio tracks, and extreme LBA bounds.
TEST(ValidationHardeningTests, InvalidImagesTruncatedFilesNonAudioTracksAndOverflowBounds) {
    TempTestDir temp("mycds_phase7_invalid");

    // 1. Truncated backing .bin file must fail open-time validation for BOTH TOC and CUE.
    const auto trunc_bin = temp.path() / "truncated.bin";
    // Write 19 sectors + 100 bytes when descriptor claims 20 sectors (47,040 bytes).
    std::vector<std::byte> short_bytes(19U * AudioFormat::kBytesPerSector + 100U, std::byte{0x33});
    write_binary_file(trunc_bin, short_bytes);

    const auto trunc_toc = temp.path() / "truncated.toc";
    write_text_file(
        trunc_toc,
        "CD_DA\n"
        "TRACK AUDIO\n"
        "DATAFILE \"truncated.bin\" 00:00:20\n");

    try {
        TocImageSource bad_toc(trunc_toc);
        FAIL() << "Expected TocImageSource to reject truncated backing .bin file";
    } catch (const DiscSourceError& ex) {
        EXPECT_EQ(ex.kind(), DiscSourceErrorKind::CorruptImage);
    }

    const auto trunc_cue = temp.path() / "truncated.cue";
    write_text_file(
        trunc_cue,
        "FILE \"truncated.bin\" BINARY\n"
        "  TRACK 01 AUDIO\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 01 00:00:19\n");
    // Note: Track 02 starts at sector 19, and truncated.bin has 19 sectors + 100 bytes
    // (not an integral sector and < 1 full sector for Track 02).
    // Also test explicit multi-track CUE where Track 02 INDEX 01 = 00:00:20 exceeds file size:
    const auto trunc_cue2 = temp.path() / "truncated2.cue";
    write_text_file(
        trunc_cue2,
        "FILE \"truncated.bin\" BINARY\n"
        "  TRACK 01 AUDIO\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 01 00:00:20\n");
    try {
        CueImageSource bad_cue(trunc_cue2);
        FAIL() << "Expected CueImageSource to reject CUE referencing past EOF";
    } catch (const DiscSourceError& ex) {
        EXPECT_EQ(ex.kind(), DiscSourceErrorKind::CorruptImage);
    }

    // 2. Missing referenced .bin file in TOC and CUE.
    const auto missing_toc = temp.path() / "missing_bin.toc";
    write_text_file(
        missing_toc,
        "CD_DA\n"
        "TRACK AUDIO\n"
        "DATAFILE \"does_not_exist.bin\" 00:00:10\n");
    EXPECT_THROW((void)open_image_source(missing_toc.string()), DiscSourceError);

    const auto missing_cue = temp.path() / "missing_bin.cue";
    write_text_file(
        missing_cue,
        "FILE \"does_not_exist.bin\" BINARY\n"
        "  TRACK 01 AUDIO\n"
        "    INDEX 01 00:00:00\n");
    EXPECT_THROW((void)open_image_source(missing_cue.string()), DiscSourceError);

    // 3. Mixed-mode disc (Track 1 Mode 1 Data [0, 10), Track 2 Audio [10, 25)) in TOC and CUE:
    // reading Track 1 throws NonAudioTrack, reading cross-boundary [8, 12) throws NonAudioTrack,
    // reading Track 2 [10, 25) succeeds, and extreme LBA values throw DiscOutOfRangeError safely.
    std::vector<std::byte> mixed_bytes(25U * AudioFormat::kBytesPerSector, std::byte{0x24});
    write_binary_file(temp.path() / "mixed.bin", mixed_bytes);

    const auto mixed_toc = temp.path() / "mixed.toc";
    write_text_file(
        mixed_toc,
        "CD_ROM\n"
        "TRACK MODE1_RAW\n"
        "DATAFILE \"mixed.bin\" 00:00:10\n"
        "TRACK AUDIO\n"
        "DATAFILE \"mixed.bin\" #23520 00:00:15\n");

    const auto mixed_cue = temp.path() / "mixed.cue";
    write_text_file(
        mixed_cue,
        "FILE \"mixed.bin\" BINARY\n"
        "  TRACK 01 MODE1/2352\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 01 00:00:10\n");

    for (const auto& mixed_path : {mixed_toc, mixed_cue}) {
        auto src = open_image_source(mixed_path.string());
        ASSERT_NE(src, nullptr);
        const auto tracks = src->getTracks();
        ASSERT_EQ(tracks.size(), 2U);
        EXPECT_EQ(tracks[0].type, TrackType::Data);
        EXPECT_EQ(tracks[1].type, TrackType::Audio);

        try {
            (void)src->readAudioSectors(0, 1);
            FAIL() << "Expected NonAudioTrack on data track for " << mixed_path;
        } catch (const DiscSourceError& ex) {
            EXPECT_EQ(ex.kind(), DiscSourceErrorKind::NonAudioTrack);
        }

        try {
            (void)src->readAudioSectors(8, 4);
            FAIL() << "Expected NonAudioTrack on mixed boundary read for " << mixed_path;
        } catch (const DiscSourceError& ex) {
            EXPECT_EQ(ex.kind(), DiscSourceErrorKind::NonAudioTrack);
        }

        const auto audio_bytes = src->readAudioSectors(10, 15);
        EXPECT_EQ(audio_bytes.size(), 15U * AudioFormat::kBytesPerSector);

        // Overflow-safe out-of-range checks.
        std::vector<std::byte> one_sector(AudioFormat::kBytesPerSector);
        EXPECT_THROW(
            src->readAudioSectors(std::numeric_limits<std::int64_t>::min(), 1, one_sector),
            DiscOutOfRangeError);
        EXPECT_THROW(
            src->readAudioSectors(std::numeric_limits<std::int64_t>::max(), 1, one_sector),
            DiscOutOfRangeError);
        EXPECT_THROW(src->readAudioSectors(-1, 1, one_sector), DiscOutOfRangeError);
        EXPECT_THROW(src->readAudioSectors(25, 1, one_sector), DiscOutOfRangeError);
    }
}

// Task 4 & Section 7.4:
// Mid-playback sector read failure (truncating backing .bin while open) must report
// ReadFailed and transition PlaybackController to Error rather than playing silent zeros!
TEST(ValidationHardeningTests, MidPlaybackFileTruncationReportsReadFailedWithoutSilentSubstitution) {
    TempTestDir temp("mycds_phase7_midtrunc");

    constexpr std::uint32_t kTotalSectors = 100;
    const auto sine_disc = generate_continuous_sine_disc(kTotalSectors, 523.25, 0.35);

    const auto toc_bin = temp.path() / "live_toc.bin";
    const auto cue_bin = temp.path() / "live_cue.bin";
    write_binary_file(toc_bin, sine_disc.s16bePcm);
    write_binary_file(cue_bin, sine_disc.s16lePcm);

    const auto toc_path = temp.path() / "live.toc";
    write_text_file(
        toc_path,
        "CD_DA\n"
        "TRACK AUDIO\n"
        "DATAFILE \"live_toc.bin\" 00:01:25\n");

    const auto cue_path = temp.path() / "live.cue";
    write_text_file(
        cue_path,
        "FILE \"live_cue.bin\" BINARY\n"
        "  TRACK 01 AUDIO\n"
        "    INDEX 01 00:00:00\n");

    // Verify direct read failure after runtime truncation for both TOC and CUE.
    {
        TocImageSource toc_src(toc_path);
        CueImageSource cue_src(cue_path);

        // Truncate both backing files to 15 sectors while sources are already open.
        std::filesystem::resize_file(toc_bin, 15U * AudioFormat::kBytesPerSector);
        std::filesystem::resize_file(cue_bin, 15U * AudioFormat::kBytesPerSector);

        EXPECT_EQ(toc_src.readAudioSectors(0, 15).size(), 15U * AudioFormat::kBytesPerSector);
        EXPECT_EQ(cue_src.readAudioSectors(0, 15).size(), 15U * AudioFormat::kBytesPerSector);

        try {
            (void)toc_src.readAudioSectors(14, 2);
            FAIL() << "Expected TocImageSource::readAudioSectors to fail on truncated sector 15";
        } catch (const DiscSourceError& ex) {
            EXPECT_EQ(ex.kind(), DiscSourceErrorKind::ReadFailed);
        }

        try {
            (void)cue_src.readAudioSectors(14, 2);
            FAIL() << "Expected CueImageSource::readAudioSectors to fail on truncated sector 15";
        } catch (const DiscSourceError& ex) {
            EXPECT_EQ(ex.kind(), DiscSourceErrorKind::ReadFailed);
        }
    }

    // Verify PlaybackController transitions cleanly to PlaybackState::Error on TOC mid-stream truncation.
    write_binary_file(toc_bin, sine_disc.s16bePcm);
    auto toc_for_play = open_image_source(toc_path.string());
    std::filesystem::resize_file(toc_bin, 20U * AudioFormat::kBytesPerSector);

    PlaybackController controller(make_fast_dummy_config());
    controller.openSource(std::move(toc_for_play));
    controller.play();

    ASSERT_TRUE(wait_until(
        [&]() { return controller.state() == PlaybackState::Error; },
        std::chrono::seconds(3)));

    const auto err_status = controller.status();
    EXPECT_EQ(err_status.state, PlaybackState::Error);
    EXPECT_EQ(err_status.errorKind, DiscSourceErrorKind::ReadFailed);
    EXPECT_FALSE(err_status.errorMessage.empty());
    ASSERT_TRUE(err_status.errorLba.has_value());
    EXPECT_LE(*err_status.errorLba, 20);

    controller.closeSource();
    EXPECT_FALSE(controller.hasSource());
    EXPECT_EQ(controller.state(), PlaybackState::Stopped);
}

// Task 5 & Acceptance Criteria 12.5:
// Simulated optical disc removal during active playback transitions cleanly to Error,
// surfaces [Disc Removed] in CommandLoop, and shuts down without dangling audio callbacks.
TEST(ValidationHardeningTests, DiscRemovalDuringActivePlaybackAndCliDiagnostics) {
    PlaybackController controller(make_fast_dummy_config());
    controller.openSource(std::make_unique<RemovableCdMockSource>(120, 28));

    controller.play();
    ASSERT_TRUE(wait_until(
        [&]() { return controller.state() == PlaybackState::Error; },
        std::chrono::seconds(3)));

    const auto st = controller.status();
    EXPECT_EQ(st.state, PlaybackState::Error);
    EXPECT_EQ(st.errorKind, DiscSourceErrorKind::DiscRemoved);
    EXPECT_NE(st.errorMessage.find("/dev/sr0"), std::string::npos);

    // Also verify CommandLoop reports [Disc Removed] during --play and exits cleanly.
    controller.openSource(std::make_unique<RemovableCdMockSource>(120, 20));
    std::istringstream in("position\nquit\n");
    std::ostringstream out;
    std::ostringstream err;

    CommandLoop loop(
        controller,
        in,
        out,
        err,
        CommandLoopOptions{.autoPlayOnStart = true});

    // Wait briefly inside a background helper or let autoPlay start then run commands:
    // Give producer 100ms before feeding commands? istringstream is immediate, so let's
    // test async error reporting after Error occurs on a fresh controller session:
    EXPECT_EQ(loop.run(), 0);
    EXPECT_FALSE(controller.hasSource());

    // Now trigger DiscRemoved while playing, then inspect via CommandLoop commands:
    controller.openSource(std::make_unique<RemovableCdMockSource>(120, 16));
    controller.play();
    ASSERT_TRUE(wait_until(
        [&]() { return controller.state() == PlaybackState::Error; },
        std::chrono::seconds(3)));

    std::istringstream in2("position\nstop\nquit\n");
    std::ostringstream out2;
    std::ostringstream err2;
    CommandLoop loop2(controller, in2, out2, err2);
    EXPECT_EQ(loop2.run(), 0);

    EXPECT_NE(err2.str().find("[Disc Removed]"), std::string::npos);
    EXPECT_NE(out2.str().find("State      : Error"), std::string::npos);
    EXPECT_FALSE(controller.hasSource());
}

// Task 6 (Part A):
// Lock-free SPSC PcmRingBuffer concurrent data-integrity stress test.
TEST(ValidationHardeningTests, PcmRingBufferConcurrentSpscDataIntegrityStress) {
    // Small 64-frame (256-byte) ring buffer forces thousands of wrap-arounds and full/empty edges.
    PcmRingBuffer ring(64U * AudioFormat::kBytesPerSampleFrame);

    constexpr std::uint32_t kTotalFrames = 200000U;
    std::atomic<bool> start_flag{false};

    std::thread producer([&]() {
        while (!start_flag.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::vector<std::byte> chunk_buf(37U * AudioFormat::kBytesPerSampleFrame);
        std::uint32_t next_frame = 0;
        std::uint32_t step = 1;
        while (next_frame < kTotalFrames) {
            const std::uint32_t batch =
                std::min<std::uint32_t>((step % 37U) + 1U, kTotalFrames - next_frame);
            ++step;
            for (std::uint32_t i = 0; i < batch; ++i) {
                const std::uint32_t marker = next_frame + i;
                std::memcpy(
                    chunk_buf.data() + i * AudioFormat::kBytesPerSampleFrame,
                    &marker,
                    sizeof(marker));
            }
            const std::size_t bytes_to_send = batch * AudioFormat::kBytesPerSampleFrame;
            const std::size_t written =
                ring.write(std::span<const std::byte>(chunk_buf.data(), bytes_to_send));
            ASSERT_EQ(written % AudioFormat::kBytesPerSampleFrame, 0U);
            next_frame += static_cast<std::uint32_t>(written / AudioFormat::kBytesPerSampleFrame);
            if (written == 0) {
                std::this_thread::yield();
            }
        }
    });

    std::uint32_t expected_frame = 0;
    bool sequence_corrupted = false;

    std::thread consumer([&]() {
        while (!start_flag.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::vector<std::byte> read_buf(29U * AudioFormat::kBytesPerSampleFrame);
        std::uint32_t step = 3;
        while (expected_frame < kTotalFrames) {
            const std::size_t req_frames = (step % 29U) + 1U;
            ++step;
            const std::size_t got = ring.read(
                std::span<std::byte>(
                    read_buf.data(),
                    req_frames * AudioFormat::kBytesPerSampleFrame));
            if (got % AudioFormat::kBytesPerSampleFrame != 0U) {
                sequence_corrupted = true;
                break;
            }
            const std::size_t got_frames = got / AudioFormat::kBytesPerSampleFrame;
            for (std::size_t i = 0; i < got_frames; ++i) {
                std::uint32_t marker = 0;
                std::memcpy(
                    &marker,
                    read_buf.data() + i * AudioFormat::kBytesPerSampleFrame,
                    sizeof(marker));
                if (marker != expected_frame) {
                    sequence_corrupted = true;
                    return;
                }
                ++expected_frame;
            }
            if (got == 0) {
                std::this_thread::yield();
            }
        }
    });

    start_flag.store(true, std::memory_order_release);
    producer.join();
    consumer.join();

    EXPECT_FALSE(sequence_corrupted);
    EXPECT_EQ(expected_frame, kTotalFrames);
    EXPECT_EQ(ring.availableToRead(), 0U);
    EXPECT_EQ(
        ring.totalBytesConsumed(),
        static_cast<std::uint64_t>(kTotalFrames) * AudioFormat::kBytesPerSampleFrame);
}

// Task 6 (Part B & C) & Acceptance Criteria 12.5:
// Concurrent SDL3 audio callback + AudioOutput control hammer + PlaybackController
// multi-threaded seek/pause/play/status/closeSource while playback is active.
TEST(ValidationHardeningTests, ConcurrentCallbackAndControllerThreadSafetyStress) {
    // Part B: Hammer AudioOutput while SDL3 dummy audio thread runs callbacks continuously.
    {
        AudioOutput audio;
        audio.open(AudioOutputConfig{
            .ringBufferSectors = 24,
            .preferredDriver = "dummy",
        });
        audio.start();

        const auto tone = generate_sine_wave_pcm(440.0, 4);
        std::atomic<bool> stop_threads{false};

        std::thread writer([&]() {
            while (!stop_threads.load(std::memory_order_acquire)) {
                (void)audio.enqueuePcm(tone);
                std::this_thread::sleep_for(std::chrono::microseconds(400));
            }
        });

        std::thread toggler([&]() {
            int iter = 0;
            while (!stop_threads.load(std::memory_order_acquire)) {
                switch (iter % 5) {
                    case 0:
                        audio.pause();
                        break;
                    case 1:
                        audio.resume();
                        break;
                    case 2:
                        audio.clearBuffer();
                        break;
                    case 3:
                        audio.stop();
                        break;
                    case 4:
                        audio.start();
                        break;
                }
                (void)audio.bufferedBytes();
                (void)audio.totalBytesConsumed();
                (void)audio.underrunCount();
                ++iter;
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        stop_threads.store(true, std::memory_order_release);
        writer.join();
        toggler.join();

        EXPECT_FALSE(audio.hasError()) << audio.lastError();
        audio.close();
        EXPECT_FALSE(audio.isOpen());
    }

    // Part C: Hammer PlaybackController from multiple control/observer threads and closeSource()
    // while playback is actively running.
    TempTestDir temp("mycds_phase7_concurrency");
    const auto sine_disc = generate_continuous_sine_disc(150, 330.0, 0.3);
    write_binary_file(temp.path() / "stress.bin", sine_disc.s16lePcm);
    const auto cue_path = temp.path() / "stress.cue";
    write_text_file(
        cue_path,
        "FILE \"stress.bin\" BINARY\n"
        "  TRACK 01 AUDIO\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 01 00:00:50\n"
        "  TRACK 03 AUDIO\n"
        "    INDEX 01 00:01:25\n");

    PlaybackController controller(make_fast_dummy_config());
    for (int cycle = 0; cycle < 4; ++cycle) {
        controller.openSource(open_image_source(cue_path.string()));
        controller.play();

        std::atomic<bool> stop_workers{false};
        std::thread control_worker([&]() {
            int step = 0;
            while (!stop_workers.load(std::memory_order_acquire)) {
                switch (step % 6) {
                    case 0:
                        controller.seekToTrack((step % 3) + 1);
                        break;
                    case 1:
                        controller.pause();
                        break;
                    case 2:
                        controller.play();
                        break;
                    case 3:
                        controller.seekToLba((step * 17) % 140);
                        break;
                    case 4:
                        controller.stop();
                        break;
                    case 5:
                        controller.play();
                        break;
                }
                ++step;
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
            }
        });

        std::thread status_observer_1([&]() {
            while (!stop_workers.load(std::memory_order_acquire)) {
                const auto st = controller.status();
                EXPECT_NE(st.state, PlaybackState::Error) << st.errorMessage;
                (void)controller.currentPosition();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

        std::thread status_observer_2([&]() {
            while (!stop_workers.load(std::memory_order_acquire)) {
                (void)controller.currentLba();
                (void)controller.currentTrack();
                (void)controller.currentIndex();
                (void)controller.isEndOfDisc();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        stop_workers.store(true, std::memory_order_release);
        control_worker.join();
        status_observer_1.join();
        status_observer_2.join();

        // Ensure playback is active right before closing source to test Section 12.5
        // ("Closing the source while playback is active").
        controller.play();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        controller.closeSource();
        EXPECT_FALSE(controller.hasSource());
    }
}

} // namespace
} // namespace mycds
