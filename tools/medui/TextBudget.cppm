/**
 * @file TextBudget.cppm
 * @brief Build-time text-budget validation: does a resolved box contain the widest approved
 *        translation, and can a dynamic-text source produce a character nobody baked.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-010 No on-device text shaping
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * Host-only, and the last check before emission. It runs after `resolveLayout()` because a budget
 * is a statement about a *resolved* rectangle: `width: Fill` has no width until the solver has run,
 * and most text in a real screen sits in one.
 *
 * ## Why this stage exists at all
 *
 * A compiled screen is locale-free (ADR-011, as amended by #203): it carries `textKey` rather than
 * glyph runs, and the device joins the two for the locale it is running. That amendment moved the
 * *substitution* to the device and deliberately left this check behind:
 *
 * > The compiler still validates every key against every approved locale, and still validates text
 * > budgets against the widest approved translation (#195).
 *
 * So the screen a device holds is one rectangle per node for all locales, and the rectangle has to
 * be the one that survives the worst translation. German and Finnish routinely run half again the
 * length of English; a box sized against the authoring locale renders a clipped word on a shipped
 * device, in a language the author does not read, on a screen nobody re-reviewed. This project
 * treats that as a usability risk to be controlled at build time, and ADR-011's answer is to make
 * it fail to compile rather than to mitigate it downstream. Where that decision sits against the
 * standards corpus is ADR-011's to record, not this module's to assert.
 *
 * ## What "measure" means here, and what it does not
 *
 * There is no shaping in this stage, and no second metrics path. The text package already holds
 * the runs `mdux-textbake` positioned for each locale, and the font package already holds the glyph
 * table those runs index. Measuring is therefore reading: decode each 6-byte record through
 * `mdux::text::draw::decodeRecord()` - the same decoder the device draws with, so the byte-order
 * and baseline contracts have one implementation - look each glyph up in the same
 * `mdux::font::FontPackage` the runtime will, and take the extent of the rectangles that result.
 *
 * Re-deriving the extent from advance widths and a string would be the second path, and it would be
 * the wrong one twice over: it would not see the baker's kerning decisions, and a disagreement
 * between it and the baked runs would surface as a device-time clip that the compiler had certified.
 *
 * The extent measured is the *ink* box - what coverage the atlas actually paints - because that is
 * what a viewer sees leave the box. `recordRun()` skips blank glyphs rather than recording empty
 * rectangles, and this measurement skips exactly the same ones for exactly the same reason: a
 * trailing space is not visible, so it cannot overflow visibly.
 *
 * Only the extent is checked, never the placement. Where a run sits inside its box - leading,
 * centred, trailing - is the emitter's decision (#197), and a stage that assumed one alignment
 * would reject screens the emitter would have laid out correctly.
 *
 * ## Dynamic text and the restricted charset
 *
 * Static text is safe by construction: the baker positioned it, so every glyph exists. Dynamic text
 * is not. A `Clock` formatting a time, or a `TextInput` echoing an operator, can reach a code point
 * nobody baked, and ADR-010 leaves the runtime no fallback - having one would mean mapping code
 * points on device.
 *
 * `FontPackage::restrictedCharset` is the declared answer and `permits()` is the test. What this
 * stage adds is the *source* side of it: a named dynamic-text value (`format:` on `Clock`,
 * `charset:` on `TextInput`) is checked against the governed table in `TextBudgetInputs`, and every
 * code point that table says the name can produce must be one the font package can draw.
 *
 * A produced range that names something which is not a character - the surrogate block, or anything
 * past U+10FFFF - is `MEDUI-E053` before the font package is consulted at all. Nothing can draw a
 * non-character, so `permits()` is the wrong question to ask about one, and a package whose table
 * claimed such a code point would answer yes.
 *
 * A name absent from the table is `MEDUI-E053` too, and that is a deliberate fail-closed reading:
 * "this name does not resolve" means the compiler cannot bound what it produces, which is
 * indistinguishable, from here, from a name that produces something unbakeable. The alternative -
 * passing a name over in silence because no rule described it - is how a device-time surprise gets
 * a compiler's signature on it.
 *
 * A `TextInput` with no `charset:` field is not checked and is not an error. The field is optional
 * in the shared component model, and its absence means the component is restricted to the font
 * package's own charset - which cannot escape itself. An author names a charset to *narrow* the
 * set, never to widen it.
 *
 * ## What this stage deliberately does not check
 *
 * `TextInput` carries `max_length`, and the shared component model asks that "bounded-dynamic" text
 * fit its box in the worst case as well. Sizing that worst case means deciding how the runtime
 * advances the pen for text no baker positioned, and until #258 no such rule existed - so the check
 * waited for the runtime rule rather than guessing at it.
 *
 * **`NumericDisplay`'s `template:` no longer waits.** It was left for the same reason and is now
 * measured: ADR-010 decision 4's amendment fixes the runtime pen rule, `mdux.medui.reading` is its
 * one implementation, and a `[numericTemplates]` table supplies the rendering a product identifier
 * stands for - the same mechanism `charset:` has always resolved through. A template with no rule
 * behind it is `MEDUI-E053`, fail-closed exactly as an unknown charset name is.
 *
 * `TextInput` still waits, and now for a smaller reason than it did: the pen rule exists, but what
 * a `max_length` of 12 renders as is a *charset* question rather than a pattern one - the worst case
 * is the widest twelve glyphs the charset admits, in an order nothing constrains - and that is
 * #260's to settle rather than this issue's to guess at.
 *
 * ## Why the shared conformance suite does not cover these two codes
 *
 * `MEDUI-E050` and `MEDUI-E053` are in the pinned contract's code table, but a case cannot exercise
 * them yet: `schemas/case.schema.json` lets a case carry theme tokens and per-locale *keys*, and
 * neither font metrics nor a charset table. A case therefore has no way to state what a translation
 * measures or what a format can produce. The codes are the contract's, the checks are here, and the
 * tests below stand in until the case schema can express the inputs.
 */
