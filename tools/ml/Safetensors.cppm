/**
 * @file Safetensors.cppm
 * @brief Host-tools-zone safetensors reader: weights in, no runtime-adjacent SOUP.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone: may throw, never linked into a device)
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * The format is small enough to parse by hand, and that is exactly why it was chosen: a `u64`
 * little-endian header length, a JSON header describing each tensor's name, dtype, shape and byte
 * range, then the raw tensor bytes. The JSON is read with `mdux.evidence.json` - the same reader
 * every other MduX artifact goes through - so importing weights adds no dependency at all. See
 * ADR-008, decision 6.
 *
 * **This code is never linked into a device target, and no build step ever feeds it a weights
 * file.** A malformed or hostile `.safetensors` therefore cannot affect a build or reach a device;
 * it can only make a developer's `mdux-mlbake` invocation fail with a diagnostic. That containment
 * is what lets the parser be as small as it is.
 *
 * Its own unit tests *do* run in CI, on fixtures this repository authored - `ml_tools_spec` is part
 * of the ordinary CTest suite. That is testing the parser, not parsing untrusted input during a
 * build, and the distinction is the whole point: nothing in the pipeline hands this code a file it
 * did not already trust.
 *
 * It still validates aggressively, because the failure it is guarding against is not an attack but
 * the ordinary one: a weights file that does not describe what the author thinks it describes.
 *
 * ## "malformed" versus "unsupported"
 *
 * Two different rejections, deliberately distinguishable by diagnostic code:
 *
 * - `mdux.ml.safetensors.malformed*` - the file is not a well-formed safetensors container. The
 *   file is wrong.
 * - `mdux.ml.safetensors.unsupported*` - the container is fine and MduX v1 does not accept it: a
 *   dtype other than `f32`, or a rank above 3. The file is fine and out of scope (ADR-008,
 *   decision 5), and the fix is a follow-up ADR rather than a different export.
 *
 * An author who gets `unsupportedDtype` on an `f16` checkpoint needs to know it is their model
 * that is out of scope, not their file that is broken.
 */
module;

export module mdux.tools.ml.safetensors;

import std;
import mdux.core.result;
import mdux.tools.cli;

export namespace mdux::tools::ml {

/// Every dtype the container format defines, so an unrecognised spelling is distinguishable from a
/// recognised one MduX does not accept. Only F32 is in v1 scope.
enum class Dtype : std::uint8_t { Bool, U8, I8, U16, I16, F16, BF16, U32, I32, F32, U64, I64, F64 };

/// Wire spellings, in enumerator order. These are the format's, not MduX's, so they are upper case.
inline constexpr std::array<std::string_view, 13> dtypeWireValues{
    "BOOL", "U8", "I8", "U16", "I16", "F16", "BF16", "U32", "I32", "F32", "U64", "I64", "F64"};

/// Bytes per element. Bool is one byte in this format.
[[nodiscard]] std::uint64_t dtypeByteWidth(Dtype dtype) noexcept;

[[nodiscard]] std::optional<Dtype> dtypeFromWire(std::string_view wire) noexcept;

/// One tensor as the file declares it. `byteOffset` is absolute within the file, not relative to
/// the data section - resolved during parsing so no caller has to remember the adjustment.
struct TensorEntry {
    std::string name;
    Dtype dtype{Dtype::F32};
    std::vector<std::uint64_t> shape;
    std::uint64_t byteOffset{0};
    std::uint64_t byteLength{0};

    [[nodiscard]] std::uint64_t elementCount() const noexcept;
};

/**
 * @brief A parsed file: its tensors, and whatever `__metadata__` carried.
 *
 * Tensors are sorted by name rather than left in header order. A JSON object has no guaranteed
 * ordering, and the baker's output has to be byte-identical between two machines - so the order is
 * fixed here, once, instead of depending on however the exporter happened to emit it.
 */
struct SafetensorsFile {
    std::vector<TensorEntry> tensors;
    std::vector<std::pair<std::string, std::string>> metadata;

    [[nodiscard]] const TensorEntry* find(std::string_view name) const noexcept;
};

/**
 * @brief Parses a whole safetensors file.
 *
 * @param bytes    the entire file
 * @param fileName used only to populate the diagnostic's `file` field
 *
 * Returns the first problem found as a `Diagnostic` carrying a stable code - see the module
 * comment for what the code prefixes mean. Parsing stops at the first problem because a file that
 * is malformed at byte 8 cannot yield trustworthy findings about byte 8000.
 */
[[nodiscard]] mdux::core::Result<SafetensorsFile, cli::Diagnostic> parseSafetensors(
    std::span<const std::byte> bytes, std::string_view fileName);

/**
 * @brief Reads `path` and parses it.
 *
 * Host-tools zone, so this throws `std::runtime_error` if the file cannot be read - a missing file
 * is a usage mistake, not a finding about a model.
 */
[[nodiscard]] mdux::core::Result<SafetensorsFile, cli::Diagnostic> readSafetensors(
    const std::filesystem::path& path);

}  // namespace mdux::tools::ml
