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

#include "../framework/RunRecords.hpp"
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

using mdux::spec::runRecord;

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
                      const auto         positive = runRecord(0x0102, 300, -40);
                      auto               decoded  = td::decodeRecord(positive);
                      checks.expect(decoded.has_value(), "a whole record decodes");
                      if (decoded.has_value()) {
                          checks.expect(decoded->packageIndex == 0x0102, "packageIndex reads bytes 0-1");
                          checks.expect(decoded->x == 300, "x reads bytes 2-3");
                          checks.expect(decoded->y == -40,
                                        std::format("y is sign-extended, got {}", decoded->y));
                      }
                      // A short buffer is a partial record, which the schema refuses to let reach
                      // a device precisely because a partial read means something different
                      // between two struct layouts.
                      const std::array<std::byte, 5> truncated{};
                      auto                           short_ = td::decodeRecord(truncated);
                      checks.expect(!short_.has_value() && short_.error() == td::DrawTextError::RecordSizeWrong,
                                    "a five-byte record is refused");
                      // And a whole run handed to the single-record decoder, which would otherwise
                      // return the first glyph and look like it had worked.
                      std::vector<std::byte> two = runRecord(1, 0, 0);
                      const auto             second = runRecord(2, 0, 0);
                      two.insert(two.end(), second.begin(), second.end());
                      auto oversize = td::decodeRecord(two);
                      checks.expect(!oversize.has_value() && oversize.error() == td::DrawTextError::RecordSizeWrong,
                                    "a two-record span is refused rather than truncated");
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
                      std::vector<std::byte> partial = runRecord(0, 0, 0);
                      partial.push_back(std::byte{0});
                      auto partialResult = td::recordRun(*list, package, partial, 0, 0, white);
                      checks.expect(!partialResult.has_value()
                                        && partialResult.error() == td::DrawTextError::PartialRecord,
                                    "a partial run is refused");
                      checks.expect(list->vertices().empty(), "and nothing was recorded before the refusal");

                      // An index into the package's table, not the font's - a record built against
                      // a different package would otherwise draw a plausible wrong character.
                      auto stray = td::recordRun(*list, package, runRecord(7, 0, 0), 0, 0, white);
                      checks.expect(!stray.has_value() && stray.error() == td::DrawTextError::GlyphIndexOutOfRange,
                                    "a glyph index past the package's table is refused");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aFailedRunLeavesNothingBehind{
    "A run that fails on its second glyph records neither glyph",
    "evidence-unit",
    [] {
        // The rejections above all fail before anything is recorded, so they would pass even if
        // recordRun() left half a run in the list. This is the case that does not: a good record
        // followed by a bad one. Without the rollback the list keeps one rectangle, and a fragment
        // of a word reaches the frame - which the header promises it cannot.
        return speclab::Test("text-draw-run-rollback")
            .Given("a list with a rectangle already recorded by someone else", [] {})
            .When("a two-glyph run fails on its second record", [] {})
            .Then("the run leaves nothing, and the earlier rectangle survives",
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

                      // Someone else's rectangle. Rolling back the run must not take this with it,
                      // which is the difference between rollback() and reset().
                      constexpr core::Rect earlier{.x = 0, .y = 0, .width = 2, .height = 2};
                      checks.expect(list->addSolidRect(earlier, white).has_value(), "the earlier rect is recorded");
                      const auto verticesBefore = list->vertices().size();
                      const auto indicesBefore  = list->indices().size();
                      const auto commandsBefore = list->commands().size();

                      // Glyph 0 exists; glyph 9 does not. The first is recorded, then the run fails.
                      std::vector<std::byte> run = runRecord(0, 0, 0);
                      const auto             bad = runRecord(9, 8, 0);
                      run.insert(run.end(), bad.begin(), bad.end());

                      auto result = td::recordRun(*list, package, run, 0, 0, white);
                      checks.expect(!result.has_value()
                                        && result.error() == td::DrawTextError::GlyphIndexOutOfRange,
                                    "the run is refused");
                      checks.expect(list->vertices().size() == verticesBefore,
                                    std::format("no vertices left behind: {} before, {} after", verticesBefore,
                                                list->vertices().size()));
                      checks.expect(list->indices().size() == indicesBefore, "no indices left behind");
                      checks.expect(list->commands().size() == commandsBefore, "no commands left behind");
                      // And the rollback stopped where it was told to, rather than emptying the list.
                      checks.expect(verticesBefore == 4 && !list->empty(),
                                    "the earlier rectangle is still there");
                      checks.raise();
                  })
            .Execute();
    }};
