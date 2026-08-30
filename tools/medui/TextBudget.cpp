/**
 * @file TextBudget.cpp
 * @brief Widest-approved-translation measurement and dynamic-text charset validation.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-010 No on-device text shaping
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 */
module;

module mdux.tools.medui.textbudget;

import std;
import mdux.font.schema;
import mdux.medui.schema;
import mdux.text.draw;
import mdux.text.schema;
import mdux.tools.cli;
import mdux.tools.medui.ast;
import mdux.tools.medui.diagnostics;
import mdux.tools.medui.layout;
import mdux.tools.medui.semantic;

namespace mdux::tools::medui {

namespace {

/// One component field whose value names a dynamic-text source.
///
/// Both entries are `Identifier`-domain fields in the shared component model, and both describe
/// text the baker never positioned. `NumericDisplay`'s `template:` and every quoted `source:` are
/// absent on purpose - see the module comment for why a product identifier cannot be expanded into
/// code points here.
struct DynamicField {
    std::string_view component;
    std::string_view field;
};

// `Clock` is deliberately absent. Its `format:` is a member of a closed set whose rendering the
// shared contract fixes (MEDUI-DEC-006), so it is *measured* by `checkClockFormat()` rather than
// resolved through a product-supplied table. `TextInput`'s `charset:` stays here because a glyph
// set is still an open name, resolved against the packages a build bakes.
constexpr std::array dynamicFields{DynamicField{.component = "TextInput", .field = "charset"}};

[[nodiscard]] bool namesDynamicText(std::string_view component, std::string_view field) noexcept {
    return std::ranges::any_of(dynamicFields, [component, field](const DynamicField& entry) {
        return entry.component == component && entry.field == field;
    });
}

/// Finds a component in the dictionary semantic analysis published, rather than a second copy of it.
[[nodiscard]] const ComponentRule* ruleFor(std::string_view component) noexcept {
    const std::span<const ComponentRule> dictionary = componentDictionary();
    const auto                           found      = std::ranges::find(dictionary, component, &ComponentRule::name);
    return found == dictionary.end() ? nullptr : &*found;
}

[[nodiscard]] const FieldRule* ruleFor(const ComponentRule& component, std::string_view field) noexcept {
    const auto found = std::ranges::find(component.fields, field, &FieldRule::name);
    return found == component.fields.end() ? nullptr : &*found;
}

/**
 * @brief The first code point in an ascending range that is not a Unicode scalar value, if any.
 *
 * Two shapes are not characters and cannot be drawn by anything: the surrogate block U+D800..U+DFFF,
 * which exists only to encode other code points in UTF-16, and anything past U+10FFFF. The lowest
 * one a range names is returned, so the diagnostic points at the first offending code point rather
 * than at whichever rule happened to be tested first.
 *
 * `range` is assumed ascending; a descending range names nothing and is the caller's to skip.
 */
[[nodiscard]] std::optional<std::uint32_t> firstNonScalar(mdux::font::CharsetRange range) noexcept {
    constexpr std::uint32_t firstSurrogate  = 0xD800;
    constexpr std::uint32_t lastSurrogate   = 0xDFFF;
    constexpr std::uint32_t lastScalarValue = 0x10FFFF;

    const auto first = static_cast<std::uint32_t>(range.first);
    const auto last  = static_cast<std::uint32_t>(range.last);

    if (first <= lastSurrogate && last >= firstSurrogate) {
        return std::max(first, firstSurrogate);
    }
    if (last > lastScalarValue) {
        return std::max(first, lastScalarValue + 1);
    }
    return std::nullopt;
}

/**
 * @brief Measures the ink one baked run paints.
 *
 * The decode is `mdux::text::draw::decodeRecord()` and the placement arithmetic is
 * `addGlyphRect()`'s: the pen plus the glyph's `bitmapOriginX`, and `bitmapOriginY` *above* the
 * record's baseline. Restating either would be the second path this stage exists to avoid, so what
 * is restated is only the part that has no shared helper - taking the union of the rectangles.
 *
 * Blank glyphs are skipped exactly as `recordRun()` skips them: they paint nothing, so they cannot
 * overflow a box visibly, and counting a trailing space as ink would reject screens that render
 * correctly.
 */
[[nodiscard]] TextExtent measureRun(const mdux::font::FontPackage& fontPackage, std::span<const std::byte> records) {
    if (records.size() % mdux::text::draw::recordSize != 0) {
        throw std::logic_error(
            std::format("text budget received {} bytes, which is not a whole number of {}-byte run records", records.size(), mdux::text::draw::recordSize));
    }

    bool         inked{false};
    std::int64_t left{0};
    std::int64_t right{0};
    std::int64_t top{0};
    std::int64_t bottom{0};

    for (std::size_t offset = 0; offset < records.size(); offset += mdux::text::draw::recordSize) {
        const auto placement = mdux::text::draw::decodeRecord(records.subspan(offset, mdux::text::draw::recordSize));
        if (!placement.has_value()) {
            throw std::logic_error(
                std::format("text budget could not decode the run record at byte {}: {}", offset, mdux::text::draw::describe(placement.error())));
        }
        if (placement->packageIndex >= fontPackage.glyphs.size()) {
            throw std::logic_error(std::format("run record at byte {} names glyph {} but font package '{}' holds {}",
                                               offset,
                                               placement->packageIndex,
                                               fontPackage.id,
                                               fontPackage.glyphs.size()));
        }

        const mdux::font::GlyphRecord& glyph = fontPackage.glyphs[placement->packageIndex];
        if (glyph.isBlank()) {
            continue;
        }

        const std::int64_t glyphLeft   = std::int64_t{placement->x} + glyph.bitmapOriginX;
        const std::int64_t glyphTop    = std::int64_t{placement->y} - glyph.bitmapOriginY;
        const std::int64_t glyphRight  = glyphLeft + glyph.width;
        const std::int64_t glyphBottom = glyphTop + glyph.height;

        if (!inked) {
            inked  = true;
            left   = glyphLeft;
            right  = glyphRight;
            top    = glyphTop;
            bottom = glyphBottom;
            continue;
        }
        left   = std::min(left, glyphLeft);
        right  = std::max(right, glyphRight);
        top    = std::min(top, glyphTop);
        bottom = std::max(bottom, glyphBottom);
    }

    if (!inked) {
        return TextExtent{};
    }
    return TextExtent{.width = right - left, .height = bottom - top};
}

/// One fail-closed budget pass over a resolved screen.
class Checker {
public:
    /// Captures the diagnostic path and the packages a screen is measured against.
    Checker(std::string file, TextBudgetInputs inputs) : file_{std::move(file)}, inputs_{inputs} {
        if (inputs_.font == nullptr) {
            throw std::logic_error("text budget requires the font package the approved locales were baked into");
        }
        checkFontCharset();
        checkLocaleWiring();
    }

