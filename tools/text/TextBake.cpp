/**
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

namespace mdux::tools::textbake {

namespace json     = mdux::evidence::json;
namespace truetype = mdux::tools::truetype;
namespace atlas    = mdux::tools::atlas;
namespace raster   = mdux::text::raster;

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
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (firsts[i] < 0 || lasts[i] < 0 || firsts[i] > lasts[i] || lasts[i] > 0x10FFFF) {
            report(diagnostics, std::string{recipePath}, 0, recipeCharsetMalformed,
                   std::format("[charset] range '{}' is not an ascending pair inside Unicode: {}..{}", names[i], firsts[i], lasts[i]));
            return std::nullopt;
        }
        spec.charset.push_back(CharsetRange{.name  = names[i],
                                            .first = static_cast<char32_t>(firsts[i]),
                                            .last  = static_cast<char32_t>(lasts[i])});
    }
    return spec;
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

            // Translate the parser's outline into the rasteriser's governed input type. This copy
            // is the trust-zone boundary made concrete: mdux.text.raster is governed and cannot
            // name a host-tools type, so the baker is what bridges them (ADR-004).
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

/// Blits each glyph's coverage into the packed sheet. The sheet starts at zero - fully
/// transparent - so the padding between slots stays empty rather than carrying whatever the
/// allocator left there.
[[nodiscard]] std::vector<std::byte> composeAtlas(const atlas::AtlasLayout& layout, const std::vector<BakedGlyph>& glyphs) {
    std::vector<std::byte> sheet(static_cast<std::size_t>(layout.width) * layout.height, std::byte{0});
    std::map<std::uint32_t, const atlas::GlyphSlot*> byId;
    for (const atlas::GlyphSlot& slot : layout.slots) {
        byId.emplace(slot.id, &slot);
    }
    for (std::uint32_t index = 0; index < glyphs.size(); ++index) {
        const auto it = byId.find(index);
        if (it == byId.end() || it->second->width == 0 || it->second->height == 0) {
            continue;
        }
        const atlas::GlyphSlot& slot   = *it->second;
        const auto&             bitmap = glyphs[index].bitmap;
        for (std::uint32_t y = 0; y < slot.height; ++y) {
            for (std::uint32_t x = 0; x < slot.width; ++x) {
                const std::size_t src = static_cast<std::size_t>(y) * bitmap.width + x;
                const std::size_t dst = static_cast<std::size_t>(slot.y + y) * layout.width + (slot.x + x);
                sheet[dst]            = static_cast<std::byte>(bitmap.coverage[src]);
            }
        }
    }
    return sheet;
}

/// Builds the font package JSON.
///
/// Emitted directly rather than through a schema type because `mdux.font.schema` is S5 (#161),
/// which lands after this. The shape below is what S5 formalises; keeping it in canonical JSON
/// now means the committed artifact does not have to change when the schema module arrives, only
/// gain a reader.
[[nodiscard]] json::Value fontPackageJson(const Recipe& recipe, const truetype::Font& font, const atlas::AtlasLayout& layout,
                                          const std::vector<BakedGlyph>& glyphs, std::string_view sidecarName,
                                          std::span<const std::byte> sheet) {
    const FontSpec& spec = *recipe.font;

    json::Value package = json::Value::emptyObject();
    static_cast<void>(package.set("schemaVersion", json::Value::integer(1)));
    static_cast<void>(package.set("id", json::Value::string(recipe.id)));
    static_cast<void>(package.set("kind", json::Value::string("font")));
    static_cast<void>(package.set("unitsPerEm", json::Value::integer(font.unitsPerEm)));
    static_cast<void>(package.set("pixelSize", json::Value::integer(spec.pixelSize)));

    std::vector<json::Value> localeValues;
    for (const std::string& tag : spec.locales) {
        localeValues.push_back(json::Value::string(tag));
    }
    static_cast<void>(package.set("locales", json::Value::array(std::move(localeValues))));

    json::Value atlasValue = json::Value::emptyObject();
    static_cast<void>(atlasValue.set("path", json::Value::string(std::string{sidecarName})));
    static_cast<void>(atlasValue.set("width", json::Value::integer(layout.width)));
    static_cast<void>(atlasValue.set("height", json::Value::integer(layout.height)));
    static_cast<void>(atlasValue.set("byteLength", json::Value::integer(static_cast<std::int64_t>(sheet.size()))));
    const auto digest = evidence::sha256(sheet);
    const auto hex    = evidence::toHex(digest);
    static_cast<void>(atlasValue.set("sha256", json::Value::string(std::string{hex.data(), hex.size()})));
    static_cast<void>(atlasValue.set("occupancyPercent", json::Value::integer(layout.occupancyPercent())));
    static_cast<void>(package.set("atlas", std::move(atlasValue)));

    std::map<std::uint32_t, const atlas::GlyphSlot*> byId;
    for (const atlas::GlyphSlot& slot : layout.slots) {
        byId.emplace(slot.id, &slot);
    }
    std::vector<json::Value> glyphValues;
    glyphValues.reserve(glyphs.size());
    for (std::uint32_t index = 0; index < glyphs.size(); ++index) {
        const BakedGlyph&       glyph = glyphs[index];
        const atlas::GlyphSlot& slot  = *byId.at(index);
        json::Value             entry = json::Value::emptyObject();
        static_cast<void>(entry.set("codePoint", json::Value::integer(glyph.codePoint)));
        static_cast<void>(entry.set("glyphIndex", json::Value::integer(glyph.glyphIndex)));
        static_cast<void>(entry.set("advanceWidth", json::Value::integer(glyph.advanceWidth)));
        static_cast<void>(entry.set("leftSideBearing", json::Value::integer(glyph.leftSideBearing)));
        static_cast<void>(entry.set("x", json::Value::integer(slot.x)));
        static_cast<void>(entry.set("y", json::Value::integer(slot.y)));
        static_cast<void>(entry.set("width", json::Value::integer(slot.width)));
        static_cast<void>(entry.set("height", json::Value::integer(slot.height)));
        static_cast<void>(entry.set("bitmapOriginX", json::Value::integer(glyph.bitmapOriginX)));
        static_cast<void>(entry.set("bitmapOriginY", json::Value::integer(glyph.bitmapOriginY)));
        glyphValues.push_back(std::move(entry));
    }
    static_cast<void>(package.set("glyphs", json::Value::array(std::move(glyphValues))));
    return package;
}

/// The font half of `run()`.
[[nodiscard]] std::optional<BakeOutputs> runFontBake(const Recipe& recipe, std::string_view recipePath,
                                                     std::span<const std::byte> recipeBytes, const std::filesystem::path& root,
                                                     std::vector<cli::Diagnostic>& diagnostics) {
    const FontSpec& spec = *recipe.font;

    auto fontBytes = readFile(root / spec.source);
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

    BakeOutputs outputs;
    outputs.sidecarName = recipe.sidecar;
    outputs.sidecar     = composeAtlas(*layout, *glyphs);
    outputs.packageId   = recipe.id;
    outputs.glyphCount  = static_cast<std::uint32_t>(glyphs->size());
    outputs.atlasWidth  = layout->width;
    outputs.atlasHeight = layout->height;

    auto packageValue = fontPackageJson(recipe, *font, *layout, *glyphs, outputs.sidecarName, outputs.sidecar);
    auto packageText  = json::write(packageValue);
    if (!packageText.has_value()) {
        // The writer's error carries a code plus context; only the code has a stable description.
        report(diagnostics, std::string{recipePath}, 0, packageInvalid,
               "assembled font package is not valid: " + std::string{json::describe(packageText.error().code)});
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

}  // namespace

std::optional<BakeOutputs> run(const Recipe& recipe, std::string_view recipePath,
                                std::span<const std::byte> recipeBytes,
                                const std::filesystem::path& root,
                                std::vector<cli::Diagnostic>& diagnostics) {
    if (recipe.font.has_value()) {
        return runFontBake(recipe, recipePath, recipeBytes, root, diagnostics);
    }
    static_cast<void>(root);  // A text bake reads no source files; the font path uses `root`.

    BakeOutputs outputs;
    outputs.sidecarName = recipe.sidecar;

    text::TextPackage package;
    package.header.id = recipe.id;
    package.header.kind = std::string{text::packageKind};
    package.atlasId = recipe.atlas;
    package.locale = recipe.locale;
    package.sidecarPath = recipe.sidecar;
    package.sidecarByteLength = 0;  // S1: no runs. The first real artifact is S4 (#160).
    package.sidecarSha256 = evidence::sha256(outputs.sidecar);
    // No runs are appended; the package validates with an empty run list, by design.

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

    // No inputs at S1: a no-run package reads no source files. S4's font pipeline will record its
    // TrueType and rasteriser inputs here; recording an empty list now keeps the report's inputs
    // array present and structured rather than omitted.
    std::vector<evidence::FileRecord> inputs;

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
