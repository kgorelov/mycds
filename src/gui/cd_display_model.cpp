#include "cd_display_model.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace mycds::gui {

CdDisplayModel::CdDisplayModel() {
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

void CdDisplayModel::update(double delta_time_seconds) {
    if (delta_time_seconds < 0.0) {
        return;
    }
    uptime_seconds_ += delta_time_seconds;

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
                    // Completed ripping final track: return timer to amber, reset to track 1 00:00
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
        // Audio synthesis simulation for volume meter
        const double t = uptime_seconds_;

        // Rhythmic beat pulses (~124 bpm)
        constexpr double bpm = 124.0;
        constexpr double beat_freq = bpm / 60.0;
        const double beat_phase = std::fmod(t * beat_freq, 1.0);
        const double kick = std::exp(-beat_phase * 6.5);

        const double snare_phase = std::fmod((t * beat_freq) + 0.5, 1.0);
        const double snare = (snare_phase < 0.3) ? (std::exp(-snare_phase * 11.0) * 0.75) : 0.0;

        const double bassline = 0.5 + 0.5 * std::sin(t * 3.7);
        const double lead = 0.5 + 0.5 * std::sin(t * 1.9 + 1.2);
        const double hihat = 0.5 + 0.5 * std::sin(t * 19.0) * std::cos(t * 8.0);

        // Musical section dynamics (verse / chorus intensity variations)
        const double macro = 0.74 + 0.20 * std::sin(t * 0.16) + 0.06 * std::cos(t * 0.05);

        const double raw = (0.42 * kick + 0.26 * snare + 0.14 * bassline + 0.12 * lead + 0.06 * hihat) * macro;

        // Stereo panning / decorrelation
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

        // VU meter ballistics: fast attack (~32.0/s), smooth decay (~7.5/s)
        constexpr double attack_rate = 32.0;
        constexpr double decay_rate = 7.5;

        // Left channel
        if (target_left > volume_levels_.left) {
            volume_levels_.left += (target_left - volume_levels_.left) * std::min(1.0, delta_time_seconds * attack_rate);
        } else {
            volume_levels_.left += (target_left - volume_levels_.left) * std::min(1.0, delta_time_seconds * decay_rate);
        }

        // Right channel
        if (target_right > volume_levels_.right) {
            volume_levels_.right += (target_right - volume_levels_.right) * std::min(1.0, delta_time_seconds * attack_rate);
        } else {
            volume_levels_.right += (target_right - volume_levels_.right) * std::min(1.0, delta_time_seconds * decay_rate);
        }

        // Peak hold ballistics
        if (volume_levels_.left >= volume_levels_.left_peak) {
            volume_levels_.left_peak = volume_levels_.left;
            left_peak_timer_ = 0.8;
        } else {
            if (left_peak_timer_ > 0.0) {
                left_peak_timer_ -= delta_time_seconds;
            } else {
                volume_levels_.left_peak = std::max(volume_levels_.left,
                                                    volume_levels_.left_peak - delta_time_seconds * 0.8);
            }
        }

        if (volume_levels_.right >= volume_levels_.right_peak) {
            volume_levels_.right_peak = volume_levels_.right;
            right_peak_timer_ = 0.8;
        } else {
            if (right_peak_timer_ > 0.0) {
                right_peak_timer_ -= delta_time_seconds;
            } else {
                volume_levels_.right_peak = std::max(volume_levels_.right,
                                                     volume_levels_.right_peak - delta_time_seconds * 0.8);
            }
        }

        volume_levels_.left = std::clamp(volume_levels_.left, 0.0, 1.0);
        volume_levels_.right = std::clamp(volume_levels_.right, 0.0, 1.0);
        volume_levels_.left_peak = std::clamp(volume_levels_.left_peak, 0.0, 1.0);
        volume_levels_.right_peak = std::clamp(volume_levels_.right_peak, 0.0, 1.0);
    }
}

void CdDisplayModel::play() {
    if (recording_) {
        return;
    }
    playing_ = true;
}

void CdDisplayModel::pause() {
    if (recording_) {
        return;
    }
    playing_ = false;
    reset_volume_levels();
}

void CdDisplayModel::stop() {
    recording_ = false;
    playing_ = false;
    current_track_index_ = 0;
    track_elapsed_seconds_ = 0.0;
    reset_volume_levels();
}

void CdDisplayModel::toggle_play_pause() {
    if (recording_) {
        return;
    }
    playing_ = !playing_;
    if (!playing_) {
        reset_volume_levels();
    }
}

void CdDisplayModel::seek_forward(double seconds) {
    if (recording_ || seconds <= 0.0 || tracks_.empty()) {
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
}

void CdDisplayModel::set_repeat_mode(RepeatMode mode) {
    repeat_mode_ = mode;
}

void CdDisplayModel::next_track() {
    if (recording_) {
        return;
    }
    track_elapsed_seconds_ = 0.0;
    if (tracks_.empty()) {
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
    return uptime_seconds_ >= 3.0;
}

bool CdDisplayModel::is_cddb_active() const {
    return uptime_seconds_ >= 3.0;
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
}

double CdDisplayModel::volume() const {
    return volume_;
}

void CdDisplayModel::set_balance(double bal) {
    balance_ = std::clamp(bal, -1.0, 1.0);
}

double CdDisplayModel::balance() const {
    return balance_;
}

void CdDisplayModel::open_eject_dialog() {
    eject_dialog_open_ = true;
}

void CdDisplayModel::close_eject_dialog() {
    eject_dialog_open_ = false;
}

bool CdDisplayModel::is_eject_dialog_open() const {
    return eject_dialog_open_;
}

void CdDisplayModel::load_disc(const std::string& album_title, const std::vector<TrackInfo>& tracks) {
    if (tracks.empty()) {
        return;
    }
    album_title_ = album_title;
    tracks_ = tracks;
    recording_ = false;
    current_track_index_ = 0;
    track_elapsed_seconds_ = 0.0;
    playing_ = true;
    eject_dialog_open_ = false;
}

const std::string& CdDisplayModel::album_title() const {
    return album_title_;
}

} // namespace mycds::gui
