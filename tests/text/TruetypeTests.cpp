/**
 * @file TruetypeTests.cpp
 * @brief BDD scenarios for the host-only TrueType (glyf) parser (issue #158).
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-010 No on-device text shaping
 *
 * The corpus is built in memory rather than committed as `.ttf` files. A malformed font is a
 * handful of bytes described by one line of code here; as a committed blob it would be an
 * opaque artifact whose intended defect a reviewer has to take on trust (the same argument
 * `SpirvTests.cpp` makes for `SpirvFixtures.hpp` and `SafetensorsTests.cpp` makes for its
 * fixtures).
 *
 * Every rejection scenario asserts the specific `ParseError` code, not merely that parsing
 * failed. A parser that rejects everything with one generic error passes "it was rejected"
 * and is useless to the author who has to fix the font - and useless to S4 (#160), which has
 * to convert each code into a distinct `TXT` diagnostic. The first scenario belows holds the
 * success case; the two table-driven scenarios hold all rejection paths for `parse()` and
 * `parseGlyph()`, so a new rejection added later lands next to its enumerator by index rather
 * than spread across the file.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.tools.truetype;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace tt = mdux::tools::truetype;
using tt::ParseError;

constexpr std::uint32_t kSfntTrue  = 0x00010000u;
constexpr std::uint32_t kHeadMagic = 0x5F0F3CF5u;

// ---------------------------------------------------------------------------
// An assembler for in-memory TrueType files, plus helpers for the glyf records each scenario
// uses. A real `.ttf` would be the same bytes in a different file; building them in code keeps
// the tests free of binary fixtures whose defect a reviewer would have to inspect with a hex
// dump. The assembler emits directory records in a fixed order (head, maxp, loca, glyf,
// extras) so the per-table offsets are deterministic and a `poke()` on a known location is
// safe across the whole corpus.
// ---------------------------------------------------------------------------

class Builder {
public:
    Builder& sfnt(std::uint32_t v) {
        sfnt_ = v;
        return *this;
    }
    Builder& unitsPerEm(std::uint16_t v) {
        upe_ = v;
        return *this;
    }
    Builder& indexToLocFormat(std::int16_t v) {
        idxFmt_ = v;
        return *this;
    }
    Builder& numGlyphs(std::uint16_t n) {
        ng_ = n;
        return *this;
    }
    Builder& headMagic(std::uint32_t v) {
        headMagic_ = v;
        return *this;
    }

    /// Replaces the glyf record at index `i`. `numGlyphs` is bumped to `i+1` if lower.
    Builder& rawGlyph(std::size_t i, std::vector<std::byte> bytes) {
        if (i + 1 > rawGlyphs_.size()) {
            rawGlyphs_.resize(i + 1);
        }
        rawGlyphs_[i] = std::move(bytes);
        if (ng_ < i + 1) {
            ng_ = static_cast<std::uint16_t>(i + 1);
        }
        return *this;
    }

    Builder& extraTable(std::string tag, std::vector<std::byte> bytes) {
        extras_.emplace_back(std::move(tag), std::move(bytes));
        return *this;
    }

    Builder& dropHead() {
        dropHead_ = true;
        return *this;
    }
    Builder& dropMaxp() {
        dropMaxp_ = true;
        return *this;
    }
    Builder& dropLoca() {
        dropLoca_ = true;
        return *this;
    }
    Builder& dropGlyf() {
        dropGlyf_ = true;
        return *this;
    }

    /// An override for the byte length the `loca` directory record declares - the directory
    /// length is what `readLoca()` reads when sizing the table, so a smaller value here is the
    /// `LocaSizeMismatch` fixture. Zero means "use the derived length".
    Builder& locaDeclaredLength(std::uint32_t v) {
        locaLen_ = v;
        return *this;
    }

    /// An override for the byte length the `head` directory record declares - a smaller value
    /// fires `TruncatedHead`, since the `head` subspan shrinks below 54 bytes.
    Builder& headDeclaredLength(std::uint32_t v) {
        headLen_ = v;
        return *this;
    }

    /// An override for the byte length the `maxp` directory record declares.
    Builder& maxpDeclaredLength(std::uint32_t v) {
        maxpLen_ = v;
        return *this;
    }

    /// An override for the head table's directory `offset` field; setting it past `bytes().size()`
    /// is the `TableOutOfBounds` fixture.
    Builder& headDeclaredOffset(std::uint32_t v) {
        headOff_ = v;
        return *this;
    }

    /// Sets `numTables` declared in the offset table to a different value than the directory's
    /// physical length - the parser reads only as many records as `numTables` claims, so a
    /// larger value is the `TruncatedTableDirectory` fixture.
    Builder& declaredNumTables(std::uint16_t n) {
        declaredNumTables_ = n;
        return *this;
    }

    /// An override that replaces the loca slot at index `i`'s stored short entry with `v`. The
    /// builder re-derives loca contents; this is the simple per-entry perturbation the
    /// `LocaOutOfBounds` fixture uses.
    Builder& locaShortEntry(std::size_t i, std::uint16_t v) {
        locaOverrides_[i] = v;
        return *this;
    }

    /// Serialized bytes plus deterministic per-table offsets, so a test can `poke()` into the
    /// directory record of each required table without searching for it.
    struct Serialized {
        std::vector<std::byte>                           bytes;
        std::size_t                                      headRecordStart{0};
        std::size_t                                      maxpRecordStart{0};
        std::size_t                                      locaRecordStart{0};
        std::size_t                                      glyfRecordStart{0};
        std::size_t                                      headDataStart{0};
        std::size_t                                      maxpDataStart{0};
        std::size_t                                      locaDataStart{0};
        std::size_t                                      glyfDataStart{0};
        std::vector<std::pair<std::size_t, std::size_t>> glyfRecords;
    };

    [[nodiscard]] Serialized serialize() const {
        // 1. Build the per-table byte contents.
        std::vector<std::byte> head;
        if (!dropHead_) {
            head.assign(headMinSize, std::byte{0});
            putU32(head, headMagicOffset, headMagic_);
            putU16(head, headUnitsPerEmOffset, upe_);
            putI16(head, headIndexToLocFormatOffset, idxFmt_);
        }
        std::vector<std::byte> maxp;
        if (!dropMaxp_) {
            maxp.assign(maxpMinSize, std::byte{0});
            putU32(maxp, 0, 0x00010000u);  // version 1.0
            putU16(maxp, 4, ng_);
        }
        std::vector<std::byte> loca;
        if (!dropLoca_) {
            const std::size_t entryCount = static_cast<std::size_t>(ng_) + 1u;
            std::uint32_t     cursor     = 0;
            for (std::size_t i = 0; i < entryCount; ++i) {
                std::uint16_t stored = 0;
                if (i < rawGlyphs_.size()) {
                    if (const auto it = locaOverrides_.find(i); it != locaOverrides_.end()) {
                        stored = static_cast<std::uint16_t>(it->second);
                    } else {
                        stored = static_cast<std::uint16_t>(cursor / 2u);
                    }
                } else if (const auto it = locaOverrides_.find(i); it != locaOverrides_.end()) {
                    stored = static_cast<std::uint16_t>(it->second);
                } else {
                    stored = static_cast<std::uint16_t>(cursor / 2u);
                }
                appendU16(loca, stored);
                if (i < rawGlyphs_.size()) {
                    cursor += static_cast<std::uint32_t>(roundUp2(rawGlyphs_[i].size()));
                }
            }
        }
        std::vector<std::byte> glyf;
        if (!dropGlyf_) {
            for (const auto& g : rawGlyphs_) {
                glyf.insert(glyf.end(), g.begin(), g.end());
                if (g.size() % 2 != 0) {
                    glyf.push_back(std::byte{0});  // pad each glyf record to 2-byte alignment
                }
            }
        }

        // 2. Plan a flat list of (tag, bytes) in fixed order, with extras appended.
        struct Entry {
            std::string                   tag;
            const std::vector<std::byte>* bytes;
        };
        std::vector<Entry> entries;
        if (!dropHead_)
            entries.push_back({"head", &head});
        if (!dropMaxp_)
            entries.push_back({"maxp", &maxp});
        if (!dropLoca_)
            entries.push_back({"loca", &loca});
        if (!dropGlyf_)
            entries.push_back({"glyf", &glyf});
        for (const auto& extra : extras_) {
            entries.push_back({extra.first, &extra.second});
        }

        const std::uint16_t numTables    = declaredNumTables_.value_or(static_cast<std::uint16_t>(entries.size()));
        const std::size_t   directoryEnd = offsetTableSize + static_cast<std::size_t>(numTables) * tableRecordSize;

        Serialized out;
        out.bytes.resize(directoryEnd);

        // Offset table.
        putU32(out.bytes, 0, sfnt_);
        putU16(out.bytes, 4, numTables);
        putU16(out.bytes, 6, 0);
        putU16(out.bytes, 8, 0);
        putU16(out.bytes, 10, 0);

        std::size_t cursor = directoryEnd;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            while (cursor % 4u != 0u) {
                out.bytes.push_back(std::byte{0});
                ++cursor;
            }
            const std::size_t rec = offsetTableSize + i * tableRecordSize;
            out.bytes[rec + 0]    = static_cast<std::byte>(entries[i].tag[0]);
            out.bytes[rec + 1]    = static_cast<std::byte>(entries[i].tag[1]);
            out.bytes[rec + 2]    = static_cast<std::byte>(entries[i].tag[2]);
            out.bytes[rec + 3]    = static_cast<std::byte>(entries[i].tag[3]);
            for (std::size_t b = 0; b < 4u; ++b)
                out.bytes[rec + 4 + b] = std::byte{0};  // checksum

            std::uint32_t dataOffset = static_cast<std::uint32_t>(cursor);
            std::uint32_t dataLength = static_cast<std::uint32_t>(entries[i].bytes->size());
            if (entries[i].tag == "head") {
                if (headOff_.has_value())
                    dataOffset = *headOff_;
                if (headLen_.has_value())
                    dataLength = *headLen_;
                out.headRecordStart = rec;
                out.headDataStart   = cursor;
            } else if (entries[i].tag == "maxp") {
                if (maxpLen_.has_value())
                    dataLength = *maxpLen_;
                out.maxpRecordStart = rec;
                out.maxpDataStart   = cursor;
            } else if (entries[i].tag == "loca") {
                if (locaLen_.has_value())
                    dataLength = *locaLen_;
                out.locaRecordStart = rec;
                out.locaDataStart   = cursor;
            } else if (entries[i].tag == "glyf") {
                out.glyfRecordStart = rec;
                out.glyfDataStart   = cursor;
            }
            putU32(out.bytes, rec + 8, dataOffset);
            putU32(out.bytes, rec + 12, dataLength);

            // Append the table's bytes. Padding below accounts for alignment to 4 between
            // tables; the embedded bytes are left untouched.
            out.bytes.insert(out.bytes.end(), entries[i].bytes->begin(), entries[i].bytes->end());
            cursor += entries[i].bytes->size();
        }

        // Trim or pad to the directory's claimed size if `declaredNumTables` lied. The parser walks
        // `numTables` records starting at offset 12, so a smaller file is itself the malformed
        // state the TruncatedTableDirectory fixture wants: leave the bytes ending at `cursor`,
        // which is shorter than `directoryEnd` claims when `numTables` was larger than the
        // physical record count.
        if (cursor < directoryEnd) {
            // The records past entries.size() are uninitialised in our vector; resize down to
            // what we actually wrote so the parser sees a real "not enough bytes" gap there.
            out.bytes.resize(cursor);
        }

        // 3. Compute glyf record offsets for individual poke operations by re-scanning the
        // directory; rawGlyphs_ order is preserved in glyf layout.
        std::uint32_t offset = 0;
        for (const auto& g : rawGlyphs_) {
            out.glyfRecords.emplace_back(out.glyfDataStart + offset, g.size());
            offset += static_cast<std::uint32_t>(roundUp2(g.size()));
        }

        return out;
    }

private:
    static constexpr std::size_t headMinSize                = 54;
    static constexpr std::size_t headMagicOffset            = 12;
    static constexpr std::size_t headUnitsPerEmOffset       = 18;
    static constexpr std::size_t headIndexToLocFormatOffset = 50;
    static constexpr std::size_t maxpMinSize                = 6;
    static constexpr std::size_t offsetTableSize            = 12;
    static constexpr std::size_t tableRecordSize            = 16;

    static std::size_t roundUp2(std::size_t n) noexcept {
        return n + (n % 2 != 0 ? 1u : 0u);
    }

    static void putU16(std::vector<std::byte>& v, std::size_t off, std::uint16_t val) {
        v[off + 0] = static_cast<std::byte>((val >> 8) & 0xffu);
        v[off + 1] = static_cast<std::byte>(val & 0xffu);
    }
    static void putI16(std::vector<std::byte>& v, std::size_t off, std::int16_t val) {
        putU16(v, off, static_cast<std::uint16_t>(val));
    }
    static void putU32(std::vector<std::byte>& v, std::size_t off, std::uint32_t val) {
        v[off + 0] = static_cast<std::byte>((val >> 24) & 0xffu);
        v[off + 1] = static_cast<std::byte>((val >> 16) & 0xffu);
        v[off + 2] = static_cast<std::byte>((val >> 8) & 0xffu);
        v[off + 3] = static_cast<std::byte>(val & 0xffu);
    }
    static void appendU16(std::vector<std::byte>& v, std::uint16_t val) {
        v.push_back(static_cast<std::byte>((val >> 8) & 0xffu));
        v.push_back(static_cast<std::byte>(val & 0xffu));
    }

    std::uint32_t                                               sfnt_      = kSfntTrue;
    std::uint16_t                                               upe_       = 2048;
    std::int16_t                                                idxFmt_    = 0;
    std::uint16_t                                               ng_        = 1;
    std::uint32_t                                               headMagic_ = kHeadMagic;
    bool                                                        dropHead_  = false;
    bool                                                        dropMaxp_  = false;
    bool                                                        dropLoca_  = false;
    bool                                                        dropGlyf_  = false;
    std::optional<std::uint16_t>                                declaredNumTables_;
    std::optional<std::uint32_t>                                headOff_;
    std::optional<std::uint32_t>                                headLen_;
    std::optional<std::uint32_t>                                maxpLen_;
    std::optional<std::uint32_t>                                locaLen_;
    std::map<std::size_t, std::uint16_t>                        locaOverrides_;
    std::vector<std::vector<std::byte>>                         rawGlyphs_;
    std::vector<std::pair<std::string, std::vector<std::byte>>> extras_;
};

/// A 12-byte empty glyf record: numberOfContours = 0, bbox 0..0, instructionLength = 0.
[[nodiscard]] std::vector<std::byte> emptyGlyph() {
    std::vector<std::byte> b;
    auto                   i16 = [&](std::int16_t v) {
        b.push_back(static_cast<std::byte>((static_cast<std::uint16_t>(v) >> 8) & 0xffu));
        b.push_back(static_cast<std::byte>(static_cast<std::uint16_t>(v) & 0xffu));
    };
    auto u16 = [&](std::uint16_t v) {
        b.push_back(static_cast<std::byte>((v >> 8) & 0xffu));
        b.push_back(static_cast<std::byte>(v & 0xffu));
    };
    i16(0);  // numberOfContours = 0
    i16(0);
    i16(0);
    i16(0);
    i16(0);  // bbox
    u16(0);  // instructionLength = 0
    return b;
}

/// A simple square glyf record: 1 contour, 4 on-curve points at (0,0), (100,0), (100,100),
/// (0,100). Coords encode as signed 16-bit deltas; flags carry on-curve only (0x01) - the
/// success-path `parseGlyph()` exercises the no-short / no-same/positive branch in both axes.
[[nodiscard]] std::vector<std::byte> squareGlyph() {
    std::vector<std::byte> b;
    auto                   i16 = [&](std::int16_t v) {
        b.push_back(static_cast<std::byte>((static_cast<std::uint16_t>(v) >> 8) & 0xffu));
        b.push_back(static_cast<std::byte>(static_cast<std::uint16_t>(v) & 0xffu));
    };
    auto u16 = [&](std::uint16_t v) {
        b.push_back(static_cast<std::byte>((v >> 8) & 0xffu));
        b.push_back(static_cast<std::byte>(v & 0xffu));
    };
    i16(1);  // numberOfContours = 1
    i16(0);
    i16(0);
    i16(100);
    i16(100);                      // bbox
    u16(3);                        // endPtsOfContours[0] = 3 (4 points, indices 0..3)
    u16(0);                        // instructionLength = 0
    b.push_back(std::byte{0x01});  // on-curve, signed-16 X delta, signed-16 Y delta
    b.push_back(std::byte{0x01});
    b.push_back(std::byte{0x01});
    b.push_back(std::byte{0x01});
    i16(0);
    i16(100);
    i16(0);
    i16(-100);  // X deltas
    i16(0);
    i16(0);
    i16(100);
    i16(0);  // Y deltas
    return b;
}

/// A 10-byte composite glyf record: numberOfContours = -1, bbox 0..0. The parser rejects at
/// the contour count check before touching the component stream - testing the rejection is the
/// point of the fixture, not exercising composite walking.
[[nodiscard]] std::vector<std::byte> compositeGlyph() {
    std::vector<std::byte> b;
    const auto             u16 = [&](std::uint16_t v) {
        b.push_back(static_cast<std::byte>((v >> 8) & 0xffu));
        b.push_back(static_cast<std::byte>(v & 0xffu));
    };
    u16(0xFFFFu);  // numberOfContours = -1 (int16)
    u16(0);
    u16(0);
    u16(0);
    u16(0);  // bbox
    return b;
}

/// A glyph whose coordinate stream is shorter than the flags declared. Built as a 1-contour
/// glyph with 4 on-curve points but with the X coordinate array truncated to one entry - the
/// parser hits `TruncatedGlyphCoords` mid-X.
[[nodiscard]] std::vector<std::byte> coordsTruncatedGlyph() {
    std::vector<std::byte> b;
    auto                   i16 = [&](std::int16_t v) {
        b.push_back(static_cast<std::byte>((static_cast<std::uint16_t>(v) >> 8) & 0xffu));
        b.push_back(static_cast<std::byte>(static_cast<std::uint16_t>(v) & 0xffu));
    };
    auto u16 = [&](std::uint16_t v) {
        b.push_back(static_cast<std::byte>((v >> 8) & 0xffu));
        b.push_back(static_cast<std::byte>(v & 0xffu));
    };
    i16(1);
    i16(0);
    i16(0);
    i16(0);
    i16(0);  // bbox
    u16(3);  // endPtsOfContours[0] = 3 (4 points)
    u16(0);  // instructionLength = 0
    for (int i = 0; i < 4; ++i)
        b.push_back(std::byte{0x01});
    i16(0);  // only one signed-16 X delta for the first point; the rest would extend past EOD
    return b;
}

/// A glyph whose flag array is shorter than its `endPtsOfContours[N] + 1` claim. One contour of
/// 4 points declared, only 1 flag byte supplied.
[[nodiscard]] std::vector<std::byte> flagsTruncatedGlyph() {
    std::vector<std::byte> b;
    auto                   i16 = [&](std::int16_t v) {
        b.push_back(static_cast<std::byte>((static_cast<std::uint16_t>(v) >> 8) & 0xffu));
        b.push_back(static_cast<std::byte>(static_cast<std::uint16_t>(v) & 0xffu));
    };
    auto u16 = [&](std::uint16_t v) {
        b.push_back(static_cast<std::byte>((v >> 8) & 0xffu));
        b.push_back(static_cast<std::byte>(v & 0xffu));
    };
    i16(1);
    i16(0);
    i16(0);
    i16(0);
    i16(0);
    u16(3);                        // 4 points
    u16(0);
    b.push_back(std::byte{0x01});  // 1 flag only
    return b;
}

/// A glyph with two contours whose end points are non-monotonic (3 then 1).
[[nodiscard]] std::vector<std::byte> nonMonotonicGlyph() {
    std::vector<std::byte> b;
    auto                   i16 = [&](std::int16_t v) {
        b.push_back(static_cast<std::byte>((static_cast<std::uint16_t>(v) >> 8) & 0xffu));
        b.push_back(static_cast<std::byte>(static_cast<std::uint16_t>(v) & 0xffu));
    };
    auto u16 = [&](std::uint16_t v) {
        b.push_back(static_cast<std::byte>((v >> 8) & 0xffu));
        b.push_back(static_cast<std::byte>(v & 0xffu));
    };
    i16(2);  // numberOfContours = 2
    i16(0);
    i16(0);
    i16(0);
    i16(0);
    u16(3);  // endPts[0] = 3
    u16(1);  // endPts[1] = 1 -- not > 3
    u16(0);  // instructionLength = 0
    return b;
}

/// A glyph declaring one contour but offering no endPtsOfContours field after the header.
[[nodiscard]] std::vector<std::byte> endPtsTruncatedGlyph() {
    std::vector<std::byte> b;
    auto                   i16 = [&](std::int16_t v) {
        b.push_back(static_cast<std::byte>((static_cast<std::uint16_t>(v) >> 8) & 0xffu));
        b.push_back(static_cast<std::byte>(static_cast<std::uint16_t>(v) & 0xffu));
    };
    i16(1);
    i16(0);
    i16(0);
    i16(0);
    i16(0);
    return b;  // 10 bytes: no endPts, no instructionLength
}

/// A single-point glyph whose flag has bits 6-7 set: parser rejects with UnsupportedGlyphFlag.
[[nodiscard]] std::vector<std::byte> reservedFlagGlyph() {
    std::vector<std::byte> b;
    auto                   i16 = [&](std::int16_t v) {
        b.push_back(static_cast<std::byte>((static_cast<std::uint16_t>(v) >> 8) & 0xffu));
        b.push_back(static_cast<std::byte>(static_cast<std::uint16_t>(v) & 0xffu));
    };
    auto u16 = [&](std::uint16_t v) {
        b.push_back(static_cast<std::byte>((v >> 8) & 0xffu));
        b.push_back(static_cast<std::byte>(v & 0xffu));
    };
    i16(1);
    i16(0);
    i16(0);
    i16(0);
    i16(0);
    u16(0);                        // endPtsOfContours[0] = 0 - one point
    u16(0);                        // instructionLength = 0
    b.push_back(std::byte{0xC1});  // on-curve + reserved bit 7 set
    return b;
}

/// A glyph whose `instructionLength` is non-zero; the parser rejects before reading flags.
[[nodiscard]] std::vector<std::byte> hintedGlyph() {
    std::vector<std::byte> b;
    auto                   i16 = [&](std::int16_t v) {
        b.push_back(static_cast<std::byte>((static_cast<std::uint16_t>(v) >> 8) & 0xffu));
        b.push_back(static_cast<std::byte>(static_cast<std::uint16_t>(v) & 0xffu));
    };
    auto u16 = [&](std::uint16_t v) {
        b.push_back(static_cast<std::byte>((v >> 8) & 0xffu));
        b.push_back(static_cast<std::byte>(v & 0xffu));
    };
    i16(1);
    i16(0);
    i16(0);
    i16(0);
    i16(0);
    u16(3);  // 4 points
    u16(1);  // instructionLength = 1
    // The instruction byte itself is irrelevant; HintingRejected fires before it would be read.
    b.push_back(std::byte{0x42});
    return b;
}

/// A glyph record shorter than the 10-byte header - the parser fails with TruncatedGlyph.
[[nodiscard]] std::vector<std::byte> shortGlyph() {
    return std::vector<std::byte>(5, std::byte{0});
}

/// A 16-byte dummy extra table - its bytes are irrelevant; the parser only inspects the tag.
[[nodiscard]] std::vector<std::byte> dummyExtra() {
    return std::vector<std::byte>(16, std::byte{0});
}

}  // namespace

// ---------------------------------------------------------------------------
// parse(): success cases
// ---------------------------------------------------------------------------

const mdux::spec::Register validFontParses{"A minimal well-formed TrueType font parses and reports its metrics", "evidence-unit", [] {
                                               struct State {
                                                   Builder::Serialized     serialized;
                                                   std::optional<tt::Font> font;
                                               };
                                               auto state = std::make_shared<State>();

                                               return speclab::Test("text-truetype-parse-valid")
                                                   .Given("a builder with empty + square + composite glyphs",
                                                          [state] {
                                                              auto serialized = Builder()
                                                                                    .unitsPerEm(2048)
                                                                                    .indexToLocFormat(0)
                                                                                    .rawGlyph(0, emptyGlyph())
                                                                                    .rawGlyph(1, squareGlyph())
                                                                                    .rawGlyph(2, compositeGlyph())
                                                                                    .serialize();
                                                              state->serialized = std::move(serialized);
                                                          })
                                                   .When("parse() is called",
                                                         [state] {
                                                             auto font = tt::parse(state->serialized.bytes);
                                                             if (!font.has_value()) {
                                                                 throw speclab::core::AssertionFailure(
                                                                     std::format("valid font was rejected: {}", tt::describe(font.error())),
                                                                     std::source_location::current());
                                                             }
                                                             state->font = std::move(*font);
                                                         })
                                                   .Then("the font carries the directory's upe, numGlyphs, indexToLocFormat and 4 loca "
                                                         "entries",
                                                         [state] {
                                                             mdux::spec::Checks checks;
                                                             const auto&        font = *state->font;
                                                             checks.expect(font.sfntVersion == kSfntTrue, "sfnt version");
                                                             checks.expect(font.unitsPerEm == 2048, "unitsPerEm");
                                                             checks.expect(font.numGlyphs == 3, "numGlyphs");
                                                             checks.expect(font.indexToLocFormat == 0, "indexToLocFormat");
                                                             checks.expect(font.loca.size() == 4, "loca has numGlyphs+1 entries");
                                                             checks.expect(!font.glyf.empty(), "glyf span is non-empty");
                                                             checks.expect(!font.head.empty(), "head span is non-empty");
                                                             checks.raise();
                                                         })
                                                   .Execute();
                                           }};

const mdux::spec::Register trueAndTyp1Acceptances{"parse() accepts 'true' and 'typ1' TrueType container variants", "evidence-unit", [] {
                                                      // The directory records walk can pass on either 'true' or 'typ1' if they actually
                                                      // describe a TrueType outline table set; the check at the version word is the first
                                                      // gate, not the only one, and these two are alternative container spellings rather than
                                                      // tow CFF-flavoured ones. (OTTO is the CFF OpenType container and would reject here too,
                                                      // but parse() rejects CFF fonts later again on the 'CFF ' table tag, regardless.)
                                                      struct State {
                                                          Builder::Serialized     s1;
                                                          Builder::Serialized     s2;
                                                          std::optional<tt::Font> f1;
                                                          std::optional<tt::Font> f2;
                                                      };
                                                      auto state = std::make_shared<State>();

                                                      return speclab::Test("text-truetype-container-variants")
                                                          .Given("a 'true' container and a 'typ1' container",
                                                                 [state] {
                                                                     state->s1 = Builder().sfnt(0x74727565u).serialize();
                                                                     state->s2 = Builder().sfnt(0x74797031u).serialize();
                                                                 })
                                                          .When("each is parsed",
                                                                [state] {
                                                                    auto v1 = tt::parse(state->s1.bytes);
                                                                    auto v2 = tt::parse(state->s2.bytes);
                                                                    if (!v1.has_value() || !v2.has_value()) {
                                                                        throw speclab::core::AssertionFailure(
                                                                            std::format("'true'={}, 'typ1'={}", v1.has_value(), v2.has_value()),
                                                                            std::source_location::current());
                                                                    }
                                                                    state->f1 = std::move(*v1);
                                                                    state->f2 = std::move(*v2);
                                                                })
                                                          .Then("both are accepted",
                                                                [state] {
                                                                    mdux::spec::Checks checks;
                                                                    checks.expect(state->f1.has_value(), "'true' accepted");
                                                                    checks.expect(state->f2.has_value(), "'typ1' accepted");
                                                                    checks.raise();
                                                                })
                                                          .Execute();
                                                  }};

// ---------------------------------------------------------------------------
// parse(): rejection corpus - one scenario per ParseError the directory walk emits.
// ---------------------------------------------------------------------------

const mdux::spec::Register parseRejections{
    "parse() emits the right stable code per failure mode",
    "evidence-unit",
    [] {
        struct Case {
            std::string_view                        what;
            ParseError                              expected;
            std::function<std::vector<std::byte>()> build;
        };

        const std::vector<Case> cases{
            {                                    "an empty buffer",
             ParseError::Empty,
             [] {
             return std::vector<std::byte>{};
             }},
            {             "a buffer shorter than the offset table",
             ParseError::TruncatedOffsetTable,
             [] {
             return std::vector<std::byte>(6, std::byte{0});
             }},
            {                        "a non-TrueType sfnt version",
             ParseError::NotATrueTypeOutlineFont,
             [] {
             return Builder().sfnt(0x44444444u).serialize().bytes;
             }},
            {"numTables claiming more records than the file holds",
             ParseError::TruncatedTableDirectory,
             [] {
             // The Builder allocates the declared directory's rows of zero bytes; in a real
             // file that the truncation check would have caught the same way, those rows
             // simply aren't there at all. Trimming to 30 bytes here leaves the header but
             // not the directory it claims, which is the only shape that fires
             // TruncatedTableDirectory before the duplicate-tag walk has anything to read.
             auto s = Builder().declaredNumTables(99).serialize().bytes;
             s.resize(30);
             return s;
             }},
            {                       "a duplicate 'head' table tag",
             ParseError::DuplicateTable,
             [] {
             auto s = Builder().extraTable("head", dummyExtra()).serialize();
             return s.bytes;
             }},
            {                             "a missing 'head' table",
             ParseError::MissingRequiredTable,
             [] {
             return Builder().dropHead().serialize().bytes;
             }},
            {"a 'head' table whose offset+length exceeds the file",
             ParseError::TableOutOfBounds,
             [] {
             return Builder().headDeclaredOffset(0xFFFFFFFFu).serialize().bytes;
             }},
            {                             "a 'CFF ' table present",
             ParseError::CffOutlinesRejected,
             [] {
             return Builder().extraTable("CFF ", dummyExtra()).serialize().bytes;
             }},
            {                             "a 'CFF2' table present",
             ParseError::CffOutlinesRejected,
             [] {
             return Builder().extraTable("CFF2", dummyExtra()).serialize().bytes;
             }},
            {                             "a 'GPOS' table present",
             ParseError::GposRejected,
             [] {
             return Builder().extraTable("GPOS", dummyExtra()).serialize().bytes;
             }},
            {                             "a 'GSUB' table present",
             ParseError::GsubRejected,
             [] {
             return Builder().extraTable("GSUB", dummyExtra()).serialize().bytes;
             }},
            {                             "a 'morx' table present",
             ParseError::AppleLayoutRejected,
             [] {
             return Builder().extraTable("morx", dummyExtra()).serialize().bytes;
             }},
            {                             "a 'mort' table present",
             ParseError::AppleLayoutRejected,
             [] {
             return Builder().extraTable("mort", dummyExtra()).serialize().bytes;
             }},
            {               "a 'head' table shorter than 54 bytes",
             ParseError::TruncatedHead,
             [] {
             return Builder().headDeclaredLength(40).serialize().bytes;
             }},
            {             "a 'head' table whose magic check fails",
             ParseError::HeadBadMagicNumber,
             [] {
             return Builder().headMagic(0xDEADBEEFu).serialize().bytes;
             }},
            {                 "a unitsPerEm value of 8 (below 16)",
             ParseError::UnsupportedUnitsPerEm,
             [] {
             return Builder().unitsPerEm(8).serialize().bytes;
             }},
            {            "a unitsPerEm value of 8192 (above 4096)",
             ParseError::UnsupportedUnitsPerEm,
             [] {
             return Builder().unitsPerEm(8192).serialize().bytes;
             }},
            {                "a 'maxp' table shorter than 6 bytes",
             ParseError::TruncatedMaxp,
             [] {
             return Builder().maxpDeclaredLength(4).serialize().bytes;
             }},
            {                     "an indexToLocFormat value of 2",
             ParseError::UnsupportedLocaFormat,
             [] {
             return Builder().indexToLocFormat(2).serialize().bytes;
             }},
            {         "a 'loca' byte length below (numGlyphs+1)*2",
             ParseError::LocaSizeMismatch,
             [] {
             return Builder().locaDeclaredLength(2).serialize().bytes;
             }},
            {         "a loca entry pointing past the end of glyf",
             ParseError::LocaOutOfBounds,
             [] {
             // Pipe the trailing entry past the glyf length; the glyph it is supposed to
             // bound is empty, so any non-zero value past the small glyf length is plenty.
             return Builder().rawGlyph(0, emptyGlyph()).locaShortEntry(1, 0xFFFFu).serialize().bytes;
             }},
        };

        return speclab::Test("text-truetype-parse-rejections")
            .Given("a corpus of deliberately malformed fonts", [] {})
            .When("each is parsed", [] {})
            .Then("each yields exactly the ParseError identified in the corpus",
                  [&cases] {
                      mdux::spec::Checks checks;
                      for (const Case& entry : cases) {
                          auto                   result = tt::parse(entry.build());
                          const std::string_view got    = result.has_value() ? std::string_view{"ok"} : tt::describe(result.error());
                          checks.expect(!result.has_value(), std::format("{}: parse succeeded unexpectedly", entry.what));
                          if (!result.has_value()) {
                              checks.expect(result.error() == entry.expected,
                                            std::format("{}: got '{}', expected '{}'", entry.what, got, tt::describe(entry.expected)));
                          }
                      }
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// parseGlyph(): success and rejection corpus
// ---------------------------------------------------------------------------

const mdux::spec::Register squareGlyphParses{
    "A simple square glyph parses into 4 on-curve points and one contour",
    "evidence-unit",
    [] {
        struct State {
            Builder::Serialized            serialized;
            std::optional<tt::Font>        font;
            std::optional<tt::SimpleGlyph> glyph;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-truetype-square-glyph-parses")
            .Given("a font with an empty and a square glyph",
                   [state] {
                       state->serialized = Builder().rawGlyph(0, emptyGlyph()).rawGlyph(1, squareGlyph()).serialize();
                       auto font         = tt::parse(state->serialized.bytes);
                       if (!font.has_value()) {
                           throw speclab::core::AssertionFailure(std::format("font failed to parse: {}", tt::describe(font.error())),
                                                                 std::source_location::current());
                       }
                       state->font = std::move(*font);
                   })
            .When("parseGlyph() is called for the square glyph",
                  [state] {
                      auto glyph = tt::parseGlyph(*state->font, 1);
                      if (!glyph.has_value()) {
                          throw speclab::core::AssertionFailure(std::format("square glyph failed to parse: {}", tt::describe(glyph.error())),
                                                                std::source_location::current());
                      }
                      state->glyph = std::move(*glyph);
                  })
            .Then("it has 4 on-curve points at the square's corners",
                  [state] {
                      const auto&        g = *state->glyph;
                      mdux::spec::Checks checks;
                      checks.expect(g.glyphIndex == 1, "glyphIndex");
                      checks.expect(g.xMin == 0 && g.yMin == 0, "min bbox");
                      checks.expect(g.xMax == 100 && g.yMax == 100, "max bbox");
                      checks.expect(g.endPtsOfContours.size() == 1, "one contour");
                      if (!g.endPtsOfContours.empty()) {
                          checks.expect(g.endPtsOfContours[0] == 3, "end point is the 4th point index");
                      }
                      checks.expect(g.points.size() == 4, "4 points");
                      if (g.points.size() == 4) {
                          checks.expect(g.points[0].x == 0 && g.points[0].y == 0 && g.points[0].onCurve, "point 0 at (0,0)");
                          checks.expect(g.points[1].x == 100 && g.points[1].y == 0 && g.points[1].onCurve, "point 1 at (100,0)");
                          checks.expect(g.points[2].x == 100 && g.points[2].y == 100 && g.points[2].onCurve, "point 2 at (100,100)");
                          checks.expect(g.points[3].x == 0 && g.points[3].y == 100 && g.points[3].onCurve, "point 3 at (0,100)");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register emptyGlyphParses{"An empty glyph (numberOfContours == 0) parses with no points", "evidence-unit", [] {
                                                struct State {
                                                    Builder::Serialized            serialized;
                                                    std::optional<tt::Font>        font;
                                                    std::optional<tt::SimpleGlyph> glyph;
                                                };
                                                auto state = std::make_shared<State>();

                                                return speclab::Test("text-truetype-empty-glyph-parses")
                                                    .Given("a font with one empty glyph",
                                                           [state] {
                                                               state->serialized = Builder().rawGlyph(0, emptyGlyph()).serialize();
                                                               auto font         = tt::parse(state->serialized.bytes);
                                                               if (!font.has_value()) {
                                                                   throw speclab::core::AssertionFailure(
                                                                       std::format("font failed to parse: {}", tt::describe(font.error())),
                                                                       std::source_location::current());
                                                               }
                                                               state->font = std::move(*font);
                                                           })
                                                    .When("parseGlyph() is called for the empty glyph",
                                                          [state] {
                                                              auto glyph = tt::parseGlyph(*state->font, 0);
                                                              if (!glyph.has_value()) {
                                                                  throw speclab::core::AssertionFailure(
                                                                      std::format("empty glyph failed to parse: {}", tt::describe(glyph.error())),
                                                                      std::source_location::current());
                                                              }
                                                              state->glyph = std::move(*glyph);
                                                          })
                                                    .Then("it has no contours and no points",
                                                          [state] {
                                                              const auto&        g = *state->glyph;
                                                              mdux::spec::Checks checks;
                                                              checks.expect(g.endPtsOfContours.empty(), "no end points");
                                                              checks.expect(g.points.empty(), "no contour points");
                                                              checks.raise();
                                                          })
                                                    .Execute();
                                            }};

const mdux::spec::Register parseGlyphRejections{
    "parseGlyph() emits the right stable code per failure mode",
    "evidence-unit",
    [] {
        using FontResult  = mdux::core::Result<tt::Font, tt::ParseError>;
        using GlyphResult = mdux::core::Result<tt::SimpleGlyph, tt::ParseError>;

        struct State {
            std::optional<tt::Font> font;
        };

        struct Case {
            std::string_view                            what;
            ParseError                                  expected;
            std::function<FontResult(State&)>           buildFont;
            std::function<GlyphResult(const tt::Font&)> parse;
        };

        const std::vector<Case> cases{
            {                     "a glyph index past numGlyphs",
             ParseError::GlyphIndexOutOfRange,
             [](State& s) {
             auto r = tt::parse(Builder().rawGlyph(0, emptyGlyph()).serialize().bytes);
             if (r.has_value())
             s.font = std::move(*r);
             return r;
             }, [](const tt::Font& f) {
 return tt::parseGlyph(f, 100);
 }},

            {    "a glyf record shorter than the 10-byte header",
             ParseError::TruncatedGlyph,
             [](State& s) {
             auto r = tt::parse(Builder().rawGlyph(0, shortGlyph()).serialize().bytes);
             if (r.has_value())
             s.font = std::move(*r);
             return r;
             }, [](const tt::Font& f) {
 return tt::parseGlyph(f, 0);
 }},

            {       "a composite glyph (numberOfContours == -1)",
             ParseError::CompositeGlyphRejected,
             [](State& s) {
             auto r = tt::parse(Builder().rawGlyph(0, compositeGlyph()).serialize().bytes);
             if (r.has_value())
             s.font = std::move(*r);
             return r;
             }, [](const tt::Font& f) {
 return tt::parseGlyph(f, 0);
 }},

            {                     "hinting instructions present",
             ParseError::HintingRejected,
             [](State& s) {
             auto r = tt::parse(Builder().rawGlyph(0, hintedGlyph()).serialize().bytes);
             if (r.has_value())
             s.font = std::move(*r);
             return r;
             }, [](const tt::Font& f) {
 return tt::parseGlyph(f, 0);
 }},

            {  "endPtsOfContours extending past the glyf record",
             ParseError::TruncatedContourEndpoints,
             [](State& s) {
             auto r = tt::parse(Builder().rawGlyph(0, endPtsTruncatedGlyph()).serialize().bytes);
             if (r.has_value())
             s.font = std::move(*r);
             return r;
             }, [](const tt::Font& f) {
 return tt::parseGlyph(f, 0);
 }},

            {                   "non-monotonic endPtsOfContours",
             ParseError::NonMonotonicContours,
             [](State& s) {
             auto r = tt::parse(Builder().rawGlyph(0, nonMonotonicGlyph()).serialize().bytes);
             if (r.has_value())
             s.font = std::move(*r);
             return r;
             }, [](const tt::Font& f) {
 return tt::parseGlyph(f, 0);
 }},

            {             "flags extending past the glyf record",
             ParseError::TruncatedGlyphFlags,
             [](State& s) {
             auto r = tt::parse(Builder().rawGlyph(0, flagsTruncatedGlyph()).serialize().bytes);
             if (r.has_value())
             s.font = std::move(*r);
             return r;
             }, [](const tt::Font& f) {
 return tt::parseGlyph(f, 0);
 }},

            {"x or y coordinates extending past the glyf record",
             ParseError::TruncatedGlyphCoords,
             [](State& s) {
             auto r = tt::parse(Builder().rawGlyph(0, coordsTruncatedGlyph()).serialize().bytes);
             if (r.has_value())
             s.font = std::move(*r);
             return r;
             }, [](const tt::Font& f) {
 return tt::parseGlyph(f, 0);
 }},

            {                "a flag with reserved bits 6-7 set",
             ParseError::UnsupportedGlyphFlag,
             [](State& s) {
             auto r = tt::parse(Builder().rawGlyph(0, reservedFlagGlyph()).serialize().bytes);
             if (r.has_value())
             s.font = std::move(*r);
             return r;
             }, [](const tt::Font& f) {
 return tt::parseGlyph(f, 0);
 }},
        };

        return speclab::Test("text-truetype-glyph-rejections")
            .Given("a corpus of malformed glyphs each in its own font", [] {})
            .When("each is parsed", [] {})
            .Then("each yields the ParseError identified in the corpus",
                  [&cases] {
                      mdux::spec::Checks checks;
                      State              state;
                      for (const Case& entry : cases) {
                          state.font.reset();
                          auto fontResult = entry.buildFont(state);
                          if (!fontResult.has_value()) {
                              throw speclab::core::AssertionFailure(
                                  std::format("{}: the fixture's font did not parse: {}", entry.what, tt::describe(fontResult.error())),
                                  std::source_location::current());
                          }
                          auto glyph = entry.parse(*state.font);
                          checks.expect(!glyph.has_value(), std::format("{}: parseGlyph succeeded unexpectedly", entry.what));
                          if (!glyph.has_value()) {
                              checks.expect(glyph.error() == entry.expected,
                                            std::format("{}: got '{}', expected '{}'", entry.what, tt::describe(glyph.error()), tt::describe(entry.expected)));
                          }
                      }
                      checks.raise();
                  })
            .Execute();
    }};