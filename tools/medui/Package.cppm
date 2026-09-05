/**
 * @file Package.cppm
 * @brief The compiled screen as a document a host tool owns, and as the canonical JSON bytes that
 *        document is committed as.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine (canonical form, byte-identity)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * Host-only, and the stage where the compiler's own types stop and the shared ones begin. Every
 * earlier stage works on the AST and the layout: types that own their strings, carry positions, and
 * exist to produce diagnostics. `mdux::medui::ScreenPackage` is none of those things - it is what a
 * device holds, non-owning and `constexpr`-constructible, so that a generated translation unit can
 * place one in read-only memory. This module is the join.
 *
 * ## Why a document, and not just a package
 *
 * `ScreenPackage` is a view: `std::string_view` names and `std::span` nodes, pointing at storage
 * somebody else owns. That is exactly right for generated code, where the storage is namespace-scope
 * `constexpr` data with static lifetime, and unusable on the host, where the strings are built at run
 * time from a file.
 *
 * `ScreenDocument` is that missing owner. It holds the text and the nodes, and hands out a
 * `ScreenPackage` viewing them - so the host compiler and the device runtime share **one** definition
 * of what a compiled screen is, which is the property #197 exists to deliver. The alternative, a
 * second host-side screen type with owning members, is two definitions that agree until they do not.
 *
 * Text is interned in a `std::deque<std::string>` rather than a `std::vector<std::string>`, and the
 * reason is not style. A `std::string` short enough for the small-string optimisation stores its
 * characters inside the object, so moving that object moves the characters and every `string_view`
 * onto them dangles. A deque never relocates an element it already holds, and the container
 * requirements keep references to its elements valid across both the deque's move construction and
 * its move assignment - the latter because `std::allocator` propagates on move assignment, which
 * makes that operation an adoption of the existing storage rather than a relocation of elements. So
 * a document may be returned by value, which every function here does, and move-assigned into a
 * result, which `readPackage()` does, without invalidating the package it hands out.
 *
 * That is the whole justification for the defaulted move operations on a type full of views, so it
 * is asserted rather than argued: a scenario moves a document both ways and reads every name back
 * through the package afterwards.
 *
 * Copying is deleted for the same reason, stated as a rule rather than left to be discovered: a
 * copied document would hold copied strings while its nodes still viewed the original's. There is no
 * correct memberwise copy, so there is none.
 *
 * ## What the artifact contains, and what it deliberately does not
 *
 * `package.json` carries the compiled nodes, their absolute rectangles, the draw budget, and the
 * validated names each node draws with, plus the locale/package/digest approval records the runtime
 * uses to authenticate a binding. It carries no selected locale, no resolved colour, no glyph run
 * and no vertex - ADR-011 fixes that boundary and ADR-012 explains it. One consequence is worth naming
 * because it is unusual for this repository: **a screen package contains no floating-point number at
 * all**, so the `{"bits": N}` encoding that ADR-007 requires for a float never appears in one. Every
 * number here is an integer the layout solver computed in integer arithmetic.
 *
 * ## The JSON shape
 *
 * Members are camelCase, as ADR-012 decision 1 fixes for all three files in a screen's directory.
 * A node names its component in `kind` and carries that component's own fields in `spec`:
 *
 * ```json
 * { "bounds": { "height": 60, "width": 120, "x": 250, "y": 60 },
 *   "id": "score",
 *   "kind": "NumericDisplay",
 *   "spec": { "colorToken": "Theme.Colors.ScoreDigits",
 *             "requirement": "REQ-NS-001",
 *             "source": "SEDATION_INDEX",
 *             "templateId": "TPL-SEDATION-INDEX-160" } }
 * ```
 *
 * One member name for the payload rather than eleven named after their components: a reader that
 * switches on `kind` reads `spec` whatever the answer, and adding a component to the dictionary adds
 * no member name to the file format. The variant's alternative *index* is never written - `kind`
 * holds the dictionary spelling, so an alternative may be added to `NodePayload` without renumbering
 * a committed artifact.
 *
 * The example is in the byte order the file actually has, because `mdux.evidence.json` sorts every
 * object's members lexicographically and these artifacts are byte-compared across toolchains. An
 * optional name the component did not declare is omitted rather than written empty, matching what
 * `goldens.json` already does with `textKey` and `colorToken`.
 *
 * ## Reading is as strict as writing
 *
 * `readPackage()` rejects a member it does not know, in either the package or a spec. That is
 * deliberate and it is the fail-closed direction: these files are reviewed and byte-compared, so a
 * member the reader silently ignored would be a member a reviewer believed was doing something. It
 * also rejects a `kind` outside the dictionary, a spec whose shape does not match its `kind`, and a
 * package the schema refuses - the same `validate()` a device would run, so a hand-edited artifact
 * fails here rather than at the `static_assert` in generated code with no file name attached.
 */
