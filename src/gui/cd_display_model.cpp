#include "cd_display_model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <utility>

#include "domain/audio_format.hpp"
#include "metadata/cddb_disc_id.hpp"
#include "metadata/cddb_provider.hpp"
#include "metadata/metadata_matcher.hpp"
#include "metadata/metadata_store.hpp"

namespace mycds::gui {

struct CdDisplayModel::AsyncCddbResult {
    std::uint64_t generation{0};
    bool ready{false};
    bool found{false};
    bool network_error{false};
    mycds::MetadataRecord record{};
};

struct CdDisplayModel::AsyncOpenResult {
    std::uint64_t generation{0};
    bool ready{false};
    bool success{false};
    std::string error_message{};
    std::unique_ptr<mycds::PlaybackController> controller{};
    mycds::DiscLayout layout{};
};

namespace {

std::filesystem::path physical_cd_cache_sidecar_path(const mycds::DiscLayout& layout) {
    std::string disc_id = layout.discMetadata.databaseDiscId;
    if (disc_id.empty() && !layout.tracks.empty()) {
        try {
            disc_id = mycds::calculate_cddb_disc_id(layout.discInfo, layout.tracks).discIdHex;
        } catch (...) {
            disc_id = "unknown";
        }
    }

    std::filesystem::path base_dir;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && xdg[0] != '\0') {
        base_dir = std::filesystem::path(xdg) / "mycds" / "cddb";
    } else if (const char* home = std::getenv("HOME"); home && home[0] != '\0') {
        base_dir = std::filesystem::path(home) / ".config" / "mycds" / "cddb";
    } else {
        base_dir = std::filesystem::current_path();
    }
    return base_dir / ("disc_" + disc_id + ".mycds.json");
}

std::filesystem::path resolve_gui_sidecar_save_path(const mycds::DiscLayout& layout) {
    if (!layout.sourcePath.empty() && layout.sourcePath.rfind("/dev/", 0) == 0) {
        return physical_cd_cache_sidecar_path(layout);
    }
    return mycds::MetadataStore::defaultSidecarPath(layout);
}

} // namespace

CdDisplayModel::CdDisplayModel()
    : CdDisplayModel(mycds::PlaybackControllerConfig{}) {}

CdDisplayModel::CdDisplayModel(mycds::PlaybackControllerConfig controller_config)
    : controller_config_(std::move(controller_config)) {
    tracks_ = {
        {1, 222, "Speak to Me / Breathe"},
        {2, 255, "On the Run"},
        {3, 178, "Time"},
        {4, 321, "The Great Gig in the Sky"},
        {5, 192, "Money"},
        {6, 288, "Us and Them"},
        {7, 364, "Any Colour You Like"},
        {8, 213, "Brain Damage"},
        {9, 260, "Eclipse"},
        {10, 165, "Shine On You Crazy Diamond (Part I)"},
        {11, 310, "Welcome to the Machine"},
        {12, 230, "Wish You Were Here"}
    };
}

CdDisplayModel::~CdDisplayModel() {
    cancel_async_open();
    cancel_async_cddb_lookup();
    if (controller_) {
        controller_->closeSource();
    }
}

void CdDisplayModel::set_playback_controller_config(const mycds::PlaybackControllerConfig& config) {
    controller_config_ = config;
}

bool CdDisplayModel::open_disc_source(
    std::unique_ptr<mycds::DiscSource> source,
    std::string* out_error) {
    cancel_async_cddb_lookup();
    cddb_status_ = CddbStatus::Inactive;
    pending_seek_active_ = false;
    pending_seek_was_playing_ = false;
    pending_seek_idle_frames_ = 0;
    playback_error_.clear();

    if (!source) {
        playback_error_ = "No disc source provided";
        if (out_error) {
            *out_error = playback_error_;
        }
        return false;
    }

    if (!controller_) {
        controller_ = std::make_unique<mycds::PlaybackController>(controller_config_);
    }

    try {
        controller_->openSource(std::move(source));
    } catch (const std::exception& ex) {
        playback_error_ = ex.what();
        if (out_error) {
            *out_error = playback_error_;
        }
        return false;
    }

    has_real_source_ = true;
    disc_layout_ = controller_->getDiscLayout();

    // Load local sidecar metadata if available (non-blocking)
    try {
        std::filesystem::path sidecar = mycds::MetadataStore::defaultSidecarPath(disc_layout_);
        if (!mycds::MetadataStore::exists(sidecar) &&
            disc_layout_.sourcePath.rfind("/dev/", 0) == 0) {
            sidecar = physical_cd_cache_sidecar_path(disc_layout_);
        }
        if (mycds::MetadataStore::exists(sidecar)) {
            const auto rec = mycds::MetadataStore::loadSidecar(sidecar);
            mycds::MetadataMatcher::applyToLayout(disc_layout_, rec);
            controller_->updateMetadata(disc_layout_.discMetadata, disc_layout_.trackMetadata);
        }
    } catch (...) {
    }

    tracks_.clear();
    tracks_.reserve(disc_layout_.tracks.size());
    for (const auto& trk : disc_layout_.tracks) {
        const std::int64_t sectors = std::max<std::int64_t>(0, trk.endLba - trk.index01Lba);
        const int duration_sec = static_cast<int>(sectors / mycds::AudioFormat::kSectorsPerSecond);
        tracks_.push_back(TrackInfo{trk.number, duration_sec, {}});
    }

    apply_layout_metadata();

    controller_->setVolume(volume_);
    controller_->setBalance(balance_);
    controller_->setSingleTrackMode(repeat_mode_ == RepeatMode::Track || shuffle_);

    recording_ = false;
    current_track_index_ = 0;
    for (size_t i = 0; i < tracks_.size(); ++i) {
        if (is_playable_track_index(i)) {
            current_track_index_ = i;
            break;
        }
    }
    track_elapsed_seconds_ = 0.0;
    eject_menu_open_ = false;
    reset_volume_levels();

    cddb_status_ = CddbStatus::Inactive;
    if (disc_layout_.discMetadata.title.empty()) {
        connectivity_ = ConnectivityStatus::Connected;
        start_async_cddb_lookup();
    }

    if (app_settings_.auto_play_on_disc_load) {
        play();
    } else {
        playing_ = false;
    }

    return true;
}

void CdDisplayModel::cancel_async_open() noexcept {
    open_generation_.fetch_add(1, std::memory_order_release);
    if (open_thread_.joinable()) {
        open_thread_.join();
    }
    std::lock_guard<std::mutex> lock(open_mutex_);
    open_result_.reset();
    loading_disc_ = false;
    loading_source_path_.clear();
}

