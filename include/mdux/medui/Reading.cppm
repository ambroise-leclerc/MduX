/**
 * @file Reading.cppm
 * @brief Governed-zone reading expansion: a live value into glyph rectangles, through a pattern
 *        whose shape a compiler already measured.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (Result-returning, noexcept)
 * @compliance ADR-010 No on-device text shaping (decision 4, as amended by #258)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * Part of MduXCore. This is what `mdux.medui.trace` is for a waveform: the expansion, split from
 * the screen runtime that decides whether to call it, so the arithmetic can be tested against
 * numbers rather than only against a rendered frame.
 *
 * It is also the module ADR-010 decision 4's amendment names, and the reason that amendment could
 * be written narrowly. Read that decision before changing anything here: every constraint below is
 * a clause of it, and loosening one silently would turn a bounded substitution into the shaping
 * engine the whole ADR exists to keep off a device.
 *
 * ## What a pattern is, and why only the digits vary
 *
 * A pattern is a short string in which some positions are **digit slots** and every other position
 * is a literal. `HH:MM:SS` is six slots and two colons; `###.# mmHg` is four slots, a point, a
 * space and four letters. The literals, the slot positions, and the total glyph count are constants
 * fixed before the device runs. What varies is which of ten digits stands in each slot.
 *
 * That is the whole of the dynamism, and it is what makes the build-time measurement possible:
 * `measurePattern()` walks the pattern once considering **all ten digits at every slot** and
 * returns the envelope of every reading the pattern can ever produce. A screen whose node cannot
 * hold that envelope fails to compile. The runtime is then replaying a shape a compiler certified,
 * not discovering one.
 *
 * Which characters are slots depends on where the pattern came from, and the two alphabets must not
 * be merged. A clock's slots are `H`, `M`, `S`, `Y` and `D`, fixed by the shared contract's
 * renderings. A numeric template's slot is `#` alone - and it has to be, because `###.# mmHg`
 * contains an `H` that is a letter of a unit rather than an hour. `PatternKind` is that distinction,
 * and passing the wrong one turns a unit into a digit.
 *
 * ## Two callers, one arithmetic
 *
 * The host budget stage (`mdux.tools.medui.textbudget`) imports this module rather than carrying
 * its own copy of `slotAt()`, `toPixels()` and the pen walk. ADR-008 decision 1's doctrine, applied
 * to text: the measurement that certifies a box and the placement that fills it are the same
 * arithmetic, so they cannot drift. A device-time clip that a compiler had certified is precisely
 * the failure two implementations would eventually produce, and it is the one a reviewer has no way
 * to localise.
 *
 * The pen is integral throughout. Advances and kerning are font units, summed as `std::int64_t` and
 * converted once per glyph by `toPixels()` - the baker's own half-up rule - so no toolchain can
 * round a pen position differently and no frame depends on a floating-point mode.
 *
 * ## Refused, never truncated or substituted
 *
 * A pattern longer than `maxPatternLength` is refused. A code point the font package cannot draw is
 * refused rather than replaced with a fallback glyph, because ADR-010 leaves no fallback and a
 * reading showing a substitute character is a reading nobody wrote. A value with more digits than
 * its slots can hold is refused rather than having its most significant digits dropped: `199`
 * displayed as `99` in a box whose label says mmHg is the single worst failure this module could
 * have, and it is the one that would look most like a reading.
 *
 * And the whole expansion is all-or-nothing: any refusal rolls the list back to where the reading
 * began, so a frame never carries half a number.
 */
module;

export module mdux.medui.reading;

import std;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;
import mdux.font.schema;
import mdux.medui.schema;
import mdux.text.draw;

