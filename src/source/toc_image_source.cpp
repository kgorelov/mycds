#include "source/toc_image_source.hpp"

#include "domain/audio_format.hpp"
#include "source/cue_image_source.hpp"
#include "source/image_source.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string_view>

#include <mirage/mirage.h>

namespace mycds {

namespace {

struct GObjectDeleter {
    void operator()(gpointer obj) const noexcept {
        if (obj) {
            g_object_unref(obj);
        }
    }
};

template <typename T>
using GObjectPtr = std::unique_ptr<T, GObjectDeleter>;

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

    std::array<guint8, AudioFormat::kBytesPerSector> probe_buffer{};

    for (gint i = 0; i < num_tracks; ++i) {
        GObjectPtr<MirageTrack> track_guard(mirage_disc_get_track_by_index(impl_->disc, i, &error));
        MirageTrack* track = track_guard.get();
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
            if (GObjectPtr<MirageFragment> frag0{
                    mirage_track_get_fragment_by_index(track, 0, nullptr)}) {
                if (mirage_fragment_main_data_get_size(frag0.get()) == 0 &&
                    mirage_fragment_get_length(frag0.get()) == AudioFormat::kRedBookLeadInSectors) {
                    redbook_leadin_sectors = AudioFormat::kRedBookLeadInSectors;
                }
            }
        }

        track_info.startLba = raw_track_start + redbook_leadin_sectors;
        track_info.endLba = raw_track_start + raw_track_len;
        track_info.index01Lba = raw_track_start + track_start_rel;

        if (track_info.startLba >= track_info.endLba) {
            throw DiscSourceError(
                DiscSourceErrorKind::CorruptImage,
                "Invalid TOC track " + std::to_string(track_info.number) +
                    " bounds [" + std::to_string(track_info.startLba) + ", " +
                    std::to_string(track_info.endLba) + ") in '" + path_str + "'");
        }
        if (track_info.index01Lba < track_info.startLba ||
            track_info.index01Lba >= track_info.endLba) {
            throw DiscSourceError(
                DiscSourceErrorKind::CorruptImage,
                "Invalid TOC track " + std::to_string(track_info.number) +
                    " INDEX 01 LBA " + std::to_string(track_info.index01Lba) +
                    " outside [" + std::to_string(track_info.startLba) + ", " +
                    std::to_string(track_info.endLba) + ") in '" + path_str + "'");
        }
        if (!impl_->layout.tracks.empty()) {
            const auto& prev = impl_->layout.tracks.back();
            if (track_info.number <= prev.number || track_info.startLba < prev.endLba) {
                throw DiscSourceError(
                    DiscSourceErrorKind::CorruptImage,
                    "Non-monotonic or overlapping TOC track " +
                        std::to_string(track_info.number) + " in '" + path_str + "'");
            }
        }

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
            if (GObjectPtr<MirageIndex> mi{
                    mirage_track_get_index_by_number(track, idx, nullptr)}) {
                const int idx_num = static_cast<int>(mirage_index_get_number(mi.get()));
                const std::int64_t idx_rel =
                    static_cast<std::int64_t>(mirage_index_get_address(mi.get()));
                const std::int64_t idx_lba = raw_track_start + idx_rel;
                if (idx_lba < track_info.startLba || idx_lba >= track_info.endLba ||
                    idx_lba < track_info.indexes.back().lba) {
                    throw DiscSourceError(
                        DiscSourceErrorKind::CorruptImage,
                        "Invalid TOC sub-index " + std::to_string(idx_num) +
                            " on track " + std::to_string(track_info.number) +
                            " at LBA " + std::to_string(idx_lba) + " in '" + path_str + "'");
                }
                track_info.indexes.push_back(IndexInfo{
                    .trackNumber = track_info.number,
                    .number = idx_num,
                    .lba = idx_lba,
                    .explicitFromSource = true,
                });
            }
        }

        if (track_info.type == TrackType::Audio) {
            const gint num_frags = mirage_track_get_number_of_fragments(track);
            if (num_frags <= 0) {
                throw DiscSourceError(
                    DiscSourceErrorKind::CorruptImage,
                    "Audio track " + std::to_string(track_info.number) +
                        " has no data fragments in '" + path_str + "'");
            }
            for (gint f = 0; f < num_frags; ++f) {
                GError* frag_error = nullptr;
                GObjectPtr<MirageFragment> frag(
                    mirage_track_get_fragment_by_index(track, f, &frag_error));
                if (!frag) {
                    std::string msg = "Failed to inspect fragment " + std::to_string(f) +
                                      " of audio track " + std::to_string(track_info.number);
                    if (frag_error) {
                        msg += std::string(": ") + frag_error->message;
                        g_error_free(frag_error);
                    }
                    throw DiscSourceError(DiscSourceErrorKind::CorruptImage, msg);
                }

                const gint frag_len = mirage_fragment_get_length(frag.get());
                if (frag_len < 0) {
                    throw DiscSourceError(
                        DiscSourceErrorKind::CorruptImage,
                        "Negative fragment length in audio track " +
                            std::to_string(track_info.number) + " in '" + path_str + "'");
                }
                if (frag_len == 0) {
                    continue;
                }

                const gint main_size = mirage_fragment_main_data_get_size(frag.get());
                const gchar* frag_file = mirage_fragment_main_data_get_filename(frag.get());
                if (main_size == 0 && frag_file == nullptr) {
                    continue;
                }
                if (main_size != static_cast<gint>(AudioFormat::kBytesPerSector)) {
                    throw DiscSourceError(
                        DiscSourceErrorKind::CorruptImage,
                        "Unsupported audio sector size " + std::to_string(main_size) +
                            " in track " + std::to_string(track_info.number) +
                            " of '" + path_str + "'");
                }

                const gint read_last = mirage_fragment_read_main_data_fast(
                    frag.get(),
                    frag_len - 1,
                    probe_buffer.data(),
                    static_cast<gint>(AudioFormat::kBytesPerSector),
                    &frag_error);
                if (read_last != static_cast<gint>(AudioFormat::kBytesPerSector)) {
                    std::string msg = "Truncated or unreadable audio data file for track " +
                                      std::to_string(track_info.number) + " in '" + path_str + "'";
                    if (frag_file) {
                        msg += std::string(" (file '") + frag_file + "')";
                    }
                    if (frag_error) {
                        msg += std::string(": ") + frag_error->message;
                        g_error_free(frag_error);
                    }
                    throw DiscSourceError(DiscSourceErrorKind::CorruptImage, msg);
                }
            }
        }

