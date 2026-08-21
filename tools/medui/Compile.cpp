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

    Value options = Value::emptyObject();
    put(options, "budget", std::move(budgetValue));
    put(options, "dynamicText", Value::array(std::move(dynamic)));
    put(options, "fontPackage", Value::string(fontPackage));
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
        const std::int64_t vertices = budget->require("maxVertices").asInteger();
        const std::int64_t indices  = budget->require("maxIndices").asInteger();
        const std::int64_t commands = budget->require("maxCommands").asInteger();
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

    // The governed dynamic-text table. Optional as a table for the same reason [text] is: a screen
    // with no `format:` and no `charset:` has no name to resolve, and the budget stage refuses an
    // unknown name rather than accepting one, so an absent table is fail-closed.
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
            DynamicText rule;
            rule.name = names[index];
            rule.produces.push_back(mdux::font::CharsetRange{.first = static_cast<char32_t>(first), .last = static_cast<char32_t>(last)});
            recipe.dynamicText.push_back(std::move(rule));
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
};

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
        inputs.push_back(fileRecord(relative, *bytes));

        auto package = mdux::text::TextPackage::parse(textOf(*bytes));
        if (!package.has_value()) {
            report(diagnostics,
                   Code::RecipeMissingMember,
                   relative,
                   0,
                   std::format("the text package is not valid: {}", mdux::text::describe(package.error())));
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

        locales.push_back(LoadedLocale{.package = std::move(*package), .sidecar = *sidecar});
    }
    return locales;
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
        if (diagnostics.empty()) {
            report(diagnostics, Code::SourceUnreadable, recipe.source, 0, "the source produced no screen and no diagnostic");
        }
        return std::nullopt;
    }
    const ast::Screen& screen = *parsed.screen;

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
    }

    std::vector<mdux::text::TextPackage> textPackages;
    textPackages.reserve(locales.size());
    for (const LoadedLocale& locale : locales) {
        textPackages.push_back(locale.package);
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

        const TextBudgetResult budgets = checkTextBudgets(layout, recipe.source, {.font = &*font, .locales = localeTexts, .dynamicText = dynamicRules});
        if (!budgets.diagnostics.empty()) {
            diagnostics.insert(diagnostics.end(), budgets.diagnostics.begin(), budgets.diagnostics.end());
            return std::nullopt;
        }
    }

    // 6. Goldens, from the resolved screen. Called rather than re-derived: ADR-012 decision 4 makes
    //    one implementation of the predicate, applied once, the thing that guarantees completeness.
    const std::vector<GoldenReference> goldens = collectGoldens(layout);

    // 7. The compiled screen and its bytes.
    const ScreenDocument    document = buildPackage(layout, {.id = recipe.id, .budget = recipe.budget});
    const ms::ScreenPackage package  = document.package();

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
