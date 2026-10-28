#include "source/toc_image_source.hpp"

#include "domain/audio_format.hpp"
#include "source/cue_image_source.hpp"
#include "source/image_source.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>
#include <string_view>

#include <mirage/mirage.h>

namespace mycds {

namespace {

std::string get_language_pack_string(MirageLanguage* lang, MirageLanguagePackType pack_type) {
    if (!lang) {
        return {};
    }
    const guint8* pack_data = nullptr;
    gint pack_len = 0;
    if (mirage_language_get_pack_data(lang, pack_type, &pack_data, &pack_len, nullptr) &&
        pack_data != nullptr && pack_len > 0) {
        const char* str_ptr = reinterpret_cast<const char*>(pack_data);
        const std::size_t actual_len = strnlen(str_ptr, static_cast<std::size_t>(pack_len));
        return std::string(str_ptr, actual_len);
    }
    return {};
}

} // namespace

struct TocImageSource::Impl {
    MirageContext* context{nullptr};
    MirageDisc* disc{nullptr};
    DiscLayout layout{};

    ~Impl() {
        if (disc) {
            g_object_unref(disc);
            disc = nullptr;
        }
        if (context) {
            g_object_unref(context);
            context = nullptr;
        }
    }
};

TocImageSource::TocImageSource(const std::filesystem::path& toc_path)
    : impl_(std::make_unique<Impl>()) {
    GError* error = nullptr;
    if (!mirage_initialize(&error)) {
        std::string msg = "Failed to initialize libMirage: ";
        msg += (error ? error->message : "unknown error");
        if (error) {
            g_error_free(error);
        }
        throw DiscSourceError(DiscSourceErrorKind::OpenFailed, msg);
    }

    impl_->context = MIRAGE_CONTEXT(g_object_new(MIRAGE_TYPE_CONTEXT, nullptr));
    const std::string path_str = toc_path.string();
    gchar* filenames[] = {const_cast<gchar*>(path_str.c_str()), nullptr};
    impl_->disc = mirage_context_load_image(impl_->context, filenames, &error);
    if (!impl_->disc) {
        std::string msg = "Failed to load TOC image '" + path_str + "': ";
        msg += (error ? error->message : "unknown error");
        if (error) {
            g_error_free(error);
        }
        throw DiscSourceError(DiscSourceErrorKind::CorruptImage, msg);
    }

    const gint num_sessions = mirage_disc_get_number_of_sessions(impl_->disc);
    const gint num_tracks = mirage_disc_get_number_of_tracks(impl_->disc);
    if (num_tracks <= 0) {
        throw DiscSourceError(
            DiscSourceErrorKind::UnsupportedLayout,
            "TOC image contains no tracks: " + path_str);
    }

    impl_->layout.sourceKind = "TOC Image (libMirage)";
    impl_->layout.sourcePath = path_str;
    impl_->layout.discInfo.sessions = static_cast<int>(num_sessions);
    impl_->layout.discInfo.rawStartSector =
        static_cast<std::int64_t>(mirage_disc_layout_get_start_sector(impl_->disc));
    impl_->layout.discInfo.rawEndSector =
        impl_->layout.discInfo.rawStartSector +
        static_cast<std::int64_t>(mirage_disc_layout_get_length(impl_->disc));

    if (MirageSession* session = mirage_disc_get_session_by_index(impl_->disc, 0, nullptr)) {
        if (const gchar* mcn = mirage_session_get_mcn(session)) {
            impl_->layout.discMetadata.catalog = mcn;
        }
        if (mirage_session_get_number_of_languages(session) > 0) {
            if (MirageLanguage* lang = mirage_session_get_language_by_index(session, 0, nullptr)) {
                impl_->layout.discMetadata.title =
                    get_language_pack_string(lang, MIRAGE_LANGUAGE_PACK_TITLE);
                impl_->layout.discMetadata.performer =
                    get_language_pack_string(lang, MIRAGE_LANGUAGE_PACK_PERFORMER);
                impl_->layout.discMetadata.songwriter =
                    get_language_pack_string(lang, MIRAGE_LANGUAGE_PACK_SONGWRITER);
                impl_->layout.discMetadata.genre =
                    get_language_pack_string(lang, MIRAGE_LANGUAGE_PACK_GENRE);
                g_object_unref(lang);
            }
        }
        g_object_unref(session);
    }

    for (gint i = 0; i < num_tracks; ++i) {
        MirageTrack* track = mirage_disc_get_track_by_index(impl_->disc, i, &error);
        if (!track) {
            std::string msg = "Failed to get track at index " + std::to_string(i);
            if (error) {
                g_error_free(error);
            }
            throw DiscSourceError(DiscSourceErrorKind::CorruptImage, msg);
        }

        TrackInfo track_info{};
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

        std::int64_t redbook_leadin_sectors = 0;
        if (raw_track_start == -AudioFormat::kRedBookLeadInSectors &&
            mirage_track_get_number_of_fragments(track) > 0) {
            if (MirageFragment* frag0 = mirage_track_get_fragment_by_index(track, 0, nullptr)) {
                if (mirage_fragment_main_data_get_size(frag0) == 0 &&
                    mirage_fragment_get_length(frag0) == AudioFormat::kRedBookLeadInSectors) {
                    redbook_leadin_sectors = AudioFormat::kRedBookLeadInSectors;
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
            track_info.pregapInFile = true;
            track_info.indexes.push_back(IndexInfo{
                .trackNumber = track_info.number,
                .number = 0,
                .lba = track_info.startLba,
                .explicitFromSource = true,
            });
        }

        track_info.indexes.push_back(IndexInfo{
            .trackNumber = track_info.number,
            .number = 1,
            .lba = track_info.index01Lba,
            .explicitFromSource = true,
        });

        const gint num_extra_indices = mirage_track_get_number_of_indices(track);
        for (gint idx = 0; idx < num_extra_indices; ++idx) {
            if (MirageIndex* mi = mirage_track_get_index_by_number(track, idx, nullptr)) {
                const int idx_num = static_cast<int>(mirage_index_get_number(mi));
                const std::int64_t idx_rel = static_cast<std::int64_t>(mirage_index_get_address(mi));
                track_info.indexes.push_back(IndexInfo{
                    .trackNumber = track_info.number,
                    .number = idx_num,
                    .lba = raw_track_start + idx_rel,
                    .explicitFromSource = true,
                });
                g_object_unref(mi);
            }
        }

        TrackMetadata track_meta{};
        track_meta.trackNumber = track_info.number;
        if (const gchar* isrc = mirage_track_get_isrc(track)) {
            track_meta.isrc = isrc;
        }
        if (mirage_track_get_number_of_languages(track) > 0) {
            if (MirageLanguage* lang = mirage_track_get_language_by_index(track, 0, nullptr)) {
                track_meta.title =
                    get_language_pack_string(lang, MIRAGE_LANGUAGE_PACK_TITLE);
                track_meta.performer =
                    get_language_pack_string(lang, MIRAGE_LANGUAGE_PACK_PERFORMER);
                track_meta.songwriter =
                    get_language_pack_string(lang, MIRAGE_LANGUAGE_PACK_SONGWRITER);
                g_object_unref(lang);
            }
        }

        impl_->layout.tracks.push_back(std::move(track_info));
        impl_->layout.trackMetadata.push_back(std::move(track_meta));
        g_object_unref(track);
    }

    impl_->layout.discInfo.firstTrack = impl_->layout.tracks.front().number;
    impl_->layout.discInfo.lastTrack = impl_->layout.tracks.back().number;
    impl_->layout.discInfo.startLba = impl_->layout.tracks.front().startLba;
    impl_->layout.discInfo.endLba = impl_->layout.tracks.back().endLba;
}

TocImageSource::~TocImageSource() = default;

std::string TocImageSource::getSourceKind() const {
    return impl_->layout.sourceKind;
}

std::string TocImageSource::getSourcePath() const {
    return impl_->layout.sourcePath;
}

DiscInfo TocImageSource::getDiscInfo() const {
    return impl_->layout.discInfo;
}

std::vector<TrackInfo> TocImageSource::getTracks() const {
    return impl_->layout.tracks;
}

DiscMetadata TocImageSource::getDiscMetadata() const {
    return impl_->layout.discMetadata;
}

std::vector<TrackMetadata> TocImageSource::getTrackMetadata() const {
    return impl_->layout.trackMetadata;
}

std::size_t TocImageSource::readAudioSectors(
    std::int64_t startLba,
    std::uint32_t sectorCount,
    std::span<std::byte> destination) {
    const std::size_t required_bytes =
        static_cast<std::size_t>(sectorCount) * AudioFormat::kBytesPerSector;
    if (destination.size() < required_bytes) {
        throw std::invalid_argument(
            "Destination buffer too small for requested sectorCount");
    }
    if (sectorCount == 0) {
        return 0;
    }

    const auto& dinfo = impl_->layout.discInfo;
    const std::int64_t endLba = startLba + static_cast<std::int64_t>(sectorCount);
    if (startLba < dinfo.startLba || endLba > dinfo.endLba) {
        throw DiscOutOfRangeError(
            "Requested LBA range [" + std::to_string(startLba) + ", " +
            std::to_string(endLba) + ") is outside TOC disc bounds [" +
            std::to_string(dinfo.startLba) + ", " + std::to_string(dinfo.endLba) + ")");
    }

    for (const auto& trk : impl_->layout.tracks) {
        if (trk.endLba > startLba && trk.startLba < endLba) {
            if (trk.type != TrackType::Audio) {
                throw DiscSourceError(
                    DiscSourceErrorKind::NonAudioTrack,
                    "Cannot read audio sectors from non-audio track " +
                        std::to_string(trk.number));
            }
        }
    }

    for (std::uint32_t i = 0; i < sectorCount; ++i) {
        const std::int64_t lba = startLba + static_cast<std::int64_t>(i);
        GError* error = nullptr;
        MirageSector* sector =
            mirage_disc_get_sector(impl_->disc, static_cast<gint>(lba), &error);
        if (!sector) {
            std::string msg = "Failed to read TOC sector at LBA " + std::to_string(lba) + ": ";
            msg += (error ? error->message : "unknown error");
            if (error) {
                g_error_free(error);
            }
            throw DiscSourceError(DiscSourceErrorKind::ReadFailed, msg);
        }

        const guint8* pcm_buf = nullptr;
        gint pcm_len = 0;
        if (!mirage_sector_get_data(sector, &pcm_buf, &pcm_len, &error) ||
            pcm_buf == nullptr ||
            static_cast<std::uint32_t>(pcm_len) != AudioFormat::kBytesPerSector) {
            std::string msg = "Invalid audio sector payload at LBA " + std::to_string(lba);
            if (error) {
                msg += std::string(": ") + error->message;
                g_error_free(error);
            }
            g_object_unref(sector);
            throw DiscSourceError(DiscSourceErrorKind::ReadFailed, msg);
        }

        std::memcpy(
            destination.data() + static_cast<std::size_t>(i) * AudioFormat::kBytesPerSector,
            pcm_buf,
            AudioFormat::kBytesPerSector);
        g_object_unref(sector);
    }

    return required_bytes;
}

std::vector<std::byte> TocImageSource::readAudioSectors(
    std::int64_t startLba,
    std::uint32_t sectorCount) {
    if (sectorCount == 0) {
        return {};
    }
    std::vector<std::byte> output(
        static_cast<std::size_t>(sectorCount) * AudioFormat::kBytesPerSector);
    readAudioSectors(startLba, sectorCount, output);
    return output;
}

} // namespace mycds