module;

export module mdux.tools.medui.package;

import std;
import mdux.draw;
import mdux.medui.schema;
import mdux.tools.cli;
import mdux.tools.medui.goldens;
import mdux.tools.medui.layout;

export namespace mdux::tools::medui {

/**
 * @brief What a compiled screen needs that its source does not carry.
 *
 * `id` is the artifact slug - `generated/screen/<id>/` - which ADR-012 requires to match
 * `^[a-z0-9][a-z0-9-]*$` while a screen's name in the grammar is CamelCase. The mapping is the
 * recipe's business (#198); this stage records the slug it is given.
 *
 * `budget` is **declared, not computed**. Computing it needs a per-component cost model, and that
 * model is the runtime's: #199 owns how a node becomes vertices, and a bound this stage invented
 * would be a second answer to a question the runtime has not yet answered once. Until then the
 * product declares its ceiling in the recipe, the schema refuses an empty one on a screen with
 * nodes, and `DrawList` fails closed against it at run time.
 */
struct PackageInputs {
    std::string_view                                   id;
    mdux::draw::DrawBudget                             budget{};
    std::span<const mdux::medui::TextPackageApproval>  approvedTextPackages;
    std::span<const mdux::medui::ImagePackageApproval> approvedImagePackages{};
};

/**
 * @brief A compiled screen with its storage: the owner a `ScreenPackage` view points into.
 *
 * Move-only. See the module comment for why copying cannot be written correctly and why the text
 * lives in a deque.
 */
class ScreenDocument {
public:
    ScreenDocument() = default;

    ScreenDocument(const ScreenDocument&)            = delete;
    ScreenDocument& operator=(const ScreenDocument&) = delete;

    /// Moving is safe, and not by accident: see the module comment for why a deque's elements
    /// survive both move operations, and `medui-package-survives-a-move` for the assertion of it.
    ScreenDocument(ScreenDocument&&)            = default;
    ScreenDocument& operator=(ScreenDocument&&) = default;
    ~ScreenDocument()                           = default;

    /// The screen, as the device's own type. Views this document; valid while it lives.
    [[nodiscard]] mdux::medui::ScreenPackage package() const noexcept;

    /// Records the header. Interns `id`, so the caller's storage need not outlive the call.
    void setHeader(std::string_view                                   id,
                   std::int32_t                                       surfaceWidth,
                   std::int32_t                                       surfaceHeight,
                   mdux::draw::DrawBudget                             budget,
                   std::span<const mdux::medui::TextPackageApproval>  approvedTextPackages,
                   std::span<const mdux::medui::ImagePackageApproval> approvedImagePackages = {});

    /// Appends a node. Its views must already point into this document - `intern()` produces them.
    ///
    /// Invalidates any package previously obtained from `package()`, whose node span is a view of a
    /// vector this grows. Call it again after the last node rather than holding one across a build.
    void addNode(mdux::medui::CompiledNode node);

