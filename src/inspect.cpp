#include "inspect.hpp"

#include "domain/audio_format.hpp"

#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>

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

} // namespace

bool inspect_physical_cd(
    const std::string& device_path,
    DiscLayout& out_disc,
    std::string& out_error) {
    try {
        auto source = open_physical_cd_source(device_path);
        out_disc = source->getDiscLayout();
        return true;
    } catch (const std::exception& ex) {
        out_error = ex.what();
        return false;
    }
}

bool inspect_image(
    const std::string& image_path,
    DiscLayout& out_disc,
    std::string& out_error) {
    try {
        auto source = open_image_source(image_path);
        out_disc = source->getDiscLayout();
        return true;
    } catch (const std::exception& ex) {
        out_error = ex.what();
        return false;
    }
}

void print_disc_inspection(const DiscLayout& disc) {
    const auto& info = disc.discInfo;
    const auto& meta = disc.discMetadata;
    const std::int64_t total_sectors = info.endLba - info.startLba;

    std::cout << "====================================================================================\n";
    std::cout << "Source Type : " << disc.sourceKind << "\n";
    std::cout << "Source Path : " << disc.sourcePath << "\n";
    std::cout << "Sessions    : " << info.sessions << "\n";
    std::cout << "Tracks      : " << info.firstTrack << " .. " << info.lastTrack
              << " (" << disc.tracks.size() << " total)\n";
    std::cout << "LBA Range   : [" << info.startLba << ", " << info.endLba << ") "
              << "(" << total_sectors << " sectors, " << sectors_to_msf(total_sectors) << ")\n";
    std::cout << "Raw Range   : [" << info.rawStartSector << ", " << info.rawEndSector << ")\n";

    if (!meta.empty()) {
        std::cout << "------------------------------------------------------------------------------------\n";
        std::cout << "Disc Metadata:\n";
        if (!meta.title.empty()) {
            std::cout << "  Title      : " << meta.title << "\n";
        }
        if (!meta.performer.empty()) {
            std::cout << "  Performer  : " << meta.performer << "\n";
        }
        if (!meta.songwriter.empty()) {
            std::cout << "  Songwriter : " << meta.songwriter << "\n";
        }
        if (!meta.catalog.empty()) {
            std::cout << "  Catalog    : " << meta.catalog << "\n";
        }
        if (!meta.genre.empty()) {
            std::cout << "  Genre      : " << meta.genre << "\n";
        }
        if (meta.releaseYear.has_value()) {
            std::cout << "  Year       : " << *meta.releaseYear << "\n";
        }
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
            idx_str << "I" << std::setfill('0') << std::setw(2) << trk.indexes[i].number
                    << ":" << trk.indexes[i].lba;
        }
        if (trk.postgapLength > 0) {
            idx_str << " [postgap:" << trk.postgapLength << "f]";
        }

        std::cout << std::setfill(' ')
                  << std::right << std::setw(3) << trk.number << "  "
                  << std::left << std::setw(7) << track_type_to_string(trk.type) << "  "
                  << std::right
                  << std::setw(9) << trk.startLba << "  "
                  << std::setw(9) << trk.index01Lba << "  "
                  << std::setw(9) << trk.endLba << "  "
                  << std::setw(7) << length << "  "
                  << std::setw(8) << sectors_to_msf(length) << "  "
                  << std::left << std::setw(10) << pregap_str.str() << "  "
                  << idx_str.str() << "\n";
    }

    bool any_track_meta = false;
    for (const auto& tm : disc.trackMetadata) {
        if (!tm.empty()) {
            any_track_meta = true;
            break;
        }
    }

    if (any_track_meta) {
        std::cout << "------------------------------------------------------------------------------------\n";
        std::cout << "Track Metadata:\n";
        for (const auto& tm : disc.trackMetadata) {
            if (tm.empty()) {
                continue;
            }
            std::cout << "  Track " << std::right << std::setw(2) << tm.trackNumber << ":";
            if (!tm.title.empty()) {
                std::cout << " Title=\"" << tm.title << "\"";
            }
            if (!tm.performer.empty()) {
                std::cout << " Performer=\"" << tm.performer << "\"";
            }
            if (!tm.songwriter.empty()) {
                std::cout << " Songwriter=\"" << tm.songwriter << "\"";
            }
            if (!tm.isrc.empty()) {
                std::cout << " ISRC=" << tm.isrc;
            }
            std::cout << "\n";
        }
    }

    std::cout << "====================================================================================\n";
}

