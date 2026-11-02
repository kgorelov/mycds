#include "cd_display_model.h"

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

void CdDisplayModel::jump_to_track(int track_number) {
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

} // namespace mycds::gui
