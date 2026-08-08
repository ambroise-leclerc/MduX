/**
 * @file Schema.cpp
 * @brief Implementation of the governed-zone font package types.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-010 No on-device text shaping
 *
 * `validate()` carries the rules; `toJson()` and `parse()` are the two sides of one shape and are
 * deliberately written next to each other, because a writer and a reader that drift apart is the
 * failure this module exists to prevent.
 */
module;

module mdux.font.schema;

import std;
import mdux.core.result;
import mdux.evidence.json;

namespace mdux::font {

namespace json = mdux::evidence::json;

using mdux::core::err;
using mdux::core::Result;
using mdux::core::ResultVoid;

namespace {

constexpr char32_t surrogateFirst = 0xD800;
constexpr char32_t surrogateLast  = 0xDFFF;

[[nodiscard]] bool isPowerOfTwo(std::uint32_t value) noexcept {
    return value != 0 && (value & (value - 1)) == 0;
}

/// The largest Unicode scalar value. Above this is not a character at all, so a package naming
/// one is describing a glyph for something no text can contain.
constexpr char32_t maxCodePoint = 0x10FFFF;

/// Reads a required integer member, or fails with the error the caller names.
[[nodiscard]] Result<std::int64_t, SchemaError> requireInt(const json::Value& object, std::string_view key) noexcept {
    const json::Value* member = object.find(key);
    if (member == nullptr) {
        return err(SchemaError::MissingMember);
    }
    auto value = member->asInt();
    if (!value.has_value()) {
        return err(SchemaError::WrongType);
    }
    return *value;
}

[[nodiscard]] Result<std::string, SchemaError> requireString(const json::Value& object, std::string_view key) noexcept {
    const json::Value* member = object.find(key);
    if (member == nullptr) {
        return err(SchemaError::MissingMember);
    }
    auto value = member->asString();
    if (!value.has_value()) {
        return err(SchemaError::WrongType);
    }
    return std::string{*value};
}

/// Reads an integer member and checks it fits `T` *before* narrowing.
///
/// Every field below goes through this rather than `static_cast`-ing a parsed `std::int64_t`.
/// Casting first is not a rounding problem, it is a correctness one: `unitsPerEm: 65537` narrows
/// to 1 and `pixelSize: -1` wraps to 4294967295, and both then satisfy `validate()`. The package
/// would parse successfully as *different data than the JSON contains*, which is precisely the
/// guarantee this module exists to make.
///
/// The unsigned branch is separate because a `T` whose maximum exceeds `int64_t`'s cannot be
/// compared against it directly.
template <typename T>
[[nodiscard]] Result<T, SchemaError> requireIntIn(const json::Value& object, std::string_view key) noexcept {
    auto raw = requireInt(object, key);
    if (!raw.has_value()) {
        return err(raw.error());
    }
    if constexpr (std::is_unsigned_v<T>) {
        if (*raw < 0 || static_cast<std::uint64_t>(*raw) > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
            return err(SchemaError::IntegerOutOfRange);
        }
    } else {
        if (*raw < static_cast<std::int64_t>(std::numeric_limits<T>::min())
            || *raw > static_cast<std::int64_t>(std::numeric_limits<T>::max())) {
            return err(SchemaError::IntegerOutOfRange);
        }
    }
    return static_cast<T>(*raw);
}

/// Reads a code point, refusing anything that is not a Unicode scalar value.
///
/// `char32_t` is 32 bits, so the type alone permits four billion values of which only 0x110000
/// are characters. The ceiling is enforced here rather than only in `validate()` so that no
/// intermediate `FontPackage` ever holds one - and so the charset loop in `validate()` cannot be
/// handed a range whose end is near the type's maximum.
[[nodiscard]] Result<char32_t, SchemaError> requireCodePoint(const json::Value& object, std::string_view key) noexcept {
    auto raw = requireInt(object, key);
    if (!raw.has_value()) {
        return err(raw.error());
    }
    if (*raw < 0 || *raw > static_cast<std::int64_t>(maxCodePoint)) {
        return err(SchemaError::CodePointOutOfRange);
    }
    return static_cast<char32_t>(*raw);
}

/// A member that must be an array. Distinguishes absent from present-but-wrong-type, because
/// reporting `"glyphs": {}` as a missing member sends an author looking for something that is
/// right there.
[[nodiscard]] Result<const json::Value*, SchemaError> requireArray(const json::Value& object, std::string_view key) noexcept {
    const json::Value* member = object.find(key);
    if (member == nullptr) {
        return err(SchemaError::MissingMember);
    }
    if (member->kind() != json::Value::Kind::Array) {
        return err(SchemaError::WrongType);
    }
    return member;
}

}  // namespace

std::string_view describe(SchemaError error) noexcept {
    switch (error) {
        case SchemaError::NotAnObject:
            return "the font package is not a JSON object";
        case SchemaError::MissingMember:
            return "a required member is absent";
        case SchemaError::WrongType:
            return "a member has the wrong JSON type";
        case SchemaError::UnsupportedVersion:
            return "schemaVersion is not one this module reads";
        case SchemaError::WrongKind:
            return "kind is not 'font'";
        case SchemaError::EmptyId:
            return "the package id is empty";
        case SchemaError::EmptyLocales:
            return "the package approves no locale";
        case SchemaError::EmptyLocaleTag:
            return "a locale entry is empty";
        case SchemaError::DuplicateLocale:
            return "a locale is listed more than once";
        case SchemaError::UnsupportedUnitsPerEm:
            return "unitsPerEm is zero";
        case SchemaError::UnsupportedPixelSize:
            return "pixelSize is zero";
        case SchemaError::AtlasNotPowerOfTwo:
            return "the atlas width or height is not a power of two";
        case SchemaError::AtlasSizeMismatch:
            return "the atlas byteLength is not width * height";
        case SchemaError::EmptyAtlasPath:
            return "the atlas path is empty";
        case SchemaError::AtlasPathHasSeparator:
            return "the atlas path is not a bare filename";
        case SchemaError::NoGlyphs:
            return "the package contains no glyphs";
        case SchemaError::DuplicateCodePoint:
            return "two glyph records claim the same code point";
        case SchemaError::GlyphsNotSorted:
            return "glyph records are not in ascending code-point order";
        case SchemaError::SurrogateCodePoint:
            return "a code point is in the UTF-16 surrogate range, which is not a scalar value";
        case SchemaError::GlyphOutsideAtlas:
            return "a glyph's slot extends past the atlas sheet";
        case SchemaError::EmptyCharset:
            return "the restricted charset declares no ranges";
        case SchemaError::CharsetRangeDescending:
            return "a restricted-charset range ends before it begins";
        case SchemaError::CharsetRangesOverlap:
            return "two restricted-charset ranges cover the same code point";
        case SchemaError::CharsetGlyphMissing:
            return "the restricted charset names a code point the package has no glyph for";
        case SchemaError::TabularFigureMismatch:
            return "the decimal digits do not share one advance width";
        case SchemaError::KerningGlyphMissing:
            return "a kerning pair names a code point the package has no glyph for";
        case SchemaError::DuplicateKerningPair:
            return "the same ordered kerning pair appears more than once";
        case SchemaError::IntegerOutOfRange:
            return "a JSON integer does not fit the field it was read into";
        case SchemaError::CodePointOutOfRange:
            return "a code point exceeds U+10FFFF and is therefore not a Unicode scalar value";
        case SchemaError::InvalidAtlasDigest:
            return "the atlas sha256 is not 64 lowercase hexadecimal characters";
    }
    return "unknown font schema error";
}

ResultVoid<SchemaError> FontPackage::validate() const noexcept {
    if (id.empty()) {
        return err(SchemaError::EmptyId);
    }
    if (unitsPerEm == 0) {
        return err(SchemaError::UnsupportedUnitsPerEm);
    }
    if (pixelSize == 0) {
        return err(SchemaError::UnsupportedPixelSize);
    }

    if (locales.empty()) {
        return err(SchemaError::EmptyLocales);
    }
    for (std::size_t i = 0; i < locales.size(); ++i) {
        if (locales[i].empty()) {
            return err(SchemaError::EmptyLocaleTag);
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (locales[i] == locales[j]) {
                return err(SchemaError::DuplicateLocale);
            }
        }
    }

    if (atlas.path.empty()) {
        return err(SchemaError::EmptyAtlasPath);
    }
    // A bare filename, resolved beside package.json. A path with a separator would let a package
    // point at a sidecar outside its own directory, which is a package describing something other
    // than itself.
    // One ordinary filename component, checked positively rather than by banning separators.
    // Rejecting only '/' and '\\' let through ".", "..", and Windows drive-relative forms like
    // "C:atlas.bin" - each of which resolves somewhere other than beside package.json on at least
    // one supported platform.
    if (atlas.path == "." || atlas.path == ".."
        || atlas.path.find_first_of("/\\:") != std::string::npos) {
        return err(SchemaError::AtlasPathHasSeparator);
    }
    // Exactly 64 lowercase hex characters. The field's whole purpose is letting a runtime check
    // the sidecar it was handed belongs to this package; a malformed digest is one it cannot use,
    // and accepting one would mean write() could emit a package with no usable integrity value.
    if (atlas.sha256.size() != 64
        || !std::ranges::all_of(atlas.sha256, [](char c) noexcept {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           })) {
        return err(SchemaError::InvalidAtlasDigest);
    }
    if (!isPowerOfTwo(atlas.width) || !isPowerOfTwo(atlas.height)) {
        return err(SchemaError::AtlasNotPowerOfTwo);
    }
    if (atlas.byteLength != static_cast<std::uint64_t>(atlas.width) * atlas.height) {
        return err(SchemaError::AtlasSizeMismatch);
    }

    if (glyphs.empty()) {
        return err(SchemaError::NoGlyphs);
    }
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        const GlyphRecord& glyph = glyphs[i];
        if (glyph.codePoint > maxCodePoint) {
            return err(SchemaError::CodePointOutOfRange);
        }
        if (glyph.codePoint >= surrogateFirst && glyph.codePoint <= surrogateLast) {
            return err(SchemaError::SurrogateCodePoint);
        }
        if (i > 0) {
            // Sorted *and* strictly increasing, so find()'s binary search is well defined and a
            // duplicate cannot hide behind an unsorted list.
            if (glyph.codePoint == glyphs[i - 1].codePoint) {
                return err(SchemaError::DuplicateCodePoint);
            }
            if (glyph.codePoint < glyphs[i - 1].codePoint) {
                return err(SchemaError::GlyphsNotSorted);
            }
        }
        if (static_cast<std::uint64_t>(glyph.x) + glyph.width > atlas.width
            || static_cast<std::uint64_t>(glyph.y) + glyph.height > atlas.height) {
            return err(SchemaError::GlyphOutsideAtlas);
        }
    }

