#pragma once

#include <cstddef>
#include <string>
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
    std::string title;
};

struct VolumeLevels {
    double left{0.0};
    double right{0.0};
    double left_peak{0.0};
    double right_peak{0.0};
};

class CdDisplayModel {
public:
    CdDisplayModel();

    void update(double delta_time_seconds);

    void play();
    void pause();
    void stop();
    void toggle_play_pause();

    void seek_forward(double seconds);
    void seek_backward(double seconds);

    void toggle_shuffle();
    void toggle_repeat();
    void set_repeat_mode(RepeatMode mode);

    void next_track();
    void prev_track();
    void jump_to_track(int track_number);

    void set_volume(double vol);
    [[nodiscard]] double volume() const;

    void set_balance(double bal);
    [[nodiscard]] double balance() const;

    void open_eject_dialog();
    void close_eject_dialog();
    [[nodiscard]] bool is_eject_dialog_open() const;

    void load_disc(const std::string& album_title, const std::vector<TrackInfo>& tracks);
    [[nodiscard]] const std::string& album_title() const;

    void set_connectivity(ConnectivityStatus status);
    void toggle_connectivity();

    [[nodiscard]] bool is_playing() const;
    [[nodiscard]] RepeatMode repeat_mode() const;
    [[nodiscard]] bool is_shuffle() const;
    [[nodiscard]] ConnectivityStatus connectivity_status() const;

    [[nodiscard]] int current_track_number() const;
    [[nodiscard]] int total_tracks() const;
    [[nodiscard]] size_t current_track_index() const;
    [[nodiscard]] const std::vector<TrackInfo>& tracks() const;

    [[nodiscard]] double track_elapsed_seconds() const;
    [[nodiscard]] int track_duration_seconds() const;
    [[nodiscard]] int elapsed_minutes() const;
    [[nodiscard]] int elapsed_seconds() const;

    [[nodiscard]] double uptime_seconds() const;
    [[nodiscard]] bool is_cloud_active() const;
    [[nodiscard]] bool is_cddb_active() const;

    [[nodiscard]] VolumeLevels volume_levels() const;
    [[nodiscard]] double left_level() const;
    [[nodiscard]] double right_level() const;
    [[nodiscard]] double left_peak() const;
    [[nodiscard]] double right_peak() const;
    void reset_volume_levels();

private:
    std::string album_title_{"The Dark Side of the Moon"};
    std::vector<TrackInfo> tracks_;
    size_t current_track_index_{0};
    double track_elapsed_seconds_{0.0};
    double uptime_seconds_{0.0};
    bool playing_{true};
    bool shuffle_{false};
    RepeatMode repeat_mode_{RepeatMode::Off};
    ConnectivityStatus connectivity_{ConnectivityStatus::Connected};
    double volume_{0.8};
    double balance_{0.0};
    bool eject_dialog_open_{false};
    VolumeLevels volume_levels_{};
    double left_peak_timer_{0.0};
    double right_peak_timer_{0.0};
};

} // namespace mycds::gui
