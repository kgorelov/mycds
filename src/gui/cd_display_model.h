#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "domain/disc_info.hpp"
#include "playback/playback_controller.hpp"

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

enum class EjectMenuScreen {
    Cd,
    ImageFile,
    Library,
    DiskInfo,
    Settings,
    About
};

enum class LayoutMode {
    Dynamic,
    Horizontal,
    Vertical
};

enum class FileBrowserViewMode {
    List,
    Icons
};

enum class LibraryViewMode {
    List,
    Covers
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

struct AppSettings {
    bool play_when_started{true};
    bool auto_play_on_disc_load{true};
    LayoutMode layout_mode{LayoutMode::Dynamic};
    bool show_playlist{true};
    bool show_cd_cover{true};
    FileBrowserViewMode file_browser_view{FileBrowserViewMode::List};
    LibraryViewMode library_view{LibraryViewMode::List};
};

class CdDisplayModel {
public:
    CdDisplayModel();
    explicit CdDisplayModel(mycds::PlaybackControllerConfig controller_config);
    ~CdDisplayModel();

    CdDisplayModel(const CdDisplayModel&) = delete;
    CdDisplayModel& operator=(const CdDisplayModel&) = delete;

    void set_playback_controller_config(const mycds::PlaybackControllerConfig& config);

    bool open_disc_source(
        std::unique_ptr<mycds::DiscSource> source,
        std::string* out_error = nullptr);
    void eject_disc() noexcept;
    void clear_disc() noexcept;

    [[nodiscard]] bool has_disc() const noexcept;
    [[nodiscard]] bool has_real_source() const noexcept;
    [[nodiscard]] const std::string& playback_error() const noexcept;
    [[nodiscard]] const std::string& source_path() const noexcept;
    [[nodiscard]] bool source_is_device() const noexcept;

    void update(double delta_time_seconds);

    void play();
    void pause();
    void stop();
    void toggle_play_pause();

    void seek_forward(double seconds);
    void seek_backward(double seconds);
    void seek_to_track_fraction(double fraction);
    void seek_to_track_and_seconds(int track_number, double seconds);
    void seek_to_disc_seconds(double seconds);

    void start_recording();
    void stop_recording();
    [[nodiscard]] bool is_recording() const;

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

    void open_eject_menu();
    void close_eject_menu();
    [[nodiscard]] bool is_eject_menu_open() const;

    void set_active_eject_screen(EjectMenuScreen screen);
    [[nodiscard]] EjectMenuScreen active_eject_screen() const;

    void set_app_settings(const AppSettings& settings);
    [[nodiscard]] const AppSettings& app_settings() const;
    void set_play_when_started(bool enabled);
    void set_auto_play_on_disc_load(bool enabled);

    void load_disc(const std::string& album_title, const std::vector<TrackInfo>& tracks);
    void load_disc(const std::string& artist_name, const std::string& album_title, const std::vector<TrackInfo>& tracks);
    [[nodiscard]] const std::string& artist_name() const;
    [[nodiscard]] const std::string& album_title() const;
    void set_artist_name(const std::string& artist);
    void set_album_title(const std::string& title);
    [[nodiscard]] bool has_cddb_data() const;
    void set_has_cddb_data(bool has_data);

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
    [[nodiscard]] std::string track_display_title(size_t track_index) const;

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
    std::string artist_name_{"Pink Floyd"};
    std::string album_title_{"The Dark Side of the Moon"};
    bool has_cddb_data_{true};
    std::vector<TrackInfo> tracks_;
    size_t current_track_index_{0};
    double track_elapsed_seconds_{0.0};
    double uptime_seconds_{0.0};
    bool playing_{true};
    bool recording_{false};
    bool shuffle_{false};
    RepeatMode repeat_mode_{RepeatMode::Off};
    ConnectivityStatus connectivity_{ConnectivityStatus::Connected};
    double volume_{0.8};
    double balance_{0.0};
    bool eject_menu_open_{false};
    EjectMenuScreen active_eject_screen_{EjectMenuScreen::Cd};
    AppSettings app_settings_{};
    VolumeLevels volume_levels_{};
    double left_peak_timer_{0.0};
    double right_peak_timer_{0.0};

    mycds::PlaybackControllerConfig controller_config_{};
    std::unique_ptr<mycds::PlaybackController> controller_{};
    mycds::DiscLayout disc_layout_{};
    bool has_real_source_{false};
    std::string playback_error_{};

    // Coalesced seeking state for smooth arrow-key / long-press scrubbing
    bool pending_seek_active_{false};
    std::int64_t pending_seek_lba_{0};
    double pending_seek_timer_{0.0};
    int pending_seek_idle_frames_{0};

    // Asynchronous CDDB lookup state
    struct AsyncCddbResult;
    std::mutex cddb_mutex_{};
    std::thread cddb_thread_{};
    std::atomic<std::uint64_t> cddb_generation_{0};
    std::unique_ptr<AsyncCddbResult> cddb_result_{};

    void apply_layout_metadata();
    void start_async_cddb_lookup();
    void cancel_async_cddb_lookup() noexcept;
    void poll_async_cddb_lookup();
    void update_vu_ballistics(double target_left, double target_right, double dt);
    [[nodiscard]] int first_playable_track_number() const noexcept;
    [[nodiscard]] int pick_random_playable_track() const noexcept;
    [[nodiscard]] bool is_playable_track_index(size_t index) const noexcept;
};

} // namespace mycds::gui
