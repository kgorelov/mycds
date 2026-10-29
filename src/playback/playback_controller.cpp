#include "playback/playback_controller.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>

namespace mycds {

std::string_view playback_state_to_string(PlaybackState state) noexcept {
    switch (state) {
        case PlaybackState::Stopped:
            return "Stopped";
        case PlaybackState::Playing:
            return "Playing";
        case PlaybackState::Paused:
            return "Paused";
        case PlaybackState::Error:
            return "Error";
    }
    return "Unknown";
}

PlaybackPosition resolve_playback_position(
    const DiscInfo& disc_info,
    const std::vector<TrackInfo>& tracks,
    std::int64_t lba,
    std::uint32_t frame_in_sector) noexcept {
    PlaybackPosition pos{};
    pos.lba = lba;
    pos.frameInSector = std::min(frame_in_sector, AudioFormat::kFramesPerSector - 1U);
    pos.discElapsedSectors = lba - disc_info.startLba;

    if (const TrackInfo* track = find_track_at_lba(tracks, lba)) {
        pos.trackNumber = track->number;
        pos.trackRelativeSectors = lba - track->index01Lba;
        if (const IndexInfo* idx = find_index_at_lba(*track, lba)) {
            pos.indexNumber = idx->number;
        }
    }

    return pos;
}

std::int64_t compute_contiguous_audio_end_lba(
    const std::vector<TrackInfo>& tracks,
    std::int64_t start_lba) noexcept {
    std::size_t idx = 0;
    while (idx < tracks.size()) {
        const auto& trk = tracks[idx];
        if (start_lba >= trk.startLba && start_lba < trk.endLba) {
            break;
        }
        ++idx;
    }

    if (idx >= tracks.size() || tracks[idx].type != TrackType::Audio) {
        return start_lba;
    }

    std::int64_t contiguous_end = tracks[idx].endLba;
    for (std::size_t next = idx + 1; next < tracks.size(); ++next) {
        const auto& trk = tracks[next];
        if (trk.type != TrackType::Audio || trk.startLba != contiguous_end) {
            break;
        }
        contiguous_end = trk.endLba;
    }

    return contiguous_end;
}

struct PlaybackController::Impl {
    PlaybackControllerConfig config{};

    mutable std::mutex controlMutex{};
    mutable std::mutex stateMutex{};
    std::condition_variable cv{};

    std::unique_ptr<DiscSource> source{};
    DiscLayout layout{};
    AudioOutput audio{};
    std::thread worker{};

    bool hasSourceFlag{false};
    PlaybackState state{PlaybackState::Stopped};
    std::int64_t defaultStartLba{0};
    std::int64_t anchorLba{0};
    std::int64_t playableEndLba{0};
    bool endOfDisc{false};

    bool workerStopRequested{false};
    bool workerActive{false};
    bool prebufferReady{false};

    std::uint64_t sectorsReadInSession{0};
    std::uint64_t lastSessionBytesConsumed{0};
    std::uint64_t audioDeviceOpenCount{0};

    std::string errorMessage{};
    std::optional<DiscSourceErrorKind> errorKind{};
    std::optional<std::int64_t> errorLba{};

    explicit Impl(PlaybackControllerConfig cfg)
        : config(std::move(cfg)) {
        if (config.readChunkSectors == 0) {
            config.readChunkSectors = 16;
        }
        if (config.audioConfig.ringBufferSectors == 0) {
            config.audioConfig.ringBufferSectors = 225;
        }
    }

    ~Impl() {
        closeSource();
    }

