#include "cli/command_loop.hpp"

#include "audio/audio_output.hpp"
#include "domain/audio_format.hpp"
#include "inspect.hpp"
#include "metadata/cddb_disc_id.hpp"
#include "metadata/cddb_provider.hpp"
#include "metadata/metadata_matcher.hpp"
#include "metadata/metadata_store.hpp"
#include "metadata/metadata_workflow.hpp"

#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

#include <poll.h>
#include <unistd.h>

namespace mycds {

namespace {

std::string_view trim_view(std::string_view sv) noexcept {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
        sv.remove_suffix(1);
    }
    return sv;
}

template <typename IntT>
std::optional<IntT> parse_strict_integer(std::string_view text) noexcept {
    const std::string_view sv = trim_view(text);
    if (sv.empty()) {
        return std::nullopt;
    }
    std::string_view digits = sv;
    if (digits.front() == '+') {
        digits.remove_prefix(1);
        if (digits.empty()) {
            return std::nullopt;
        }
    }
    IntT value{};
    const auto [ptr, ec] =
        std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (ec != std::errc{} || ptr != digits.data() + digits.size()) {
        return std::nullopt;
    }
    return value;
}

std::string format_two_digits(int value) {
    std::ostringstream oss;
    oss << std::right << std::setfill('0') << std::setw(2) << value;
    return oss.str();
}

std::string_view track_type_label(TrackType type) noexcept {
    switch (type) {
        case TrackType::Audio:
            return "Audio";
        case TrackType::Data:
            return "Data";
        case TrackType::Unknown:
            return "Unknown";
    }
    return "Unknown";
}

std::filesystem::path resolve_cue_export_target_path(
    const DiscLayout& layout,
    std::string_view arg_path) {
    namespace fs = std::filesystem;
    if (!arg_path.empty()) {
        return fs::path(std::string(arg_path));
    }
    if (!layout.sourcePath.empty() && layout.sourcePath.rfind("/dev/", 0) != 0) {
        const fs::path src(layout.sourcePath);
        return src.parent_path() / (src.stem().string() + ".cue");
    }
    throw std::runtime_error("Please specify a target .cue file path for metadata export-cue");
}

std::filesystem::path resolve_cue_template_path(
    const DiscLayout& layout,
    const std::filesystem::path& target_cue) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(target_cue, ec)) {
        return target_cue;
    }
    if (!layout.sourcePath.empty() && layout.sourcePath.rfind("/dev/", 0) != 0) {
        const fs::path src(layout.sourcePath);
        if (src.extension() == ".cue" || src.extension() == ".CUE") {
            return src;
        }
        const fs::path sibling_cue = src.parent_path() / (src.stem().string() + ".cue");
        if (fs::exists(sibling_cue, ec)) {
            return sibling_cue;
        }
    }
    return target_cue;
}

struct ScopeCleanup {
    PlaybackController& controller;
    ~ScopeCleanup() noexcept {
        controller.stop();
        controller.closeSource();
    }
};

} // namespace

std::string_view disc_source_error_kind_label(DiscSourceErrorKind kind) noexcept {
    switch (kind) {
        case DiscSourceErrorKind::OpenFailed:
            return "Open Failed";
        case DiscSourceErrorKind::NoDisc:
            return "No Disc Inserted";
        case DiscSourceErrorKind::UnsupportedLayout:
            return "Unsupported Disc Layout";
        case DiscSourceErrorKind::CorruptImage:
            return "Corrupt or Invalid Image";
        case DiscSourceErrorKind::OutOfRangeLba:
            return "LBA Out of Range";
        case DiscSourceErrorKind::NonAudioTrack:
            return "Non-Audio Track";
        case DiscSourceErrorKind::ReadFailed:
            return "Audio Sector Read Failed";
        case DiscSourceErrorKind::DiscRemoved:
            return "Disc Removed";
    }
    return "Disc Source Error";
}

std::string format_exception_diagnostic(const std::exception& ex) {
    if (const auto* src_err = dynamic_cast<const DiscSourceError*>(&ex)) {
        std::ostringstream oss;
        oss << "[" << disc_source_error_kind_label(src_err->kind()) << "] "
            << src_err->what();
        return oss.str();
    }
    if (const auto* range_err = dynamic_cast<const DiscOutOfRangeError*>(&ex)) {
        return std::string("[Out of Range] ") + range_err->what();
    }
    return ex.what();
}

CommandLoop::CommandLoop(
    PlaybackController& controller,
    std::istream& in,
    std::ostream& out,
    std::ostream& err,
    CommandLoopOptions options)
    : controller_(controller),
      in_(in),
      out_(out),
      err_(err),
      options_(std::move(options)) {
    if (controller_.hasSource()) {
        display_layout_ = controller_.getDiscLayout();
        if (!options_.customSidecarPath.empty()) {
            if (MetadataStore::exists(options_.customSidecarPath)) {
                try {
                    const auto rec = MetadataStore::loadSidecar(options_.customSidecarPath);
                    MetadataMatcher::applyToLayout(display_layout_, rec);
                } catch (const std::exception& ex) {
                    err_ << "Warning: Failed to load sidecar metadata '"
                         << options_.customSidecarPath.string() << "': " << ex.what() << "\n";
                }
            }
        } else {
            try {
                const auto default_sidecar =
                    MetadataStore::defaultSidecarPath(display_layout_);
                if (MetadataStore::exists(default_sidecar)) {
                    const auto rec = MetadataStore::loadSidecar(default_sidecar);
                    MetadataMatcher::applyToLayout(display_layout_, rec);
                }
            } catch (...) {
            }
        }
    }
}

