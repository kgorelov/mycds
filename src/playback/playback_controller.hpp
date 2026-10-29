#pragma once

#include "audio/audio_output.hpp"
#include "domain/audio_format.hpp"
#include "domain/disc_info.hpp"
#include "domain/track_info.hpp"
#include "source/disc_source.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mycds {

enum class PlaybackState {
    Stopped,
    Playing,
    Paused,
    Error
};

std::string_view playback_state_to_string(PlaybackState state) noexcept;

struct PlaybackPosition {
    std::int64_t lba{0};
    std::uint32_t frameInSector{0};
    std::optional<int> trackNumber{};
    std::optional<int> indexNumber{};
    std::int64_t trackRelativeSectors{0};
    std::int64_t discElapsedSectors{0};
};

// Resolves track number, index number, track-relative sector offset (relative to
// INDEX 01, negative inside INDEX 00 pregaps), and disc-elapsed sectors from an
// absolute LBA using the disc's logical layout.
PlaybackPosition resolve_playback_position(
    const DiscInfo& disc_info,
    const std::vector<TrackInfo>& tracks,
    std::int64_t lba,
    std::uint32_t frame_in_sector = 0) noexcept;

// Computes the exclusive end LBA of the contiguous playable audio range starting at
// start_lba. Traverses across adjacent TrackType::Audio boundaries seamlessly and
// stops at disc end or at the first non-audio track / non-contiguous gap.
std::int64_t compute_contiguous_audio_end_lba(
    const std::vector<TrackInfo>& tracks,
    std::int64_t start_lba) noexcept;

struct PlaybackControllerConfig {
    AudioOutputConfig audioConfig{
        .ringBufferSectors = 225, // 3 seconds of CD-DA audio
        .preferredDriver = {},
    };
    std::uint32_t readChunkSectors{16};
    std::size_t prebufferSectors{30};
};

struct PlaybackStatus {
    bool hasSource{false};
    std::string sourceKind{};
    std::string sourcePath{};
    PlaybackState state{PlaybackState::Stopped};
    PlaybackPosition position{};
    bool endOfDisc{false};
    std::size_t bufferedBytes{0};
    std::uint64_t sectorsReadInSession{0};
    std::uint64_t bytesConsumedInSession{0};
    std::uint64_t underrunCount{0};
    std::uint64_t audioDeviceOpenCount{0};
    std::string errorMessage{};
    std::optional<DiscSourceErrorKind> errorKind{};
    std::optional<std::int64_t> errorLba{};
};

// Source-independent CD-DA playback controller operating exclusively through
// DiscSource and AudioOutput.
//
// - Preserves gapless transitions across contiguous audio tracks and pregaps
//   without stopping or reopening AudioOutput.
// - Executes all DiscSource sector reads on a background producer thread, never
//   inside the real-time SDL3 audio callback.
// - seekToTrack(n) targets track n's conventional INDEX 01 start address, whereas
//   continuous playback plays uninterrupted through INDEX 00 pregaps.
class PlaybackController {
public:
    explicit PlaybackController(const PlaybackControllerConfig& config = {});
    ~PlaybackController();

    PlaybackController(const PlaybackController&) = delete;
    PlaybackController& operator=(const PlaybackController&) = delete;
    PlaybackController(PlaybackController&&) noexcept;
    PlaybackController& operator=(PlaybackController&&) noexcept;

    void openSource(std::unique_ptr<DiscSource> source);
    void closeSource() noexcept;

    void play();
    void pause();
    void stop() noexcept;
    void seekToTrack(int trackNumber);
    void seekToLba(std::int64_t lba);

    bool hasSource() const noexcept;
    PlaybackState state() const noexcept;
    PlaybackStatus status() const;

    PlaybackPosition currentPosition() const noexcept;
    std::int64_t currentLba() const noexcept;
    std::optional<int> currentTrack() const noexcept;
    std::optional<int> currentIndex() const noexcept;
    bool isEndOfDisc() const noexcept;

    std::string lastError() const;
    std::optional<DiscSourceErrorKind> lastErrorKind() const noexcept;

    DiscInfo getDiscInfo() const;
    std::vector<TrackInfo> getTracks() const;
    DiscLayout getDiscLayout() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mycds
