/**
 * @file Compile.cppm
 * @brief The `.medui` compiler driver: a recipe in, three committed artifacts out.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-005 Error handling and exceptions policy (host tools may throw)
 * @compliance ADR-007 Evidence pipeline doctrine (canonical form, byte-identity, bake reports)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * Every stage this epic built exists as a call. This is the file that says in what order they run,
 * and it is the only place that answer is written down - a driver that let each caller choose would
 * make the order a convention rather than a contract.
 *
 * ## The order, and why it is this one
 *
 * 1. **parse** - structure the grammar fixes, nothing the dictionary does (#192).
 * 2. **analyze** - components, field domains, theme tokens, and keys against *every* approved
 *    locale (#193).
 * 3. **validate annotations** - `@safety_critical` needs a requirement, and a `cv_check` must name a
 *    verification that exists (#196). Before layout, deliberately: the shared conformance suite pins
 *    `MEDUI-E070` on a screen with no `surface:`, which the solver cannot resolve at all, so a
 *    design that could only report it after layout could not satisfy the contract it implements.
 * 4. **resolve layout** - integer-only, bounded, one absolute rectangle per node (#194).
 * 5. **check text budgets** - every resolved box against the widest approved translation (#195).
 * 6. **collect goldens** - where safety-critical content must appear (#196).
 * 7. **build and write** - the compiled screen, its sidecar and the bake report (#197).
 *
 * A stage that reports anything stops the compile. Diagnostics are not accumulated across stages,
 * because a later stage reading a screen an earlier one rejected reports consequences rather than
 * causes - the layout solver on an unresolvable surface being the clearest case.
 *
 * ## The budget stage runs when there is anything to measure
 *
 * `checkTextBudgets()` requires a font package and *exactly* the locales that font approves, so a
 * recipe that declares neither cannot run it. That is not a hole: a screen carrying any `t("KEY")`
 * with no approved locale to check it against is already refused by stage 2, which validates every
 * key against every declared locale and finds none. So the stage is skipped only for a screen that
 * draws no text at all, and a screen that draws text cannot reach stage 5 without the inputs.
 *
 * ## What the recipe carries, and why nothing defaults
 *
 * Every knob is required. `MEDUI-E002` says why in the registry: a defaulted knob does not appear in
 * `report.json`, so a silently changed default would leave every report looking unchanged - which
 * ADR-007 forbids. The surface is declared by the recipe rather than taken from the source, and the
 * solver checks the source's own `surface:` against it, so the panel a product ships and the screen
 * an author drew for it disagree loudly rather than silently.
 */
module;

export module mdux.tools.medui.compile;

import std;
import mdux.draw;
import mdux.evidence.json;
import mdux.font.schema;
import mdux.tools.cli;

export namespace mdux::tools::medui {

/// The tool name that appears in every diagnostic and in `report.json`.
inline constexpr std::string_view compilerToolName = "mdux-meduic";

/**
 * @brief One named dynamic-text source from the recipe, and every code point it can produce.
 *
 * The governed table an author's `format: TimeSeconds` or `charset: Ascii` resolves against. It is a
 * recipe input rather than a compiler constant for the reason `DynamicTextRule` gives: the names
 * belong to a product's governed tables, and a compiler shipping its own list would be authoritative
 * about a set it does not own.
 *
 * Spelled as parallel arrays because `mdux.tools.toml` implements a subset with no array-of-tables
 * support - the same shape `recipes/font/dejavu-ui.toml` uses for its charset, and the same
 * length-mismatch check.
 *
 * Owning its ranges rather than viewing them: a `DynamicTextRule` is a view, and the storage it
 * views has to outlive the compile that uses it.
 */
struct DynamicText {
    std::string                           name;
    std::vector<mdux::font::CharsetRange> produces;
};

/**
 * @brief A parsed and resolved screen recipe.
 *
 * Paths are repository-relative, because the compiler runs with the repository root as its working
 * directory and `BakeReport::validate()` rejects an absolute path - a report naming a path that
 * exists on one machine is not evidence.
 *
 * `textPackages` is one committed text package per approved locale, and `fontPackage` the font they
 * were baked into. Both are empty for a screen that draws no text; see the module comment for why
 * that is safe rather than a way around the check.
 */
struct Recipe {
    std::string              id;      ///< the artifact slug: `generated/screen/<id>/`
    std::string              source;  ///< the `.medui` file
    std::int64_t             surfaceWidth{0};
    std::int64_t             surfaceHeight{0};
    mdux::draw::DrawBudget   budget{};
    std::string              fontPackage;   ///< committed font package.json, or empty
    std::vector<std::string> textPackages;  ///< committed text package.json, one per approved locale
    std::vector<DynamicText> dynamicText;   ///< the product's governed dynamic-text table

    /// The fully resolved options, as `report.json` records them.
    [[nodiscard]] evidence::json::Value toOptions() const;
};

/**
 * @brief Everything a compile produces, held in memory so bake and verify share one code path.
 *
 * Bytes and two counts, not the structured screen: a caller wanting the package parses the JSON,
 * which exercises the reader on the writer's own output rather than inspecting a value that never
 * made the round trip. `mdux.tools.shaderbake` gives the same reasoning at greater length, plus the
 * GCC 15 `std::optional` constraint that makes carrying a module type here a hazard.
 */
struct CompileOutputs {
    std::string packageJson;  ///< canonical `package.json` text
    std::string goldensJson;  ///< canonical `goldens.json` text, `[]` when nothing is pinned
    std::string reportJson;   ///< canonical `report.json` text
    std::string screenId;     ///< for the summary line
    std::size_t nodeCount{0};
    std::size_t goldenCount{0};
};

/// Reads a file as bytes. Returns nullopt when it cannot be opened or read.
[[nodiscard]] std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path);

/// Parses recipe text. Diagnostics are appended; nullopt means it did not parse.
[[nodiscard]] std::optional<Recipe> parseRecipe(std::string_view text, std::string_view recipePath, std::vector<cli::Diagnostic>& diagnostics);

/**
 * @brief Runs every compiler stage over the screen the recipe names.
 *
 * @param recipe      the resolved recipe
 * @param recipePath  repository-relative, for `report.json`'s recipe record and for diagnostics
 * @param recipeBytes the recipe's own bytes, for its digest
 * @param root        the directory the recipe's paths resolve against - the repository root
 * @param diagnostics appended to; a stage that reports anything stops the compile
 *
 * Returns nullopt when any stage rejects the screen, when an input cannot be read, or when the
 * compiled screen fails its own schema.
 */
[[nodiscard]] std::optional<CompileOutputs> run(const Recipe&                 recipe,
                                                std::string_view              recipePath,
                                                std::span<const std::byte>    recipeBytes,
                                                const std::filesystem::path&  root,
                                                std::vector<cli::Diagnostic>& diagnostics);

/// Writes `outputs` into `outputDir`, creating it if needed. All three files, always: ADR-012 makes
/// them unconditional outputs, so "this screen pins nothing" is an empty array rather than a missing
/// file.
[[nodiscard]] bool write(const CompileOutputs& outputs, const std::filesystem::path& outputDir, std::vector<cli::Diagnostic>& diagnostics);

/// Compares `outputs` against the committed files, appending a diagnostic per mismatch.
[[nodiscard]] bool verify(const CompileOutputs&         outputs,
                          const std::filesystem::path&  packagePath,
                          const std::filesystem::path&  goldensPath,
                          const std::filesystem::path&  reportPath,
                          std::vector<cli::Diagnostic>& diagnostics);

}  // namespace mdux::tools::medui
