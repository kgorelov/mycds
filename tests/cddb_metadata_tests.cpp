#include "domain/audio_format.hpp"
#include "metadata/cddb_disc_id.hpp"
#include "metadata/cddb_provider.hpp"
#include "metadata/metadata_matcher.hpp"
#include "metadata/metadata_store.hpp"
#include "metadata/metadata_workflow.hpp"
#include "source/cue_parser.hpp"
#include "source/disc_source.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace mycds {
namespace {

namespace fs = std::filesystem;

DiscLayout make_ok_computer_layout() {
    const std::vector<std::int64_t> index01_lbas = {
        0, 21330, 50093, 70170, 90030, 112475,
        132098, 140898, 158195, 179585, 196725, 216198
    };
    const std::int64_t leadout_lba = 240688;

    DiscLayout layout{};
    layout.sourceKind = "Test Layout";
    layout.sourcePath = "/tmp/ok_computer.cue";
    layout.discInfo = DiscInfo{
        .firstTrack = 1,
        .lastTrack = 12,
        .startLba = 0,
        .endLba = leadout_lba,
        .sessions = 1,
        .rawStartSector = 0,
        .rawEndSector = leadout_lba,
    };

    for (std::size_t i = 0; i < index01_lbas.size(); ++i) {
        const std::int64_t start = index01_lbas[i];
        const std::int64_t end =
            (i + 1 < index01_lbas.size()) ? index01_lbas[i + 1] : leadout_lba;
        layout.tracks.push_back(TrackInfo{
            .number = static_cast<int>(i + 1),
            .type = TrackType::Audio,
            .startLba = start,
            .endLba = end,
            .index01Lba = start,
            .pregapStartLba = std::nullopt,
            .pregapLength = 0,
            .pregapInFile = false,
            .postgapLength = 0,
            .indexes = {IndexInfo{
                .trackNumber = static_cast<int>(i + 1),
                .number = 1,
                .lba = start,
                .explicitFromSource = true,
            }},
        });
        layout.trackMetadata.push_back(TrackMetadata{
            .trackNumber = static_cast<int>(i + 1),
            .isrc = "GBAYE970" + std::to_string(1000 + i),
        });
    }

    return layout;
}

class MockCddbHttpTransport final : public CddbHttpTransport {
public:
    std::string queryResponse{};
    std::string readResponse{};
    bool shouldThrowNetworkError{false};
    std::string networkErrorMessage{"Simulated DNS failure"};
    std::vector<std::string> requestedUrls{};

