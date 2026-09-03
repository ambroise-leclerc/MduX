/**
 * @file TextBake.cppm
 * @brief The text baker's recipe model and bake/verify core, separated from `main()`.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-005 Error handling and exceptions policy (host tools may throw)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-010 No on-device text shaping (the baker is one of the host-side enforcement points)
 *
 * ## Two recipe kinds, one tool
 *
 * A recipe carrying a `[charset]` table is a **font** recipe: it bakes an R8 coverage atlas out
 * of a TrueType file, producing `package.json`, `report.json` and `atlas.bin` under
 * `generated/font/<id>/`. One without it is a **text** recipe: positioned glyph runs against an
 * atlas some font package already baked.
 *
 * They share `parseRecipe()`, `run()`, `write()` and `verify()` because they share everything
 * around the edges - the report, the digests, the write/verify pair - and differ only in what
 * fills the sidecar. `run()` dispatches on `Recipe::font`, so `mdux-textbake` stays one tool with
 * one CLI rather than growing a second entry point that would have to be kept in step.
 *
 * S1 (#157) landed the schema, the library and the host-tools wiring with the text path only,
 * producing a no-run package whose sidecar was zero bytes. S4 (#160) added the font path and the
 * first committed artifact, built on the TrueType parser (S2 / #158) and the rasteriser
 * (S3 / #159).
 *
 * ## One code path for bake and verify
 *
 * `run()` produces the whole artifact set in memory - `package.json`, `report.json` and the binary
 * sidecar - and only then does the caller either write those bytes or compare them. ADR-007 asks
 * for this explicitly, and the reason is worth restating: if verify were a second implementation,
 * a CI check comparing a baker against a different baker would prove nothing about either.
 *
 * ## A text recipe
 *
 * ```toml
 * [package]
 * id      = "label-welcome"
 * atlas   = "roboto-ui"     # the font package id these runs are positioned against
 * locale  = "en-US"
 * font    = "generated/font/roboto-ui/package.json"
 * sidecar = "runs.bin"
 *
 * [strings]
 * keys   = ["STR-TITLE", "STR-SUBTITLE"]
 * values = ["Welcome",   "Select a patient"]
 * ```
 *
 * Two parallel arrays rather than the `[[strings]]` array-of-tables TOML would normally use:
 * `mdux.tools.toml` implements a deliberate subset with no arrays of tables - see
 * `tools/common/Toml.cppm:13`, where it is listed as unsupported on purpose.
 *
 * A key becomes a run id, which is what `t("STR-TITLE")` resolves against in a `.medui` source.
 * The value is the translation for this package's one locale; a second locale is a second recipe
 * and a second package, because a text package is one locale's worth of runs (`mdux.text.schema`).
 *
 * S1 (#157) landed the recipe and the surrounding machinery with the run list empty, because
 * nothing could position a run until a font package existed to position it against. #235 fills
 * it in: `font` names that package, and the baker walks each value's code points through its
 * glyph table.
 *
 * ## Positioning, and why it is arithmetic rather than shaping
 *
 * ADR-010 puts shaping on the host, and this is the host side of it - but "shaping" is more than
 * this baker does and more than the format can carry. A v1 record is a glyph and a position, so
 * what happens here is a pen walk: look each code point up in the font package, emit a record at
 * the pen, advance the pen by the glyph's advance width plus whatever kerning the package baked
 * for the pair. No reordering, no substitution, no ligatures, no mark attachment, no bidirectional
 * resolution.
 *
 * The limit is enforced, not merely documented, and it is enforced against a **declared
 * repertoire** rather than against the font's charset. The distinction is the whole point: a font
 * package is free to bake Arabic, or a combining acute, and for both of those the glyph lookup
 * succeeds while the pen walk is wrong - isolated unjoined letters running the wrong way, an
 * accent parked after the base rather than over it. Either would produce a package that validates,
 * byte-compares across both toolchains, and renders incorrectly on a device, which is precisely
 * the outcome ADR-010 exists to prevent. So a code point outside the repertoire is refused
 * *before* the font is consulted.
 *
 * The repertoire is ADR-010's v1 scope and nothing else - Latin, Greek and Cyrillic, left to
 * right, with those blocks' combining marks carved out. It is not this file's to widen or narrow:
 * a baker enforcing less than the ADR ships a rendering nobody reviewed, and one enforcing more
 * refuses text the accepted architecture promises. Both are the same defect in opposite
 * directions, so a change to the repertoire is a change to ADR-010 first.
 *
 * The pen accumulates in **font units** and converts to pixels per glyph, rather than converting
 * each advance and accumulating pixels. Both are defensible; only the first keeps a rounding error
 * from compounding along a line, so a long string does not drift by a pixel per word. The
 * conversion is integer throughout - `(pen * pixelSize + unitsPerEm / 2) / unitsPerEm`, in
 * `std::int64_t` - because the sidecar is committed bytes compared across toolchains, and a float
 * would make the artifact depend on the host's rounding mode.
 *
 * A record is emitted for every code point, including the blanks. Neither consumer draws them -
 * `mdux::text::draw::recordRun()` skips a glyph with no coverage, and #195's budget measurement
 * skips exactly the same ones - so the six bytes buy nothing at run time. They buy something at
 * review time: a run's record count is its character count, so a sidecar that lost a character is
 * visible in `package.json` as a byte length that no longer matches the string.
 */
