/**
 * @brief Governed-zone text package types: the canonical shape of every baked `package.json`
 *        produced by the text pipeline, and the runtime view generated code exposes.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-010 No on-device text shaping (the runtime consumes runs; it does not produce them)
 *
 * Part of MduXCore. This module is canonical: `mdux-textbake` (issue #157) and the `.medui`
 * compiler (#15) import it rather than restating its records. The two arrangements that would
 * disagree eventually - a baker that produced one shape and a runtime that read another, or a
 * compiler that emitted one and a device that held a second - are the Wave 2 lesson applied, and
 * ADR-008 decision 1, mirrored to text by ADR-010.
 *
 * A text package is one locale's worth of positioned glyph runs against one font package's
 * atlas. The runtime records `DrawMode::CoverageR8` rectangles drawn from the atlas (#13, S5 of
 * #14/#161) at positions the package records; it performs no shaping, no layout, and no parsing
 * of font tables. See ADR-010 for the architectural commitment.
 *
 * A text package looks like this:
 *
 * ```json
 * {
 *   "schemaVersion": 1,
 *   "id": "label-welcome",
 *   "kind": "text",
 *   "atlas":  "roboto-ui",
 *   "locale": "en-US",
 *   "sidecar":   { "path": "runs.bin", "byteLength": 336, "sha256": "…" },
 *   "runs": [
 *     { "id": "title", "byteOffset": 0, "byteLength": 48, "sha256": "…" },
 *     { "id": "subtitle", "byteOffset": 48, "byteLength": 288, "sha256": "…" }
 *   ]
 * }
 * ```
 *
 * Each run's bytes are a sequence of `RunRecord`-sized records; `byteLength` is a multiple of
 * `recordSize` or the package is rejected. The records themselves are interpreted in the adapter
 * zone (#162, S6) - this module never reads their contents, only their bounds.
 *
 * Note there are no Vulkan types here, deliberately. `DrawMode::CoverageR8` is `mdux.draw`'s own
 * enumeration; the text package is atlas-id + bounds. A governed module that named a Vk type
 * could not be linked into `MduXCore` at all - `mdux_verify_trust_zones()` fails the configure
 * step - and the package format would become unreadable by any tool that does not have the
 * Vulkan headers, which includes `mdux-textbake`.
 *
 * ## Why the atlas is a string id, not an embedded font package
 *
 * A font package (`mdux.font.schema`, #161) holds the R8 atlas, glyph metrics and restricted
 * charset. A text package *references* one by id and records the positioned runs against it; it
 * does not embed the atlas. Two reasons:
 *
 * 1. **Atlas reuse.** Many screens in one locale share one font. Embedding the atlas per text
 *    package would duplicate megabytes of R8 coverage per screen, where one atlas serves all.
 * 2. **Evidence boundary.** The font baker commits the atlas once and byte-verifies it once;
 *    text packages commit only what is local to a screen. A change to the atlas re-bakes the
 *    font package and verifies under it; the text packages reference the new id and carry their
 *    own digest over runs, not over atlas bytes.
 *
 * ## Why byteLength is validated so aggressively
 *
 * `validate()` rejects overlapping run ranges, ranges past the end of the sidecar, and byte
 * lengths that are not a multiple of `recordSize`. A run whose `byteLength` is not a multiple of
 * `recordSize` cannot be enumerated into records on the device without a partial-record read,
 * which is the one case byte-identity cannot detect - the partial bytes mean something different
 * between two toolchains' struct layouts. Cheap to check here, produces an actionable diagnostic,
 * where the same mistake reaching the device costs a half-rendered glyph and no error.
 */
module;

export module mdux.text.schema;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;

export namespace mdux::text {

/// The `<kind>` component of `generated/<kind>/<id>/`, and the value of a package's `kind` member.
///
/// Named packageKind rather than kind: MSVC raises C4459 ("declaration hides global declaration")
/// wherever a parameter is called `kind`, which for a type bound to a font atlas with a `kind`
/// member is most of them, and warnings are errors here - see `mdux.shader.schema` for the same call.
inline constexpr std::string_view packageKind = "text";

/// One positioned-glyph-run record on the device side, as the adapter zone interprets it.
///
/// `recordSize` is the published stride every run's `byteLength` must be a multiple of. A change
/// to this layout is a schema-version change, not a silent edit - every committed text package's
/// digest depends on it.
///
/// The fields are: `glyphIndex` (an index into the referenced atlas's glyph table), `x` and `y`
/// (signed pixel positions in the run's coordinate frame). The schema does not interpret them;
/// it only confirms that the bytes are enumerable as whole records. Interpretation is #162's job.
inline constexpr std::size_t recordSize = 6;

/// Wire spelling for the run-record size, recorded in `package.json` so an auditor can read the
/// stride without consulting this module. The number is small and stable; spelling it explicitly
/// also keeps two implementations honest if a future wave grows the record.
inline constexpr std::string_view recordSizeWire = "6";

enum class SchemaError : std::uint8_t {
    WrongKind,                  ///< `kind` is not "text"
    EmptyAtlasId,               ///< `atlas` is empty; a text package must reference a font package
    EmptyLocale,                ///< `locale` is empty; an unlocalized text package is not meaningful
    EmptySidecarPath,
    SidecarPathHasSeparator,    ///< a sidecar sits beside package.json; it is a bare filename
    EmptyRunId,
    DuplicateRunId,
    UnalignedRun,               ///< byte length is not a multiple of recordSize
    RunOutOfBounds,             ///< the range extends past the end of the sidecar
    OverlappingRuns,
    UnsupportedSchemaVersion,
    MalformedPackage,           ///< parsed JSON did not have the expected shape
    ReportRejected,             ///< the embedded PackageHeader failed its own validate()
};

[[nodiscard]] std::string_view describe(SchemaError error) noexcept;

/// One positioned glyph run, addressed as a range of the package's binary sidecar.
struct TextRun {
    std::string id;          ///< stable within a package, e.g. "title"
    std::uint64_t byteOffset{0};
    std::uint64_t byteLength{0};
    evidence::Digest sha256{};