module;

export module mdux.tools.medui.textbudget;

import std;
import mdux.font.schema;
import mdux.text.schema;
import mdux.tools.cli;
import mdux.tools.medui.ast;
import mdux.tools.medui.layout;

export namespace mdux::tools::medui {

/**
 * @brief One approved locale: its baked text package, and the sidecar bytes its runs address.
 *
 * The package and the sidecar are separate because that is how they are committed - `package.json`
 * beside `runs.bin` - and the caller is what reads them. `sidecar` must be the whole file: run
 * ranges are absolute offsets into it, exactly as `TextPackage::validate()` checked them, and
 * `checkTextBudgets()` refuses a span whose size is not the package's own `sidecarByteLength`.
 *
 * Refusing on the *whole* length rather than on each run's end is the difference between a check
 * that holds and one that happens to hold: a truncated sidecar whose first run survives the cut
 * would measure that run correctly and mismeasure - or silently skip - every run after it.
 */
struct LocaleText {
    const mdux::text::TextPackage* package{nullptr};
    std::span<const std::byte>     sidecar{};
};

/**
 * @brief One named dynamic-text source, and every code point it is able to produce.
 *
 * The governed table an author's `format: HH_MM` or `charset: LATIN_BASIC` is resolved against.
 * It is an input rather than a constant here for the same reason `themeTokens` is an input to
 * semantic analysis: the names belong to a product's governed tables, not to the language, and a
 * compiler that shipped its own list would be authoritative about a set it does not own.
 *
 * `produces` is stated as charset ranges rather than a code-point list because a set is reviewed
 * as ranges - "U+0030..U+0039, U+003A" is checkable by eye - and because it is then the same shape
 * as the font package's own `restrictedCharset` that it is compared against.
 */
struct DynamicTextRule {
    std::string_view                          name;
    std::span<const mdux::font::CharsetRange> produces;
};

/**
 * @brief One named numeric template, and the shape it renders as (#258).
 *
 * The product's governed table, in the shape `DynamicTextRule` established and for its reason: a
 * `template:` names something the product owns, not something the language defines, and a compiler
 * shipping its own list would be authoritative about a set it does not own.
 *
 * `rendering` is a pattern in `mdux::medui::PatternKind::Numeric`'s alphabet - `#` is a digit slot
 * and every other character, letters included, is a literal. `###.# mmHg` is four digit slots, a
 * point, a space and a four-letter unit. That alphabet is why the slot character is `#` rather than
 * a letter: a unit is made of letters, and `mmHg` contains an `H` a clock would read as an hour.
 *
 * The device draws through this same pattern, and gets it from its `ReadingBinding` rather than
 * from the compiled screen - see `mdux.medui.screen`, which explains why the artifact carries the
 * template's *name* and what the runtime re-checks so that a drifted table cannot overflow a node.
 */
struct NumericTemplateRule {
    std::string_view name;
    std::string_view rendering;
};

/**
 * @brief Everything one screen is measured against.
 *
 * `font` is the committed font package the approved locales were baked into, and it is required:
 * there is no measurement without metrics, and a defaulted one would be a second metrics path.
 *
 * `locales` must be *exactly* the set the font package approves - one entry per tag in
 * `FontPackage::locales`, none missing, none repeated, none outside it - and that is checked rather
 * than assumed. A budget is a claim about the worst approved translation, so a caller that supplied
 * only the locale it happened to be looking at would get a screen certified against a set nobody
 * approved: the omitted German or Finnish translation is exactly the one that overflows, and it
 * would overflow on a device with the compiler's blessing. Requiring the whole set makes that
 * particular silence impossible.
 */
// Every member carries a default initialiser. `-Wmissing-field-initializers` is an error in this
// tree, so a type whose optional members still have to be spelled at every call site is a trap - and
// an omitted `dynamicText` is fail-closed anyway: a name with no rule behind it is `MEDUI-E053`
// rather than a name silently accepted.
struct TextBudgetInputs {
    const mdux::font::FontPackage*       font{nullptr};
    std::span<const LocaleText>          locales{};
    std::span<const DynamicTextRule>     dynamicText{};
    std::span<const NumericTemplateRule> numericTemplates{};
};

/// The ink one baked run paints, in surface pixels.
struct TextExtent {
    std::int64_t width{0};
    std::int64_t height{0};