    /// Checks one resolved screen, returning no measurements on any diagnostic.
    [[nodiscard]] TextBudgetResult run(const LayoutResult& layout) {
        for (const ResolvedNode& node : layout.nodes) {
            checkNode(node);
        }
        if (!diagnostics_.empty()) {
            measurements_.clear();
        }
        return TextBudgetResult{.measurements = std::move(measurements_), .diagnostics = std::move(diagnostics_)};
    }

private:
    /**
     * @brief Checks that the font package's own charset table is walkable, once.
     *
     * The charset walk in `checkDynamicText()` advances by jumping to the end of the range that
     * admitted a code point. That is only a *step* if the range ends somewhere below the type's
     * maximum: a range ending at `0xFFFFFFFF` would make `last + 1` wrap to zero and select the same
     * range forever, so a malformed table would hang the compiler rather than fail it.
     *
     * `FontPackage::validate()` rejects such a range, but this stage takes the package by pointer
     * and never re-validates it - the same reason the produced ranges are treated as untrusted.
     * Trusting one table while distrusting the other was the inconsistency this check removes.
     *
     * Only what the walk relies on is checked: an ascending range that stops at a Unicode scalar
     * value. Sortedness, overlap and glyph coverage belong to `validate()` and are not restated
     * here, because a second partial copy of that contract is worse than none.
     */
    void checkFontCharset() const {
        constexpr std::uint32_t lastScalarValue = 0x10FFFF;
        for (const mdux::font::CharsetRange& range : inputs_.font->restrictedCharset) {
            if (range.last < range.first) {
                throw std::logic_error(std::format("font package '{}' declares a charset range from U+{:04X} down to U+{:04X}",
                                                   inputs_.font->id,
                                                   static_cast<std::uint32_t>(range.first),
                                                   static_cast<std::uint32_t>(range.last)));
            }
            if (static_cast<std::uint32_t>(range.last) > lastScalarValue) {
                throw std::logic_error(std::format("font package '{}' declares a charset range ending at U+{:04X}, past the last Unicode scalar value",
                                                   inputs_.font->id,
                                                   static_cast<std::uint32_t>(range.last)));
            }
        }
    }