    /// One past the last byte of this run's range. Never overflows for a validated package,
    /// whose ranges are bounded by the sidecar length.
    [[nodiscard]] std::uint64_t byteEnd() const noexcept { return byteOffset + byteLength; }
};

/**
 * @brief The `package.json` a text bake produces, and the runtime consumes (via a PackageView
 *        the emitter #15-S8 generates from this owning form).
 */
struct TextPackage {
    /// `kind` is defaulted so a hand-built package cannot forget it; every member is named
    /// because a partial designated initializer is a -Wmissing-field-initializers error here.
    evidence::PackageHeader header{.schemaVersion = evidence::kSchemaVersion,
                                    .id = {},
                                    .kind = std::string{packageKind}};
    std::string atlasId;     ///< the referenced font-package id (mdux.font.schema, #161)
    std::string locale;      ///< BCP 47 tag, e.g. "en-US"; the runtime reads no others
    std::string sidecarPath;  ///< bare filename beside package.json, e.g. "runs.bin"
    std::uint64_t sidecarByteLength{0};
    evidence::Digest sidecarSha256{};
    std::vector<TextRun> runs;

    /// Checks every invariant a consumer is entitled to assume. See the module comment for why
    /// the range checks in particular are worth their cost.
    [[nodiscard]] mdux::core::ResultVoid<SchemaError> validate() const noexcept;

    [[nodiscard]] mdux::core::Result<evidence::json::Value, SchemaError> toJson() const noexcept;

    /// Serializes to canonical JSON text, trailing newline included. Validates first.
    [[nodiscard]] mdux::core::Result<std::string, SchemaError> write() const noexcept;

    /// Parses canonical `package.json` text. Strict: rejects a malformed shape, an unsupported
    /// `schemaVersion` and validates the result.
    [[nodiscard]] static mdux::core::Result<TextPackage, SchemaError> parse(
        std::string_view text) noexcept;

    /// The run with this id, or nullptr. Linear: a package holds a handful of runs, and a map
    /// would cost more to build than every lookup it could ever serve.
    [[nodiscard]] const TextRun* find(std::string_view id) const noexcept;
};

// ---------------------------------------------------------------------------
// The view layer: what generated code exposes and the runtime consumes
// ---------------------------------------------------------------------------
//
// `TextPackage` above owns its strings and vectors, which is right for a baker assembling an
// artifact and for a reader validating one. It is wrong for the device side: the runtime wants a
// contract it can hold as `constexpr` data with no allocation and no parsing, addressed straight
// into a byte array the linker placed.
//
// So there are two representations of one format, and they live in the same module deliberately.
// A view type declared next to the owning type it mirrors is one edit away from staying in step;
// a view type declared in the emitter would be a second definition of the format, which is the
// arrangement ADR-008 decision 1 calls out and ADR-010 inherits.

/// One run as generated code exposes it: non-owning, `constexpr`-constructible.
struct RunView {
    std::string_view id;
    std::size_t byteOffset{0};
    std::size_t byteLength{0};

    [[nodiscard]] bool operator==(const RunView&) const noexcept = default;
};

/**
 * @brief A whole package as generated code exposes it.
 *
 * Everything here is a span or a `string_view` over storage the generated translation unit owns,
 * so a `PackageView` costs nothing to copy and needs no lifetime management beyond that of the
 * generated code itself - which is static.
 */
struct PackageView {
    std::string_view id;
    std::string_view atlasId;
    std::string_view locale;
    std::span<const std::byte> runsBytes;  ///< the whole sidecar; runs address into it
    std::span<const RunView> runs;

    [[nodiscard]] const RunView* find(std::string_view runId) const noexcept;

    /// The bytes of one run, or an empty span when `runId` is not in this package.
    ///
    /// Empty rather than a `Result`: the caller is generated-code-adjacent and knows the ids at
    /// compile time, so a miss is a programming error rather than a runtime condition - and an
    /// empty span is what the recorder would feed `DrawList` for "no glyphs", which a caller is
    /// entitled to detect without treating it as a failure.
    [[nodiscard]] std::span<const std::byte> runBytes(std::string_view runId) const noexcept;
};

}  // namespace mdux::text