    // Tabular figures. Only the digits the package actually contains are compared - a charset
    // without digits is not required to invent them - but every one that is present must share
    // the advance, because a field mixing them is what jitters.
    std::optional<std::uint16_t> digitAdvance;
    for (char32_t digit = U'0'; digit <= U'9'; ++digit) {
        const GlyphRecord* glyph = find(digit);
        if (glyph == nullptr) {
            continue;
        }
        if (!digitAdvance.has_value()) {
            digitAdvance = glyph->advanceWidth;
            continue;
        }
        if (glyph->advanceWidth != *digitAdvance) {
            return err(SchemaError::TabularFigureMismatch);
        }
    }

    if (restrictedCharset.empty()) {
        return err(SchemaError::EmptyCharset);
    }
    for (std::size_t i = 0; i < restrictedCharset.size(); ++i) {
        const CharsetRange& range = restrictedCharset[i];
        if (range.last < range.first) {
            return err(SchemaError::CharsetRangeDescending);
        }
        // Checked before the membership walk below, which increments a char32_t: a range ending at
        // the type's maximum would wrap `point` to 0 and loop forever. Bounding to a scalar value
        // makes that unreachable, and the loop is written so it would terminate regardless.
        if (range.last > maxCodePoint) {
            return err(SchemaError::CodePointOutOfRange);
        }
        if (range.first <= surrogateLast && range.last >= surrogateFirst) {
            return err(SchemaError::SurrogateCodePoint);
        }
        if (i > 0 && range.first <= restrictedCharset[i - 1].last) {
            return err(SchemaError::CharsetRangesOverlap);
        }
        // Every code point the charset permits must be one the package can actually draw.
        // Without this, `permits()` would promise a glyph the runtime would then fail to find -
        // and the runtime has no fallback, because having one would mean shaping on device.
        for (char32_t point = range.first;; ++point) {
            if (find(point) == nullptr) {
                return err(SchemaError::CharsetGlyphMissing);
            }
            if (point == range.last) {
                break;  // tested here, not in the condition, so `++point` can never wrap past it
            }
        }
    }

