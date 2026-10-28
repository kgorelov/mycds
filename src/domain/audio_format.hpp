#pragma once

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace mycds {

struct AudioFormat {
    static constexpr std::uint32_t kSampleRate = 44100;
    static constexpr std::uint32_t kChannels = 2;
    static constexpr std::uint32_t kBitsPerSample = 16;
    static constexpr std::uint32_t kBytesPerSampleFrame = kChannels * (kBitsPerSample / 8);
    static constexpr std::uint32_t kFramesPerSector = 588;
    static constexpr std::uint32_t kBytesPerSector = kFramesPerSector * kBytesPerSampleFrame; // 2352
    static constexpr std::uint32_t kSectorsPerSecond = 75;
    static constexpr std::uint32_t kSecondsPerMinute = 60;
    static constexpr std::uint32_t kSectorsPerMinute = kSectorsPerSecond * kSecondsPerMinute; // 4500
};

inline std::string sectors_to_msf(std::int64_t sectors) {
    const bool negative = sectors < 0;
    const std::int64_t abs_sectors = negative ? -sectors : sectors;
    const std::int64_t minutes = abs_sectors / AudioFormat::kSectorsPerMinute;
    const std::int64_t seconds = (abs_sectors / AudioFormat::kSectorsPerSecond) % AudioFormat::kSecondsPerMinute;
    const std::int64_t frames = abs_sectors % AudioFormat::kSectorsPerSecond;

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

inline std::optional<std::int64_t> parse_msf_to_sectors(std::string_view msf) {
    if (msf.empty()) {
        return std::nullopt;
    }

    const std::size_t first_colon = msf.find(':');
    if (first_colon == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t second_colon = msf.find(':', first_colon + 1);
    if (second_colon == std::string_view::npos) {
        return std::nullopt;
    }
    if (msf.find(':', second_colon + 1) != std::string_view::npos) {
        return std::nullopt;
    }

    const std::string_view mm_str = msf.substr(0, first_colon);
    const std::string_view ss_str = msf.substr(first_colon + 1, second_colon - first_colon - 1);
    const std::string_view ff_str = msf.substr(second_colon + 1);

    if (mm_str.empty() || ss_str.empty() || ff_str.empty()) {
        return std::nullopt;
    }

    auto parse_non_negative_int = [](std::string_view s) -> std::optional<std::int64_t> {
        std::int64_t val = 0;
        for (char c : s) {
            if (c < '0' || c > '9') {
                return std::nullopt;
            }
            val = val * 10 + (c - '0');
            if (val > 1000000) {
                return std::nullopt;
            }
        }
        return val;
    };

    const auto mm = parse_non_negative_int(mm_str);
    const auto ss = parse_non_negative_int(ss_str);
    const auto ff = parse_non_negative_int(ff_str);
    if (!mm || !ss || !ff) {
        return std::nullopt;
    }

    if (*ss >= static_cast<std::int64_t>(AudioFormat::kSecondsPerMinute) ||
        *ff >= static_cast<std::int64_t>(AudioFormat::kSectorsPerSecond)) {
        return std::nullopt;
    }

    return (*mm * static_cast<std::int64_t>(AudioFormat::kSectorsPerMinute)) +
           (*ss * static_cast<std::int64_t>(AudioFormat::kSectorsPerSecond)) +
           *ff;
}

} // namespace mycds