void CdDisplayModel::open_source_async(const AsyncOpenRequest& request) {
    cancel_async_open();
    cancel_async_cddb_lookup();
    eject_disc();

    if (request.path.empty()) {
        playback_error_ = "No disc source path provided";
        return;
    }

    loading_disc_ = true;
    loading_source_path_ = request.path;
    playback_error_.clear();

    const std::uint64_t gen = open_generation_.load(std::memory_order_acquire);
    const mycds::PlaybackControllerConfig cfg = controller_config_;
    const double vol = volume_;
    const double bal = balance_;
    const bool single_trk = (repeat_mode_ == RepeatMode::Track || shuffle_);

    {
        std::lock_guard<std::mutex> lock(open_mutex_);
        open_result_ = std::make_unique<AsyncOpenResult>();
        open_result_->generation = gen;
    }

    open_thread_ = std::thread([this, gen, request, cfg, vol, bal, single_trk]() {
        auto result = std::make_unique<AsyncOpenResult>();
        result->generation = gen;
        result->ready = true;

        try {
            auto src = request.is_device
                ? mycds::open_physical_cd_source(request.path)
                : mycds::open_image_source(request.path);

            if (open_generation_.load(std::memory_order_acquire) != gen) {
                return;
            }

            auto ctrl = std::make_unique<mycds::PlaybackController>(cfg);
            ctrl->openSource(std::move(src));
            mycds::DiscLayout layout = ctrl->getDiscLayout();

            try {
                std::filesystem::path sidecar = mycds::MetadataStore::defaultSidecarPath(layout);
                if (!mycds::MetadataStore::exists(sidecar) &&
                    layout.sourcePath.rfind("/dev/", 0) == 0) {
                    sidecar = physical_cd_cache_sidecar_path(layout);
                }
                if (mycds::MetadataStore::exists(sidecar)) {
                    const auto rec = mycds::MetadataStore::loadSidecar(sidecar);
                    mycds::MetadataMatcher::applyToLayout(layout, rec);
                    ctrl->updateMetadata(layout.discMetadata, layout.trackMetadata);
                }
            } catch (...) {
            }

            ctrl->setVolume(vol);
            ctrl->setBalance(bal);
            ctrl->setSingleTrackMode(single_trk);

            if (request.initial_track.has_value()) {
                const double sec = std::max(0.0, request.initial_seek_seconds.value_or(0.0));
                if (const mycds::TrackInfo* trk =
                        mycds::find_track_by_number(layout.tracks, *request.initial_track);
                    trk && trk->type == mycds::TrackType::Audio) {
                    const std::int64_t trk_secs = trk->endLba - trk->index01Lba;
                    if (trk_secs > 0) {
                        const std::int64_t off = std::clamp(
                            static_cast<std::int64_t>(std::llround(sec * mycds::AudioFormat::kSectorsPerSecond)),
                            std::int64_t{0},
                            trk_secs - 1);
                        ctrl->seekToLba(trk->index01Lba + off);
                    }
                }
            } else if (request.initial_seek_seconds.has_value() && request.seek_is_disc_global) {
                const double sec = std::max(0.0, *request.initial_seek_seconds);
                std::int64_t start_lba = layout.discInfo.startLba;
                for (const auto& t : layout.tracks) {
                    if (t.type == mycds::TrackType::Audio) {
                        start_lba = t.index01Lba;
                        break;
                    }
                }
                const std::int64_t end_lba =
                    mycds::compute_contiguous_audio_end_lba(layout.tracks, start_lba);
                if (end_lba > start_lba) {
                    const std::int64_t off = static_cast<std::int64_t>(
                        std::llround(sec * mycds::AudioFormat::kSectorsPerSecond));
                    ctrl->seekToLba(std::clamp(start_lba + off, start_lba, end_lba - 1));
                }
            }

            if (open_generation_.load(std::memory_order_acquire) != gen) {
                ctrl->closeSource();
                return;
            }

            if (request.auto_play) {
                ctrl->play();
            }

            result->success = true;
            result->controller = std::move(ctrl);
            result->layout = std::move(layout);
        } catch (const std::exception& ex) {
            result->success = false;
            result->error_message = ex.what();
        }

        std::lock_guard<std::mutex> lock(open_mutex_);
        if (open_result_ && open_result_->generation == gen) {
            open_result_ = std::move(result);
        }
    });
}

void CdDisplayModel::poll_async_open() {
    std::unique_ptr<AsyncOpenResult> completed;
    {
        std::lock_guard<std::mutex> lock(open_mutex_);
        if (open_result_ && open_result_->ready) {
            completed = std::move(open_result_);
        }
    }
    if (!completed) {
        return;
    }

    if (open_thread_.joinable()) {
        open_thread_.join();
    }

    if (completed->generation != open_generation_.load(std::memory_order_acquire)) {
        if (completed->controller) {
            completed->controller->closeSource();
        }
        return;
    }

    loading_disc_ = false;
    loading_source_path_.clear();

    if (!completed->success || !completed->controller) {
        playback_error_ = completed->error_message;
        has_real_source_ = false;
        playing_ = false;
        return;
    }

    if (controller_) {
        controller_->closeSource();
    }
    controller_ = std::move(completed->controller);
    disc_layout_ = std::move(completed->layout);
    has_real_source_ = true;
    playback_error_.clear();

    tracks_.clear();
    tracks_.reserve(disc_layout_.tracks.size());
    for (const auto& trk : disc_layout_.tracks) {
        const std::int64_t sectors = std::max<std::int64_t>(0, trk.endLba - trk.index01Lba);
        const int duration_sec = static_cast<int>(sectors / mycds::AudioFormat::kSectorsPerSecond);
        tracks_.push_back(TrackInfo{trk.number, duration_sec, {}});
    }

    apply_layout_metadata();

    const auto st = controller_->status();
    playing_ = (st.state == mycds::PlaybackState::Playing);
    current_track_index_ = 0;
    if (st.position.trackNumber.has_value()) {
        for (size_t i = 0; i < tracks_.size(); ++i) {
            if (tracks_[i].number == *st.position.trackNumber) {
                current_track_index_ = i;
                break;
            }
        }
    }
    track_elapsed_seconds_ = (st.position.trackRelativeSectors >= 0)
        ? (static_cast<double>(st.position.trackRelativeSectors) / mycds::AudioFormat::kSectorsPerSecond)
        : 0.0;
    reset_volume_levels();

    cddb_status_ = CddbStatus::Inactive;
    if (disc_layout_.discMetadata.title.empty()) {
        connectivity_ = ConnectivityStatus::Connected;
        start_async_cddb_lookup();
    }
}

bool CdDisplayModel::is_loading_disc() const noexcept {
    return loading_disc_;
}

const std::string& CdDisplayModel::loading_source_path() const noexcept {
    return loading_source_path_;
}

void CdDisplayModel::eject_disc() noexcept {
    cancel_async_cddb_lookup();
    if (controller_) {
        controller_->stop();
        controller_->closeSource();
    }
    has_real_source_ = false;
    clear_disc();
}

void CdDisplayModel::clear_disc() noexcept {
    cancel_async_cddb_lookup();
    if (controller_ && has_real_source_) {
        controller_->stop();
        controller_->closeSource();
    }
    has_real_source_ = false;
    disc_layout_ = mycds::DiscLayout{};
    tracks_.clear();
    artist_name_.clear();
    album_title_.clear();
    has_cddb_data_ = false;
    cddb_status_ = CddbStatus::Inactive;
    connectivity_ = ConnectivityStatus::Connected;
    playing_ = false;
    recording_ = false;
    current_track_index_ = 0;
    track_elapsed_seconds_ = 0.0;
    pending_seek_active_ = false;
    pending_seek_was_playing_ = false;
    pending_seek_idle_frames_ = 0;
    playback_error_.clear();
    reset_volume_levels();
}