module;

export module mdux.tools.textbake;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.text.schema;
import mdux.tools.cli;
import mdux.tools.toml;

export namespace mdux::tools::textbake {

/// The tool name that appears in every diagnostic and in `report.json`.
inline constexpr std::string_view toolName = "mdux-textbake";

/// One code-point range from a font recipe's `[charset]` parallel arrays.
struct CharsetRange {
    std::string name;
    char32_t    first{0};
    char32_t    last{0};

    [[nodiscard]] std::uint32_t count() const noexcept {
        return static_cast<std::uint32_t>(last - first) + 1u;
    }
};

/**
 * @brief The font-baking half of a recipe, present when the recipe carries a `[charset]` table.
 *
 * A recipe is either a *text* package (S1: positioned runs against an existing atlas) or a *font*
 * package (S4: an atlas baked from a TrueType file). They share `parseRecipe()` and `run()`
 * because they share everything around the edges - the report, the digests, the write/verify
 * pair - and differ only in what fills the sidecar. Dispatching on the presence of `[charset]`
 * keeps `mdux-textbake` one tool with one CLI, which is what issue #160 asks for.
 */
struct FontSpec {
    std::string               source;        ///< repository-relative path to the .ttf
    std::uint32_t             pixelSize{0};  ///< em size in pixels
    std::vector<std::string>  locales;       ///< locales this package is approved for
    std::vector<CharsetRange> charset;       ///< the code points to bake, in recipe order

    /// Total code points across every range, which is the glyph count before any are found
    /// missing from the font.
    [[nodiscard]] std::uint32_t codePointCount() const noexcept;
};

/// One entry of a text recipe's `[strings]` table: the key a `.medui` source names with
/// `t("...")`, and the translation for this package's locale.
///
/// The key becomes the run id in `package.json`, so it inherits the schema's rules for one:
/// non-empty, and unique within the package.
struct TextString {
    std::string key;
    std::string text;
};

/// A parsed and resolved recipe. Every default is expanded here rather than at the point of use,
/// so `report.json`'s `options` records what the bake actually did - ADR-007's rule that a
/// silently changed default must not leave every report looking unchanged.
struct Recipe {
    std::string id;
    std::string atlas;
    std::string locale;
    std::string sidecar{"runs.bin"};

    /// Repository-relative path to the committed font package these runs are positioned against.
    /// Empty for a font recipe, and for a text recipe carrying no strings.
    std::string fontPackage;

    /// The strings to position, in recipe order - which is the order their runs appear in the
    /// sidecar, so the file reads in the order the recipe was written.
    std::vector<TextString> strings;

    /// Set when this recipe carries a `[charset]` table, which is what makes it a font recipe.
    /// `run()` dispatches on it.
    std::optional<FontSpec> font{};

