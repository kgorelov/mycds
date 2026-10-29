#include "cli/command_loop.hpp"

#include <bit>
#include <concepts>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include <cdio/cdio.h>
#include <cdio/disc.h>
#include <cdio/read.h>
#include <cdio/track.h>
#include <cdio/version.h>

#include <mirage/mirage.h>

#include <SDL3/SDL.h>

namespace {

volatile std::sig_atomic_t g_interrupt_signal = 0;

void handle_termination_signal(int sig) noexcept {
    g_interrupt_signal = sig;
}

void install_termination_signal_handlers() noexcept {
    struct sigaction sa{};
    sa.sa_handler = &handle_termination_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

template <std::integral T>
constexpr T bytes_per_sectors(T sector_count) noexcept {
    return sector_count * static_cast<T>(CDIO_CD_FRAMESIZE_RAW);
}

bool verify_cpp20() {
    static_assert(std::endian::native == std::endian::little ||
                  std::endian::native == std::endian::big);
    constexpr std::uint32_t single_sector_bytes = bytes_per_sectors<std::uint32_t>(1);
    static_assert(single_sector_bytes == 2352U);

    const std::vector<std::byte> sample_buffer(single_sector_bytes);
    const std::span<const std::byte> sector_span(sample_buffer);

    std::cout << "[C++20] Verified C++20 features (concepts, std::span, std::endian, "
              << "sector size = " << sector_span.size() << " bytes)\n";
    return sector_span.size() == 2352U;
}

bool verify_libcdio() {
    if (!cdio_init()) {
        std::cerr << "[libcdio] Failed to initialize libcdio\n";
        return false;
    }

    [[maybe_unused]] auto open_fn = &cdio_open;
    [[maybe_unused]] auto destroy_fn = &cdio_destroy;
    [[maybe_unused]] auto first_track_fn = &cdio_get_first_track_num;
    [[maybe_unused]] auto last_track_fn = &cdio_get_last_track_num;
    [[maybe_unused]] auto track_format_fn = &cdio_get_track_format;
    [[maybe_unused]] auto track_lsn_fn = &cdio_get_track_lsn;
    [[maybe_unused]] auto track_last_lsn_fn = &cdio_get_track_last_lsn;
    [[maybe_unused]] auto track_pregap_lsn_fn = &cdio_get_track_pregap_lsn;
    [[maybe_unused]] auto read_audio_sectors_fn = &cdio_read_audio_sectors;

    std::cout << "[libcdio] Initialized libcdio version: " << cdio_version_string << "\n";
    return true;
}

bool verify_libmirage_image_operations() {
    namespace fs = std::filesystem;

    const fs::path temp_dir = fs::temp_directory_path() / "mycds_phase1_verify";
    std::error_code ec;
    fs::create_directories(temp_dir, ec);
    if (ec) {
        std::cerr << "[libMirage] Failed to create temp dir: " << ec.message() << "\n";
        return false;
    }

    const fs::path bin_path = temp_dir / "verify.bin";
    const fs::path toc_path = temp_dir / "verify.toc";

    {
        std::ofstream bin_out(bin_path, std::ios::binary);
        std::vector<char> audio_data(bytes_per_sectors<std::size_t>(2), 0);
        audio_data[0] = 0x12;
        audio_data[1] = 0x34;
        bin_out.write(audio_data.data(), static_cast<std::streamsize>(audio_data.size()));
    }

    {
        std::ofstream toc_out(toc_path);
        toc_out << "CD_DA\n\n"
                << "TRACK AUDIO\n"
                << "NO COPY\n"
                << "NO PRE_EMPHASIS\n"
                << "TWO_CHANNEL_AUDIO\n"
                << "DATAFILE \"verify.bin\" 00:00:02\n"
                << "INDEX 00:00:01\n";
    }

    bool ok = false;
    GError* error = nullptr;
    MirageContext* context =
        MIRAGE_CONTEXT(g_object_new(MIRAGE_TYPE_CONTEXT, nullptr));

    gchar* filenames[] = {const_cast<gchar*>(toc_path.c_str()), nullptr};
    MirageDisc* disc = mirage_context_load_image(context, filenames, &error);

    if (!disc) {
        std::cerr << "[libMirage] Failed to load synthetic BIN/TOC image: "
                  << (error ? error->message : "unknown error") << "\n";
        if (error) {
            g_error_free(error);
        }
    } else {
        const gint num_sessions = mirage_disc_get_number_of_sessions(disc);
        const gint num_tracks = mirage_disc_get_number_of_tracks(disc);
        const gint disc_start = mirage_disc_layout_get_start_sector(disc);
        const gint disc_len = mirage_disc_layout_get_length(disc);

        MirageTrack* track = mirage_disc_get_track_by_index(disc, 0, &error);
        if (track) {
            const gint track_num = mirage_track_layout_get_track_number(track);
            const MirageSectorType sector_type = mirage_track_get_sector_type(track);
            const gint track_start = mirage_track_layout_get_start_sector(track);
            const gint track_len = mirage_track_layout_get_length(track);
            const gint index01_rel = mirage_track_get_track_start(track);
            const gint num_indices = mirage_track_get_number_of_indices(track);

            MirageSector* sector = mirage_disc_get_sector(disc, track_start, &error);
            if (sector) {
                const guint8* pcm_buf = nullptr;
                gint pcm_len = 0;
                if (mirage_sector_get_data(sector, &pcm_buf, &pcm_len, &error) &&
                    pcm_buf != nullptr && pcm_len == 2352) {
                    std::cout << "[libMirage] Verified BIN/TOC load & sector read: "
                              << "sessions=" << num_sessions
                              << ", tracks=" << num_tracks
                              << ", disc_range=[" << disc_start << ", " << (disc_start + disc_len) << ")"
                              << ", track=" << track_num
                              << " (type=" << (sector_type == MIRAGE_SECTOR_AUDIO ? "AUDIO" : "OTHER")
                              << ", start=" << track_start
                              << ", len=" << track_len
                              << ", index01_offset=" << index01_rel
                              << ", indices=" << num_indices
                              << ", sector_bytes=" << pcm_len << ")\n";
                    ok = true;
                }
                g_object_unref(sector);
            }
            g_object_unref(track);
        }

        if (error) {
            std::cerr << "[libMirage] Error during inspection: " << error->message << "\n";
            g_error_free(error);
        }
        g_object_unref(disc);
    }

    g_object_unref(context);
    fs::remove_all(temp_dir, ec);
    return ok;
}

bool verify_libmirage() {
    GError* error = nullptr;
    if (!mirage_initialize(&error)) {
        std::cerr << "[libMirage] Failed to initialize libMirage: "
                  << (error ? error->message : "unknown error") << "\n";
        if (error) {
            g_error_free(error);
        }
        return false;
    }

    const MirageParserInfo* parsers = nullptr;
    gint num_parsers = 0;
    bool has_toc_parser = false;
    if (mirage_get_parsers_info(&parsers, &num_parsers, &error)) {
        for (gint i = 0; i < num_parsers; ++i) {
            if (parsers[i].id && std::string_view(parsers[i].id) == "PARSER-TOC") {
                has_toc_parser = true;
                break;
            }
        }
    } else if (error) {
        g_error_free(error);
        error = nullptr;
    }

    std::cout << "[libMirage] Initialized libMirage version: " << mirage_version_long
              << " (loaded " << num_parsers << " image parsers, TOC parser: "
              << (has_toc_parser ? "yes" : "no") << ")\n";

    if (!has_toc_parser) {
        std::cerr << "[libMirage] Required PARSER-TOC plugin is missing\n";
        mirage_shutdown(nullptr);
        return false;
    }

    const bool ops_ok = verify_libmirage_image_operations();
    mirage_shutdown(nullptr);
    return ops_ok;
}

bool verify_sdl3_audio() {
    const int version = SDL_GetVersion();
    std::cout << "[SDL3] Linked SDL version: "
              << SDL_VERSIONNUM_MAJOR(version) << "."
              << SDL_VERSIONNUM_MINOR(version) << "."
              << SDL_VERSIONNUM_MICRO(version) << "\n";

    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        std::cerr << "[SDL3] SDL_Init(SDL_INIT_AUDIO) failed: " << SDL_GetError() << "\n";
        return false;
    }

    const char* current_driver = SDL_GetCurrentAudioDriver();
    std::cout << "[SDL3] Active audio driver: "
              << (current_driver ? current_driver : "none") << "\n";

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = 44100;

    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream) {
        std::cerr << "[SDL3] SDL_OpenAudioDeviceStream failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return false;
    }

    const SDL_AudioDeviceID dev_id = SDL_GetAudioStreamDevice(stream);
    const char* dev_name = SDL_GetAudioDeviceName(dev_id);
    std::cout << "[SDL3] Opened default playback device ("
              << (dev_name ? dev_name : "default")
              << ") at 44100 Hz, 2 channels, 16-bit signed PCM\n";

    SDL_DestroyAudioStream(stream);
    SDL_Quit();
    return true;
}

int run_phase1_verification() {
    std::cout << "MyCDs CD Player Prototype - Phase 1 Environment & Dependency Verification\n";
    std::cout << "-------------------------------------------------------------------------\n";

    if (!verify_cpp20()) {
        return 1;
    }
    if (!verify_libcdio()) {
        return 1;
    }
    if (!verify_libmirage()) {
        return 1;
    }
    if (!verify_sdl3_audio()) {
        return 1;
    }

    std::cout << "-------------------------------------------------------------------------\n";
    std::cout << "All Phase 1 dependencies and APIs verified successfully.\n";
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    install_termination_signal_handlers();

    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--verify-deps") {
            return run_phase1_verification();
        }
    }

    return mycds::run_cdplayer_cli(
        argc, argv, std::cin, std::cout, std::cerr, &g_interrupt_signal);
}