int CommandLoop::run() {
    ScopeCleanup cleanup{controller_};

    if (!controller_.hasSource()) {
        err_ << "Error: Cannot start interactive console without an open disc source.\n";
        return 1;
    }

    const auto& info = display_layout_.discInfo;
    const std::int64_t total_sectors = info.endLba - info.startLba;
    out_ << "Opened " << display_layout_.sourceKind << ": " << display_layout_.sourcePath
         << " (" << display_layout_.tracks.size() << " tracks, "
         << sectors_to_msf(total_sectors) << ")";
    if (!display_layout_.discMetadata.title.empty()) {
        out_ << " — ";
        if (!display_layout_.discMetadata.performer.empty()) {
            out_ << display_layout_.discMetadata.performer << " - ";
        }
        out_ << display_layout_.discMetadata.title;
    }
    out_ << "\nType 'help' for available commands, 'quit' to exit.\n";

    if (options_.autoPlayOnStart) {
        cmdPlay();
    }

    std::string line;
    while (true) {
        if (options_.interruptSignal != nullptr && *options_.interruptSignal != 0) {
            const int sig = *options_.interruptSignal;
            out_ << "\nInterrupted (signal " << sig << "). Shutting down cleanly...\n";
            return 128 + sig;
        }

        reportNewAsyncErrorIfAny();

        out_ << "cdplayer> ";
        out_.flush();

        if (!readNextLine(line)) {
            if (options_.interruptSignal != nullptr && *options_.interruptSignal != 0) {
                const int sig = *options_.interruptSignal;
                out_ << "\nInterrupted (signal " << sig << "). Shutting down cleanly...\n";
                return 128 + sig;
            }
            out_ << "\n";
            break;
        }

        reportNewAsyncErrorIfAny();

        if (!executeLine(line)) {
            break;
        }
    }

    return 0;
}

bool CommandLoop::readNextLine(std::string& out_line) {
    if (&in_ == &std::cin) {
        while (options_.interruptSignal == nullptr || *options_.interruptSignal == 0) {
            if (in_.rdbuf() && in_.rdbuf()->in_avail() > 0) {
                break;
            }

            struct pollfd pfd{};
            pfd.fd = STDIN_FILENO;
            pfd.events = POLLIN;

            const int rc = ::poll(&pfd, 1, 100);
            if (rc > 0) {
                break;
            }
            if (rc < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }

            // While idle at the prompt, surface any newly encountered background error
            const auto st = controller_.status();
            if (st.state == PlaybackState::Error && !reported_current_error_) {
                out_ << "\n";
                reportNewAsyncErrorIfAny();
                out_ << "cdplayer> ";
                out_.flush();
            }
        }

        if (options_.interruptSignal != nullptr && *options_.interruptSignal != 0) {
            return false;
        }
    } else if (options_.interruptSignal != nullptr && *options_.interruptSignal != 0) {
        return false;
    }

    return static_cast<bool>(std::getline(in_, out_line));
}

bool CommandLoop::executeLine(std::string_view line) {
    const std::string_view trimmed = trim_view(line);
    if (trimmed.empty()) {
        return true;
    }

    const std::size_t space_pos = trimmed.find_first_of(" \t");
    const std::string_view cmd =
        (space_pos == std::string_view::npos) ? trimmed : trimmed.substr(0, space_pos);
    const std::string_view rest =
        (space_pos == std::string_view::npos)
            ? std::string_view{}
            : trim_view(trimmed.substr(space_pos + 1));

    if (cmd == "quit" || cmd == "exit") {
        out_ << "Stopping playback and closing source...\n";
        return false;
    }
    if (cmd == "help" || cmd == "?") {
        cmdHelp();
        return true;
    }
    if (cmd == "info") {
        cmdInfo();
        return true;
    }
    if (cmd == "tracks") {
        cmdTracks();
        return true;
    }
    if (cmd == "play") {
        if (!rest.empty()) {
            err_ << "Error: 'play' takes no arguments. Use 'seek-track <n>' or 'seek-lba <lba>' first.\n";
            return true;
        }
        cmdPlay();
        return true;
    }
    if (cmd == "pause") {
        cmdPause();
        return true;
    }
    if (cmd == "stop") {
        cmdStop();
        return true;
    }
    if (cmd == "seek-track") {
        cmdSeekTrack(rest);
        return true;
    }
    if (cmd == "seek-lba") {
        cmdSeekLba(rest);
        return true;
    }
    if (cmd == "position") {
        cmdPosition();
        return true;
    }
    if (cmd == "metadata") {
        cmdMetadata(rest);
        return true;
    }

    err_ << "Error: Unknown command '" << cmd
         << "'. Type 'help' for available commands.\n";
    return true;
}