bool CdDisplayModel::has_disc() const noexcept {
    return !tracks_.empty();
}

bool CdDisplayModel::has_real_source() const noexcept {
    return has_real_source_;
}

const std::string& CdDisplayModel::playback_error() const noexcept {
    return playback_error_;
}

const std::string& CdDisplayModel::source_path() const noexcept {
    return disc_layout_.sourcePath;
}

bool CdDisplayModel::source_is_device() const noexcept {
    return !disc_layout_.sourcePath.empty() && disc_layout_.sourcePath.rfind("/dev/", 0) == 0;
}

void CdDisplayModel::apply_layout_metadata() {
    artist_name_ = !disc_layout_.discMetadata.performer.empty()
        ? disc_layout_.discMetadata.performer
        : (!disc_layout_.discMetadata.title.empty() ? "Unknown Artist" : "");
    album_title_ = disc_layout_.discMetadata.title;
    if (album_title_.empty() && !disc_layout_.sourcePath.empty()) {
        const std::filesystem::path src_path(disc_layout_.sourcePath);
        album_title_ = src_path.has_stem() ? src_path.stem().string() : disc_layout_.sourceKind;
    }

    for (size_t i = 0; i < tracks_.size(); ++i) {
        std::string title;
        for (const auto& tm : disc_layout_.trackMetadata) {
            if (tm.trackNumber == tracks_[i].number && !tm.title.empty()) {
                title = tm.title;
                break;
            }
        }
        if (title.empty() && i < disc_layout_.tracks.size() &&
            disc_layout_.tracks[i].type == mycds::TrackType::Data) {
            title = "Track " + std::to_string(tracks_[i].number) + " [Data]";
        }
        tracks_[i].title = std::move(title);
    }

    has_cddb_data_ = !disc_layout_.discMetadata.empty();
}

void CdDisplayModel::cancel_async_cddb_lookup() noexcept {
    cddb_generation_.fetch_add(1, std::memory_order_release);
    if (cddb_thread_.joinable()) {
        cddb_thread_.join();
    }
    std::lock_guard<std::mutex> lock(cddb_mutex_);
    cddb_result_.reset();
    if (cddb_status_ == CddbStatus::Querying) {
        cddb_status_ = CddbStatus::Inactive;
    }
}

void CdDisplayModel::start_async_cddb_lookup() {
    cancel_async_cddb_lookup();
    if (!has_real_source_ || disc_layout_.tracks.empty()) {
        return;
    }

    cddb_status_ = CddbStatus::Querying;
    const std::uint64_t gen = cddb_generation_.load(std::memory_order_acquire);
    const mycds::DiscInfo disc_info = disc_layout_.discInfo;
    const std::vector<mycds::TrackInfo> layout_tracks = disc_layout_.tracks;
    auto transport = cddb_transport_;

    {
        std::lock_guard<std::mutex> lock(cddb_mutex_);
        cddb_result_ = std::make_unique<AsyncCddbResult>();
        cddb_result_->generation = gen;
    }

    cddb_thread_ = std::thread([this, gen, disc_info, layout_tracks, transport]() {
        bool found = false;
        bool net_err = false;
        mycds::MetadataRecord resolved{};

        try {
            mycds::CddbClientConfig cfg;
            cfg.connectTimeoutMs = 4000;
            cfg.requestTimeoutMs = 6000;
            mycds::CddbCompatibleProvider provider(cfg, transport);

            const auto candidates = provider.search(disc_info, layout_tracks);
            if (!candidates.empty()) {
                std::size_t chosen = 0;
                for (std::size_t i = 0; i < candidates.size(); ++i) {
                    if (candidates[i].exactMatch) {
                        chosen = i;
                        break;
                    }
                }
                if (cddb_generation_.load(std::memory_order_acquire) == gen) {
                    const auto record = provider.fetch(candidates[chosen], disc_info, layout_tracks);
                    const auto validation = mycds::MetadataMatcher::validate(disc_info, layout_tracks, record);
                    if (validation.valid) {
                        resolved = record;
                        found = true;
                    }
                }
            }
        } catch (...) {
            net_err = true;
        }

        std::lock_guard<std::mutex> lock(cddb_mutex_);
        if (cddb_result_ && cddb_result_->generation == gen) {
            cddb_result_->found = found;
            cddb_result_->network_error = net_err;
            cddb_result_->record = std::move(resolved);
            cddb_result_->ready = true;
        }
    });
}

void CdDisplayModel::poll_async_cddb_lookup() {
    std::unique_ptr<AsyncCddbResult> completed;
    {
        std::lock_guard<std::mutex> lock(cddb_mutex_);
        if (cddb_result_ && cddb_result_->ready) {
            completed = std::move(cddb_result_);
        }
    }
    if (!completed) {
        return;
    }

    if (cddb_thread_.joinable()) {
        cddb_thread_.join();
    }

    if (completed->generation != cddb_generation_.load(std::memory_order_acquire) || !has_real_source_) {
        return;
    }

    if (completed->network_error) {
        connectivity_ = ConnectivityStatus::Disconnected;
        cddb_status_ = CddbStatus::Failed;
        return;
    }

    connectivity_ = ConnectivityStatus::Connected;
    cddb_status_ = CddbStatus::Succeeded;
    if (completed->found) {
        mycds::MetadataMatcher::applyToLayout(disc_layout_, completed->record);
        apply_layout_metadata();
        if (controller_) {
            controller_->updateMetadata(disc_layout_.discMetadata, disc_layout_.trackMetadata);
        }
        try {
            const auto save_path = resolve_gui_sidecar_save_path(disc_layout_);
            mycds::MetadataStore::saveSidecar(
                save_path,
                mycds::MetadataRecord{
                    .disc = disc_layout_.discMetadata,
                    .tracks = disc_layout_.trackMetadata,
                    .remoteTrackFrameOffsets = completed->record.remoteTrackFrameOffsets,
                    .remoteDiscLengthSeconds = completed->record.remoteDiscLengthSeconds,
                });
        } catch (...) {
        }
    }
}

