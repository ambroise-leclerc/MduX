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

[[nodiscard]] const json::Value* requireArray(const json::Value& object, std::string_view key) noexcept {
    const json::Value* member = object.find(key);
    if (member == nullptr || member->kind() != json::Value::Kind::Array) {
        return nullptr;
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
    if (atlas.path.find('/') != std::string::npos || atlas.path.find('\\') != std::string::npos) {
        return err(SchemaError::AtlasPathHasSeparator);
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
        if (range.first <= surrogateLast && range.last >= surrogateFirst) {
            return err(SchemaError::SurrogateCodePoint);
        }
        if (i > 0 && range.first <= restrictedCharset[i - 1].last) {
            return err(SchemaError::CharsetRangesOverlap);
        }
        // Every code point the charset permits must be one the package can actually draw.
        // Without this, `permits()` would promise a glyph the runtime would then fail to find -
        // and the runtime has no fallback, because having one would mean shaping on device.
        for (char32_t point = range.first; point <= range.last; ++point) {
            if (find(point) == nullptr) {
                return err(SchemaError::CharsetGlyphMissing);
            }
        }
    }

    for (std::size_t i = 0; i < kerning.size(); ++i) {
        const KerningPair& pair = kerning[i];
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

    auto upem = requireInt(*document, "unitsPerEm");
    if (!upem.has_value()) {
        return err(upem.error());
    }
    package.unitsPerEm = static_cast<std::uint16_t>(*upem);
    auto pixels        = requireInt(*document, "pixelSize");
    if (!pixels.has_value()) {
        return err(pixels.error());
    }
    package.pixelSize = static_cast<std::uint32_t>(*pixels);

    const json::Value* locales = requireArray(*document, "locales");
    if (locales == nullptr) {
        return err(SchemaError::MissingMember);
    }
    for (const json::Value& entry : locales->elements()) {
        auto tag = entry.asString();
        if (!tag.has_value()) {
            return err(SchemaError::WrongType);
        }
        package.locales.emplace_back(*tag);
    }

    const json::Value* atlasValue = document->find("atlas");
    if (atlasValue == nullptr || atlasValue->kind() != json::Value::Kind::Object) {
        return err(SchemaError::MissingMember);
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
        auto width = requireInt(*atlasValue, "width");
        auto height = requireInt(*atlasValue, "height");
        auto length = requireInt(*atlasValue, "byteLength");
        auto occupancy = requireInt(*atlasValue, "occupancyPercent");
        if (!width.has_value() || !height.has_value() || !length.has_value() || !occupancy.has_value()) {
            return err(SchemaError::MissingMember);
        }
        package.atlas.width            = static_cast<std::uint32_t>(*width);
        package.atlas.height           = static_cast<std::uint32_t>(*height);
        package.atlas.byteLength       = static_cast<std::uint64_t>(*length);
        package.atlas.occupancyPercent = static_cast<std::uint32_t>(*occupancy);
    }

    const json::Value* glyphs = requireArray(*document, "glyphs");
    if (glyphs == nullptr) {
        return err(SchemaError::MissingMember);
    }
    for (const json::Value& entry : glyphs->elements()) {
        if (entry.kind() != json::Value::Kind::Object) {
            return err(SchemaError::WrongType);
        }
        GlyphRecord glyph;
        auto        point    = requireInt(entry, "codePoint");
        auto        index    = requireInt(entry, "glyphIndex");
        auto        advance  = requireInt(entry, "advanceWidth");
        auto        bearing  = requireInt(entry, "leftSideBearing");
        auto        x        = requireInt(entry, "x");
        auto        y        = requireInt(entry, "y");
        auto        width    = requireInt(entry, "width");
        auto        height   = requireInt(entry, "height");
        auto        originX  = requireInt(entry, "bitmapOriginX");
        auto        originY  = requireInt(entry, "bitmapOriginY");
        if (!point.has_value() || !index.has_value() || !advance.has_value() || !bearing.has_value() || !x.has_value()
            || !y.has_value() || !width.has_value() || !height.has_value() || !originX.has_value() || !originY.has_value()) {
            return err(SchemaError::MissingMember);
        }
        glyph.codePoint       = static_cast<char32_t>(*point);
        glyph.glyphIndex      = static_cast<std::uint16_t>(*index);
        glyph.advanceWidth    = static_cast<std::uint16_t>(*advance);
        glyph.leftSideBearing = static_cast<std::int16_t>(*bearing);
        glyph.x               = static_cast<std::uint32_t>(*x);
        glyph.y               = static_cast<std::uint32_t>(*y);
        glyph.width           = static_cast<std::uint32_t>(*width);
        glyph.height          = static_cast<std::uint32_t>(*height);
        glyph.bitmapOriginX   = static_cast<std::int32_t>(*originX);
        glyph.bitmapOriginY   = static_cast<std::int32_t>(*originY);
        package.glyphs.push_back(glyph);
    }

    const json::Value* kerning = requireArray(*document, "kerning");
    if (kerning == nullptr) {
        return err(SchemaError::MissingMember);
    }
    for (const json::Value& entry : kerning->elements()) {
        auto left       = requireInt(entry, "left");
        auto right      = requireInt(entry, "right");
        auto adjustment = requireInt(entry, "adjustment");
        if (!left.has_value() || !right.has_value() || !adjustment.has_value()) {
            return err(SchemaError::MissingMember);
        }
        package.kerning.push_back(KerningPair{.left       = static_cast<char32_t>(*left),
                                              .right      = static_cast<char32_t>(*right),
                                              .adjustment = static_cast<std::int16_t>(*adjustment)});
    }

    const json::Value* charset = requireArray(*document, "restrictedCharset");
    if (charset == nullptr) {
        return err(SchemaError::MissingMember);
    }
    for (const json::Value& entry : charset->elements()) {
        auto first = requireInt(entry, "first");
        auto last  = requireInt(entry, "last");
        if (!first.has_value() || !last.has_value()) {
            return err(SchemaError::MissingMember);
        }
        package.restrictedCharset.push_back(
            CharsetRange{.first = static_cast<char32_t>(*first), .last = static_cast<char32_t>(*last)});
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
