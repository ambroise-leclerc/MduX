/**
 * @file DrawTests.cpp
 * @brief BDD scenarios for the governed coverage draw path (issue #162).
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone)
 * @compliance ADR-010 No on-device text shaping
 *
 * These run without a GPU, which is the point of the draw path being governed: the record decode
 * and the uv arithmetic are checkable directly, so a failure names the wrong number rather than
 * showing up as a glyph in the wrong place in a rendered frame. `TextPixelTests.cpp` covers what
 * only a render can - that the numbers reach the screen.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.core.units;
import mdux.draw;
import mdux.font.schema;
import mdux.text.draw;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace core = mdux::core;
namespace draw = mdux::draw;
namespace font = mdux::font;
namespace td   = mdux::text::draw;

/// A package with one 4x4 glyph at slot (4, 0) of an 8x8 sheet, so the normalised coordinates are
/// halves and quarters - values a reader can check without a calculator.
[[nodiscard]] font::FontPackage packageWithOneGlyph() {
    font::FontPackage package;
    package.id         = "fixture";
    package.unitsPerEm = 2048;
    package.pixelSize  = 4;
    package.locales    = {"en-US"};
    package.atlas      = font::AtlasMetrics{.path             = "atlas.bin",
                                            .width            = 8,
                                            .height           = 8,
                                            .byteLength       = 64,
                                            .sha256           = std::string(64, 'a'),
                                            .occupancyPercent = 25};
    package.glyphs     = {font::GlyphRecord{.codePoint       = U'A',
                                            .glyphIndex      = 1,
                                            .advanceWidth    = 1024,
                                            .leftSideBearing = 0,
                                            .x               = 4,
                                            .y               = 0,
                                            .width           = 4,
                                            .height          = 4,
                                            .bitmapOriginX   = 1,
                                            .bitmapOriginY   = 4}};
    package.restrictedCharset = {font::CharsetRange{.first = U'A', .last = U'A'}};
    return package;
}

struct Buffers {
    std::array<draw::UiVertex, 16>   vertices{};
    std::array<draw::Index, 24>      indices{};
    std::array<draw::DrawCommand, 2> commands{};
    [[nodiscard]] static constexpr draw::DrawBudget budget() noexcept {
        return draw::DrawBudget{.maxVertices = 16, .maxIndices = 24, .maxCommands = 2};
    }
};

[[nodiscard]] std::vector<std::byte> record(std::uint16_t glyphIndex, std::int16_t x, std::int16_t y) {
    const auto ux = std::bit_cast<std::uint16_t>(x);
    const auto uy = std::bit_cast<std::uint16_t>(y);
    return {static_cast<std::byte>(glyphIndex & 0xFFu), static_cast<std::byte>((glyphIndex >> 8) & 0xFFu),
            static_cast<std::byte>(ux & 0xFFu),         static_cast<std::byte>((ux >> 8) & 0xFFu),
            static_cast<std::byte>(uy & 0xFFu),         static_cast<std::byte>((uy >> 8) & 0xFFu)};
}

}  // namespace

const mdux::spec::Register recordsDecodeLittleEndian{
    "A run record decodes little-endian, including negative positions",
    "evidence-unit",
    [] {
        // The byte order is a contract, not an implementation detail: the sidecar is committed
        // bytes compared across toolchains, so a decode that inherited the host's order would
        // move glyphs on one leg and not the other. Negative x is ordinary - a glyph can sit left
        // of its run's origin - and is the case a sign-extension mistake breaks.
        return speclab::Test("text-draw-record-decode")
            .Given("nothing", [] {})
            .When("nothing", [] {})
            .Then("each field is read from its own two bytes, with sign preserved",
                  [] {
                      mdux::spec::Checks checks;
                      const auto         positive = record(0x0102, 300, -40);
                      auto               decoded  = td::decodeRecord(positive);
                      checks.expect(decoded.has_value(), "a whole record decodes");
                      if (decoded.has_value()) {
                          checks.expect(decoded->glyphIndex == 0x0102, "glyphIndex reads bytes 0-1");
                          checks.expect(decoded->x == 300, "x reads bytes 2-3");
                          checks.expect(decoded->y == -40,
                                        std::format("y is sign-extended, got {}", decoded->y));
                      }
                      // A short buffer is a partial record, which the schema refuses to let reach
                      // a device precisely because a partial read means something different
                      // between two struct layouts.
                      const std::array<std::byte, 5> truncated{};
                      auto                           short_ = td::decodeRecord(truncated);
                      checks.expect(!short_.has_value() && short_.error() == td::DrawTextError::PartialRecord,
                                    "a five-byte record is refused");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register glyphRectIsPlacedAndNormalised{
    "A glyph is placed against the baseline and its slot normalised against the sheet",
    "evidence-unit",
    [] {
        // The arithmetic that a rendered frame can only show indirectly. The slot is (4,0) 4x4 on
        // an 8x8 sheet, so u runs 0.5..1.0 and v runs 0.0..0.5 - and the rect sits at
        // (pen + bitmapOriginX, baseline - bitmapOriginY), which is the inversion most likely to
        // be written the wrong way round.
        return speclab::Test("text-draw-glyph-rect")
            .Given("nothing", [] {})
            .When("nothing", [] {})
            .Then("the vertices carry the derived position and normalised uv",
                  [] {
                      mdux::spec::Checks checks;
                      const auto         package = packageWithOneGlyph();
                      Buffers            buffers;
                      auto list = draw::DrawList::create(buffers.vertices, buffers.indices, buffers.commands,
                                                         Buffers::budget());
                      checks.expect(list.has_value(), "the list is created");
                      if (!list.has_value()) {
                          checks.raise();
                          return;
                      }
                      constexpr core::ColorRgba8 white{.r = 255, .g = 255, .b = 255, .a = 255};
                      auto added = td::addGlyphRect(*list, package, package.glyphs[0], 20, 30, white);
                      checks.expect(added.has_value(), "the glyph is recorded");

                      const auto vertices = list->vertices();
                      checks.expect(vertices.size() == 4, "one quad");
                      if (vertices.size() != 4) {
                          checks.raise();
                          return;
                      }
                      // Top-left corner: pen 20 + bitmapOriginX 1 = 21; baseline 30 - originY 4 = 26.
                      checks.expect(vertices[0].x == 21.0F && vertices[0].y == 26.0F,
                                    std::format("top-left at (21, 26), got ({}, {})", vertices[0].x, vertices[0].y));
                      checks.expect(vertices[2].x == 25.0F && vertices[2].y == 30.0F,
                                    "bottom-right is four pixels along and down, sitting on the baseline");
                      checks.expect(vertices[0].u == 0.5F && vertices[0].v == 0.0F,
                                    std::format("uv starts at (0.5, 0.0), got ({}, {})", vertices[0].u, vertices[0].v));
                      checks.expect(vertices[2].u == 1.0F && vertices[2].v == 0.5F,
                                    std::format("uv ends at (1.0, 0.5), got ({}, {})", vertices[2].u, vertices[2].v));
                      checks.expect(vertices[0].mode == static_cast<std::uint32_t>(draw::DrawMode::CoverageR8),
                                    "recorded in CoverageR8, not solid");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register runRejections{
    "recordRun() refuses a malformed run without recording part of it",
    "evidence-unit",
    [] {
        return speclab::Test("text-draw-run-rejections")
            .Given("nothing", [] {})
            .When("nothing", [] {})
            .Then("a partial run and an out-of-range glyph index are both refused",
                  [] {
                      mdux::spec::Checks         checks;
                      const auto                 package = packageWithOneGlyph();
                      constexpr core::ColorRgba8 white{.r = 255, .g = 255, .b = 255, .a = 255};

                      Buffers buffers;
                      auto    list = draw::DrawList::create(buffers.vertices, buffers.indices, buffers.commands,
                                                            Buffers::budget());
                      checks.expect(list.has_value(), "the list is created");
                      if (!list.has_value()) {
                          checks.raise();
                          return;
                      }

                      // Seven bytes: one whole record and one byte of another. Refused before
                      // anything is recorded, so a truncated run cannot draw its first glyph and
                      // look merely short.
                      std::vector<std::byte> partial = record(0, 0, 0);
                      partial.push_back(std::byte{0});
                      auto partialResult = td::recordRun(*list, package, partial, 0, 0, white);
                      checks.expect(!partialResult.has_value()
                                        && partialResult.error() == td::DrawTextError::PartialRecord,
                                    "a partial run is refused");
                      checks.expect(list->vertices().empty(), "and nothing was recorded before the refusal");

                      // An index into the package's table, not the font's - a record built against
                      // a different package would otherwise draw a plausible wrong character.
                      auto stray = td::recordRun(*list, package, record(7, 0, 0), 0, 0, white);
                      checks.expect(!stray.has_value() && stray.error() == td::DrawTextError::GlyphIndexOutOfRange,
                                    "a glyph index past the package's table is refused");
                      checks.raise();
                  })
            .Execute();
    }};
