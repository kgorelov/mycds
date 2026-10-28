#include "domain/audio_format.hpp"
#include "domain/track_info.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

namespace mycds {
namespace {

TEST(AddressConversionTest, MsffDurationAndRelativeSectorRoundTrip) {
    EXPECT_EQ(sectors_to_msf(0), "00:00:00");
    EXPECT_EQ(sectors_to_msf(74), "00:00:74");
    EXPECT_EQ(sectors_to_msf(75), "00:01:00");
    EXPECT_EQ(sectors_to_msf(4500), "01:00:00");
    EXPECT_EQ(sectors_to_msf(21330), "04:44:30");
    EXPECT_EQ(sectors_to_msf(240688), "53:29:13");
    EXPECT_EQ(sectors_to_msf(-150), "-00:02:00");

    for (const std::int64_t s : {0, 1, 74, 75, 150, 4499, 4500, 21330, 50086, 50093, 240688}) {
        const std::string msf = sectors_to_msf(s);
        const auto parsed = parse_msf_to_sectors(msf);
        ASSERT_TRUE(parsed.has_value()) << "Failed to parse MSF: " << msf;
        EXPECT_EQ(*parsed, s);
    }
}

TEST(AddressConversionTest, RedBookPhysicalMsfToLbaNormalization) {
    // Red Book places LBA 0 at physical MSF 00:02:00 (150 frames)
    EXPECT_EQ(lba_to_msf_address(0), "00:02:00");
    EXPECT_EQ(lba_to_msf_address(-150), "00:00:00");
    EXPECT_EQ(lba_to_msf_address(21330), "04:46:30");

    EXPECT_EQ(msf_address_to_lba("00:02:00"), 0);
    EXPECT_EQ(msf_address_to_lba("00:00:00"), -150);
    EXPECT_EQ(msf_address_to_lba("04:46:30"), 21330);
    EXPECT_FALSE(msf_address_to_lba("invalid").has_value());
    EXPECT_FALSE(msf_address_to_lba("00:60:00").has_value());
    EXPECT_FALSE(msf_address_to_lba("00:00:75").has_value());
}

TEST(AddressConversionTest, Pcm16ByteOrderSwap) {
    std::array<std::byte, 4> samples{
        std::byte{0x12}, std::byte{0x34},
        std::byte{0xAB}, std::byte{0xCD}
    };
    swap_pcm16_byte_order(samples);
    EXPECT_EQ(samples[0], std::byte{0x34});
    EXPECT_EQ(samples[1], std::byte{0x12});
    EXPECT_EQ(samples[2], std::byte{0xCD});
    EXPECT_EQ(samples[3], std::byte{0xAB});

    std::array<std::byte, 3> odd_bytes{};
    EXPECT_THROW(swap_pcm16_byte_order(odd_bytes), std::invalid_argument);
}

TEST(AddressConversionTest, TrackAndIndexLookupFromAbsoluteLba) {
    std::vector<TrackInfo> tracks{
        TrackInfo{
            .number = 1,
            .type = TrackType::Audio,
            .startLba = 0,
            .endLba = 100,
            .index01Lba = 0,
            .pregapStartLba = std::nullopt,
            .pregapLength = 0,
            .pregapInFile = false,
            .postgapLength = 0,
            .indexes = {
                IndexInfo{.trackNumber = 1, .number = 1, .lba = 0},
                IndexInfo{.trackNumber = 1, .number = 2, .lba = 50},
            },
        },
        TrackInfo{
            .number = 2,
            .type = TrackType::Audio,
            .startLba = 100,
            .endLba = 250,
            .index01Lba = 110,
            .pregapStartLba = 100,
            .pregapLength = 10,
            .pregapInFile = true,
            .postgapLength = 0,
            .indexes = {
                IndexInfo{.trackNumber = 2, .number = 0, .lba = 100},
                IndexInfo{.trackNumber = 2, .number = 1, .lba = 110},
            },
        },
    };

    EXPECT_EQ(find_track_at_lba(tracks, -1), nullptr);
    EXPECT_EQ(find_track_at_lba(tracks, 250), nullptr);

    const TrackInfo* t1 = find_track_at_lba(tracks, 0);
    ASSERT_NE(t1, nullptr);
    EXPECT_EQ(t1->number, 1);
    ASSERT_NE(find_index_at_lba(*t1, 0), nullptr);
    EXPECT_EQ(find_index_at_lba(*t1, 0)->number, 1);
    EXPECT_EQ(find_index_at_lba(*t1, 49)->number, 1);
    EXPECT_EQ(find_index_at_lba(*t1, 50)->number, 2);
    EXPECT_EQ(find_index_at_lba(*t1, 99)->number, 2);
    EXPECT_EQ(find_index_at_lba(*t1, 100), nullptr);

    const TrackInfo* t2_pregap = find_track_at_lba(tracks, 105);
    ASSERT_NE(t2_pregap, nullptr);
    EXPECT_EQ(t2_pregap->number, 2);
    ASSERT_NE(find_index_at_lba(*t2_pregap, 105), nullptr);
    EXPECT_EQ(find_index_at_lba(*t2_pregap, 105)->number, 0);
    EXPECT_EQ(find_index_at_lba(*t2_pregap, 110)->number, 1);

    EXPECT_EQ(find_track_by_number(tracks, 2), &tracks[1]);
    EXPECT_EQ(find_track_by_number(tracks, 99), nullptr);
}

} // namespace
} // namespace mycds
