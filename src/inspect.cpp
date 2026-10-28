#include "inspect.hpp"

#include <cstdio>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <cdio/cdio.h>
#include <cdio/disc.h>
#include <cdio/sector.h>
#include <cdio/track.h>

#include <mirage/mirage.h>

namespace mycds {

namespace {

std::string track_type_to_string(TrackType type) {
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

std::string sectors_to_msf_string(std::int64_t sectors) {
    const bool negative = sectors < 0;
    const std::int64_t abs_sectors = negative ? -sectors : sectors;
    const std::int64_t minutes = abs_sectors / (60 * 75);
    const std::int64_t seconds = (abs_sectors / 75) % 60;
    const std::int64_t frames = abs_sectors % 75;

    std::ostringstream oss;
    if (negative) {
        oss << "-";
    }
    oss << std::setfill('0')
        << std::setw(2) << minutes << ":"
        << std::setw(2) << seconds << ":"
        << std::setw(2) << frames;
    return oss.str();
}

} // namespace

bool inspect_physical_cd(
    const std::string& device_path,
    DiscInspection& out_disc,
    std::string& out_error) {
    if (!cdio_init()) {
        out_error = "Failed to initialize libcdio";
        return false;
    }

    CdIo_t* cdio = cdio_open(device_path.c_str(), DRIVER_UNKNOWN);
    if (!cdio) {
        out_error = "Failed to open optical drive: " + device_path;
        return false;
    }

    const discmode_t disc_mode = cdio_get_discmode(cdio);
    if (disc_mode == CDIO_DISC_MODE_NO_INFO || disc_mode == CDIO_DISC_MODE_ERROR) {
        cdio_destroy(cdio);
        out_error = "No readable disc found in drive: " + device_path;
        return false;
    }

    const track_t first_track = cdio_get_first_track_num(cdio);
    const track_t last_track = cdio_get_last_track_num(cdio);
    if (first_track == CDIO_INVALID_TRACK || last_track == CDIO_INVALID_TRACK ||
        first_track > last_track) {
        cdio_destroy(cdio);
        out_error = "Invalid track table reported by drive: " + device_path;
        return false;
    }

    out_disc = DiscInspection{};
    out_disc.sourceKind = "Physical CD (libcdio)";
    out_disc.sourcePath = device_path;
    out_disc.sessions = 1;
    out_disc.firstTrack = static_cast<int>(first_track);
    out_disc.lastTrack = static_cast<int>(last_track);

    if (char* mcn = cdio_get_mcn(cdio)) {
        out_disc.mcn = mcn;
        cdio_free(mcn);
    }

    for (track_t t = first_track; t <= last_track; ++t) {
        TrackInspection track_info{};
        track_info.number = static_cast<int>(t);

        const track_format_t fmt = cdio_get_track_format(cdio, t);
        if (fmt == TRACK_FORMAT_AUDIO) {
            track_info.type = TrackType::Audio;
        } else if (fmt == TRACK_FORMAT_DATA || fmt == TRACK_FORMAT_XA ||
                   fmt == TRACK_FORMAT_CDI || fmt == TRACK_FORMAT_PSX) {
            track_info.type = TrackType::Data;
        } else {
            track_info.type = TrackType::Unknown;
        }

        // In libcdio, lsn_t is the 0-based logical sector address (where Track 1 INDEX 01 = 0,
        // i.e., MSF 00:02:00), whereas libcdio's lba_t adds CDIO_PREGAP_SECTORS (+150).
        // We normalize to 0-based LBA (matching cdio_read_audio_sectors and libMirage).
        const lsn_t index01_lsn = cdio_get_track_lsn(cdio, t);
        const lsn_t last_lsn = cdio_get_track_last_lsn(cdio, t);
        const lsn_t pregap_lsn = cdio_get_track_pregap_lsn(cdio, t);

        if (index01_lsn == CDIO_INVALID_LSN || last_lsn == CDIO_INVALID_LSN) {
            cdio_destroy(cdio);
            out_error = "Failed to query LSN range for track " + std::to_string(t);
            return false;
        }

        track_info.index01Lba = static_cast<std::int64_t>(index01_lsn);
        track_info.endLba = static_cast<std::int64_t>(last_lsn) + 1;

        if (pregap_lsn != CDIO_INVALID_LSN && pregap_lsn < index01_lsn) {
            track_info.startLba = static_cast<std::int64_t>(pregap_lsn);
            track_info.pregapStartLba = track_info.startLba;
            track_info.pregapLength = track_info.index01Lba - track_info.startLba;
            track_info.indexes.push_back(IndexEntry{
                .trackNumber = track_info.number,
                .indexNumber = 0,
                .lba = track_info.startLba,
                .explicitFromSource = true,
            });
        } else {
            track_info.startLba = track_info.index01Lba;
        }

        track_info.indexes.push_back(IndexEntry{
            .trackNumber = track_info.number,
            .indexNumber = 1,
            .lba = track_info.index01Lba,
            .explicitFromSource = true,
        });

        if (char* isrc = cdio_get_track_isrc(cdio, t)) {
            track_info.isrc = isrc;
            cdio_free(isrc);
        }

        out_disc.tracks.push_back(std::move(track_info));
    }

    out_disc.startLba = out_disc.tracks.front().startLba;
    out_disc.endLba = out_disc.tracks.back().endLba;
    out_disc.rawStartSector = static_cast<std::int64_t>(cdio_get_track_lba(cdio, first_track));
    out_disc.rawEndSector = static_cast<std::int64_t>(cdio_get_track_lba(cdio, CDIO_CDROM_LEADOUT_TRACK));

    cdio_destroy(cdio);
    return true;
}

bool inspect_image(
    const std::string& image_path,
    DiscInspection& out_disc,
    std::string& out_error) {
    GError* error = nullptr;
    if (!mirage_initialize(&error)) {
        out_error = "Failed to initialize libMirage: ";
        out_error += (error ? error->message : "unknown error");
        if (error) {
            g_error_free(error);
        }
        return false;
    }

    MirageContext* context = MIRAGE_CONTEXT(g_object_new(MIRAGE_TYPE_CONTEXT, nullptr));
    gchar* filenames[] = {const_cast<gchar*>(image_path.c_str()), nullptr};
    MirageDisc* disc = mirage_context_load_image(context, filenames, &error);
    if (!disc) {
        out_error = "Failed to load image '" + image_path + "': ";
        out_error += (error ? error->message : "unknown error");
        if (error) {
            g_error_free(error);
        }
        g_object_unref(context);
        return false;
    }

    const gint num_sessions = mirage_disc_get_number_of_sessions(disc);
    const gint num_tracks = mirage_disc_get_number_of_tracks(disc);
    if (num_tracks <= 0) {
        g_object_unref(disc);
        g_object_unref(context);
        out_error = "Image contains no tracks: " + image_path;
        return false;
    }

    out_disc = DiscInspection{};
    out_disc.sourceKind = "Disc Image (libMirage)";
    out_disc.sourcePath = image_path;
    out_disc.sessions = static_cast<int>(num_sessions);
    out_disc.rawStartSector = static_cast<std::int64_t>(mirage_disc_layout_get_start_sector(disc));
    out_disc.rawEndSector =
        out_disc.rawStartSector + static_cast<std::int64_t>(mirage_disc_layout_get_length(disc));

    if (MirageSession* session = mirage_disc_get_session_by_index(disc, 0, nullptr)) {
        if (const gchar* mcn = mirage_session_get_mcn(session)) {
            out_disc.mcn = mcn;
        }
        g_object_unref(session);
    }

    for (gint i = 0; i < num_tracks; ++i) {
        MirageTrack* track = mirage_disc_get_track_by_index(disc, i, &error);
        if (!track) {
            out_error = "Failed to get track at index " + std::to_string(i);
            if (error) {
                g_error_free(error);
            }
            g_object_unref(disc);
            g_object_unref(context);
            return false;
        }

        TrackInspection track_info{};
        track_info.number = static_cast<int>(mirage_track_layout_get_track_number(track));

        const MirageSectorType sec_type = mirage_track_get_sector_type(track);
        if (sec_type == MIRAGE_SECTOR_AUDIO) {
            track_info.type = TrackType::Audio;
        } else if (sec_type == MIRAGE_SECTOR_MODE1 ||
                   sec_type == MIRAGE_SECTOR_MODE2 ||
                   sec_type == MIRAGE_SECTOR_MODE2_FORM1 ||
                   sec_type == MIRAGE_SECTOR_MODE2_FORM2 ||
                   sec_type == MIRAGE_SECTOR_MODE2_MIXED) {
            track_info.type = TrackType::Data;
        } else {
            track_info.type = TrackType::Unknown;
        }

        const std::int64_t raw_track_start =
            static_cast<std::int64_t>(mirage_track_layout_get_start_sector(track));
        const std::int64_t raw_track_len =
            static_cast<std::int64_t>(mirage_track_layout_get_length(track));
        const std::int64_t track_start_rel =
            static_cast<std::int64_t>(mirage_track_get_track_start(track));

        // libMirage's mirage_parser_add_redbook_pregap() prepends a 150-sector NULL
        // fragment covering [-150, 0) to the first track of each CD session.
        // Detect this synthetic lead-in fragment so normalized LBAs start at 0.
        std::int64_t redbook_leadin_sectors = 0;
        if (raw_track_start == -150 && mirage_track_get_number_of_fragments(track) > 0) {
            if (MirageFragment* frag0 = mirage_track_get_fragment_by_index(track, 0, nullptr)) {
                if (mirage_fragment_main_data_get_size(frag0) == 0 &&
                    mirage_fragment_get_length(frag0) == 150) {
                    redbook_leadin_sectors = 150;
                }
                g_object_unref(frag0);
            }
        }

        track_info.startLba = raw_track_start + redbook_leadin_sectors;
        track_info.endLba = raw_track_start + raw_track_len;
        track_info.index01Lba = raw_track_start + track_start_rel;

        const std::int64_t pregap_len = track_info.index01Lba - track_info.startLba;
        track_info.pregapLength = pregap_len;
        if (pregap_len > 0) {
            track_info.pregapStartLba = track_info.startLba;
            track_info.indexes.push_back(IndexEntry{
                .trackNumber = track_info.number,
                .indexNumber = 0,
                .lba = track_info.startLba,
                .explicitFromSource = true,
            });
        }

        track_info.indexes.push_back(IndexEntry{
            .trackNumber = track_info.number,
            .indexNumber = 1,
            .lba = track_info.index01Lba,
            .explicitFromSource = true,
        });

        const gint num_extra_indices = mirage_track_get_number_of_indices(track);
        for (gint idx = 0; idx < num_extra_indices; ++idx) {
            if (MirageIndex* mi = mirage_track_get_index_by_number(track, idx, nullptr)) {
                const int idx_num = static_cast<int>(mirage_index_get_number(mi));
                const std::int64_t idx_rel = static_cast<std::int64_t>(mirage_index_get_address(mi));
                track_info.indexes.push_back(IndexEntry{
                    .trackNumber = track_info.number,
                    .indexNumber = idx_num,
                    .lba = raw_track_start + idx_rel,
                    .explicitFromSource = true,
                });
                g_object_unref(mi);
            }
        }

        if (const gchar* isrc = mirage_track_get_isrc(track)) {
            track_info.isrc = isrc;
        }

        out_disc.tracks.push_back(std::move(track_info));
        g_object_unref(track);
    }

    out_disc.firstTrack = out_disc.tracks.front().number;
    out_disc.lastTrack = out_disc.tracks.back().number;
    out_disc.startLba = out_disc.tracks.front().startLba;
    out_disc.endLba = out_disc.tracks.back().endLba;

    g_object_unref(disc);
    g_object_unref(context);
    return true;
}

void print_disc_inspection(const DiscInspection& disc) {
    const std::int64_t total_sectors = disc.endLba - disc.startLba;
    std::cout << "====================================================================================\n";
    std::cout << "Source Type : " << disc.sourceKind << "\n";
    std::cout << "Source Path : " << disc.sourcePath << "\n";
    std::cout << "Sessions    : " << disc.sessions << "\n";
    std::cout << "Tracks      : " << disc.firstTrack << " .. " << disc.lastTrack
              << " (" << disc.tracks.size() << " total)\n";
    std::cout << "LBA Range   : [" << disc.startLba << ", " << disc.endLba << ") "
              << "(" << total_sectors << " sectors, " << sectors_to_msf_string(total_sectors) << ")\n";
    std::cout << "Raw Range   : [" << disc.rawStartSector << ", " << disc.rawEndSector << ")\n";
    if (!disc.mcn.empty()) {
        std::cout << "MCN         : " << disc.mcn << "\n";
    }
    std::cout << "------------------------------------------------------------------------------------\n";
    std::cout << "Trk  Type     Start LBA  INDEX 01   End LBA    Length   Duration  Pregap      Indexes\n";
    std::cout << "------------------------------------------------------------------------------------\n";

    for (const auto& trk : disc.tracks) {
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
            idx_str << "I" << std::setfill('0') << std::setw(2) << trk.indexes[i].indexNumber
                    << ":" << trk.indexes[i].lba;
        }

        std::cout << std::setfill(' ')
                  << std::right << std::setw(3) << trk.number << "  "
                  << std::left << std::setw(7) << track_type_to_string(trk.type) << "  "
                  << std::right
                  << std::setw(9) << trk.startLba << "  "
                  << std::setw(9) << trk.index01Lba << "  "
                  << std::setw(9) << trk.endLba << "  "
                  << std::setw(7) << length << "  "
                  << std::setw(8) << sectors_to_msf_string(length) << "  "
                  << std::left << std::setw(10) << pregap_str.str() << "  "
                  << idx_str.str() << "\n";
    }
    std::cout << "====================================================================================\n";
}

bool compare_disc_inspections(
    const DiscInspection& physical,
    const DiscInspection& image) {
    std::cout << "\nComparison: Physical CD (" << physical.sourcePath
              << ") vs Image (" << image.sourcePath << ")\n";
    std::cout << "------------------------------------------------------------------------------------\n";

    bool consistent = true;

    if (physical.firstTrack != image.firstTrack ||
        physical.lastTrack != image.lastTrack ||
        physical.tracks.size() != image.tracks.size()) {
        std::cout << "[MISMATCH] Track range: physical=" << physical.firstTrack << ".." << physical.lastTrack
                  << " vs image=" << image.firstTrack << ".." << image.lastTrack << "\n";
        return false;
    }

    if (physical.startLba != image.startLba || physical.endLba != image.endLba) {
        std::cout << "[MISMATCH] Disc LBA range: physical=[" << physical.startLba << ", " << physical.endLba
                  << ") vs image=[" << image.startLba << ", " << image.endLba << ")\n";
        consistent = false;
    } else {
        std::cout << "[OK] Disc LBA range matches: [" << physical.startLba << ", " << physical.endLba << ")\n";
    }

    for (std::size_t i = 0; i < physical.tracks.size(); ++i) {
        const auto& pt = physical.tracks[i];
        const auto& it = image.tracks[i];

        if (pt.number != it.number || pt.type != it.type) {
            std::cout << "[MISMATCH] Track " << pt.number << " number/type mismatch\n";
            consistent = false;
            continue;
        }

        if (pt.index01Lba != it.index01Lba) {
            std::cout << "[MISMATCH] Track " << pt.number << " INDEX 01 LBA: physical="
                      << pt.index01Lba << " vs image=" << it.index01Lba << "\n";
            consistent = false;
        }

        // Check if startLba/endLba difference is explained by next/current track INDEX 00 pregap
        // (since physical CD Lead-In TOC only stores INDEX 01 addresses, whereas cdrdao TOC
        // records subchannel INDEX 00 pregaps).
        const std::int64_t img_pregap = it.pregapLength.value_or(0);
        const std::int64_t next_img_pregap =
            (i + 1 < image.tracks.size()) ? image.tracks[i + 1].pregapLength.value_or(0) : 0;

        const bool start_matches_index01 = (pt.startLba == it.startLba + img_pregap);
        const bool end_matches_next_index01 = (pt.endLba == it.endLba + next_img_pregap);

        if (pt.startLba == it.startLba && pt.endLba == it.endLba) {
            std::cout << std::right << "[OK] Track " << std::setw(2) << pt.number
                      << ": INDEX 01=" << std::setw(6) << pt.index01Lba
                      << ", range=[" << std::setw(6) << pt.startLba << ", "
                      << std::setw(6) << pt.endLba << ")\n";
        } else if (start_matches_index01 && end_matches_next_index01) {
            std::cout << std::right << "[OK] Track " << std::setw(2) << pt.number
                      << ": INDEX 01=" << std::setw(6) << pt.index01Lba
                      << " matches; image has explicit INDEX 00 pregap ("
                      << "phys=[" << pt.startLba << ", " << pt.endLba << "), "
                      << "img=[" << it.startLba << ", " << it.endLba << "))\n";
        } else {
            std::cout << std::right << "[MISMATCH] Track " << std::setw(2) << pt.number
                      << ": phys=[" << pt.startLba << ", " << pt.endLba << ") "
                      << "vs img=[" << it.startLba << ", " << it.endLba << ")\n";
            consistent = false;
        }
    }

    std::cout << "------------------------------------------------------------------------------------\n";
    std::cout << "Overall Layout Consistency: " << (consistent ? "PASS" : "FAIL") << "\n";
    return consistent;
}

} // namespace mycds
