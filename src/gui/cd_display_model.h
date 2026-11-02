#pragma once

#include <cstddef>
#include <vector>

namespace mycds::gui {

enum class RepeatMode {
    Off,
    Disc,
    Track
};

enum class ConnectivityStatus {
    Connected,
    Disconnected
};

struct TrackInfo {
    int number{1};
    int duration_seconds{222};
};

class CdDisplayModel {
public:
    CdDisplayModel();

    void update(double delta_time_seconds);

    void play();
    void pause();
    void toggle_play_pause();

    void toggle_shuffle();
    void toggle_repeat();
    void set_repeat_mode(RepeatMode mode);

    void next_track();
    void prev_track();

    void set_connectivity(ConnectivityStatus status);
    void toggle_connectivity();

    [[nodiscard]] bool is_playing() const;
    [[nodiscard]] RepeatMode repeat_mode() const;
    [[nodiscard]] bool is_shuffle() const;
    [[nodiscard]] ConnectivityStatus connectivity_status() const;

    [[nodiscard]] int current_track_number() const;
    [[nodiscard]] int total_tracks() const;

    [[nodiscard]] double track_elapsed_seconds() const;
    [[nodiscard]] int track_duration_seconds() const;
    [[nodiscard]] int elapsed_minutes() const;
    [[nodiscard]] int elapsed_seconds() const;

    [[nodiscard]] double uptime_seconds() const;
    [[nodiscard]] bool is_cloud_active() const;
    [[nodiscard]] bool is_cddb_active() const;

private:
    std::vector<TrackInfo> tracks_;
    size_t current_track_index_{0};
    double track_elapsed_seconds_{0.0};
    double uptime_seconds_{0.0};
    bool playing_{true};
    bool shuffle_{false};
    RepeatMode repeat_mode_{RepeatMode::Off};
    ConnectivityStatus connectivity_{ConnectivityStatus::Connected};
};

} // namespace mycds::gui