    auto operator<=>(const TextExtent&) const = default;
};

/**
 * @brief The worst approved case for one authored text value.
 *
 * `extent` is the per-axis maximum across every approved locale, which is what a locale-free screen
 * has to reserve, and what #197's `DrawBudget` computation consumes rather than measuring again.
 * `locale` names the locale that produced the widest width - the one an author needs to open when
 * a budget fails - so on a screen whose tallest and widest translations differ, `extent.height` may
 * come from a locale this field does not name. Width is what a budget conversation is almost always
 * about, and naming two locales in one record would make the common case read like an edge case.
 */
struct TextMeasurement {
    std::string nodeId;
    std::string field;  ///< the authored field, e.g. `text` or `label`
    std::string textKey;
    std::string locale;
    TextExtent  extent{};
};

/**
 * @brief Measurements and any diagnostic that stopped the screen.
 *
 * `measurements` is empty whenever a diagnostic is present, matching `LayoutResult`: a caller
 * cannot accidentally consume the budget of a screen that failed its budget check.
 */
struct TextBudgetResult {
    std::vector<TextMeasurement>              measurements;
    std::vector<mdux::tools::cli::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept {
        return diagnostics.empty();
    }
};

/**
 * @brief Checks every text budget on a resolved screen, and every dynamic-text charset on it.
 *
 * Diagnostics accumulate in resolved-node order, then field order within a node, then the input
 * order of `locales` within a field - so a screen with three bad boxes reports all three, and an
 * author reading top to bottom reads them in the order they were written.
 *
 * The stage assumes its gates: `analyze()` has accepted the screen, so every text key resolves in
 * every approved locale, and `resolveLayout()` has produced these bounds. An approved locale that
 * was not supplied, a supplied locale the font package does not approve, a duplicate locale, a
 * sidecar whose size is not the one its package declares, a key with no run, a text package baked
 * against a different font, a font package whose restricted charset descends or reaches past the
 * last Unicode scalar value, or a record naming a glyph outside the package all mean a caller wired
 * the stages together wrongly rather than an author writing a bad screen, and each throws
 * `std::logic_error` rather than being reported as a diagnostic an author cannot act on.
 *
 * The locale, sidecar, font-identity and duplicate checks run once, before any node is looked at,
 * so a miswired call fails on the wiring rather than on whichever screen happened to have text.
 *
 * @throws std::logic_error if `inputs.font` is null, or if any gate above was bypassed.
 */
[[nodiscard]] TextBudgetResult checkTextBudgets(const LayoutResult& layout, std::string file, TextBudgetInputs inputs);

/**
 * @brief Whether this screen has anything for the budget stage to measure.
 *
 * True when the screen carries a text key, a fixed rendering such as `Clock.format`, or a field
 * whose value names a dynamic-text source such as `TextInput.charset`. All three are things
 * `checkTextBudgets()` checks and nothing else does.
 *
 * Exported so a compiler driver can decide whether a recipe *must* supply a font package and its
 * approved locales, rather than deciding by inspecting the screen itself. The predicates for fixed
 * and dynamic text would otherwise exist in two places, and the second copy is the one that would go
 * stale the day another field joins them.
 */
[[nodiscard]] bool needsTextBudget(const ast::Screen& screen);

}  // namespace mdux::tools::medui