    /**
     * @brief Checks the supplied locales against the set the font package approves, once.
     *
     * A budget is a claim about the *worst* approved translation, so the set it was measured over
     * has to be the approved one. Accepting a subset would let a caller hand over the locale it was
     * looking at and receive a screen certified against a set nobody approved - and the omitted
     * translation is precisely the one that overflows, because a locale nobody measured is a locale
     * nobody sized for.
     *
     * Each package's own wiring is checked here too, rather than at the point some key happens to
     * be measured: identity against the font, and a sidecar whose size is the one the package
     * declares. A truncated sidecar that still contains the first run would otherwise measure that
     * run and mismeasure everything after it, which is the failure this check exists to make loud.
     */
    void checkLocaleWiring() const {
        std::vector<std::string_view> supplied;
        supplied.reserve(inputs_.locales.size());

        for (const LocaleText& locale : inputs_.locales) {
            if (locale.package == nullptr) {
                throw std::logic_error("text budget received an approved locale with no text package");
            }
            const std::string_view tag = locale.package->locale;

            if (locale.package->atlasId != inputs_.font->id) {
                throw std::logic_error(
                    std::format("locale '{}' was baked against font package '{}', not '{}'", tag, locale.package->atlasId, inputs_.font->id));
            }
            if (locale.sidecar.size() != locale.package->sidecarByteLength) {
                throw std::logic_error(std::format("locale '{}' declares a {}-byte sidecar and was given {} bytes",
                                                   tag,
                                                   locale.package->sidecarByteLength,
                                                   locale.sidecar.size()));
            }
            if (std::ranges::any_of(supplied, [tag](std::string_view seen) {
                    return seen == tag;
                })) {
                throw std::logic_error(std::format("locale '{}' was supplied twice", tag));
            }
            if (!std::ranges::any_of(inputs_.font->locales, [tag](std::string_view approved) {
                    return approved == tag;
                })) {
                throw std::logic_error(std::format("locale '{}' is not approved by font package '{}'", tag, inputs_.font->id));
            }
            supplied.push_back(tag);
        }

        for (const std::string& approved : inputs_.font->locales) {
            if (!std::ranges::any_of(supplied, [&approved](std::string_view seen) {
                    return seen == approved;
                })) {
                throw std::logic_error(std::format("font package '{}' approves locale '{}', which was not supplied; a "
                                                   "budget measured over a subset of the approved locales is not a budget",
                                                   inputs_.font->id,
                                                   approved));
            }
        }
    }

    void report(Code code, ast::Position position, std::string message) {
        diagnostics_.push_back(diagnose(code, file_, position.line, position.column, std::move(message)));
    }