    void stopWorker() noexcept {
        std::thread to_join;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            workerStopRequested = true;
            cv.notify_all();
            if (worker.joinable()) {
                to_join = std::move(worker);
            }
        }
        if (to_join.joinable()) {
            to_join.join();
        }
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            workerActive = false;
        }
    }

    PlaybackPosition computePositionLocked() const noexcept {
        if (!hasSourceFlag) {
            return PlaybackPosition{};
        }

        if (state == PlaybackState::Playing || state == PlaybackState::Paused) {
            const std::uint64_t consumed = audio.totalBytesConsumed();
            const std::uint64_t consumed_sectors = consumed / AudioFormat::kBytesPerSector;
            const std::uint64_t rem_bytes = consumed % AudioFormat::kBytesPerSector;

            std::int64_t lba = anchorLba + static_cast<std::int64_t>(consumed_sectors);
            std::uint32_t frame_in_sector =
                static_cast<std::uint32_t>(rem_bytes / AudioFormat::kBytesPerSampleFrame);

            if (playableEndLba > anchorLba && lba >= playableEndLba) {
                lba = playableEndLba - 1;
                frame_in_sector = AudioFormat::kFramesPerSector - 1U;
            }

            return resolve_playback_position(
                layout.discInfo, layout.tracks, lba, frame_in_sector);
        }

        const std::int64_t lba = endOfDisc ? playableEndLba : anchorLba;
        return resolve_playback_position(layout.discInfo, layout.tracks, lba, 0U);
    }

    void recordAudioErrorLocked(std::string message) noexcept {
        anchorLba = computePositionLocked().lba;
        lastSessionBytesConsumed = audio.totalBytesConsumed();
        try {
            audio.pause();
        } catch (...) {
        }
        state = PlaybackState::Error;
        errorMessage = std::move(message);
        errorKind = std::nullopt;
        errorLba = std::nullopt;
        prebufferReady = true;
        workerActive = false;
        cv.notify_all();
    }

    void workerLoop(
        std::int64_t start_lba,
        std::int64_t end_lba,
        std::size_t target_prebuffer_sectors) noexcept {
        const std::size_t chunk_sectors =
            std::max<std::size_t>(1U, config.readChunkSectors);
        const std::size_t ring_cap_sectors = std::max<std::size_t>(
            1U, audio.ringBufferCapacity() / AudioFormat::kBytesPerSector);

        std::vector<std::byte> sector_buf;
        try {
            sector_buf.resize(chunk_sectors * AudioFormat::kBytesPerSector);
        } catch (const std::exception& ex) {
            std::lock_guard<std::mutex> lock(stateMutex);
            recordAudioErrorLocked(ex.what());
            return;
        }

        std::int64_t next_lba = start_lba;
        std::uint64_t local_sectors_read = 0;

        while (next_lba < end_lba) {
            const std::size_t remaining_sectors =
                static_cast<std::size_t>(end_lba - next_lba);
            const std::size_t min_free_sectors = std::min<std::size_t>({
                chunk_sectors,
                remaining_sectors,
                std::max<std::size_t>(1U, ring_cap_sectors / 2U),
            });
            const std::size_t min_free_bytes =
                min_free_sectors * AudioFormat::kBytesPerSector;

            {
                std::unique_lock<std::mutex> lock(stateMutex);
                while (!workerStopRequested) {
                    if (audio.hasError()) {
                        recordAudioErrorLocked(audio.lastError());
                        return;
                    }
                    if (state != PlaybackState::Paused &&
                        audio.availableBytesToWrite() >= min_free_bytes) {
                        break;
                    }
                    cv.wait_for(lock, std::chrono::milliseconds(5));
                }

                if (workerStopRequested) {
                    workerActive = false;
                    cv.notify_all();
                    return;
                }
            }

            const std::size_t writable_sectors =
                audio.availableBytesToWrite() / AudioFormat::kBytesPerSector;
            const auto sectors_to_read = static_cast<std::uint32_t>(std::min<std::size_t>({
                chunk_sectors,
                remaining_sectors,
                std::max<std::size_t>(1U, writable_sectors),
            }));
            const std::size_t bytes_to_read =
                static_cast<std::size_t>(sectors_to_read) * AudioFormat::kBytesPerSector;

            try {
                source->readAudioSectors(
                    next_lba,
                    sectors_to_read,
                    std::span<std::byte>(sector_buf.data(), bytes_to_read));
            } catch (const DiscSourceError& ex) {
                std::lock_guard<std::mutex> lock(stateMutex);
                if (!workerStopRequested) {
                    anchorLba = computePositionLocked().lba;
                    lastSessionBytesConsumed = audio.totalBytesConsumed();
                    try {
                        audio.pause();
                    } catch (...) {
                    }
                    state = PlaybackState::Error;
                    errorMessage = ex.what();
                    errorKind = ex.kind();
                    errorLba = next_lba;
                    prebufferReady = true;
                }
                workerActive = false;
                cv.notify_all();
                return;
            } catch (const std::exception& ex) {
                std::lock_guard<std::mutex> lock(stateMutex);
                if (!workerStopRequested) {
                    anchorLba = computePositionLocked().lba;
                    lastSessionBytesConsumed = audio.totalBytesConsumed();
                    try {
                        audio.pause();
                    } catch (...) {
                    }
                    state = PlaybackState::Error;
                    errorMessage = ex.what();
                    errorKind = DiscSourceErrorKind::ReadFailed;
                    errorLba = next_lba;
                    prebufferReady = true;
                }
                workerActive = false;
                cv.notify_all();
                return;
            }

            std::size_t enqueued_offset = 0;
            while (enqueued_offset < bytes_to_read) {
                {
                    std::unique_lock<std::mutex> lock(stateMutex);
                    while (!workerStopRequested && state == PlaybackState::Paused &&
                           audio.availableBytesToWrite() < AudioFormat::kBytesPerSampleFrame) {
                        cv.wait_for(lock, std::chrono::milliseconds(5));
                    }
                    if (workerStopRequested) {
                        workerActive = false;
                        cv.notify_all();
                        return;
                    }
                    if (audio.hasError()) {
                        recordAudioErrorLocked(audio.lastError());
                        return;
                    }
                }

                const std::size_t written = audio.enqueuePcm(std::span<const std::byte>(
                    sector_buf.data() + enqueued_offset,
                    bytes_to_read - enqueued_offset));
                enqueued_offset += written;

                if (enqueued_offset < bytes_to_read && written == 0) {
                    std::unique_lock<std::mutex> lock(stateMutex);
                    if (workerStopRequested) {
                        workerActive = false;
                        cv.notify_all();
                        return;
                    }
                    cv.wait_for(lock, std::chrono::milliseconds(2));
                }
            }

            next_lba += static_cast<std::int64_t>(sectors_to_read);
            local_sectors_read += sectors_to_read;

            {
                std::lock_guard<std::mutex> lock(stateMutex);
                sectorsReadInSession = local_sectors_read;
                if (!prebufferReady &&
                    (local_sectors_read >= target_prebuffer_sectors || next_lba >= end_lba)) {
                    prebufferReady = true;
                    cv.notify_all();
                }
            }
        }

        audio.setEndOfStream(true);
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (!prebufferReady) {
                prebufferReady = true;
                cv.notify_all();
            }
        }

        // Wait for all buffered PCM frames to drain through SDL3 audio output
        std::unique_lock<std::mutex> lock(stateMutex);
        while (!workerStopRequested) {
            if (audio.hasError()) {
                recordAudioErrorLocked(audio.lastError());
                return;
            }

            if (state == PlaybackState::Playing &&
                audio.availableBytesToRead() == 0 &&
                audio.bufferedBytes() == 0) {
                lastSessionBytesConsumed = audio.totalBytesConsumed();
                try {
                    audio.pause();
                } catch (...) {
                }
                state = PlaybackState::Stopped;
                endOfDisc = true;
                anchorLba = end_lba;
                workerActive = false;
                cv.notify_all();
                return;
            }

            cv.wait_for(lock, std::chrono::milliseconds(5));
        }

        workerActive = false;
        cv.notify_all();
    }

    void startStreamingFromAnchor() {
        stopWorker();

        std::int64_t start_lba = 0;
        std::int64_t end_lba = 0;
        std::size_t target_prebuffer = 0;

        {
            std::lock_guard<std::mutex> lock(stateMutex);
            audio.stop();

            playableEndLba = compute_contiguous_audio_end_lba(layout.tracks, anchorLba);
            if (playableEndLba <= anchorLba) {
                throw DiscSourceError(
                    DiscSourceErrorKind::NonAudioTrack,
                    "No playable audio sectors starting at LBA " + std::to_string(anchorLba));
            }

            start_lba = anchorLba;
            end_lba = playableEndLba;
            const auto total_sectors = static_cast<std::size_t>(end_lba - start_lba);
            const std::size_t ring_cap_sectors = std::max<std::size_t>(
                1U, audio.ringBufferCapacity() / AudioFormat::kBytesPerSector);

            target_prebuffer = std::min<std::size_t>({
                config.prebufferSectors,
                ring_cap_sectors,
                total_sectors,
            });

            endOfDisc = false;
            sectorsReadInSession = 0;
            lastSessionBytesConsumed = 0;
            errorMessage.clear();
            errorKind = std::nullopt;
            errorLba = std::nullopt;

            workerStopRequested = false;
            workerActive = true;
            prebufferReady = (target_prebuffer == 0);
            state = PlaybackState::Playing;

            worker = std::thread(
                &Impl::workerLoop,
                this,
                start_lba,
                end_lba,
                target_prebuffer);
        }

        bool should_unpause_audio = false;
        {
            std::unique_lock<std::mutex> lock(stateMutex);
            cv.wait(lock, [&]() {
                return prebufferReady || state != PlaybackState::Playing || workerStopRequested;
            });
            should_unpause_audio = (state == PlaybackState::Playing && !workerStopRequested);
        }

        if (should_unpause_audio) {
            try {
                audio.start();
                // Notify worker in case all sectors already fit in prebuffer and it is
                // waiting for AudioOutput to drain
                cv.notify_all();
            } catch (const std::exception& ex) {
                stopWorker();
                std::lock_guard<std::mutex> lock(stateMutex);
                state = PlaybackState::Error;
                errorMessage = ex.what();
                errorKind = std::nullopt;
                errorLba = std::nullopt;
                throw;
            }
        }
    }

    void openSource(std::unique_ptr<DiscSource> new_source) {
        if (!new_source) {
            throw std::invalid_argument("DiscSource cannot be null");
        }

        DiscLayout new_layout = new_source->getDiscLayout();
        if (new_layout.tracks.empty()) {
            throw DiscSourceError(
                DiscSourceErrorKind::UnsupportedLayout,
                "Disc source contains no tracks");
        }

        const TrackInfo* first_audio = nullptr;
        for (const auto& trk : new_layout.tracks) {
            if (trk.type == TrackType::Audio && trk.endLba > trk.startLba) {
                first_audio = &trk;
                break;
            }
        }
        if (!first_audio) {
            throw DiscSourceError(
                DiscSourceErrorKind::UnsupportedLayout,
                "Disc source contains no playable audio tracks");
        }

        std::lock_guard<std::mutex> ctrl_lock(controlMutex);
        stopWorker();

        {
            std::lock_guard<std::mutex> state_lock(stateMutex);
            audio.close();
            source.reset();
            hasSourceFlag = false;
        }

        audio.open(config.audioConfig);

        {
            std::lock_guard<std::mutex> state_lock(stateMutex);
            ++audioDeviceOpenCount;
            source = std::move(new_source);
            layout = std::move(new_layout);
            hasSourceFlag = true;

            defaultStartLba = first_audio->index01Lba;
            anchorLba = defaultStartLba;
            playableEndLba =
                compute_contiguous_audio_end_lba(layout.tracks, anchorLba);
            state = PlaybackState::Stopped;
            endOfDisc = false;
            sectorsReadInSession = 0;
            lastSessionBytesConsumed = 0;
            errorMessage.clear();
            errorKind = std::nullopt;
            errorLba = std::nullopt;
        }
    }

    void closeSource() noexcept {
        std::lock_guard<std::mutex> ctrl_lock(controlMutex);
        stopWorker();

        std::lock_guard<std::mutex> state_lock(stateMutex);
        audio.close();
        source.reset();
        layout = DiscLayout{};
        hasSourceFlag = false;
        state = PlaybackState::Stopped;
        defaultStartLba = 0;
        anchorLba = 0;
        playableEndLba = 0;
        endOfDisc = false;
        sectorsReadInSession = 0;
        lastSessionBytesConsumed = 0;
        errorMessage.clear();
        errorKind = std::nullopt;
        errorLba = std::nullopt;
    }

    void play() {
        std::lock_guard<std::mutex> ctrl_lock(controlMutex);

        bool resume_existing_worker = false;
        {
            std::lock_guard<std::mutex> state_lock(stateMutex);
            if (!hasSourceFlag) {
                throw std::runtime_error("Cannot play: no disc source is open");
            }

            if (state == PlaybackState::Playing) {
                return;
            }

            if (state == PlaybackState::Paused && workerActive) {
                state = PlaybackState::Playing;
                resume_existing_worker = true;
            } else if (state == PlaybackState::Stopped && endOfDisc) {
                anchorLba = defaultStartLba;
                endOfDisc = false;
            }
        }

        if (resume_existing_worker) {
            try {
                audio.resume();
                cv.notify_all();
            } catch (const std::exception& ex) {
                stopWorker();
                std::lock_guard<std::mutex> state_lock(stateMutex);
                state = PlaybackState::Error;
                errorMessage = ex.what();
                errorKind = std::nullopt;
                errorLba = std::nullopt;
                throw;
            }
            return;
        }

        startStreamingFromAnchor();
    }

    void pause() {
        std::lock_guard<std::mutex> ctrl_lock(controlMutex);
        std::lock_guard<std::mutex> state_lock(stateMutex);

        if (!hasSourceFlag) {
            throw std::runtime_error("Cannot pause: no disc source is open");
        }

        if (state == PlaybackState::Playing) {
            audio.pause();
            state = PlaybackState::Paused;
            cv.notify_all();
        }
    }

    void stop() noexcept {
        std::lock_guard<std::mutex> ctrl_lock(controlMutex);
        stopWorker();

        std::lock_guard<std::mutex> state_lock(stateMutex);
        if (!hasSourceFlag) {
            return;
        }

        audio.stop();
        state = PlaybackState::Stopped;
        anchorLba = defaultStartLba;
        playableEndLba =
            compute_contiguous_audio_end_lba(layout.tracks, anchorLba);
        endOfDisc = false;
        sectorsReadInSession = 0;
        lastSessionBytesConsumed = 0;
        errorMessage.clear();
        errorKind = std::nullopt;
        errorLba = std::nullopt;
    }

    void seekToLbaWithControlLock(std::int64_t lba) {
        PlaybackState prev_state = PlaybackState::Stopped;
        {
            std::lock_guard<std::mutex> state_lock(stateMutex);
            if (!hasSourceFlag) {
                throw std::runtime_error("Cannot seek: no disc source is open");
            }

            if (lba < layout.discInfo.startLba || lba >= layout.discInfo.endLba) {
                throw DiscOutOfRangeError(
                    "Seek target LBA " + std::to_string(lba) +
                    " is outside disc LBA range [" +
                    std::to_string(layout.discInfo.startLba) + ", " +
                    std::to_string(layout.discInfo.endLba) + ")");
            }

            const TrackInfo* target_track = find_track_at_lba(layout.tracks, lba);
            if (!target_track || target_track->type != TrackType::Audio) {
                throw DiscSourceError(
                    DiscSourceErrorKind::NonAudioTrack,
                    "Seek target LBA " + std::to_string(lba) +
                        " does not belong to an audio track");
            }

            prev_state = state;
        }

        try {
            audio.pause();
        } catch (...) {
        }
        stopWorker();

        {
            std::lock_guard<std::mutex> state_lock(stateMutex);
            audio.clearBuffer();
            audio.setEndOfStream(false);

            anchorLba = lba;
            playableEndLba = compute_contiguous_audio_end_lba(layout.tracks, lba);
            endOfDisc = false;
            sectorsReadInSession = 0;
            lastSessionBytesConsumed = 0;
            errorMessage.clear();
            errorKind = std::nullopt;
            errorLba = std::nullopt;

            if (prev_state == PlaybackState::Paused) {
                state = PlaybackState::Paused;
            } else if (prev_state != PlaybackState::Playing) {
                state = PlaybackState::Stopped;
            }
        }

        if (prev_state == PlaybackState::Playing) {
            startStreamingFromAnchor();
        }
    }

    void seekToLba(std::int64_t lba) {
        std::lock_guard<std::mutex> ctrl_lock(controlMutex);
        seekToLbaWithControlLock(lba);
    }

    void seekToTrack(int track_number) {
        std::lock_guard<std::mutex> ctrl_lock(controlMutex);

        std::int64_t target_lba = 0;
        {
            std::lock_guard<std::mutex> state_lock(stateMutex);
            if (!hasSourceFlag) {
                throw std::runtime_error("Cannot seek: no disc source is open");
            }

            const TrackInfo* track = find_track_by_number(layout.tracks, track_number);
            if (!track) {
                throw DiscOutOfRangeError(
                    "Track " + std::to_string(track_number) +
                    " does not exist on disc");
            }
            if (track->type != TrackType::Audio) {
                throw DiscSourceError(
                    DiscSourceErrorKind::NonAudioTrack,
                    "Track " + std::to_string(track_number) +
                        " is not an audio track");
            }
            target_lba = track->index01Lba;
        }

        seekToLbaWithControlLock(target_lba);
    }

    PlaybackStatus status() const {
        std::lock_guard<std::mutex> state_lock(stateMutex);

        PlaybackStatus st{};
        st.hasSource = hasSourceFlag;
        st.state = state;
        st.endOfDisc = endOfDisc;
        st.sectorsReadInSession = sectorsReadInSession;
        st.audioDeviceOpenCount = audioDeviceOpenCount;
        st.errorMessage = errorMessage;
        st.errorKind = errorKind;
        st.errorLba = errorLba;

        if (hasSourceFlag) {
            st.sourceKind = layout.sourceKind;
            st.sourcePath = layout.sourcePath;
            st.position = computePositionLocked();
            st.bufferedBytes = audio.bufferedBytes();
            st.underrunCount = audio.underrunCount();
            if (state == PlaybackState::Playing || state == PlaybackState::Paused) {
                st.bytesConsumedInSession = audio.totalBytesConsumed();
            } else {
                st.bytesConsumedInSession = lastSessionBytesConsumed;
            }
        }

        return st;
    }
};

