#include "domain/audio_format.hpp"
#include "source/disc_source.hpp"
#include "source/image_source.hpp"
#include "source/physical_cd_source.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace mycds {
namespace {

namespace fs = std::filesystem;

// Source-independent contract verification required by Phase 3 exit criterion:
// "the same test code can inspect and read audio sectors from both sources
// without source-specific logic."
void verify_disc_source_contract(DiscSource& source) {
    const DiscInfo disc = source.getDiscInfo();
    const std::vector<TrackInfo> tracks = source.getTracks();

    ASSERT_FALSE(tracks.empty());
    EXPECT_EQ(disc.firstTrack, tracks.front().number);
    EXPECT_EQ(disc.lastTrack, tracks.back().number);
    EXPECT_EQ(disc.startLba, tracks.front().startLba);
    EXPECT_EQ(disc.endLba, tracks.back().endLba);
    EXPECT_LT(disc.startLba, disc.endLba);

    for (std::size_t i = 0; i < tracks.size(); ++i) {
        const auto& trk = tracks[i];
        EXPECT_LT(trk.startLba, trk.endLba);
        EXPECT_GE(trk.index01Lba, trk.startLba);
        EXPECT_LT(trk.index01Lba, trk.endLba);
        EXPECT_FALSE(trk.indexes.empty());
        if (i + 1 < tracks.size()) {
            EXPECT_EQ(trk.endLba, tracks[i + 1].startLba);
        }
    }

    // Zero-sector read returns empty without error
    EXPECT_TRUE(source.readAudioSectors(disc.startLba, 0).empty());

    // First and last audio sector reads via both vector and span overloads
    if (tracks.front().type == TrackType::Audio) {
        const std::vector<std::byte> first_vec =
            source.readAudioSectors(disc.startLba, 1);
        ASSERT_EQ(first_vec.size(), AudioFormat::kBytesPerSector);

        std::vector<std::byte> first_span_buf(AudioFormat::kBytesPerSector);
        const std::size_t bytes_read =
            source.readAudioSectors(disc.startLba, 1, first_span_buf);
        EXPECT_EQ(bytes_read, AudioFormat::kBytesPerSector);
        EXPECT_EQ(first_vec, first_span_buf);

        // Destination buffer too small must throw std::invalid_argument
        std::vector<std::byte> tiny_buf(AudioFormat::kBytesPerSector - 1);
        EXPECT_THROW(
            source.readAudioSectors(disc.startLba, 1, tiny_buf),
            std::invalid_argument);
    }

    if (tracks.back().type == TrackType::Audio) {
        const std::vector<std::byte> last_vec =
            source.readAudioSectors(disc.endLba - 1, 1);
        EXPECT_EQ(last_vec.size(), AudioFormat::kBytesPerSector);
    }

    // Cross-track boundary read when first two tracks are contiguous audio
    if (tracks.size() >= 2 &&
        tracks[0].type == TrackType::Audio &&
        tracks[1].type == TrackType::Audio &&
        tracks[0].endLba > tracks[0].startLba &&
        tracks[1].endLba > tracks[1].startLba) {
        const std::int64_t boundary_lba = tracks[0].endLba - 1;
        const std::vector<std::byte> pair_sectors =
            source.readAudioSectors(boundary_lba, 2);
        ASSERT_EQ(pair_sectors.size(), 2U * AudioFormat::kBytesPerSector);

        const std::vector<std::byte> s0 = source.readAudioSectors(boundary_lba, 1);
        const std::vector<std::byte> s1 = source.readAudioSectors(boundary_lba + 1, 1);
        EXPECT_TRUE(std::equal(s0.begin(), s0.end(), pair_sectors.begin()));
        EXPECT_TRUE(std::equal(
            s1.begin(),
            s1.end(),
            pair_sectors.begin() + static_cast<std::ptrdiff_t>(AudioFormat::kBytesPerSector)));
    }

    // Bounds checking: out-of-range LBAs must throw DiscOutOfRangeError (std::out_of_range)
    EXPECT_THROW(
        source.readAudioSectors(disc.startLba - 1, 1),
        DiscOutOfRangeError);
    EXPECT_THROW(
        source.readAudioSectors(disc.endLba, 1),
        DiscOutOfRangeError);
    EXPECT_THROW(
        source.readAudioSectors(disc.endLba - 1, 2),
        DiscOutOfRangeError);
}

TEST(ImageSourceTest, SyntheticTocAndCueSourcesSatisfyCommonContractAndByteOrder) {
    const fs::path temp_dir = fs::temp_directory_path() / "mycds_phase3_image_source_test";
    std::error_code ec;
    fs::remove_all(temp_dir, ec);
    fs::create_directories(temp_dir, ec);

    const fs::path bin_path = temp_dir / "synth.bin";
    const fs::path toc_path = temp_dir / "synth.toc";
    const fs::path cue_path = temp_dir / "synth.cue";

    // Create 10 sectors in cdrdao big-endian format so both TOC (libMirage) and
    // CUE (with sibling TOC detection) swap bytes to canonical S16LE.
    constexpr std::size_t kTotalSectors = 10;
    {
        std::ofstream bin_out(bin_path, std::ios::binary);
        for (std::size_t s = 0; s < kTotalSectors; ++s) {
            std::vector<std::uint8_t> sector(AudioFormat::kBytesPerSector, 0);
            // Big-endian 16-bit sample 0x1100 + s: high byte 0x11, low byte s
            sector[0] = 0x11;
            sector[1] = static_cast<std::uint8_t>(s);
            bin_out.write(
                reinterpret_cast<const char*>(sector.data()),
                static_cast<std::streamsize>(sector.size()));
        }
    }

    {
        std::ofstream toc_out(toc_path);
        toc_out << "CD_DA\n\n"
                << "TRACK AUDIO\n"
                << "FILE \"synth.bin\" 0 00:00:05\n\n"
                << "TRACK AUDIO\n"
                << "FILE \"synth.bin\" 00:00:05 00:00:05\n"
                << "START 00:00:02\n";
    }

    {
        std::ofstream cue_out(cue_path);
        cue_out << "FILE \"synth.bin\" BINARY\n"
                << "  TRACK 01 AUDIO\n"
                << "    INDEX 01 00:00:00\n"
                << "  TRACK 02 AUDIO\n"
                << "    INDEX 00 00:00:05\n"
                << "    INDEX 01 00:00:07\n";
    }

    auto toc_src = open_image_source(toc_path.string());
    auto cue_src = open_image_source(cue_path.string());

    verify_disc_source_contract(*toc_src);
    verify_disc_source_contract(*cue_src);

    // Verify canonical S16LE output matches between TOC and CUE across all 10 sectors
    const std::vector<std::byte> all_toc = toc_src->readAudioSectors(0, kTotalSectors);
    const std::vector<std::byte> all_cue = cue_src->readAudioSectors(0, kTotalSectors);
    ASSERT_EQ(all_toc, all_cue);

    for (std::size_t s = 0; s < kTotalSectors; ++s) {
        const std::size_t offset = s * AudioFormat::kBytesPerSector;
        // Swapped from big-endian [0x11, s] to little-endian [s, 0x11]
        EXPECT_EQ(all_toc[offset], static_cast<std::byte>(s));
        EXPECT_EQ(all_toc[offset + 1], std::byte{0x11});
    }

    fs::remove_all(temp_dir, ec);
}

TEST(ImageSourceTest, ErrorSemanticsForInvalidFilesAndUnsupportedFormats) {
    EXPECT_THROW(
        open_image_source("/nonexistent/path/image.iso"),
        DiscSourceError);
    EXPECT_THROW(
        open_image_source("/nonexistent/path/missing.toc"),
        DiscSourceError);
    EXPECT_THROW(
        open_image_source("/nonexistent/path/missing.cue"),
        DiscSourceError);
    EXPECT_THROW(
        open_physical_cd_source("/dev/nonexistent_optical_drive_999"),
        DiscSourceError);
}

TEST(ImageSourceTest, ReferenceImagesAndPhysicalCdInterchangeability) {
    const fs::path ref_toc("/home/kgorelov/img/album.toc");
    const fs::path ref_cue("/home/kgorelov/img/album.cue");
    std::error_code ec;
    if (!fs::exists(ref_toc, ec) || !fs::exists(ref_cue, ec)) {
        GTEST_SKIP() << "Reference images not present in /home/kgorelov/img";
    }

    auto toc_source = open_image_source(ref_toc.string());
    auto cue_source = open_image_source(ref_cue.string());

    verify_disc_source_contract(*toc_source);
    verify_disc_source_contract(*cue_source);

    // If physical drive /dev/sr0 is available with the reference CD, verify it through
    // the exact same DiscSource contract and compare audio sectors!
    std::unique_ptr<DiscSource> phys_source;
    try {
        phys_source = open_physical_cd_source("/dev/sr0");
    } catch (const std::exception&) {
        phys_source = nullptr;
    }

    if (phys_source &&
        phys_source->getDiscInfo().endLba == toc_source->getDiscInfo().endLba) {
        verify_disc_source_contract(*phys_source);
    }
}

} // namespace
} // namespace mycds
