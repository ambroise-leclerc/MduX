/**
 * @file Goldens.cppm
 * @brief Golden references for safety-critical nodes: which nodes a verifier must check, where they
 *        must appear, and in what tint.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * Host-only. A golden reference is the compiler's statement of what a rendered frame must show for
 * the content that matters clinically, written down at build time so that #16's verifier checks a
 * frame against a reviewed expectation rather than against the source that produced it. ADR-012
 * decision 1 commits them to `generated/screen/<id>/goldens.json`, beside the screen package and
 * outside it - a reviewer approving a moved safety-critical rectangle should see that hunk on its
 * own, and a goldens-only change should have its own digest.
 *
 * ## The predicate is fixed, not chosen here
 *
 * ADR-011 settles which nodes are selected, and #16 must use the same rule, so it is restated
 * rather than re-decided:
 *
 * - a node carrying `@safety_critical` emits an entry;
 * - **any** node with an explicit `position:` emits an entry with a `Bounds` check, annotated or
 *   not, because a declared position is a safety-relevant claim by itself;
 * - a node matching both emits exactly **one** entry, with the two check lists merged and
 *   deduplicated - never two entries for one node;
 * - a `@safety_critical` node with no `requirement:` is an error (`MEDUI-E070`), because a
 *   safety-critical node that cannot be traced is the thing the annotation exists to prevent. An
 *   *empty* requirement is the same error: `requirement:` is a `String` field and the semantic
 *   domain accepts `""`, so presence alone would let an untraceable node through the rule written
 *   to stop it.
 *
 * One consequence of the pinned component model, stated because it is easy to read the predicate as
 * promising otherwise: **only a component the dictionary gives a `requirement:` can be
 * safety-critical.** That is `CriticalButton`, `Button`, `NumericDisplay`, `StatusIndicator` and
 * `TextInput`. `Row` takes only `id`, `height`, `spacing` and `background`; `Label`, `Clock`,
 * `Image`, `SignalTrace` and `VulkanViewport` take no requirement either - so an annotation on any
 * of them always fails the rule above. That is the shared contract's answer rather than this
 * compiler's, and the diagnostic says so instead of asking an author to add a field the language
 * would then reject.
 *
 * The consequence worth stating: the file's content is *exactly determined* by the screen. That is
 * what lets #197's consistency test compare sets rather than resolve references - it applies this
 * predicate to the compiled nodes, derives the id set the goldens must contain, and asserts
 * equality. A test that only checked that listed ids resolve would accept the dangerous direction
 * silently, because a safety-critical node whose golden was dropped looks exactly like a screen
 * with fewer safety-critical nodes (ADR-012, consequences).
 *
 * ## Why validation and emission are separate calls
 *
 * `validateSafetyAnnotations()` reads the parsed screen; `collectGoldens()` reads the resolved one.
 * The split is not tidiness: the annotation rules are decidable from source alone, and the shared
 * conformance suite requires that. `MEDUI-CASE-SAFETY-MISSING-REQUIREMENT` pins `MEDUI-E070` on a
 * screen that declares no `surface:`, which the layout solver cannot resolve at all - so a design
 * that could only report `MEDUI-E070` after layout could not satisfy the contract it implements.
 *
 * The order for a compiler driver is therefore: parse, analyze, validate annotations, resolve
 * layout, collect goldens.
 *
 * ## The emitted shape, agreed here
 *
 * #196 is where the shape is settled, because #197 serialises it and #16 consumes it, and agreeing
 * it after either of those exists means changing a committed artifact. `GoldenReference` maps
 * one-to-one onto the canonical JSON:
 *
 * ```json
 * { "bounds": { "height": 512, "width": 512, "x": 1392, "y": 80 },
 *   "colorToken": "Theme.Colors.ScoreDigits",
 *   "cvChecks": ["Bounds", "ColorHash"],
 *   "nodeId": "sedation-index",
 *   "textKey": "STR-SEDATION-LABEL" }
 * ```
 *
 * The C++ members and the JSON members are now the same words, which is worth one line because they
 * were not: an earlier revision spelled the file `node_id`, `text_key`, `color_token`, `cv_checks`,
 * transcribed from TrustSC's Rust identifiers. TrustSC emits no screen JSON at all - its compiled
 * screen is generated Rust - so nothing was being matched, while every package this repository has
 * committed is camelCase. ADR-011 records the amendment; the effect here is that a reader has one
 * vocabulary for the struct, the file and the verifier that reads it.
 *
 * The example is in the byte order the file actually has. `mdux.evidence.json` sorts every object's
 * members lexicographically by UTF-8 code unit, nested objects included, so `bounds` reads
 * `height, width, x, y` rather than the order a reader would write by hand. Object order is
 * semantically irrelevant in JSON and deliberately relevant here: `goldens.json` is byte-compared
 * across toolchains, so an example in a different order would be describing a file that cannot exist.
 *
 * `textKey` and `colorToken` are omitted when the node has none. `cvChecks` is sorted and
 * deduplicated so that one screen has one serialisation - `goldens.json` is byte-compared across
 * toolchains like every other committed artifact, and a set whose order depended on which rule
 * selected the node first would not survive that.
 *
 * The two members are halves of one claim, which is why a `ColorHash` check is *refused* for a node
 * with no single declared colour token rather than emitted beside an absent `colorToken`. A
 * verifier asked to compare a tint has to be told which tint; a reference that asked without saying
 * would be one #16 could only skip.
 *
 * ## What a golden pins for dynamic content, and what it must not
 *
 * A `NumericDisplay`, `Clock`, `SignalTrace` or `StatusIndicator` shows a value that changes. The
 * golden pins **where** that content appears and **in what tint**, never what the value is: pinning
 * a live number would make the verifier fail whenever the demonstrator changed, which trains a team
 * to ignore it. So `textKey` is filled only from a field whose value is a single static key, and a
 * list-valued field such as `StatusIndicator`'s `states:` leaves it empty - the state shown is
 * exactly the varying part.
 */
