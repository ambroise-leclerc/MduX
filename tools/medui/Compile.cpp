/**
 * @file Compile.cpp
 * @brief Implementation of the `.medui` compiler driver.
 */

module;

module mdux.tools.medui.compile;

import std;
import mdux.draw;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.font.schema;
import mdux.image.schema;
import mdux.medui.reading;
import mdux.medui.schema;
import mdux.text.schema;
import mdux.tools.cli;
import mdux.tools.medui.ast;
import mdux.tools.medui.diagnostics;
import mdux.tools.medui.goldens;
import mdux.tools.medui.layout;
import mdux.tools.medui.package;
import mdux.tools.medui.parser;
import mdux.tools.medui.semantic;
import mdux.tools.medui.textbudget;
import mdux.tools.toml;

namespace mdux::tools::medui {

namespace {

namespace ms = mdux::medui;

void report(std::vector<cli::Diagnostic>& diagnostics, Code code, std::string file, std::size_t line, std::string message, std::string fixHint = {}) {
    diagnostics.push_back(diagnose(code, std::move(file), line, 0, std::move(message), std::move(fixHint)));
}

/// Whether `id` is the lowercase slug an artifact directory, an evidence test name and a generated
/// C++ identifier are all derived from. `mdux_bake_artifact()` enforces the same shape at configure
/// time; this is the half that also holds when the compiler is run directly.
[[nodiscard]] bool isArtifactSlug(std::string_view id) noexcept {
    const auto lower = [](char c) {
        return c >= 'a' && c <= 'z';
    };
    const auto digit = [](char c) {
        return c >= '0' && c <= '9';
    };
    if (id.empty() || (!lower(id.front()) && !digit(id.front()))) {
        return false;
    }
    return std::ranges::all_of(id, [&](char c) {
        return lower(c) || digit(c) || c == '-';
    });
}

/// A screen name and an artifact slug reduced to the one word they must both spell: lowercase, with
/// every separator removed.
///
/// A weaker check than deriving one from the other, and deliberately so. A slug cannot be derived
/// injectively from a screen name - a name may carry `-` and `_`, and lowercasing is not injective
/// across case - which is why ADR-012 has the recipe *record* the mapping rather than compute it.
/// What must not happen is the two drifting apart: `generated/screen/foo/` holding a package that
/// calls itself `bar`. Comparing the reduced forms catches exactly that, and leaves an author free
/// to hyphenate a slug as they like.
[[nodiscard]] std::string reduced(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '-' || c == '_') {
            continue;
        }
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

[[nodiscard]] std::span<const std::byte> asBytes(const std::string& text) noexcept {
    return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

[[nodiscard]] evidence::FileRecord fileRecord(std::string path, std::span<const std::byte> bytes) {
    return evidence::FileRecord{.path = std::move(path), .sha256 = evidence::sha256(bytes)};
}

/// A path as a report and a diagnostic spell it: forward slashes on every platform, because
/// `BakeReport::validate()` rejects a backslash and would be right to.
///
/// Named for the spelling rather than for the file, because `verify()` takes a `reportPath`
/// parameter and a helper sharing that name would be shadowed at exactly one call site.
[[nodiscard]] std::string displayPath(const std::filesystem::path& path) {
    return path.generic_string();
}

[[nodiscard]] std::string_view textOf(std::span<const std::byte> bytes) noexcept {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// Reads a committed package beside the recipe, as text, reporting the path that failed.
[[nodiscard]] std::optional<std::vector<std::byte>>
readInput(const std::filesystem::path& root, const std::string& relative, std::vector<cli::Diagnostic>& diagnostics) {
    std::optional<std::vector<std::byte>> bytes = readFile(root / relative);
    if (!bytes.has_value()) {
        report(diagnostics, Code::SourceUnreadable, relative, 0, "the file named by the recipe could not be read");
    }
    return bytes;
}

}  // namespace

std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file) {
        return std::nullopt;
    }
    // tellg() answers -1 on a stream error rather than throwing, and sizing the vector before the
    // check would turn that into a request for 2^64-1 bytes.
    const std::streamoff size = file.tellg();
    if (size < 0 || !file.seekg(0)) {
        return std::nullopt;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (size > 0) {
        file.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!file) {
            return std::nullopt;
        }
    }
    return bytes;
}

evidence::json::Value Recipe::toOptions() const {
    using evidence::json::Value;

    const auto put = [](Value& object, std::string key, Value value) {
        if (const auto inserted = object.set(std::move(key), std::move(value)); !inserted.has_value()) {
            throw std::logic_error("canonical JSON refused a recipe option");
        }
    };

    Value budgetValue = Value::emptyObject();
    put(budgetValue, "maxCommands", Value::unsignedInteger(budget.maxCommands));
    put(budgetValue, "maxIndices", Value::unsignedInteger(budget.maxIndices));
    put(budgetValue, "maxVertices", Value::unsignedInteger(budget.maxVertices));

    std::vector<Value> packages;
    packages.reserve(textPackages.size());
    for (const std::string& package : textPackages) {
        packages.push_back(Value::string(package));
    }
    std::vector<Value> images;
    images.reserve(imagePackages.size());
    for (const std::string& package : imagePackages) {
        images.push_back(Value::string(package));
    }

    // The table as resolved, so a report shows what a compile was actually checked against rather
    // than the name of a table whose contents nobody can see.
    std::vector<Value> dynamic;
    dynamic.reserve(dynamicText.size());
    for (const DynamicText& rule : dynamicText) {
        std::vector<Value> ranges;
        ranges.reserve(rule.produces.size());
        for (const mdux::font::CharsetRange& range : rule.produces) {
            Value entry = Value::emptyObject();
            put(entry, "first", Value::unsignedInteger(static_cast<std::uint64_t>(range.first)));
            put(entry, "last", Value::unsignedInteger(static_cast<std::uint64_t>(range.last)));
            ranges.push_back(std::move(entry));
        }
        Value named = Value::emptyObject();
        put(named, "name", Value::string(rule.name));
        put(named, "produces", Value::array(std::move(ranges)));
        dynamic.push_back(std::move(named));
    }

    // Likewise, and for the same reason: a report that named `TPL-PRESSURE-MMHG` without recording
    // what it renders as would say which template a screen was measured against and not what that
    // measurement was of.
    std::vector<Value> templates;
    templates.reserve(numericTemplates.size());
    for (const NumericTemplate& rule : numericTemplates) {
        Value named = Value::emptyObject();
        put(named, "name", Value::string(rule.name));
        put(named, "rendering", Value::string(rule.rendering));
        templates.push_back(std::move(named));
    }

    Value options = Value::emptyObject();
    put(options, "budget", std::move(budgetValue));
    put(options, "dynamicText", Value::array(std::move(dynamic)));
    put(options, "numericTemplates", Value::array(std::move(templates)));
    // The id belongs in the report for the reason every other resolved knob does: it names the
    // directory the artifact lives in, and a report that did not record it would describe a compile
    // without saying which screen it produced.
    put(options, "id", Value::string(id));
    put(options, "fontPackage", Value::string(fontPackage));
    put(options, "imagePackages", Value::array(std::move(images)));
    put(options, "source", Value::string(source));
    put(options, "surfaceHeight", Value::integer(surfaceHeight));
    put(options, "surfaceWidth", Value::integer(surfaceWidth));
    put(options, "textPackages", Value::array(std::move(packages)));
    return options;
}

std::optional<Recipe> parseRecipe(std::string_view text, std::string_view recipePath, std::vector<cli::Diagnostic>& diagnostics) {
    toml::Document document{};
    try {
        document = toml::parse(text);
    } catch (const toml::TomlError& error) {
        report(diagnostics,
               Code::RecipeUnparsed,
               std::string{recipePath},
               error.line(),
               error.what(),
               "see recipes/screen/ for the accepted shape; there is no [[table]] support");
        return std::nullopt;
    }

    Recipe recipe;

    const toml::Table* package = document.table("package");
    if (package == nullptr) {
        report(diagnostics,
               Code::RecipeMissingMember,
               std::string{recipePath},
               0,
               "recipe has no [package] table",
               "add a [package] table with 'id' and 'source' keys");
        return std::nullopt;
    }

    // Every knob is required, defaults included - which is to say there are none. MEDUI-E002 states
    // the reason: a defaulted knob does not appear in report.json, so changing the default later
    // would leave every report looking unchanged, which ADR-007 forbids.
    try {
        recipe.id            = package->require("id").asString();
        recipe.source        = package->require("source").asString();
        recipe.surfaceWidth  = package->require("surfaceWidth").asInteger();
        recipe.surfaceHeight = package->require("surfaceHeight").asInteger();
    } catch (const toml::TomlError& error) {
        report(diagnostics,
               Code::RecipeMissingMember,
               std::string{recipePath},
               error.line(),
               error.what(),
               "[package] needs 'id', 'source', 'surfaceWidth' and 'surfaceHeight'");
        return std::nullopt;
    }

    // The slug shape ADR-012 fixes, checked here rather than left to `mdux_bake_artifact()`'s
    // FATAL_ERROR: the id names the directory, the evidence test and the emitted C++ identifier, and
    // a compile run outside CMake would otherwise write `generated/screen/NeuroSense500/`, which no
    // registration could ever match.
    if (!isArtifactSlug(recipe.id)) {
        report(diagnostics,
               Code::RecipeMissingMember,
               std::string{recipePath},
               0,
               std::format("the id '{}' is not a lowercase slug", recipe.id),
               "an artifact id matches ^[a-z0-9][a-z0-9-]*$ - it names a directory, a test and a C++ identifier");
        return std::nullopt;
    }

    const toml::Table* budget = document.table("budget");
    if (budget == nullptr) {
        report(diagnostics,
               Code::RecipeMissingMember,
               std::string{recipePath},
               0,
               "recipe has no [budget] table",
               "add a [budget] table with 'maxVertices', 'maxIndices' and 'maxCommands'. The runtime "
               "fails closed against these, so a product declares them rather than inheriting them");
        return std::nullopt;
    }

    try {
        const std::int64_t vertices   = budget->require("maxVertices").asInteger();
        const std::int64_t indices    = budget->require("maxIndices").asInteger();
        const std::int64_t commands   = budget->require("maxCommands").asInteger();
        const std::size_t  budgetLine = budget->require("maxVertices").line();
        for (const std::int64_t value : {vertices, indices, commands}) {
            if (value < 0 || value > std::numeric_limits<std::uint32_t>::max()) {
                report(diagnostics,
                       Code::RecipeMissingMember,
                       std::string{recipePath},
                       budget->require("maxVertices").line(),
                       std::format("a draw budget entry is {}, which is outside the range a budget holds", value));
                return std::nullopt;
            }
        }
        // The 16-bit index bound, refused while the recipe can still be told about it. Left to
        // `ScreenPackage::validate()` it becomes `BudgetExceedsIndexWidth` reported to a caller that
        // treats a schema failure as a compiler bug and throws - so an author's extra digit would
        // terminate the tool instead of producing a diagnostic.
        if (vertices > mdux::draw::maxIndexableVertices) {
            report(diagnostics,
                   Code::RecipeMissingMember,
                   std::string{recipePath},
                   budgetLine,
                   std::format("maxVertices is {}, and a 16-bit index cannot address more than {}", vertices, mdux::draw::maxIndexableVertices));
            return std::nullopt;
        }
        recipe.budget = mdux::draw::DrawBudget{.maxVertices = static_cast<std::uint32_t>(vertices),
                                               .maxIndices  = static_cast<std::uint32_t>(indices),
                                               .maxCommands = static_cast<std::uint32_t>(commands)};
    } catch (const toml::TomlError& error) {
        report(diagnostics,
               Code::RecipeMissingMember,
               std::string{recipePath},
               error.line(),
               error.what(),
               "[budget] needs 'maxVertices', 'maxIndices' and 'maxCommands'");
        return std::nullopt;
    }

    // The text half is optional as a *table*, and the compile refuses later if the screen turns out
    // to need it - see run(). A screen that draws no text has no font to name, and requiring one
    // would mean inventing an approved locale set for a screen that has no text to approve.
    if (const toml::Table* textTable = document.table("text"); textTable != nullptr) {
        try {
            recipe.fontPackage  = textTable->require("fontPackage").asString();
            recipe.textPackages = textTable->require("packages").asStringArray();
        } catch (const toml::TomlError& error) {
            report(diagnostics,
                   Code::RecipeMissingMember,
                   std::string{recipePath},
                   error.line(),
                   error.what(),
                   "[text] needs 'fontPackage' and a 'packages' array, one committed text package per approved locale");
            return std::nullopt;
        }
    }

    if (const toml::Table* imageTable = document.table("images"); imageTable != nullptr) {
        try {
            recipe.imagePackages = imageTable->require("packages").asStringArray();
        } catch (const toml::TomlError& error) {
            report(diagnostics,
                   Code::RecipeMissingMember,
                   std::string{recipePath},
                   error.line(),
                   error.what(),
                   "[images] needs a 'packages' array of committed image package.json files");
            return std::nullopt;
        }
    }

    // The governed dynamic-text table. Optional as a table for the same reason [text] is: a screen
    // with no `charset:` has no open name to resolve, and the budget stage refuses an unknown name
    // rather than accepting one, so an absent table is fail-closed. A Clock's closed `format:` is
    // measured directly and never resolves through this table.
    if (const toml::Table* dynamicTable = document.table("dynamicText"); dynamicTable != nullptr) {
        std::vector<std::string>  names;
        std::vector<std::int64_t> firstPoints;
        std::vector<std::int64_t> lastPoints;
        std::size_t               namesLine = 0;
        try {
            const toml::Value& namesValue = dynamicTable->require("names");
            namesLine                     = namesValue.line();
            names                         = namesValue.asStringArray();
            firstPoints                   = dynamicTable->require("firstCodePoints").asIntegerArray();
            lastPoints                    = dynamicTable->require("lastCodePoints").asIntegerArray();
        } catch (const toml::TomlError& error) {
            report(diagnostics,
                   Code::RecipeMissingMember,
                   std::string{recipePath},
                   error.line(),
                   error.what(),
                   "[dynamicText] needs parallel 'names', 'firstCodePoints' and 'lastCodePoints' arrays");
            return std::nullopt;
        }

        if (names.size() != firstPoints.size() || names.size() != lastPoints.size()) {
            report(diagnostics,
                   Code::RecipeMissingMember,
                   std::string{recipePath},
                   namesLine,
                   std::format("[dynamicText] names has {} entries, firstCodePoints {} and lastCodePoints {}",
                               names.size(),
                               firstPoints.size(),
                               lastPoints.size()),
                   "the arrays are positional: entry N of each describes rule N");
            return std::nullopt;
        }

        for (std::size_t index = 0; index < names.size(); ++index) {
            const std::int64_t first = firstPoints[index];
            const std::int64_t last  = lastPoints[index];
            // Bounded here rather than left to the walk: the budget stage refuses a range outside
            // Unicode, but a recipe naming one should be told which entry it was rather than which
            // glyph the walk stopped at.
            if (first < 0 || last < 0 || first > 0x10FFFF || last > 0x10FFFF) {
                report(diagnostics,
                       Code::RecipeMissingMember,
                       std::string{recipePath},
                       namesLine,
                       std::format("[dynamicText] entry '{}' names the range {}..{}, which is outside Unicode", names[index], first, last));
                return std::nullopt;
            }
            // A descending range is refused rather than read as empty, and it is the one entry here
            // that would be fail-open if it were wrong. The budget stage deliberately skips a range
            // with `last < first` - what an author wrote is a name, and the shape of the table behind
            // it is not theirs to fix - so an inverted range produces *no* charset check at all for a
            // Clock or a TextInput using that rule, while this table is the governed upper bound on
            // what the runtime can put on screen.
            if (first > last) {
                report(diagnostics,
                       Code::RecipeMissingMember,
                       std::string{recipePath},
                       namesLine,
                       std::format("[dynamicText] entry '{}' names the descending range {}..{}", names[index], first, last),
                       "an inverted range checks nothing, so it is refused rather than read as empty");
                return std::nullopt;
            }
            DynamicText rule;
            rule.name = names[index];
            rule.produces.push_back(mdux::font::CharsetRange{.first = static_cast<char32_t>(first), .last = static_cast<char32_t>(last)});
            recipe.dynamicText.push_back(std::move(rule));
        }
    }

    // The product's numeric-template table (#258). Optional as a table for [dynamicText]'s reason: a
    // screen with no `NumericDisplay` names no template, and the budget stage refuses an unknown
    // name rather than accepting one, so an absent table is fail-closed rather than permissive.
    if (const toml::Table* templateTable = document.table("numericTemplates"); templateTable != nullptr) {
        std::vector<std::string> names;
        std::vector<std::string> renderings;
        std::size_t              namesLine = 0;
        try {
            const toml::Value& namesValue = templateTable->require("names");
            namesLine                     = namesValue.line();
            names                         = namesValue.asStringArray();
            renderings                    = templateTable->require("renderings").asStringArray();
        } catch (const toml::TomlError& error) {
            report(diagnostics,
                   Code::RecipeMissingMember,
                   std::string{recipePath},
                   error.line(),
                   error.what(),
                   "[numericTemplates] needs parallel 'names' and 'renderings' arrays");
            return std::nullopt;
        }

        if (names.size() != renderings.size()) {
            report(diagnostics,
                   Code::RecipeMissingMember,
                   std::string{recipePath},
                   namesLine,
                   std::format("[numericTemplates] names has {} entries and renderings {}", names.size(), renderings.size()),
                   "the arrays are positional: entry N of each describes template N");
            return std::nullopt;
        }

        for (std::size_t index = 0; index < names.size(); ++index) {
            // Both bounds are checked here rather than left to the budget stage, so a recipe error
            // names the entry an author wrote instead of the glyph a walk stopped at. An empty
            // rendering is the fail-open one: it measures as zero and would fit any box.
            if (renderings[index].empty()) {
                report(diagnostics,
                       Code::RecipeMissingMember,
                       std::string{recipePath},
                       namesLine,
                       std::format("[numericTemplates] entry '{}' renders as nothing", names[index]),
                       "an empty rendering measures as zero width and would fit any node, so it is refused");
                return std::nullopt;
            }
            if (renderings[index].size() > mdux::medui::maxPatternLength) {
                report(diagnostics,
                       Code::RecipeMissingMember,
                       std::string{recipePath},
                       namesLine,
                       std::format("[numericTemplates] entry '{}' renders as {} characters, and the runtime draws at most {}",
                                   names[index],
                                   renderings[index].size(),
                                   mdux::medui::maxPatternLength),
                       "the cap is mdux::medui::maxPatternLength, which bounds per-node work on a device");
                return std::nullopt;
            }
            // A name is a lookup key, and the budget stage resolves it with a find-first. Two
            // entries sharing one name therefore make the mapping ambiguous in the quietest way
            // available: the second rendering is simply never measured, so an author who edited the
            // wrong duplicate would see a template that compiles and draws the other one's shape.
            // Refused rather than de-duplicated, because which entry was meant is not knowable here.
            const auto duplicate = std::ranges::find(recipe.numericTemplates, names[index], &NumericTemplate::name);
            if (duplicate != recipe.numericTemplates.end()) {
                report(diagnostics,
                       Code::RecipeMissingMember,
                       std::string{recipePath},
                       namesLine,
                       std::format("[numericTemplates] declares '{}' twice, rendering '{}' and '{}'", names[index], duplicate->rendering, renderings[index]),
                       "a template name is a lookup key, so only the first entry would ever be measured");
                return std::nullopt;
            }
            recipe.numericTemplates.push_back(NumericTemplate{.name = names[index], .rendering = renderings[index]});
        }
    }

    return recipe;
}


// ---------------------------------------------------------------------------
// run(): every stage, in the one order this file fixes
// ---------------------------------------------------------------------------

namespace {

/// One approved locale's committed package and the sidecar its runs address.
struct LoadedLocale {
    mdux::text::TextPackage package;
    std::vector<std::byte>  sidecar;
    evidence::Digest        packageSha256{};
};

struct LoadedImage {
    mdux::image::ImagePackage package;
    evidence::Digest          packageSha256{};
};

[[nodiscard]] std::optional<std::vector<LoadedImage>>
loadImages(const Recipe& recipe, const std::filesystem::path& root, std::vector<evidence::FileRecord>& inputs, std::vector<cli::Diagnostic>& diagnostics) {
    std::vector<LoadedImage> images;
    images.reserve(recipe.imagePackages.size());
    for (const std::string& relative : recipe.imagePackages) {
        const auto bytes = readInput(root, relative, diagnostics);
        if (!bytes.has_value()) {
            return std::nullopt;
        }
        const evidence::FileRecord packageRecord = fileRecord(relative, *bytes);
        inputs.push_back(packageRecord);
        auto package = mdux::image::ImagePackage::parse(textOf(*bytes));
        if (!package.has_value()) {
            report(diagnostics,
                   Code::RecipeMissingMember,
                   relative,
                   0,
                   std::format("the image package is not valid: {}", mdux::image::describe(package.error())));
            return std::nullopt;
        }
        const auto canonical = package->write();
        if (!canonical.has_value() || *canonical != textOf(*bytes)) {
            report(diagnostics,
                   Code::RecipeMissingMember,
                   relative,
                   0,
                   "the image package is valid but its package.json is not canonical",
                   "re-bake the image package instead of editing package.json by hand");
            return std::nullopt;
        }
        const std::filesystem::path sidecarRelative = std::filesystem::path{relative}.parent_path() / package->sidecarPath;
        const auto                  sidecar         = readInput(root, sidecarRelative.generic_string(), diagnostics);
        if (!sidecar.has_value()) {
            return std::nullopt;
        }
        inputs.push_back(fileRecord(sidecarRelative.generic_string(), *sidecar));
        if (sidecar->size() != package->sidecarByteLength || evidence::sha256(*sidecar) != package->sidecarSha256) {
            report(diagnostics,
                   Code::RecipeMissingMember,
                   sidecarRelative.generic_string(),
                   0,
                   "the image sidecar does not match its package length and digest");
            return std::nullopt;
        }
        images.push_back(LoadedImage{.package = std::move(*package), .packageSha256 = packageRecord.sha256});
    }
    return images;
}

/// Reads the font package the recipe names, or reports why it could not.
[[nodiscard]] std::optional<mdux::font::FontPackage>
loadFont(const Recipe& recipe, const std::filesystem::path& root, std::vector<evidence::FileRecord>& inputs, std::vector<cli::Diagnostic>& diagnostics) {
    const std::optional<std::vector<std::byte>> bytes = readInput(root, recipe.fontPackage, diagnostics);
    if (!bytes.has_value()) {
        return std::nullopt;
    }
    inputs.push_back(fileRecord(recipe.fontPackage, *bytes));

    auto package = mdux::font::FontPackage::parse(textOf(*bytes));
    if (!package.has_value()) {
        report(diagnostics,
               Code::RecipeMissingMember,
               recipe.fontPackage,
               0,
               std::format("the font package is not valid: {}", mdux::font::describe(package.error())));
        return std::nullopt;
    }
    return std::move(*package);
}

/// Reads every text package the recipe names, with the sidecar each one records.
[[nodiscard]] std::optional<std::vector<LoadedLocale>>
loadLocales(const Recipe& recipe, const std::filesystem::path& root, std::vector<evidence::FileRecord>& inputs, std::vector<cli::Diagnostic>& diagnostics) {
    std::vector<LoadedLocale> locales;
    locales.reserve(recipe.textPackages.size());

    for (const std::string& relative : recipe.textPackages) {
        const std::optional<std::vector<std::byte>> bytes = readInput(root, relative, diagnostics);
        if (!bytes.has_value()) {
            return std::nullopt;
        }
        const evidence::FileRecord packageRecord = fileRecord(relative, *bytes);
        inputs.push_back(packageRecord);

        auto package = mdux::text::TextPackage::parse(textOf(*bytes));
        if (!package.has_value()) {
            report(diagnostics,
                   Code::RecipeMissingMember,
                   relative,
                   0,
                   std::format("the text package is not valid: {}", mdux::text::describe(package.error())));
            return std::nullopt;
        }

        // The device hashes these bytes directly. Accepting a parseable but noncanonical file here
        // would record one digest while the in-memory package denotes the canonical form of another,
        // leaving a package that compiles successfully and can never bind. Diagnose that authoring
        // error on the host, where rebaking it is actionable.
        const auto canonical = package->write();
        if (!canonical.has_value() || *canonical != textOf(*bytes)) {
            report(diagnostics,
                   Code::RecipeMissingMember,
                   relative,
                   0,
                   "the text package is valid but its package.json is not canonical",
                   "re-bake the text package instead of editing package.json by hand");
            return std::nullopt;
        }

        // The sidecar sits beside the package, under the name the package itself records - the same
        // arrangement every other artifact kind uses, so a caller never guesses a filename.
        const std::filesystem::path                 sidecarRelative = std::filesystem::path{relative}.parent_path() / package->sidecarPath;
        const std::optional<std::vector<std::byte>> sidecar         = readInput(root, sidecarRelative.generic_string(), diagnostics);
        if (!sidecar.has_value()) {
            return std::nullopt;
        }
        inputs.push_back(fileRecord(sidecarRelative.generic_string(), *sidecar));

        locales.push_back(LoadedLocale{.package = std::move(*package), .sidecar = *sidecar, .packageSha256 = packageRecord.sha256});
    }
    return locales;
}

/**
 * @brief The cross-package invariants `checkTextBudgets()` would otherwise throw over.
 *
 * Its contract is that `locales` is *exactly* the set the font approves - one entry per tag, none
 * missing, none repeated, none outside it - and that every package addresses that font's atlas. Each
 * of those is decided by paths an author typed into a recipe, so each is a diagnostic here rather
 * than a `std::logic_error` from a library whose caller is supposed to be code.
 *
 * The omission is the dangerous one and the reason the library refuses it: a budget is a claim about
 * the worst approved translation, so a compile checked against only the locale someone happened to
 * list would certify a screen against a set nobody approved - and the missing German or Finnish
 * string is exactly the one that overflows the box.
 */
[[nodiscard]] bool checkLocaleWiring(const mdux::font::FontPackage& font,
                                     std::span<const LoadedLocale>  locales,
                                     std::string_view               recipePath,
                                     std::vector<cli::Diagnostic>&  diagnostics) {
    if (locales.empty()) {
        report(diagnostics,
               Code::RecipeMissingMember,
               std::string{recipePath},
               0,
               "this screen draws text and the recipe lists no text package",
               "[text] needs one committed package per locale the font approves");
        return false;
    }

    std::vector<std::string_view> seen;
    seen.reserve(locales.size());
    for (const LoadedLocale& locale : locales) {
        if (locale.package.atlasId != font.id) {
            report(diagnostics,
                   Code::RecipeMissingMember,
                   std::string{recipePath},
                   0,
                   std::format("the text package for '{}' was baked against the font '{}', and the recipe names '{}'",
                               locale.package.locale,
                               locale.package.atlasId,
                               font.id),
                   "a package measured against one atlas cannot be checked against another");
            return false;
        }
        if (std::ranges::find(seen, std::string_view{locale.package.locale}) != seen.end()) {
            report(diagnostics, Code::RecipeMissingMember, std::string{recipePath}, 0, std::format("the locale '{}' is listed twice", locale.package.locale));
            return false;
        }
        if (std::ranges::find(font.locales, locale.package.locale) == font.locales.end()) {
            report(diagnostics,
                   Code::RecipeMissingMember,
                   std::string{recipePath},
                   0,
                   std::format("the locale '{}' is not one the font package approves", locale.package.locale));
            return false;
        }
        seen.push_back(locale.package.locale);
    }

    for (const std::string& approved : font.locales) {
        if (std::ranges::find(seen, std::string_view{approved}) == seen.end()) {
            report(diagnostics,
                   Code::RecipeMissingMember,
                   std::string{recipePath},
                   0,
                   std::format("the font approves '{}' and the recipe lists no text package for it", approved),
                   "a budget checked against fewer locales than were approved is a claim nobody made");
            return false;
        }
    }

    return true;
}

}  // namespace

std::optional<CompileOutputs> run(const Recipe&                 recipe,
                                  std::string_view              recipePath,
                                  std::span<const std::byte>    recipeBytes,
                                  const std::filesystem::path&  root,
                                  std::vector<cli::Diagnostic>& diagnostics) {
    std::vector<evidence::FileRecord> inputs;

    const std::optional<std::vector<std::byte>> sourceBytes = readFile(root / recipe.source);
    if (!sourceBytes.has_value()) {
        report(diagnostics, Code::SourceUnreadable, recipe.source, 0, "the .medui source could not be read");
        return std::nullopt;
    }
    inputs.push_back(fileRecord(recipe.source, *sourceBytes));

    // 1. Parse. The lexer decides UTF-8 validity, so MEDUI-E004 comes from there rather than from a
    //    second check here that could disagree with it.
    ParseResult parsed = parse(textOf(*sourceBytes), recipe.source);
    if (!parsed.diagnostics.empty() || !parsed.screen.has_value()) {
        diagnostics.insert(diagnostics.end(), parsed.diagnostics.begin(), parsed.diagnostics.end());
        // Guarded on the parser's own diagnostics, not on the caller's vector: `diagnostics` is an
        // out-parameter a caller may hand over already populated, and then a parse that produced
        // neither a screen nor a reason would have returned nullopt with nothing of its own to say.
        if (parsed.diagnostics.empty()) {
            report(diagnostics, Code::SourceUnreadable, recipe.source, 0, "the source produced no screen and no diagnostic");
        }
        return std::nullopt;
    }
    const ast::Screen& screen = *parsed.screen;

    // The slug and the screen name have to be the same screen. Without this the registration can
    // put outputs in `generated/screen/foo/` while the package inside calls itself `bar`, and every
    // later consumer - the evidence test, the emitted identifier, a requirement trace - follows one
    // of the two names with nothing to say the other exists.
    if (reduced(recipe.id) != reduced(screen.name)) {
        report(diagnostics,
               Code::RecipeMissingMember,
               std::string{recipePath},
               0,
               std::format("the recipe compiles 'Screen {}' as the artifact '{}', which is a different name", screen.name, recipe.id),
               "the slug is the screen's name in lowercase; hyphenation is free, the word is not");
        return std::nullopt;
    }

    // The recipe has to be complete for *this* screen. Asked of the budget stage's own module rather
    // than answered here, so the two cannot disagree about what counts as text.
    const bool measurable = needsTextBudget(screen);
    if (measurable && recipe.fontPackage.empty()) {
        report(diagnostics,
               Code::RecipeMissingMember,
               std::string{recipePath},
               0,
               "this screen draws text, so the recipe needs a [text] table naming the font package and one text package per "
               "approved locale",
               "a screen that draws text and declares no approved locale would be certified against a set nobody approved");
        return std::nullopt;
    }
    if (!measurable && (!recipe.fontPackage.empty() || !recipe.textPackages.empty())) {
        report(diagnostics,
               Code::RecipeMissingMember,
               std::string{recipePath},
               0,
               "this screen carries no measurable text, so its recipe must not declare a [text] table",
               "remove the unused font and locale packages; otherwise the report would name inputs the compiler never authenticated");
        return std::nullopt;
    }

    std::optional<mdux::font::FontPackage> font;
    std::vector<LoadedLocale>              locales;
    if (measurable) {
        font = loadFont(recipe, root, inputs, diagnostics);
        if (!font.has_value()) {
            return std::nullopt;
        }
        std::optional<std::vector<LoadedLocale>> loaded = loadLocales(recipe, root, inputs, diagnostics);
        if (!loaded.has_value()) {
            return std::nullopt;
        }
        locales = std::move(*loaded);

        // `checkTextBudgets()` classifies every one of these as a *caller wiring* error and throws
        // `std::logic_error`, which is right for a library whose caller is code. Here the caller is
        // a recipe an author wrote, so each one is diagnosed instead: a mistyped path or a forgotten
        // locale must produce a report, not terminate the compiler.
        if (!checkLocaleWiring(*font, locales, recipePath, diagnostics)) {
            return std::nullopt;
        }
    }

    std::vector<mdux::text::TextPackage> textPackages;
    textPackages.reserve(locales.size());
    for (const LoadedLocale& locale : locales) {
        textPackages.push_back(locale.package);
    }

    std::vector<ms::TextPackageApproval> approvals;
    approvals.reserve(locales.size());
    for (const LoadedLocale& locale : locales) {
        approvals.push_back(
            ms::TextPackageApproval{.locale = locale.package.locale, .packageId = locale.package.header.id, .packageSha256 = locale.packageSha256});
    }

    const auto loadedImages = loadImages(recipe, root, inputs, diagnostics);
    if (!loadedImages.has_value()) {
        return std::nullopt;
    }
    if (loadedImages->size() > 1) {
        report(diagnostics,
               Code::RecipeMissingMember,
               std::string{recipePath},
               0,
               "S1 supports one RGBA image package per screen",
               "compose artwork into one QOI atlas, or wait for the multi-image atlas follow-up");
        return std::nullopt;
    }
    std::vector<ms::ImagePackageApproval> imageApprovals;
    imageApprovals.reserve(loadedImages->size());
    for (const LoadedImage& image : *loadedImages) {
        if (std::ranges::find_if(imageApprovals,
                                 [&](const ms::ImagePackageApproval& approval) {
                                     return approval.packageId == image.package.header.id;
                                 })
            != imageApprovals.end()) {
            report(diagnostics,
                   Code::RecipeMissingMember,
                   std::string{recipePath},
                   0,
                   std::format("the image package '{}' is listed twice", image.package.header.id));
            return std::nullopt;
        }
        imageApprovals.push_back(ms::ImagePackageApproval{.packageId     = image.package.header.id,
                                                          .packageSha256 = image.packageSha256,
                                                          .width         = image.package.width,
                                                          .height        = image.package.height});
    }

    // 2. Semantic analysis. The theme tokens are the governed table the schema publishes, not a
    //    second list: `resolveColorToken()` on a device and this check on the host then agree by
    //    construction rather than by review.
    std::vector<std::string_view> themeTokens;
    themeTokens.reserve(ms::themeColors.size());
    for (const ms::ThemeColor& colour : ms::themeColors) {
        themeTokens.push_back(colour.token);
    }

    const SemanticResult semantic = analyze(screen, recipe.source, {.themeTokens = themeTokens, .textPackages = textPackages});
    if (!semantic.ok()) {
        diagnostics.insert(diagnostics.end(), semantic.diagnostics.begin(), semantic.diagnostics.end());
        return std::nullopt;
    }

    // 3. Annotations, before layout: the shared conformance suite pins MEDUI-E070 on a screen with
    //    no `surface:`, which the solver cannot resolve at all.
    const SafetyResult safety = validateSafetyAnnotations(screen, recipe.source);
    if (!safety.ok()) {
        diagnostics.insert(diagnostics.end(), safety.diagnostics.begin(), safety.diagnostics.end());
        return std::nullopt;
    }

    // 4. Layout, against the surface the recipe declares. The solver checks the source's own
    //    `surface:` against it, so a screen drawn for another panel is a diagnostic rather than a
    //    silently rescaled frame.
    const LayoutResult layout = resolveLayout(screen, recipe.source, {.surfaceWidth = recipe.surfaceWidth, .surfaceHeight = recipe.surfaceHeight});
    if (!layout.ok()) {
        diagnostics.insert(diagnostics.end(), layout.diagnostics.begin(), layout.diagnostics.end());
        return std::nullopt;
    }

    for (const ResolvedNode& node : layout.nodes) {
        if (node.component != "Image") {
            continue;
        }
        const auto sourceField = std::ranges::find_if(node.source.fields, [](const ast::Field& field) {
            return field.name == "source";
        });
        if (sourceField == node.source.fields.end() || sourceField->value == nullptr) {
            report(diagnostics, Code::RecipeMissingMember, std::string{recipePath}, 0, std::format("Image '{}' reached layout without a source", node.id));
            return std::nullopt;
        }
        const std::string_view source   = sourceField->value->text;
        const auto             approval = std::ranges::find_if(imageApprovals, [&](const ms::ImagePackageApproval& candidate) {
            return candidate.packageId == source;
        });
        if (approval == imageApprovals.end()) {
            report(diagnostics,
                   Code::RecipeMissingMember,
                   std::string{recipePath},
                   0,
                   std::format("Image '{}' names '{}', which [images] does not approve", node.id, source));
            return std::nullopt;
        }
        if (node.bounds.width != static_cast<std::int64_t>(approval->width) || node.bounds.height != static_cast<std::int64_t>(approval->height)) {
            report(diagnostics,
                   Code::RecipeMissingMember,
                   std::string{recipePath},
                   0,
                   std::format("Image '{}' resolves to {}x{}, but package '{}' is intrinsically {}x{}",
                               node.id,
                               node.bounds.width,
                               node.bounds.height,
                               source,
                               approval->width,
                               approval->height),
                   "export the QOI at the exact component size; S1 performs no runtime scaling");
            return std::nullopt;
        }
    }
    const bool hasImage = std::ranges::any_of(layout.nodes, [](const ResolvedNode& node) {
        return node.component == "Image";
    });
    if (!hasImage && !imageApprovals.empty()) {
        report(diagnostics,
               Code::RecipeMissingMember,
               std::string{recipePath},
               0,
               "this screen carries no Image, so its recipe must not declare an [images] table");
        return std::nullopt;
    }

    // 5. Text budgets, when there is anything to measure.
    if (measurable) {
        std::vector<LocaleText> localeTexts;
        localeTexts.reserve(locales.size());
        for (const LoadedLocale& locale : locales) {
            localeTexts.push_back(LocaleText{.package = &locale.package, .sidecar = locale.sidecar});
        }

        std::vector<DynamicTextRule> dynamicRules;
        dynamicRules.reserve(recipe.dynamicText.size());
        for (const DynamicText& rule : recipe.dynamicText) {
            dynamicRules.push_back(DynamicTextRule{.name = rule.name, .produces = rule.produces});
        }

        std::vector<NumericTemplateRule> templateRules;
        templateRules.reserve(recipe.numericTemplates.size());
        for (const NumericTemplate& rule : recipe.numericTemplates) {
            templateRules.push_back(NumericTemplateRule{.name = rule.name, .rendering = rule.rendering});
        }

        const TextBudgetResult budgets =
            checkTextBudgets(layout, recipe.source, {.font = &*font, .locales = localeTexts, .dynamicText = dynamicRules, .numericTemplates = templateRules});
        if (!budgets.diagnostics.empty()) {
            diagnostics.insert(diagnostics.end(), budgets.diagnostics.begin(), budgets.diagnostics.end());
            return std::nullopt;
        }
    }

    // The budget the recipe declared has to be usable for the screen that came out of the solver.
    // `ScreenPackage::validate()` refuses an empty budget on a screen with nodes - the first
    // rectangle would be turned away and the frame would silently be blank - and `buildPackage()`
    // treats a schema failure as a compiler bug and throws. So the recipe is told here, where it can
    // still be corrected.
    if (!layout.nodes.empty() && (recipe.budget.maxVertices == 0 || recipe.budget.maxIndices == 0 || recipe.budget.maxCommands == 0)) {
        report(diagnostics,
               Code::RecipeMissingMember,
               std::string{recipePath},
               0,
               std::format("the budget is empty and this screen resolves to {} nodes", layout.nodes.size()),
               "a screen with nodes needs room for at least one rectangle, or every draw call it makes is refused");
        return std::nullopt;
    }

    // 6. Goldens, from the resolved screen. Called rather than re-derived: ADR-012 decision 4 makes
    //    one implementation of the predicate, applied once, the thing that guarantees completeness.
    const std::vector<GoldenReference> goldens = collectGoldens(layout);

    // 7. The compiled screen and its bytes.
    const ScreenDocument document = buildPackage(
        layout,
        {.id = recipe.id, .budget = recipe.budget, .approvedTextPackages = approvals, .approvedImagePackages = imageApprovals});
    const ms::ScreenPackage package = document.package();

    CompileOutputs outputs;
    outputs.packageJson = writePackage(package);
    outputs.goldensJson = writeGoldens(goldens);
    outputs.screenId    = recipe.id;
    outputs.nodeCount   = package.nodes.size();
    outputs.goldenCount = goldens.size();

    evidence::BakeReport bakeReport;
    bakeReport.tool        = std::string{compilerToolName};
    bakeReport.toolVersion = MDUX_TOOL_VERSION;
    bakeReport.recipe      = fileRecord(std::string{recipePath}, recipeBytes);
    bakeReport.inputs      = std::move(inputs);
    bakeReport.options     = recipe.toOptions();
    // report.json is deliberately absent from its own outputs: a file cannot carry its own digest,
    // for the same reason ADR-007 gives for there being no commit SHA in one.
    bakeReport.outputs = {fileRecord("goldens.json", asBytes(outputs.goldensJson)), fileRecord("package.json", asBytes(outputs.packageJson))};

    auto reportText = bakeReport.write();
    if (!reportText.has_value()) {
        report(diagnostics,
               Code::RecipeMissingMember,
               std::string{recipePath},
               0,
               std::format("the bake report is not valid: {}", evidence::describe(reportText.error())));
        return std::nullopt;
    }
    outputs.reportJson = std::move(*reportText);

    return outputs;
}

// ---------------------------------------------------------------------------
// write() / verify()
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] bool writeText(const std::filesystem::path& path, const std::string& text, std::vector<cli::Diagnostic>& diagnostics) {
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    if (!file) {
        report(diagnostics, Code::SourceUnreadable, displayPath(path), 0, "cannot open for writing");
        return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) {
        report(diagnostics, Code::SourceUnreadable, displayPath(path), 0, "write failed");
        return false;
    }
    return true;
}