void CdDisplayModel::update_vu_ballistics(double target_left, double target_right, double dt) {
    constexpr double attack_rate = 32.0;
    constexpr double decay_rate = 7.5;

    if (target_left > volume_levels_.left) {
        volume_levels_.left += (target_left - volume_levels_.left) * std::min(1.0, dt * attack_rate);
    } else {
        volume_levels_.left += (target_left - volume_levels_.left) * std::min(1.0, dt * decay_rate);
    }

    if (target_right > volume_levels_.right) {
        volume_levels_.right += (target_right - volume_levels_.right) * std::min(1.0, dt * attack_rate);
    } else {
        volume_levels_.right += (target_right - volume_levels_.right) * std::min(1.0, dt * decay_rate);
    }

    if (volume_levels_.left >= volume_levels_.left_peak) {
        volume_levels_.left_peak = volume_levels_.left;
        left_peak_timer_ = 0.8;
    } else if (left_peak_timer_ > 0.0) {
        left_peak_timer_ -= dt;
    } else {
        volume_levels_.left_peak = std::max(volume_levels_.left,
                                            volume_levels_.left_peak - dt * 0.8);
    }

    if (volume_levels_.right >= volume_levels_.right_peak) {
        volume_levels_.right_peak = volume_levels_.right;
        right_peak_timer_ = 0.8;
    } else if (right_peak_timer_ > 0.0) {
        right_peak_timer_ -= dt;
    } else {
        volume_levels_.right_peak = std::max(volume_levels_.right,
                                             volume_levels_.right_peak - dt * 0.8);
    }

    volume_levels_.left = std::clamp(volume_levels_.left, 0.0, 1.0);
    volume_levels_.right = std::clamp(volume_levels_.right, 0.0, 1.0);
    volume_levels_.left_peak = std::clamp(volume_levels_.left_peak, 0.0, 1.0);
    volume_levels_.right_peak = std::clamp(volume_levels_.right_peak, 0.0, 1.0);
}

bool CdDisplayModel::is_playable_track_index(size_t index) const noexcept {
    if (index >= tracks_.size()) {
        return false;
    }
    if (!has_real_source_ || index >= disc_layout_.tracks.size()) {
        return true;
    }
    return disc_layout_.tracks[index].type == mycds::TrackType::Audio;
}

int CdDisplayModel::first_playable_track_number() const noexcept {
    for (size_t i = 0; i < tracks_.size(); ++i) {
        if (is_playable_track_index(i)) {
            return tracks_[i].number;
        }
    }
    return tracks_.empty() ? 0 : tracks_.front().number;
}

int CdDisplayModel::pick_random_playable_track() const noexcept {
    std::vector<int> candidates;
    candidates.reserve(tracks_.size());
    const int cur_num = current_track_number();
    for (size_t i = 0; i < tracks_.size(); ++i) {
        if (is_playable_track_index(i) && tracks_[i].number != cur_num) {
            candidates.push_back(tracks_[i].number);
        }
    }
    if (candidates.empty()) {
        return cur_num > 0 ? cur_num : first_playable_track_number();
    }
    return candidates[static_cast<size_t>(std::rand()) % candidates.size()];
}

void CdDisplayModel::update(double delta_time_seconds) {
    if (delta_time_seconds < 0.0) {
        return;
    }
    uptime_seconds_ += delta_time_seconds;

    poll_async_open();

    if (has_real_source_ && controller_) {
        poll_async_cddb_lookup();

        if (pending_seek_active_) {
            ++pending_seek_idle_frames_;
            if (pending_seek_idle_frames_ >= 2) {
                try {
                    controller_->seekToLba(pending_seek_lba_);
                    if (pending_seek_was_playing_) {
                        controller_->play();
                    }
                } catch (const std::exception& ex) {
                    playback_error_ = ex.what();
                }
                pending_seek_active_ = false;
                pending_seek_was_playing_ = false;
                pending_seek_idle_frames_ = 0;
            }
        }

        const auto st = controller_->status();
        if (st.state == mycds::PlaybackState::Error) {
            playing_ = false;
            playback_error_ = st.errorMessage;
            reset_volume_levels();
            return;
        }

        playback_error_.clear();
        playing_ = pending_seek_active_
            ? pending_seek_was_playing_
            : (st.state == mycds::PlaybackState::Playing);
        const int active_track_before_sync = current_track_number();

        if (!pending_seek_active_ && !(st.state == mycds::PlaybackState::Stopped && st.endOfDisc)) {
            if (st.position.trackNumber.has_value()) {
                for (size_t i = 0; i < tracks_.size(); ++i) {
                    if (tracks_[i].number == *st.position.trackNumber) {
                        current_track_index_ = i;
                        break;
                    }
                }
            }
            if (st.position.trackRelativeSectors >= 0) {
                const double frames =
                    static_cast<double>(st.position.trackRelativeSectors) * mycds::AudioFormat::kFramesPerSector +
                    static_cast<double>(st.position.frameInSector);
                track_elapsed_seconds_ = frames / static_cast<double>(mycds::AudioFormat::kSampleRate);
            } else {
                track_elapsed_seconds_ = 0.0;
            }

            // If single-track mode was toggled on mid-playback after next track was buffered,
            // enforce RepeatMode::Track / Shuffle transition at track boundary.
            if (playing_ && st.position.trackNumber.has_value() &&
                active_track_before_sync > 0 && *st.position.trackNumber != active_track_before_sync) {
                if (repeat_mode_ == RepeatMode::Track) {
                    try {
                        controller_->seekToTrack(active_track_before_sync);
                        for (size_t i = 0; i < tracks_.size(); ++i) {
                            if (tracks_[i].number == active_track_before_sync) {
                                current_track_index_ = i;
                                break;
                            }
                        }
                        track_elapsed_seconds_ = 0.0;
                    } catch (...) {
                    }
                } else if (shuffle_) {
                    try {
                        const int next_rand = pick_random_playable_track();
                        controller_->seekToTrack(next_rand);
                        for (size_t i = 0; i < tracks_.size(); ++i) {
                            if (tracks_[i].number == next_rand) {
                                current_track_index_ = i;
                                break;
                            }
                        }
                        track_elapsed_seconds_ = 0.0;
                    } catch (...) {
                    }
                }
            }
        }

        if (st.state == mycds::PlaybackState::Stopped && st.endOfDisc) {
            try {
                if (repeat_mode_ == RepeatMode::Track) {
                    const int target = active_track_before_sync > 0
                        ? active_track_before_sync
                        : first_playable_track_number();
                    controller_->seekToTrack(target);
                    controller_->play();
                    playing_ = true;
                    track_elapsed_seconds_ = 0.0;
                } else if (shuffle_) {
                    const int next_rand = pick_random_playable_track();
                    controller_->seekToTrack(next_rand);
                    for (size_t i = 0; i < tracks_.size(); ++i) {
                        if (tracks_[i].number == next_rand) {
                            current_track_index_ = i;
                            break;
                        }
                    }
                    controller_->play();
                    playing_ = true;
                    track_elapsed_seconds_ = 0.0;
                } else if (repeat_mode_ == RepeatMode::Disc) {
                    const int first_trk = first_playable_track_number();
                    controller_->seekToTrack(first_trk);
                    for (size_t i = 0; i < tracks_.size(); ++i) {
                        if (tracks_[i].number == first_trk) {
                            current_track_index_ = i;
                            break;
                        }
                    }
                    controller_->play();
                    playing_ = true;
                    track_elapsed_seconds_ = 0.0;
                } else {
                    controller_->stop();
                    playing_ = false;
                    current_track_index_ = 0;
                    track_elapsed_seconds_ = 0.0;
                }
            } catch (const std::exception& ex) {
                playback_error_ = ex.what();
                playing_ = false;
            }
        }

        if (playing_) {
            const auto levels = controller_->meterLevels();
            update_vu_ballistics(levels.left, levels.right, delta_time_seconds);
        } else {
            reset_volume_levels();
        }
        return;
    }

    if (recording_) {
        if (tracks_.empty()) {
            recording_ = false;
            current_track_index_ = 0;
            track_elapsed_seconds_ = 0.0;
        } else {
            // High-speed optical CD image ripping simulation across all tracks
            constexpr double kRecordSeekSpeed = 350.0;
            track_elapsed_seconds_ += delta_time_seconds * kRecordSeekSpeed;
            const int duration = track_duration_seconds();
            if (track_elapsed_seconds_ >= static_cast<double>(duration)) {
                if (current_track_index_ + 1 < tracks_.size()) {
                    ++current_track_index_;
                    track_elapsed_seconds_ = 0.0;
                } else {
                    recording_ = false;
                    current_track_index_ = 0;
                    track_elapsed_seconds_ = 0.0;
                    playing_ = false;
                    reset_volume_levels();
                }
            }
        }
    } else if (playing_ && !tracks_.empty()) {
        track_elapsed_seconds_ += delta_time_seconds;
        const int duration = track_duration_seconds();
        if (track_elapsed_seconds_ >= static_cast<double>(duration)) {
            if (repeat_mode_ == RepeatMode::Track) {
                track_elapsed_seconds_ = 0.0;
            } else {
                next_track();
            }
        }
    }

    if (!playing_ || tracks_.empty() || recording_) {
        reset_volume_levels();
    } else {
        // Audio synthesis simulation for volume meter (mock mode when no real source is open)
        const double t = uptime_seconds_;
        constexpr double bpm = 124.0;
        constexpr double beat_freq = bpm / 60.0;
        const double beat_phase = std::fmod(t * beat_freq, 1.0);
        const double kick = std::exp(-beat_phase * 6.5);

        const double snare_phase = std::fmod((t * beat_freq) + 0.5, 1.0);
        const double snare = (snare_phase < 0.3) ? (std::exp(-snare_phase * 11.0) * 0.75) : 0.0;

        const double bassline = 0.5 + 0.5 * std::sin(t * 3.7);
        const double lead = 0.5 + 0.5 * std::sin(t * 1.9 + 1.2);
        const double hihat = 0.5 + 0.5 * std::sin(t * 19.0) * std::cos(t * 8.0);
        const double macro = 0.74 + 0.20 * std::sin(t * 0.16) + 0.06 * std::cos(t * 0.05);
        const double raw = (0.42 * kick + 0.26 * snare + 0.14 * bassline + 0.12 * lead + 0.06 * hihat) * macro;

        const double pan_l = 0.5 + 0.35 * std::sin(t * 1.15 + 0.6);
        const double pan_r = 0.5 + 0.35 * std::cos(t * 1.35 + 0.3);
        const double bal_left = (balance_ > 0.0) ? (1.0 - balance_) : 1.0;
        const double bal_right = (balance_ < 0.0) ? (1.0 + balance_) : 1.0;
        const double vol_gain = volume_;

        const double target_left = (vol_gain <= 0.001 || bal_left <= 0.001)
                                       ? 0.0
                                       : std::clamp(raw * (0.8 + 0.4 * pan_l) * vol_gain * bal_left, 0.04, 0.98);
        const double target_right = (vol_gain <= 0.001 || bal_right <= 0.001)
                                        ? 0.0
                                        : std::clamp(raw * (0.8 + 0.4 * pan_r) * vol_gain * bal_right, 0.04, 0.98);

        update_vu_ballistics(target_left, target_right, delta_time_seconds);
    }
}