void CommandLoop::cmdHelp() {
    out_ << "Commands:\n"
         << "  info                         Display source type, disc layout summary, and metadata\n"
         << "  tracks                       Display track numbers, types, LBA ranges, and indexes\n"
         << "  play                         Start or resume audio playback\n"
         << "  pause                        Pause active audio playback\n"
         << "  stop                         Stop playback and reset to start of disc\n"
         << "  seek-track <number>          Seek to track <number> (INDEX 01)\n"
         << "  seek-lba <lba>               Seek to absolute LBA sector address\n"
         << "  position                     Display current LBA, track, index, and playback state\n"
         << "  metadata lookup [index]      Query CDDB service and select candidate match\n"
         << "  metadata show                Display album and track metadata summary\n"
         << "  metadata edit                Interactively edit album and track metadata\n"
         << "  metadata save [path]         Save metadata to local sidecar JSON file\n"
         << "  metadata export-cue [path]   Export metadata safely to CUE file\n"
         << "  help                         Display this command reference\n"
         << "  quit                         Stop playback, release resources, and exit\n";
}

void CommandLoop::cmdInfo() {
    const auto st = controller_.status();
    const auto& info = display_layout_.discInfo;
    const auto& meta = display_layout_.discMetadata;
    const std::int64_t total_sectors = info.endLba - info.startLba;

    std::string cddb_id = meta.databaseDiscId;
    if (cddb_id.empty() && !display_layout_.tracks.empty()) {
        try {
            cddb_id =
                calculate_cddb_disc_id(info, display_layout_.tracks).discIdHex;
        } catch (...) {
        }
    }

    out_ << "------------------------------------------------------------------------------------\n";
    out_ << "Source Type : " << display_layout_.sourceKind << "\n";
    out_ << "Source Path : " << display_layout_.sourcePath << "\n";
    out_ << "Sessions    : " << info.sessions << "\n";
    out_ << "Tracks      : " << info.firstTrack << " .. " << info.lastTrack
         << " (" << display_layout_.tracks.size() << " total)\n";
    out_ << "LBA Range   : [" << info.startLba << ", " << info.endLba << ") ("
         << total_sectors << " sectors, " << sectors_to_msf(total_sectors) << ")\n";
    if (!cddb_id.empty()) {
        out_ << "CDDB DiscID : " << cddb_id << "\n";
    }
    out_ << "State       : " << playback_state_to_string(st.state);
    if (st.endOfDisc) {
        out_ << " (End of Disc)";
    }
    out_ << "\n";

    if (!meta.empty()) {
        out_ << "Album Metadata:\n";
        if (!meta.title.empty()) {
            out_ << "  Title      : " << meta.title << "\n";
        }
        if (!meta.performer.empty()) {
            out_ << "  Performer  : " << meta.performer << "\n";
        }
        if (!meta.songwriter.empty()) {
            out_ << "  Songwriter : " << meta.songwriter << "\n";
        }
        if (!meta.catalog.empty()) {
            out_ << "  Catalog    : " << meta.catalog << "\n";
        }
        if (!meta.genre.empty()) {
            out_ << "  Genre      : " << meta.genre << "\n";
        }
        if (meta.releaseYear.has_value()) {
            out_ << "  Year       : " << *meta.releaseYear << "\n";
        }
        if (!meta.databaseProvider.empty()) {
            out_ << "  Provider   : " << meta.databaseProvider << "\n";
        }
        if (!meta.externalRecordId.empty()) {
            out_ << "  Record ID  : " << meta.externalRecordId << "\n";
        }
    }
    out_ << "------------------------------------------------------------------------------------\n";
}

void CommandLoop::cmdTracks() {
    out_ << "------------------------------------------------------------------------------------\n";
    out_ << "Trk  Type     Start LBA  INDEX 01   End LBA    Length   Duration  Pregap      Indexes\n";
    out_ << "------------------------------------------------------------------------------------\n";

    for (const auto& trk : display_layout_.tracks) {
        const std::int64_t length = trk.endLba - trk.startLba;
        std::ostringstream pregap_str;
        if (trk.pregapLength.has_value()) {
            if (*trk.pregapLength > 0) {
                pregap_str << *trk.pregapLength << "f @" << *trk.pregapStartLba;
            } else {
                pregap_str << "0f";
            }
        } else {
            pregap_str << "n/a (TOC)";
        }

        std::ostringstream idx_str;
        for (std::size_t i = 0; i < trk.indexes.size(); ++i) {
            if (i > 0) {
                idx_str << ", ";
            }
            idx_str << "I" << std::setfill('0') << std::setw(2) << trk.indexes[i].number
                    << ":" << trk.indexes[i].lba;
            if (!trk.indexes[i].explicitFromSource) {
                idx_str << "(inferred)";
            }
        }
        if (trk.postgapLength > 0) {
            idx_str << " [postgap:" << trk.postgapLength << "f]";
        }

        out_ << std::setfill(' ')
             << std::right << std::setw(3) << trk.number << "  "
             << std::left << std::setw(7) << track_type_label(trk.type) << "  "
             << std::right
             << std::setw(9) << trk.startLba << "  "
             << std::setw(9) << trk.index01Lba << "  "
             << std::setw(9) << trk.endLba << "  "
             << std::setw(7) << length << "  "
             << std::setw(8) << sectors_to_msf(length) << "  "
             << std::left << std::setw(10) << pregap_str.str() << std::right << "  "
             << idx_str.str();

        if (const TrackMetadata* tm = findTrackMetadata(trk.number)) {
            if (!tm->title.empty()) {
                out_ << "  \"" << tm->title << "\"";
            }
        }
        out_ << "\n";
    }
    out_ << "------------------------------------------------------------------------------------\n";
}

