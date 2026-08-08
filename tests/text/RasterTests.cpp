/**
 * @file RasterTests.cpp
 * @brief BDD scenarios for the governed-zone glyph rasteriser (issue #159).
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-008 Zero-SOUP ML inference (decision 1, mirrored to text by ADR-010)
 * @compliance ADR-010 No on-device text shaping
 *
 * The outlines here are built in code rather than loaded from a font, for the reason
 * `TruetypeTests.cpp` gives: a fixture whose intended defect a reviewer has to take on trust is
 * a worse fixture. They are also chosen so their *expected coverage is derivable by hand* rather
 * than recorded from a run - a square aligned to pixel boundaries must be solid 255, a square
 * covering exactly half of each edge pixel must be 127, and a counter-rotating inner contour
 * must leave a hole. A test that only asserts "the same as last time" cannot distinguish correct
 * output from consistently wrong output, and the frozen-digest scenario at the bottom is exactly
 * that kind of test - which is why it sits alongside these rather than replacing them.
 *
 * Coordinates below use unitsPerEm 2048 and pixel sizes that divide it, so font units map to
 * pixels exactly and the arithmetic in an assertion is visible rather than approximate.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.text.raster;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace rr = mdux::text::raster;
using rr::RasterError;

constexpr std::uint16_t unitsPerEm = 2048;

/// One closed contour of on-curve points, in font units.
[[nodiscard]] std::vector<rr::OutlinePoint> box(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, bool clockwise = false) {
    if (clockwise) {
        return {{x0, y0, true}, {x0, y1, true}, {x1, y1, true}, {x1, y0, true}};
    }
    return {{x0, y0, true}, {x1, y0, true}, {x1, y1, true}, {x0, y1, true}};
}

struct Built {
    std::vector<rr::OutlinePoint>  points;
    std::vector<std::uint16_t>     ends;
};

/// Concatenates contours and derives the end-index list, so a scenario names shapes rather than
/// bookkeeping.
[[nodiscard]] Built contours(std::initializer_list<std::vector<rr::OutlinePoint>> shapes) {
    Built built;
    for (const auto& shape : shapes) {
        built.points.insert(built.points.end(), shape.begin(), shape.end());
        built.ends.push_back(static_cast<std::uint16_t>(built.points.size() - 1u));
    }
    return built;
}

[[nodiscard]] rr::RasterRequest requestFor(const Built& built, std::uint32_t pixelSize) {
    return rr::RasterRequest{.outline    = rr::Outline{.points = built.points, .contourEnds = built.ends},
                             .unitsPerEm = unitsPerEm,
                             .pixelSize  = pixelSize};
}

[[nodiscard]] std::uint8_t at(const rr::CoverageBitmap& bitmap, std::uint32_t x, std::uint32_t y) {
    return bitmap.coverage[static_cast<std::size_t>(y) * bitmap.width + x];
}

/// Renders a bitmap as text, so a failure message shows the shape rather than an index.
[[nodiscard]] std::string render(const rr::CoverageBitmap& bitmap) {
    std::string out = std::format("\n{}x{} origin=({},{})\n", bitmap.width, bitmap.height, bitmap.originX, bitmap.originY);
    for (std::uint32_t y = 0; y < bitmap.height; ++y) {
        for (std::uint32_t x = 0; x < bitmap.width; ++x) {
            out += std::format("{:4}", static_cast<int>(at(bitmap, x, y)));
        }
        out += '\n';
    }
    return out;
}

[[nodiscard]] rr::CoverageBitmap mustRasterise(const rr::RasterRequest& request, std::string_view what) {
    auto result = rr::rasterise(request);
    if (!result.has_value()) {
        throw speclab::core::AssertionFailure(std::format("{} was rejected: {}", what, rr::describe(result.error())),
                                              std::source_location::current());
    }
    return std::move(*result);
}

}  // namespace

// ---------------------------------------------------------------------------
// Coverage the assertion can derive rather than record.
// ---------------------------------------------------------------------------

const mdux::spec::Register solidSquareIsFullyCovered{
    "A pixel-aligned square rasterises to solid 255, reaching full coverage without a clamp",
    "evidence-unit",
    [] {
        // 2048 font units at upem 2048 and pixelSize 4 is exactly 4 pixels, so every pixel of the
        // result is entirely inside the contour. This is the scenario that proves 255 is
        // *reachable*: if the normalisation divided by anything larger than the true accumulator
        // ceiling, full coverage would read 254 and nothing else here would notice.
        struct State {
            Built                  built;
            rr::CoverageBitmap     bitmap;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-raster-solid-square")
            .Given("a square covering exactly 4x4 pixels", [state] { state->built = contours({box(0, 0, 2048, 2048)}); })
            .When("it is rasterised", [state] { state->bitmap = mustRasterise(requestFor(state->built, 4), "a 4x4 square"); })
            .Then("every pixel is 255 and the bitmap is 4x4",
                  [state] {
                      mdux::spec::Checks checks;
                      const auto&        b = state->bitmap;
                      checks.expect(b.width == 4 && b.height == 4, std::format("4x4 bitmap{}", render(b)));
                      if (b.width == 4 && b.height == 4) {
                          bool allFull = true;
                          for (std::uint8_t value : b.coverage) {
                              allFull = allFull && (value == 255);
                          }
                          checks.expect(allFull, std::format("every pixel fully covered{}", render(b)));
                      }
                      checks.expect(b.originX == 0, "left edge at the glyph origin");
                      checks.expect(b.originY == 4, "top edge 4 pixels above the baseline");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register halfCoveredColumnIsHalfValue{
    "A square ending mid-pixel yields half coverage in the partial column",
    "evidence-unit",
    [] {
        // At 2 pixels per em, one pixel is half an em, so 1536 font units is 1.5 pixels wide and
        // 1024 is 1 pixel tall. Column 0 is fully inside; column 1 is covered for exactly half
        // its width. Half of the 4096 accumulator ceiling is 2048, and 2048 * 255 / 4096 == 127
        // by integer division - derived, not recorded.
        struct State {
            Built              built;
            rr::CoverageBitmap bitmap;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-raster-half-covered-column")
            .Given("a rectangle 1.5 pixels wide and 1 pixel tall",
                   [state] { state->built = contours({box(0, 0, 1536, 1024)}); })
            .When("it is rasterised at 2 pixels per em",
                  [state] { state->bitmap = mustRasterise(requestFor(state->built, 2), "a 1.5x1 rectangle"); })
            .Then("column 0 is 255 and column 1 is 127",
                  [state] {
                      mdux::spec::Checks checks;
                      const auto&        b = state->bitmap;
                      checks.expect(b.width == 2 && b.height == 1, std::format("2x1 bitmap{}", render(b)));
                      if (b.width == 2 && b.height == 1) {
                          checks.expect(at(b, 0, 0) == 255, std::format("full column is 255{}", render(b)));
                          checks.expect(at(b, 1, 0) == 127, std::format("half column is 127{}", render(b)));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register counterContourLeavesHole{
    "A counter-rotating inner contour is subtracted by the nonzero winding rule",
    "evidence-unit",
    [] {
        // Two nested squares wound in opposite directions. Under nonzero winding the inner one
        // cancels the outer and leaves a hole; under even-odd it would too, so the discriminating
        // half of this scenario is its sibling below, where both wind the same way.
        struct State {
            Built              built;
            rr::CoverageBitmap bitmap;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-raster-counter-contour-hole")
            .Given("a 4x4 square with a counter-wound 2x2 square inside it",
                   [state] {
                       state->built = contours({box(0, 0, 2048, 2048), box(512, 512, 1536, 1536, /*clockwise=*/true)});
                   })
            .When("it is rasterised at 4 pixels per em",
                  [state] { state->bitmap = mustRasterise(requestFor(state->built, 4), "a square with a counter"); })
            .Then("the middle is empty and the border is solid",
                  [state] {
                      mdux::spec::Checks checks;
                      const auto&        b = state->bitmap;
                      checks.expect(b.width == 4 && b.height == 4, std::format("4x4 bitmap{}", render(b)));
                      if (b.width == 4 && b.height == 4) {
                          checks.expect(at(b, 1, 1) == 0 && at(b, 2, 1) == 0 && at(b, 1, 2) == 0 && at(b, 2, 2) == 0,
                                        std::format("the 2x2 centre is a hole{}", render(b)));
                          checks.expect(at(b, 0, 0) == 255 && at(b, 3, 3) == 255,
                                        std::format("the border stays solid{}", render(b)));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register sameWoundContourFillsSolid{
    "A same-wound inner contour does not make a hole, which is what makes the rule nonzero",
    "evidence-unit",
    [] {
        // The same geometry as above with the inner square wound the *same* way. Even-odd would
        // still punch a hole here; nonzero must not. Real fonts rely on this - an 'o' works only
        // because its counter is wound against the bowl, and a glyph whose contours happen to
        // agree must stay solid.
        struct State {
            Built              built;
            rr::CoverageBitmap bitmap;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-raster-same-wound-solid")
            .Given("a 4x4 square with a same-wound 2x2 square inside it",
                   [state] { state->built = contours({box(0, 0, 2048, 2048), box(512, 512, 1536, 1536)}); })
            .When("it is rasterised at 4 pixels per em",
                  [state] { state->bitmap = mustRasterise(requestFor(state->built, 4), "two same-wound squares"); })
            .Then("the result is solid, with no hole in the middle",
                  [state] {
                      mdux::spec::Checks checks;
                      const auto&        b = state->bitmap;
                      if (b.width == 4 && b.height == 4) {
                          checks.expect(at(b, 1, 1) == 255 && at(b, 2, 2) == 255,
                                        std::format("the centre stays filled{}", render(b)));
                      } else {
                          checks.expect(false, std::format("4x4 bitmap{}", render(b)));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register triangleCornerIsPartial{
    "A diagonal edge produces partial coverage that increases along the wedge",
    "evidence-unit",
    [] {
        // A right triangle over 4x4 pixels, with the right angle at the origin and the hypotenuse
        // running from (4,0) to (0,4) in pixel space. That line bisects every pixel it crosses,
        // so each diagonal pixel is covered exactly half and reads 2048 * 255 / 4096 == 127 -
        // derivable, so this asserts the value rather than merely that it is "somewhere between".
        struct State {
            Built              built;
            rr::CoverageBitmap bitmap;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-raster-triangle-corner")
            .Given("a right triangle filling the lower-left half of a 4x4 box",
                   [state] {
                       state->built = contours({std::vector<rr::OutlinePoint>{{0, 0, true}, {2048, 0, true}, {0, 2048, true}}});
                   })
            .When("it is rasterised at 4 pixels per em",
                  [state] { state->bitmap = mustRasterise(requestFor(state->built, 4), "a triangle"); })
            .Then("the right-angle corner is solid, the opposite corner empty, the diagonal partial",
                  [state] {
                      mdux::spec::Checks checks;
                      const auto&        b = state->bitmap;
                      checks.expect(b.width == 4 && b.height == 4, std::format("4x4 bitmap{}", render(b)));
                      if (b.width == 4 && b.height == 4) {
                          // Bitmap row 3 is the bottom of the glyph, row 0 the top.
                          checks.expect(at(b, 0, 3) == 255, std::format("the right-angle corner is solid{}", render(b)));
                          checks.expect(at(b, 3, 0) == 0, std::format("the far corner is empty{}", render(b)));
                          // Every pixel the hypotenuse crosses, halved.
                          for (std::uint32_t i = 0; i < 4; ++i) {
                              checks.expect(at(b, i, i) == 127,
                                            std::format("diagonal pixel ({0},{0}) is 127, got {1}{2}", i,
                                                        static_cast<int>(at(b, i, i)), render(b)));
                          }
                          // And the halves either side of it: below-left solid, above-right empty.
                          checks.expect(at(b, 0, 1) == 255 && at(b, 1, 2) == 255,
                                        std::format("below the diagonal is solid{}", render(b)));
                          checks.expect(at(b, 2, 1) == 0 && at(b, 3, 2) == 0,
                                        std::format("above the diagonal is empty{}", render(b)));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register quadraticCurveApexIsExact{
    "A quadratic curve reaches exactly its computed apex, and implied midpoints do too",
    "evidence-unit",
    [] {
        // The regression test for the subdivision bug found in review: `buildEdges()` used to
        // advance the pen before evaluating the next sample, so from step 2 onward the evaluator
        // received the previous *sample* as the curve start rather than the curve's own. The
        // endpoints still landed correctly - the final sample's `u` term is zero - so only an
        // assertion about the middle of a curve can see it. Measured deviation on a 5-segment
        // curve was 266 fixed units, just over one pixel.
        //
        // The apex here is derivable. For a quadratic, B(1/2) = (p0 + 2c + p1) / 4; with
        // p0.x = 1024, c.x = 2048 and p1.x = 1024 that is 1536 font units, which at 8 px/em over
        // a 2048 em is exactly 6.0 pixels. So a correct rasteriser produces a bitmap exactly 6
        // pixels wide. The buggy one pushed the curve outward and produced 7.
        //
        // The curve needs 6 segments at this size, so it is well past the >= 3 threshold where
        // the defect appears at all.
        struct State {
            Built              single;
            Built              implied;
            rr::CoverageBitmap singleBitmap;
            rr::CoverageBitmap impliedBitmap;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-raster-quadratic-apex")
            .Given("a box whose right side bulges out on one quadratic, and one built from two adjacent off-curve points",
                   [state] {
                       state->single = contours({std::vector<rr::OutlinePoint>{
                           {0, 0, true}, {1024, 0, true}, {2048, 1024, false}, {1024, 2048, true}, {0, 2048, true}}});
                       state->implied = contours({std::vector<rr::OutlinePoint>{
                           {0, 0, true}, {1024, 0, true}, {2048, 512, false}, {2048, 1536, false}, {1024, 2048, true}, {0, 2048, true}}});
                   })
            .When("both are rasterised at 8 pixels per em",
                  [state] {
                      state->singleBitmap  = mustRasterise(requestFor(state->single, 8), "a bulging box");
                      state->impliedBitmap = mustRasterise(requestFor(state->implied, 8), "an implied-midpoint box");
                  })
            .Then("the single-control curve stops exactly at its apex, and the implied one fills too",
                  [state] {
                      mdux::spec::Checks checks;
                      const auto&        single = state->singleBitmap;
                      // The derived assertion. 7 here means the subdivision regressed.
                      checks.expect(single.width == 6 && single.height == 8,
                                    std::format("apex at exactly 6 px wide, 8 tall{}", render(single)));
                      if (single.width == 6 && single.height == 8) {
                          // Guarded: expect() is non-fatal, so indexing outside this branch would
                          // read past `coverage` and bury the geometry failure under a crash.
                          checks.expect(at(single, 0, 4) == 255, std::format("the interior is filled{}", render(single)));
                          checks.expect(at(single, 5, 0) < 255 && at(single, 5, 4) > 0,
                                        std::format("the curved edge is antialiased{}", render(single)));
                      }

                      const auto& implied = state->impliedBitmap;
                      checks.expect(implied.width >= 6 && implied.height == 8,
                                    std::format("the implied-midpoint curve fills a comparable box{}", render(implied)));
                      if (implied.width >= 6 && implied.height == 8) {
                          checks.expect(at(implied, 0, 4) == 255, std::format("its interior is filled{}", render(implied)));
                          const bool anyPartial =
                              std::ranges::any_of(implied.coverage, [](std::uint8_t v) { return v > 0 && v < 255; });
                          checks.expect(anyPartial, std::format("its edges are antialiased{}", render(implied)));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register blankOutlineYieldsEmptyBitmap{
    "An outline that encloses no area yields an empty bitmap rather than a 1x1 of zeros",
    "evidence-unit",
    [] {
        // The space character's shape, once S4 feeds a real font through: a contour exists but is
        // degenerate. An atlas packer must be able to tell "nothing to pack" from "one blank
        // pixel to pack", so the distinction is part of the contract rather than a detail.
        struct State {
            Built              built;
            rr::CoverageBitmap bitmap;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-raster-blank-outline")
            .Given("a contour whose points are collinear along a horizontal line",
                   [state] {
                       state->built = contours({std::vector<rr::OutlinePoint>{{0, 0, true}, {1024, 0, true}, {2048, 0, true}}});
                   })
            .When("it is rasterised", [state] { state->bitmap = mustRasterise(requestFor(state->built, 8), "a collinear contour"); })
            .Then("the bitmap is empty",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->bitmap.width == 0 && state->bitmap.height == 0, "zero extent");
                      checks.expect(state->bitmap.coverage.empty(), "no coverage bytes");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register coverageStaysInRangeAcrossSizes{
    "Coverage spans the full byte range across pixel sizes, and 255 is never reached by saturation",
    "evidence-unit",
    [] {
        // Issue #159 asks for coverage in [0,255] "with no clamping surprises". A uint8 is
        // trivially in range, so asserting that would assert nothing. What is worth asserting is
        // the property the implementation claims: the accumulator ceiling is exact, so full
        // coverage lands on 255 at every size, and a shape with an unaligned edge produces
        // intermediate values rather than a two-level image.
        return speclab::Test("text-raster-coverage-range")
            .Given("nothing", [] {})
            .When("nothing", [] {})
            .Then("every pixel size produces a solid 255 interior and some partial edge",
                  [] {
                      mdux::spec::Checks checks;
                      const Built        solid   = contours({box(0, 0, 2048, 2048)});
                      const Built        skewed  = contours({std::vector<rr::OutlinePoint>{
                          {100, 100, true}, {1900, 300, true}, {1700, 1900, true}, {200, 1500, true}}});
                      for (const std::uint32_t pixelSize : {1u, 2u, 3u, 5u, 8u, 13u, 32u, 64u}) {
                          auto full = rr::rasterise(requestFor(solid, pixelSize));
                          checks.expect(full.has_value(), std::format("size {}: the solid square rasterises", pixelSize));
                          if (full.has_value() && !full->coverage.empty()) {
                              const auto maxValue = *std::ranges::max_element(full->coverage);
                              checks.expect(maxValue == 255, std::format("size {}: full coverage is exactly 255, got {}", pixelSize,
                                                                         static_cast<int>(maxValue)));
                          }
                          auto skew = rr::rasterise(requestFor(skewed, pixelSize));
                          checks.expect(skew.has_value(), std::format("size {}: the skewed quad rasterises", pixelSize));
                          if (skew.has_value() && pixelSize >= 8u) {
                              const bool anyPartial = std::ranges::any_of(skew->coverage, [](std::uint8_t v) { return v > 0 && v < 255; });
                              checks.expect(anyPartial, std::format("size {}: unaligned edges are antialiased", pixelSize));
                          }
                      }
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Rejection corpus.
// ---------------------------------------------------------------------------

const mdux::spec::Register rasterRejections{
    "rasterise() emits the right stable code per failure mode",
    "evidence-unit",
    [] {
        struct Case {
            std::string_view                   what;
            RasterError                        expected;
            std::function<rr::RasterRequest()> build;
        };

        // Held outside the lambdas so the spans in each request stay alive while it is used.
        static const Built square = contours({box(0, 0, 2048, 2048)});

        const std::vector<Case> cases{
            {"a zero unitsPerEm", RasterError::UnsupportedUnitsPerEm,
             [] {
                 auto request       = requestFor(square, 8);
                 request.unitsPerEm = 0;
                 return request;
             }},
            {"a zero pixelSize", RasterError::UnsupportedPixelSize,
             [] {
                 return requestFor(square, 0);
             }},
            {"a pixelSize past the supported maximum", RasterError::UnsupportedPixelSize,
             [] {
                 return requestFor(square, rr::maxPixelSize + 1u);
             }},
            {"an outline with no contours", RasterError::EmptyOutline,
             [] {
                 auto request                    = requestFor(square, 8);
                 request.outline.contourEnds     = {};
                 return request;
             }},
            {"an outline with no points", RasterError::EmptyOutline,
             [] {
                 auto request            = requestFor(square, 8);
                 request.outline.points  = {};
                 return request;
             }},
            {"contour ends that do not increase", RasterError::MalformedContours,
             [] {
                 static const std::vector<std::uint16_t> ends{3, 3};
                 auto                                    request = requestFor(square, 8);
                 request.outline.contourEnds                      = ends;
                 return request;
             }},
            {"a contour end past the last point", RasterError::MalformedContours,
             [] {
                 static const std::vector<std::uint16_t> ends{99};
                 auto                                    request = requestFor(square, 8);
                 request.outline.contourEnds                      = ends;
                 return request;
             }},
        };

        return speclab::Test("text-raster-rejections")
            .Given("a corpus of deliberately malformed requests", [] {})
            .When("each is rasterised", [] {})
            .Then("each yields exactly the RasterError identified in the corpus",
                  [&cases] {
                      mdux::spec::Checks checks;
                      for (const Case& entry : cases) {
                          auto result = rr::rasterise(entry.build());
                          checks.expect(!result.has_value(), std::format("{}: rasterise succeeded unexpectedly", entry.what));
                          if (!result.has_value()) {
                              checks.expect(result.error() == entry.expected,
                                            std::format("{}: got '{}', expected '{}'", entry.what, rr::describe(result.error()),
                                                        rr::describe(entry.expected)));
                          }
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register sweepWorkBoundIsEnforced{
    "An outline whose sweep cost explodes is refused before the sweep, not merely before allocation",
    "evidence-unit",
    [] {
        // The denial-of-service path found in review. The bitmap-area limit does not bound the
        // sweep: `contourEnds` holds uint16 indices so an outline can carry ~65k points, each
        // off-curve one flattening into up to maxCurveSegments edges, while a tall narrow glyph
        // reaches a large height far under maxBitmapPixels. The old sweep was
        // height * subScanlines * edgeCount and would have run for hours on the fixture below.
        //
        // The shape is a comb: one closed contour zigzagging between y=0 and y=2048 across 2400
        // points, so every edge - including the return segment that closes each stroke - spans
        // the full height. At 4096 px/em over a 2048 em the scale is 2 px per font unit, making
        // the glyph 4096 pixels tall and about 2398 wide.
        //
        //   edge-row visits : 2400 edges * 4096 rows = 9,830,400   (budget 4,194,304 - exceeded)
        //   bitmap          : 2398 * 4096            = 9,822,208   (limit 67,108,864 - inside)
        //
        // That combination is the point: the request is comfortably legal by area and is refused
        // only because the sweep it asks for is not.
        struct State {
            Built built;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-raster-sweep-work-bound")
            .Given("a comb of ~1200 full-height strokes at the maximum pixel size",
                   [state] {
                       std::vector<rr::OutlinePoint> points;
                       points.reserve(2400);
                       for (std::int32_t i = 0; i < 1200; ++i) {
                           points.push_back({i, 0, true});
                           points.push_back({i, 2048, true});
                       }
                       state->built = contours({points});
                   })
            .When("nothing", [] {})
            .Then("it is refused with OutlineTooComplex, and a normal glyph at the same size still works",
                  [state] {
                      mdux::spec::Checks checks;
                      auto              result = rr::rasterise(requestFor(state->built, rr::maxPixelSize));
                      checks.expect(!result.has_value(), "the comb is refused");
                      if (!result.has_value()) {
                          checks.expect(result.error() == RasterError::OutlineTooComplex,
                                        std::format("got '{}', expected '{}'", rr::describe(result.error()),
                                                    rr::describe(RasterError::OutlineTooComplex)));
                      }
                      // The bound must not reject ordinary work. A plain square at the same
                      // maximum pixel size is 4096x4096 - the largest legitimate request there is
                      // - and has to keep rasterising.
                      const Built square = contours({box(0, 0, 2048, 2048)});
                      auto        ok     = rr::rasterise(requestFor(square, rr::maxPixelSize));
                      checks.expect(ok.has_value(), "a full-size square is still accepted");
                      if (ok.has_value()) {
                          checks.expect(ok->width == 4096 && ok->height == 4096,
                                        std::format("4096x4096, got {}x{}", ok->width, ok->height));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register wideFillIsBoundedByBitmapArea{
    "A very wide solid outline fills in time proportional to its area, not to its span width",
    "evidence-unit",
    [] {
        // The companion to the sweep-work bound, and the second half of a review finding: the
        // sweep budget counts edge-row visits, which a solid rectangle barely spends - four
        // edges, two of them vertical. What it does spend is *fill*, and writing coverage pixel
        // by pixel made that `height * subScanlines * spanWidth`, unbounded by anything above.
        //
        // This rectangle is 16384 x 512 pixels: 1024 edge-row visits, trivially inside the sweep
        // budget, but 512 * 16 * 16384 = 134,217,728 per-pixel writes under the old scheme. Spans
        // are now recorded as range-adds and resolved by one prefix pass per row, so the cost is
        // O(bitmap area) - already bounded by maxBitmapPixels - and the scenario completes in
        // well under a second. A regression to per-pixel writes would show up here as a timeout
        // rather than a wrong answer, which is why the assertions below also check the coverage:
        // a fast wrong answer would otherwise pass.
        struct State {
            Built              built;
            rr::CoverageBitmap bitmap;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-raster-wide-fill-bounded")
            .Given("a rectangle 16384 pixels wide and 512 tall",
                   [state] { state->built = contours({box(0, 0, 65536, 2048)}); })
            .When("it is rasterised at 512 pixels per em",
                  [state] { state->bitmap = mustRasterise(requestFor(state->built, 512), "a very wide rectangle"); })
            .Then("it is the expected extent and solid throughout",
                  [state] {
                      mdux::spec::Checks checks;
                      const auto&        b = state->bitmap;
                      checks.expect(b.width == 16384 && b.height == 512,
                                    std::format("16384x512, got {}x{}", b.width, b.height));
                      if (b.width == 16384 && b.height == 512) {
                          checks.expect(at(b, 0, 0) == 255 && at(b, 16383, 511) == 255 && at(b, 8192, 256) == 255,
                                        "corners and centre are solid");
                          const bool allFull = std::ranges::all_of(b.coverage, [](std::uint8_t v) { return v == 255; });
                          checks.expect(allFull, "every pixel of a pixel-aligned rectangle is fully covered");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Cross-toolchain determinism.
// ---------------------------------------------------------------------------

/// The digest the reference glyph must produce. See the scenario body for why this is never
/// updated to match new output.
constexpr std::string_view frozenDigest = "1b998a7518a981f22d0a917dad7a478cd3ff1ee242619f8d315db4f365081b24";

/// Deliberately coprime with the em, so no coordinate lands on a pixel boundary by accident.
constexpr std::uint32_t determinismPixelSize = 37;

/// Builds the reference glyph the determinism scenario digests: a shape with a straight edge, a
/// diagonal, a curve, and a counter, at a size where none of them land on pixel boundaries. The
/// awkward coordinates are the point - a shape that happened to align would hide exactly the
/// rounding disagreements this scenario exists to catch.
[[nodiscard]] Built referenceGlyph() {
    return contours({std::vector<rr::OutlinePoint>{{137, 91, true},
                                                   {1613, 241, true},
                                                   {1901, 1033, false},
                                                   {1187, 1811, true},
                                                   {449, 1699, true},
                                                   {211, 1109, false},
                                                   {137, 91, true}},
                     std::vector<rr::OutlinePoint>{{701, 743, true}, {683, 1201, true}, {1163, 1231, true}, {1181, 761, true}}});
}

const mdux::spec::Register rasterDeterminism{
    "The reference glyph rasterises to a frozen digest on every toolchain",
    "determinism",
    [] {
        /*
         * ## What the frozen digest below actually is
         *
         * Not "the correct rendering" in any perceptual sense, and nothing here claims it is. It
         * is what the algorithm specified in Raster.cppm produces, recorded once, so that **every
         * toolchain must produce the same thing**. The Linux/GCC and Windows/MSVC legs run this
         * identical file; if either disagrees in one byte, issue #159's acceptance criterion -
         * "the same glyph rasterises byte-identically on MSVC and GCC" - has been falsified.
         *
         * So a failure here is never fixed by updating the constant. It means one of:
         *   - the flattening segment count, the fixed-point scale, or the sampling grid changed,
         *   - a division changed rounding mode (floorDiv replaced by `/`, most likely),
         *   - the crossing sort stopped being a total order and two equal-x crossings swapped,
         *   - floating point entered a file whose contract is that it contains none,
         *   - the compiler is miscompiling the rasteriser.
         *
         * Updating the constant to match new output converts the one loud signal in this
         * subsystem into a rubber stamp. Change it only alongside a deliberate, documented change
         * to the specified algorithm - and in the same commit as that change, so the diff shows
         * both halves. The sibling scenarios above are what keep this one honest: they assert
         * coverage that is derivable by hand, so a change that makes the rasteriser consistently
         * *wrong* fails there rather than silently re-freezing here.
         */
        struct State {
            Built                    built;
            rr::CoverageBitmap       bitmap;
            std::array<char, 64>     hex{};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-raster-determinism-crossToolchain")
            .Given("the reference glyph", [state] { state->built = referenceGlyph(); })
            .When("it is rasterised and its coverage digested",
                  [state] {
                      state->bitmap = mustRasterise(requestFor(state->built, determinismPixelSize), "the reference glyph");
                      const auto digest =
                          mdux::evidence::sha256(std::as_bytes(std::span<const std::uint8_t>{state->bitmap.coverage}));
                      state->hex = mdux::evidence::toHex(digest);
                  })
            .Then("the digest and the bitmap geometry match the frozen values",
                  [state] {
                      mdux::spec::Checks checks;
                      const std::string_view actual{state->hex.data(), state->hex.size()};
                      // Geometry is asserted alongside the digest so a failure says which of the
                      // two moved: a size change is a different bug from a coverage change, and
                      // the digest alone cannot tell them apart.
                      checks.expect(state->bitmap.width == 29 && state->bitmap.height == 32,
                                    std::format("bitmap is 29x32, got {}x{}", state->bitmap.width, state->bitmap.height));
                      checks.expect(actual == frozenDigest,
                                    std::format("coverage digest\n  expected {}\n  actual   {}", frozenDigest, actual));
                      checks.raise();
                  })
            .Execute();
    }};