    /// Interns text and returns a stable view of the copy this document now owns.
    ///
    /// Empty text interns to an empty view without storing anything: "absent" is a legal value for
    /// every optional name, and it is not worth a deque entry.
    [[nodiscard]] std::string_view intern(std::string_view text);

    /// Interns a list of names and returns a stable span of the views, for a `StatusIndicator`'s
    /// parallel `stateKeys` and `colorTokens`.
    [[nodiscard]] std::span<const std::string_view> internList(std::span<const std::string> items);

private:
    std::deque<std::string>                        text_;
    std::deque<std::vector<std::string_view>>      lists_;
    std::vector<mdux::medui::TextPackageApproval>  approvedTextPackages_;
    std::vector<mdux::medui::ImagePackageApproval> storedImagePackages;
    std::vector<mdux::medui::CompiledNode>         nodes_;
    std::string_view                               id_;
    std::int32_t                                   surfaceWidth_{0};
    std::int32_t                                   surfaceHeight_{0};
    mdux::draw::DrawBudget                         budget_{};
};

/**
 * @brief Builds the compiled screen from a resolved layout.
 *
 * Returns a document rather than a result type with a diagnostic list, for the reason
 * `collectGoldens()` gives: every rule that can reject a screen was decided by an earlier stage, and
 * a permanently empty diagnostic vector would suggest this one can reject a screen when it cannot.
 * The component dictionary was checked by semantic analysis, the rectangles by the layout solver,
 * and - the case #197 raised as an open gap - **compiled** id uniqueness by the layout solver too,
 * which re-runs it after synthesising a Row's background node (`Layout.cpp`, `validateUniqueIds()`).
 * That gap is therefore closed upstream, and this stage inherits the guarantee rather than repeating
 * the check.
 *
 * A screen reaching here that breaks any of those means a gate was bypassed - the AST is public and
 * can be built by hand - and throws, as layout and goldens do for the same reason. So does a budget
 * the schema refuses: `PackageInputs` documents it as a precondition the recipe reader diagnoses,
 * because this stage has no diagnostic code for a recipe's value and inventing one is the mistake
 * #219 exists to avoid repeating.
 *
 * @throws std::logic_error if a gate was bypassed, or if the resulting package does not validate.
 */
[[nodiscard]] ScreenDocument buildPackage(const LayoutResult& layout, PackageInputs inputs);

/// The canonical `package.json` bytes for a validated screen, trailing newline included.
///
/// @throws std::logic_error if the package does not validate, or if a name is not valid UTF-8 - both
///         of which mean it did not come from `buildPackage()` or `readPackage()`.
[[nodiscard]] std::string writePackage(const mdux::medui::ScreenPackage& package);

/// The canonical `goldens.json` bytes: ADR-012's sidecar, written as `[]` when a screen pins nothing.
///
/// @throws std::logic_error if a reference carries text that is not valid UTF-8.
[[nodiscard]] std::string writeGoldens(std::span<const GoldenReference> goldens);

/// A screen read back from committed bytes, and any diagnostic that prevented one.
///
/// `document` is meaningful only when `diagnostics` is empty; a failed read leaves it empty rather
/// than partially filled, so a caller cannot consume half a screen.
struct PackageReadResult {
    ScreenDocument                            document;
    std::vector<mdux::tools::cli::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept {
        return diagnostics.empty();
    }
};

/**
 * @brief Parses canonical `package.json` bytes into a document.
 *
 * Strict in the ways the module comment describes. Diagnostics carry `SCP0NN` codes, a family local
 * to this tool in the same sense as `mdux-shaderemit`'s `SHE0NN` - the `MEDUI-E0NN` registry is the
 * shared contract for what a *screen source* may say, and a malformed committed artifact is not that.
 *
 * @param file the path to name in diagnostics, e.g. `generated/screen/neurosense-500/package.json`
 */
[[nodiscard]] PackageReadResult readPackage(std::string_view text, std::string file);

}  // namespace mdux::tools::medui