void CommandLoop::cmdPlay() {
    try {
        reported_current_error_ = false;
        controller_.play();
        const auto st = controller_.status();
        if (st.state == PlaybackState::Error) {
            reportNewAsyncErrorIfAny();
            return;
        }
        out_ << "State: " << playback_state_to_string(st.state)
             << " | LBA " << st.position.lba
             << " (" << sectors_to_msf(st.position.discElapsedSectors) << ")";
        if (st.position.trackNumber.has_value()) {
            out_ << " | Track " << *st.position.trackNumber;
            if (st.position.indexNumber.has_value()) {
                out_ << " INDEX " << format_two_digits(*st.position.indexNumber);
            }
            if (const TrackMetadata* tm = findTrackMetadata(*st.position.trackNumber)) {
                if (!tm->title.empty()) {
                    out_ << " \"" << tm->title << "\"";
                }
            }
        }
        out_ << "\n";
    } catch (const std::exception& ex) {
        err_ << "Error: " << format_exception_diagnostic(ex) << "\n";
    }
}

void CommandLoop::cmdPause() {
    try {
        const auto before = controller_.status();
        if (before.state == PlaybackState::Paused) {
            out_ << "Playback is already Paused at LBA " << before.position.lba << ".\n";
            return;
        }
        if (before.state == PlaybackState::Stopped) {
            out_ << "Playback is Stopped (nothing to pause).\n";
            return;
        }
        if (before.state == PlaybackState::Error) {
            reportNewAsyncErrorIfAny();
            err_ << "Error: Cannot pause while in Error state.\n";
            return;
        }

        controller_.pause();
        const auto after = controller_.status();
        out_ << "Paused at LBA " << after.position.lba
             << " (" << sectors_to_msf(after.position.discElapsedSectors) << ")";
        if (after.position.trackNumber.has_value()) {
            out_ << ", Track " << *after.position.trackNumber;
            if (after.position.indexNumber.has_value()) {
                out_ << " INDEX " << format_two_digits(*after.position.indexNumber);
            }
        }
        out_ << "\n";
    } catch (const std::exception& ex) {
        err_ << "Error: " << format_exception_diagnostic(ex) << "\n";
    }
}

void CommandLoop::cmdStop() {
    reported_current_error_ = false;
    controller_.stop();
    const auto st = controller_.status();
    out_ << "Stopped. Position reset to LBA " << st.position.lba;
    if (st.position.trackNumber.has_value()) {
        out_ << " (Track " << *st.position.trackNumber;
        if (st.position.indexNumber.has_value()) {
            out_ << ", INDEX " << format_two_digits(*st.position.indexNumber);
        }
        out_ << ")";
    }
    out_ << ".\n";
}

void CommandLoop::cmdSeekTrack(std::string_view arg) {
    const auto parsed = parse_strict_integer<int>(arg);
    if (!parsed.has_value()) {
        err_ << "Error: Invalid or missing track number. Usage: seek-track <number>\n";
        return;
    }

    try {
        reported_current_error_ = false;
        controller_.seekToTrack(*parsed);
        const auto st = controller_.status();
        if (st.state == PlaybackState::Error) {
            reportNewAsyncErrorIfAny();
            return;
        }

        out_ << "Seeked to Track " << *parsed << " (INDEX "
             << format_two_digits(st.position.indexNumber.value_or(1))
             << ") at LBA " << st.position.lba
             << " [" << playback_state_to_string(st.state) << "]";
        if (const TrackMetadata* tm = findTrackMetadata(*parsed)) {
            if (!tm->title.empty()) {
                out_ << " — \"" << tm->title << "\"";
            }
        }
        out_ << "\n";
    } catch (const std::exception& ex) {
        err_ << "Error: " << format_exception_diagnostic(ex) << "\n";
    }
}

void CommandLoop::cmdSeekLba(std::string_view arg) {
    const auto parsed = parse_strict_integer<std::int64_t>(arg);
    if (!parsed.has_value()) {
        err_ << "Error: Invalid or missing LBA address. Usage: seek-lba <lba>\n";
        return;
    }

    try {
        reported_current_error_ = false;
        controller_.seekToLba(*parsed);
        const auto st = controller_.status();
        if (st.state == PlaybackState::Error) {
            reportNewAsyncErrorIfAny();
            return;
        }

        out_ << "Seeked to LBA " << st.position.lba;
        if (st.position.trackNumber.has_value()) {
            out_ << " (Track " << *st.position.trackNumber;
            if (st.position.indexNumber.has_value()) {
                out_ << ", INDEX " << format_two_digits(*st.position.indexNumber);
            }
            out_ << ", track time "
                 << sectors_to_msf(st.position.trackRelativeSectors) << ")";
        }
        out_ << " [" << playback_state_to_string(st.state) << "]\n";
    } catch (const std::exception& ex) {
        err_ << "Error: " << format_exception_diagnostic(ex) << "\n";
    }
}

