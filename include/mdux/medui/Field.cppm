/**
 * @file Field.cppm
 * @brief The fixed-pitch text field a `TextInput` displays: cells, a caret, and the envelope a
 *        compiler measures both against.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 * @compliance ADR-010 No on-device text shaping (decision 4, as amended by #258 and #260)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * `mdux.medui.reading`'s sibling, split out for its reason: geometry that can be tested against
 * numbers rather than only against a rendered frame. Both are imported by the device runtime *and*
 * by the host budget stage, which is what makes a compile-time certificate mean anything (ADR-008
 * decision 1 applied to text).
 *
 * ## Why a field is fixed pitch, and why that is not a style choice
 *
 * ADR-010 decision 4 forbids "on-device code that advances a pen by a runtime-computed width", and
 * #258's amendment admits one bounded exception: a *pattern* whose literals, slot positions and
 * glyph count are build-time constants, in which only which digit stands in each slot varies. A
 * `Clock` and a `NumericDisplay` fit that exactly - their patterns are constants and digits share an
 * advance, so every pen position is a constant.
 *
 * A `TextInput` cannot. Its value is a run of characters from an open-ended charset, so a
 * proportional pen would put cell 5 at a position that depends on characters 0 to 4 - the
 * runtime-computed width the decision names. **The field is therefore a grid**: cell *k* is at
 * `k * cellWidth(font)` and nothing about that position depends on the value. What varies is which
 * glyph occupies a cell, which is the same freedom the digit slots already have.
 *
 * The cost is stated rather than hidden: a proportional font drawn on a fixed pitch looks
 * monospaced, with the loose spacing that implies for narrow characters. That is the appearance
 * price of a placement a compiler can certify, and the alternative is not a nicer field - it is a
 * pen this architecture does not admit.
 *
 * ## Why the cell width comes from the font package rather than from the screen
 *
 * `cellWidth()` is the widest advance over the font package's **whole restricted charset**, which is
 * a property of one committed artifact that the compiler and the device both hold. Neither has to be
 * told it, and neither can be told a different one: two sides that derived it from the same bytes
 * derive the same number.
 *
 * A `TextInput`'s `charset:` field could in principle narrow it - a field restricted to digits would
 * fit in a tighter grid than one that admits `@` - and that is deliberately *not* done. The
 * compiled node carries `charset` as a validated name rather than as a set (ADR-011), so a runtime
 * that wanted the narrower pitch would need a product table shipped beside the screen, which is the
 * exposure `ReadingSlot` documents at length for a `templateId`. The conservative direction is free
 * here: a box sized for the font's widest glyph holds every narrower one, so an author who narrows
 * the charset gets a box larger than they strictly need and never one too small.
 *
 * ## What is bounded, and by what
 *
 * `maxFieldCells` caps the cells one field can draw, so per-node work is a constant a device knows
 * before it runs. A `max_length` past it is refused at compile time rather than clipped, because a
 * clipped field is one that stops showing characters an operator typed.
 *
 * A value longer than the field has cells is `TextTooLong` and **nothing is drawn**. Dropping the
 * tail of a patient identifier shows a shorter, entirely plausible identifier, in a box whose label
 * says what it is, with nothing on screen to say it was cut - the same refusal `recordNumeric()`
 * makes about a value with more digits than its slots, for the same reason.
 *
 * A character the font package cannot draw is `GlyphNotInPackage`. There is no fallback and no
 * substitute: ADR-010 leaves the runtime none, and a `?` in a patient identifier is a different
 * identifier. The compile-time counterpart is the charset check the budget stage already performs,
 * which is what makes this refusal the second line rather than the first.
 */
module;

export module mdux.medui.field;

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;
import mdux.font.schema;
import mdux.medui.reading;
import mdux.text.draw;

