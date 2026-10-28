#pragma once

#include "domain/audio_format.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

namespace mycds {

// Lock-free Single-Producer / Single-Consumer (SPSC) bounded byte ring buffer
// for 16-bit stereo interleaved PCM audio frames (4 bytes per sample frame).
//
// Producer thread calls write().
// Real-time SDL3 audio callback calls read() without locks or allocations.
class PcmRingBuffer {
public:
    explicit PcmRingBuffer(std::size_t capacity_bytes = 150U * AudioFormat::kBytesPerSector) {
        resize(capacity_bytes);
    }

    void resize(std::size_t capacity_bytes) {
        const std::size_t frame_aligned =
            (capacity_bytes / AudioFormat::kBytesPerSampleFrame) *
            AudioFormat::kBytesPerSampleFrame;
        if (frame_aligned == 0) {
            throw std::invalid_argument("PcmRingBuffer capacity must be at least one sample frame (4 bytes)");
        }
        capacity_ = frame_aligned;
        storage_.assign(capacity_, std::byte{0});
        clear();
    }

    std::size_t capacity() const noexcept {
        return capacity_;
    }

    std::size_t availableToRead() const noexcept {
        const std::size_t w = write_pos_.load(std::memory_order_acquire);
        const std::size_t r = read_pos_.load(std::memory_order_acquire);
        return w - r;
    }

    std::size_t availableToWrite() const noexcept {
        const std::size_t used = availableToRead();
        return (used <= capacity_) ? (capacity_ - used) : 0U;
    }

    std::size_t write(std::span<const std::byte> input) noexcept {
        const std::size_t w = write_pos_.load(std::memory_order_relaxed);
        const std::size_t r = read_pos_.load(std::memory_order_acquire);
        const std::size_t free_bytes = capacity_ - (w - r);

        const std::size_t frame_aligned_request =
            (input.size() / AudioFormat::kBytesPerSampleFrame) *
            AudioFormat::kBytesPerSampleFrame;
        const std::size_t to_write = std::min(frame_aligned_request, free_bytes);
        if (to_write == 0) {
            return 0;
        }

        const std::size_t offset = w % capacity_;
        const std::size_t first_chunk = std::min(to_write, capacity_ - offset);
        std::memcpy(storage_.data() + offset, input.data(), first_chunk);
        if (to_write > first_chunk) {
            std::memcpy(
                storage_.data(),
                input.data() + first_chunk,
                to_write - first_chunk);
        }

        write_pos_.store(w + to_write, std::memory_order_release);
        return to_write;
    }

    std::size_t read(std::span<std::byte> output) noexcept {
        const std::size_t r = read_pos_.load(std::memory_order_relaxed);
        const std::size_t w = write_pos_.load(std::memory_order_acquire);
        const std::size_t avail = w - r;

        const std::size_t frame_aligned_request =
            (output.size() / AudioFormat::kBytesPerSampleFrame) *
            AudioFormat::kBytesPerSampleFrame;
        const std::size_t to_read = std::min(frame_aligned_request, avail);
        if (to_read == 0) {
            return 0;
        }

        const std::size_t offset = r % capacity_;
        const std::size_t first_chunk = std::min(to_read, capacity_ - offset);
        std::memcpy(output.data(), storage_.data() + offset, first_chunk);
        if (to_read > first_chunk) {
            std::memcpy(
                output.data() + first_chunk,
                storage_.data(),
                to_read - first_chunk);
        }

        read_pos_.store(r + to_read, std::memory_order_release);
        total_bytes_consumed_.fetch_add(to_read, std::memory_order_relaxed);
        return to_read;
    }

    void clear() noexcept {
        read_pos_.store(0, std::memory_order_release);
        write_pos_.store(0, std::memory_order_release);
        total_bytes_consumed_.store(0, std::memory_order_release);
    }

    std::uint64_t totalBytesConsumed() const noexcept {
        return total_bytes_consumed_.load(std::memory_order_relaxed);
    }

    void recordUnderrun() noexcept {
        underrun_count_.fetch_add(1, std::memory_order_relaxed);
    }

    std::uint64_t underrunCount() const noexcept {
        return underrun_count_.load(std::memory_order_relaxed);
    }

    void resetUnderrunCount() noexcept {
        underrun_count_.store(0, std::memory_order_relaxed);
    }

private:
    std::size_t capacity_{0};
    std::vector<std::byte> storage_{};
    alignas(64) std::atomic<std::size_t> write_pos_{0};
    alignas(64) std::atomic<std::size_t> read_pos_{0};
    alignas(64) std::atomic<std::uint64_t> total_bytes_consumed_{0};
    alignas(64) std::atomic<std::uint64_t> underrun_count_{0};
};

} // namespace mycds
