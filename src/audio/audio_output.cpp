#include "audio/audio_output.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <numbers>
#include <stdexcept>
#include <thread>

#include <SDL3/SDL.h>

namespace mycds {

std::vector<std::byte> generate_sine_wave_pcm(
    double frequency_hz,
    std::uint32_t sector_count,
    double amplitude) {
    const double clamped_amp = std::clamp(amplitude, 0.0, 1.0);
    const std::size_t total_frames =
        static_cast<std::size_t>(sector_count) * AudioFormat::kFramesPerSector;
    std::vector<std::byte> pcm(total_frames * AudioFormat::kBytesPerSampleFrame);

    const double phase_inc =
        (2.0 * std::numbers::pi * frequency_hz) /
        static_cast<double>(AudioFormat::kSampleRate);

    for (std::size_t frame = 0; frame < total_frames; ++frame) {
        const double sample_val =
            std::sin(static_cast<double>(frame) * phase_inc) * clamped_amp;
        const auto s16 = static_cast<std::int16_t>(std::lrint(sample_val * 32767.0));
        const auto u16 = static_cast<std::uint16_t>(s16);

        // Explicit little-endian S16LE stereo (Left = s16, Right = s16)
        const std::size_t byte_idx = frame * AudioFormat::kBytesPerSampleFrame;
        pcm[byte_idx + 0] = static_cast<std::byte>(u16 & 0xFFU);
        pcm[byte_idx + 1] = static_cast<std::byte>((u16 >> 8U) & 0xFFU);
        pcm[byte_idx + 2] = static_cast<std::byte>(u16 & 0xFFU);
        pcm[byte_idx + 3] = static_cast<std::byte>((u16 >> 8U) & 0xFFU);
    }

    return pcm;
}

struct AudioOutput::Impl {
    PcmRingBuffer ringBuffer{150U * AudioFormat::kBytesPerSector};
    SDL_AudioStream* stream{nullptr};
    bool sdlAudioInitialized{false};
    std::atomic<bool> paused{true};
    std::atomic<bool> endOfStream{false};
    std::atomic<bool> streamError{false};
    std::string activeDriver{};
    std::string activeDevice{};
    std::string errorMessage{};

    static void SDLCALL audioStreamCallback(
        void* userdata,
        SDL_AudioStream* sdl_stream,
        int additional_amount,
        int /*total_amount*/) noexcept {
        if (!userdata || !sdl_stream || additional_amount <= 0) {
            return;
        }
        auto* self = static_cast<Impl*>(userdata);
        if (self->paused.load(std::memory_order_acquire)) {
            return;
        }

        std::array<std::byte, 4096> chunk{};
        std::size_t remaining = static_cast<std::size_t>(additional_amount);
        bool recorded_underrun_this_callback = false;

        while (remaining >= AudioFormat::kBytesPerSampleFrame) {
            const std::size_t req = std::min(remaining, chunk.size());
            const std::size_t got =
                self->ringBuffer.read(std::span<std::byte>(chunk.data(), req));
            if (got > 0) {
                if (!SDL_PutAudioStreamData(sdl_stream, chunk.data(), static_cast<int>(got))) {
                    self->streamError.store(true, std::memory_order_relaxed);
                    return;
                }
                remaining -= got;
            }

            if (got < req) {
                if (!self->endOfStream.load(std::memory_order_acquire)) {
                    if (!recorded_underrun_this_callback) {
                        self->ringBuffer.recordUnderrun();
                        recorded_underrun_this_callback = true;
                    }
                    // Pad remaining requested bytes with silence so device never glitches
                    const std::size_t silence_bytes =
                        std::min(remaining, chunk.size());
                    std::memset(chunk.data(), 0, silence_bytes);
                    if (!SDL_PutAudioStreamData(
                            sdl_stream,
                            chunk.data(),
                            static_cast<int>(silence_bytes))) {
                        self->streamError.store(true, std::memory_order_relaxed);
                        return;
                    }
                    remaining -= silence_bytes;
                } else {
                    SDL_FlushAudioStream(sdl_stream);
                    break;
                }
            }
        }
    }

    ~Impl() {
        close();
    }

    void close() noexcept {
        paused.store(true, std::memory_order_release);
        if (stream) {
            SDL_PauseAudioStreamDevice(stream);
            SDL_DestroyAudioStream(stream);
            stream = nullptr;
        }
        if (sdlAudioInitialized) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            sdlAudioInitialized = false;
        }
        ringBuffer.clear();
    }
};

