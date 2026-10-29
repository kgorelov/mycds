#pragma once

#include "domain/disc_info.hpp"
#include "metadata/metadata_provider.hpp"
#include "playback/playback_controller.hpp"
#include "source/disc_source.hpp"

#include <csignal>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <string_view>

namespace mycds {

std::string_view disc_source_error_kind_label(DiscSourceErrorKind kind) noexcept;

std::string format_exception_diagnostic(const std::exception& ex);

struct CommandLoopOptions {
    MetadataProvider* metadataProvider{nullptr};
    std::filesystem::path customSidecarPath{};
    bool autoPlayOnStart{false};
    const volatile std::sig_atomic_t* interruptSignal{nullptr};
};

// Interactive console interface driving PlaybackController.
// Supports the complete required Phase 6 command set:
//   info, tracks, play, pause, stop, seek-track <n>, seek-lba <lba>, position, quit
// plus help and Phase 2.2 metadata commands.
class CommandLoop {
public:
    CommandLoop(
        PlaybackController& controller,
        std::istream& in,
        std::ostream& out,
        std::ostream& err,
        CommandLoopOptions options = {});

    // Runs the interactive command loop until 'quit', EOF, or interrupt signal.
    // Always stops playback and closes the source before returning.
    int run();

private:
    bool readNextLine(std::string& out_line);
    bool executeLine(std::string_view line);

    void cmdHelp();
    void cmdInfo();
    void cmdTracks();
    void cmdPlay();
    void cmdPause();
    void cmdStop();
    void cmdSeekTrack(std::string_view arg);
    void cmdSeekLba(std::string_view arg);
    void cmdPosition();
    void cmdMetadata(std::string_view rest);

    void reportNewAsyncErrorIfAny();
    const TrackMetadata* findTrackMetadata(int track_number) const noexcept;

    PlaybackController& controller_;
    std::istream& in_;
    std::ostream& out_;
    std::ostream& err_;
    CommandLoopOptions options_;
    DiscLayout display_layout_{};
    bool reported_current_error_{false};
};

void print_cli_usage(std::string_view program_name, std::ostream& out);

int run_cdplayer_cli(
    int argc,
    const char* const* argv,
    std::istream& in,
    std::ostream& out,
    std::ostream& err,
    const volatile std::sig_atomic_t* interrupt_signal = nullptr);

} // namespace mycds