module;

export module mdux.tools.medui.goldens;

import std;
import mdux.tools.cli;
import mdux.tools.medui.ast;
import mdux.tools.medui.layout;
import mdux.verify;

export namespace mdux::tools::medui {

/**
 * @brief A verification #16's driver performs against a rendered frame.
 *
 * The closed set the shared language defines, named here and *defined* in `mdux.verify` (#252).
 * Order is the serialisation order: `cvChecks` is sorted by this enumeration so that a screen has
 * one canonical form.
 *
 * An alias rather than a second enumeration, for the reason ADR-012 decision 4 gives about the
 * golden predicate: the compiler that writes these names and the verifier that reads them back must
 * not be able to disagree about what the set contains, and two declarations of one closed set agree
 * until the day they matter. The direction is deliberate too - the governed module owns the type,
 * because a device-side reader may not depend on a host tool.
 */
using mdux::verify::CvCheck;

/// The spelling an author writes and the serialiser emits, e.g. `Bounds`.
using mdux::verify::spell;

/// The check `name` spells, or nothing when the name is not one of the closed set.
using mdux::verify::parseCvCheck;

/**
 * @brief One golden reference: what a verifier must find, and where.
 *
 * Field names mirror the canonical JSON members one-to-one; see the module comment for the mapping.
 * `textKey` and `colorToken` are empty when the node draws neither.
 */
struct GoldenReference {
    std::string          nodeId;
    LayoutRect           bounds{};
    std::string          textKey;
    std::string          colorToken;
    std::vector<CvCheck> cvChecks;  ///< sorted, deduplicated, never empty

    [[nodiscard]] bool operator==(const GoldenReference&) const = default;
};

/// Accumulated annotation diagnostics; an empty result admits the screen to layout and emission.
struct SafetyResult {
    std::vector<mdux::tools::cli::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept {
        return diagnostics.empty();
    }
};

/**
 * @brief Checks the `@safety_critical` annotation rules on a parsed screen.
 *
 * Reports, all at the position of the offending token and in source order:
 *
 * - `MEDUI-E070` for an annotated node whose `requirement:` is absent, blank, or unavailable to the
 *   component at all, at the annotation;
 * - `MEDUI-E071` for a `cv_check` naming a verification outside the closed set, and for a
 *   `ColorHash` on a node whose expected tint cannot be derived, at the offending name;
 * - `MEDUI-E033` for a `cv_check` value that is not a name.
 *
 * Deliberately AST-level: see the module comment for why the shared contract requires these two
 * codes to be reportable without a resolved layout.
 */
[[nodiscard]] SafetyResult validateSafetyAnnotations(const ast::Screen& screen, std::string file);

/**
 * @brief Derives the golden set from a resolved screen.
 *
 * Entries follow resolved-node order, which is the order the compiled package lists its nodes in,
 * so a set comparison and a diff both read top to bottom.
 *
 * Returns references rather than a result type with a diagnostic list: every rule that can fail was
 * decided by `validateSafetyAnnotations()`, and a permanently empty diagnostic vector would suggest
 * this stage can reject a screen when it cannot.
 *
 * That validation is a required gate. A malformed annotation reaching here - a `cv_check` value
 * that is not a name, or a name outside the closed set - means it was bypassed, and throws
 * `std::logic_error` rather than emitting a golden nobody can verify.
 *
 * Only authored nodes are described. The solver's one synthetic node is a Row's background, and a
 * Row can be neither annotated nor positioned, so it is never selected - see the interface comment
 * above for why a container cannot be safety-critical at all.
 *
 * @throws std::logic_error if the annotation gate was bypassed.
 */
[[nodiscard]] std::vector<GoldenReference> collectGoldens(const LayoutResult& layout);

}  // namespace mdux::tools::medui