export namespace mdux::medui {

/**
 * @brief The longest pattern this module will measure or draw, in characters.
 *
 * The type-level cap `maxGlyphsPerRun` is for a `Label` and `maxSamplesPerTrace` is for a
 * waveform, and it exists for their reason: per-node work has to be a constant a device can
 * multiply by its node count before it runs.
 *
 * 32 because the longest rendering anything can ask for today is `YYYY-MM-DD HH:MM:SS`, which is
 * 19, and because a numeric template carrying more than a dozen characters of unit is a screen
 * problem rather than a budget one. A pattern past it is refused, never truncated.
 */
inline constexpr std::size_t maxPatternLength = 32;

/// The largest digit run one field can fill. Ten digits is more than any reading a device shows,
/// and it is what bounds the fixed-point conversion below without reaching for a 128-bit type.
inline constexpr std::size_t maxDigitsPerField = 10;

/**
 * @brief Which alphabet a pattern's digit slots are written in.
 *
 * Not a detail, and not merged into one set on purpose: `###.# mmHg` contains an `H`, and reading
 * it as an hour slot would draw a digit where a unit belongs. See the module comment.
 */
enum class PatternKind : std::uint8_t {
    Clock,    ///< `H`, `M`, `S`, `Y`, `D` are digit slots; everything else is a literal
    Numeric,  ///< `#` is the digit slot; everything else, letters included, is a literal
};

/// Whether `character` is a digit slot in a pattern of this kind.
[[nodiscard]] constexpr bool isDigitSlot(char character, PatternKind kind) noexcept {
    if (kind == PatternKind::Numeric) {
        return character == '#';
    }
    return character == 'H' || character == 'M' || character == 'S' || character == 'Y' || character == 'D';
}

/**
 * @brief The code points one pattern position can produce.
 *
 * Ten for a digit slot, one for a literal. The host budget stage walks all of them to build an
 * envelope; the runtime picks one. Same function, so the set the compiler bounded is exactly the
 * set the device draws from.
 */
struct PatternSlot {
    std::array<char32_t, 10> points{};
    std::size_t              count{0};
};

[[nodiscard]] constexpr PatternSlot slotAt(char character, PatternKind kind) noexcept {
    PatternSlot slot;
    if (isDigitSlot(character, kind)) {
        for (char32_t digit = U'0'; digit <= U'9'; ++digit) {
            slot.points[slot.count] = digit;
            ++slot.count;
        }
        return slot;
    }
    slot.points[0] = static_cast<char32_t>(static_cast<unsigned char>(character));
    slot.count     = 1;
    return slot;
}

/**
 * @brief Font units to pixels, using the half-up rule the text baker uses for every pen position.
 *
 * Governed and shared rather than restated by each caller: this is the conversion that decides
 * which column a glyph lands in, and a baker and a runtime that rounded it differently would put
 * static and dynamic text on two different grids in the same node.
 */
[[nodiscard]] constexpr std::int64_t toPixels(std::int64_t units, const mdux::font::FontPackage& font) noexcept {
    if (font.unitsPerEm == 0) {
        // `FontPackage::validate()` refuses this, and a package that reached here without it would
        // otherwise divide by zero. Zero is the answer that draws everything on top of itself,
        // which is visibly wrong rather than undefined.
        return 0;
    }
    const auto perEm = static_cast<std::int64_t>(font.unitsPerEm);
    return ((units * static_cast<std::int64_t>(font.pixelSize)) + (perEm / 2)) / perEm;
}

/// Why a reading was refused. Every one leaves the draw list exactly as it was found.
enum class ReadingError : std::uint8_t {
    PatternEmpty,       ///< the pattern has no characters, so there is nothing to draw
    PatternTooLong,     ///< the pattern is longer than `maxPatternLength`
    GlyphNotInPackage,  ///< a character the pattern needs is one the font package cannot draw
    PenMovedBackwards,  ///< kerning would move the pen left of the reading's origin
    ValueTooLarge,      ///< the value has more digits than the pattern's slots can hold
    ValueNegative,      ///< a negative reading, which no pattern here can render
    NoDigitSlots,       ///< a value was offered to a pattern with nowhere to put it
    ListRejected,       ///< `DrawList` refused a rectangle - budget, or a degenerate extent
};

[[nodiscard]] std::string_view describe(ReadingError error) noexcept;

/**
 * @brief The ink a pattern can occupy at worst, over every digit it can show.
 *
 * `inked` false means the pattern paints nothing at all - a pattern of spaces - which is a
 * legitimate pattern rather than an error, and one that produces no rectangles.
 */
struct PatternExtent {
    bool         inked{false};
    std::int64_t width{0};
    std::int64_t height{0};