PlaybackController::PlaybackController(const PlaybackControllerConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

PlaybackController::~PlaybackController() = default;
PlaybackController::PlaybackController(PlaybackController&&) noexcept = default;
PlaybackController& PlaybackController::operator=(PlaybackController&&) noexcept = default;

void PlaybackController::openSource(std::unique_ptr<DiscSource> source) {
    impl_->openSource(std::move(source));
}

void PlaybackController::closeSource() noexcept {
    if (impl_) {
        impl_->closeSource();
    }
}

void PlaybackController::play() {
    impl_->play();
}

void PlaybackController::pause() {
    impl_->pause();
}

void PlaybackController::stop() noexcept {
    if (impl_) {
        impl_->stop();
    }
}

void PlaybackController::seekToTrack(int trackNumber) {
    impl_->seekToTrack(trackNumber);
}

void PlaybackController::seekToLba(std::int64_t lba) {
    impl_->seekToLba(lba);
}

bool PlaybackController::hasSource() const noexcept {
    if (!impl_) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->stateMutex);
    return impl_->hasSourceFlag;
}

PlaybackState PlaybackController::state() const noexcept {
    if (!impl_) {
        return PlaybackState::Stopped;
    }
    std::lock_guard<std::mutex> lock(impl_->stateMutex);
    return impl_->state;
}