    for (std::size_t i = 0; i < kerning.size(); ++i) {
        const KerningPair& pair = kerning[i];
        if (pair.left > maxCodePoint || pair.right > maxCodePoint) {
            return err(SchemaError::CodePointOutOfRange);
        }
        if (find(pair.left) == nullptr || find(pair.right) == nullptr) {
            return err(SchemaError::KerningGlyphMissing);
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (kerning[j].left == pair.left && kerning[j].right == pair.right) {
                return err(SchemaError::DuplicateKerningPair);
            }
        }
    }

    return {};
}

Result<json::Value, SchemaError> FontPackage::toJson() const noexcept {
    json::Value package = json::Value::emptyObject();
    const auto  set     = [&package](std::string key, json::Value value) noexcept {
        static_cast<void>(package.set(std::move(key), std::move(value)));
    };
    set("schemaVersion", json::Value::integer(schemaVersion));
    set("id", json::Value::string(id));
    set("kind", json::Value::string(std::string{packageKind}));
    set("unitsPerEm", json::Value::integer(unitsPerEm));
    set("pixelSize", json::Value::integer(pixelSize));

    std::vector<json::Value> localeValues;
    localeValues.reserve(locales.size());
    for (const std::string& tag : locales) {
        localeValues.push_back(json::Value::string(tag));
    }
    set("locales", json::Value::array(std::move(localeValues)));

    json::Value atlasValue = json::Value::emptyObject();
    static_cast<void>(atlasValue.set("path", json::Value::string(atlas.path)));
    static_cast<void>(atlasValue.set("width", json::Value::integer(atlas.width)));
    static_cast<void>(atlasValue.set("height", json::Value::integer(atlas.height)));
    static_cast<void>(atlasValue.set("byteLength", json::Value::integer(static_cast<std::int64_t>(atlas.byteLength))));
    static_cast<void>(atlasValue.set("sha256", json::Value::string(atlas.sha256)));
    static_cast<void>(atlasValue.set("occupancyPercent", json::Value::integer(atlas.occupancyPercent)));
    set("atlas", std::move(atlasValue));

    std::vector<json::Value> glyphValues;
    glyphValues.reserve(glyphs.size());
    for (const GlyphRecord& glyph : glyphs) {
        json::Value entry = json::Value::emptyObject();
        static_cast<void>(entry.set("codePoint", json::Value::integer(glyph.codePoint)));
        static_cast<void>(entry.set("glyphIndex", json::Value::integer(glyph.glyphIndex)));
        static_cast<void>(entry.set("advanceWidth", json::Value::integer(glyph.advanceWidth)));
        static_cast<void>(entry.set("leftSideBearing", json::Value::integer(glyph.leftSideBearing)));
        static_cast<void>(entry.set("x", json::Value::integer(glyph.x)));
        static_cast<void>(entry.set("y", json::Value::integer(glyph.y)));
        static_cast<void>(entry.set("width", json::Value::integer(glyph.width)));
        static_cast<void>(entry.set("height", json::Value::integer(glyph.height)));
        static_cast<void>(entry.set("bitmapOriginX", json::Value::integer(glyph.bitmapOriginX)));
        static_cast<void>(entry.set("bitmapOriginY", json::Value::integer(glyph.bitmapOriginY)));
        glyphValues.push_back(std::move(entry));
    }
    set("glyphs", json::Value::array(std::move(glyphValues)));

    std::vector<json::Value> kerningValues;
    kerningValues.reserve(kerning.size());
    for (const KerningPair& pair : kerning) {
        json::Value entry = json::Value::emptyObject();
        static_cast<void>(entry.set("left", json::Value::integer(pair.left)));
        static_cast<void>(entry.set("right", json::Value::integer(pair.right)));
        static_cast<void>(entry.set("adjustment", json::Value::integer(pair.adjustment)));
        kerningValues.push_back(std::move(entry));
    }
    set("kerning", json::Value::array(std::move(kerningValues)));

    std::vector<json::Value> charsetValues;
    charsetValues.reserve(restrictedCharset.size());
    for (const CharsetRange& range : restrictedCharset) {
        json::Value entry = json::Value::emptyObject();
        static_cast<void>(entry.set("first", json::Value::integer(range.first)));
        static_cast<void>(entry.set("last", json::Value::integer(range.last)));
        charsetValues.push_back(std::move(entry));
    }
    set("restrictedCharset", json::Value::array(std::move(charsetValues)));

    return package;
}

