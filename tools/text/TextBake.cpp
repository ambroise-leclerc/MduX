/**
 * @file TextBake.cpp
 * @brief Implementation of the text baker's recipe model and bake/verify core.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-010 No on-device text shaping
 */
module;

module mdux.tools.textbake;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.text.schema;
import mdux.tools.cli;
import mdux.tools.toml;
import mdux.tools.truetype;
import mdux.tools.atlaspacker;
import mdux.text.raster;
import mdux.font.schema;

namespace mdux::tools::textbake {

namespace json     = mdux::evidence::json;
namespace truetype = mdux::tools::truetype;
namespace atlas    = mdux::tools::atlas;
namespace raster   = mdux::text::raster;
namespace fontpkg  = mdux::font;

namespace {

/// Diagnostic codes. Stable once published: an agent keys off these, and a reworded message must
/// not break it. See docs/governance/schemas/diagnostic.schema.json.
///
/// `TXT000` is the recipe-unreadable code, emitted by `mdux-textbake`'s `main()` (matching
/// `SHB000` in the shader baker). The numbered library codes below begin at `TXT001`.
constexpr std::string_view recipeUnparsed = "TXT001";
constexpr std::string_view recipeMissingMember = "TXT002";
constexpr std::string_view recipeEmptyAtlas = "TXT003";
constexpr std::string_view recipeEmptyLocale = "TXT004";
constexpr std::string_view packageInvalid = "TXT005";
constexpr std::string_view outputUnwritable = "TXT006";
constexpr std::string_view artifactMissing = "TXT007";
constexpr std::string_view artifactDiffers = "TXT008";
constexpr std::string_view recipeEmptyId = "TXT009";
constexpr std::string_view recipeSidecarPathHasSeparator = "TXT010";
// The font pipeline (#160). Each parser, rasteriser and packer refusal keeps its own code rather
// than collapsing into one "bake failed", because an author fixing a recipe needs to know whether
// the font, the size or the charset is the problem - which only holds if the size genuinely has
// its own code, hence TXT018 rather than reusing the charset's.
constexpr std::string_view recipeCharsetMalformed = "TXT011";
constexpr std::string_view recipeFontUnreadable   = "TXT012";
constexpr std::string_view fontUnparsed           = "TXT013";
constexpr std::string_view charsetNotInFont       = "TXT014";
constexpr std::string_view glyphOutlineRejected   = "TXT015";
constexpr std::string_view glyphRasterFailed      = "TXT016";
constexpr std::string_view atlasPackingFailed     = "TXT017";
constexpr std::string_view recipePixelSizeInvalid = "TXT018";
// The digits in a baked font must share an advance, or a numeric field jitters as its
// value changes. Its own code because it is a property of the *font*, not the recipe:
// the fix is a different font, not a different charset.
constexpr std::string_view tabularFiguresMismatch = "TXT019";

// #235's text path. A font recipe's own "cannot read the font" is TXT012, which is about a
// TrueType file; these are about a baked font *package*, which fails in different ways and sends
// an author somewhere else - so they get their own codes rather than overloading TXT012/TXT013.
constexpr std::string_view recipeStringsMalformed  = "TXT020";
constexpr std::string_view recipeFontPackageUnread = "TXT021";
constexpr std::string_view fontPackageUnparsed     = "TXT022";
constexpr std::string_view localeNotApproved       = "TXT023";
constexpr std::string_view stringNotDrawable       = "TXT024";
constexpr std::string_view runTooWide              = "TXT025";

void report(std::vector<cli::Diagnostic>& diagnostics, std::string file, std::size_t line,
             std::string_view code, std::string message, std::string fixHint = {}) {
    diagnostics.push_back(cli::Diagnostic{.file = std::move(file),
                                          .line = line,
                                          .code = std::string{code},
                                          .severity = cli::Severity::Error,
                                          .message = std::move(message),
                                          .fixHint = std::move(fixHint)});
}

/// A path as it is recorded in a report: repository-relative with `/` separators, on every
/// platform. BakeReport::validate() rejects a backslash, and would be right to.
[[nodiscard]] std::string reportPath(const std::filesystem::path& path) {
    return path.generic_string();
}

[[nodiscard]] evidence::FileRecord fileRecord(std::string path,
                                              std::span<const std::byte> bytes) {
    return evidence::FileRecord{.path = std::move(path), .sha256 = evidence::sha256(bytes)};
}

[[nodiscard]] std::span<const std::byte> asBytes(const std::string& text) noexcept {
    return std::as_bytes(std::span{text.data(), text.size()});
}

/**
 * @brief Resolves `relativePath` under `root`, or nullopt when it would escape.
 *
 * `root / relativePath` is not the containment it looks like: std::filesystem's operator/
 * *replaces* the left operand when the right is absolute, so `source = "/etc/shadow"` reads that
 * file and ignores `root` entirely. A relative path with `..` components escapes the same way by a
 * different route. The recipe is repository content rather than network input, so this is not a
 * live attack surface today - but a baker that will one day run over a recipe somebody else wrote
 * should not have "the recipe is trusted" as its only defence.
 *
 * Shared by the font path's `source` and the text path's `font`, so the two cannot drift into
 * disagreeing about what is inside the repository.
 */
[[nodiscard]] std::optional<std::filesystem::path> confine(const std::filesystem::path& root,
                                                            std::string_view relativePath) {
    if (relativePath.empty() || std::filesystem::path{relativePath}.is_absolute()) {
        return std::nullopt;
    }
    const std::filesystem::path resolved   = (root / relativePath).lexically_normal();
    const std::filesystem::path normalRoot = root.lexically_normal();
    // lexically_relative() gives the path from root to the target; a result that starts with ".."
    // means the target is outside root. Comparing the normalised forms is what makes `a/../../b`
    // fail rather than pass on a string prefix check.
    const auto relative = resolved.lexically_relative(normalRoot);
    if (relative.empty() || *relative.begin() == "..") {
        return std::nullopt;
    }
    return resolved;
}

}  // namespace

// ---------------------------------------------------------------------------
// Recipe
// ---------------------------------------------------------------------------

json::Value Recipe::toOptions() const {
    json::Value options = json::Value::emptyObject();
    static_cast<void>(options.set("id", json::Value::string(id)));
    static_cast<void>(options.set("atlas", json::Value::string(atlas)));
    static_cast<void>(options.set("locale", json::Value::string(locale)));
    static_cast<void>(options.set("sidecar", json::Value::string(sidecar)));
    if (!font.has_value()) {
        // The resolved font package path, and how many runs were asked for. Not the strings
        // themselves: they are the recipe's own content, the recipe's digest is recorded a few
        // lines above this in every report, and their keys are already `package.json`'s run ids.
        // Copying them here would make one edited translation appear in three places of the same
        // artifact diff, which makes the diff harder to read rather than more complete.
        static_cast<void>(options.set("font", json::Value::string(fontPackage)));
        static_cast<void>(options.set("strings", json::Value::integer(strings.size())));
    }
    if (font.has_value()) {
        // Every resolved knob, because ADR-007's rule is that a silently changed default must not
        // leave every report looking unchanged. The charset is recorded as its ranges rather than
        // as a count: two different ranges can total the same number of code points.
        static_cast<void>(options.set("source", json::Value::string(font->source)));
        static_cast<void>(options.set("pixelSize", json::Value::integer(font->pixelSize)));
        std::vector<json::Value> localeValues;
        for (const std::string& tag : font->locales) {
            localeValues.push_back(json::Value::string(tag));
        }
        static_cast<void>(options.set("locales", json::Value::array(std::move(localeValues))));
        std::vector<json::Value> rangeValues;
        for (const CharsetRange& range : font->charset) {
            json::Value entry = json::Value::emptyObject();
            static_cast<void>(entry.set("name", json::Value::string(range.name)));
            static_cast<void>(entry.set("first", json::Value::integer(range.first)));
            static_cast<void>(entry.set("last", json::Value::integer(range.last)));
            rangeValues.push_back(std::move(entry));
        }
        static_cast<void>(options.set("charset", json::Value::array(std::move(rangeValues))));
    }
    return options;
}

std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::nullopt;
    }
    std::vector<std::byte> bytes;
    std::array<char, 8192> buffer{};
    while (file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) ||
           file.gcount() > 0) {
        const auto read = static_cast<std::size_t>(file.gcount());
        const auto* start = reinterpret_cast<const std::byte*>(buffer.data());
        bytes.insert(bytes.end(), start, start + read);
        if (file.eof()) {
            break;
        }
    }
    if (file.bad()) {
        return std::nullopt;
    }
    return bytes;
}