void CdDisplayModel::play() {
    if (recording_ || tracks_.empty()) {
        return;
    }
    if (has_real_source_ && controller_) {
        try {
            if (controller_->state() == mycds::PlaybackState::Error) {
                const int trk = current_track_number() > 0
                    ? current_track_number()
                    : first_playable_track_number();
                controller_->seekToTrack(trk);
            }
            controller_->play();
            playing_ = true;
            playback_error_.clear();
        } catch (const std::exception& ex) {
            playback_error_ = ex.what();
            playing_ = false;
        }
        return;
    }
    playing_ = true;
}

void CdDisplayModel::pause() {
    if (recording_) {
        return;
    }
    if (has_real_source_ && controller_) {
        try {
            controller_->pause();
        } catch (const std::exception& ex) {
            playback_error_ = ex.what();
        }
    }
    playing_ = false;
    reset_volume_levels();
}

void CdDisplayModel::stop() {
    recording_ = false;
    playing_ = false;
    pending_seek_active_ = false;
    pending_seek_was_playing_ = false;
    pending_seek_idle_frames_ = 0;
    if (has_real_source_ && controller_) {
        controller_->stop();
        playback_error_.clear();
    }
    current_track_index_ = 0;
    for (size_t i = 0; i < tracks_.size(); ++i) {
        if (is_playable_track_index(i)) {
            current_track_index_ = i;
            break;
        }
    }
    track_elapsed_seconds_ = 0.0;
    reset_volume_levels();
}

void CdDisplayModel::toggle_play_pause() {
    if (recording_ || tracks_.empty()) {
        return;
    }
    if (playing_) {
        pause();
    } else {
        play();
    }
}

void CdDisplayModel::seek_forward(double seconds) {
    if (recording_ || seconds <= 0.0 || tracks_.empty()) {
        return;
    }
    if (has_real_source_ && controller_ && !disc_layout_.tracks.empty()) {
        const std::int64_t min_lba = disc_layout_.discInfo.startLba;
        const std::int64_t end_lba = mycds::compute_contiguous_audio_end_lba(
            disc_layout_.tracks, min_lba);
        const std::int64_t max_lba = std::max(min_lba, end_lba - 1);

        if (!pending_seek_active_) {
            pending_seek_lba_ = std::clamp(controller_->currentLba(), min_lba, max_lba);
            pending_seek_was_playing_ = playing_;
            if (playing_) {
                try {
                    controller_->pause();
                } catch (...) {
                }
            }
        }
        const std::int64_t delta = std::max<std::int64_t>(
            1,
            static_cast<std::int64_t>(std::llround(seconds * mycds::AudioFormat::kSectorsPerSecond)));
        pending_seek_lba_ = std::clamp(pending_seek_lba_ + delta, min_lba, max_lba);
        pending_seek_active_ = true;
        pending_seek_idle_frames_ = 0;

        const auto pos = mycds::resolve_playback_position(
            disc_layout_.discInfo, disc_layout_.tracks, pending_seek_lba_, 0);
        if (pos.trackNumber.has_value()) {
            for (size_t i = 0; i < tracks_.size(); ++i) {
                if (tracks_[i].number == *pos.trackNumber) {
                    current_track_index_ = i;
                    break;
                }
            }
        }
        track_elapsed_seconds_ = (pos.trackRelativeSectors >= 0)
            ? (static_cast<double>(pos.trackRelativeSectors) / mycds::AudioFormat::kSectorsPerSecond)
            : 0.0;
        return;
    }

    track_elapsed_seconds_ += seconds;
    const int duration = track_duration_seconds();
    if (track_elapsed_seconds_ >= static_cast<double>(duration)) {
        if (current_track_index_ + 1 < tracks_.size()) {
            next_track();
        } else {
            track_elapsed_seconds_ = std::max(0.0, static_cast<double>(duration) - 0.1);
        }
    }
}