bool compare_disc_inspections(
    const DiscLayout& lhs,
    const DiscLayout& rhs) {
    std::cout << "\nComparison: " << lhs.sourceKind << " (" << lhs.sourcePath
              << ") vs " << rhs.sourceKind << " (" << rhs.sourcePath << ")\n";
    std::cout << "------------------------------------------------------------------------------------\n";

    bool consistent = true;

    if (lhs.discInfo.firstTrack != rhs.discInfo.firstTrack ||
        lhs.discInfo.lastTrack != rhs.discInfo.lastTrack ||
        lhs.tracks.size() != rhs.tracks.size()) {
        std::cout << "[MISMATCH] Track range: lhs=" << lhs.discInfo.firstTrack << ".."
                  << lhs.discInfo.lastTrack << " vs rhs=" << rhs.discInfo.firstTrack << ".."
                  << rhs.discInfo.lastTrack << "\n";
        return false;
    }

    if (lhs.discInfo.startLba != rhs.discInfo.startLba ||
        lhs.discInfo.endLba != rhs.discInfo.endLba) {
        std::cout << "[MISMATCH] Disc LBA range: lhs=[" << lhs.discInfo.startLba << ", "
                  << lhs.discInfo.endLba << ") vs rhs=[" << rhs.discInfo.startLba << ", "
                  << rhs.discInfo.endLba << ")\n";
        consistent = false;
    } else {
        std::cout << "[OK] Disc LBA range matches: [" << lhs.discInfo.startLba << ", "
                  << lhs.discInfo.endLba << ")\n";
    }

    for (std::size_t i = 0; i < lhs.tracks.size(); ++i) {
        const auto& lt = lhs.tracks[i];
        const auto& rt = rhs.tracks[i];

        if (lt.number != rt.number || lt.type != rt.type) {
            std::cout << "[MISMATCH] Track " << lt.number << " number/type mismatch\n";
            consistent = false;
            continue;
        }

        if (lt.index01Lba != rt.index01Lba) {
            std::cout << "[MISMATCH] Track " << lt.number << " INDEX 01 LBA: lhs="
                      << lt.index01Lba << " vs rhs=" << rt.index01Lba << "\n";
            consistent = false;
        }

        const std::int64_t lhs_pregap = lt.pregapLength.value_or(0);
        const std::int64_t rhs_pregap = rt.pregapLength.value_or(0);
        const std::int64_t next_lhs_pregap =
            (i + 1 < lhs.tracks.size()) ? lhs.tracks[i + 1].pregapLength.value_or(0) : 0;
        const std::int64_t next_rhs_pregap =
            (i + 1 < rhs.tracks.size()) ? rhs.tracks[i + 1].pregapLength.value_or(0) : 0;

        const bool start_matches_index01 =
            (lt.startLba + lhs_pregap == rt.startLba + rhs_pregap);
        const bool end_matches_next_index01 =
            (lt.endLba + next_lhs_pregap == rt.endLba + next_rhs_pregap);

        if (lt.startLba == rt.startLba && lt.endLba == rt.endLba) {
            std::cout << std::right << "[OK] Track " << std::setw(2) << lt.number
                      << ": INDEX 01=" << std::setw(6) << lt.index01Lba
                      << ", range=[" << std::setw(6) << lt.startLba << ", "
                      << std::setw(6) << lt.endLba << ")\n";
        } else if (start_matches_index01 && end_matches_next_index01) {
            std::cout << std::right << "[OK] Track " << std::setw(2) << lt.number
                      << ": INDEX 01=" << std::setw(6) << lt.index01Lba
                      << " matches; INDEX 00 pregap difference ("
                      << "lhs=[" << lt.startLba << ", " << lt.endLba << "), "
                      << "rhs=[" << rt.startLba << ", " << rt.endLba << "))\n";
        } else {
            std::cout << std::right << "[MISMATCH] Track " << std::setw(2) << lt.number
                      << ": lhs=[" << lt.startLba << ", " << lt.endLba << ") "
                      << "vs rhs=[" << rt.startLba << ", " << rt.endLba << ")\n";
            consistent = false;
        }
    }

    std::cout << "------------------------------------------------------------------------------------\n";
    std::cout << "Overall Layout Consistency: " << (consistent ? "PASS" : "FAIL") << "\n";
    return consistent;
}

} // namespace mycds
