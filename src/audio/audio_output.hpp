#pragma once

#include "audio/pcm_ring_buffer.hpp"
#include "domain/audio_format.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mycds {

struct AudioOutputConfig {
    std::size_t ringBufferSectors{150}; // 2 seconds of CD-DA audio (352,800 bytes)
    std::string preferredDriver{};      // Optional SDL audio driver hint (e.g. "dummy" in tests)
};

// Generates canonical S16LE 44.1 kHz stereo interleaved PCM containing a sine wave
// test signal for the specified number of CD-DA sectors (2352 bytes / 588 frames each).
std::vector<std::byte> generate_sine_wave_pcm(
    double frequency_hz,
    std::uint32_t sector_count,
    double amplitude = 0.25);

class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;
    AudioOutput(AudioOutput&&) noexcept;
    AudioOutput& operator=(AudioOutput&&) noexcept;

    void open(const AudioOutputConfig& config = {});
    void close() noexcept;

    bool isOpen() const noexcept;
    bool isPaused() const noexcept;

    void start();
    void resume();
    void pause();
    void stop();
    void clearBuffer();

    void setEndOfStream(bool eos) noexcept;
    bool isEndOfStream() const noexcept;

    // Enqueues S16LE 44.1 kHz stereo PCM into the lock-free ring buffer.
    // Returns the number of bytes actually written (0 .. pcm_data.size()).
    std::size_t enqueuePcm(std::span<const std::byte> pcm_data) noexcept;

    std::size_t ringBufferCapacity() const noexcept;
    std::size_t availableBytesToRead() const noexcept;
    std::size_t availableBytesToWrite() const noexcept;
    std::size_t bufferedBytes() const noexcept;

    std::uint64_t totalBytesConsumed() const noexcept;
    std::uint64_t underrunCount() const noexcept;

    std::string driverName() const;
    std::string deviceName() const;
    std::string lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Plays a synthesized sine wave test signal through AudioOutput independently of CD access.
bool play_test_tone(
    double frequency_hz,
    double duration_seconds,
    const std::string& driver_hint,
    std::string& out_error);

} // namespace mycds
