#include "cd_display_model.h"

#include <cmath>
#include <cstdlib>

namespace mycds::gui {

CdDisplayModel::CdDisplayModel() {
    tracks_ = {
        {1, 222},  // 03:42
        {2, 255},  // 04:15
        {3, 178},  // 02:58
        {4, 321},  // 05:21
        {5, 192},  // 03:12
        {6, 288},  // 04:48
        {7, 364},  // 06:04
        {8, 213},  // 03:33
        {9, 260},  // 04:20
        {10, 165}, // 02:45
        {11, 310}, // 05:10
        {12, 230}  // 03:50
    };
}

void CdDisplayModel::update(double delta_time_seconds) {
    if (delta_time_seconds < 0.0) {
        return;
    }
    uptime_seconds_ += delta_time_seconds;

    if (playing_ && !tracks_.empty()) {
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
}

void CdDisplayModel::play() {
    playing_ = true;
}

void CdDisplayModel::pause() {
    playing_ = false;
}

void CdDisplayModel::toggle_play_pause() {
    playing_ = !playing_;
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
            }
        }
    }
}

void CdDisplayModel::prev_track() {
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

} // namespace mycds::gui