    /// The fully resolved options, as they are recorded in the report.
    [[nodiscard]] evidence::json::Value toOptions() const;
};

/**
 * @brief Everything a bake produces, held in memory so bake and verify can share one code path.
 *
 * Deliberately just bytes plus two summary fields, rather than also carrying the `text::TextPackage`
 * the JSON was rendered from. A caller that wants the structured package calls
 * `text::TextPackage::parse(packageJson)`, which is strictly better for a test: it exercises the
 * reader on the writer's own output instead of inspecting a value that never made the round trip.
 *
 * See `tools/shader/ShaderBake.cppm` for the GCC 15 / `std::optional<T>` note that applies to any
 * baker carrying a non-trivially-destructible type across a module boundary; this struct avoids
 * the issue by holding bytes and POD-like summaries only.
 */
struct BakeOutputs {
    std::string packageJson;         ///< canonical `package.json` text
    std::string reportJson;          ///< canonical `report.json` text
    std::vector<std::byte> sidecar;  ///< at S1: empty; S4 fills it from the rasteriser
    std::string sidecarName;         ///< the sidecar's bare filename
    std::string packageId;           ///< for the summary line; the package itself is in the JSON
    std::size_t runCount{0};

    /// Set for a font bake, for the summary line and the tests. Zero for a text bake.
    std::uint32_t glyphCount{0};
    std::uint32_t atlasWidth{0};
    std::uint32_t atlasHeight{0};
};

/// Reads a file as bytes. Returns nullopt when it cannot be opened or read.
[[nodiscard]] std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path);

/// Parses recipe text. Diagnostics are appended to `diagnostics`; nullopt means it did not parse.
[[nodiscard]] std::optional<Recipe> parseRecipe(std::string_view text,
                                                std::string_view recipePath,
                                                std::vector<cli::Diagnostic>& diagnostics);

/**
 * @brief Produces every output byte for `recipe`.
 *
 * Dispatches on `Recipe::font`. A font recipe rasterises its charset, packs the glyphs and fills
 * the sidecar with the atlas sheet; a text recipe reads the font package its recipe names and
 * positions one run per string, filling the sidecar with v1 records. A text recipe with no
 * `[strings]` table still produces a valid package, with no runs and an empty sidecar.
 *
 * @param recipe      the resolved recipe
 * @param recipePath  repository-relative, for `report.json`'s recipe record and for diagnostics
 * @param recipeBytes the recipe's own bytes, for its digest
 * @param root        the directory a recipe's file references resolve against - the repository
 *                    root, for a font recipe's `source` and a text recipe's `font` alike. Both
 *                    are confined to it: absolute paths and `..` escapes are refused rather than
 *                    followed.
 * @param diagnostics appended to on any problem
 *
 * Returns nullopt when the recipe fails to build a valid package - which for a font recipe
 * includes a character the font cannot draw, an outline it refuses, or a glyph set the atlas
 * budget cannot hold, and for a text recipe includes a font package it cannot read, a locale that
 * package does not approve, and a string reaching a code point nobody baked. Every such refusal
 * appends its own `TXT` diagnostic.
 */
[[nodiscard]] std::optional<BakeOutputs> run(const Recipe& recipe, std::string_view recipePath,
                                              std::span<const std::byte> recipeBytes,
                                              const std::filesystem::path& root,
                                              std::vector<cli::Diagnostic>& diagnostics);

/// Writes `outputs` into `outputDir`, creating it if needed. Appends a diagnostic and returns
/// false on any write failure.
[[nodiscard]] bool write(const BakeOutputs& outputs, const std::filesystem::path& outputDir,
                          std::vector<cli::Diagnostic>& diagnostics);

/// Compares `outputs` against the committed files, appending a diagnostic per mismatch.
[[nodiscard]] bool verify(const BakeOutputs& outputs, const std::filesystem::path& packagePath,
                          const std::filesystem::path& reportPath,
                          std::vector<cli::Diagnostic>& diagnostics);

}  // namespace mdux::tools::textbake
