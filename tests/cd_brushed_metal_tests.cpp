#include <gtest/gtest.h>

#include "gui/cd_brushed_metal.h"

using namespace mycds::gui;

TEST(CdBrushedMetalTests, EmptyDimensionsReturnsEmpty) {
    auto p1 = CdBrushedMetal::GeneratePixels(0, 100);
    EXPECT_TRUE(p1.empty());

    auto p2 = CdBrushedMetal::GeneratePixels(100, 0);
    EXPECT_TRUE(p2.empty());

    auto p3 = CdBrushedMetal::GeneratePixels(-10, -10);
    EXPECT_TRUE(p3.empty());
}

TEST(CdBrushedMetalTests, GeneratesExpectedDimensionsAndAlpha) {
    constexpr int W = 128;
    constexpr int H = 64;
    const auto pixels = CdBrushedMetal::GeneratePixels(W, H);

    ASSERT_EQ(pixels.size(), static_cast<size_t>(W * H));

    for (const auto& px : pixels) {
        EXPECT_EQ(px.a, 255);
    }
}

TEST(CdBrushedMetalTests, ColorRangeAndCoolDarkMetalTone) {
    constexpr int W = 256;
    constexpr int H = 128;
    const auto pixels = CdBrushedMetal::GeneratePixels(W, H);

    ASSERT_FALSE(pixels.empty());

    double sum_r = 0.0;
    double sum_g = 0.0;
    double sum_b = 0.0;
    uint8_t min_v = 255;
    uint8_t max_v = 0;

    for (const auto& px : pixels) {
        sum_r += px.r;
        sum_g += px.g;
        sum_b += px.b;
        min_v = std::min(min_v, px.g);
        max_v = std::max(max_v, px.g);
    }

    const double mean_r = sum_r / pixels.size();
    const double mean_g = sum_g / pixels.size();
    const double mean_b = sum_b / pixels.size();

    // Dark brushed metal mean luminance should be in dark charcoal range (~20 to 35)
    EXPECT_GT(mean_g, 18.0);
    EXPECT_LT(mean_g, 38.0);

    // Realistic variation range
    EXPECT_GE(min_v, 5);
    EXPECT_LE(max_v, 95);
    EXPECT_GT(max_v - min_v, 15); // Clear grain contrast

    // Cool tone of anodized dark metal: blue channel subtly higher than red
    EXPECT_GT(mean_b, mean_r);
}

TEST(CdBrushedMetalTests, DeterministicOutputWithSameSeed) {
    constexpr int W = 64;
    constexpr int H = 32;

    const auto run1 = CdBrushedMetal::GeneratePixels(W, H, 0x12345678);
    const auto run2 = CdBrushedMetal::GeneratePixels(W, H, 0x12345678);
    const auto diff_seed = CdBrushedMetal::GeneratePixels(W, H, 0x87654321);

    ASSERT_EQ(run1.size(), run2.size());
    for (size_t i = 0; i < run1.size(); ++i) {
        EXPECT_EQ(run1[i].r, run2[i].r);
        EXPECT_EQ(run1[i].g, run2[i].g);
        EXPECT_EQ(run1[i].b, run2[i].b);
        EXPECT_EQ(run1[i].a, run2[i].a);
    }

    bool has_difference = false;
    for (size_t i = 0; i < run1.size(); ++i) {
        if (run1[i].r != diff_seed[i].r || run1[i].g != diff_seed[i].g) {
            has_difference = true;
            break;
        }
    }
    EXPECT_TRUE(has_difference);
}

TEST(CdBrushedMetalTests, NullDrawListSafety) {
    CdBrushedMetal metal;
    // Calling render methods with nullptr draw_list must safely no-op without crashing
    metal.RenderBackground(nullptr, ImVec2(0, 0), ImVec2(100, 100));
    metal.RenderPanelChassis(nullptr, ImVec2(0, 0), ImVec2(100, 100));
    metal.RenderChassisBolt(nullptr, ImVec2(50, 50), 7.0f);
}

TEST(CdBrushedMetalTests, SymmetricTileBoundaries) {
    constexpr int size = 128;
    const auto pixels = CdBrushedMetal::GeneratePixels(size, size);

    ASSERT_EQ(pixels.size(), static_cast<size_t>(size * size));

    for (int y = 0; y < size; ++y) {
        // Outer boundary seamless wrap: left edge == right edge
        EXPECT_EQ(pixels[y * size + 0].r, pixels[y * size + (size - 1)].r);
        EXPECT_EQ(pixels[y * size + 0].g, pixels[y * size + (size - 1)].g);
        EXPECT_EQ(pixels[y * size + 0].b, pixels[y * size + (size - 1)].b);

        // Center mirror seam: row center is seamless
        EXPECT_EQ(pixels[y * size + 63].r, pixels[y * size + 64].r);
        EXPECT_EQ(pixels[y * size + 63].g, pixels[y * size + 64].g);
        EXPECT_EQ(pixels[y * size + 63].b, pixels[y * size + 64].b);
    }

    for (int x = 0; x < size; ++x) {
        // Outer boundary seamless wrap: top edge == bottom edge
        EXPECT_EQ(pixels[0 * size + x].r, pixels[(size - 1) * size + x].r);
        EXPECT_EQ(pixels[0 * size + x].g, pixels[(size - 1) * size + x].g);
        EXPECT_EQ(pixels[0 * size + x].b, pixels[(size - 1) * size + x].b);

        // Center mirror seam: col center is seamless
        EXPECT_EQ(pixels[63 * size + x].r, pixels[64 * size + x].r);
        EXPECT_EQ(pixels[63 * size + x].g, pixels[64 * size + x].g);
        EXPECT_EQ(pixels[63 * size + x].b, pixels[64 * size + x].b);
    }
}

TEST(CdBrushedMetalTests, InitializeDefaultTileAndEnsureCapacity) {
    CdBrushedMetal metal;
    EXPECT_FALSE(metal.EnsureCapacity(0, 100));
    EXPECT_FALSE(metal.EnsureCapacity(100, 0));
    EXPECT_FALSE(metal.EnsureCapacity(-1, -1));

    EXPECT_TRUE(metal.Initialize(CdBrushedMetal::kDefaultTileSize));
    EXPECT_EQ(metal.GetWidth(), CdBrushedMetal::kDefaultTileSize);
    EXPECT_EQ(metal.GetHeight(), CdBrushedMetal::kDefaultTileSize);
    EXPECT_TRUE(metal.IsInitialized());

    // With static tile, EnsureCapacity returns true immediately with zero re-allocations
    EXPECT_TRUE(metal.EnsureCapacity(1920, 1080));
    EXPECT_EQ(metal.GetWidth(), CdBrushedMetal::kDefaultTileSize);
    EXPECT_EQ(metal.GetHeight(), CdBrushedMetal::kDefaultTileSize);
}
