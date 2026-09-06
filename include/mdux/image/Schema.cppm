/**
 * @file Schema.cppm
 * @brief Governed baked-image package schema shared by the host baker and runtime.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only)
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-007 Evidence pipeline doctrine
 */
module;

export module mdux.image.schema;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;

export namespace mdux::image {

inline constexpr std::string_view packageKind = "image";

/** @brief Whether a sidecar name would collide with an artifact on any supported filesystem. */
[[nodiscard]] constexpr bool isReservedSidecarPath(std::string_view path) noexcept {
    // Win32 ignores trailing spaces and periods when resolving ordinary file names. Compare the
    // portable spelling so `package.json.` cannot overwrite `package.json` on one supported host
    // while remaining a distinct file on another.
    while (!path.empty() && (path.back() == '.' || path.back() == ' ')) {
        path.remove_suffix(1);
    }
    const auto equalsAsciiCaseInsensitive = [path](std::string_view reserved) {
        if (path.size() != reserved.size()) {
            return false;
        }
        for (std::size_t index = 0; index < path.size(); ++index) {
            const auto fold = [](char character) {
                return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
            };
            if (fold(path[index]) != fold(reserved[index])) {
                return false;
            }
        }
        return true;
    };
    return equalsAsciiCaseInsensitive("package.json") || equalsAsciiCaseInsensitive("report.json");
}

enum class SchemaError : std::uint8_t {
    WrongKind,
    UnsupportedSchemaVersion,
    EmptyId,
    ZeroExtent,
    PixelCountOverflow,
    SidecarSizeMismatch,
    EmptySidecarPath,
    SidecarPathHasSeparator,
    SidecarPathHasControlCharacter,
    ReservedSidecarPath,
    MalformedPackage,
    ReportRejected,
};

[[nodiscard]] std::string_view describe(SchemaError error) noexcept;

/**
 * @brief One straight-alpha RGBA8 image, with pixels in a binary sidecar.
 *
 * Pixels are row-major with a top-left origin. Width and height are intrinsic: the MedUI compiler
 * refuses an Image node whose resolved rectangle differs, so the device performs no scaling.
 */
struct ImagePackage {
    evidence::PackageHeader header{.schemaVersion = evidence::kSchemaVersion, .id = {}, .kind = std::string{packageKind}};
    std::uint32_t           width{0};
    std::uint32_t           height{0};
    std::string             sidecarPath{"pixels.rgba"};
    std::uint64_t           sidecarByteLength{0};
    evidence::Digest        sidecarSha256{};

    [[nodiscard]] mdux::core::ResultVoid<SchemaError>                    validate() const noexcept;
    [[nodiscard]] mdux::core::Result<evidence::json::Value, SchemaError> toJson() const noexcept;
    [[nodiscard]] mdux::core::Result<std::string, SchemaError>           write() const noexcept;

    /** @brief Hashes the exact canonical bytes `write()` produces, without allocating. */
    [[nodiscard]] mdux::core::Result<evidence::Digest, SchemaError> canonicalSha256() const noexcept;

    /** @brief Parses canonical package JSON and validates every structural invariant. */
    [[nodiscard]] static mdux::core::Result<ImagePackage, SchemaError> parse(std::string_view text) noexcept;
};

}  // namespace mdux::image