std::uint32_t FontSpec::codePointCount() const noexcept {
    std::uint32_t total = 0;
    for (const CharsetRange& range : charset) {
        total += range.count();
    }
    return total;
}

namespace {

/// Reads the `[charset]` parallel arrays and the `[locales]` list into a FontSpec.
///
/// Parallel arrays rather than an array of tables because mdux.tools.toml implements a subset with
/// no `[[table]]` support - the same shape recipes/model/ecg-demo.toml uses for its layers. Entry
/// N of each array describes range N, so a length mismatch is rejected by name rather than
/// silently truncating to the shortest array and baking a charset nobody asked for.
[[nodiscard]] std::optional<FontSpec> parseFontSpec(const toml::Document& document, const toml::Table& package,
                                                    std::string_view recipePath, std::vector<cli::Diagnostic>& diagnostics) {
    FontSpec     spec;
    std::int64_t declaredPixelSize = 0;
    try {
        spec.source       = package.require("source").asString();
        declaredPixelSize = package.require("pixelSize").asInteger();
    } catch (const toml::TomlError& error) {
        report(diagnostics, std::string{recipePath}, error.line(), recipeMissingMember, error.what(),
               "A font recipe needs [package] 'source' and 'pixelSize'.");
        return std::nullopt;
    }
    if (spec.source.empty()) {
        report(diagnostics, std::string{recipePath}, 0, recipeMissingMember, "[package] source is empty");
        return std::nullopt;
    }
    // Range-check the value TOML actually parsed, in its own width, *before* narrowing. Casting
    // the int64 to uint32 first would let -4294967280 and 4294967312 both arrive as 16 and bake
    // successfully at a size nobody wrote, which is worse than a rejected recipe because the
    // artifact would look deliberate.
    if (declaredPixelSize <= 0 || declaredPixelSize > static_cast<std::int64_t>(raster::maxPixelSize)) {
        // Its own code, not the charset's: an author who mistyped a size should not be sent to
        // look at [charset]. That was the whole argument for having distinct TXT codes.
        report(diagnostics, std::string{recipePath}, 0, recipePixelSizeInvalid,
               std::format("[package] pixelSize {} is outside 1..{}", declaredPixelSize, raster::maxPixelSize));
        return std::nullopt;
    }
    spec.pixelSize = static_cast<std::uint32_t>(declaredPixelSize);

    if (const toml::Table* locales = document.table("locales"); locales != nullptr) {
        try {
            for (const toml::Value& value : locales->require("ids").asArray()) {
                spec.locales.push_back(value.asString());
            }
        } catch (const toml::TomlError& error) {
            report(diagnostics, std::string{recipePath}, error.line(), recipeMissingMember, error.what());
            return std::nullopt;
        }
    }
    if (spec.locales.empty()) {
        report(diagnostics, std::string{recipePath}, 0, recipeEmptyLocale,
               "a font recipe declares no locales",
               "Add [locales] with an 'ids' array. A font containing a glyph is not the same as "
               "a font approved for a locale.");
        return std::nullopt;
    }
    // Every entry, not just the list. A text recipe's single locale must be non-empty, and a font
    // recipe's list had no equivalent check - so `ids = [""]` passed and emitted `"locales": [""]`,
    // which reads downstream as a locale rather than as the mistake it is. Duplicates are refused
    // for the same reason: a second identical approval records nothing and only invites a reader
    // to wonder what distinguishes them.
    for (std::size_t i = 0; i < spec.locales.size(); ++i) {
        if (spec.locales[i].empty()) {
            report(diagnostics, std::string{recipePath}, 0, recipeEmptyLocale,
                   std::format("[locales] entry {} is empty", i),
                   "Use a BCP 47 tag such as 'en-US'.");
            return std::nullopt;
        }
        if (std::ranges::find(spec.locales.begin(), spec.locales.begin() + static_cast<std::ptrdiff_t>(i), spec.locales[i])
            != spec.locales.begin() + static_cast<std::ptrdiff_t>(i)) {
            report(diagnostics, std::string{recipePath}, 0, recipeEmptyLocale,
                   std::format("[locales] lists '{}' more than once", spec.locales[i]));
            return std::nullopt;
        }
    }

    const toml::Table* charset = document.table("charset");
    if (charset == nullptr) {
        return std::nullopt;  // not a font recipe at all; the caller decides what that means
    }
    std::vector<std::string>   names;
    std::vector<std::int64_t>  firsts;
    std::vector<std::int64_t>  lasts;
    try {
        for (const toml::Value& v : charset->require("names").asArray())
            names.push_back(v.asString());
        for (const toml::Value& v : charset->require("firstCodePoints").asArray())
            firsts.push_back(v.asInteger());
        for (const toml::Value& v : charset->require("lastCodePoints").asArray())
            lasts.push_back(v.asInteger());
    } catch (const toml::TomlError& error) {
        report(diagnostics, std::string{recipePath}, error.line(), recipeCharsetMalformed, error.what(),
               "[charset] needs 'names', 'firstCodePoints' and 'lastCodePoints' arrays of equal length.");
        return std::nullopt;
    }
    if (names.size() != firsts.size() || names.size() != lasts.size()) {
        report(diagnostics, std::string{recipePath}, 0, recipeCharsetMalformed,
               std::format("[charset] arrays differ in length: names={}, firstCodePoints={}, lastCodePoints={}", names.size(),
                           firsts.size(), lasts.size()),
               "Entry N of each array describes range N, so all three must be the same length.");
        return std::nullopt;
    }
    if (names.empty()) {
        report(diagnostics, std::string{recipePath}, 0, recipeCharsetMalformed, "[charset] declares no ranges");
        return std::nullopt;
    }
    constexpr std::int64_t surrogateFirst = 0xD800;
    constexpr std::int64_t surrogateLast   = 0xDFFF;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (firsts[i] < 0 || lasts[i] < 0 || firsts[i] > lasts[i] || lasts[i] > 0x10FFFF) {
            report(diagnostics, std::string{recipePath}, 0, recipeCharsetMalformed,
                   std::format("[charset] range '{}' is not an ascending pair inside Unicode: {}..{}", names[i], firsts[i], lasts[i]));
            return std::nullopt;
        }
        // U+D800..U+DFFF are UTF-16 surrogate code *points*, not scalar values: they exist only to
        // encode astral characters as pairs and can never be a character in their own right. A
        // package listing one would be describing a glyph for something that cannot appear in
        // text, so the range is refused rather than silently split.
        if (firsts[i] <= surrogateLast && lasts[i] >= surrogateFirst) {
            report(diagnostics, std::string{recipePath}, 0, recipeCharsetMalformed,
                   std::format("[charset] range '{}' ({}..{}) intersects the UTF-16 surrogate block U+D800..U+DFFF", names[i],
                               firsts[i], lasts[i]),
                   "Surrogates are not Unicode scalar values. Split the range around them.");
            return std::nullopt;
        }
        spec.charset.push_back(CharsetRange{.name  = names[i],
                                            .first = static_cast<char32_t>(firsts[i]),
                                            .last  = static_cast<char32_t>(lasts[i])});
    }