void CdDisplayModel::seek_backward(double seconds) {
    if (recording_ || seconds <= 0.0 || tracks_.empty()) {
        return;
    }
    if (has_real_source_ && controller_ && !disc_layout_.tracks.empty()) {
        const std::int64_t min_lba = disc_layout_.discInfo.startLba;
        const std::int64_t end_lba = mycds::compute_contiguous_audio_end_lba(
            disc_layout_.tracks, min_lba);
        const std::int64_t max_lba = std::max(min_lba, end_lba - 1);

        if (!pending_seek_active_) {
            pending_seek_lba_ = std::clamp(controller_->currentLba(), min_lba, max_lba);
            pending_seek_was_playing_ = playing_;
            if (playing_) {
                try {
                    controller_->pause();
                } catch (...) {
                }
            }
        }
        const std::int64_t delta = std::max<std::int64_t>(
            1,
            static_cast<std::int64_t>(std::llround(seconds * mycds::AudioFormat::kSectorsPerSecond)));
        pending_seek_lba_ = std::clamp(pending_seek_lba_ - delta, min_lba, max_lba);
        pending_seek_active_ = true;
        pending_seek_idle_frames_ = 0;

        const auto pos = mycds::resolve_playback_position(
            disc_layout_.discInfo, disc_layout_.tracks, pending_seek_lba_, 0);
        if (pos.trackNumber.has_value()) {
            for (size_t i = 0; i < tracks_.size(); ++i) {
                if (tracks_[i].number == *pos.trackNumber) {
                    current_track_index_ = i;
                    break;
                }
            }
        }
        track_elapsed_seconds_ = (pos.trackRelativeSectors >= 0)
            ? (static_cast<double>(pos.trackRelativeSectors) / mycds::AudioFormat::kSectorsPerSecond)
            : 0.0;
        return;
    }

    track_elapsed_seconds_ -= seconds;
    if (track_elapsed_seconds_ < 0.0) {
        if (current_track_index_ > 0) {
            --current_track_index_;
            track_elapsed_seconds_ = std::max(0.0, static_cast<double>(track_duration_seconds()) + track_elapsed_seconds_);
        } else {
            track_elapsed_seconds_ = 0.0;
        }
    }
}

void CdDisplayModel::seek_to_track_fraction(double fraction) {
    if (recording_ || tracks_.empty()) {
        return;
    }
    const double clamped = std::clamp(fraction, 0.0, 1.0);

    if (has_real_source_ && controller_ && !disc_layout_.tracks.empty()) {
        pending_seek_active_ = false;
        pending_seek_was_playing_ = false;
        pending_seek_idle_frames_ = 0;

        const int trk_num = current_track_number();
        const mycds::TrackInfo* target_trk = mycds::find_track_by_number(disc_layout_.tracks, trk_num);
        if (!target_trk || target_trk->type != mycds::TrackType::Audio) {
            return;
        }

        const std::int64_t track_sectors = target_trk->endLba - target_trk->index01Lba;
        if (track_sectors <= 0) {
            return;
        }

        const std::int64_t offset_sectors = std::clamp(
            static_cast<std::int64_t>(std::llround(clamped * static_cast<double>(track_sectors))),
            std::int64_t{0},
            track_sectors - 1);
        const std::int64_t target_lba = target_trk->index01Lba + offset_sectors;

        try {
            controller_->seekToLba(target_lba);
            track_elapsed_seconds_ =
                static_cast<double>(offset_sectors) / mycds::AudioFormat::kSectorsPerSecond;
            playback_error_.clear();
        } catch (const std::exception& ex) {
            playback_error_ = ex.what();
        }
        return;
    }

    const int dur = track_duration_seconds();
    if (dur <= 0) {
        track_elapsed_seconds_ = 0.0;
        return;
    }
    track_elapsed_seconds_ = std::clamp(
        clamped * static_cast<double>(dur),
        0.0,
        std::max(0.0, static_cast<double>(dur) - 0.1));
}

void CdDisplayModel::seek_to_track_and_seconds(int track_number, double seconds) {
    if (recording_ || tracks_.empty()) {
        return;
    }
    const double clamped_sec = std::max(0.0, seconds);

    if (has_real_source_ && controller_ && !disc_layout_.tracks.empty()) {
        pending_seek_active_ = false;
        const mycds::TrackInfo* target_trk = mycds::find_track_by_number(disc_layout_.tracks, track_number);
        if (!target_trk || target_trk->type != mycds::TrackType::Audio) {
            return;
        }
        const std::int64_t track_sectors = target_trk->endLba - target_trk->index01Lba;
        if (track_sectors <= 0) {
            return;
        }
        const std::int64_t offset_sectors = std::clamp(
            static_cast<std::int64_t>(std::llround(clamped_sec * mycds::AudioFormat::kSectorsPerSecond)),
            std::int64_t{0},
            track_sectors - 1);
        const std::int64_t target_lba = target_trk->index01Lba + offset_sectors;
        try {
            controller_->seekToLba(target_lba);
            for (size_t i = 0; i < tracks_.size(); ++i) {
                if (tracks_[i].number == track_number) {
                    current_track_index_ = i;
                    break;
                }
            }
            track_elapsed_seconds_ =
                static_cast<double>(offset_sectors) / mycds::AudioFormat::kSectorsPerSecond;
            playback_error_.clear();
        } catch (const std::exception& ex) {
            playback_error_ = ex.what();
        }
        return;
    }

    jump_to_track(track_number);
    if (clamped_sec > 0.0) {
        const int dur = track_duration_seconds();
        track_elapsed_seconds_ = std::min(clamped_sec, std::max(0.0, static_cast<double>(dur) - 0.1));
    }
}

