/**
 * @file Semantic.cppm
 * @brief Build-time semantic analysis for parsed `.medui` screens.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * This stage validates names but deliberately does not substitute them. The AST remains the
 * author's unresolved description; later compiler stages receive it only after this result is
 * successful and perform layout and emission against their own governed inputs.
 */
module;

export module mdux.tools.medui.semantic;

import std;
import mdux.text.schema;
import mdux.tools.cli;
import mdux.tools.medui.ast;

export namespace mdux::tools::medui {

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
};

struct SemanticResult {
    std::vector<mdux::tools::cli::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept {
        return diagnostics.empty();
    }
};

/**
 * @brief Validates component and field names, theme tokens, and locale-complete text keys.
 *
 * Diagnostics accumulate in source traversal order. The screen is never modified and resolved
 * values are not returned: successful name validation is a gate, not a substitution pass.
 */
[[nodiscard]] SemanticResult analyze(const ast::Screen& screen, std::string file, SemanticInputs inputs);

}  // namespace mdux::tools::medui
