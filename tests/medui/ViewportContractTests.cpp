/**
 * @file ViewportContractTests.cpp
 * @brief BDD scenarios for `mdux.medui.viewport` (issue #322): the streaming-viewport data and
 *        composition contract.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: this suite links MduX::Core only)
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-022 Streaming viewport data and composition contract
 *
 * This module records no primitive and touches no `DrawList` - #323 does that. So this suite checks
 * numbers rather than vertices: where a cell's rectangle lands, what colour a sample maps to, which
 * ring shapes `validate()` admits, and which refusal each malformed input earns.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.core.units;
import mdux.medui.viewport;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace ms   = mdux::medui;
namespace core = mdux::core;

constexpr core::Rect          band{.x = 10, .y = 20, .width = 100, .height = 40};
constexpr core::ColorRgba8    cold{.r = 0, .g = 0, .b = 200, .a = 255};
constexpr core::ColorRgba8    hot{.r = 220, .g = 20, .b = 0, .a = 255};
constexpr ms::WaterfallStyle  unitRamp{.minimum = 0.0F, .maximum = 1.0F, .lowColor = cold, .highColor = hot};

/// A grid over `storage` of `bins` scalars per row, unwrapped: the oldest row is index 0.
[[nodiscard]] ms::WaterfallGrid gridOver(std::span<const float> storage, std::size_t bins) noexcept {
    return ms::WaterfallGrid{.storage = storage, .bins = bins, .oldestRow = 0, .rowCount = storage.size() / bins};
}

/// Hard failure (REQUIRE-equivalent): a grid/style pair expected to validate must have.
void requireValid(core::ResultVoid<ms::WaterfallError> result, std::string_view what, std::source_location where = std::source_location::current()) {
    if (!result.has_value()) {
        throw speclab::core::AssertionFailure(std::format("{}: {}", what, ms::describe(result.error())), where);
    }
}

/// Hard failure (REQUIRE-equivalent): a grid/style pair expected to be refused must be refused.
[[nodiscard]] ms::WaterfallError
requireRefused(core::ResultVoid<ms::WaterfallError> result, std::string_view what, std::source_location where = std::source_location::current()) {
    if (result.has_value()) {
        throw speclab::core::AssertionFailure(std::format("{}: expected a refusal but the grid validated", what), where);
    }
    return result.error();
}

// ---------------------------------------------------------------------------
// Composition: cell geometry
// ---------------------------------------------------------------------------

const mdux::spec::Register cellsTileTheBandExactly{
    "Every cell of a grid tiles the band exactly, with no gap and no overflow",
    "evidence-unit",
    [] {
        return speclab::Test("viewport-cells-tile-the-band")
            .Given("a 4-row, 5-bin grid over a band divisible by both", [] {})
            .When("every cell's rectangle is computed", [] {})
            .Then("adjacent cells touch with no gap, and the far edges land on the band's own edges",
                  [] {
                      constexpr std::size_t rows = 4;
                      constexpr std::size_t cols = 5;
                      mdux::spec::Checks    checks;

                      for (std::size_t row = 0; row < rows; ++row) {
                          core::Px runningRight = band.x;
                          for (std::size_t col = 0; col < cols; ++col) {
                              const core::Rect cell = ms::waterfallCellRect(band, rows, cols, row, col);
                              checks.expect(cell.x == runningRight, std::format("row {} col {} starts where the previous cell ended", row, col));
                              checks.expect(cell.y >= band.y && cell.bottom() <= band.bottom(), std::format("row {} col {} stays within the band's rows", row, col));
                              runningRight = cell.right();
                          }
                          checks.expect(runningRight == band.right(), std::format("row {}'s last cell reaches the band's right edge", row));
                      }

                      core::Px runningBottom = band.y;
                      for (std::size_t row = 0; row < rows; ++row) {
                          const core::Rect cell = ms::waterfallCellRect(band, rows, cols, row, 0);
                          checks.expect(cell.y == runningBottom, std::format("row {} starts where the previous row ended", row));
                          runningBottom = cell.bottom();
                      }
                      checks.expect(runningBottom == band.bottom(), "the last row reaches the band's bottom edge");

                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register unevenDivisionAbsorbsTheRemainderAtTheFarEdge{
    "A band that does not divide evenly still tiles exactly, remainder on the last cell",
    "evidence-unit",
    [] {
        return speclab::Test("viewport-uneven-division-absorbs-remainder")
            .Given("a band whose width is not a multiple of the bin count", [] {})
            .When("every cell in one row is computed", [] {})
            .Then("every cell but the last is the same width, and the last keeps what is left over",
                  [] {
                      constexpr core::Rect oddBand{.x = 0, .y = 0, .width = 17, .height = 10};
                      constexpr std::size_t cols = 5;  // 17 / 5 = 3 remainder 2
                      mdux::spec::Checks    checks;

                      core::Px runningRight = oddBand.x;
                      for (std::size_t col = 0; col < cols; ++col) {
                          const core::Rect cell = ms::waterfallCellRect(oddBand, 1, cols, 0, col);
                          checks.expect(cell.x == runningRight, std::format("bin {} starts where the previous one ended", col));
                          if (col + 1 < cols) {
                              checks.expect(cell.width == 3, std::format("bin {} is the plain 3px width, got {}", col, cell.width));
                          } else {
                              checks.expect(cell.width == 5, std::format("the last bin absorbs the remainder (3 + 2), got {}", cell.width));
                          }
                          runningRight = cell.right();
                      }
                      checks.expect(runningRight == oddBand.right(), "the tiling still reaches the band's right edge exactly");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register outOfContractIndicesAreZeroRects{
    "A degenerate call - no rows, no bins, or an out-of-range index - is a zero-area rectangle, not undefined",
    "evidence-unit",
    [] {
        return speclab::Test("viewport-degenerate-cell-is-zero-rect")
            .Given("zero rows, zero bins, and an index past each", [] {})
            .When("each is asked for its cell rectangle", [] {})
            .Then("every one comes back zero-area, at the band's own origin",
                  [] {
                      mdux::spec::Checks checks;
                      for (const core::Rect& cell : {ms::waterfallCellRect(band, 0, 5, 0, 0),
                                                      ms::waterfallCellRect(band, 5, 0, 0, 0),
                                                      ms::waterfallCellRect(band, 5, 5, 5, 0),
                                                      ms::waterfallCellRect(band, 5, 5, 0, 5)}) {
                          checks.expect(cell.width == 0 && cell.height == 0, "the rectangle encloses no area");
                          checks.expect(cell.x == band.x && cell.y == band.y, "the rectangle sits at the band's origin");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Composition: the colour ramp
// ---------------------------------------------------------------------------

const mdux::spec::Register rampEndpointsAreExact{
    "A sample at either end of the range paints exactly that end's colour",
    "evidence-unit",
    [] {
        return speclab::Test("viewport-ramp-endpoints-exact")
            .Given("samples at the range's minimum and maximum", [] {})
            .When("each is mapped through the ramp", [] {})
            .Then("the low sample is exactly lowColor and the high sample is exactly highColor",
                  [] {
                      mdux::spec::Checks checks;
                      checks.expect(ms::waterfallCellColor(0.0F, unitRamp) == cold, "the minimum maps to the ramp's low end");
                      checks.expect(ms::waterfallCellColor(1.0F, unitRamp) == hot, "the maximum maps to the ramp's high end");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register rampClampsExcursions{
    "A sample outside the declared range clamps to the rail it overshot, never extrapolating past it",
    "evidence-unit",
    [] {
        return speclab::Test("viewport-ramp-clamps-excursions")
            .Given("samples far outside the declared range, and a NaN", [] {})
            .When("each is mapped through the ramp", [] {})
            .Then("the below-range and NaN samples are exactly lowColor, and the above-range sample is exactly highColor",
                  [] {
                      mdux::spec::Checks checks;
                      checks.expect(ms::waterfallCellColor(-40.0F, unitRamp) == cold, "an excursion below the rail pins to lowColor");
                      checks.expect(ms::waterfallCellColor(90.0F, unitRamp) == hot, "an excursion above the rail pins to highColor");
                      checks.expect(ms::waterfallCellColor(std::numeric_limits<float>::quiet_NaN(), unitRamp) == cold,
                                    "a NaN sample is treated as at-or-below the low rail, never propagated");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register rampHandlesADegenerateStyleWithoutFaulting{
    "A degenerate ramp - an empty or non-finite range - still returns a defined colour",
    "evidence-unit",
    [] {
        return speclab::Test("viewport-ramp-degenerate-style-is-defined")
            .Given("a style whose range is empty", [] {})
            .When("a sample is mapped through it directly, bypassing validate()", [] {})
            .Then("the fallback is the ramp's own low colour, not a fault",
                  [] {
                      constexpr ms::WaterfallStyle empty{.minimum = 1.0F, .maximum = 1.0F, .lowColor = cold, .highColor = hot};
                      mdux::spec::Checks           checks;
                      checks.expect(ms::waterfallCellColor(0.5F, empty) == cold, "an empty range's fallback is lowColor, defined rather than UB");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aFullScaleRangeStillSeparatesItsRamp{
    "A full-scale range maps its midpoint and its maximum, rather than folding both to the low colour",
    "evidence-unit",
    [] {
        return speclab::Test("viewport-full-scale-range-separates-ramp")
            .Given("a range spanning the whole float line, and samples at its middle and its top", [] {})
            .When("each is mapped through the ramp", [] {})
            .Then("the middle sample is the ramp's midpoint colour and the top sample is its high end, not both the low end",
                  [] {
                      // Every value here is finite, so `validate()`'s checks pass - and `maximum -
                      // minimum` overflows to infinity in float, exactly the arithmetic
                      // `mdux::medui::rowFor()` in Trace.cpp already had to widen to double for.
                      // Before that widening, this scenario returned black for both 0.0F and
                      // FLT_MAX instead of grey and white - the largest sample indistinguishable
                      // from the smallest, which is the one failure a monitor must not have.
                      constexpr core::ColorRgba8   black{.r = 0, .g = 0, .b = 0, .a = 255};
                      constexpr core::ColorRgba8   white{.r = 255, .g = 255, .b = 255, .a = 255};
                      constexpr float               extreme = std::numeric_limits<float>::max();
                      constexpr ms::WaterfallStyle wide{.minimum = -extreme, .maximum = extreme, .lowColor = black, .highColor = white};

                      const core::ColorRgba8 mid = ms::waterfallCellColor(0.0F, wide);
                      const core::ColorRgba8 top = ms::waterfallCellColor(extreme, wide);

                      mdux::spec::Checks checks;
                      checks.expect(mid == core::ColorRgba8{.r = 128, .g = 128, .b = 128, .a = 255},
                                    std::format("the midpoint is mid-grey (128), got ({}, {}, {})", mid.r, mid.g, mid.b));
                      checks.expect(top == white, std::format("the maximum is white (255), got ({}, {}, {})", top.r, top.g, top.b));
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// The ring
// ---------------------------------------------------------------------------

const mdux::spec::Register wrappedGridReadsOldestFirst{
    "A wrapped grid is read oldest-row-first, from its own oldest index",
    "evidence-unit",
    [] {
        return speclab::Test("viewport-wrapped-grid-order")
            .Given("a 2-bin, 3-row-capacity grid whose producer has wrapped past the end of its storage", [] {})
            .When("all three rows are read by position", [] {})
            .Then("they come back in the order the producer wrote them",
                  [] {
                      // Physical rows are {0.3, 0.4}, {0.5, 0.6}, {0.1, 0.2}, and the producer has
                      // wrapped so physical row 2 - the last one written - is the oldest live row.
                      static constexpr std::array<float, 6> storage{0.3F, 0.4F, 0.5F, 0.6F, 0.1F, 0.2F};
                      const ms::WaterfallGrid                grid{.storage = storage, .bins = 2, .oldestRow = 2, .rowCount = 3};

                      mdux::spec::Checks checks;
                      checks.expect(grid.at(0, 0) == 0.1F && grid.at(0, 1) == 0.2F, "logical row 0 is the oldest physical row");
                      checks.expect(grid.at(1, 0) == 0.3F && grid.at(1, 1) == 0.4F, "logical row 1 follows, wrapping to physical row 0");
                      checks.expect(grid.at(2, 0) == 0.5F && grid.at(2, 1) == 0.6F, "logical row 2 is the newest live row");
                      checks.expect(!grid.at(3, 0).has_value(), "a row past rowCount reads as nothing");
                      checks.expect(!grid.at(0, 2).has_value(), "a bin past the declared width reads as nothing");
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Refusals
// ---------------------------------------------------------------------------

const mdux::spec::Register aWellFormedGridValidates{
    "A grid within every bound and a sane style validates",
    "evidence-unit",
    [] {
        return speclab::Test("viewport-well-formed-grid-validates")
            .Given("a 3-row, 4-bin grid, and a band with room for it", [] {})
            .When("it is validated", [] {})
            .Then("no refusal is raised",
                  [] {
                      static constexpr std::array<float, 12> storage{0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F,
                                                                     0.7F, 0.8F, 0.9F, 1.0F, 0.0F, 0.5F};
                      requireValid(ms::validate(band, gridOver(storage, 4), unitRamp), "the well-formed grid");
                  })
            .Execute();
    }};

const mdux::spec::Register refusesAnOversizedHistory{
    "A grid with more live rows than maxWaterfallRows admits is refused, not truncated",
    "evidence-unit",
    [] {
        return speclab::Test("viewport-refuses-oversized-history")
            .Given("a grid holding one row more than the cap", [] {})
            .When("it is validated", [] {})
            .Then("the refusal names the row cap",
                  [] {
                      static std::array<float, (ms::maxWaterfallRows + 1) * 2> storage{};
                      const ms::WaterfallError error = requireRefused(ms::validate(band, gridOver(storage, 2), unitRamp), "the oversized history");
                      mdux::spec::Checks       checks;
                      checks.expect(error == ms::WaterfallError::TooManyRows, "the refusal names the row cap");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register refusesTooManyBins{
    "A grid declaring more bins than maxWaterfallBins admits is refused",
    "evidence-unit",
    [] {
        return speclab::Test("viewport-refuses-too-many-bins")
            .Given("a grid declaring one bin more than the cap", [] {})
            .When("it is validated", [] {})
            .Then("the refusal names the bin cap",
                  [] {
                      static std::array<float, ms::maxWaterfallBins + 1> storage{};
                      const ms::WaterfallError error = requireRefused(ms::validate(band, gridOver(storage, ms::maxWaterfallBins + 1), unitRamp), "the oversized row width");
                      mdux::spec::Checks       checks;
                      checks.expect(error == ms::WaterfallError::TooManyBins, "the refusal names the bin cap");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register refusesMalformedInputs{
    "A malformed grid, sample, style or band is refused with its own cause",
    "evidence-unit",
    [] {
        return speclab::Test("viewport-refuses-malformed-inputs")
            .Given("a battery of malformed grids, styles and bands", [] {})
            .When("each is validated in turn", [] {})
            .Then("each names its own cause",
                  [] {
                      mdux::spec::Checks checks;

                      // A row/bin count of zero.
                      static constexpr std::array<float, 4> plain{0.1F, 0.2F, 0.3F, 0.4F};
                      checks.expect(requireRefused(ms::validate(band, ms::WaterfallGrid{.storage = plain, .bins = 0, .oldestRow = 0, .rowCount = 0}, unitRamp),
                                                    "a grid with no bins") == ms::WaterfallError::MalformedGrid,
                                    "a grid with no bins is MalformedGrid");

                      // A storage size that is not a whole number of rows.
                      checks.expect(requireRefused(ms::validate(band, ms::WaterfallGrid{.storage = plain, .bins = 3, .oldestRow = 0, .rowCount = 1}, unitRamp),
                                                    "a storage size not a multiple of bins") == ms::WaterfallError::MalformedGrid,
                                    "storage not a multiple of bins is MalformedGrid");

                      // A live count past the ring's own capacity.
                      checks.expect(requireRefused(ms::validate(band, ms::WaterfallGrid{.storage = plain, .bins = 2, .oldestRow = 0, .rowCount = 3}, unitRamp),
                                                    "a rowCount past capacity") == ms::WaterfallError::MalformedGrid,
                                    "a rowCount past the ring's capacity is MalformedGrid");

                      // An oldest-row index past the ring's own capacity.
                      checks.expect(requireRefused(ms::validate(band, ms::WaterfallGrid{.storage = plain, .bins = 2, .oldestRow = 2, .rowCount = 2}, unitRamp),
                                                    "an oldestRow past capacity") == ms::WaterfallError::MalformedGrid,
                                    "an oldestRow past the ring's capacity is MalformedGrid");

                      // A NaN sample.
                      static const std::array<float, 4> withNan{0.1F, std::numeric_limits<float>::quiet_NaN(), 0.3F, 0.4F};
                      checks.expect(requireRefused(ms::validate(band, gridOver(withNan, 2), unitRamp), "a NaN sample") == ms::WaterfallError::NonFiniteSample,
                                    "a NaN sample is NonFiniteSample");

                      // An empty style range.
                      checks.expect(requireRefused(ms::validate(band, gridOver(plain, 2), ms::WaterfallStyle{.minimum = 1.0F, .maximum = 1.0F}), "an empty range") ==
                                        ms::WaterfallError::MalformedStyle,
                                    "an empty range is MalformedStyle");

                      // An inverted style range.
                      checks.expect(requireRefused(ms::validate(band, gridOver(plain, 2), ms::WaterfallStyle{.minimum = 1.0F, .maximum = 0.0F}), "an inverted range") ==
                                        ms::WaterfallError::MalformedStyle,
                                    "an inverted range is MalformedStyle");

                      // A band too small to hold one pixel per row and per bin at the grid's live extent.
                      static constexpr std::array<float, 40> wideRow{};  // 20 bins, 2 rows
                      constexpr core::Rect                   tinyBand{.x = 0, .y = 0, .width = 5, .height = 1};
                      checks.expect(requireRefused(ms::validate(tinyBand, gridOver(wideRow, 20), unitRamp), "a band narrower than its bins") ==
                                        ms::WaterfallError::BandTooSmall,
                                    "a band with fewer pixels than bins is BandTooSmall");

                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register anEmptyGridNeedsNoBand{
    "An empty grid - a ring that has not filled yet - validates against any band, including a tiny one",
    "evidence-unit",
    [] {
        return speclab::Test("viewport-empty-grid-needs-no-band")
            .Given("a grid with zero live rows, and a band too small for even one row", [] {})
            .When("it is validated", [] {})
            .Then("no refusal is raised",
                  [] {
                      static constexpr std::array<float, 8> storage{};
                      constexpr core::Rect                  tinyBand{.x = 0, .y = 0, .width = 1, .height = 1};
                      requireValid(ms::validate(tinyBand, ms::WaterfallGrid{.storage = storage, .bins = 8, .oldestRow = 0, .rowCount = 0}, unitRamp),
                                   "the empty grid");
                  })
            .Execute();
    }};

const mdux::spec::Register everyWaterfallErrorDescribesItself{
    "Every WaterfallError has its own non-empty description",
    "evidence-unit",
    [] {
        return speclab::Test("viewport-error-descriptions")
            .Given("every WaterfallError enumerator", [] {})
            .When("each is described", [] {})
            .Then("each has a unique, non-empty description",
                  [] {
                      constexpr std::array<ms::WaterfallError, 6> all{ms::WaterfallError::MalformedGrid,
                                                                      ms::WaterfallError::TooManyRows,
                                                                      ms::WaterfallError::TooManyBins,
                                                                      ms::WaterfallError::NonFiniteSample,
                                                                      ms::WaterfallError::MalformedStyle,
                                                                      ms::WaterfallError::BandTooSmall};
                      std::vector<std::string_view>                seen;
                      mdux::spec::Checks                           checks;
                      for (const ms::WaterfallError error : all) {
                          const std::string_view text = ms::describe(error);
                          checks.expect(!text.empty(), "a description exists");
                          checks.expect(std::ranges::find(seen, text) == seen.end(), "the description is unique");
                          seen.push_back(text);
                      }
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// The cost model and its bounds
// ---------------------------------------------------------------------------

const mdux::spec::Register costModelMatchesTheCapAndFitsHalfTheScreenBudget{
    "quadsForWaterfall() is exact, and the type-level cap costs exactly half the endoscope-monitor budget",
    "evidence-unit",
    [] {
        return speclab::Test("viewport-cost-model-fits-half-the-budget")
            .Given("the worst-case grid this module admits", [] {})
            .When("its cost is computed", [] {})
            .Then("the model is exact, and a full-cap grid is exactly half of the committed screen's 4096/6144 budget",
                  [] {
                      mdux::spec::Checks checks;
                      const std::size_t  quads = ms::quadsForWaterfall(3, 4);
                      checks.expect(quads == 12, std::format("a 3x4 grid is 12 quads, got {}", quads));

                      const std::size_t worstQuads = ms::quadsForWaterfall(ms::maxWaterfallRows, ms::maxWaterfallBins);
                      checks.expect(4 * worstQuads == 2048, std::format("the full-cap grid is 2048 vertices, got {}", 4 * worstQuads));
                      checks.expect(6 * worstQuads == 3072, std::format("the full-cap grid is 3072 indices, got {}", 6 * worstQuads));
                      // The committed endoscope-monitor screen's whole-frame budget - see the module
                      // comment for why this margin is a deliberate, checked property rather than luck.
                      checks.expect(2 * (4 * worstQuads) == 4096, "the full-cap grid is exactly half the screen's vertex budget");
                      checks.expect(2 * (6 * worstQuads) == 6144, "the full-cap grid is exactly half the screen's index budget");
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
