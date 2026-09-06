/**
 * @file Qoi.cppm
 * @brief Host-only, dependency-free QOI decoder used by mdux-imagebake.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 */
module;

export module mdux.tools.qoi;

import std;
import mdux.core.result;

export namespace mdux::tools::qoi {

/// Maximum decoded pixel count: 4096 squared RGBA8 pixels, or 64 MiB.
inline constexpr std::uint64_t maxDecodedPixels = 4'096u * 4'096u;

enum class DecodeError : std::uint8_t {
    Truncated,
    BadMagic,
    ZeroExtent,
    UnsupportedChannels,
    UnsupportedColorSpace,
    SizeOverflow,
    BadEndMarker,
    TruncatedChunk,
    RunExceedsImage,
    TrailingChunkData,
};

[[nodiscard]] std::string_view describe(DecodeError error) noexcept;

struct Image {
    std::uint32_t          width{0};
    std::uint32_t          height{0};
    std::uint8_t           sourceChannels{0};
    std::uint8_t           colorSpace{0};
    std::vector<std::byte> rgba;
};

/** @brief Decodes a complete QOI stream to straight-alpha, row-major RGBA8. */
[[nodiscard]] mdux::core::Result<Image, DecodeError> decode(std::span<const std::byte> bytes) noexcept;

}  // namespace mdux::tools::qoi
