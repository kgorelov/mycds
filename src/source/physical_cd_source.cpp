#include "source/physical_cd_source.hpp"

#include "domain/audio_format.hpp"

#include <stdexcept>
#include <utility>

#include <cdio/cdio.h>
#include <cdio/cdtext.h>
#include <cdio/disc.h>
#include <cdio/read.h>
#include <cdio/sector.h>
#include <cdio/track.h>

namespace mycds {

struct PhysicalCdSource::Impl {
    CdIo_t* cdio{nullptr};
    DiscLayout layout{};

    ~Impl() {
        if (cdio) {
            cdio_destroy(cdio);
            cdio = nullptr;
        }
    }
};

PhysicalCdSource::PhysicalCdSource(const std::string& device_path)
    : impl_(std::make_unique<Impl>()) {
    if (!cdio_init()) {
        throw std::runtime_error("Failed to initialize libcdio");
    }

    impl_->cdio = cdio_open(device_path.c_str(), DRIVER_UNKNOWN);
    if (!impl_->cdio) {
        throw std::runtime_error("Failed to open optical drive: " + device_path);
    }

    const discmode_t disc_mode = cdio_get_discmode(impl_->cdio);
    if (disc_mode == CDIO_DISC_MODE_NO_INFO || disc_mode == CDIO_DISC_MODE_ERROR) {
        throw std::runtime_error("No readable disc found in drive: " + device_path);
    }

    const track_t first_track = cdio_get_first_track_num(impl_->cdio);
    const track_t last_track = cdio_get_last_track_num(impl_->cdio);
    if (first_track == CDIO_INVALID_TRACK || last_track == CDIO_INVALID_TRACK ||
        first_track > last_track) {
        throw std::runtime_error("Invalid track table reported by drive: " + device_path);
    }

    impl_->layout.sourceKind = "Physical CD (libcdio)";
    impl_->layout.sourcePath = device_path;
    impl_->layout.discInfo.sessions = 1;
    impl_->layout.discInfo.firstTrack = static_cast<int>(first_track);
    impl_->layout.discInfo.lastTrack = static_cast<int>(last_track);

    if (char* mcn = cdio_get_mcn(impl_->cdio)) {
        impl_->layout.discMetadata.catalog = mcn;
        cdio_free(mcn);
    }

    cdtext_t* cdtext = cdio_get_cdtext(impl_->cdio);
    if (cdtext) {
        if (const char* title = cdtext_get_const(cdtext, CDTEXT_FIELD_TITLE, 0)) {
            impl_->layout.discMetadata.title = title;
        }
        if (const char* perf = cdtext_get_const(cdtext, CDTEXT_FIELD_PERFORMER, 0)) {
            impl_->layout.discMetadata.performer = perf;
        }
        if (const char* sw = cdtext_get_const(cdtext, CDTEXT_FIELD_SONGWRITER, 0)) {
            impl_->layout.discMetadata.songwriter = sw;
        }
        if (const char* genre = cdtext_get_const(cdtext, CDTEXT_FIELD_GENRE, 0)) {
            impl_->layout.discMetadata.genre = genre;
        }
    }

    for (track_t t = first_track; t <= last_track; ++t) {
        TrackInfo track_info{};
        track_info.number = static_cast<int>(t);

        const track_format_t fmt = cdio_get_track_format(impl_->cdio, t);
        if (fmt == TRACK_FORMAT_AUDIO) {
            track_info.type = TrackType::Audio;
        } else if (fmt == TRACK_FORMAT_DATA || fmt == TRACK_FORMAT_XA ||
                   fmt == TRACK_FORMAT_CDI || fmt == TRACK_FORMAT_PSX) {
            track_info.type = TrackType::Data;
        } else {
            track_info.type = TrackType::Unknown;
        }

        const lsn_t index01_lsn = cdio_get_track_lsn(impl_->cdio, t);
        const lsn_t last_lsn = cdio_get_track_last_lsn(impl_->cdio, t);
        const lsn_t pregap_lsn = cdio_get_track_pregap_lsn(impl_->cdio, t);

        if (index01_lsn == CDIO_INVALID_LSN || last_lsn == CDIO_INVALID_LSN) {
            throw std::runtime_error(
                "Failed to query LSN range for track " + std::to_string(t));
        }

        track_info.index01Lba = static_cast<std::int64_t>(index01_lsn);
        track_info.endLba = static_cast<std::int64_t>(last_lsn) + 1;

        if (pregap_lsn != CDIO_INVALID_LSN && pregap_lsn < index01_lsn) {
            track_info.startLba = static_cast<std::int64_t>(pregap_lsn);
            track_info.pregapStartLba = track_info.startLba;
            track_info.pregapLength = track_info.index01Lba - track_info.startLba;
            track_info.pregapInFile = true;
            track_info.indexes.push_back(IndexInfo{
                .trackNumber = track_info.number,
                .number = 0,
                .lba = track_info.startLba,
                .explicitFromSource = true,
            });
        } else {
            track_info.startLba = track_info.index01Lba;
        }

        track_info.indexes.push_back(IndexInfo{
            .trackNumber = track_info.number,
            .number = 1,
            .lba = track_info.index01Lba,
            .explicitFromSource = true,
        });

        TrackMetadata track_meta{};
        track_meta.trackNumber = track_info.number;
        if (char* isrc = cdio_get_track_isrc(impl_->cdio, t)) {
            track_meta.isrc = isrc;
            cdio_free(isrc);
        }
        if (cdtext) {
            if (const char* title = cdtext_get_const(cdtext, CDTEXT_FIELD_TITLE, t)) {
                track_meta.title = title;
            }
            if (const char* perf = cdtext_get_const(cdtext, CDTEXT_FIELD_PERFORMER, t)) {
                track_meta.performer = perf;
            }
            if (const char* sw = cdtext_get_const(cdtext, CDTEXT_FIELD_SONGWRITER, t)) {
                track_meta.songwriter = sw;
            }
        }

        impl_->layout.tracks.push_back(std::move(track_info));
        impl_->layout.trackMetadata.push_back(std::move(track_meta));
    }

    impl_->layout.discInfo.startLba = impl_->layout.tracks.front().startLba;
    impl_->layout.discInfo.endLba = impl_->layout.tracks.back().endLba;
    impl_->layout.discInfo.rawStartSector =
        static_cast<std::int64_t>(cdio_get_track_lba(impl_->cdio, first_track));
    impl_->layout.discInfo.rawEndSector =
        static_cast<std::int64_t>(cdio_get_track_lba(impl_->cdio, CDIO_CDROM_LEADOUT_TRACK));
}

PhysicalCdSource::~PhysicalCdSource() = default;

std::string PhysicalCdSource::getSourceKind() const {
    return impl_->layout.sourceKind;
}

std::string PhysicalCdSource::getSourcePath() const {
    return impl_->layout.sourcePath;
}

DiscInfo PhysicalCdSource::getDiscInfo() const {
    return impl_->layout.discInfo;
}

std::vector<TrackInfo> PhysicalCdSource::getTracks() const {
    return impl_->layout.tracks;
}

DiscMetadata PhysicalCdSource::getDiscMetadata() const {
    return impl_->layout.discMetadata;
}

std::vector<TrackMetadata> PhysicalCdSource::getTrackMetadata() const {
    return impl_->layout.trackMetadata;
}

std::vector<std::byte> PhysicalCdSource::readAudioSectors(
    std::int64_t startLba,
    std::uint32_t sectorCount) {
    if (sectorCount == 0) {
        return {};
    }

    const auto& dinfo = impl_->layout.discInfo;
    const std::int64_t endLba = startLba + static_cast<std::int64_t>(sectorCount);
    if (startLba < dinfo.startLba || endLba > dinfo.endLba) {
        throw std::out_of_range(
            "Requested LBA range [" + std::to_string(startLba) + ", " +
            std::to_string(endLba) + ") is outside physical CD bounds [" +
            std::to_string(dinfo.startLba) + ", " + std::to_string(dinfo.endLba) + ")");
    }

    for (const auto& trk : impl_->layout.tracks) {
        if (trk.endLba > startLba && trk.startLba < endLba) {
            if (trk.type != TrackType::Audio) {
                throw std::runtime_error(
                    "Cannot read audio sectors from non-audio track " +
                    std::to_string(trk.number));
            }
        }
    }

    std::vector<std::byte> output(
        static_cast<std::size_t>(sectorCount) * AudioFormat::kBytesPerSector);

    const driver_return_code_t rc = cdio_read_audio_sectors(
        impl_->cdio,
        output.data(),
        static_cast<lsn_t>(startLba),
        sectorCount);
    if (rc != DRIVER_OP_SUCCESS) {
        throw std::runtime_error(
            "Failed to read audio sectors from physical CD at LBA " +
            std::to_string(startLba) + " (count=" + std::to_string(sectorCount) + ")");
    }

    return output;
}

std::unique_ptr<DiscSource> open_physical_cd_source(const std::string& device_path) {
    return std::make_unique<PhysicalCdSource>(device_path);
}

} // namespace mycds