AudioOutput::AudioOutput() : impl_(std::make_unique<Impl>()) {}
AudioOutput::~AudioOutput() = default;
AudioOutput::AudioOutput(AudioOutput&&) noexcept = default;
AudioOutput& AudioOutput::operator=(AudioOutput&&) noexcept = default;

void AudioOutput::open(const AudioOutputConfig& config) {
    close();

    const std::size_t cap_sectors =
        config.ringBufferSectors > 0 ? config.ringBufferSectors : 150U;
    impl_->ringBuffer.resize(cap_sectors * AudioFormat::kBytesPerSector);
    impl_->ringBuffer.resetUnderrunCount();
    impl_->endOfStream.store(false, std::memory_order_release);
    impl_->streamError.store(false, std::memory_order_release);
    impl_->errorMessage.clear();

    // Prevent SDL3's implicit event subsystem from hijacking SIGINT and SIGTERM
    // in console applications that do not pump SDL_EVENT_QUIT.
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

    if (!config.preferredDriver.empty()) {
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, config.preferredDriver.c_str());
    } else {
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, nullptr);
    }

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        impl_->errorMessage =
            std::string("SDL_InitSubSystem(SDL_INIT_AUDIO) failed: ") + SDL_GetError();
        throw std::runtime_error(impl_->errorMessage);
    }
    impl_->sdlAudioInitialized = true;

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = static_cast<int>(AudioFormat::kChannels);
    spec.freq = static_cast<int>(AudioFormat::kSampleRate);

    impl_->stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &spec,
        &Impl::audioStreamCallback,
        impl_.get());
    if (!impl_->stream) {
        impl_->errorMessage =
            std::string("SDL_OpenAudioDeviceStream failed: ") + SDL_GetError();
        impl_->close();
        throw std::runtime_error(impl_->errorMessage);
    }

    const char* drv = SDL_GetCurrentAudioDriver();
    impl_->activeDriver = drv ? drv : "unknown";

    const SDL_AudioDeviceID dev_id = SDL_GetAudioStreamDevice(impl_->stream);
    const char* dev_name = SDL_GetAudioDeviceName(dev_id);
    impl_->activeDevice = dev_name ? dev_name : "default";

    impl_->paused.store(true, std::memory_order_release);
}

void AudioOutput::close() noexcept {
    if (impl_) {
        impl_->close();
    }
}

bool AudioOutput::isOpen() const noexcept {
    return impl_ && impl_->stream != nullptr;
}

bool AudioOutput::isPaused() const noexcept {
    if (!isOpen()) {
        return true;
    }
    return impl_->paused.load(std::memory_order_acquire);
}

void AudioOutput::start() {
    resume();
}

void AudioOutput::resume() {
    if (!isOpen()) {
        throw std::runtime_error("Cannot resume AudioOutput: device is not open");
    }
    impl_->paused.store(false, std::memory_order_release);
    if (!SDL_ResumeAudioStreamDevice(impl_->stream)) {
        impl_->errorMessage =
            std::string("SDL_ResumeAudioStreamDevice failed: ") + SDL_GetError();
        throw std::runtime_error(impl_->errorMessage);
    }
}

void AudioOutput::pause() {
    if (!isOpen()) {
        return;
    }
    impl_->paused.store(true, std::memory_order_release);
    if (!SDL_PauseAudioStreamDevice(impl_->stream)) {
        impl_->errorMessage =
            std::string("SDL_PauseAudioStreamDevice failed: ") + SDL_GetError();
        throw std::runtime_error(impl_->errorMessage);
    }
}

void AudioOutput::stop() {
    if (!isOpen()) {
        return;
    }
    impl_->paused.store(true, std::memory_order_release);
    SDL_PauseAudioStreamDevice(impl_->stream);
    SDL_LockAudioStream(impl_->stream);
    SDL_ClearAudioStream(impl_->stream);
    impl_->ringBuffer.clear();
    impl_->endOfStream.store(false, std::memory_order_release);
    SDL_UnlockAudioStream(impl_->stream);
}