    std::string performGet(
        const std::string& url,
        long /*connect_timeout_ms*/,
        long /*request_timeout_ms*/) override {
        requestedUrls.push_back(url);
        if (shouldThrowNetworkError) {
            throw std::runtime_error("CDDB network request failed: " + networkErrorMessage);
        }
        if (url.find("cddb+query") != std::string::npos) {
            return queryResponse;
        }
        if (url.find("cddb+read") != std::string::npos) {
            return readResponse;
        }
        throw std::runtime_error("Unexpected URL in MockCddbHttpTransport: " + url);
    }
};

const char* kOkComputerXmcdResponse =
    "210 data a60c8990 CD database entry follows (until terminating `.')\r\n"
    "# xmcd\r\n"
    "#\r\n"
    "# Track frame offsets:\r\n"
    "#     150\r\n"
    "#     21480\r\n"
    "#     50243\r\n"
    "#     70320\r\n"
    "#     90180\r\n"
    "#     112625\r\n"
    "#     132248\r\n"
    "#     141048\r\n"
    "#     158345\r\n"
    "#     179735\r\n"
    "#     196875\r\n"
    "#     216348\r\n"
    "#\r\n"
    "# Disc length: 3211\r\n"
    "#\r\n"
    "DISCID=a60c890c\r\n"
    "DTITLE=Radiohead / OK Computer\r\n"
    "DYEAR=1997\r\n"
    "DGENRE=Alternative Rock\r\n"
    "TTITLE0=Airbag\r\n"
    "TTITLE1=Paranoid Android\r\n"
    "TTITLE2=Subterranean Homesick Alien\r\n"
    "TTITLE3=Exit Music (For A Film)\r\n"
    "TTITLE4=Let Down\r\n"
    "TTITLE5=Karma Police\r\n"
    "TTITLE6=Fitter Happier\r\n"
    "TTITLE7=Electioneering\r\n"
    "TTITLE8=Climbing Up The Walls\r\n"
    "TTITLE9=No Surprises\r\n"
    "TTITLE10=Lucky\r\n"
    "TTITLE11=The Tourist\r\n"
    "EXTD=YEAR: 1997\r\n"
    "PLAYORDER=\r\n"
    ".\r\n";

const char* kCollisionMudvayneXmcdResponse =
    "210 rock a60c890c CD database entry follows (until terminating `.')\r\n"
    "# xmcd\r\n"
    "# Track frame offsets:\r\n"
    "#     150\r\n"
    "#     12099\r\n"
    "#     27739\r\n"
    "#     44086\r\n"
    "#     70419\r\n"
    "#     87820\r\n"
    "#     108522\r\n"
    "#     144921\r\n"
    "#     161074\r\n"
    "#     176577\r\n"
    "#     190120\r\n"
    "#     217999\r\n"
    "# Disc length: 3211 seconds\r\n"
    "DISCID=a60c890c\r\n"
    "DTITLE=Mudvayne / Lost And Found\r\n"
    "DYEAR=2005\r\n"
    "DGENRE=Metal\r\n"
    "TTITLE0=Determined\r\n"
    "TTITLE1=Pushing Through\r\n"
    "TTITLE2=Happy?\r\n"
    "TTITLE3=IMN\r\n"
    "TTITLE4=Fall Into Sleep\r\n"
    "TTITLE5=Rain. Sun. Gone.\r\n"
    "TTITLE6=Choices\r\n"
    "TTITLE7=Forget To Remember\r\n"
    "TTITLE8=TV Radio\r\n"
    "TTITLE9=Just\r\n"
    "TTITLE10=All That You Are\r\n"
    "TTITLE11= Pulling The String\r\n"
    ".\r\n";

TEST(CddbDiscIdTest, ComputesExpectedDiscIdAndQueryCommandForOkComputer) {
    const DiscLayout layout = make_ok_computer_layout();
    const CddbDiscQueryInfo info =
        calculate_cddb_disc_id(layout.discInfo, layout.tracks);

    EXPECT_EQ(info.discIdNumeric, 0xa60c890cU);
    EXPECT_EQ(info.discIdHex, "a60c890c");
    EXPECT_EQ(info.trackCount, 12);
    EXPECT_EQ(info.totalLengthSeconds, 3211);
    ASSERT_EQ(info.trackFrameOffsets.size(), 12U);
    EXPECT_EQ(info.trackFrameOffsets.front(), 150);
    EXPECT_EQ(info.trackFrameOffsets[1], 21480);
    EXPECT_EQ(info.trackFrameOffsets.back(), 216348);

    const std::string cmd = format_cddb_query_command(info);
    EXPECT_EQ(
        cmd,
        "cddb query a60c890c 12 150 21480 50243 70320 90180 112625 "
        "132248 141048 158345 179735 196875 216348 3211");
}

TEST(CddbDiscIdTest, RejectsInvalidDiscLayouts) {
    DiscInfo empty_disc{.firstTrack = 1, .lastTrack = 0, .startLba = 0, .endLba = 1000};
    EXPECT_THROW(calculate_cddb_disc_id(empty_disc, {}), std::invalid_argument);

    DiscInfo bad_range{.firstTrack = 1, .lastTrack = 1, .startLba = 100, .endLba = 100};
    std::vector<TrackInfo> one_track{TrackInfo{.number = 1, .endLba = 100, .index01Lba = 0}};
    EXPECT_THROW(calculate_cddb_disc_id(bad_range, one_track), std::invalid_argument);
}

TEST(CddbProviderTest, HandlesZeroOneAndMultipleMatchesAndMalformedResponses) {
    const auto zero = parse_cddb_query_response("202 No match for disc ID a60c890c.\r\n");
    EXPECT_TRUE(zero.empty());

    const auto single = parse_cddb_query_response(
        "200 rock a60c890c Radiohead / OK Computer\r\n");
    ASSERT_EQ(single.size(), 1U);
    EXPECT_EQ(single[0].category, "rock");
    EXPECT_EQ(single[0].discId, "a60c890c");
    EXPECT_EQ(single[0].performer, "Radiohead");
    EXPECT_EQ(single[0].title, "OK Computer");
    EXPECT_TRUE(single[0].exactMatch);

    const auto multi = parse_cddb_query_response(
        "210 Found exact matches, list follows (until terminating `.')\r\n"
        "data a60c8990 Radiohead / OK Computer\r\n"
        "data a60c878c Radiohead / OK Computer\r\n"
        ".\r\n");
    ASSERT_EQ(multi.size(), 2U);
    EXPECT_EQ(multi[0].externalId(), "data/a60c8990");
    EXPECT_EQ(multi[1].externalId(), "data/a60c878c");
    EXPECT_TRUE(multi[0].exactMatch);

    const auto inexact = parse_cddb_query_response(
        "211 Found inexact matches, list follows (until terminating `.')\r\n"
        "rock 9d0c8888 Radiohead / OK Computer\r\n"
        ".\r\n");
    ASSERT_EQ(inexact.size(), 1U);
    EXPECT_FALSE(inexact[0].exactMatch);

    EXPECT_THROW(parse_cddb_query_response(""), std::runtime_error);
    EXPECT_THROW(parse_cddb_query_response("NOT_A_CODE invalid\r\n"), std::runtime_error);
    EXPECT_THROW(
        parse_cddb_query_response("500 Command syntax error\r\n"),
        std::runtime_error);
    EXPECT_THROW(
        parse_cddb_query_response("200 rock nothexid Title\r\n"),
        std::runtime_error);
}

TEST(CddbProviderTest, ParsesMultilineXmcdAndHandlesNetworkErrors) {
    const std::string multiline_xmcd =
        "210 jazz 12345678 CD database entry follows\r\n"
        "# xmcd\r\n"
        "# Track frame offsets:\r\n"
        "# 150\r\n"
        "# 15150\r\n"
        "# Disc length: 400 seconds\r\n"
        "DISCID=12345678\r\n"
        "DTITLE=Various /Long Compilation \r\n"
        "DTITLE=Title Part 2\r\n"
        "DYEAR=2001\r\n"
        "DGENRE=Electronic\r\n"
        "TTITLE0=Artist One / First Track Part A \r\n"
        "TTITLE0=Part B\r\n"
        "TTITLE1=Artist Two / Second Track\\tWith Tab\r\n"
        "EXTD=Line 1\\nLine 2\r\n"
        ".\r\n";

    const MetadataRecord rec = parse_xmcd_record(
        multiline_xmcd, "jazz", "12345678", "12345678", "TestProvider");
    EXPECT_EQ(rec.disc.performer, "Various");
    EXPECT_EQ(rec.disc.title, "Long Compilation Title Part 2");
    EXPECT_EQ(rec.disc.releaseYear, 2001);
    EXPECT_EQ(rec.disc.genre, "Electronic");
    EXPECT_EQ(rec.disc.externalRecordId, "jazz/12345678");
    EXPECT_EQ(rec.disc.comment, "Line 1\nLine 2");
    ASSERT_EQ(rec.remoteTrackFrameOffsets.size(), 2U);
    EXPECT_EQ(rec.remoteTrackFrameOffsets[0], 150);
    EXPECT_EQ(rec.remoteTrackFrameOffsets[1], 15150);
    EXPECT_EQ(rec.remoteDiscLengthSeconds, 400);
    ASSERT_EQ(rec.tracks.size(), 2U);
    EXPECT_EQ(rec.tracks[0].performer, "Artist One");
    EXPECT_EQ(rec.tracks[0].title, "First Track Part A Part B");
    EXPECT_EQ(rec.tracks[1].performer, "Artist Two");
    EXPECT_EQ(rec.tracks[1].title, "Second Track\tWith Tab");

    auto transport = std::make_shared<MockCddbHttpTransport>();
    transport->shouldThrowNetworkError = true;
    transport->networkErrorMessage = "Connection timed out after 5000 milliseconds";

    CddbCompatibleProvider provider(CddbClientConfig{}, transport);
    const DiscLayout layout = make_ok_computer_layout();
    EXPECT_THROW(
        provider.search(layout.discInfo, layout.tracks),
        std::runtime_error);
}

TEST(MetadataMatcherTest, ValidatesOffsetsDetectsDiscIdCollisionAndPreservesLocalLayout) {
    DiscLayout layout = make_ok_computer_layout();
    const DiscInfo original_disc_info = layout.discInfo;
    const std::vector<TrackInfo> original_tracks = layout.tracks;

    const MetadataRecord exact_rec = parse_xmcd_record(
        kOkComputerXmcdResponse, "data", "a60c8990", "a60c890c", "gnudb");
    const MetadataValidationResult exact_val =
        MetadataMatcher::validate(layout.discInfo, layout.tracks, exact_rec);
    EXPECT_TRUE(exact_val.valid);
    EXPECT_EQ(exact_val.quality, MatchQuality::ExactOffsetMatch);
    EXPECT_EQ(exact_val.maxFrameOffsetDelta, 0);
    EXPECT_EQ(exact_val.discLengthSecondsDelta, 0);

    const MetadataRecord collision_rec = parse_xmcd_record(
        kCollisionMudvayneXmcdResponse, "rock", "a60c890c", "a60c890c", "gnudb");
    const MetadataValidationResult collision_val =
        MetadataMatcher::validate(layout.discInfo, layout.tracks, collision_rec);
    EXPECT_TRUE(collision_val.valid);
    EXPECT_EQ(collision_val.quality, MatchQuality::OffsetMismatch);
    EXPECT_GT(collision_val.maxFrameOffsetDelta, 15000);
    EXPECT_FALSE(collision_val.warnings.empty());

    MetadataRecord count_mismatch_rec = exact_rec;
    count_mismatch_rec.tracks.pop_back();
    const MetadataValidationResult bad_count_val =
        MetadataMatcher::validate(layout.discInfo, layout.tracks, count_mismatch_rec);
    EXPECT_FALSE(bad_count_val.valid);
    EXPECT_EQ(bad_count_val.quality, MatchQuality::LayoutMismatch);
    EXPECT_FALSE(bad_count_val.trackCountMatches);

    MetadataMatcher::applyToLayout(layout, exact_rec);
    EXPECT_EQ(layout.discMetadata.performer, "Radiohead");
    EXPECT_EQ(layout.discMetadata.title, "OK Computer");
    ASSERT_EQ(layout.trackMetadata.size(), 12U);
    EXPECT_EQ(layout.trackMetadata[0].title, "Airbag");
    // Local ISRC from disc/image must be preserved when remote CDDB has no ISRC
    EXPECT_EQ(layout.trackMetadata[0].isrc, "GBAYE9701000");

    // Local disc layout and track boundaries must never be modified
    EXPECT_EQ(layout.discInfo.startLba, original_disc_info.startLba);
    EXPECT_EQ(layout.discInfo.endLba, original_disc_info.endLba);
    ASSERT_EQ(layout.tracks.size(), original_tracks.size());
    for (std::size_t i = 0; i < original_tracks.size(); ++i) {
        EXPECT_EQ(layout.tracks[i].startLba, original_tracks[i].startLba);
        EXPECT_EQ(layout.tracks[i].index01Lba, original_tracks[i].index01Lba);
        EXPECT_EQ(layout.tracks[i].endLba, original_tracks[i].endLba);
    }
}

TEST(MetadataStoreAndWorkflowTest, InteractiveLookupEditSidecarPersistenceAndSafeCueExport) {
    const fs::path temp_dir = fs::temp_directory_path() / "mycds_phase2_2_test";
    std::error_code ec;
    fs::remove_all(temp_dir, ec);
    fs::create_directories(temp_dir, ec);

    const fs::path sidecar_path = temp_dir / "test_album.mycds.json";
    const fs::path cue_path = temp_dir / "test_album.cue";
    const fs::path bin_path = temp_dir / "test_album.bin";

    {
        std::ofstream bin_out(bin_path, std::ios::binary);
        const std::vector<char> dummy_sectors(300 * 2352, 0);
        bin_out.write(dummy_sectors.data(), static_cast<std::streamsize>(dummy_sectors.size()));
    }

    const std::string original_cue =
        "FILE \"test_album.bin\" BINARY\n"
        "  TRACK 01 AUDIO\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n"
        "    INDEX 01 00:00:25\n"
        "  TRACK 03 AUDIO\n"
        "    INDEX 00 00:00:50\n"
        "    INDEX 01 00:00:57\n"
        "  TRACK 04 AUDIO\n"
        "    INDEX 01 00:01:00\n"
        "  TRACK 05 AUDIO\n"
        "    INDEX 01 00:01:25\n"
        "  TRACK 06 AUDIO\n"
        "    INDEX 01 00:01:50\n"
        "  TRACK 07 AUDIO\n"
        "    INDEX 01 00:02:00\n"
        "  TRACK 08 AUDIO\n"
        "    INDEX 01 00:02:25\n"
        "  TRACK 09 AUDIO\n"
        "    INDEX 01 00:02:50\n"
        "  TRACK 10 AUDIO\n"
        "    INDEX 01 00:03:00\n"
        "  TRACK 11 AUDIO\n"
        "    INDEX 01 00:03:25\n"
        "  TRACK 12 AUDIO\n"
        "    INDEX 01 00:03:50\n";

    {
        std::ofstream out(cue_path);
        out << original_cue;
    }

    auto transport = std::make_shared<MockCddbHttpTransport>();
    transport->queryResponse =
        "210 Found exact matches, list follows (until terminating `.')\r\n"
        "rock a60c890c Mudvayne / Lost And Found\r\n"
        "data a60c8990 Radiohead / OK Computer\r\n"
        ".\r\n";
    transport->readResponse = kOkComputerXmcdResponse;

    CddbCompatibleProvider provider(CddbClientConfig{}, transport);
    DiscLayout layout = make_ok_computer_layout();
    layout.sourcePath = cue_path.string();

    // Simulate user choosing candidate #2 ("data a60c8990 Radiohead / OK Computer")
    std::istringstream lookup_in("2\n");
    std::ostringstream lookup_out;
    ASSERT_TRUE(run_interactive_metadata_lookup(
        layout, provider, lookup_in, lookup_out, sidecar_path, std::nullopt, true));

    EXPECT_EQ(layout.discMetadata.performer, "Radiohead");
    EXPECT_EQ(layout.discMetadata.title, "OK Computer");
    EXPECT_EQ(layout.discMetadata.externalRecordId, "data/a60c8990");
    ASSERT_TRUE(MetadataStore::exists(sidecar_path));

    // Simulate user editing album genre and Track 1 title, then saving
    std::istringstream edit_in(
        "\n"                  // keep Performer
        "OK Computer (1997)\n"// update Album Title
        "Art Rock\n"          // update Genre
        "\n"                  // keep Year
        "Verified UK pressing\n" // User Notes
        "Airbag (Remastered)\n"  // Track 1 Title
    );
    std::ostringstream edit_out;
    run_interactive_metadata_edit(layout, edit_in, edit_out);
    EXPECT_TRUE(layout.discMetadata.userCorrected);
    EXPECT_EQ(layout.discMetadata.title, "OK Computer (1997)");
    EXPECT_EQ(layout.discMetadata.genre, "Art Rock");
    EXPECT_EQ(layout.discMetadata.userNotes, "Verified UK pressing");
    EXPECT_EQ(layout.trackMetadata[0].title, "Airbag (Remastered)");

    MetadataStore::saveSidecar(sidecar_path, extract_metadata_record(layout));

    // Reload from sidecar without network access
    const MetadataRecord reloaded = MetadataStore::loadSidecar(sidecar_path);
    EXPECT_EQ(reloaded.disc.performer, "Radiohead");
    EXPECT_EQ(reloaded.disc.title, "OK Computer (1997)");
    EXPECT_EQ(reloaded.disc.genre, "Art Rock");
    EXPECT_EQ(reloaded.disc.externalRecordId, "data/a60c8990");
    EXPECT_TRUE(reloaded.disc.userCorrected);
    EXPECT_EQ(reloaded.disc.userNotes, "Verified UK pressing");
    ASSERT_EQ(reloaded.tracks.size(), 12U);
    EXPECT_EQ(reloaded.tracks[0].title, "Airbag (Remastered)");
    EXPECT_EQ(reloaded.tracks[1].title, "Paranoid Android");

    const ParsedCueSheet parsed_before = parse_cue_file(cue_path);

    // Export to CUE and verify layout directives are untouched while metadata is present
    MetadataStore::exportToCue(cue_path, reloaded);
    const ParsedCueSheet parsed_after = parse_cue_file(cue_path);
    EXPECT_EQ(parsed_after.layout.discMetadata.performer, "Radiohead");
    EXPECT_EQ(parsed_after.layout.discMetadata.title, "OK Computer (1997)");
    EXPECT_EQ(parsed_after.layout.discMetadata.genre, "Art Rock");
    EXPECT_EQ(parsed_after.layout.discMetadata.releaseYear, 1997);
    ASSERT_EQ(parsed_after.layout.tracks.size(), parsed_before.layout.tracks.size());
    ASSERT_EQ(parsed_after.layout.trackMetadata.size(), 12U);
    EXPECT_EQ(parsed_after.layout.trackMetadata[0].title, "Airbag (Remastered)");
    EXPECT_EQ(parsed_after.layout.trackMetadata[1].title, "Paranoid Android");
    // Verify Track 3's INDEX 00 and INDEX 01 offsets were preserved verbatim
    ASSERT_EQ(parsed_after.layout.tracks[2].indexes.size(), 2U);
    EXPECT_EQ(parsed_after.layout.tracks[2].indexes[0].number, 0);
    EXPECT_EQ(
        parsed_after.layout.tracks[2].indexes[0].lba,
        parsed_before.layout.tracks[2].indexes[0].lba);
    EXPECT_EQ(parsed_after.layout.tracks[2].indexes[1].number, 1);
    EXPECT_EQ(
        parsed_after.layout.tracks[2].indexes[1].lba,
        parsed_before.layout.tracks[2].indexes[1].lba);

    fs::remove_all(temp_dir, ec);
}

} // namespace
} // namespace mycds