    void checkNode(const ResolvedNode& node) {
        // An unknown component is MEDUI-E011 and belongs to semantic analysis; a synthetic Row
        // background resolves to its originating Row, which carries no text.
        const ComponentRule* component = ruleFor(node.source.component);
        if (component == nullptr) {
            return;
        }

        for (const ast::Field& field : node.source.fields) {
            const FieldRule* fieldRule = ruleFor(*component, field.name);
            if (fieldRule == nullptr || field.value == nullptr) {
                continue;
            }
            if (fieldRule->domain == FieldDomain::TextKey) {
                checkTextKey(node, field, *field.value);
            } else if (fieldRule->domain == FieldDomain::TextKeyList) {
                for (const std::shared_ptr<ast::Value>& element : field.value->list) {
                    if (element != nullptr) {
                        checkTextKey(node, field, *element);
                    }
                }
            } else if (node.source.component == "Clock" && field.name == "format") {
                checkClockFormat(node, field, *field.value);
            } else if (namesDynamicText(node.source.component, field.name)) {
                checkDynamicText(field, *field.value);
            }
        }
    }

    /// Measures one key in every approved locale, reporting each locale that does not fit.
    void checkTextKey(const ResolvedNode& node, const ast::Field& field, const ast::Value& value) {
        if (value.kind != ast::ValueKind::TextKey) {
            // MEDUI-E017 or MEDUI-E033, already reported by analyze(): there is no key to measure.
            return;
        }
        if (inputs_.locales.empty()) {
            throw std::logic_error(std::format("text budget was given no approved locale to measure text key '{}' against", value.text));
        }

        TextExtent  worst{};
        std::string widestLocale;
        // An explicit "nothing measured yet" flag rather than `widestLocale.empty()`. The tag would
        // read as a sentinel only for as long as no approved locale has an empty one - and this
        // stage takes packages by pointer without re-validating them, so an unvalidated package
        // with an empty `locale` would make every later locale overwrite the widest measurement
        // with a narrower one. Under-reporting the worst case is the one direction a budget check
        // must not fail in.
        bool measured{false};

        for (const LocaleText& locale : inputs_.locales) {
            const TextExtent extent = measure(locale, value.text);

            if (extent.width > node.bounds.width) {
                report(Code::TextBudgetExceeded,
                       value.position,
                       std::format("text key '{}' in locale '{}' needs {}px of width, and '{}' resolved to {}px",
                                   value.text,
                                   locale.package->locale,
                                   extent.width,
                                   node.id,
                                   node.bounds.width));
            }
            if (extent.height > node.bounds.height) {
                report(Code::TextBudgetExceeded,
                       value.position,
                       std::format("text key '{}' in locale '{}' needs {}px of height, and '{}' resolved to {}px",
                                   value.text,
                                   locale.package->locale,
                                   extent.height,
                                   node.id,
                                   node.bounds.height));
            }

            if (!measured || extent.width > worst.width) {
                worst.width  = extent.width;
                widestLocale = locale.package->locale;
            }
            worst.height = std::max(worst.height, extent.height);
            measured     = true;
        }

        measurements_.push_back(
            TextMeasurement{.nodeId = node.id, .field = field.name, .textKey = value.text, .locale = std::move(widestLocale), .extent = worst});
    }

    /// Measures one key against one locale's committed package.
    ///
    /// The package's identity, and that its sidecar is whole, were settled by `checkLocaleWiring()`
    /// before any node was read. What is left here is the range this key actually names.
    [[nodiscard]] TextExtent measure(const LocaleText& locale, std::string_view key) const {
        const mdux::text::TextRun* run = locale.package->find(key);
        if (run == nullptr) {
            throw std::logic_error(std::format("text key '{}' has no run in approved locale '{}'; semantic analysis is a required gate before the budget check",
                                               key,
                                               locale.package->locale));
        }
        if (run->byteEnd() > locale.sidecar.size()) {
            throw std::logic_error(std::format("locale '{}' declares run '{}' at bytes {}..{} of a sidecar that is {} bytes long",
                                               locale.package->locale,
                                               key,
                                               run->byteOffset,
                                               run->byteEnd(),
                                               locale.sidecar.size()));
        }

        return measureRun(*inputs_.font, locale.sidecar.subspan(static_cast<std::size_t>(run->byteOffset), static_cast<std::size_t>(run->byteLength)));
    }