void CommandLoop::cmdPosition() {
    const auto st = controller_.status();
    const auto& info = display_layout_.discInfo;
    const std::int64_t total_sectors = info.endLba - info.startLba;
    const std::size_t buffered_sectors = st.bufferedBytes / AudioFormat::kBytesPerSector;

    out_ << "State      : " << playback_state_to_string(st.state);
    if (st.endOfDisc) {
        out_ << " (End of Disc)";
    }
    out_ << "\n";

    out_ << "LBA        : " << st.position.lba << " (frame "
         << st.position.frameInSector << "/" << AudioFormat::kFramesPerSector
         << ", disc " << sectors_to_msf(st.position.discElapsedSectors)
         << " / " << sectors_to_msf(total_sectors) << ")\n";

    out_ << "Track      : ";
    if (st.position.trackNumber.has_value()) {
        out_ << *st.position.trackNumber;
        if (const TrackMetadata* tm = findTrackMetadata(*st.position.trackNumber)) {
            if (!tm->title.empty()) {
                out_ << " (\"" << tm->title << "\")";
            }
        }
        out_ << "\n";

        out_ << "Index      : ";
        if (st.position.indexNumber.has_value()) {
            out_ << "INDEX " << format_two_digits(*st.position.indexNumber);
        } else {
            out_ << "n/a";
        }
        out_ << " (track time " << sectors_to_msf(st.position.trackRelativeSectors) << ")\n";
    } else {
        out_ << "n/a\n";
    }

    out_ << "Buffer     : " << buffered_sectors << " sectors ("
         << st.bufferedBytes << " bytes), underruns: " << st.underrunCount << "\n";

    if (st.state == PlaybackState::Error || !st.errorMessage.empty()) {
        out_ << "Diagnostic : ";
        if (st.errorKind.has_value()) {
            out_ << "[" << disc_source_error_kind_label(*st.errorKind) << "] ";
        }
        if (st.errorLba.has_value()) {
            out_ << "(at LBA " << *st.errorLba << ") ";
        }
        out_ << st.errorMessage << "\n";
        reported_current_error_ = true;
    }
}

void CommandLoop::cmdMetadata(std::string_view rest) {
    const std::size_t space_pos = rest.find_first_of(" \t");
    const std::string_view subcmd =
        (space_pos == std::string_view::npos) ? rest : rest.substr(0, space_pos);
    const std::string_view arg =
        (space_pos == std::string_view::npos)
            ? std::string_view{}
            : trim_view(rest.substr(space_pos + 1));

    if (subcmd == "show") {
        print_metadata_summary(display_layout_, out_);
        return;
    }
    if (subcmd == "lookup") {
        if (!options_.metadataProvider) {
            err_ << "Error: No CDDB metadata provider configured.\n";
            return;
        }
        std::optional<std::size_t> preselected;
        if (!arg.empty()) {
            const auto parsed = parse_strict_integer<std::size_t>(arg);
            if (!parsed.has_value()) {
                err_ << "Error: Invalid candidate index '" << arg << "'.\n";
                return;
            }
            preselected = *parsed;
        }
        run_interactive_metadata_lookup(
            display_layout_,
            *options_.metadataProvider,
            in_,
            out_,
            options_.customSidecarPath,
            preselected,
            false);
        return;
    }
    if (subcmd == "edit") {
        run_interactive_metadata_edit(display_layout_, in_, out_);
        print_metadata_summary(display_layout_, out_);
        return;
    }
    if (subcmd == "save") {
        const std::filesystem::path target =
            !arg.empty() ? std::filesystem::path(std::string(arg))
            : !options_.customSidecarPath.empty() ? options_.customSidecarPath
            : MetadataStore::defaultSidecarPath(display_layout_);
        try {
            MetadataStore::saveSidecar(target, extract_metadata_record(display_layout_));
            out_ << "[Metadata] Saved sidecar metadata to " << target.string() << "\n";
        } catch (const std::exception& ex) {
            err_ << "Error: [Metadata Save] " << ex.what() << "\n";
        }
        return;
    }
    if (subcmd == "export-cue") {
        try {
            const auto target_cue = resolve_cue_export_target_path(display_layout_, arg);
            const auto template_cue = resolve_cue_template_path(display_layout_, target_cue);
            MetadataStore::exportToCue(
                target_cue, extract_metadata_record(display_layout_), template_cue);
            out_ << "[Metadata] Exported metadata to CUE file: "
                 << target_cue.string() << "\n";
        } catch (const std::exception& ex) {
            err_ << "Error: [Metadata CUE Export] " << ex.what() << "\n";
        }
        return;
    }

    err_ << "Error: Unknown metadata subcommand '" << subcmd
         << "'. Expected: lookup, show, edit, save, export-cue\n";
}

void CommandLoop::reportNewAsyncErrorIfAny() {
    const auto st = controller_.status();
    if (st.state != PlaybackState::Error) {
        reported_current_error_ = false;
        return;
    }
    if (reported_current_error_) {
        return;
    }
    reported_current_error_ = true;

    err_ << "[Playback Error] ";
    if (st.errorKind.has_value()) {
        err_ << "[" << disc_source_error_kind_label(*st.errorKind) << "] ";
    }
    if (st.errorLba.has_value()) {
        err_ << "(at LBA " << *st.errorLba << ") ";
    }
    err_ << st.errorMessage << "\n";
}

const TrackMetadata* CommandLoop::findTrackMetadata(int track_number) const noexcept {
    for (const auto& tm : display_layout_.trackMetadata) {
        if (tm.trackNumber == track_number) {
            return &tm;
        }
    }
    return nullptr;
}