    // Ranges must not overlap each other. A code point covered twice is rasterised twice, takes
    // two atlas slots, and appears twice in `package.json`'s glyph list - so a consumer indexing
    // by code point gets an ambiguous package and the atlas grows for nothing. That is the same
    // reasoning that makes a missing glyph fatal: the package must describe the charset that was
    // asked for, and a duplicate makes it describe something else.
    //
    // Checked on a sorted copy so the diagnostic can name both ranges, while `spec.charset` keeps
    // the recipe's order - which is what package.json's glyph list is emitted in.
    std::vector<const CharsetRange*> sorted;
    sorted.reserve(spec.charset.size());
    for (const CharsetRange& range : spec.charset) {
        sorted.push_back(&range);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const CharsetRange* a, const CharsetRange* b) noexcept { return a->first < b->first; });
    for (std::size_t i = 1; i < sorted.size(); ++i) {
        if (sorted[i]->first <= sorted[i - 1]->last) {
            report(diagnostics, std::string{recipePath}, 0, recipeCharsetMalformed,
                   std::format("[charset] ranges '{}' ({}..{}) and '{}' ({}..{}) overlap", sorted[i - 1]->name,
                               static_cast<std::uint32_t>(sorted[i - 1]->first), static_cast<std::uint32_t>(sorted[i - 1]->last),
                               sorted[i]->name, static_cast<std::uint32_t>(sorted[i]->first),
                               static_cast<std::uint32_t>(sorted[i]->last)),
                   "Each code point must be named by exactly one range.");
            return std::nullopt;
        }
    }
    return spec;
}

/**
 * @brief Reads a text recipe's `[strings]` table and its `[package] font` key into `recipe`.
 *
 * Both are optional together and required together. A recipe with neither is the S1 shape - a
 * package with no runs - which stays valid because a locale can legitimately have nothing to say
 * yet, and because every text package in this repository was that shape until #235. A recipe with
 * one and not the other is a half-written recipe, and saying so here is more useful than baking an
 * empty package and leaving the author to wonder where their strings went.
 */
[[nodiscard]] bool parseStrings(const toml::Document& document, const toml::Table& package, Recipe& recipe,
                                std::string_view recipePath, std::vector<cli::Diagnostic>& diagnostics) {
    const toml::Table* strings = document.table("strings");
    const toml::Value* font    = package.find("font");

    if (strings == nullptr && font == nullptr) {
        return true;
    }
    if (strings == nullptr || font == nullptr) {
        report(diagnostics, std::string{recipePath}, 0, recipeStringsMalformed,
               strings == nullptr ? "recipe names a font package and has no [strings] table"
                                  : "recipe has a [strings] table and names no font package",
               "A run is a string positioned against a font package: neither is meaningful alone.");
        return false;
    }

    std::vector<std::string> keys;
    std::vector<std::string> values;
    try {
        recipe.fontPackage = font->asString();
        keys               = strings->require("keys").asStringArray();
        values             = strings->require("values").asStringArray();
    } catch (const toml::TomlError& error) {
        report(diagnostics, std::string{recipePath}, error.line(), recipeStringsMalformed, error.what(),
               "[strings] needs parallel 'keys' and 'values' string arrays.");
        return false;
    }

    if (keys.size() != values.size()) {
        report(diagnostics, std::string{recipePath}, 0, recipeStringsMalformed,
               std::format("[strings] has {} keys and {} values", keys.size(), values.size()),
               "The two arrays are parallel: one value per key, in the same order.");
        return false;
    }
    if (keys.empty()) {
        report(diagnostics, std::string{recipePath}, 0, recipeStringsMalformed,
               "[strings] is present and empty",
               "Remove the table to bake a package with no runs, or give it a key.");
        return false;
    }

    for (std::size_t i = 0; i < keys.size(); ++i) {
        // Empty on either side, refused here rather than at schema validation. An empty key cannot
        // be named by `t("")`; an empty value would bake a zero-length run, which validates and
        // draws nothing, so a missing translation would ship as a blank label rather than as a
        // build failure. Both are recipe-authoring mistakes, and a diagnostic naming the index is
        // more actionable than a schema error at line 0.
        if (keys[i].empty()) {
            report(diagnostics, std::string{recipePath}, 0, recipeStringsMalformed,
                   std::format("[strings] key {} is empty", i));
            return false;
        }
        if (values[i].empty()) {
            report(diagnostics, std::string{recipePath}, 0, recipeStringsMalformed,
                   std::format("[strings] value for '{}' is empty", keys[i]),
                   "An empty translation bakes a run that draws nothing, which ships as a blank label.");
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (keys[j] == keys[i]) {
                report(diagnostics, std::string{recipePath}, 0, recipeStringsMalformed,
                       std::format("[strings] names the key '{}' twice", keys[i]),
                       "A key becomes a run id, which is unique within a package.");
                return false;
            }
        }
        recipe.strings.push_back(TextString{.key = keys[i], .text = values[i]});
    }
    return true;
}

}  // namespace

std::optional<Recipe> parseRecipe(std::string_view text, std::string_view recipePath,
                                   std::vector<cli::Diagnostic>& diagnostics) {
    toml::Document document{};
    try {
        document = toml::parse(text);
    } catch (const toml::TomlError& error) {
        report(diagnostics, std::string{recipePath}, error.line(), recipeUnparsed, error.what(),
               "See tools/text/TextBake.cppm for the recipe format.");
        return std::nullopt;
    }

    const toml::Table* package = document.table("package");
    if (package == nullptr) {
        report(diagnostics, std::string{recipePath}, 0, recipeMissingMember,
               "recipe has no [package] table",
               "Add a [package] table with 'id', 'atlas' and 'locale' keys.");
        return std::nullopt;
    }

    Recipe recipe;

    // A recipe carrying [charset] is a *font* recipe: it bakes an atlas out of a TrueType file
    // rather than positioning runs against one somebody else baked. The two share everything
    // around the edges - report, digests, write and verify - so they share this function and
    // `run()`, and differ only in what fills the sidecar. Dispatching on [charset] keeps
    // mdux-textbake one tool with one CLI, which is what #160 asks for.
    const bool isFontRecipe = document.table("charset") != nullptr;

    try {
        recipe.id = package->require("id").asString();
        if (!isFontRecipe) {
            recipe.atlas = package->require("atlas").asString();
            recipe.locale = package->require("locale").asString();
        }
        if (const toml::Value* sidecar = package->find("sidecar"); sidecar != nullptr) {
            recipe.sidecar = sidecar->asString();
        } else if (isFontRecipe) {
            recipe.sidecar = "atlas.bin";
        }
    } catch (const toml::TomlError& error) {
        report(diagnostics, std::string{recipePath}, error.line(), recipeMissingMember,
               error.what());
        return std::nullopt;
    }

    if (isFontRecipe) {
        auto spec = parseFontSpec(document, *package, recipePath, diagnostics);
        if (!spec.has_value()) {
            return std::nullopt;
        }
        recipe.font = std::move(*spec);
    } else if (!parseStrings(document, *package, recipe, recipePath, diagnostics)) {
        return std::nullopt;
    }

    if (!isFontRecipe && recipe.atlas.empty()) {
        report(diagnostics, std::string{recipePath}, 0, recipeEmptyAtlas,
               "recipe's 'atlas' is empty",
               "A text package references a font package id produced by S4 (#160).");
        return std::nullopt;
    }
    if (!isFontRecipe && recipe.locale.empty()) {
        report(diagnostics, std::string{recipePath}, 0, recipeEmptyLocale,
               "recipe's 'locale' is empty",
               "Use a BCP 47 tag such as 'en-US'. An unlocalized text package is not meaningful.");
        return std::nullopt;
    }
    // `id` and `sidecar` get dedicated recipe-level checks (rather than falling through to schema
    // validation as generic TXT005 "assembled package is not valid") because both are recipe-authoring
    // mistakes and a fix hint at the recipe line is more actionable than one at schema line 0.
    if (recipe.id.empty()) {
        report(diagnostics, std::string{recipePath}, 0, recipeEmptyId,
               "recipe's 'id' is empty",
               "The id is the <id> in generated/text/<id>/ and must be non-empty.");
        return std::nullopt;
    }
    if (recipe.sidecar.empty()) {
        report(diagnostics, std::string{recipePath}, 0, recipeSidecarPathHasSeparator,
               "recipe's 'sidecar' is empty; it must be a bare filename",
               "A sidecar sits beside package.json in generated/text/<id>/.");
        return std::nullopt;
    }
    if (recipe.sidecar.find('/') != std::string::npos ||
        recipe.sidecar.find('\\') != std::string::npos) {
        report(diagnostics, std::string{recipePath}, 0, recipeSidecarPathHasSeparator,
               "recipe's 'sidecar' contains a path separator; it must be a bare filename",
               "A sidecar sits beside package.json in generated/text/<id>/; a path would let it "
               "escape that directory.");
        return std::nullopt;
    }

    return recipe;
}

