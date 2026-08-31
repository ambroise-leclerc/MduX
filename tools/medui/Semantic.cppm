/**
 * @file Semantic.cppm
 * @brief Build-time semantic analysis for parsed `.medui` screens.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * This stage validates the component dictionary, field value domains, and external names but
 * deliberately does not substitute them. The AST remains the author's unresolved description;
 * later compiler stages receive it only after this result is successful and perform layout and
 * emission against their own governed inputs.
 */
module;

export module mdux.tools.medui.semantic;

import std;
import mdux.text.schema;
import mdux.tools.cli;
import mdux.tools.medui.ast;

export namespace mdux::tools::medui {

/// The syntactic value form one component field admits during semantic analysis.
enum class FieldDomain : std::uint8_t {
    Identifier,
    Size,
    Point,
    String,
    TextKey,
    TextKeyList,
    ColorToken,
    ColorTokenList,
    ImageRef,
    Number,
    /// An identifier that must also be a member of a closed set the shared contract enumerates.
    /// Kind and membership are separate failures: a wrong kind is `MEDUI-E033`, a non-member
    /// `MEDUI-E034`, and an author fixes them differently.
    ClockFormatName,
    SystemEventName,
};

/// One field admitted by a component and the value form that field accepts.
struct FieldRule {
    /// Field spelling from the canonical component dictionary.
    std::string_view name;
    /// Whether every instance of the component must carry this field.
    bool required{false};
    /// Syntactic value form accepted for this field.
    FieldDomain domain{FieldDomain::Identifier};
};

/// One component and its complete closed set of fields.
struct ComponentRule {
    /// Component spelling from the canonical component dictionary.
    std::string_view name;
    /// Complete required and optional field set for the component.
    std::span<const FieldRule> fields;
};

/**
 * @brief The closed component dictionary implemented by this semantic stage.
 *
 * Exposed so the pinned shared-conformance test can compare the transcription with the contract
 * it implements. Consumers should still treat Compliatory/MedUI as the canonical definition.
 */
[[nodiscard]] std::span<const ComponentRule> componentDictionary() noexcept;

/**
 * @brief Whether a caller is entitled to skip the locale-completeness check.
 *
 * The default is the one that fails closed. An empty `textPackages` span under `Required` means an
 * approved set containing no locale, so every key is absent from all of them - which is what a
 * compile must see rather than a clean result.
 *
 * `Skipped` exists for one caller: `mdux-medui-check` validates a file that may belong to no recipe,
 * so there are no approved locales to check against and reporting every `t("STR-KEY")` as absent
 * would be a vacuous truth dressed as a finding. It is a *mode a caller asks for*, not a meaning
 * read into an empty span, because the second kind would hand every present and future caller a
 * silently weaker check with nothing in the result to say so.
 */
enum class LocalePolicy : std::uint8_t {
    Required,  ///< keys must resolve in every approved locale; no locales means none resolve
    Skipped,   ///< the caller has no approved locale set and accepts that keys go unchecked
};

/**
 * @brief External name tables against which one screen is checked.
 *
 * Text packages are prevalidated, one per approved locale, with unique locale names. The
 * analyzer reads only each package's locale and run IDs. Theme tokens are full names such as
 * `Theme.Colors.Title`; their concrete colour values belong to the later emitter stage.
 */
struct SemanticInputs {
    std::span<const std::string_view>        themeTokens;
    std::span<const mdux::text::TextPackage> textPackages;
    LocalePolicy                             locales{LocalePolicy::Required};
};

/// Accumulated semantic diagnostics; an empty result admits the screen to the next stage.
struct SemanticResult {
    std::vector<mdux::tools::cli::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept {
        return diagnostics.empty();
    }
};

/**
 * @brief Validates component fields and values, theme tokens, and locale-complete text keys.
 *
 * Diagnostics accumulate in source traversal order. The screen is never modified and resolved
 * values are not returned: successful semantic validation is a gate, not a substitution pass.
 */
[[nodiscard]] SemanticResult analyze(const ast::Screen& screen, std::string file, SemanticInputs inputs);

}  // namespace mdux::tools::medui