        TrackMetadata track_meta{};
        track_meta.trackNumber = track_info.number;
        if (const gchar* isrc = mirage_track_get_isrc(track)) {
            track_meta.isrc = isrc;
        }
        if (mirage_track_get_number_of_languages(track) > 0) {
            if (GObjectPtr<MirageLanguage> lang{
                    mirage_track_get_language_by_index(track, 0, nullptr)}) {
                track_meta.title =
                    get_language_pack_string(lang.get(), MIRAGE_LANGUAGE_PACK_TITLE);
                track_meta.performer =
                    get_language_pack_string(lang.get(), MIRAGE_LANGUAGE_PACK_PERFORMER);
                track_meta.songwriter =
                    get_language_pack_string(lang.get(), MIRAGE_LANGUAGE_PACK_SONGWRITER);
            }
        }

        impl_->layout.tracks.push_back(std::move(track_info));
        impl_->layout.trackMetadata.push_back(std::move(track_meta));
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
    if (startLba < dinfo.startLba ||
        startLba > dinfo.endLba ||
        static_cast<std::int64_t>(sectorCount) > (dinfo.endLba - startLba)) {
        throw DiscOutOfRangeError(
            "Requested LBA start " + std::to_string(startLba) + " (count " +
            std::to_string(sectorCount) + ") is outside TOC disc bounds [" +
            std::to_string(dinfo.startLba) + ", " + std::to_string(dinfo.endLba) + ")");
    }
    const std::int64_t endLba = startLba + static_cast<std::int64_t>(sectorCount);

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
        GObjectPtr<MirageTrack> track(
            mirage_disc_get_track_by_address(impl_->disc, static_cast<gint>(lba), &error));
        if (!track) {
            std::string msg = "Failed to locate TOC track for LBA " + std::to_string(lba) + ": ";
            msg += (error ? error->message : "unknown error");
            if (error) {
                g_error_free(error);
            }
            throw DiscSourceError(DiscSourceErrorKind::ReadFailed, msg);
        }

        const gint rel_addr =
            static_cast<gint>(lba) - mirage_track_layout_get_start_sector(track.get());
        GObjectPtr<MirageFragment> frag(
            mirage_track_get_fragment_by_address(track.get(), rel_addr, &error));
        if (!frag) {
            std::string msg = "Failed to locate TOC fragment for LBA " + std::to_string(lba) + ": ";
            msg += (error ? error->message : "unknown error");
            if (error) {
                g_error_free(error);
            }
            throw DiscSourceError(DiscSourceErrorKind::ReadFailed, msg);
        }

        guint8* out_ptr = reinterpret_cast<guint8*>(
            destination.data() + static_cast<std::size_t>(i) * AudioFormat::kBytesPerSector);

        const gint main_size = mirage_fragment_main_data_get_size(frag.get());
        const gchar* frag_file = mirage_fragment_main_data_get_filename(frag.get());
        if (main_size == 0 && frag_file == nullptr) {
            std::memset(out_ptr, 0, AudioFormat::kBytesPerSector);
            continue;
        }

        if (main_size != static_cast<gint>(AudioFormat::kBytesPerSector)) {
            throw DiscSourceError(
                DiscSourceErrorKind::ReadFailed,
                "Unexpected TOC audio fragment sector size " + std::to_string(main_size) +
                    " at LBA " + std::to_string(lba));
        }

        const gint frag_rel = rel_addr - mirage_fragment_get_address(frag.get());
        const gint read_len = mirage_fragment_read_main_data_fast(
            frag.get(),
            frag_rel,
            out_ptr,
            static_cast<gint>(AudioFormat::kBytesPerSector),
            &error);
        if (read_len != static_cast<gint>(AudioFormat::kBytesPerSector)) {
            std::string msg = "Short or failed audio sector read at LBA " + std::to_string(lba) +
                              " (got " + std::to_string(read_len) + " of " +
                              std::to_string(AudioFormat::kBytesPerSector) + " bytes)";
            if (error) {
                msg += std::string(": ") + error->message;
                g_error_free(error);
            }
            throw DiscSourceError(DiscSourceErrorKind::ReadFailed, msg);
        }
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