    /**
     * @brief Measures a clock against its node, from the rendering its format fixes.
     *
     * This is what closing `ClockFormat` bought. The contract pins each member's rendering, so the
     * widest string a clock can draw is known here - `HH:MM:SS` is six digits and two colons - and
     * the box can be checked at compile time instead of being trusted.
     *
     * The measurement is exact rather than an upper bound because the font package guarantees
     * tabular figures: `FontPackage::validate()` requires every decimal digit it contains to share
     * one advance width, so "the widest digit" is *the* digit. Without that rule this would have to
     * assume the widest glyph in the set and would reject boxes that fit.
     */
    void checkClockFormat(const ResolvedNode& node, const ast::Field& field, const ast::Value& value) {
        if (value.kind != ast::ValueKind::Identifier) {
            return;  // MEDUI-E033, already reported by analyze().
        }
        const auto format = mdux::medui::clockFormatFromWire(value.text);
        if (!format.has_value()) {
            return;  // MEDUI-E034, already reported by analyze().
        }

        std::int64_t width{0};
        std::int64_t height{0};
        for (const char character : mdux::medui::rendering(*format)) {
            // A digit position can hold any decimal digit, so it is measured as the widest one -
            // which tabular figures make a single value. Every other character in a rendering is a
            // literal separator and measures as itself.
            const char32_t point = (character >= '0' && character <= '9') ? U'0' : static_cast<char32_t>(character);
            const auto     glyph = std::ranges::find(inputs_.font->glyphs, point, &mdux::font::GlyphRecord::codePoint);
            if (glyph == inputs_.font->glyphs.end()) {
                report(Code::CharsetEscape,
                       value.position,
                       std::format("format '{}' renders U+{:04X}, which font package '{}' cannot draw",
                                   value.text,
                                   static_cast<std::uint32_t>(point),
                                   inputs_.font->id));
                return;
            }
            width  += static_cast<std::int64_t>(glyph->advanceWidth);
            height  = std::max(height, static_cast<std::int64_t>(glyph->height));
        }

        // Font units to pixels, the same conversion `measureRun()` rests on. Integer throughout:
        // this stage decides a compile outcome, and a rounding difference between host toolchains
        // would make that outcome depend on the compiler.
        const auto units      = static_cast<std::int64_t>(inputs_.font->unitsPerEm);
        const auto pixelSize  = static_cast<std::int64_t>(inputs_.font->pixelSize);
        const std::int64_t px = units == 0 ? 0 : (width * pixelSize) / units;

        if (px > node.bounds.width) {
            report(Code::TextBudgetExceeded,
                   value.position,
                   std::format("a '{}' clock renders '{}', which needs {}px of width, and '{}' resolved to {}px",
                               value.text,
                               mdux::medui::rendering(*format),
                               px,
                               node.id,
                               node.bounds.width));
        }
        if (height > node.bounds.height) {
            report(Code::TextBudgetExceeded,
                   value.position,
                   std::format("a '{}' clock renders '{}', which needs {}px of height, and '{}' resolved to {}px",
                               value.text,
                               mdux::medui::rendering(*format),
                               height,
                               node.id,
                               node.bounds.height));
        }
        static_cast<void>(field);
    }