// ---------------------------------------------------------------------------
// run()
// ---------------------------------------------------------------------------

namespace {

/// One baked glyph, carrying everything the package records about it.
struct BakedGlyph {
    char32_t      codePoint{0};
    std::uint16_t glyphIndex{0};
    std::uint16_t advanceWidth{0};   ///< font units, as ADR-010 requires a baked glyph to carry
    std::int16_t  leftSideBearing{0};
    std::int32_t  bitmapOriginX{0};  ///< pixels, from the pen position to the bitmap's left edge
    std::int32_t  bitmapOriginY{0};  ///< pixels, from the baseline up to the bitmap's top edge
    raster::CoverageBitmap bitmap{};
};

/// Rasterises every code point the charset names, in recipe order.
///
/// Order matters and is the recipe's, not the font's: the packer sorts by size and then by id, so
/// its output does not depend on this order - but `package.json`'s glyph list is emitted in it,
/// and a reviewer reading a diff should see the charset they wrote.
[[nodiscard]] std::optional<std::vector<BakedGlyph>> rasteriseCharset(const truetype::Font& font, const FontSpec& spec,
                                                                      std::string_view recipePath,
                                                                      std::vector<cli::Diagnostic>& diagnostics) {
    std::vector<BakedGlyph> baked;
    baked.reserve(spec.codePointCount());
    bool failed = false;

    for (const CharsetRange& range : spec.charset) {
        for (char32_t point = range.first; point <= range.last; ++point) {
            const auto glyphIndex = truetype::glyphForCodePoint(font, point);
            if (!glyphIndex.has_value()) {
                // Fatal, not skipped. A recipe that names a character the font cannot draw is a
                // recipe with a mistake in it, and silently omitting the glyph would produce a
                // package whose charset does not match what was asked for - which the .medui
                // compiler (#15) would then trust.
                report(diagnostics, std::string{recipePath}, 0, charsetNotInFont,
                       std::format("range '{}': the font has no glyph for U+{:04X}", range.name, static_cast<std::uint32_t>(point)),
                       "Narrow the range, or bake a font that covers it.");
                failed = true;
                continue;
            }
            auto metrics = truetype::metricsFor(font, *glyphIndex);
            if (!metrics.has_value()) {
                report(diagnostics, std::string{recipePath}, 0, fontUnparsed,
                       std::format("U+{:04X} (glyph {}): {}", static_cast<std::uint32_t>(point), *glyphIndex,
                                   truetype::describe(metrics.error())));
                failed = true;
                continue;
            }
            auto outline = truetype::parseGlyph(font, *glyphIndex);
            if (!outline.has_value()) {
                // Composites land here, which is the ADR-010 seam: the parser refuses them and
                // this is where an author is told which character could not be pre-baked.
                report(diagnostics, std::string{recipePath}, 0, glyphOutlineRejected,
                       std::format("U+{:04X} (glyph {}): {}", static_cast<std::uint32_t>(point), *glyphIndex,
                                   truetype::describe(outline.error())));
                failed = true;
                continue;
            }

            // Translate the parser's outline into the rasteriser's own input type. This copy was
            // the trust-zone boundary made concrete until #116, when mdux.text.raster joined this
            // zone; the rasteriser could now name `truetype::GlyphPoint` directly. It is kept
            // because the decoupling is what would let the rasteriser move back into MduXCore if a
            // device path is ever built (see Raster.cppm), and the copy costs one pass per glyph
            // at build time.
            std::vector<raster::OutlinePoint> points;
            points.reserve(outline->points.size());
            for (const truetype::GlyphPoint& gp : outline->points) {
                points.push_back(raster::OutlinePoint{.x = gp.x, .y = gp.y, .onCurve = gp.onCurve});
            }
            // A glyph with no outline at all - the space, and every other blank - is recognised
            // here rather than sent to the rasteriser. `rasterise()` refuses an empty request with
            // `EmptyOutline`, and it is right to: an empty span is a caller mistake in every
            // context except this one, where it is the font correctly saying "this character
            // draws nothing". Encoding that knowledge in the baker keeps the rasteriser's contract
            // strict, and the alternative - having it return success for an empty request - would
            // make a genuinely malformed call indistinguishable from a space.
            //
            // The packer gives a zero-sized glyph a slot but no area, so the space still appears
            // in the package with its advance, which is the whole point of baking it.
            raster::CoverageBitmap coverage{};
            if (outline->points.empty() || outline->endPtsOfContours.empty()) {
                baked.push_back(BakedGlyph{.codePoint       = point,
                                           .glyphIndex      = *glyphIndex,
                                           .advanceWidth    = metrics->advanceWidth,
                                           .leftSideBearing = metrics->leftSideBearing,
                                           .bitmapOriginX   = 0,
                                           .bitmapOriginY   = 0,
                                           .bitmap          = std::move(coverage)});
                continue;
            }

            const raster::RasterRequest request{
                .outline    = raster::Outline{.points = points, .contourEnds = outline->endPtsOfContours},
                .unitsPerEm = font.unitsPerEm,
                .pixelSize  = spec.pixelSize};
            auto bitmap = raster::rasterise(request);
            if (!bitmap.has_value()) {
                report(diagnostics, std::string{recipePath}, 0, glyphRasterFailed,
                       std::format("U+{:04X} (glyph {}): {}", static_cast<std::uint32_t>(point), *glyphIndex,
                                   raster::describe(bitmap.error())));
                failed = true;
                continue;
            }

            baked.push_back(BakedGlyph{.codePoint       = point,
                                       .glyphIndex      = *glyphIndex,
                                       .advanceWidth    = metrics->advanceWidth,
                                       .leftSideBearing = metrics->leftSideBearing,
                                       .bitmapOriginX   = bitmap->originX,
                                       .bitmapOriginY   = bitmap->originY,
                                       .bitmap          = std::move(*bitmap)});
        }
    }
    if (failed) {
        return std::nullopt;
    }
    return baked;
}

/// The slot for each glyph, indexed by the id the packer was given - which is the glyph's index
/// in `glyphs`. Built once and validated once, then shared.
///
/// It used to be rebuilt independently by the blit and by the JSON writer, and the two disagreed
/// about a missing id: one skipped it, the other called `.at()` and threw out of a baker whose
/// contract is to return diagnostics. Building it here gives both call sites the same guarantee -
/// every glyph has exactly one slot, that slot is at least as large as its bitmap, and it lies
/// inside the sheet - so neither has to decide what a violation means.
using SlotIndex = std::vector<const atlas::GlyphSlot*>;

[[nodiscard]] std::optional<SlotIndex> indexSlots(const atlas::AtlasLayout& layout, const std::vector<BakedGlyph>& glyphs,
                                                  std::string_view recipePath, std::vector<cli::Diagnostic>& diagnostics) {
    SlotIndex index(glyphs.size(), nullptr);
    for (const atlas::GlyphSlot& slot : layout.slots) {
        if (slot.id >= glyphs.size()) {
            report(diagnostics, std::string{recipePath}, 0, atlasPackingFailed,
                   std::format("the packer returned a slot for id {}, which is not a baked glyph", slot.id));
            return std::nullopt;
        }
        if (index[slot.id] != nullptr) {
            report(diagnostics, std::string{recipePath}, 0, atlasPackingFailed,
                   std::format("the packer returned two slots for glyph {}", slot.id));
            return std::nullopt;
        }
        index[slot.id] = &slot;
    }
    for (std::uint32_t id = 0; id < glyphs.size(); ++id) {
        if (index[id] == nullptr) {
            report(diagnostics, std::string{recipePath}, 0, atlasPackingFailed,
                   std::format("the packer returned no slot for glyph {}", id));
            return std::nullopt;
        }
        const atlas::GlyphSlot& slot   = *index[id];
        const auto&             bitmap = glyphs[id].bitmap;
        if (slot.width != bitmap.width || slot.height != bitmap.height) {
            report(diagnostics, std::string{recipePath}, 0, atlasPackingFailed,
                   std::format("glyph {}'s slot is {}x{} but its bitmap is {}x{}", id, slot.width, slot.height, bitmap.width,
                               bitmap.height));
            return std::nullopt;
        }
        if (static_cast<std::uint64_t>(slot.x) + slot.width > layout.width
            || static_cast<std::uint64_t>(slot.y) + slot.height > layout.height) {
            report(diagnostics, std::string{recipePath}, 0, atlasPackingFailed,
                   std::format("glyph {}'s slot at ({},{}) {}x{} leaves the {}x{} sheet", id, slot.x, slot.y, slot.width,
                               slot.height, layout.width, layout.height));
            return std::nullopt;
        }
    }
    return index;
}

/// Blits each glyph's coverage into the packed sheet. The sheet starts at zero - fully
/// transparent - so the padding between slots stays empty rather than carrying whatever the
/// allocator left there.
[[nodiscard]] std::vector<std::byte> composeAtlas(const atlas::AtlasLayout& layout, const std::vector<BakedGlyph>& glyphs,
                                                  const SlotIndex& slots) {
    std::vector<std::byte> sheet(static_cast<std::size_t>(layout.width) * layout.height, std::byte{0});
    for (std::uint32_t index = 0; index < glyphs.size(); ++index) {
        const atlas::GlyphSlot& slot   = *slots[index];
        const auto&             bitmap = glyphs[index].bitmap;
        // Driven by the bitmap's own extent, not the slot's. indexSlots() has already established
        // that they agree, so this is belt and braces - but it is the loop that indexes
        // `bitmap.coverage`, and a source read should be bounded by the source.
        for (std::uint32_t y = 0; y < bitmap.height; ++y) {
            for (std::uint32_t x = 0; x < bitmap.width; ++x) {
                const std::size_t src = static_cast<std::size_t>(y) * bitmap.width + x;
                const std::size_t dst = static_cast<std::size_t>(slot.y + y) * layout.width + (slot.x + x);
                sheet[dst]            = static_cast<std::byte>(bitmap.coverage[src]);
            }
        }
    }
    return sheet;
}

/// Assembles the governed `font::FontPackage` for this bake.
///
/// S4 emitted this JSON by hand because the baker landed before `mdux.font.schema` did. Going
/// through the governed type now is the point of S5: one definition, imported by both the baker
/// that writes a package and the runtime that reads one, rather than a writer and a reader that
/// could drift (ADR-008 decision 1, mirrored to text by ADR-010). `FontPackage::write()` also
/// validates, so a package this baker emits is one that passed the same checks a consumer applies
/// on the way in.
[[nodiscard]] fontpkg::FontPackage buildFontPackage(const Recipe& recipe, const truetype::Font& font,
                                                    const atlas::AtlasLayout& layout, const std::vector<BakedGlyph>& glyphs,
                                                    const SlotIndex& slots, std::string_view sidecarName,
                                                    std::span<const std::byte> sheet) {
    const FontSpec& spec = *recipe.font;

    fontpkg::FontPackage package;
    package.id         = recipe.id;
    package.unitsPerEm = font.unitsPerEm;
    package.pixelSize  = spec.pixelSize;
    package.locales    = spec.locales;

    const auto digest    = evidence::sha256(sheet);
    const auto hex       = evidence::toHex(digest);
    package.atlas.path             = std::string{sidecarName};
    package.atlas.width            = layout.width;
    package.atlas.height           = layout.height;
    package.atlas.byteLength       = sheet.size();
    package.atlas.sha256           = std::string{hex.data(), hex.size()};
    package.atlas.occupancyPercent = layout.occupancyPercent();

    package.glyphs.reserve(glyphs.size());
    for (std::uint32_t index = 0; index < glyphs.size(); ++index) {
        const BakedGlyph&       glyph = glyphs[index];
        const atlas::GlyphSlot& slot  = *slots[index];
        package.glyphs.push_back(fontpkg::GlyphRecord{.codePoint       = glyph.codePoint,
                                                      .glyphIndex      = glyph.glyphIndex,
                                                      .advanceWidth    = glyph.advanceWidth,
                                                      .leftSideBearing = glyph.leftSideBearing,
                                                      .x               = slot.x,
                                                      .y               = slot.y,
                                                      .width           = slot.width,
                                                      .height          = slot.height,
                                                      .bitmapOriginX   = glyph.bitmapOriginX,
                                                      .bitmapOriginY   = glyph.bitmapOriginY});
    }
    // The schema requires ascending code-point order so its find() can binary-search. The bake
    // walks the recipe's ranges in the order they were written, which is not necessarily sorted.
    std::sort(package.glyphs.begin(), package.glyphs.end(),
              [](const fontpkg::GlyphRecord& a, const fontpkg::GlyphRecord& b) noexcept { return a.codePoint < b.codePoint; });

    // The restricted charset is the recipe's ranges, sorted. It is what the .medui compiler (#15)
    // will check a dynamic-text format against, and validate() refuses a package whose charset
    // names a code point it has no glyph for - so the table cannot promise more than the atlas
    // delivers.
    package.restrictedCharset.reserve(spec.charset.size());
    for (const CharsetRange& range : spec.charset) {
        package.restrictedCharset.push_back(fontpkg::CharsetRange{.first = range.first, .last = range.last});
    }
    std::sort(package.restrictedCharset.begin(), package.restrictedCharset.end(),
              [](const fontpkg::CharsetRange& a, const fontpkg::CharsetRange& b) noexcept { return a.first < b.first; });

    // No kerning yet. The field exists and is emitted empty rather than omitted, so a consumer
    // reads "this package bakes no kerning" instead of having to distinguish an absent member
    // from an empty one. Populating it is future work; ADR-010's rule is that whatever is not
    // here does not apply, because no runtime lookup can find it.
    return package;
}

/// The font half of `run()`.
[[nodiscard]] std::optional<BakeOutputs> runFontBake(const Recipe& recipe, std::string_view recipePath,
                                                     std::span<const std::byte> recipeBytes, const std::filesystem::path& root,
                                                     std::vector<cli::Diagnostic>& diagnostics) {
    const FontSpec& spec = *recipe.font;

    // Confine the source path to the repository before opening it. See `confine()` for why the
    // obvious `root / spec.source` is not containment.
    const auto sourcePath = confine(root, spec.source);
    if (!sourcePath.has_value()) {
        report(diagnostics, std::string{recipePath}, 0, recipeFontUnreadable,
               std::format("font path '{}' must be relative to the repository root and stay inside it", spec.source));
        return std::nullopt;
    }

    auto fontBytes = readFile(*sourcePath);
    if (!fontBytes.has_value()) {
        report(diagnostics, std::string{recipePath}, 0, recipeFontUnreadable,
               std::format("cannot read font '{}'", spec.source),
               "The path is resolved against the repository root.");
        return std::nullopt;
    }
    auto font = truetype::parse(*fontBytes);
    if (!font.has_value()) {
        report(diagnostics, std::string{recipePath}, 0, fontUnparsed,
               std::format("'{}': {}", spec.source, truetype::describe(font.error())));
        return std::nullopt;
    }

    auto glyphs = rasteriseCharset(*font, spec, recipePath, diagnostics);
    if (!glyphs.has_value()) {
        return std::nullopt;
    }

    std::vector<atlas::GlyphExtent> extents;
    extents.reserve(glyphs->size());
    for (std::uint32_t index = 0; index < glyphs->size(); ++index) {
        extents.push_back(atlas::GlyphExtent{.id     = index,
                                             .width  = (*glyphs)[index].bitmap.width,
                                             .height = (*glyphs)[index].bitmap.height});
    }
    auto layout = atlas::pack(extents);
    if (!layout.has_value()) {
        report(diagnostics, std::string{recipePath}, 0, atlasPackingFailed, std::string{atlas::describe(layout.error())},
               "Reduce the charset or the pixelSize.");
        return std::nullopt;
    }

    auto slots = indexSlots(*layout, *glyphs, recipePath, diagnostics);
    if (!slots.has_value()) {
        return std::nullopt;
    }

    BakeOutputs outputs;
    outputs.sidecarName = recipe.sidecar;
    outputs.sidecar     = composeAtlas(*layout, *glyphs, *slots);
    outputs.packageId   = recipe.id;
    outputs.glyphCount  = static_cast<std::uint32_t>(glyphs->size());
    outputs.atlasWidth  = layout->width;
    outputs.atlasHeight = layout->height;

    const auto package     = buildFontPackage(recipe, *font, *layout, *glyphs, *slots, outputs.sidecarName, outputs.sidecar);
    auto       packageText = package.write();
    if (!packageText.has_value()) {
        // Its own code when the digits disagree: that is a property of the font, so the fix is a
        // different font rather than a different recipe, and sending an author to re-read their
        // charset would waste their time.
        const bool tabular = packageText.error() == fontpkg::SchemaError::TabularFigureMismatch;
        report(diagnostics, std::string{recipePath}, 0, tabular ? tabularFiguresMismatch : packageInvalid,
               std::format("font package is not valid: {}", fontpkg::describe(packageText.error())),
               tabular ? "A numeric field redrawn as its value changes will jitter unless every digit "
                         "shares an advance. Use a font with tabular figures."
                       : "");
        return std::nullopt;
    }
    outputs.packageJson = std::move(*packageText);

    evidence::BakeReport bakeReport;
    bakeReport.tool        = std::string{toolName};
    bakeReport.toolVersion = MDUX_TOOL_VERSION;
    bakeReport.recipe      = fileRecord(std::string{recipePath}, recipeBytes);
    bakeReport.inputs      = {fileRecord(spec.source, *fontBytes)};
    bakeReport.options     = recipe.toOptions();
    bakeReport.outputs     = {fileRecord("package.json", asBytes(outputs.packageJson)),
                              fileRecord(outputs.sidecarName, outputs.sidecar)};

    auto reportText = bakeReport.write();
    if (!reportText.has_value()) {
        report(diagnostics, std::string{recipePath}, 0, packageInvalid,
               "bake report is not valid: " + std::string{evidence::describe(reportText.error())});
        return std::nullopt;
    }
    outputs.reportJson = std::move(*reportText);
    return outputs;
}

/// Decodes UTF-8 into scalar values, or nullopt at the first malformed byte.
///
/// A local decoder rather than a shared one: the only other UTF-8 code in the tools is the medui
/// lexer's `firstInvalidUtf8()`, which validates without decoding, and a shared decoder would be a
/// module boundary for forty lines. It rejects what the standard rejects - overlong forms,
/// surrogates, and anything past U+10FFFF - because each of those is a byte sequence that some
/// other decoder accepts and turns into a different character, and this one's output ends up in a
/// committed artifact.
[[nodiscard]] std::optional<std::vector<char32_t>> decodeUtf8(std::string_view input) {
    std::vector<char32_t> points;
    points.reserve(input.size());

    std::size_t i = 0;
    while (i < input.size()) {
        const auto    lead = static_cast<std::uint32_t>(static_cast<std::uint8_t>(input[i]));
        std::size_t   length = 0;
        std::uint32_t point  = 0;
        std::uint32_t lowest = 0;  ///< the smallest scalar this length may legally encode
        if (lead < 0x80u) {
            length = 1; point = lead;         lowest = 0u;
        } else if ((lead & 0xE0u) == 0xC0u) {
            length = 2; point = lead & 0x1Fu; lowest = 0x80u;
        } else if ((lead & 0xF0u) == 0xE0u) {
            length = 3; point = lead & 0x0Fu; lowest = 0x800u;
        } else if ((lead & 0xF8u) == 0xF0u) {
            length = 4; point = lead & 0x07u; lowest = 0x10000u;
        } else {
            return std::nullopt;  // a continuation byte, or a lead no encoding uses
        }
        if (i + length > input.size()) {
            return std::nullopt;
        }
        for (std::size_t k = 1; k < length; ++k) {
            const auto continuation = static_cast<std::uint32_t>(static_cast<std::uint8_t>(input[i + k]));
            if ((continuation & 0xC0u) != 0x80u) {
                return std::nullopt;
            }
            point = (point << 6u) | (continuation & 0x3Fu);
        }
        // `lowest` is what rejects the overlong forms: U+0041 encoded in two bytes decodes to 65
        // just as the one-byte form does, and a decoder that accepted both would let two different
        // recipes bake byte-different sidecars for the same string.
        if (point < lowest || point > 0x10FFFFu || (point >= 0xD800u && point <= 0xDFFFu)) {
            return std::nullopt;
        }
        points.push_back(static_cast<char32_t>(point));
        i += length;
    }
    return points;
}

/// Encodes one v1 record: little-endian `uint16 packageIndex`, `int16 x`, `int16 y`.
///
/// Spelled out byte by byte rather than memcpy'd from a struct, for the reason
/// `mdux::text::draw::decodeRecord()` gives for decoding the same way: the sidecar is committed
/// bytes compared across toolchains, so the encoding must not inherit the host's byte order or its
/// struct padding. Two toolchains disagreeing here would move glyphs, not fail.
void appendRecord(std::vector<std::byte>& out, std::uint16_t packageIndex, std::int16_t x, std::int16_t y) {
    const auto emit16 = [&out](std::uint16_t value) {
        out.push_back(static_cast<std::byte>(value & 0xFFu));
        out.push_back(static_cast<std::byte>((value >> 8) & 0xFFu));
    };
    emit16(packageIndex);
    emit16(std::bit_cast<std::uint16_t>(x));
    emit16(std::bit_cast<std::uint16_t>(y));
}

/// Font units to pixels, rounded half up, in integers throughout.
///
/// `pen` is non-negative for every run this baker produces - the pen starts at zero and advances -
/// so half-up and half-away-from-zero are the same rule here. Written as half-up because that is
/// what the expression says, rather than implying a negative case that cannot arise.
[[nodiscard]] std::int64_t toPixels(std::int64_t pen, std::uint32_t pixelSize, std::uint16_t unitsPerEm) noexcept {
    const auto scaled = pen * static_cast<std::int64_t>(pixelSize) + static_cast<std::int64_t>(unitsPerEm) / 2;
    return scaled / static_cast<std::int64_t>(unitsPerEm);
}

/// A font package and the bytes it was parsed from.
///
/// The bytes are carried rather than re-read for the report's input record. Reading the file twice
/// would digest something the bake did not necessarily use - a file that changed between the two
/// reads would produce a report attesting to a bake that never happened, which is the one thing an
/// evidence artifact may not do.
struct LoadedFont {
    fontpkg::FontPackage   package;
    std::vector<std::byte> bytes;
};

/// Reads and validates the font package a text recipe names, checking it is the one the recipe
/// says it is and that it approves the recipe's locale.
[[nodiscard]] std::optional<LoadedFont> loadFontPackage(const Recipe& recipe, std::string_view recipePath,
                                                         const std::filesystem::path& root,
                                                         std::vector<cli::Diagnostic>& diagnostics) {
    const auto path = confine(root, recipe.fontPackage);
    if (!path.has_value()) {
        report(diagnostics, std::string{recipePath}, 0, recipeFontPackageUnread,
               std::format("font package path '{}' must be relative to the repository root and stay inside it",
                           recipe.fontPackage));
        return std::nullopt;
    }
    auto bytes = readFile(*path);
    if (!bytes.has_value()) {
        report(diagnostics, std::string{recipePath}, 0, recipeFontPackageUnread,
               std::format("cannot read font package '{}'", recipe.fontPackage),
               "Bake the font package first; the path is resolved against the repository root.");
        return std::nullopt;
    }
    // Not named `text`: that is the enclosing `mdux::text` namespace's name, and a local shadowing
    // it is C4459 on MSVC, where warnings are errors.
    const std::string_view packageText{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
    auto package = fontpkg::FontPackage::parse(packageText);
    if (!package.has_value()) {
        report(diagnostics, std::string{recipePath}, 0, fontPackageUnparsed,
               std::format("'{}': {}", recipe.fontPackage, fontpkg::describe(package.error())));
        return std::nullopt;
    }

    // The recipe names the atlas twice - once as an id the package records, once as a path to read.
    // Checking they agree is what stops a recipe from recording runs as belonging to one font while
    // having measured them against another, which no consumer could detect: the compiler's own
    // wiring check (`checkLocaleWiring()`) compares the text package's `atlas` to the font it was
    // handed, and would pass on a package whose id was right and whose positions were not.
    if (package->id != recipe.atlas) {
        report(diagnostics, std::string{recipePath}, 0, fontPackageUnparsed,
               std::format("recipe's atlas is '{}' and '{}' holds the font package '{}'", recipe.atlas,
                           recipe.fontPackage, package->id),
               "The 'atlas' id and the 'font' path must name the same package.");
        return std::nullopt;
    }
    // A v1 record addresses a glyph with a `std::uint16_t`, so a package with more glyphs than
    // that cannot be indexed by one. Checked once here rather than per glyph: the alternative is a
    // static_cast that truncates, and a truncated index draws a different character rather than
    // failing - which is precisely the confusion `GlyphPlacement::packageIndex` is named to avoid.
    if (package->glyphs.size() > std::numeric_limits<std::uint16_t>::max()) {
        report(diagnostics, std::string{recipePath}, 0, fontPackageUnparsed,
               std::format("the font package '{}' holds {} glyphs, more than a v1 record can index",
                           package->id, package->glyphs.size()));
        return std::nullopt;
    }
    if (std::ranges::find(package->locales, recipe.locale) == package->locales.end()) {
        report(diagnostics, std::string{recipePath}, 0, localeNotApproved,
               std::format("the font package '{}' does not approve the locale '{}'", package->id, recipe.locale),
               "Add the locale to the font recipe, or bake this text against a font that approves it.");
        return std::nullopt;
    }
    return LoadedFont{.package = std::move(*package), .bytes = std::move(*bytes)};
}

/// Positions one string into v1 records. See TextBake.cppm for why this is a pen walk rather than
/// shaping, and why the pen accumulates in font units.
[[nodiscard]] std::optional<std::vector<std::byte>> positionRun(const fontpkg::FontPackage& font, const TextString& entry,
                                                                 std::string_view recipePath,
                                                                 std::vector<cli::Diagnostic>& diagnostics) {
    const auto points = decodeUtf8(entry.text);
    if (!points.has_value()) {
        report(diagnostics, std::string{recipePath}, 0, recipeStringsMalformed,
               std::format("the value for '{}' is not valid UTF-8", entry.key));
        return std::nullopt;
    }

    std::vector<std::byte> records;
    records.reserve(points->size() * text::recordSize);
    std::int64_t pen = 0;  // font units

    for (std::size_t i = 0; i < points->size(); ++i) {
        const char32_t point = (*points)[i];
        const fontpkg::GlyphRecord* glyph = font.find(point);
        if (glyph == nullptr) {
            // Fail rather than substitute. ADR-010 leaves the runtime no fallback, so a glyph this
            // baker cannot place is one that would be missing on the device - and a package that
            // silently dropped it would ship a word with a hole in it that nobody reviewed.
            report(diagnostics, std::string{recipePath}, 0, stringNotDrawable,
                   std::format("the value for '{}' contains U+{:04X}, which the font package '{}' cannot draw",
                               entry.key, static_cast<std::uint32_t>(point), font.id),
                   "Widen the font recipe's charset, or write the string with characters it bakes.");
            return std::nullopt;
        }

        // The record's index is into the *package's* glyph list, not the font's own glyph id -
        // `mdux::text::draw::GlyphPlacement` documents the distinction, and swapping the two draws
        // the wrong character rather than failing. `find()` returns a pointer into `font.glyphs`,
        // so the index is that pointer's offset.
        const auto packageIndex = static_cast<std::uint16_t>(glyph - font.glyphs.data());

        const std::int64_t x = toPixels(pen, font.pixelSize, font.unitsPerEm);
        if (x > std::numeric_limits<std::int16_t>::max()) {
            report(diagnostics, std::string{recipePath}, 0, runTooWide,
                   std::format("the value for '{}' reaches x={}px, past the {}px a v1 record can hold", entry.key, x,
                               std::numeric_limits<std::int16_t>::max()),
                   "Split the string, or shorten it.");
            return std::nullopt;
        }

        // y is the baseline, and it is zero for every record this baker writes: a run's own origin
        // is its baseline, and where the run sits on a screen is the emitter's decision, applied as
        // `originY` at draw time. A baker that baked a y offset in would be deciding layout.
        appendRecord(records, packageIndex, static_cast<std::int16_t>(x), std::int16_t{0});

        pen += glyph->advanceWidth;
        if (i + 1 < points->size()) {
            pen += font.kerningFor(point, (*points)[i + 1]);
        }
    }
    return records;
}

}  // namespace

std::optional<BakeOutputs> run(const Recipe& recipe, std::string_view recipePath,
                                std::span<const std::byte> recipeBytes,
                                const std::filesystem::path& root,
                                std::vector<cli::Diagnostic>& diagnostics) {
    if (recipe.font.has_value()) {
        return runFontBake(recipe, recipePath, recipeBytes, root, diagnostics);
    }

    BakeOutputs outputs;
    outputs.sidecarName = recipe.sidecar;

    // The font package, read once and shared by every run. Absent when the recipe carries no
    // strings, which is the pre-#235 shape and still valid: a package with no runs reads nothing.
    std::optional<LoadedFont> font;
    if (!recipe.strings.empty()) {
        font = loadFontPackage(recipe, recipePath, root, diagnostics);
        if (!font.has_value()) {
            return std::nullopt;
        }
    }

    text::TextPackage package;
    package.header.id = recipe.id;
    package.header.kind = std::string{text::packageKind};
    package.atlasId = recipe.atlas;
    package.locale = recipe.locale;
    package.sidecarPath = recipe.sidecar;

    for (const TextString& entry : recipe.strings) {
        auto records = positionRun(font->package, entry, recipePath, diagnostics);
        if (!records.has_value()) {
            return std::nullopt;
        }
        // Appended in recipe order, each run starting where the last one ended. Contiguous rather
        // than padded: the schema's overlap and bounds rules are about ranges, not alignment, and
        // a gap would be bytes no run accounts for in an artifact whose whole point is that every
        // byte is accounted for.
        text::TextRun positioned;
        positioned.id         = entry.key;
        positioned.byteOffset = outputs.sidecar.size();
        positioned.byteLength = records->size();
        positioned.sha256     = evidence::sha256(*records);
        outputs.sidecar.insert(outputs.sidecar.end(), records->begin(), records->end());
        package.runs.push_back(std::move(positioned));
    }

    package.sidecarByteLength = outputs.sidecar.size();
    package.sidecarSha256 = evidence::sha256(outputs.sidecar);

    auto packageText = package.write();
    if (!packageText.has_value()) {
        report(diagnostics, std::string{recipePath}, 0, packageInvalid,
               "assembled package is not valid: " +
                   std::string{text::describe(packageText.error())});
        return std::nullopt;
    }
    outputs.packageJson = std::move(*packageText);
    outputs.packageId = package.header.id;
    outputs.runCount = package.runs.size();

    // The font package is this bake's one input, and recording it is what makes the evidence chain
    // hold: re-baking the font changes its package.json, which changes this report's input digest,
    // which fails `evidence.text.<id>` until the runs are re-positioned against the new atlas. A
    // package with no runs reads nothing and records an empty list - present and structured rather
    // than omitted, so a reader distinguishes "no inputs" from "inputs not recorded".
    std::vector<evidence::FileRecord> inputs;
    if (font.has_value()) {
        inputs.push_back(fileRecord(recipe.fontPackage, font->bytes));
    }

    evidence::BakeReport bakeReport;
    bakeReport.tool = std::string{toolName};
    bakeReport.toolVersion = MDUX_TOOL_VERSION;
    bakeReport.recipe = fileRecord(std::string{recipePath}, recipeBytes);
    bakeReport.inputs = std::move(inputs);
    bakeReport.options = recipe.toOptions();
    // report.json is deliberately absent from its own outputs: a file cannot carry its own
    // digest, for the same reason ADR-007 gives for there being no commit SHA in here.
    bakeReport.outputs = {fileRecord("package.json", asBytes(outputs.packageJson)),
                           fileRecord(outputs.sidecarName, outputs.sidecar)};

    auto reportText = bakeReport.write();
    if (!reportText.has_value()) {
        report(diagnostics, std::string{recipePath}, 0, packageInvalid,
               "bake report is not valid: " +
                   std::string{evidence::describe(reportText.error())});
        return std::nullopt;
    }
    outputs.reportJson = std::move(*reportText);

    return outputs;
}

// ---------------------------------------------------------------------------
// write() / verify()
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] bool writeBytes(const std::filesystem::path& path, std::span<const std::byte> bytes,
                                std::vector<cli::Diagnostic>& diagnostics) {
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    if (!file) {
        report(diagnostics, reportPath(path), 0, outputUnwritable, "cannot open for writing");
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        report(diagnostics, reportPath(path), 0, outputUnwritable, "write failed");
        return false;
    }
    return true;
}