void CdDisplayModel::seek_to_disc_seconds(double seconds) {
    if (recording_ || tracks_.empty()) {
        return;
    }
    const double clamped_sec = std::max(0.0, seconds);

    if (has_real_source_ && controller_ && !disc_layout_.tracks.empty()) {
        pending_seek_active_ = false;
        const int first_trk_num = first_playable_track_number();
        const mycds::TrackInfo* first_trk = mycds::find_track_by_number(disc_layout_.tracks, first_trk_num);
        const std::int64_t start_lba = first_trk ? first_trk->index01Lba : disc_layout_.discInfo.startLba;
        const std::int64_t end_lba = mycds::compute_contiguous_audio_end_lba(disc_layout_.tracks, start_lba);
        if (end_lba <= start_lba) {
            return;
        }
        const std::int64_t offset_sectors = static_cast<std::int64_t>(
            std::llround(clamped_sec * mycds::AudioFormat::kSectorsPerSecond));
        const std::int64_t target_lba = std::clamp(start_lba + offset_sectors, start_lba, end_lba - 1);
        try {
            controller_->seekToLba(target_lba);
            const auto pos = mycds::resolve_playback_position(
                disc_layout_.discInfo, disc_layout_.tracks, target_lba, 0);
            if (pos.trackNumber.has_value()) {
                for (size_t i = 0; i < tracks_.size(); ++i) {
                    if (tracks_[i].number == *pos.trackNumber) {
                        current_track_index_ = i;
                        break;
                    }
                }
            }
            track_elapsed_seconds_ = (pos.trackRelativeSectors >= 0)
                ? (static_cast<double>(pos.trackRelativeSectors) / mycds::AudioFormat::kSectorsPerSecond)
                : 0.0;
            playback_error_.clear();
        } catch (const std::exception& ex) {
            playback_error_ = ex.what();
        }
        return;
    }

    double remaining = clamped_sec;
    for (size_t i = 0; i < tracks_.size(); ++i) {
        const double dur = static_cast<double>(tracks_[i].duration_seconds);
        if (remaining < dur || i + 1 == tracks_.size()) {
            current_track_index_ = i;
            track_elapsed_seconds_ = std::min(remaining, std::max(0.0, dur - 0.1));
            return;
        }
        remaining -= dur;
    }
}

void CdDisplayModel::start_recording() {
    if (recording_ || tracks_.empty()) {
        return;
    }
    recording_ = true;
    playing_ = false;
    current_track_index_ = 0;
    track_elapsed_seconds_ = 0.0;
    reset_volume_levels();
}

void CdDisplayModel::stop_recording() {
    if (!recording_) {
        return;
    }
    recording_ = false;
    current_track_index_ = 0;
    track_elapsed_seconds_ = 0.0;
    playing_ = false;
    reset_volume_levels();
}

bool CdDisplayModel::is_recording() const {
    return recording_;
}

void CdDisplayModel::toggle_shuffle() {
    shuffle_ = !shuffle_;
    if (has_real_source_ && controller_) {
        controller_->setSingleTrackMode(repeat_mode_ == RepeatMode::Track || shuffle_);
    }
}

void CdDisplayModel::toggle_repeat() {
    switch (repeat_mode_) {
        case RepeatMode::Off:
            repeat_mode_ = RepeatMode::Disc;
            break;
        case RepeatMode::Disc:
            repeat_mode_ = RepeatMode::Track;
            break;
        case RepeatMode::Track:
            repeat_mode_ = RepeatMode::Off;
            break;
    }
    if (has_real_source_ && controller_) {
        controller_->setSingleTrackMode(repeat_mode_ == RepeatMode::Track || shuffle_);
    }
}

void CdDisplayModel::set_repeat_mode(RepeatMode mode) {
    repeat_mode_ = mode;
    if (has_real_source_ && controller_) {
        controller_->setSingleTrackMode(repeat_mode_ == RepeatMode::Track || shuffle_);
    }
}

void CdDisplayModel::next_track() {
    if (recording_) {
        return;
    }
    track_elapsed_seconds_ = 0.0;
    if (tracks_.empty()) {
        return;
    }

    if (has_real_source_ && controller_) {
        pending_seek_active_ = false;
        int target_track_num = current_track_number();
        bool should_stop = false;

        if (shuffle_ && tracks_.size() > 1) {
            target_track_num = pick_random_playable_track();
        } else {
            size_t next_idx = current_track_index_ + 1;
            while (next_idx < tracks_.size() && !is_playable_track_index(next_idx)) {
                ++next_idx;
            }
            if (next_idx < tracks_.size()) {
                target_track_num = tracks_[next_idx].number;
            } else {
                target_track_num = first_playable_track_number();
                if (repeat_mode_ == RepeatMode::Off) {
                    should_stop = true;
                }
            }
        }

        if (should_stop) {
            stop();
            return;
        }

        try {
            controller_->seekToTrack(target_track_num);
            for (size_t i = 0; i < tracks_.size(); ++i) {
                if (tracks_[i].number == target_track_num) {
                    current_track_index_ = i;
                    break;
                }
            }
            playback_error_.clear();
        } catch (const std::exception& ex) {
            playback_error_ = ex.what();
        }
        return;
    }

    if (shuffle_ && tracks_.size() > 1) {
        const size_t offset = 1 + (static_cast<size_t>(std::rand()) % (tracks_.size() - 1));
        current_track_index_ = (current_track_index_ + offset) % tracks_.size();
    } else {
        if (current_track_index_ + 1 < tracks_.size()) {
            ++current_track_index_;
        } else {
            current_track_index_ = 0;
            if (repeat_mode_ == RepeatMode::Off) {
                playing_ = false;
                reset_volume_levels();
            }
        }
    }
}

void CdDisplayModel::prev_track() {
    if (recording_) {
        return;
    }

    if (has_real_source_ && controller_) {
        pending_seek_active_ = false;
        if (tracks_.empty()) {
            return;
        }
        int target_track_num = current_track_number();
        if (track_elapsed_seconds_ <= 2.0) {
            if (current_track_index_ > 0) {
                size_t prev_idx = current_track_index_;
                do {
                    --prev_idx;
                    if (is_playable_track_index(prev_idx)) {
                        target_track_num = tracks_[prev_idx].number;
                        break;
                    }
                } while (prev_idx > 0);
            } else {
                for (size_t idx = tracks_.size(); idx > 0; --idx) {
                    if (is_playable_track_index(idx - 1)) {
                        target_track_num = tracks_[idx - 1].number;
                        break;
                    }
                }
            }
        }

        try {
            controller_->seekToTrack(target_track_num);
            for (size_t i = 0; i < tracks_.size(); ++i) {
                if (tracks_[i].number == target_track_num) {
                    current_track_index_ = i;
                    break;
                }
            }
            track_elapsed_seconds_ = 0.0;
            playback_error_.clear();
        } catch (const std::exception& ex) {
            playback_error_ = ex.what();
        }
        return;
    }

    if (track_elapsed_seconds_ > 2.0) {
        track_elapsed_seconds_ = 0.0;
        return;
    }

    track_elapsed_seconds_ = 0.0;
    if (tracks_.empty()) {
        return;
    }

    if (current_track_index_ > 0) {
        --current_track_index_;
    } else {
        current_track_index_ = tracks_.size() - 1;
    }
}

void CdDisplayModel::jump_to_track(int track_number) {
    if (recording_) {
        return;
    }
    if (has_real_source_ && controller_) {
        pending_seek_active_ = false;
        for (size_t i = 0; i < tracks_.size(); ++i) {
            if (tracks_[i].number == track_number) {
                if (!is_playable_track_index(i)) {
                    return;
                }
                try {
                    controller_->seekToTrack(track_number);
                    current_track_index_ = i;
                    track_elapsed_seconds_ = 0.0;
                    playback_error_.clear();
                } catch (const std::exception& ex) {
                    playback_error_ = ex.what();
                }
                return;
            }
        }
        return;
    }

    if (track_number < 1 || track_number > static_cast<int>(tracks_.size())) {
        return;
    }
    current_track_index_ = static_cast<size_t>(track_number - 1);
    track_elapsed_seconds_ = 0.0;
}