    [[nodiscard]] constexpr bool operator==(const PatternExtent&) const noexcept = default;
};

/**
 * @brief How many digit slots `pattern` has, in `kind`'s alphabet.
 *
 * Exported so the compiler and the device count slots with the *same* walk (#258). `digitsOf()`
 * refuses a pattern with no slots and one with more than `maxDigitsPerField`, and a template that
 * cannot satisfy those has no drawable reading - so a compiler that measured only geometry could
 * sign a template the runtime must refuse on every frame. One implementation is what keeps the
 * compiler's answer and the device's answer the same answer.
 */
[[nodiscard]] std::size_t countSlots(std::string_view pattern, PatternKind kind) noexcept;

/**
 * @brief The envelope of every reading `pattern` can ever produce, in pixels.
 *
 * The build-time half of ADR-010 decision 4's amendment, and the function that makes the runtime
 * half safe: a node that can hold this can hold any reading the pattern will draw.
 *
 * Deliberately **conservative rather than exact**. Equal digit advances stop a clock jittering but
 * do not make digit ink identical - bitmap origins and dimensions differ, and a package may carry
 * kerning - so the walk tracks the minimum and maximum pen position separately and takes the union
 * of every digit's rectangle at both. The extrema may therefore come from different readings, and a
 * box sized to this envelope holds more than any single reading needs. That is the fail-closed
 * direction for a compile-time bound.
 */
[[nodiscard]] mdux::core::Result<PatternExtent, ReadingError>
measurePattern(const mdux::font::FontPackage& font, std::string_view pattern, PatternKind kind) noexcept;

/**
 * @brief Broken-down civil time, as a device's clock service reports it.
 *
 * Fields rather than a timestamp, and that is the decision rather than a convenience. Turning an
 * epoch count into a date needs calendar arithmetic and a time zone; `std::chrono`'s zoned time
 * parses a tzdata database and allocates, which is a parser in the governed zone and therefore not
 * available (ADR-004). Every real-time clock a device carries reports broken-down fields already,
 * so this asks the host for what it has rather than for what it would have to compute.
 *
 * No validation of the *calendar* here - the 31st of February is the host's error to make and this
 * module has no basis to correct it. What is checked is what the pattern needs: a field with more
 * digits than its slots is `ValueTooLarge` rather than a truncated year.
 */
struct CivilTime {
    std::int32_t year{0};    ///< the full year, e.g. 2026
    std::uint8_t month{1};   ///< 1-12
    std::uint8_t day{1};     ///< 1-31
    std::uint8_t hour{0};    ///< 0-23
    std::uint8_t minute{0};  ///< 0-59
    std::uint8_t second{0};  ///< 0-59

    [[nodiscard]] constexpr bool operator==(const CivilTime&) const noexcept = default;
};

/**
 * @brief Records `value` into `node`, through `pattern`, as `CoverageR8` rectangles.
 *
 * @param list    the destination; rectangles are appended in pattern order
 * @param font    the font package the glyphs come from
 * @param node    the node's resolved rectangle, in surface pixels
 * @param pattern the template's rendering, e.g. `###.# mmHg`
 * @param value   the reading, in the template's own fixed-point units - `1234` for `123.4`
 * @param color   the tint; coverage modulates its alpha, never its rgb
 *
 * The value fills every digit slot in the pattern, most significant first, zero-padded to the slot
 * count. A value that does not fit is `ValueTooLarge` and **nothing is drawn** - dropping the
 * leading digits would show `99` for `199` in a box whose unit says mmHg, which is the worst thing
 * this module could do and the one that would look most like a reading.
 *
 * The ink is placed at the node's top-left corner, which is the rule `mdux.medui.screen` fixes for
 * a `Label` and for the same reason: it is the box the build-time measurement proved fits.
 *
 * Allocation-free, `noexcept`, and all-or-nothing: on any refusal the list is rolled back to where
 * it stood on entry.
 */
[[nodiscard]] mdux::core::ResultVoid<ReadingError> recordNumeric(mdux::draw::DrawList&          list,
                                                                 const mdux::font::FontPackage& font,
                                                                 const mdux::core::Rect&        node,
                                                                 std::string_view               pattern,
                                                                 std::int64_t                   value,
                                                                 mdux::core::ColorRgba8         color) noexcept;

/**
 * @brief Records `now` into `node`, through the rendering `format` fixes.
 *
 * The pattern is the artifact's rather than a caller's: `ClockFormat` is closed by the shared
 * contract (MEDUI-DEC-006) and `rendering()` returns each member's fixed shape, so a clock needs no
 * table and cannot be joined to the wrong one.
 *
 * Which field fills which slot run is written out per format rather than derived from the slot
 * letters, and that is deliberate. `YYYY-MM-DD HH:MM:SS` uses `M` for both the month and the
 * minute; any rule that read the letter alone would have to break the tie by position, which is a
 * fragile thing to infer from a string when the set of strings is closed and has two members. The
 * contract fixes the renderings, so the mapping is fixed too, and writing it out is transcription
 * rather than invention.
 */
[[nodiscard]] mdux::core::ResultVoid<ReadingError> recordClock(mdux::draw::DrawList&          list,
                                                               const mdux::font::FontPackage& font,
                                                               const mdux::core::Rect&        node,
                                                               ClockFormat                    format,
                                                               const CivilTime&               now,
                                                               mdux::core::ColorRgba8         color) noexcept;

}  // namespace mdux::medui