/// Compares produced bytes against a committed file, reporting the first differing offset.
///
/// An offset is worth the few lines it costs: "package.json differs" sends a reader to a diff
/// tool, where "differs at byte 412 (produced 0x33, committed 0x34)" usually identifies the field
/// on its own.
[[nodiscard]] bool compareArtifact(std::string_view label, std::span<const std::byte> produced,
                                    const std::filesystem::path& committedPath,
                                    std::vector<cli::Diagnostic>& diagnostics) {
    auto committed = readFile(committedPath);
    if (!committed.has_value()) {
        report(diagnostics, reportPath(committedPath), 0, artifactMissing,
               std::string{label} + " is missing or unreadable",
               "Run `cmake --build <dir> --target mdux-bake-update` to stage it.");
        return false;
    }

    const std::size_t common = std::min(produced.size(), committed->size());
    for (std::size_t i = 0; i < common; ++i) {
        if (produced[i] != (*committed)[i]) {
            report(diagnostics, reportPath(committedPath), 0, artifactDiffers,
                   std::string{label} + " differs at byte " + std::to_string(i) + ": produced 0x" +
                       std::format("{:02x}", std::to_integer<unsigned>(produced[i])) +
                       ", committed 0x" +
                       std::format("{:02x}", std::to_integer<unsigned>((*committed)[i])),
                   "Run `cmake --build <dir> --target mdux-bake-update` and review the diff.");
            return false;
        }
    }
    if (produced.size() != committed->size()) {
        report(diagnostics, reportPath(committedPath), 0, artifactDiffers,
               std::string{label} + " length differs: produced " +
                   std::to_string(produced.size()) + " bytes, committed " +
                   std::to_string(committed->size()),
               "Run `cmake --build <dir> --target mdux-bake-update` and review the diff.");
        return false;
    }
    return true;
}

}  // namespace