Result<std::string, SchemaError> FontPackage::write() const noexcept {
    if (auto valid = validate(); !valid.has_value()) {
        return err(valid.error());
    }
    auto value = toJson();
    if (!value.has_value()) {
        return err(value.error());
    }
    auto text = json::write(*value);
    if (!text.has_value()) {
        return err(SchemaError::WrongType);
    }
    return *text;
}

Result<FontPackage, SchemaError> FontPackage::parse(std::string_view text) noexcept {
    auto document = json::parse(text);
    if (!document.has_value()) {
        return err(SchemaError::NotAnObject);
    }
    if (document->kind() != json::Value::Kind::Object) {
        return err(SchemaError::NotAnObject);
    }

    auto version = requireInt(*document, "schemaVersion");
    if (!version.has_value()) {
        return err(version.error());
    }
    if (*version != schemaVersion) {
        return err(SchemaError::UnsupportedVersion);
    }
    auto kind = requireString(*document, "kind");
    if (!kind.has_value()) {
        return err(kind.error());
    }
    if (*kind != packageKind) {
        return err(SchemaError::WrongKind);
    }

    FontPackage package;
    auto        packageId = requireString(*document, "id");
    if (!packageId.has_value()) {
        return err(packageId.error());
    }
    package.id = std::move(*packageId);

    // Every numeric field goes through requireIntIn<T>, which range-checks before narrowing.
    // See its comment: casting a parsed int64 first is not a rounding problem but a correctness
    // one, because the result validates as different data than the JSON contains.
    auto upem = requireIntIn<std::uint16_t>(*document, "unitsPerEm");
    if (!upem.has_value()) {
        return err(upem.error());
    }
    package.unitsPerEm = *upem;
    auto pixels        = requireIntIn<std::uint32_t>(*document, "pixelSize");
    if (!pixels.has_value()) {
        return err(pixels.error());
    }
    package.pixelSize = *pixels;

    auto locales = requireArray(*document, "locales");
    if (!locales.has_value()) {
        return err(locales.error());
    }
    for (const json::Value& entry : (*locales)->elements()) {
        auto tag = entry.asString();
        if (!tag.has_value()) {
            return err(SchemaError::WrongType);
        }
        package.locales.emplace_back(*tag);
    }

    const json::Value* atlasValue = document->find("atlas");
    if (atlasValue == nullptr) {
        return err(SchemaError::MissingMember);
    }
    if (atlasValue->kind() != json::Value::Kind::Object) {
        return err(SchemaError::WrongType);
    }
    auto atlasPath = requireString(*atlasValue, "path");
    if (!atlasPath.has_value()) {
        return err(atlasPath.error());
    }
    package.atlas.path = std::move(*atlasPath);
    auto atlasSha      = requireString(*atlasValue, "sha256");
    if (!atlasSha.has_value()) {
        return err(atlasSha.error());
    }
    package.atlas.sha256 = std::move(*atlasSha);
    {
        auto width     = requireIntIn<std::uint32_t>(*atlasValue, "width");
        auto height    = requireIntIn<std::uint32_t>(*atlasValue, "height");
        auto length    = requireIntIn<std::uint64_t>(*atlasValue, "byteLength");
        auto occupancy = requireIntIn<std::uint32_t>(*atlasValue, "occupancyPercent");
        // Reported individually rather than collapsed, so a diagnostic distinguishes an absent
        // member from one that is present but the wrong type or out of range.
        if (!width.has_value()) {
            return err(width.error());
        }
        if (!height.has_value()) {
            return err(height.error());
        }
        if (!length.has_value()) {
            return err(length.error());
        }
        if (!occupancy.has_value()) {
            return err(occupancy.error());
        }
        package.atlas.width            = *width;
        package.atlas.height           = *height;
        package.atlas.byteLength       = *length;
        package.atlas.occupancyPercent = *occupancy;
    }

    auto glyphs = requireArray(*document, "glyphs");
    if (!glyphs.has_value()) {
        return err(glyphs.error());
    }
    for (const json::Value& entry : (*glyphs)->elements()) {
        if (entry.kind() != json::Value::Kind::Object) {
            return err(SchemaError::WrongType);
        }
        GlyphRecord glyph;
        auto        point   = requireCodePoint(entry, "codePoint");
        if (!point.has_value()) {
            return err(point.error());
        }
        auto index = requireIntIn<std::uint16_t>(entry, "glyphIndex");
        if (!index.has_value()) {
            return err(index.error());
        }
        auto advance = requireIntIn<std::uint16_t>(entry, "advanceWidth");
        if (!advance.has_value()) {
            return err(advance.error());
        }
        auto bearing = requireIntIn<std::int16_t>(entry, "leftSideBearing");
        if (!bearing.has_value()) {
            return err(bearing.error());
        }
        auto x = requireIntIn<std::uint32_t>(entry, "x");
        if (!x.has_value()) {
            return err(x.error());
        }
        auto y = requireIntIn<std::uint32_t>(entry, "y");
        if (!y.has_value()) {
            return err(y.error());
        }
        auto width = requireIntIn<std::uint32_t>(entry, "width");
        if (!width.has_value()) {
            return err(width.error());
        }
        auto height = requireIntIn<std::uint32_t>(entry, "height");
        if (!height.has_value()) {
            return err(height.error());
        }
        auto originX = requireIntIn<std::int32_t>(entry, "bitmapOriginX");
        if (!originX.has_value()) {
            return err(originX.error());
        }
        auto originY = requireIntIn<std::int32_t>(entry, "bitmapOriginY");
        if (!originY.has_value()) {
            return err(originY.error());
        }
        glyph.codePoint       = *point;
        glyph.glyphIndex      = *index;
        glyph.advanceWidth    = *advance;
        glyph.leftSideBearing = *bearing;
        glyph.x               = *x;
        glyph.y               = *y;
        glyph.width           = *width;
        glyph.height          = *height;
        glyph.bitmapOriginX   = *originX;
        glyph.bitmapOriginY   = *originY;
        package.glyphs.push_back(glyph);
    }

    auto kerning = requireArray(*document, "kerning");
    if (!kerning.has_value()) {
        return err(kerning.error());
    }
    for (const json::Value& entry : (*kerning)->elements()) {
        if (entry.kind() != json::Value::Kind::Object) {
            return err(SchemaError::WrongType);
        }
        auto left = requireCodePoint(entry, "left");
        if (!left.has_value()) {
            return err(left.error());
        }
        auto right = requireCodePoint(entry, "right");
        if (!right.has_value()) {
            return err(right.error());
        }
        auto adjustment = requireIntIn<std::int16_t>(entry, "adjustment");
        if (!adjustment.has_value()) {
            return err(adjustment.error());
        }
        package.kerning.push_back(KerningPair{.left = *left, .right = *right, .adjustment = *adjustment});
    }

    auto charset = requireArray(*document, "restrictedCharset");
    if (!charset.has_value()) {
        return err(charset.error());
    }
    for (const json::Value& entry : (*charset)->elements()) {
        if (entry.kind() != json::Value::Kind::Object) {
            return err(SchemaError::WrongType);
        }
        auto first = requireCodePoint(entry, "first");
        if (!first.has_value()) {
            return err(first.error());
        }
        auto last = requireCodePoint(entry, "last");
        if (!last.has_value()) {
            return err(last.error());
        }
        package.restrictedCharset.push_back(CharsetRange{.first = *first, .last = *last});
    }

    // Validated on the way out, so nobody can hold an unchecked FontPackage.
    if (auto valid = package.validate(); !valid.has_value()) {
        return err(valid.error());
    }
    return package;
}

const GlyphRecord* FontPackage::find(char32_t point) const noexcept {
    const auto it = std::lower_bound(glyphs.begin(), glyphs.end(), point,
                                     [](const GlyphRecord& glyph, char32_t value) noexcept { return glyph.codePoint < value; });
    if (it == glyphs.end() || it->codePoint != point) {
        return nullptr;
    }
    return &*it;
}

bool FontPackage::permits(char32_t point) const noexcept {
    const auto it = std::upper_bound(restrictedCharset.begin(), restrictedCharset.end(), point,
                                     [](char32_t value, const CharsetRange& range) noexcept { return value < range.first; });
    if (it == restrictedCharset.begin()) {
        return false;
    }
    return std::prev(it)->contains(point);
}

std::int16_t FontPackage::kerningFor(char32_t left, char32_t right) const noexcept {
    for (const KerningPair& pair : kerning) {
        if (pair.left == left && pair.right == right) {
            return pair.adjustment;
        }
    }
    return 0;
}

}  // namespace mdux::font