void print_cli_usage(std::string_view program_name, std::ostream& out) {
    out << "Usage:\n"
        << "  " << program_name << " --device <device_path>                 Open physical CD in interactive console\n"
        << "  " << program_name << " --image <toc_or_cue_path>              Open BIN/TOC or BIN/CUE image in interactive console\n"
        << "\n"
        << "Interactive Commands:\n"
        << "  info, tracks, play, pause, stop, seek-track <number>, seek-lba <lba>, position, quit\n"
        << "\n"
        << "Options & Additional Modes:\n"
        << "  --play [seconds]                                 Auto-start playback (or play for <seconds> and exit)\n"
        << "  --seek-track <number>                            Seek to track <number> on startup\n"
        << "  --seek-lba <lba>                                 Seek to LBA <lba> on startup\n"
        << "  --audio-driver <driver>                          Preferred SDL3 audio driver (e.g. pipewire, alsa, dummy)\n"
        << "  --inspect                                        Print disc layout inspection and exit\n"
        << "  --compare <src1> <src2>                          Inspect and compare two disc layouts and exit\n"
        << "  metadata <lookup|show|edit|save|export-cue>      Run one-shot metadata command and exit\n"
        << "  --cddb-url <url>                                 Override CDDB CGI endpoint URL\n"
        << "  --sidecar <path>                                 Override sidecar metadata file path\n"
        << "  --test-tone [seconds]                            Play 440 Hz PCM test signal via AudioOutput\n"
        << "  --verify-deps                                    Run dependency verification\n"
        << "  --help, -h                                       Display this help message\n";
}

