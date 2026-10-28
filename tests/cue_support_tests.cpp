#include "domain/audio_format.hpp"
#include "inspect.hpp"
#include "source/cue_image_source.hpp"
#include "source/cue_parser.hpp"
#include "source/disc_source.hpp"
#include "source/toc_image_source.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace mycds {
namespace {

namespace fs = std::filesystem;

class TempTestDir {
public:
    explicit TempTestDir(const std::string& name)
        : path_(fs::temp_directory_path() / ("mycds_test_" + name)) {
        std::error_code ec;
        fs::remove_all(path_, ec);
        fs::create_directories(path_, ec);
    }

    ~TempTestDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    const fs::path& path() const noexcept {
        return path_;
    }

    fs::path write_bin_sectors(
        const std::string& filename,
        std::size_t sector_count,
        std::size_t sector_size = AudioFormat::kBytesPerSector) const {
        const fs::path file_path = path_ / filename;
        std::ofstream out(file_path, std::ios::binary);
        std::vector<std::uint8_t> sector_buf(sector_size, 0);
        for (std::size_t s = 0; s < sector_count; ++s) {
            // Stamp sector index into the first 4 bytes in little-endian order
            sector_buf[0] = static_cast<std::uint8_t>(s & 0xFFU);
            sector_buf[1] = static_cast<std::uint8_t>((s >> 8U) & 0xFFU);
            sector_buf[2] = static_cast<std::uint8_t>((s >> 16U) & 0xFFU);
            sector_buf[3] = static_cast<std::uint8_t>((s >> 24U) & 0xFFU);
            out.write(
                reinterpret_cast<const char*>(sector_buf.data()),
                static_cast<std::streamsize>(sector_buf.size()));
        }
        return file_path;
    }