void CdDisplayModel::set_connectivity(ConnectivityStatus status) {
    connectivity_ = status;
}

void CdDisplayModel::toggle_connectivity() {
    connectivity_ = (connectivity_ == ConnectivityStatus::Connected)
                        ? ConnectivityStatus::Disconnected
                        : ConnectivityStatus::Connected;
}

bool CdDisplayModel::is_playing() const {
    return playing_;
}

RepeatMode CdDisplayModel::repeat_mode() const {
    return repeat_mode_;
}

bool CdDisplayModel::is_shuffle() const {
    return shuffle_;
}

ConnectivityStatus CdDisplayModel::connectivity_status() const {
    return connectivity_;
}

int CdDisplayModel::current_track_number() const {
    if (tracks_.empty() || current_track_index_ >= tracks_.size()) {
        return 0;
    }
    return tracks_[current_track_index_].number;
}

int CdDisplayModel::total_tracks() const {
    return static_cast<int>(tracks_.size());
}

size_t CdDisplayModel::current_track_index() const {
    return current_track_index_;
}

const std::vector<TrackInfo>& CdDisplayModel::tracks() const {
    return tracks_;
}

std::string CdDisplayModel::track_display_title(size_t track_index) const {
    if (track_index >= tracks_.size()) {
        return {};
    }
    if (!has_cddb_data() || tracks_[track_index].title.empty()) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Track %d", tracks_[track_index].number);
        return std::string(buf);
    }
    return tracks_[track_index].title;
}

double CdDisplayModel::track_elapsed_seconds() const {
    return track_elapsed_seconds_;
}

int CdDisplayModel::track_duration_seconds() const {
    if (tracks_.empty() || current_track_index_ >= tracks_.size()) {
        return 0;
    }
    return tracks_[current_track_index_].duration_seconds;
}

int CdDisplayModel::elapsed_minutes() const {
    const int total_s = static_cast<int>(std::floor(track_elapsed_seconds_));
    return total_s / 60;
}

int CdDisplayModel::elapsed_seconds() const {
    const int total_s = static_cast<int>(std::floor(track_elapsed_seconds_));
    return total_s % 60;
}

double CdDisplayModel::uptime_seconds() const {
    return uptime_seconds_;
}

bool CdDisplayModel::is_cloud_active() const {
    return false;
}

bool CdDisplayModel::is_cddb_active() const {
    return cddb_status_ != CddbStatus::Inactive;
}

CddbStatus CdDisplayModel::cddb_status() const {
    return cddb_status_;
}

void CdDisplayModel::retry_cddb_lookup() {
    if (!has_real_source_ || disc_layout_.tracks.empty()) {
        return;
    }
    if (cddb_status_ == CddbStatus::Querying) {
        return;
    }
    connectivity_ = ConnectivityStatus::Connected;
    start_async_cddb_lookup();
}

void CdDisplayModel::set_cddb_transport(std::shared_ptr<mycds::CddbHttpTransport> transport) {
    cddb_transport_ = std::move(transport);
}

VolumeLevels CdDisplayModel::volume_levels() const {
    return volume_levels_;
}

double CdDisplayModel::left_level() const {
    return volume_levels_.left;
}

double CdDisplayModel::right_level() const {
    return volume_levels_.right;
}

double CdDisplayModel::left_peak() const {
    return volume_levels_.left_peak;
}

double CdDisplayModel::right_peak() const {
    return volume_levels_.right_peak;
}

void CdDisplayModel::reset_volume_levels() {
    volume_levels_ = {0.0, 0.0, 0.0, 0.0};
    left_peak_timer_ = 0.0;
    right_peak_timer_ = 0.0;
}

void CdDisplayModel::set_volume(double vol) {
    volume_ = std::clamp(vol, 0.0, 1.0);
    if (controller_) {
        controller_->setVolume(volume_);
    }
}

double CdDisplayModel::volume() const {
    return volume_;
}

void CdDisplayModel::set_balance(double bal) {
    balance_ = std::clamp(bal, -1.0, 1.0);
    if (controller_) {
        controller_->setBalance(balance_);
    }
}

double CdDisplayModel::balance() const {
    return balance_;
}

void CdDisplayModel::open_eject_menu() {
    eject_menu_open_ = true;
}

void CdDisplayModel::close_eject_menu() {
    eject_menu_open_ = false;
}

bool CdDisplayModel::is_eject_menu_open() const {
    return eject_menu_open_;
}

void CdDisplayModel::set_active_eject_screen(EjectMenuScreen screen) {
    active_eject_screen_ = screen;
}

EjectMenuScreen CdDisplayModel::active_eject_screen() const {
    return active_eject_screen_;
}

void CdDisplayModel::set_app_settings(const AppSettings& settings) {
    app_settings_ = settings;
}

const AppSettings& CdDisplayModel::app_settings() const {
    return app_settings_;
}

void CdDisplayModel::set_play_when_started(bool enabled) {
    app_settings_.play_when_started = enabled;
}

void CdDisplayModel::set_auto_play_on_disc_load(bool enabled) {
    app_settings_.auto_play_on_disc_load = enabled;
}

void CdDisplayModel::load_disc(const std::string& album_title, const std::vector<TrackInfo>& tracks) {
    load_disc(artist_name_, album_title, tracks);
}

void CdDisplayModel::load_disc(const std::string& artist_name, const std::string& album_title, const std::vector<TrackInfo>& tracks) {
    if (tracks.empty()) {
        return;
    }
    if (has_real_source_ && controller_) {
        cancel_async_cddb_lookup();
        controller_->stop();
        controller_->closeSource();
        has_real_source_ = false;
        disc_layout_ = mycds::DiscLayout{};
    }
    artist_name_ = artist_name;
    album_title_ = album_title;
    tracks_ = tracks;
    cddb_status_ = CddbStatus::Inactive;
    recording_ = false;
    current_track_index_ = 0;
    track_elapsed_seconds_ = 0.0;
    playing_ = app_settings_.auto_play_on_disc_load;
    eject_menu_open_ = false;
}

const std::string& CdDisplayModel::artist_name() const {
    return artist_name_;
}

const std::string& CdDisplayModel::album_title() const {
    return album_title_;
}

void CdDisplayModel::set_artist_name(const std::string& artist) {
    artist_name_ = artist;
}

void CdDisplayModel::set_album_title(const std::string& title) {
    album_title_ = title;
}

bool CdDisplayModel::has_cddb_data() const {
    const bool is_connected = (connectivity_ == ConnectivityStatus::Connected);
    return has_cddb_data_ && is_connected && !artist_name_.empty() && !album_title_.empty();
}

void CdDisplayModel::set_has_cddb_data(bool has_data) {
    has_cddb_data_ = has_data;
}

} // namespace mycds::gui