[[nodiscard]] bool matches(const std::filesystem::path& path, const std::string& expected, std::vector<cli::Diagnostic>& diagnostics) {
    const std::optional<std::vector<std::byte>> committed = readFile(path);
    if (!committed.has_value()) {
        report(diagnostics, Code::SourceUnreadable, displayPath(path), 0, "the committed artifact could not be read");
        return false;
    }
    if (textOf(*committed) != expected) {
        report(diagnostics,
               Code::RecipeMissingMember,
               displayPath(path),
               0,
               "the committed artifact differs from what this compiler produces",
               "run `cmake --build <dir> --target mdux-bake-update` and review the diff; do not hand-edit generated/");
        return false;
    }
    return true;
}

}  // namespace

bool write(const CompileOutputs& outputs, const std::filesystem::path& outputDir, std::vector<cli::Diagnostic>& diagnostics) {
    std::error_code code;
    std::filesystem::create_directories(outputDir, code);
    if (code) {
        report(diagnostics, Code::SourceUnreadable, displayPath(outputDir), 0, std::format("cannot create the output directory: {}", code.message()));
        return false;
    }

    // All three, unconditionally. ADR-012 declares each an add_custom_command OUTPUT, so a compiler
    // that skipped `goldens.json` for a screen with nothing to pin would break the build rather than
    // produce a smaller artifact.
    bool ok = writeText(outputDir / "package.json", outputs.packageJson, diagnostics);
    ok      = writeText(outputDir / "goldens.json", outputs.goldensJson, diagnostics) && ok;
    ok      = writeText(outputDir / "report.json", outputs.reportJson, diagnostics) && ok;
    return ok;
}

bool verify(const CompileOutputs&         outputs,
            const std::filesystem::path&  packagePath,
            const std::filesystem::path&  goldensPath,
            const std::filesystem::path&  reportPath,
            std::vector<cli::Diagnostic>& diagnostics) {
    bool ok = matches(packagePath, outputs.packageJson, diagnostics);
    ok      = matches(goldensPath, outputs.goldensJson, diagnostics) && ok;
    ok      = matches(reportPath, outputs.reportJson, diagnostics) && ok;
    return ok;
}

}  // namespace mdux::tools::medui