PlaybackStatus PlaybackController::status() const {
    if (!impl_) {
        return PlaybackStatus{};
    }
    return impl_->status();
}

PlaybackPosition PlaybackController::currentPosition() const noexcept {
    if (!impl_) {
        return PlaybackPosition{};
    }
    std::lock_guard<std::mutex> lock(impl_->stateMutex);
    return impl_->computePositionLocked();
}

std::int64_t PlaybackController::currentLba() const noexcept {
    return currentPosition().lba;
}

std::optional<int> PlaybackController::currentTrack() const noexcept {
    return currentPosition().trackNumber;
}

std::optional<int> PlaybackController::currentIndex() const noexcept {
    return currentPosition().indexNumber;
}

bool PlaybackController::isEndOfDisc() const noexcept {
    if (!impl_) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->stateMutex);
    return impl_->endOfDisc;
}

std::string PlaybackController::lastError() const {
    if (!impl_) {
        return {};
    }
    std::lock_guard<std::mutex> lock(impl_->stateMutex);
    return impl_->errorMessage;
}

std::optional<DiscSourceErrorKind> PlaybackController::lastErrorKind() const noexcept {
    if (!impl_) {
        return std::nullopt;
    }
    std::lock_guard<std::mutex> lock(impl_->stateMutex);
    return impl_->errorKind;
}

DiscInfo PlaybackController::getDiscInfo() const {
    if (!impl_) {
        throw std::runtime_error("No disc source is open");
    }
    std::lock_guard<std::mutex> lock(impl_->stateMutex);
    if (!impl_->hasSourceFlag) {
        throw std::runtime_error("No disc source is open");
    }
    return impl_->layout.discInfo;
}

std::vector<TrackInfo> PlaybackController::getTracks() const {
    if (!impl_) {
        throw std::runtime_error("No disc source is open");
    }
    std::lock_guard<std::mutex> lock(impl_->stateMutex);
    if (!impl_->hasSourceFlag) {
        throw std::runtime_error("No disc source is open");
    }
    return impl_->layout.tracks;
}

DiscLayout PlaybackController::getDiscLayout() const {
    if (!impl_) {
        throw std::runtime_error("No disc source is open");
    }
    std::lock_guard<std::mutex> lock(impl_->stateMutex);
    if (!impl_->hasSourceFlag) {
        throw std::runtime_error("No disc source is open");
    }
    return impl_->layout;
}

} // namespace mycds
