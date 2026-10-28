#include "audio/audio_output.hpp"
#include "audio/pcm_ring_buffer.hpp"
#include "domain/audio_format.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace mycds {
namespace {

TEST(PcmRingBufferTest, WrapAroundFrameAlignmentAndConsumedCounter) {
    // 4 stereo frames = 16 bytes capacity
    PcmRingBuffer ring(16);
    EXPECT_EQ(ring.capacity(), 16U);
    EXPECT_EQ(ring.availableToRead(), 0U);
    EXPECT_EQ(ring.availableToWrite(), 16U);

    std::vector<std::byte> in1(12);
    for (std::size_t i = 0; i < in1.size(); ++i) {
        in1[i] = static_cast<std::byte>(i + 1);
    }
    EXPECT_EQ(ring.write(in1), 12U);
    EXPECT_EQ(ring.availableToRead(), 12U);
    EXPECT_EQ(ring.availableToWrite(), 4U);

    // Read 8 bytes so read_pos advances to 8
    std::vector<std::byte> out1(8);
    EXPECT_EQ(ring.read(out1), 8U);
    EXPECT_EQ(ring.totalBytesConsumed(), 8ULL);
    for (std::size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(out1[i], static_cast<std::byte>(i + 1));
    }

    // Write 12 bytes (wraps around end of 16-byte storage)
    std::vector<std::byte> in2(12);
    for (std::size_t i = 0; i < in2.size(); ++i) {
        in2[i] = static_cast<std::byte>(100 + i);
    }
    EXPECT_EQ(ring.write(in2), 12U);
    EXPECT_EQ(ring.availableToRead(), 16U);
    EXPECT_EQ(ring.availableToWrite(), 0U);

    // Read all 16 bytes and verify order across wrap boundary
    std::vector<std::byte> out2(16);
    EXPECT_EQ(ring.read(out2), 16U);
    EXPECT_EQ(ring.totalBytesConsumed(), 24ULL);
    EXPECT_EQ(out2[0], std::byte{9});
    EXPECT_EQ(out2[1], std::byte{10});
    EXPECT_EQ(out2[2], std::byte{11});
    EXPECT_EQ(out2[3], std::byte{12});
    for (std::size_t i = 0; i < 12; ++i) {
        EXPECT_EQ(out2[4 + i], static_cast<std::byte>(100 + i));
    }
}

TEST(PcmRingBufferTest, LockFreeProducerConsumerThreadSafety) {
    constexpr std::size_t kTotalFrames = 588 * 100; // 100 sectors
    constexpr std::size_t kTotalBytes = kTotalFrames * AudioFormat::kBytesPerSampleFrame;

    PcmRingBuffer ring(10 * AudioFormat::kBytesPerSector);
    std::vector<std::byte> produced(kTotalBytes);
    for (std::size_t i = 0; i < kTotalBytes; ++i) {
        produced[i] = static_cast<std::byte>((i * 31U + 7U) & 0xFFU);
    }
    std::vector<std::byte> consumed(kTotalBytes, std::byte{0});

    std::thread producer([&]() {
        std::size_t offset = 0;
        while (offset < kTotalBytes) {
            const std::size_t chunk = std::min<std::size_t>(2352, kTotalBytes - offset);
            const std::size_t written = ring.write(
                std::span<const std::byte>(produced.data() + offset, chunk));
            offset += written;
            if (written == 0) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&]() {
        std::size_t offset = 0;
        while (offset < kTotalBytes) {
            const std::size_t chunk = std::min<std::size_t>(1024, kTotalBytes - offset);
            const std::size_t got = ring.read(
                std::span<std::byte>(consumed.data() + offset, chunk));
            offset += got;
            if (got == 0) {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(ring.totalBytesConsumed(), kTotalBytes);
    EXPECT_EQ(produced, consumed);
}

TEST(AudioOutputTest, GeneratesValidSineWavePcmAndStreamsThroughSdlAudioCallback) {
    const std::vector<std::byte> signal = generate_sine_wave_pcm(440.0, 20, 0.5);
    ASSERT_EQ(signal.size(), 20U * AudioFormat::kBytesPerSector);

    // Frame 0 is sin(0) = 0
    EXPECT_EQ(signal[0], std::byte{0});
    EXPECT_EQ(signal[1], std::byte{0});
    EXPECT_EQ(signal[2], std::byte{0});
    EXPECT_EQ(signal[3], std::byte{0});

    // Verify Left and Right stereo channels are identical S16LE samples and non-zero
    bool any_nonzero = false;
    for (std::size_t i = 0; i < signal.size(); i += 4) {
        EXPECT_EQ(signal[i], signal[i + 2]);
        EXPECT_EQ(signal[i + 1], signal[i + 3]);
        if (signal[i] != std::byte{0} || signal[i + 1] != std::byte{0}) {
            any_nonzero = true;
        }
    }
    EXPECT_TRUE(any_nonzero);

    AudioOutput output;
    output.open(AudioOutputConfig{.ringBufferSectors = 50});
    ASSERT_TRUE(output.isOpen());
    EXPECT_TRUE(output.isPaused());
    EXPECT_FALSE(output.driverName().empty());

    const std::size_t written = output.enqueuePcm(signal);
    EXPECT_EQ(written, signal.size());
    EXPECT_EQ(output.availableBytesToRead(), signal.size());

    output.start();
    EXPECT_FALSE(output.isPaused());

    // Wait up to 1000 ms for the SDL3 audio callback to consume data from the ring buffer
    const auto start_time = std::chrono::steady_clock::now();
    while (output.totalBytesConsumed() == 0 &&
           std::chrono::steady_clock::now() - start_time < std::chrono::milliseconds(1000)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EXPECT_GT(output.totalBytesConsumed(), 0ULL);

    output.pause();
    EXPECT_TRUE(output.isPaused());

    output.stop();
    EXPECT_EQ(output.availableBytesToRead(), 0U);
    output.close();
    EXPECT_FALSE(output.isOpen());
}

TEST(AudioOutputTest, ContinuousTestTonePlaybackIndependentlyOfCdAccess) {
    std::string error_msg;
    EXPECT_TRUE(play_test_tone(440.0, 0.15, "", error_msg)) << error_msg;
}

} // namespace
} // namespace mycds
