#include "metadata/metadata_matcher.hpp"

#include "domain/audio_format.hpp"
#include "metadata/cddb_disc_id.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace mycds {

std::string match_quality_to_string(MatchQuality quality) {
    switch (quality) {
        case MatchQuality::ExactOffsetMatch:
            return "Exact Offset Match";
        case MatchQuality::CloseOffsetMatch:
            return "Close Offset Match (<= 2s delta)";
        case MatchQuality::MetadataOnlyMatch:
            return "Track Count & Order Match (no remote offsets)";
        case MatchQuality::OffsetMismatch:
            return "Offset Mismatch (possible disc ID collision or different pressing)";
        case MatchQuality::LayoutMismatch:
            return "Layout Mismatch (invalid track count or order)";
    }
    return "Unknown";
}

MetadataValidationResult MetadataMatcher::validate(
    const DiscInfo& local_disc,
    const std::vector<TrackInfo>& local_tracks,
    const MetadataRecord& record) {
    MetadataValidationResult result{};

    if (local_tracks.empty()) {
        result.errors.push_back("Local disc has no tracks");
        return result;
    }

    if (record.tracks.size() != local_tracks.size()) {
        result.trackCountMatches = false;
        result.errors.push_back(
            "Track count mismatch: remote record has " + std::to_string(record.tracks.size()) +
            " tracks, but local disc has " + std::to_string(local_tracks.size()) + " tracks");
        result.quality = MatchQuality::LayoutMismatch;
        return result;
    }
    result.trackCountMatches = true;

    result.trackOrderValid = true;
    result.allTrackTitlesPresent = true;
    for (std::size_t i = 0; i < local_tracks.size(); ++i) {
        if (record.tracks[i].trackNumber != local_tracks[i].number) {
            result.trackOrderValid = false;
            result.errors.push_back(
                "Track number ordering mismatch at index " + std::to_string(i) +
                ": remote track number is " + std::to_string(record.tracks[i].trackNumber) +
                ", local track number is " + std::to_string(local_tracks[i].number));
        }
        if (record.tracks[i].title.empty()) {
            result.allTrackTitlesPresent = false;
            result.warnings.push_back(
                "Track " + std::to_string(local_tracks[i].number) + " has an empty title");
        }
    }

    if (record.disc.title.empty()) {
        result.warnings.push_back("Album title is empty in metadata record");
    }

    if (!result.trackOrderValid) {
        result.quality = MatchQuality::LayoutMismatch;
        return result;
    }

    result.valid = true;

    if (!record.remoteTrackFrameOffsets.empty()) {
        if (record.remoteTrackFrameOffsets.size() != local_tracks.size()) {
            result.warnings.push_back(
                "Remote frame offset list count (" +
                std::to_string(record.remoteTrackFrameOffsets.size()) +
                ") does not match track count (" + std::to_string(local_tracks.size()) + ")");
            result.quality = MatchQuality::OffsetMismatch;
            return result;
        }

        result.offsetsAvailable = true;
        std::int64_t max_delta = 0;
        for (std::size_t i = 0; i < local_tracks.size(); ++i) {
            const std::int64_t local_frame =
                local_tracks[i].index01Lba + kCddbLeadInFrameOffset;
            const std::int64_t delta =
                std::llabs(record.remoteTrackFrameOffsets[i] - local_frame);
            if (delta > max_delta) {
                max_delta = delta;
            }
        }
        result.maxFrameOffsetDelta = max_delta;

        if (record.remoteDiscLengthSeconds.has_value()) {
            const std::int64_t local_seconds =
                (local_disc.endLba + kCddbLeadInFrameOffset) / AudioFormat::kSectorsPerSecond;
            result.discLengthSecondsDelta =
                std::llabs(*record.remoteDiscLengthSeconds - local_seconds);
        }

        if (result.maxFrameOffsetDelta == 0 && result.discLengthSecondsDelta == 0) {
            result.quality = MatchQuality::ExactOffsetMatch;
        } else if (result.maxFrameOffsetDelta <= 150 && result.discLengthSecondsDelta <= 2) {
            result.quality = MatchQuality::CloseOffsetMatch;
            result.warnings.push_back(
                "Track offsets differ by up to " + std::to_string(result.maxFrameOffsetDelta) +
                " frames (disc length delta: " + std::to_string(result.discLengthSecondsDelta) +
                "s)");
        } else {
            result.quality = MatchQuality::OffsetMismatch;
            result.warnings.push_back(
                "Significant track offset difference (max delta = " +
                std::to_string(result.maxFrameOffsetDelta) +
                " frames, disc length delta = " +
                std::to_string(result.discLengthSecondsDelta) +
                "s); candidate may be a different pressing or CDDB disc ID collision");
        }
    } else {
        result.quality = MatchQuality::MetadataOnlyMatch;
    }

    return result;
}

void MetadataMatcher::applyToLayout(
    DiscLayout& layout,
    const MetadataRecord& record) {
    const std::string existing_catalog = layout.discMetadata.catalog;
    layout.discMetadata = record.disc;
    if (layout.discMetadata.catalog.empty() && !existing_catalog.empty()) {
        layout.discMetadata.catalog = existing_catalog;
    }

    std::vector<TrackMetadata> merged_tracks;
    merged_tracks.reserve(layout.tracks.size());

    for (std::size_t i = 0; i < layout.tracks.size(); ++i) {
        const int trk_num = layout.tracks[i].number;
        TrackMetadata existing_tm{};
        existing_tm.trackNumber = trk_num;
        for (const auto& old_tm : layout.trackMetadata) {
            if (old_tm.trackNumber == trk_num) {
                existing_tm = old_tm;
                break;
            }
        }

        TrackMetadata incoming_tm = existing_tm;
        for (const auto& new_tm : record.tracks) {
            if (new_tm.trackNumber == trk_num) {
                incoming_tm = new_tm;
                if (incoming_tm.isrc.empty() && !existing_tm.isrc.empty()) {
                    incoming_tm.isrc = existing_tm.isrc;
                }
                break;
            }
        }
        merged_tracks.push_back(std::move(incoming_tm));
    }

    layout.trackMetadata = std::move(merged_tracks);
}

} // namespace mycds