export namespace mdux::medui {

/**
 * @brief The largest field this runtime will draw, in cells.
 *
 * `maxPatternLength`'s counterpart, and chosen the same way: large enough for any identifier a
 * bounded input field on a medical display should hold, small enough that a reviewer can multiply it
 * by a `DrawBudget` in their head. A `max_length` beyond it is refused rather than clipped.
 */
inline constexpr std::size_t maxFieldCells = 64;

/**
 * @brief The caret's width in pixels.
 *
 * One pixel, and named rather than inlined because the measurement has to reserve it: a caret sitting
 * after the last cell is the rightmost thing a field ever draws, so a box that fits the glyphs but
 * not the caret is a box the caret leaves.
 */
inline constexpr std::int64_t caretWidth = 1;

/// Why a field was refused. Every one leaves the draw list exactly as it was found.
enum class FieldError : std::uint8_t {
    NoCells,            ///< the field has no cells, so there is nowhere to display anything
    TooManyCells,       ///< the field has more cells than `maxFieldCells`
    TextTooLong,        ///< the value has more characters than the field has cells
    CaretOutOfRange,    ///< the caret is not a position in the field, nor just past its last cell
    GlyphNotInPackage,  ///< a character the value needs is one the font package cannot draw
    EmptyCharset,       ///< the font package admits no code point, so no cell width can be derived
    ListRejected,       ///< `DrawList` refused a rectangle - budget, or a degenerate extent
};

[[nodiscard]] std::string_view describe(FieldError error) noexcept;

/**
 * @brief The width of one cell, in pixels: the widest advance the font package can produce.
 *
 * Derived from `restrictedCharset` rather than from the glyph table, so the answer is the set the
 * package *declares* it can draw rather than whatever happens to be in it. A package whose charset
 * admits a code point it has no glyph for contributes nothing for that point, which is the
 * fail-closed direction - `FontPackage::validate()` refuses such a package, and one that reached
 * here without it should not be able to widen a cell with a glyph it cannot draw.
 */
[[nodiscard]] mdux::core::Result<std::int64_t, FieldError> cellWidth(const mdux::font::FontPackage& font) noexcept;

/**
 * @brief The ink a field of `cells` cells can occupy at worst, over every value it can show.
 *
 * The build-time half of the rule `recordField()` implements: a node that can hold this can hold any
 * value the field will ever display, with its caret at any position.
 *
 * Conservative rather than exact, for `measurePattern()`'s reason and by the same construction: the
 * union of every permitted glyph's rectangle at the leftmost and rightmost cell, plus the caret's
 * column past the last one. The extrema may come from different values; a box sized to this holds
 * more than any single value needs, which is the fail-closed direction for a compile-time bound.
 *
 * `inked` false means the package's charset paints nothing at all - a font of blanks - which is a
 * degenerate package rather than an error here.
 */
struct FieldExtent {
    bool         inked{false};
    std::int64_t width{0};
    std::int64_t height{0};

    [[nodiscard]] constexpr bool operator==(const FieldExtent&) const noexcept = default;
};

[[nodiscard]] mdux::core::Result<FieldExtent, FieldError> measureField(const mdux::font::FontPackage& font, std::size_t cells) noexcept;

/**
 * @brief Whether a field of `cells` cells can display `textLength` characters with `caret` where it is.
 *
 * Every refusal that depends on the value rather than on the font, in one place. `recordField()`
 * asks this before it draws anything and `mdux.medui.screen`'s `TextInputBinding::create()` asks it
 * at start-up, so a caller learns at the join what a frame would otherwise tell it later - and the
 * two answers are the same answer because there is one function, not two lists of conditions.
 */
[[nodiscard]] mdux::core::ResultVoid<FieldError> fieldAccepts(std::size_t cells, std::size_t textLength, std::optional<std::size_t> caret) noexcept;

/**
 * @brief Records `text`, and a caret when one is offered, into `node` as `CoverageR8` rectangles.
 *
 * @param list  the destination; rectangles are appended in cell order, the caret last
 * @param font  the font package the glyphs come from
 * @param node  the node's resolved rectangle, in surface pixels
 * @param cells the field's `max_length`: how many cells the grid has
 * @param text  the value, as code points - decoded by the host, never parsed here
 * @param caret the cell the caret sits before, or `nullopt` for a field that is not being edited
 * @param color the tint; coverage modulates its alpha for glyphs, and the caret is solid
 *
 * ## The value is code points, not bytes
 *
 * `std::span<const char32_t>` rather than a `string_view`, and that is the trust-zone boundary
 * showing through rather than a convenience. Decoding UTF-8 is parsing, the governed zone does not
 * parse (ADR-004), and a decoder here would be the first step of exactly the on-device text
 * machinery ADR-010 exists to keep out. The host has the bytes and the encoding; this takes what
 * they mean.
 *
 * ## Where the grid sits, and why it does not move
 *
 * The field's origin is the node's top-left corner and its baseline is placed so the *envelope's*
 * top edge - a build-time constant over the charset - lands on the node's top edge. Deliberately not
 * the value's own ink box, which is what a `Label` uses: a label is drawn once and a field is
 * redrawn as an operator types, and a baseline derived from the current value would shift the whole
 * field the moment a character with a taller ink box was typed into it. A text box that jumps while
 * you type is a defect an author cannot fix from the screen.
 *
 * Allocation-free, `noexcept`, and all-or-nothing: on any refusal the list is rolled back to where
 * it stood on entry.
 */
[[nodiscard]] mdux::core::ResultVoid<FieldError> recordField(mdux::draw::DrawList&          list,
                                                             const mdux::font::FontPackage& font,
                                                             const mdux::core::Rect&        node,
                                                             std::size_t                    cells,
                                                             std::span<const char32_t>      text,
                                                             std::optional<std::size_t>     caret,
                                                             mdux::core::ColorRgba8         color) noexcept;

}  // namespace mdux::medui