    fs::path write_text_file(const std::string& filename, const std::string& content) const {
        const fs::path file_path = path_ / filename;
        std::ofstream out(file_path, std::ios::binary);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        return file_path;
    }

private:
    fs::path path_;
};

TEST(CueTimestampTest, ParsesValidAndRejectsInvalidTimestamps) {
    EXPECT_EQ(parse_msf_to_sectors("00:00:00"), 0);
    EXPECT_EQ(parse_msf_to_sectors("00:00:01"), 1);
    EXPECT_EQ(parse_msf_to_sectors("00:02:00"), 150);
    EXPECT_EQ(parse_msf_to_sectors("04:44:30"), 21330);
    EXPECT_EQ(parse_msf_to_sectors("11:07:61"), 50086);
    EXPECT_EQ(parse_msf_to_sectors("11:07:68"), 50093);
    EXPECT_EQ(parse_msf_to_sectors("99:59:74"), (99 * 60 + 59) * 75 + 74);

    EXPECT_EQ(sectors_to_msf(0), "00:00:00");
    EXPECT_EQ(sectors_to_msf(21330), "04:44:30");
    EXPECT_EQ(sectors_to_msf(50086), "11:07:61");

    // Invalid seconds (>= 60), frames (>= 75), negative, or malformed
    EXPECT_FALSE(parse_msf_to_sectors("").has_value());
    EXPECT_FALSE(parse_msf_to_sectors("00:60:00").has_value());
    EXPECT_FALSE(parse_msf_to_sectors("00:00:75").has_value());
    EXPECT_FALSE(parse_msf_to_sectors("-01:00:00").has_value());
    EXPECT_FALSE(parse_msf_to_sectors("00:00").has_value());
    EXPECT_FALSE(parse_msf_to_sectors("00:00:00:00").has_value());
    EXPECT_FALSE(parse_msf_to_sectors("00:ab:00").has_value());
}

TEST(CueParserTest, ParsesAlbumAndTrackMetadataAndMultiTrackLayout) {
    TempTestDir dir("metadata_and_layout");
    dir.write_bin_sectors("album.bin", 300);

    // Include UTF-8 BOM and CRLF line endings
    const std::string cue_content =
        "\xEF\xBB\xBF"
        "REM GENRE \"Alternative Rock\"\r\n"
        "REM DATE 1997\r\n"
        "CATALOG 0724385522925\r\n"
        "PERFORMER \"Radiohead\"\r\n"
        "TITLE \"OK Computer\"\r\n"
        "SONGWRITER \"Thom Yorke & Radiohead\"\r\n"
        "FILE \"album.bin\" BINARY\r\n"
        "  TRACK 01 AUDIO\r\n"
        "    TITLE \"Airbag\"\r\n"
        "    PERFORMER \"Radiohead\"\r\n"
        "    SONGWRITER \"Radiohead\"\r\n"
        "    ISRC GBAYE9701274\r\n"
        "    FLAGS DCP\r\n"
        "    INDEX 01 00:00:00\r\n"
        "    INDEX 02 00:00:50\r\n"
        "  TRACK 02 AUDIO\r\n"
        "    TITLE \"Paranoid Android\"\r\n"
        "    ISRC GBAYE9701376\r\n"
        "    INDEX 00 00:01:25\r\n"
        "    INDEX 01 00:01:32\r\n"
        "  TRACK 03 AUDIO\r\n"
        "    TITLE \"Subterranean Homesick Alien\"\r\n"
        "    INDEX 01 00:02:50\r\n";

    const fs::path cue_path = dir.write_text_file("album.cue", cue_content);
    const ParsedCueSheet parsed = parse_cue_file(cue_path);

    // DiscMetadata checks
    EXPECT_EQ(parsed.layout.discMetadata.title, "OK Computer");
    EXPECT_EQ(parsed.layout.discMetadata.performer, "Radiohead");
    EXPECT_EQ(parsed.layout.discMetadata.songwriter, "Thom Yorke & Radiohead");
    EXPECT_EQ(parsed.layout.discMetadata.catalog, "0724385522925");
    EXPECT_EQ(parsed.layout.discMetadata.genre, "Alternative Rock");
    ASSERT_TRUE(parsed.layout.discMetadata.releaseYear.has_value());
    EXPECT_EQ(*parsed.layout.discMetadata.releaseYear, 1997);

    // DiscInfo checks
    EXPECT_EQ(parsed.layout.discInfo.firstTrack, 1);
    EXPECT_EQ(parsed.layout.discInfo.lastTrack, 3);
    EXPECT_EQ(parsed.layout.discInfo.startLba, 0);
    EXPECT_EQ(parsed.layout.discInfo.endLba, 300);

    // Track layout checks
    ASSERT_EQ(parsed.layout.tracks.size(), 3U);

    // Track 1: [0, 100), INDEX 01 @ 0, INDEX 02 @ 50
    EXPECT_EQ(parsed.layout.tracks[0].number, 1);
    EXPECT_EQ(parsed.layout.tracks[0].type, TrackType::Audio);
    EXPECT_EQ(parsed.layout.tracks[0].startLba, 0);
    EXPECT_EQ(parsed.layout.tracks[0].index01Lba, 0);
    EXPECT_EQ(parsed.layout.tracks[0].endLba, 100);
    EXPECT_EQ(parsed.layout.tracks[0].pregapLength.value_or(-1), 0);
    ASSERT_EQ(parsed.layout.tracks[0].indexes.size(), 2U);
    EXPECT_EQ(parsed.layout.tracks[0].indexes[0].number, 1);
    EXPECT_EQ(parsed.layout.tracks[0].indexes[0].lba, 0);
    EXPECT_EQ(parsed.layout.tracks[0].indexes[1].number, 2);
    EXPECT_EQ(parsed.layout.tracks[0].indexes[1].lba, 50);

    // Track 2: INDEX 00 @ 00:01:25 (100), INDEX 01 @ 00:01:32 (107), ends @ 00:02:50 (200)
    EXPECT_EQ(parsed.layout.tracks[1].number, 2);
    EXPECT_EQ(parsed.layout.tracks[1].startLba, 100);
    EXPECT_EQ(parsed.layout.tracks[1].index01Lba, 107);
    EXPECT_EQ(parsed.layout.tracks[1].endLba, 200);
    EXPECT_EQ(parsed.layout.tracks[1].pregapStartLba.value_or(-1), 100);
    EXPECT_EQ(parsed.layout.tracks[1].pregapLength.value_or(-1), 7);
    EXPECT_TRUE(parsed.layout.tracks[1].pregapInFile);
    ASSERT_EQ(parsed.layout.tracks[1].indexes.size(), 2U);
    EXPECT_EQ(parsed.layout.tracks[1].indexes[0].number, 0);
    EXPECT_EQ(parsed.layout.tracks[1].indexes[0].lba, 100);
    EXPECT_EQ(parsed.layout.tracks[1].indexes[1].number, 1);
    EXPECT_EQ(parsed.layout.tracks[1].indexes[1].lba, 107);

    // Track 3: [200, 300)
    EXPECT_EQ(parsed.layout.tracks[2].number, 3);
    EXPECT_EQ(parsed.layout.tracks[2].startLba, 200);
    EXPECT_EQ(parsed.layout.tracks[2].index01Lba, 200);
    EXPECT_EQ(parsed.layout.tracks[2].endLba, 300);

    // TrackMetadata checks
    ASSERT_EQ(parsed.layout.trackMetadata.size(), 3U);
    EXPECT_EQ(parsed.layout.trackMetadata[0].title, "Airbag");
    EXPECT_EQ(parsed.layout.trackMetadata[0].isrc, "GBAYE9701274");
    EXPECT_EQ(parsed.layout.trackMetadata[1].title, "Paranoid Android");
    EXPECT_EQ(parsed.layout.trackMetadata[1].isrc, "GBAYE9701376");
    EXPECT_EQ(parsed.layout.trackMetadata[2].title, "Subterranean Homesick Alien");
}

TEST(CueImageSourceTest, HandlesSyntheticPregapPostgapAndMultipleFiles) {
    TempTestDir dir("pregap_postgap_multifile");
    dir.write_bin_sectors("track1.bin", 10);
    dir.write_bin_sectors("track2.bin", 15);

    const std::string cue_content =
        "FILE \"track1.bin\" BINARY\n"
        "  TRACK 01 AUDIO\n"
        "    PREGAP 00:00:02\n"
        "    INDEX 01 00:00:00\n"
        "    POSTGAP 00:00:03\n"
        "FILE \"track2.bin\" BINARY\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 01 00:00:00\n";

    const fs::path cue_path = dir.write_text_file("multi.cue", cue_content);
    auto src = open_image_source(cue_path.string());

    const DiscInfo dinfo = src->getDiscInfo();
    // Track 1: 2 pregap + 10 file + 3 postgap = 15 sectors [0, 15)
    // Track 2: 15 file sectors [15, 30)
    EXPECT_EQ(dinfo.startLba, 0);
    EXPECT_EQ(dinfo.endLba, 30);

    const auto tracks = src->getTracks();
    ASSERT_EQ(tracks.size(), 2U);
    EXPECT_EQ(tracks[0].startLba, 0);
    EXPECT_EQ(tracks[0].index01Lba, 2);
    EXPECT_EQ(tracks[0].endLba, 15);
    EXPECT_EQ(tracks[0].pregapLength.value_or(-1), 2);
    EXPECT_FALSE(tracks[0].pregapInFile);
    EXPECT_EQ(tracks[0].postgapLength, 3);

    EXPECT_EQ(tracks[1].startLba, 15);
    EXPECT_EQ(tracks[1].index01Lba, 15);
    EXPECT_EQ(tracks[1].endLba, 30);

    // Read across synthetic pregap (LBA 0..1) and first file sector (LBA 2, which is sector 0 of track1.bin)
    // and across synthetic postgap (LBA 12..14) into Track 2's first sector (LBA 15, sector 0 of track2.bin)
    const auto pre_and_first = src->readAudioSectors(1, 3); // LBA 1 (pregap), LBA 2 (file s=0), LBA 3 (file s=1)
    ASSERT_EQ(pre_and_first.size(), 3U * AudioFormat::kBytesPerSector);
    // LBA 1 is synthetic silence
    EXPECT_EQ(std::to_integer<std::uint8_t>(pre_and_first[0]), 0U);
    // LBA 2 is sector 0 of track1.bin
    EXPECT_EQ(std::to_integer<std::uint8_t>(pre_and_first[AudioFormat::kBytesPerSector]), 0U);
    // LBA 3 is sector 1 of track1.bin
    EXPECT_EQ(std::to_integer<std::uint8_t>(pre_and_first[2 * AudioFormat::kBytesPerSector]), 1U);

    // Read across postgap into Track 2: LBA 14 (postgap) and LBA 15 (sector 0 of track2.bin), LBA 16 (sector 1)
    const auto post_to_t2 = src->readAudioSectors(14, 3);
    EXPECT_EQ(std::to_integer<std::uint8_t>(post_to_t2[0]), 0U);
    EXPECT_EQ(std::to_integer<std::uint8_t>(post_to_t2[AudioFormat::kBytesPerSector]), 0U);
    EXPECT_EQ(std::to_integer<std::uint8_t>(post_to_t2[2 * AudioFormat::kBytesPerSector]), 1U);
}

TEST(CueImageSourceTest, DistinguishesDataTracksAndRejectsAudioReadsFromDataTrack) {
    TempTestDir dir("mixed_mode");
    // 5 sectors of MODE1/2048 + 10 sectors of AUDIO (2352)
    {
        std::ofstream bin_out(dir.path() / "mixed.bin", std::ios::binary);
        std::vector<char> data_part(5 * 2048, 0x55);
        std::vector<char> audio_part(10 * 2352, 0x11);
        bin_out.write(data_part.data(), static_cast<std::streamsize>(data_part.size()));
        bin_out.write(audio_part.data(), static_cast<std::streamsize>(audio_part.size()));
    }

    const std::string cue_content =
        "FILE \"mixed.bin\" BINARY\n"
        "  TRACK 01 MODE1/2048\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 01 00:00:05\n";

    const fs::path cue_path = dir.write_text_file("mixed.cue", cue_content);
    CueImageSource src(cue_path);

    const auto tracks = src.getTracks();
    ASSERT_EQ(tracks.size(), 2U);
    EXPECT_EQ(tracks[0].type, TrackType::Data);
    EXPECT_EQ(tracks[0].startLba, 0);
    EXPECT_EQ(tracks[0].endLba, 5);
    EXPECT_EQ(tracks[1].type, TrackType::Audio);
    EXPECT_EQ(tracks[1].startLba, 5);
    EXPECT_EQ(tracks[1].endLba, 15);

    // Reading from data track or crossing from data to audio must throw
    EXPECT_THROW(src.readAudioSectors(0, 1), std::runtime_error);
    EXPECT_THROW(src.readAudioSectors(4, 2), std::runtime_error);

    // Reading from audio track succeeds
    const auto audio = src.readAudioSectors(5, 2);
    EXPECT_EQ(audio.size(), 2U * AudioFormat::kBytesPerSector);
    EXPECT_EQ(std::to_integer<std::uint8_t>(audio[0]), 0x11U);
}

TEST(CueParserTest, RejectsMissingFilesMalformedInputAndUnsupportedConstructs) {
    TempTestDir dir("invalid_cases");
    dir.write_bin_sectors("valid.bin", 20);

    // 1. Missing binary file
    {
        const fs::path cue = dir.write_text_file(
            "missing_bin.cue",
            "FILE \"does_not_exist.bin\" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n");
        EXPECT_THROW(parse_cue_file(cue), std::runtime_error);
    }

    // 2. Unsupported FILE type (MP3 / WAVE)
    {
        const fs::path cue = dir.write_text_file(
            "mp3.cue",
            "FILE \"valid.bin\" MP3\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n");
        EXPECT_THROW(parse_cue_file(cue), std::runtime_error);
    }

    // 3. Unclosed quote
    {
        const fs::path cue = dir.write_text_file(
            "unclosed.cue",
            "TITLE \"Unclosed Album Title\nFILE \"valid.bin\" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n");
        EXPECT_THROW(parse_cue_file(cue), std::runtime_error);
    }

    // 4. TRACK before FILE
    {
        const fs::path cue = dir.write_text_file(
            "track_before_file.cue",
            "TRACK 01 AUDIO\n  INDEX 01 00:00:00\n");
        EXPECT_THROW(parse_cue_file(cue), std::runtime_error);
    }

    // 5. Missing INDEX 01
    {
        const fs::path cue = dir.write_text_file(
            "no_index01.cue",
            "FILE \"valid.bin\" BINARY\n  TRACK 01 AUDIO\n    INDEX 00 00:00:00\n");
        EXPECT_THROW(parse_cue_file(cue), std::runtime_error);
    }

    // 6. Both PREGAP and INDEX 00 in same track
    {
        const fs::path cue = dir.write_text_file(
            "pregap_and_idx00.cue",
            "FILE \"valid.bin\" BINARY\n  TRACK 01 AUDIO\n    PREGAP 00:00:02\n    INDEX 00 00:00:00\n    INDEX 01 00:00:02\n");
        EXPECT_THROW(parse_cue_file(cue), std::runtime_error);
    }

    // 7. INDEX offset exceeds image file size
    {
        const fs::path cue = dir.write_text_file(
            "out_of_bounds.cue",
            "FILE \"valid.bin\" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n  TRACK 02 AUDIO\n    INDEX 01 00:01:00\n");
        EXPECT_THROW(parse_cue_file(cue), std::runtime_error);
    }

    // 8. Invalid CATALOG and ISRC
    {
        const fs::path cue1 = dir.write_text_file(
            "bad_cat.cue",
            "CATALOG 12345\nFILE \"valid.bin\" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n");
        EXPECT_THROW(parse_cue_file(cue1), std::runtime_error);

        const fs::path cue2 = dir.write_text_file(
            "bad_isrc.cue",
            "FILE \"valid.bin\" BINARY\n  TRACK 01 AUDIO\n    ISRC SHORT\n    INDEX 01 00:00:00\n");
        EXPECT_THROW(parse_cue_file(cue2), std::runtime_error);
    }
}

TEST(CueTocConsistencyTest, MatchesReferenceTocAndCueLayoutAndAudioSectors) {
    const fs::path toc_path("/home/kgorelov/img/album.toc");
    const fs::path cue_path("/home/kgorelov/img/album.cue");

    if (!fs::exists(toc_path) || !fs::exists(cue_path)) {
        GTEST_SKIP() << "Reference images /home/kgorelov/img/album.{toc,cue} not present";
    }

    auto toc_src = open_image_source(toc_path.string());
    auto cue_src = open_image_source(cue_path.string());

    const auto toc_layout = toc_src->getDiscLayout();
    const auto cue_layout = cue_src->getDiscLayout();

    EXPECT_TRUE(compare_disc_inspections(toc_layout, cue_layout));

    ASSERT_EQ(toc_layout.tracks.size(), cue_layout.tracks.size());
    for (std::size_t i = 0; i < toc_layout.tracks.size(); ++i) {
        EXPECT_EQ(toc_layout.tracks[i].number, cue_layout.tracks[i].number);
        EXPECT_EQ(toc_layout.tracks[i].type, cue_layout.tracks[i].type);
        EXPECT_EQ(toc_layout.tracks[i].startLba, cue_layout.tracks[i].startLba);
        EXPECT_EQ(toc_layout.tracks[i].index01Lba, cue_layout.tracks[i].index01Lba);
        EXPECT_EQ(toc_layout.tracks[i].endLba, cue_layout.tracks[i].endLba);
        EXPECT_EQ(toc_layout.tracks[i].pregapLength, cue_layout.tracks[i].pregapLength);
        ASSERT_EQ(toc_layout.tracks[i].indexes.size(), cue_layout.tracks[i].indexes.size());
        for (std::size_t j = 0; j < toc_layout.tracks[i].indexes.size(); ++j) {
            EXPECT_EQ(toc_layout.tracks[i].indexes[j].number, cue_layout.tracks[i].indexes[j].number);
            EXPECT_EQ(toc_layout.tracks[i].indexes[j].lba, cue_layout.tracks[i].indexes[j].lba);
        }
    }

    // Verify byte-for-byte identical PCM sectors at Track 1, across Track 2->3 pregap boundary, and Track 12
    const auto toc_s100 = toc_src->readAudioSectors(100, 4);
    const auto cue_s100 = cue_src->readAudioSectors(100, 4);
    EXPECT_EQ(toc_s100, cue_s100);

    const auto toc_boundary = toc_src->readAudioSectors(50084, 12);
    const auto cue_boundary = cue_src->readAudioSectors(50084, 12);
    EXPECT_EQ(toc_boundary, cue_boundary);

    const auto toc_end = toc_src->readAudioSectors(240680, 8);
    const auto cue_end = cue_src->readAudioSectors(240680, 8);
    EXPECT_EQ(toc_end, cue_end);
}

} // namespace
} // namespace mycds