void AudioOutput::clearBuffer() {
    if (!impl_) {
        return;
    }
    if (impl_->stream) {
        SDL_LockAudioStream(impl_->stream);
        SDL_ClearAudioStream(impl_->stream);
        impl_->ringBuffer.clear();
        SDL_UnlockAudioStream(impl_->stream);
    } else {
        impl_->ringBuffer.clear();
    }
}

void AudioOutput::setEndOfStream(bool eos) noexcept {
    if (impl_) {
        impl_->endOfStream.store(eos, std::memory_order_release);
        if (eos && impl_->stream) {
            SDL_FlushAudioStream(impl_->stream);
        }
    }
}

bool AudioOutput::isEndOfStream() const noexcept {
    return impl_ && impl_->endOfStream.load(std::memory_order_acquire);
}

std::size_t AudioOutput::enqueuePcm(std::span<const std::byte> pcm_data) noexcept {
    if (!impl_) {
        return 0;
    }
    return impl_->ringBuffer.write(pcm_data);
}

std::size_t AudioOutput::ringBufferCapacity() const noexcept {
    return impl_ ? impl_->ringBuffer.capacity() : 0U;
}

std::size_t AudioOutput::availableBytesToRead() const noexcept {
    return impl_ ? impl_->ringBuffer.availableToRead() : 0U;
}

std::size_t AudioOutput::availableBytesToWrite() const noexcept {
    return impl_ ? impl_->ringBuffer.availableToWrite() : 0U;
}

std::size_t AudioOutput::bufferedBytes() const noexcept {
    if (!impl_) {
        return 0U;
    }
    std::size_t total = impl_->ringBuffer.availableToRead();
    if (impl_->stream) {
        const int sdl_avail = SDL_GetAudioStreamAvailable(impl_->stream);
        if (sdl_avail > 0) {
            total += static_cast<std::size_t>(sdl_avail);
        }
    }
    return total;
}

std::uint64_t AudioOutput::totalBytesConsumed() const noexcept {
    return impl_ ? impl_->ringBuffer.totalBytesConsumed() : 0ULL;
}

std::uint64_t AudioOutput::underrunCount() const noexcept {
    return impl_ ? impl_->ringBuffer.underrunCount() : 0ULL;
}

std::string AudioOutput::driverName() const {
    return impl_ ? impl_->activeDriver : "";
}

std::string AudioOutput::deviceName() const {
    return impl_ ? impl_->activeDevice : "";
}

bool AudioOutput::hasError() const noexcept {
    if (!impl_) {
        return false;
    }
    return impl_->streamError.load(std::memory_order_acquire) || !impl_->errorMessage.empty();
}

std::string AudioOutput::lastError() const {
    if (!impl_) {
        return {};
    }
    if (impl_->streamError.load(std::memory_order_acquire) && impl_->errorMessage.empty()) {
        return "SDL audio stream callback encountered an error";
    }
    return impl_->errorMessage;
}

bool play_test_tone(
    double frequency_hz,
    double duration_seconds,
    const std::string& driver_hint,
    std::string& out_error) {
    try {
        if (duration_seconds <= 0.0) {
            out_error = "Duration must be positive";
            return false;
        }
        const auto total_sectors = static_cast<std::uint32_t>(std::max(
            1L,
            std::lrint(duration_seconds * static_cast<double>(AudioFormat::kSectorsPerSecond))));
        const std::vector<std::byte> tone_pcm =
            generate_sine_wave_pcm(frequency_hz, total_sectors, 0.25);

        AudioOutput output;
        output.open(AudioOutputConfig{
            .ringBufferSectors = 150,
            .preferredDriver = driver_hint,
        });

        std::size_t offset = 0;
        bool started = false;

        while (offset < tone_pcm.size()) {
            const std::size_t written = output.enqueuePcm(
                std::span<const std::byte>(tone_pcm.data() + offset, tone_pcm.size() - offset));
            offset += written;

            if (!started &&
                (offset == tone_pcm.size() ||
                 output.availableBytesToWrite() < AudioFormat::kBytesPerSector)) {
                output.start();
                started = true;
            }

            if (offset < tone_pcm.size() && written == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }

        output.setEndOfStream(true);
        if (!started) {
            output.start();
        }

        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds(static_cast<long>(duration_seconds * 1000.0) + 2000);
        while (output.availableBytesToRead() > 0 &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        output.stop();
        output.close();
        return true;
    } catch (const std::exception& ex) {
        out_error = ex.what();
        return false;
    }
}

} // namespace mycds