bool write(const BakeOutputs& outputs, const std::filesystem::path& outputDir,
           std::vector<cli::Diagnostic>& diagnostics) {
    std::error_code code;
    std::filesystem::create_directories(outputDir, code);
    if (code) {
        report(diagnostics, reportPath(outputDir), 0, outputUnwritable,
               "cannot create output directory: " + code.message());
        return false;
    }

    bool ok = writeBytes(outputDir / "package.json", asBytes(outputs.packageJson), diagnostics);
    ok = writeBytes(outputDir / "report.json", asBytes(outputs.reportJson), diagnostics) && ok;
    ok = writeBytes(outputDir / outputs.sidecarName, outputs.sidecar, diagnostics) && ok;
    return ok;
}

bool verify(const BakeOutputs& outputs, const std::filesystem::path& packagePath,
            const std::filesystem::path& reportPath, std::vector<cli::Diagnostic>& diagnostics) {
    bool ok = compareArtifact("package.json", asBytes(outputs.packageJson), packagePath,
                              diagnostics);
    ok = compareArtifact("report.json", asBytes(outputs.reportJson), reportPath, diagnostics) && ok;
    // The sidecar is not named on the command line - it sits beside package.json, under the name
    // the package itself records. Verifying it here rather than trusting the package's digest is
    // the point: a package whose recorded digest matches a sidecar nobody compared proves nothing.
    ok = compareArtifact(outputs.sidecarName, outputs.sidecar,
                         packagePath.parent_path() / outputs.sidecarName, diagnostics) &&
         ok;
    return ok;
}

}  // namespace mdux::tools::textbake
