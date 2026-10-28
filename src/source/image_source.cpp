#include "source/image_source.hpp"

#include "source/cue_image_source.hpp"
#include "source/toc_image_source.hpp"

#include <cctype>
#include <string>
#include <string_view>

namespace mycds {

namespace {

std::string to_lower_ascii(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (unsigned char c : sv) {
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

std::unique_ptr<DiscSource> create_image_delegate(const std::filesystem::path& image_path) {
    const std::string ext = to_lower_ascii(image_path.extension().string());
    if (ext == ".cue") {
        return std::make_unique<CueImageSource>(image_path);
    }
    if (ext == ".toc") {
        return std::make_unique<TocImageSource>(image_path);
    }
    throw DiscSourceError(
        DiscSourceErrorKind::UnsupportedLayout,
        "Unsupported image descriptor format '" + image_path.extension().string() +
            "' for '" + image_path.string() + "' (expected .cue or .toc)");
}

} // namespace

ImageSource::ImageSource(const std::filesystem::path& image_path)
    : delegate_(create_image_delegate(image_path)) {}

std::string ImageSource::getSourceKind() const {
    return delegate_->getSourceKind();
}

std::string ImageSource::getSourcePath() const {
    return delegate_->getSourcePath();
}

DiscInfo ImageSource::getDiscInfo() const {
    return delegate_->getDiscInfo();
}

std::vector<TrackInfo> ImageSource::getTracks() const {
    return delegate_->getTracks();
}

DiscMetadata ImageSource::getDiscMetadata() const {
    return delegate_->getDiscMetadata();
}

std::vector<TrackMetadata> ImageSource::getTrackMetadata() const {
    return delegate_->getTrackMetadata();
}

DiscLayout ImageSource::getDiscLayout() const {
    return delegate_->getDiscLayout();
}

std::vector<std::byte> ImageSource::readAudioSectors(
    std::int64_t startLba,
    std::uint32_t sectorCount) {
    return delegate_->readAudioSectors(startLba, sectorCount);
}

std::size_t ImageSource::readAudioSectors(
    std::int64_t startLba,
    std::uint32_t sectorCount,
    std::span<std::byte> destination) {
    return delegate_->readAudioSectors(startLba, sectorCount, destination);
}

std::unique_ptr<DiscSource> open_image_source(const std::string& image_path) {
    return std::make_unique<ImageSource>(std::filesystem::path(image_path));
}

} // namespace mycds