int run_cdplayer_cli(
    int argc,
    const char* const* argv,
    std::istream& in,
    std::ostream& out,
    std::ostream& err,
    const volatile std::sig_atomic_t* interrupt_signal) {
    const std::string_view prog =
        (argc > 0 && argv && argv[0]) ? std::string_view(argv[0]) : "cdplayer";

    if (argc <= 1) {
        err << "Error: No disc source specified. Provide --device <path> or --image <path>.\n";
        print_cli_usage(prog, err);
        return 1;
    }

    struct SourceSpec {
        bool is_device{false};
        std::string path{};
    };

    std::vector<SourceSpec> specs;
    bool inspect_only = false;
    bool compare_mode = false;
    bool run_test_tone = false;
    double test_tone_seconds = 1.0;
    bool auto_play = false;
    std::optional<double> timed_play_seconds{};
    std::optional<int> initial_seek_track{};
    std::optional<std::int64_t> initial_seek_lba{};
    bool auto_save_lookup = false;
    std::string audio_driver{};
    CddbClientConfig cddb_config{};
    std::filesystem::path custom_sidecar_path{};
    std::string metadata_subcmd{};
    std::string metadata_subcmd_arg{};

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            print_cli_usage(prog, out);
            return 0;
        }
        if (arg == "--device" || arg == "--inspect-device") {
            if (i + 1 >= argc) {
                err << "Error: '" << arg << "' requires a device path argument.\n";
                return 1;
            }
            if (arg == "--inspect-device") {
                inspect_only = true;
            }
            specs.push_back(SourceSpec{.is_device = true, .path = argv[++i]});
        } else if (arg == "--image" || arg == "--inspect-image") {
            if (i + 1 >= argc) {
                err << "Error: '" << arg << "' requires an image file path argument.\n";
                return 1;
            }
            if (arg == "--inspect-image") {
                inspect_only = true;
            }
            specs.push_back(SourceSpec{.is_device = false, .path = argv[++i]});
        } else if (arg == "--inspect") {
            inspect_only = true;
        } else if (arg == "--interactive" || arg == "-i") {
            // Interactive mode is now the default for single-source invocation;
            // accepted for compatibility with earlier phases.
        } else if (arg == "--compare") {
            if (i + 2 >= argc) {
                err << "Error: '--compare' requires two source path arguments.\n";
                return 1;
            }
            compare_mode = true;
            inspect_only = true;
            const std::string p1 = argv[++i];
            const std::string p2 = argv[++i];
            specs.push_back(SourceSpec{.is_device = p1.rfind("/dev/", 0) == 0, .path = p1});
            specs.push_back(SourceSpec{.is_device = p2.rfind("/dev/", 0) == 0, .path = p2});
        } else if (arg == "--audio-driver") {
            if (i + 1 >= argc) {
                err << "Error: '--audio-driver' requires a driver name argument.\n";
                return 1;
            }
            audio_driver = argv[++i];
        } else if (arg == "--cddb-url") {
            if (i + 1 >= argc) {
                err << "Error: '--cddb-url' requires a URL argument.\n";
                return 1;
            }
            cddb_config.endpointUrl = argv[++i];
        } else if (arg == "--sidecar") {
            if (i + 1 >= argc) {
                err << "Error: '--sidecar' requires a file path argument.\n";
                return 1;
            }
            custom_sidecar_path = argv[++i];
        } else if (arg == "--save") {
            auto_save_lookup = true;
        } else if (arg == "--lookup-metadata") {
            metadata_subcmd = "lookup";
        } else if (arg == "metadata") {
            if (i + 1 >= argc) {
                err << "Error: 'metadata' requires a subcommand (lookup, show, edit, save, export-cue).\n";
                return 1;
            }
            metadata_subcmd = argv[++i];
            if (i + 1 < argc && std::string_view(argv[i + 1]).rfind("--", 0) != 0) {
                metadata_subcmd_arg = argv[++i];
            }
        } else if (arg == "--test-tone") {
            run_test_tone = true;
            if (i + 1 < argc && std::string_view(argv[i + 1]).rfind("--", 0) != 0) {
                try {
                    test_tone_seconds = std::stod(argv[++i]);
                } catch (...) {
                    err << "Error: Invalid duration for '--test-tone'.\n";
                    return 1;
                }
            }
        } else if (arg == "--play") {
            auto_play = true;
            if (i + 1 < argc && std::string_view(argv[i + 1]).rfind("--", 0) != 0 &&
                std::string_view(argv[i + 1]) != "metadata") {
                try {
                    const double secs = std::stod(argv[++i]);
                    if (secs <= 0.0) {
                        err << "Error: Duration for '--play' must be positive.\n";
                        return 1;
                    }
                    timed_play_seconds = secs;
                } catch (...) {
                    err << "Error: Invalid duration for '--play'.\n";
                    return 1;
                }
            }
        } else if (arg == "--seek-track") {
            if (i + 1 >= argc) {
                err << "Error: '--seek-track' requires a track number argument.\n";
                return 1;
            }
            const auto parsed = parse_strict_integer<int>(argv[++i]);
            if (!parsed.has_value()) {
                err << "Error: Invalid track number for '--seek-track'.\n";
                return 1;
            }
            initial_seek_track = *parsed;
        } else if (arg == "--seek-lba") {
            if (i + 1 >= argc) {
                err << "Error: '--seek-lba' requires an LBA integer argument.\n";
                return 1;
            }
            const auto parsed = parse_strict_integer<std::int64_t>(argv[++i]);
            if (!parsed.has_value()) {
                err << "Error: Invalid LBA for '--seek-lba'.\n";
                return 1;
            }
            initial_seek_lba = *parsed;
        } else {
            err << "Error: Unknown argument '" << arg << "'.\n";
            print_cli_usage(prog, err);
            return 1;
        }
    }

    if (run_test_tone) {
        out << "[AudioOutput] Playing 440 Hz sine wave test signal for "
            << test_tone_seconds << "s (44.1 kHz, 16-bit S16LE stereo)...\n";
        std::string tone_err;
        if (!play_test_tone(440.0, test_tone_seconds, audio_driver, tone_err)) {
            err << "Error: AudioOutput test tone failed: " << tone_err << "\n";
            return 1;
        }
        out << "[AudioOutput] Test tone playback completed successfully.\n";
        if (specs.empty()) {
            return 0;
        }
    }

    if (specs.empty()) {
        err << "Error: No --device or --image source specified.\n";
        print_cli_usage(prog, err);
        return 1;
    }

    if (specs.size() > 1 && !inspect_only && !compare_mode) {
        err << "Error: Multiple sources specified. Provide a single --device <path> or "
            << "--image <path> (or pass --inspect / --compare to compare layouts).\n";
        return 1;
    }

    CddbCompatibleProvider provider(cddb_config);

    // One-shot inspection, comparison, or metadata subcommand (without opening AudioOutput)
    if (inspect_only || compare_mode || !metadata_subcmd.empty()) {
        std::vector<DiscLayout> layouts;
        for (const auto& spec : specs) {
            DiscLayout layout{};
            std::string inspect_err;
            const bool ok = spec.is_device
                                ? inspect_physical_cd(spec.path, layout, inspect_err)
                                : inspect_image(spec.path, layout, inspect_err);
            if (!ok) {
                err << "Error: " << inspect_err << "\n";
                return 1;
            }
            if (!custom_sidecar_path.empty() && MetadataStore::exists(custom_sidecar_path)) {
                try {
                    const auto rec = MetadataStore::loadSidecar(custom_sidecar_path);
                    MetadataMatcher::applyToLayout(layout, rec);
                } catch (const std::exception& ex) {
                    err << "Warning: Failed to load sidecar '" << custom_sidecar_path.string()
                        << "': " << ex.what() << "\n";
                }
            }
            if (inspect_only || compare_mode) {
                print_disc_inspection(layout);
            }
            layouts.push_back(std::move(layout));
        }

        if (layouts.size() >= 2) {
            if (!compare_disc_inspections(layouts[0], layouts[1])) {
                return 1;
            }
        }

        if (!metadata_subcmd.empty()) {
            auto& active_layout = layouts.front();
            if (metadata_subcmd == "lookup") {
                std::optional<std::size_t> preselected;
                if (!metadata_subcmd_arg.empty()) {
                    const auto parsed = parse_strict_integer<std::size_t>(metadata_subcmd_arg);
                    if (!parsed.has_value()) {
                        err << "Error: Invalid candidate selection index '"
                            << metadata_subcmd_arg << "'.\n";
                        return 1;
                    }
                    preselected = *parsed;
                }
                if (!run_interactive_metadata_lookup(
                        active_layout,
                        provider,
                        in,
                        out,
                        custom_sidecar_path,
                        preselected,
                        auto_save_lookup)) {
                    return 1;
                }
            } else if (metadata_subcmd == "show") {
                print_metadata_summary(active_layout, out);
            } else if (metadata_subcmd == "edit") {
                run_interactive_metadata_edit(active_layout, in, out);
                print_metadata_summary(active_layout, out);
                const auto target =
                    !custom_sidecar_path.empty()
                        ? custom_sidecar_path
                        : MetadataStore::defaultSidecarPath(active_layout);
                MetadataStore::saveSidecar(target, extract_metadata_record(active_layout));
                out << "[Metadata] Saved sidecar metadata to " << target.string() << "\n";
            } else if (metadata_subcmd == "save") {
                const auto target =
                    !metadata_subcmd_arg.empty()
                        ? std::filesystem::path(metadata_subcmd_arg)
                        : !custom_sidecar_path.empty()
                              ? custom_sidecar_path
                              : MetadataStore::defaultSidecarPath(active_layout);
                try {
                    MetadataStore::saveSidecar(target, extract_metadata_record(active_layout));
                    out << "[Metadata] Saved sidecar metadata to " << target.string() << "\n";
                } catch (const std::exception& ex) {
                    err << "Error: [Metadata Save] " << ex.what() << "\n";
                    return 1;
                }
            } else if (metadata_subcmd == "export-cue") {
                try {
                    const auto target_cue =
                        resolve_cue_export_target_path(active_layout, metadata_subcmd_arg);
                    const auto template_cue =
                        resolve_cue_template_path(active_layout, target_cue);
                    MetadataStore::exportToCue(
                        target_cue, extract_metadata_record(active_layout), template_cue);
                    out << "[Metadata] Exported metadata to CUE file: "
                        << target_cue.string() << "\n";
                } catch (const std::exception& ex) {
                    err << "Error: [Metadata CUE Export] " << ex.what() << "\n";
                    return 1;
                }
            } else {
                err << "Error: Unknown metadata subcommand '" << metadata_subcmd << "'.\n";
                return 1;
            }
        }

        return 0;
    }

    // Open single selected source in PlaybackController
    const auto& spec = specs.front();
    PlaybackController controller(PlaybackControllerConfig{
        .audioConfig = {
            .ringBufferSectors = 225,
            .preferredDriver = audio_driver,
        },
        .readChunkSectors = 16,
        .prebufferSectors = 30,
    });
    ScopeCleanup cleanup{controller};

    try {
        auto src = spec.is_device
                       ? open_physical_cd_source(spec.path)
                       : open_image_source(spec.path);
        controller.openSource(std::move(src));

        if (initial_seek_track.has_value()) {
            controller.seekToTrack(*initial_seek_track);
        }
        if (initial_seek_lba.has_value()) {
            controller.seekToLba(*initial_seek_lba);
        }
    } catch (const std::exception& ex) {
        err << "Error: " << format_exception_diagnostic(ex) << "\n";
        return 1;
    }

    // Timed non-interactive playback window (--play <seconds>)
    if (timed_play_seconds.has_value()) {
        try {
            controller.play();
            const auto start_tp = std::chrono::steady_clock::now();
            std::optional<int> last_reported_track{};

            while (interrupt_signal == nullptr || *interrupt_signal == 0) {
                const auto st = controller.status();
                if (st.state == PlaybackState::Error) {
                    err << "Error: [Playback] " << st.errorMessage << "\n";
                    return 1;
                }
                if (st.position.trackNumber != last_reported_track &&
                    st.position.trackNumber.has_value()) {
                    last_reported_track = st.position.trackNumber;
                    out << "[Playback] Playing Track " << *st.position.trackNumber
                        << " (INDEX " << std::setfill('0') << std::setw(2)
                        << st.position.indexNumber.value_or(1) << std::setfill(' ')
                        << ", LBA " << st.position.lba << ")\n";
                }
                if (st.state == PlaybackState::Stopped && st.endOfDisc) {
                    out << "[Playback] Reached end of disc at LBA "
                        << st.position.lba << ".\n";
                    break;
                }
                const double elapsed =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - start_tp)
                        .count();
                if (elapsed >= *timed_play_seconds) {
                    const auto end_st = controller.status();
                    out << "[Playback] Completed " << *timed_play_seconds
                        << "s playback window at LBA " << end_st.position.lba
                        << " (Track " << end_st.position.trackNumber.value_or(0)
                        << ", INDEX " << std::setfill('0') << std::setw(2)
                        << end_st.position.indexNumber.value_or(0) << std::setfill(' ')
                        << ").\n";
                    controller.stop();
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(15));
            }

            if (interrupt_signal != nullptr && *interrupt_signal != 0) {
                const int sig = *interrupt_signal;
                out << "\n[Playback] Interrupted by signal " << sig
                    << "; stopped cleanly.\n";
                return 128 + sig;
            }
            return 0;
        } catch (const std::exception& ex) {
            err << "Error: " << format_exception_diagnostic(ex) << "\n";
            return 1;
        }
    }

    // Interactive console loop (Phase 6)
    CommandLoop loop(
        controller,
        in,
        out,
        err,
        CommandLoopOptions{
            .metadataProvider = &provider,
            .customSidecarPath = custom_sidecar_path,
            .autoPlayOnStart = auto_play,
            .interruptSignal = interrupt_signal,
        });
    return loop.run();
}

} // namespace mycds
