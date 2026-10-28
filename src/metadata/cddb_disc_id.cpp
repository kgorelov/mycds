#include "metadata/cddb_disc_id.hpp"

#include "domain/audio_format.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace mycds {

int cddb_sum_digits(std::int64_t value) noexcept {
    if (value < 0) {
        value = -value;
    }
    int sum = 0;
    while (value > 0) {
        sum += static_cast<int>(value % 10);
        value /= 10;
    }
    return sum;
}

CddbDiscQueryInfo calculate_cddb_disc_id(
    const DiscInfo& disc_info,
    const std::vector<TrackInfo>& tracks) {
    if (tracks.empty()) {
        throw std::invalid_argument("Cannot calculate CDDB disc ID: disc has no tracks");
    }
    if (tracks.size() > 99) {
        throw std::invalid_argument("Cannot calculate CDDB disc ID: track count exceeds 99");
    }
    if (disc_info.endLba <= disc_info.startLba) {
        throw std::invalid_argument("Cannot calculate CDDB disc ID: invalid disc LBA range");
    }

    CddbDiscQueryInfo info{};
    info.trackCount = static_cast<int>(tracks.size());
    info.trackFrameOffsets.reserve(tracks.size());

    int checksum = 0;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        const std::int64_t frame_offset = tracks[i].index01Lba + kCddbLeadInFrameOffset;
        if (frame_offset < 0) {
            throw std::invalid_argument("Cannot calculate CDDB disc ID: negative track frame offset");
        }
        if (i > 0 && frame_offset < info.trackFrameOffsets.back()) {
            throw std::invalid_argument("Cannot calculate CDDB disc ID: track offsets are not monotonic");
        }
        info.trackFrameOffsets.push_back(frame_offset);
        checksum += cddb_sum_digits(frame_offset / AudioFormat::kSectorsPerSecond);
    }

    const std::int64_t leadout_frame = disc_info.endLba + kCddbLeadInFrameOffset;
    if (leadout_frame <= info.trackFrameOffsets.back()) {
        throw std::invalid_argument("Cannot calculate CDDB disc ID: lead-out frame precedes last track");
    }

    info.totalLengthSeconds = leadout_frame / AudioFormat::kSectorsPerSecond;
    const std::int64_t first_track_seconds =
        info.trackFrameOffsets.front() / AudioFormat::kSectorsPerSecond;
    const std::int64_t span_seconds = info.totalLengthSeconds - first_track_seconds;

    const std::uint32_t xx = static_cast<std::uint32_t>(checksum % 0xFF) & 0xFFU;
    const std::uint32_t yyyy = static_cast<std::uint32_t>(span_seconds) & 0xFFFFU;
    const std::uint32_t zz = static_cast<std::uint32_t>(info.trackCount) & 0xFFU;

    info.discIdNumeric = (xx << 24U) | (yyyy << 8U) | zz;

    std::ostringstream hex_out;
    hex_out << std::hex << std::nouppercase << std::setfill('0') << std::setw(8)
            << info.discIdNumeric;
    info.discIdHex = hex_out.str();

    return info;
}

std::string format_cddb_query_command(const CddbDiscQueryInfo& query_info) {
    if (query_info.discIdHex.empty() || query_info.trackFrameOffsets.empty()) {
        throw std::invalid_argument("Invalid CddbDiscQueryInfo for query command formatting");
    }
    std::ostringstream cmd;
    cmd << "cddb query " << query_info.discIdHex << " " << query_info.trackCount;
    for (const std::int64_t offset : query_info.trackFrameOffsets) {
        cmd << " " << offset;
    }
    cmd << " " << query_info.totalLengthSeconds;
    return cmd.str();
}

} // namespace mycds
