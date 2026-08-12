/**
 * @file Diagnostics.cppm
 * @brief The `.medui` compiler's diagnostic code registry: one table, enumerated by tests.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * Host-only. The compiler never reaches a device, so this module may allocate and throw freely -
 * and the codes it publishes are the compiler's contract with an agent, which is the reason the
 * registry exists at all.
 *
 * ## Why a registry rather than string literals
 *
 * Every other tool in this repository spells its codes as string literals at the call site:
 * `constexpr std::string_view recipeUnparsed = "TXT001";` in `tools/text/TextBake.cpp`, and in
 * `mdux-docs-lint` not even that - its nineteen `MDX-D###` call sites spell the code inline, with
 * no table anywhere. That works until someone needs to answer a question about the *set*: is this
 * code already taken, is this code still emitted, does every code have a fix hint. None of those
 * is answerable by grep with any confidence, and all three are answerable by construction here.
 *
 * The shape is a `constexpr` table keyed by an enum. Call sites name `Code::UnknownColorToken`,
 * never `"MDX-E030"`, so:
 *
 * - **An unregistered code cannot be emitted.** There is no overload taking a string.
 * - **A registered-but-unused code is detectable**, because the enum is exhaustive and a test can
 *   walk it. `DiagnosticsTests.cpp` does.
 * - **A typo is a compile error** rather than a diagnostic nobody can grep for.
 *
 * ## The `MDX-E` prefix
 *
 * `mdux-docs-lint` publishes `MDX-D###`, and the diagnostic schema names `MDX-D011` in its own
 * description, so `MDX-` + one letter + three digits is an established family and this joins it as
 * `MDX-E###`. The bakers' three-letter codes (`SHB`, `SHE`, `TXT`, `MLB`, `EVL`, `GOV`) are a
 * second, older convention; they are not changed here, and nothing needs them to be - the schema's
 * `code` pattern admits both, and `code` is opaque to a consumer that keys off it.
 *
 * **`E` is for the language, not for "error".** Severity is a separate field, and an `MDX-E` code
 * may be a warning. Reading the letter as a severity would make `MDX-E###` at severity `warning`
 * look like a mistake, so it is worth saying which reading is intended.
 *
 * ## Stability
 *
 * A code's meaning is fixed once published. Rewording `message` or `fixHint` is always allowed and
 * never a breaking change; changing what a code *means* is not, and the replacement takes a new
 * number rather than reusing a retired one. Numbers are therefore not reused even when a check is
 * deleted - `retired` records that, so the gap is visible rather than looking like an oversight.
 */
module;

export module mdux.tools.medui.diagnostics;

import std;
import mdux.tools.cli;

export namespace mdux::tools::medui {

/**
 * @brief Every diagnostic the `.medui` compiler can emit.
 *
 * Each enumerator has exactly one row in `registry()`, and a test asserts that. The order here is
 * the order of the table and has no other meaning; codes are assigned by number, not by position.
 *
 * Only codes whose meaning is already fixed by an accepted document appear. ADR-011, ADR-012 and
 * the `medui-authoring` skill between them mandate every one below; nothing here anticipates a
 * check that has not yet been argued for, because a code invented ahead of its rule is a code
 * whose meaning is decided by whoever implements it first.
 */
enum class Code : std::uint8_t {
    // 000-009: the recipe and the file, before any `.medui` text is read.
    RecipeUnreadable,
    RecipeUnparsed,
    RecipeMissingMember,
    SourceUnreadable,
    SourceNotUtf8,

    // 010-029: the grammar. See the `medui-authoring` skill for the shape being enforced.
    UnexpectedToken,
    UnknownComponent,
    MissingRequiredField,
    UnknownField,
    DuplicateNodeId,
    NestedRow,
    ForbiddenConstruct,
    HardcodedString,

    // 030-049: names that must resolve. ADR-011: validated at build time, substituted on device.
    UnknownColorToken,
    UnknownTextKey,
    TextKeyMissingForLocale,

    // 050-069: the bounds and budgets that make the runtime's job finite.
    TextBudgetExceeded,
    LayoutOverflow,
    SurfaceExceeded,
    CharsetEscape,

    // 070-089: safety-critical annotation rules, per the skill and ADR-012 decision 1.
    SafetyCriticalWithoutRequirement,
    UnknownCvCheck,
};

/// One row of the registry. `constexpr`-friendly throughout: the table is built at compile time.
struct CodeInfo {
    Code code{};
    std::string_view id;       ///< the published `MDX-E###` string; stable once shipped
    cli::Severity severity{};  ///< the severity this code is emitted at, unless a call site lowers it
    std::string_view summary;  ///< what the code means, one clause, for documentation and tests
    std::string_view fixHint;  ///< what an author should do; empty when there is no single fix
};

/**
 * @brief The registry, in code order.
 *
 * `std::span` over a namespace-scope `constexpr` array in the module's implementation unit
 * rather than an inline variable, so there is exactly one table however many translation units
 * import this module.
 */
[[nodiscard]] std::span<const CodeInfo> registry() noexcept;

/// The row for `code`. Every enumerator has one, which `registry()`'s test asserts.
[[nodiscard]] const CodeInfo& info(Code code) noexcept;

/// The published identifier, e.g. `"MDX-E030"`.
[[nodiscard]] std::string_view id(Code code) noexcept;

/**
 * @brief Numbers that were published and then retired.
 *
 * Empty today, and present from the start so that the first retirement has somewhere to go rather
 * than prompting a decision under time pressure. A retired number is never reused: a consumer that
 * pinned behaviour to `MDX-E017` must not silently start matching a different rule.
 */
[[nodiscard]] std::span<const std::string_view> retired() noexcept;

/**
 * @brief Builds a `cli::Diagnostic` carrying `code`'s registered identity.
 *
 * The only way to produce a diagnostic from this compiler, and deliberately so - there is no
 * overload taking a code string, so an unregistered code cannot be emitted. `message` is the
 * caller's, because only the caller knows the offending value; `fixHint` comes from the registry
 * unless the caller has something more specific to say.
 *
 * `line` and `column` are 1-based, 0 meaning "no precise position on this axis", exactly as the
 * shared envelope defines them.
 */
[[nodiscard]] cli::Diagnostic diagnose(Code code, std::string file, std::size_t line,
                                       std::size_t column, std::string message,
                                       std::string fixHint = {});

}  // namespace mdux::tools::medui