    /// Checks that a named dynamic-text source can only produce glyphs the font package holds.
    void checkDynamicText(const ast::Field& field, const ast::Value& value) {
        if (value.kind != ast::ValueKind::Identifier) {
            // MEDUI-E033's, reported by analyze(): there is no name to resolve.
            return;
        }

        // Searched as a `string_view` on both sides: the projection yields one, and comparing it
        // with the AST's `std::string` would rest on a heterogeneous comparison the range concepts
        // do not owe us.
        const auto found = std::ranges::find(inputs_.dynamicText, std::string_view{value.text}, &DynamicTextRule::name);
        if (found == inputs_.dynamicText.end()) {
            report(Code::CharsetEscape,
                   value.position,
                   std::format("'{}' is not in the governed dynamic-text table, so what '{}' can produce is not bounded", value.text, field.name));
            return;
        }

        // One diagnostic per field rather than per code point: a charset that escapes usually
        // escapes over a whole range, and an author fixes the range rather than each character.
        //
        // The walk counts in `std::uint32_t` rather than `char32_t` because a supplied range is not
        // `FontPackage::validate()`'d: `last` may be anything the type holds, and `point <= last`
        // with `last` at the type's maximum would never terminate. `firstNonScalar()` below bounds
        // every range that reaches the walk to U+10FFFF, so the counting type is now belt as well as
        // braces - and it stays, because the bound is one edit away from being someone else's
        // assumption. A descending range produces nothing and is skipped rather than reported: what
        // a `.medui` author wrote is a name, and the shape of the table behind it is not theirs to
        // fix.
        for (const mdux::font::CharsetRange& range : found->produces) {
            if (range.last < range.first) {
                continue;
            }

            // A range that names something which is not a character is reported before it is
            // walked, and reported as what it is. Two shapes qualify: a code point past U+10FFFF,
            // and the surrogate block, which exists only to encode other code points in UTF-16 and
            // is not a scalar value either. Asking `permits()` about those would be asking the
            // wrong question - a font package cannot draw them whatever its table claims, and a
            // package whose table did claim them would answer yes.
            if (const auto offending = firstNonScalar(range)) {
                report(Code::CharsetEscape,
                       value.position,
                       std::format("'{}' can produce U+{:04X}, which is not a Unicode scalar value", value.text, *offending));
                return;
            }

            const auto    last  = static_cast<std::uint32_t>(range.last);
            std::uint32_t point = static_cast<std::uint32_t>(range.first);

            while (point <= last) {
                if (!inputs_.font->permits(static_cast<char32_t>(point))) {
                    report(Code::CharsetEscape,
                           value.position,
                           std::format("'{}' can produce U+{:04X}, which font package '{}' cannot draw", value.text, point, inputs_.font->id));
                    return;
                }
                // `permits()` decides; this only chooses the next point worth asking about. A
                // produced range covering the whole plane would otherwise cost a million binary
                // searches per field, where the answer changes only at a charset-range edge.
                const auto covering = std::ranges::find_if(inputs_.font->restrictedCharset, [point](const mdux::font::CharsetRange& admitted) {
                    return admitted.contains(static_cast<char32_t>(point));
                });
                // The step is strictly forward, and both branches are bounded: `checkFontCharset()`
                // has established that every charset range ends at a scalar value, so `last + 1`
                // cannot wrap, and `point` is itself bounded by `last` above. `permits()` just said
                // yes, so a covering range exists; stepping by one where it does not keeps the walk
                // finite rather than trusting two lookups to agree.
                point = covering == inputs_.font->restrictedCharset.end() ? point + 1 : static_cast<std::uint32_t>(covering->last) + 1;
            }
        }
    }

    std::string                  file_;
    TextBudgetInputs             inputs_;
    std::vector<TextMeasurement> measurements_;
    std::vector<cli::Diagnostic> diagnostics_;
};

}  // namespace

TextBudgetResult checkTextBudgets(const LayoutResult& layout, std::string file, TextBudgetInputs inputs) {
    return Checker{std::move(file), inputs}.run(layout);
}


bool needsTextBudget(const ast::Screen& screen) {
    // Walks the authored tree rather than a resolved layout: the question is asked before layout
    // runs, because its answer decides whether the recipe is complete enough to compile at all.
    const auto carriesMeasurableText = [](const ast::Node& node) {
        for (const ast::Field& field : node.fields) {
            if (field.value == nullptr) {
                continue;
            }
            if (field.value->kind == ast::ValueKind::TextKey) {
                return true;
            }
            if (field.value->kind == ast::ValueKind::List) {
                for (const std::shared_ptr<ast::Value>& element : field.value->list) {
                    if (element != nullptr && element->kind == ast::ValueKind::TextKey) {
                        return true;
                    }
                }
            }
            // Asked through the same predicate the measuring pass uses, so a third dynamic-text
            // field reaches this question without anyone having to remember it.
            if (namesDynamicText(node.component, field.name)) {
                return true;
            }
        }
        return false;
    };

    for (const ast::Node& node : screen.nodes) {
        if (carriesMeasurableText(node)) {
            return true;
        }
        for (const ast::Node& child : node.children) {
            if (carriesMeasurableText(child)) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace mdux::tools::medui
