/**
 * @file TraceTests.cpp
 * @brief BDD scenarios for `mdux.medui.trace` and the screen runtime's signal binding (issue #257).
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: this suite links MduX::Core only)
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * Two halves, and they are tested apart for the reason the module is split apart. The geometry half
 * reads vertices back and checks numbers - where a sample landed, whether the stroke stayed inside
 * its band, what a second identical expansion produced. The binding half checks the join: which
 * refusals `SignalBinding::create()` makes once, and what `render()` composes for a trace that has
 * samples versus one that does not.
 *
 * The scenario this issue is judged on is `trace-refuses-an-oversized-ring`: a trace whose sample
 * count exceeds the cap must be **refused**, not truncated. A truncating implementation passes every
 * other scenario in this file, draws a plausible waveform, and is wrong in the one way a monitor
 * cannot show.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.core.units;
import mdux.evidence.digest;
import mdux.evidence.report;
import mdux.draw;
import mdux.medui.schema;
import mdux.medui.screen;
import mdux.medui.trace;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace ms   = mdux::medui;
namespace core = mdux::core;
namespace draw = mdux::draw;

/// Storage a caller sizes once, exactly as a device would - and never grows.
///
/// Generous enough for a full-cap trace (511 quads is 2044 vertices) so that a scenario about
/// geometry fails on geometry rather than on a budget it did not mean to test. The budget-refusal
/// scenarios build their own deliberately small list.
struct Scratch {
    std::array<draw::UiVertex, 2560>  vertices{};
    std::array<draw::Index, 3840>     indices{};
    std::array<draw::DrawCommand, 32> commands{};

    [[nodiscard]] static constexpr draw::DrawBudget budget() noexcept {
        return draw::DrawBudget{.maxVertices = 2560, .maxIndices = 3840, .maxCommands = 32};
    }

    [[nodiscard]] draw::DrawList list() {
        auto created = draw::DrawList::create(vertices, indices, commands, budget());
        if (!created.has_value()) {
            throw speclab::core::AssertionFailure("the scratch does not satisfy its own budget", std::source_location::current());
        }
        return std::move(*created);
    }
};

constexpr core::Rect       band{.x = 10, .y = 20, .width = 100, .height = 50};
constexpr core::ColorRgba8 nominal{.r = 33, .g = 184, .b = 107, .a = 255};
constexpr ms::TraceStyle   unitStyle{.minimum = 0.0F, .maximum = 1.0F, .strokeWidth = 1};

/// A ring over `storage`, unwrapped: the caller has not filled it yet, so the oldest sample is at 0.
[[nodiscard]] ms::SampleRing ringOver(std::span<const float> storage) noexcept {
    return ms::SampleRing{.storage = storage, .oldest = 0, .count = storage.size()};
}

/// The box every recorded vertex lies in. Floats, because a stroke's corners are not whole pixels.
struct VertexBox {
    float left{0.0F};
    float top{0.0F};
    float right{0.0F};
    float bottom{0.0F};
};

[[nodiscard]] std::optional<VertexBox> boxOf(std::span<const draw::UiVertex> vertices) noexcept {
    if (vertices.empty()) {
        return std::nullopt;
    }
    VertexBox box{.left = vertices[0].x, .top = vertices[0].y, .right = vertices[0].x, .bottom = vertices[0].y};
    for (const draw::UiVertex& vertex : vertices) {
        box.left   = std::min(box.left, vertex.x);
        box.top    = std::min(box.top, vertex.y);
        box.right  = std::max(box.right, vertex.x);
        box.bottom = std::max(box.bottom, vertex.y);
    }
    return box;
}

/// Hard failure (REQUIRE-equivalent): an expansion that was expected to succeed must have.
void requireRecorded(core::ResultVoid<ms::TraceError> result, std::string_view what, std::source_location where = std::source_location::current()) {
    if (!result.has_value()) {
        throw speclab::core::AssertionFailure(std::format("{}: {}", what, ms::describe(result.error())), where);
    }
}

/// Hard failure (REQUIRE-equivalent): an expansion that was expected to be refused must be refused.
[[nodiscard]] ms::TraceError
requireRefused(core::ResultVoid<ms::TraceError> result, std::string_view what, std::source_location where = std::source_location::current()) {
    if (result.has_value()) {
        throw speclab::core::AssertionFailure(std::format("{}: expected a refusal but the trace was recorded", what), where);
    }
    return result.error();
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

const mdux::spec::Register samplesSpanTheBand{
    "A trace's first and last samples reach the band's edges, and its extremes reach its rows",
    "evidence-unit",
    [] {
        struct State {
            Scratch                       scratch;
            std::optional<draw::DrawList> list;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("trace-spans-the-band")
            .Given("five samples alternating between the range's ends",
                   [state] {
                       static constexpr std::array<float, 5> samples{0.0F, 1.0F, 0.0F, 1.0F, 0.0F};
                       state->list = state->scratch.list();
                       requireRecorded(ms::recordTrace(*state->list, band, ringOver(samples), unitStyle, nominal), "the trace");
                   })
            .When("the recorded vertices are read back", [] {})
            .Then("they fill the band exactly and leave none of it",
                  [state] {
                      // Both directions in one scenario, deliberately. "Inside the band" alone is
                      // satisfied by a trace drawn in one corner, and "reaches the edges" alone is
                      // satisfied by one that spills - and the pair is what the inset arithmetic and
                      // the pixel-centre convention exist to make simultaneously true.
                      const std::optional<VertexBox> box = boxOf(state->list->vertices());
                      mdux::spec::Checks             checks;
                      if (!box.has_value()) {
                          checks.expect(false, "the trace recorded something");
                          checks.raise();
                          return;
                      }
                      checks.expect(box->left == static_cast<float>(band.x), std::format("the left edge is {}, expected {}", box->left, band.x));
                      checks.expect(box->right == static_cast<float>(band.right()), std::format("the right edge is {}, expected {}", box->right, band.right()));
                      checks.expect(box->top == static_cast<float>(band.y), std::format("the top edge is {}, expected {}", box->top, band.y));
                      checks.expect(box->bottom == static_cast<float>(band.bottom()),
                                    std::format("the bottom edge is {}, expected {}", box->bottom, band.bottom()));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register valueIsUp{"The larger sample is the higher one on screen", "evidence-unit", [] {
                                         struct State {
                                             Scratch                       scratch;
                                             std::optional<draw::DrawList> list;
                                         };
                                         auto state = std::make_shared<State>();

                                         return speclab::Test("trace-value-is-up")
                                             .Given("two samples, the second larger than the first",
                                                    [state] {
                                                        static constexpr std::array<float, 2> samples{0.0F, 1.0F};
                                                        state->list = state->scratch.list();
                                                        requireRecorded(ms::recordTrace(*state->list, band, ringOver(samples), unitStyle, nominal),
                                                                        "the trace");
                                                    })
                                             .When("the two caps are compared", [] {})
                                             .Then("the larger sample sits at the smaller y",
                                                   [state] {
                                                       // The convention every physiological trace is read with, and the one an inverted
                                                       // y axis makes easy to get backwards. Record order is cap, then cap and the
                                                       // segment behind it - so the two caps are the first and second quads, four
                                                       // vertices apart, and each quad's first vertex is its top-left corner.
                                                       const std::span<const draw::UiVertex> vertices = state->list->vertices();
                                                       mdux::spec::Checks                    checks;
                                                       checks.expect(vertices.size() == 12,
                                                                     std::format("two caps and one segment is twelve vertices, got {}", vertices.size()));
                                                       if (vertices.size() != 12) {
                                                           checks.raise();
                                                           return;
                                                       }
                                                       checks.expect(vertices[4].y < vertices[0].y,
                                                                     std::format("the larger sample is higher: y {} against {}", vertices[4].y, vertices[0].y));
                                                       checks.raise();
                                                   })
                                             .Execute();
                                     }};

const mdux::spec::Register anExtremeRangeStillSeparatesItsRails{
    "A full-scale range maps its endpoints to opposite rails",
    "evidence-unit",
    [] {
        struct State {
            Scratch                       scratch;
            std::optional<draw::DrawList> list;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("trace-extreme-range-separates-rails")
            .Given("a range spanning the whole float line, and a sample at each end",
                   [state] {
                       // Every value here is finite, so `create()`'s checks pass - and the span
                       // between them is not: `maximum - minimum` overflows in float, and so does
                       // `sample - minimum` at the top. The float path divided one infinity by
                       // another and sent the resulting NaN to the bottom rail, so a transition from
                       // the smallest sample to the largest drew a flat line rather than a step.
                       // The one failure a monitor must not have, from a range that is merely wide.
                       static constexpr float                extreme = std::numeric_limits<float>::max();
                       static constexpr std::array<float, 2> samples{-extreme, extreme};
                       static constexpr ms::TraceStyle       wide{.minimum = -extreme, .maximum = extreme, .strokeWidth = 1};
                       state->list = state->scratch.list();
                       requireRecorded(ms::recordTrace(*state->list, band, ringOver(samples), wide, nominal), "the full-scale trace");
                   })
            .When("the two caps are compared", [] {})
            .Then("they sit on opposite rails, the larger sample higher",
                  [state] {
                      // Record order and vertex layout as in `trace-value-is-up`: cap, then cap and
                      // the segment behind it, each quad's first vertex its top-left corner.
                      const std::span<const draw::UiVertex> vertices = state->list->vertices();
                      mdux::spec::Checks                    checks;
                      checks.expect(vertices.size() == 12, std::format("two caps and one segment is twelve vertices, got {}", vertices.size()));
                      if (vertices.size() != 12) {
                          checks.raise();
                          return;
                      }
                      checks.expect(vertices[4].y < vertices[0].y,
                                    std::format("the largest sample is higher than the smallest: y {} against {}", vertices[4].y, vertices[0].y));
                      checks.expect(vertices[0].y == static_cast<float>(band.y + band.height - 1),
                                    std::format("the smallest sample is on the bottom rail, got y {}", vertices[0].y));
                      checks.expect(vertices[4].y == static_cast<float>(band.y), std::format("the largest sample is on the top rail, got y {}", vertices[4].y));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register everyStrokeWidthStaysInside{
    "Every admitted stroke width stays inside the node",
    "evidence-unit",
    [] {
        struct State {
            Scratch                                 scratch;
            std::array<std::optional<VertexBox>, 3> boxes{};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("trace-stroke-widths-stay-inside")
            .Given("a sawtooth expanded at one, two and three pixels",
                   [state] {
                       static constexpr std::array<float, 7> samples{0.0F, 1.0F, 0.25F, 0.75F, 0.0F, 0.5F, 1.0F};
                       for (core::Px width = 1; width <= ms::maxStrokeWidth; ++width) {
                           draw::DrawList       list = state->scratch.list();
                           const ms::TraceStyle style{.minimum = 0.0F, .maximum = 1.0F, .strokeWidth = width};
                           requireRecorded(ms::recordTrace(list, band, ringOver(samples), style, nominal), std::format("the {}px trace", width));
                           state->boxes[static_cast<std::size_t>(width - 1)] = boxOf(list.vertices());
                       }
                   })
            .When("each expansion's extent is compared against the node", [] {})
            .Then(
                "none of them leaves it",
                [state] {
                    // A stroke is drawn half its width either side of the path, so an inset that
                    // was merely "about right" would spill by half a pixel at exactly one of the
                    // three widths - which is the kind of defect a single-width test misses and a
                    // neighbouring node discovers.
                    mdux::spec::Checks checks;
                    for (std::size_t index = 0; index < state->boxes.size(); ++index) {
                        const std::optional<VertexBox>& box = state->boxes[index];
                        if (!box.has_value()) {
                            checks.expect(false, std::format("the {}px trace recorded something", index + 1));
                            continue;
                        }
                        checks.expect(box->left >= static_cast<float>(band.x) && box->right <= static_cast<float>(band.right()),
                                      std::format("the {}px trace stays within x {}..{}, got {}..{}", index + 1, band.x, band.right(), box->left, box->right));
                        checks.expect(box->top >= static_cast<float>(band.y) && box->bottom <= static_cast<float>(band.bottom()),
                                      std::format("the {}px trace stays within y {}..{}, got {}..{}", index + 1, band.y, band.bottom(), box->top, box->bottom));
                    }
                    checks.raise();
                })
            .Execute();
    }};

const mdux::spec::Register excursionsAreClamped{"A sample outside the display range is pinned to the rail, not refused", "evidence-unit", [] {
                                                    struct State {
                                                        Scratch                       scratch;
                                                        std::optional<draw::DrawList> list;
                                                        std::optional<VertexBox>      box;
                                                    };
                                                    auto state = std::make_shared<State>();

                                                    return speclab::Test("trace-clamps-excursions")
                                                        .Given("samples far outside the declared range",
                                                               [state] {
                                                                   static constexpr std::array<float, 3> samples{-40.0F, 0.5F, 90.0F};
                                                                   state->list = state->scratch.list();
                                                                   requireRecorded(ms::recordTrace(*state->list, band, ringOver(samples), unitStyle, nominal),
                                                                                   "the trace");
                                                                   state->box = boxOf(state->list->vertices());
                                                               })
                                                        .When("the recorded extent is measured", [] {})
                                                        .Then("the trace is drawn against the band's rails and stays inside it",
                                                              [state] {
                                                                  // An excursion past the display range is a real reading a monitor shows pinned
                                                                  // to its rail. Refusing the frame would blank the display at the moment the
                                                                  // waveform became most interesting, which is the opposite of fail-safe here.
                                                                  mdux::spec::Checks checks;
                                                                  if (!state->box.has_value()) {
                                                                      checks.expect(false, "the trace recorded something");
                                                                      checks.raise();
                                                                      return;
                                                                  }
                                                                  checks.expect(state->box->top == static_cast<float>(band.y),
                                                                                "the high excursion is pinned to the top rail");
                                                                  checks.expect(state->box->bottom == static_cast<float>(band.bottom()),
                                                                                "the low excursion is pinned to the bottom rail");
                                                                  checks.raise();
                                                              })
                                                        .Execute();
                                                }};

const mdux::spec::Register aShortRingDrawsNothing{
    "A ring with fewer than two samples draws nothing and is not an error",
    "evidence-unit",
    [] {
        struct State {
            Scratch                       scratch;
            std::optional<draw::DrawList> list;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("trace-short-ring-draws-nothing")
            .Given("an empty ring and a one-sample ring",
                   [state] {
                       static constexpr std::array<float, 4> storage{0.5F, 0.5F, 0.5F, 0.5F};
                       state->list = state->scratch.list();
                       requireRecorded(ms::recordTrace(*state->list, band, ms::SampleRing{.storage = storage, .oldest = 0, .count = 0}, unitStyle, nominal),
                                       "the empty ring");
                       requireRecorded(ms::recordTrace(*state->list, band, ms::SampleRing{.storage = storage, .oldest = 2, .count = 1}, unitStyle, nominal),
                                       "the one-sample ring");
                   })
            .When("the list is inspected", [] {})
            .Then("nothing was recorded and neither call failed",
                  [state] {
                      // A ring fills over the first frames of a device's life. Refusing then would
                      // make a monitor's start-up an error state, and drawing a single point would
                      // put a dot on screen that reads as a signal.
                      mdux::spec::Checks checks;
                      checks.expect(state->list->vertices().empty(), "no vertex was recorded");
                      checks.expect(ms::quadsForSamples(0) == 0 && ms::quadsForSamples(1) == 0, "the cost model agrees that both are free");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aWrappedRingReadsOldestFirst{"A wrapped ring is read oldest-first from its own oldest index", "evidence-unit", [] {
                                                            struct State {
                                                                std::array<std::optional<float>, 4> read{};
                                                            };
                                                            auto state = std::make_shared<State>();

                                                            return speclab::Test("trace-wrapped-ring-order")
                                                                .Given("a ring whose producer has wrapped past the end of its storage",
                                                                       [state] {
                                                                           static constexpr std::array<float, 4> storage{0.3F, 0.4F, 0.1F, 0.2F};
                                                                           const ms::SampleRing ring{.storage = storage, .oldest = 2, .count = 4};
                                                                           for (std::size_t index = 0; index < 4; ++index) {
                                                                               state->read[index] = ring.at(index);
                                                                           }
                                                                       })
                                                                .When("its samples are read by position", [] {})
                                                                .Then("they come back in the order the producer wrote them",
                                                                      [state] {
                                                                          // The property the whole binding rests on: the caller never reorders anything
                                                                          // for this module, so a ring read from the wrong end would draw a waveform
                                                                          // made of the right samples in the wrong order - which looks like a signal.
                                                                          constexpr std::array<float, 4> expected{0.1F, 0.2F, 0.3F, 0.4F};
                                                                          mdux::spec::Checks             checks;
                                                                          for (std::size_t index = 0; index < expected.size(); ++index) {
                                                                              checks.expect(state->read[index] == expected[index],
                                                                                            std::format("sample {} is {}", index, expected[index]));
                                                                          }
                                                                          checks.raise();
                                                                      })
                                                                .Execute();
                                                        }};

const mdux::spec::Register expansionIsDeterministic{
    "The same samples expand to byte-identical buffers",
    "determinism",
    [] {
        struct State {
            Scratch                       first;
            Scratch                       second;
            std::optional<draw::DrawList> a;
            std::optional<draw::DrawList> b;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("trace-expansion-is-deterministic")
            .Given("one sample set expanded twice into separate storage",
                   [state] {
                       static constexpr std::array<float, 9> samples{0.0F, 0.9F, 0.15F, 0.62F, 0.33F, 1.0F, 0.05F, 0.48F, 0.71F};
                       state->a = state->first.list();
                       state->b = state->second.list();
                       requireRecorded(ms::recordTrace(*state->a, band, ringOver(samples), unitStyle, nominal), "the first expansion");
                       requireRecorded(ms::recordTrace(*state->b, band, ringOver(samples), unitStyle, nominal), "the second expansion");
                   })
            .When("the two buffers are compared", [] {})
            .Then("they are identical",
                  [state] {
                      // A frame is byte-compared across toolchains, and this module is the first in
                      // the tree whose geometry passes through a square root. `std::sqrt` is
                      // correctly rounded by IEEE 754, which is what makes that safe; this is the
                      // scenario that would notice if something less well-behaved crept in beside it.
                      mdux::spec::Checks checks;
                      checks.expect(std::ranges::equal(state->a->vertices(), state->b->vertices()), "the vertex buffers match");
                      checks.expect(std::ranges::equal(state->a->indices(), state->b->indices()), "the index buffers match");
                      checks.expect(std::ranges::equal(state->a->commands(), state->b->commands()), "the command buffers match");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register costModelBoundsTheExpansion{
    "quadsForSamples() bounds what an expansion actually records",
    "evidence-unit",
    [] {
        struct State {
            Scratch     scratch;
            std::size_t recorded{0};
            std::size_t predicted{0};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("trace-cost-model-bounds-expansion")
            .Given("a full-cap ring of distinct samples",
                   [state] {
                       static std::array<float, ms::maxSamplesPerTrace> samples{};
                       for (std::size_t index = 0; index < samples.size(); ++index) {
                           samples[index] = static_cast<float>(index % 17) / 16.0F;
                       }
                       draw::DrawList list = state->scratch.list();
                       requireRecorded(ms::recordTrace(list, band, ringOver(samples), unitStyle, nominal), "the full-cap trace");
                       state->recorded  = list.vertices().size() / 4;
                       state->predicted = ms::quadsForSamples(samples.size());
                   })
            .When("the quads recorded are compared against the cost model", [] {})
            .Then("the model is an upper bound, and a budget sized from it holds",
                  [state] {
                      // The number a reviewer multiplies against a DrawBudget. It is a bound rather
                      // than an equality because a segment between two samples that land on the same
                      // column is skipped - which is what a 256-sample ring in a 100px band does a
                      // great deal of, and exactly why the bound is the number that belongs in a
                      // budget.
                      mdux::spec::Checks checks;
                      checks.expect(state->recorded <= state->predicted,
                                    std::format("{} quads recorded, at most {} predicted", state->recorded, state->predicted));
                      checks.expect(state->predicted == (2 * ms::maxSamplesPerTrace) - 1, "the model is one cap per sample and one quad per segment");
                      checks.expect(4 * state->predicted <= 2560, "a full-cap trace fits the storage this suite sizes once");
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Refusals
// ---------------------------------------------------------------------------

const mdux::spec::Register refusesAnOversizedRing{"A ring past the cap is refused, not truncated", "evidence-unit", [] {
                                                      struct State {
                                                          Scratch                       scratch;
                                                          std::optional<draw::DrawList> list;
                                                          std::optional<ms::TraceError> error;
                                                      };
                                                      auto state = std::make_shared<State>();

                                                      return speclab::Test("trace-refuses-an-oversized-ring")
                                                          .Given("a ring holding one sample more than the cap admits",
                                                                 [state] {
                                                                     static std::array<float, ms::maxSamplesPerTrace + 1> samples{};
                                                                     samples.fill(0.5F);
                                                                     state->list  = state->scratch.list();
                                                                     state->error = requireRefused(
                                                                         ms::recordTrace(*state->list, band, ringOver(samples), unitStyle, nominal),
                                                                         "the oversized ring");
                                                                 })
                                                          .When("the list is inspected", [] {})
                                                          .Then("the expansion is TooManySamples and nothing was drawn",
                                                                [state] {
                                                                    // This issue's acceptance criterion. A truncating implementation would draw a
                                                                    // perfectly plausible waveform of the newest 256 samples, in the tint that says
                                                                    // it is the window the caller asked for - and there is nothing on a monitor
                                                                    // that distinguishes that from a correct reading of different data.
                                                                    mdux::spec::Checks checks;
                                                                    checks.expect(state->error == ms::TraceError::TooManySamples, "the refusal names the cap");
                                                                    checks.expect(state->list->vertices().empty(), "no vertex was recorded");
                                                                    checks.expect(state->list->commands().empty(), "no command was started");
                                                                    checks.raise();
                                                                })
                                                          .Execute();
                                                  }};

const mdux::spec::Register refusesMalformedInputs{
    "A malformed ring, sample, style or band is refused",
    "evidence-unit",
    [] {
        struct State {
            Scratch                                      scratch;
            std::optional<draw::DrawList>                list;
            std::array<std::optional<ms::TraceError>, 6> errors{};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("trace-refuses-malformed-inputs")
            .Given("a valid ring and a list",
                   [state] {
                       state->list = state->scratch.list();
                   })
            .When("each malformed input is offered in turn",
                  [state] {
                      static constexpr std::array<float, 4> storage{0.1F, 0.2F, 0.3F, 0.4F};
                      static const std::array<float, 3>     withNan{0.1F, std::numeric_limits<float>::quiet_NaN(), 0.3F};

                      state->errors[0] = requireRefused(
                          ms::recordTrace(*state->list, band, ms::SampleRing{.storage = storage, .oldest = 0, .count = 9}, unitStyle, nominal),
                          "a live count past the storage");
                      state->errors[1] = requireRefused(
                          ms::recordTrace(*state->list, band, ms::SampleRing{.storage = storage, .oldest = 4, .count = 4}, unitStyle, nominal),
                          "an oldest index past the storage");
                      state->errors[2] = requireRefused(ms::recordTrace(*state->list, band, ringOver(withNan), unitStyle, nominal), "a NaN sample");
                      state->errors[3] = requireRefused(
                          ms::recordTrace(*state->list, band, ringOver(storage), ms::TraceStyle{.minimum = 1.0F, .maximum = 1.0F, .strokeWidth = 1}, nominal),
                          "an empty range");
                      state->errors[4] = requireRefused(ms::recordTrace(*state->list,
                                                                        band,
                                                                        ringOver(storage),
                                                                        ms::TraceStyle{.minimum = 0.0F, .maximum = 1.0F, .strokeWidth = ms::maxStrokeWidth + 1},
                                                                        nominal),
                                                        "a stroke wider than the cap approximation admits");
                      state->errors[5] = requireRefused(ms::recordTrace(*state->list,
                                                                        core::Rect{.x = 0, .y = 0, .width = 2, .height = 2},
                                                                        ringOver(storage),
                                                                        ms::TraceStyle{.minimum = 0.0F, .maximum = 1.0F, .strokeWidth = 3},
                                                                        nominal),
                                                        "a node too small for its stroke");
                  })
            .Then("each names its own cause and the list is untouched",
                  [state] {
                      // Distinct causes rather than one "the trace was refused": a malformed ring is
                      // the producer's defect, a NaN sample is a driver fault, an out-of-range stroke
                      // is the integrator's - and one shared error would send two of the three to
                      // the wrong person.
                      constexpr std::array<ms::TraceError, 6> expected{ms::TraceError::MalformedRing,
                                                                       ms::TraceError::MalformedRing,
                                                                       ms::TraceError::NonFiniteSample,
                                                                       ms::TraceError::MalformedStyle,
                                                                       ms::TraceError::MalformedStyle,
                                                                       ms::TraceError::BandTooSmall};
                      mdux::spec::Checks                      checks;
                      for (std::size_t index = 0; index < expected.size(); ++index) {
                          checks.expect(state->errors[index] == expected[index], std::format("refusal {} is {}", index, ms::describe(expected[index])));
                      }
                      checks.expect(state->list->vertices().empty(), "no refusal left a vertex behind");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aBudgetRefusalRollsBack{
    "An expansion that does not fit its budget leaves no partial waveform",
    "evidence-unit",
    [] {
        struct State {
            std::array<draw::UiVertex, 32>   vertices{};
            std::array<draw::Index, 48>      indices{};
            std::array<draw::DrawCommand, 4> commands{};
            std::optional<draw::DrawList>    list;
            std::optional<ms::TraceError>    error;
            std::size_t                      keptVertices{0};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("trace-budget-refusal-rolls-back")
            .Given("a list with room for eight quads and one rectangle already in it",
                   [state] {
                       auto created = draw::DrawList::create(state->vertices,
                                                             state->indices,
                                                             state->commands,
                                                             draw::DrawBudget{.maxVertices = 32, .maxIndices = 48, .maxCommands = 4});
                       if (!created.has_value()) {
                           throw speclab::core::AssertionFailure("the small list was not created", std::source_location::current());
                       }
                       state->list = std::move(*created);
                       if (!state->list->addSolidRect(band, nominal).has_value()) {
                           throw speclab::core::AssertionFailure("the prior rectangle was refused", std::source_location::current());
                       }
                   })
            .When("a trace far larger than the remaining budget is expanded",
                  [state] {
                      static constexpr std::array<float, 40> samples{};
                      state->error        = requireRefused(ms::recordTrace(*state->list, band, ringOver(samples), unitStyle, nominal), "the oversized trace");
                      state->keptVertices = state->list->vertices().size();
                  })
            .Then("the trace is rolled back and what was there before it survives",
                  [state] {
                      // All-or-nothing, and the "nothing" has to stop at the trace: a fragment of a
                      // waveform reads as a flat line, and a rollback that went too far would take
                      // the caller's own rectangle with it.
                      mdux::spec::Checks checks;
                      checks.expect(state->error == ms::TraceError::ListRejected, "the refusal names the list");
                      checks.expect(state->keptVertices == 4, std::format("only the prior rectangle survives, got {} vertices", state->keptVertices));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register everyTraceErrorDescribesItself{"Every TraceError has its own description", "evidence-unit", [] {
                                                              return speclab::Test("trace-error-descriptions")
                                                                  .Given("every TraceError enumerator", [] {})
                                                                  .When("each is described", [] {})
                                                                  .Then("each has a unique, non-empty description",
                                                                        [] {
                                                                            constexpr std::array<ms::TraceError, 6> all{ms::TraceError::MalformedRing,
                                                                                                                        ms::TraceError::TooManySamples,
                                                                                                                        ms::TraceError::NonFiniteSample,
                                                                                                                        ms::TraceError::MalformedStyle,
                                                                                                                        ms::TraceError::BandTooSmall,
                                                                                                                        ms::TraceError::ListRejected};
                                                                            std::vector<std::string_view>           seen;
                                                                            mdux::spec::Checks                      checks;
                                                                            for (const ms::TraceError error : all) {
                                                                                const std::string_view text = ms::describe(error);
                                                                                checks.expect(!text.empty(), "a description exists");
                                                                                checks.expect(std::ranges::find(seen, text) == seen.end(),
                                                                                              "the description is unique");
                                                                                seen.push_back(text);
                                                                            }
                                                                            checks.raise();
                                                                        })
                                                                  .Execute();
                                                          }};

// ---------------------------------------------------------------------------
// The binding, and what render() composes with it
// ---------------------------------------------------------------------------

constexpr ms::SignalTraceSpec ecg{.streamSource = "ECG_LEAD_II", .colorToken = "Theme.Colors.Nominal"};
constexpr ms::SignalTraceSpec pleth{.streamSource = "PLETH", .colorToken = "Theme.Colors.Alert"};

constexpr std::array<ms::CompiledNode, 2> traceNodes{
    ms::CompiledNode{  .id = "ecg",  .bounds = {0, 0, 200, 60},   .payload = ecg},
    ms::CompiledNode{.id = "pleth", .bounds = {0, 60, 200, 60}, .payload = pleth}
};

constexpr draw::DrawBudget traceBudget{.maxVertices = 2560, .maxIndices = 3840, .maxCommands = 32};

constexpr ms::ScreenPackage traceScreen{.id                   = "traces",
                                        .schemaVersion        = mdux::evidence::kSchemaVersion,
                                        .surfaceWidth         = 200,
                                        .surfaceHeight        = 120,
                                        .approvedTextPackages = {},
                                        .nodes                = traceNodes,
                                        .budget               = traceBudget};

static_assert(traceScreen.validate().has_value(), "the screen under test must be one a device could hold");

/// Hard failure (REQUIRE-equivalent): a binding that was expected to be made must exist.
[[nodiscard]] ms::SignalBinding
requireBound(core::Result<ms::SignalBinding, ms::ScreenError> result, std::string_view what, std::source_location where = std::source_location::current()) {
    if (!result.has_value()) {
        throw speclab::core::AssertionFailure(std::format("{}: {}", what, ms::describe(result.error())), where);
    }
    return *result;
}

/// Hard failure (REQUIRE-equivalent): a binding that was expected to be refused must be refused.
[[nodiscard]] ms::ScreenError
requireUnbound(core::Result<ms::SignalBinding, ms::ScreenError> result, std::string_view what, std::source_location where = std::source_location::current()) {
    if (result.has_value()) {
        throw speclab::core::AssertionFailure(std::format("{}: expected a refusal but a binding was made", what), where);
    }
    return result.error();
}

const mdux::spec::Register anUnboundTraceIsUnchanged{
    "A screen rendered without signals draws the fields it always drew",
    "evidence-unit",
    [] {
        struct State {
            Scratch                       scratch;
            std::optional<ms::FrameStats> stats;
            std::vector<draw::UiVertex>   vertices;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("screen-unbound-trace-unchanged")
            .Given("a screen of two traces and no signal binding",
                   [state] {
                       draw::DrawList list  = state->scratch.list();
                       const auto     frame = ms::render(traceScreen, list);
                       if (!frame.has_value()) {
                           throw speclab::core::AssertionFailure(std::format("the frame was refused: {}", ms::describe(frame.error())),
                                                                 std::source_location::current());
                       }
                       state->stats = *frame;
                       state->vertices.assign(list.vertices().begin(), list.vertices().end());
                   })
            .When("the frame is inspected", [] {})
            .Then("each trace is one opaque rectangle, exactly as it was before #257",
                  [state] {
                      // The unbound path is the one every existing caller takes - the committed
                      // screen's pixel test and its `verify` leg among them - so it is a tested
                      // contract rather than a code path nobody exercises now that a binding exists.
                      mdux::spec::Checks checks;
                      checks.expect(state->stats->rects == 2, std::format("two rectangles, got {}", state->stats->rects));
                      checks.expect(state->stats->traces == 0, "no trace was expanded");
                      checks.expect(state->stats->deferred == 0, "a reserved field is drawn, not deferred");
                      checks.expect(state->vertices.size() == 8, std::format("two rectangles is eight vertices, got {}", state->vertices.size()));
                      const bool opaque = std::ranges::all_of(state->vertices, [](const draw::UiVertex& vertex) {
                          const auto bytes = std::bit_cast<std::array<std::uint8_t, 4>>(vertex.color);
                          return bytes[3] == 255;
                      });
                      checks.expect(opaque, "an unbound field is opaque");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aBoundTraceDimsItsField{
    "A bound trace dims its field and strokes at full tint",
    "evidence-unit",
    [] {
        struct State {
            Scratch                       scratch;
            std::optional<ms::FrameStats> stats;
            std::vector<draw::UiVertex>   vertices;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("screen-bound-trace-dims-its-field")
            .Given("a screen whose first trace is bound to a ring",
                   [state] {
                       static constexpr std::array<float, 6>      samples{0.0F, 1.0F, 0.4F, 0.6F, 0.2F, 0.8F};
                       static const ms::SampleRing                ring = ringOver(samples);
                       static const std::array<ms::SignalSlot, 1> slots{
                           ms::SignalSlot{.streamSource = "ECG_LEAD_II", .ring = &ring, .style = unitStyle}
                       };
                       const ms::SignalBinding binding = requireBound(ms::SignalBinding::create(traceScreen, slots), "the binding");

                       draw::DrawList list  = state->scratch.list();
                       const auto     frame = ms::render(traceScreen, list, {}, {}, binding);
                       if (!frame.has_value()) {
                           throw speclab::core::AssertionFailure(std::format("the frame was refused: {}", ms::describe(frame.error())),
                                                                 std::source_location::current());
                       }
                       state->stats = *frame;
                       state->vertices.assign(list.vertices().begin(), list.vertices().end());
                   })
            .When("the alpha of every recorded vertex is read", [] {})
            .Then("exactly one primitive is dimmed and the rest carry the full tint",
                  [state] {
                      // One tint at two coverages, which is the composition Screen.cppm argues is the
                      // only one an additive draw list and a `ColorHash` golden both admit. The
                      // stroke has to reach full coverage somewhere or `colorHash()` would report
                      // TintAbsent for the node; the field has to differ from the ground or
                      // `goldenBounds()` would stop seeing the node's whole rectangle as painted.
                      const auto  expectedDim = static_cast<std::uint8_t>((255.0F * ms::boundTraceFieldCoverage) + 0.5F);
                      std::size_t dimmed      = 0;
                      std::size_t solid       = 0;
                      for (const draw::UiVertex& vertex : state->vertices) {
                          const auto bytes = std::bit_cast<std::array<std::uint8_t, 4>>(vertex.color);
                          if (bytes[3] == 255) {
                              ++solid;
                          } else if (bytes[3] == expectedDim) {
                              ++dimmed;
                          }
                      }
                      mdux::spec::Checks checks;
                      checks.expect(state->stats->traces == 1, std::format("one trace was expanded, got {}", state->stats->traces));
                      checks.expect(dimmed == 4, std::format("exactly one dimmed rectangle, got {} vertices at alpha {}", dimmed, expectedDim));
                      checks.expect(solid + dimmed == state->vertices.size(), "every vertex is either the dimmed field or the full tint");
                      checks.expect(solid >= 4 * ms::quadsForSamples(6) - 8, "the stroke's quads carry the full tint");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register anUnboundStreamStillReservesItsField{
    "A trace the caller has no samples for still draws its reserved field",
    "evidence-unit",
    [] {
        struct State {
            std::optional<ms::FrameStats> stats;
            Scratch                       scratch;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("screen-partially-bound-screen")
            .Given("a screen of two traces with only the first bound",
                   [state] {
                       static constexpr std::array<float, 4>      samples{0.0F, 1.0F, 0.5F, 0.25F};
                       static const ms::SampleRing                ring = ringOver(samples);
                       static const std::array<ms::SignalSlot, 1> slots{
                           ms::SignalSlot{.streamSource = "ECG_LEAD_II", .ring = &ring, .style = unitStyle}
                       };
                       const ms::SignalBinding binding = requireBound(ms::SignalBinding::create(traceScreen, slots), "the binding");

                       draw::DrawList list  = state->scratch.list();
                       const auto     frame = ms::render(traceScreen, list, {}, {}, binding);
                       if (!frame.has_value()) {
                           throw speclab::core::AssertionFailure(std::format("the frame was refused: {}", ms::describe(frame.error())),
                                                                 std::source_location::current());
                       }
                       state->stats = *frame;
                   })
            .When("the frame's statistics are read", [] {})
            .Then("one trace is expanded and the other is a reserved field, not a deferral",
                  [state] {
                      // A stream that has not started is a normal state, not a broken one. Binding
                      // every trace a screen carries is not a precondition of rendering it.
                      mdux::spec::Checks checks;
                      checks.expect(state->stats->traces == 1, "one trace was expanded");
                      checks.expect(state->stats->deferred == 0, "the unbound trace is drawn as a field, not deferred");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register bindingRefusesWhatItCanCheck{
    "A binding refuses an unknown stream, a duplicate, a missing ring and a bad style",
    "evidence-unit",
    [] {
        struct State {
            std::array<std::optional<ms::ScreenError>, 4> errors{};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("screen-signal-binding-refusals")
            .Given("a screen carrying two named streams", [] {})
            .When("each malformed slot set is offered",
                  [state] {
                      static constexpr std::array<float, 4> samples{0.0F, 1.0F, 0.5F, 0.25F};
                      static const ms::SampleRing           ring = ringOver(samples);

                      const std::array<ms::SignalSlot, 1> unknown{
                          ms::SignalSlot{.streamSource = "ECG_LEAD_III", .ring = &ring, .style = unitStyle}
                      };
                      state->errors[0] = requireUnbound(ms::SignalBinding::create(traceScreen, unknown), "an unknown stream");

                      const std::array<ms::SignalSlot, 2> duplicated{
                          ms::SignalSlot{.streamSource = "ECG_LEAD_II", .ring = &ring, .style = unitStyle},
                          ms::SignalSlot{.streamSource = "ECG_LEAD_II", .ring = &ring, .style = unitStyle}
                      };
                      state->errors[1] = requireUnbound(ms::SignalBinding::create(traceScreen, duplicated), "a duplicated stream");

                      const std::array<ms::SignalSlot, 1> ringless{
                          ms::SignalSlot{.streamSource = "ECG_LEAD_II", .ring = nullptr, .style = unitStyle}
                      };
                      state->errors[2] = requireUnbound(ms::SignalBinding::create(traceScreen, ringless), "a slot with no ring");

                      const std::array<ms::SignalSlot, 1> badStyle{
                          ms::SignalSlot{.streamSource = "PLETH", .ring = &ring, .style = ms::TraceStyle{.minimum = 5.0F, .maximum = 1.0F, .strokeWidth = 1}}
                      };
                      state->errors[3] = requireUnbound(ms::SignalBinding::create(traceScreen, badStyle), "an inverted range");
                  })
            .Then("each refusal names its own cause",
                  [state] {
                      // The unknown-stream check is the one that earns this type. A mistyped stream
                      // name would otherwise leave that trace drawing its reserved field forever,
                      // with nothing to distinguish it from a stream that has not started.
                      constexpr std::array<ms::ScreenError, 4> expected{ms::ScreenError::UnknownStreamSource,
                                                                        ms::ScreenError::DuplicateStream,
                                                                        ms::ScreenError::MissingSampleRing,
                                                                        ms::ScreenError::MalformedTraceStyle};
                      mdux::spec::Checks                       checks;
                      for (std::size_t index = 0; index < expected.size(); ++index) {
                          checks.expect(state->errors[index] == expected[index], std::format("refusal {} is {}", index, ms::describe(expected[index])));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aBindingIsNotPortableBetweenScreens{
    "A binding built for one screen is refused by another",
    "evidence-unit",
    [] {
        struct State {
            Scratch                        scratch;
            std::optional<ms::ScreenError> error;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("screen-signal-binding-is-not-portable")
            .Given("a binding validated against one screen",
                   [state] {
                       static constexpr std::array<float, 4>      samples{0.0F, 1.0F, 0.5F, 0.25F};
                       static const ms::SampleRing                ring = ringOver(samples);
                       static const std::array<ms::SignalSlot, 1> slots{
                           ms::SignalSlot{.streamSource = "ECG_LEAD_II", .ring = &ring, .style = unitStyle}
                       };
                       const ms::SignalBinding binding = requireBound(ms::SignalBinding::create(traceScreen, slots), "the binding");

                       // The same nodes under a different id: everything about this screen would let
                       // the binding work, and it is refused anyway, because what was validated was
                       // the pairing rather than the shape.
                       ms::ScreenPackage other = traceScreen;
                       other.id                = "other-traces";

                       draw::DrawList list  = state->scratch.list();
                       const auto     frame = ms::render(other, list, {}, {}, binding);
                       if (frame.has_value()) {
                           throw speclab::core::AssertionFailure("the foreign screen accepted the binding", std::source_location::current());
                       }
                       state->error = frame.error();
                   })
            .When("the refusal is read", [] {})
            .Then("it names the screen rather than the samples",
                  [state] {
                      // TextBinding closes this path with a digest. There is no digest to hold for a
                      // signal - samples are produced at run time and no artifact describes them -
                      // so the identity is the screen's id, and `approvedBy()` says as much.
                      mdux::spec::Checks checks;
                      checks.expect(state->error == ms::ScreenError::ScreenNotApproved, "the frame is refused as ScreenNotApproved");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register anOversizedRingRefusesTheWholeFrame{
    "A trace past the cap refuses the frame and leaves nothing behind",
    "evidence-unit",
    [] {
        struct State {
            Scratch                        scratch;
            std::optional<ms::ScreenError> error;
            std::size_t                    kept{0};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("screen-oversized-trace-refuses-the-frame")
            .Given("a screen whose bound ring holds more samples than the cap admits",
                   [state] {
                       static std::array<float, ms::maxSamplesPerTrace + 1> samples{};
                       samples.fill(0.5F);
                       static const ms::SampleRing                ring = ringOver(samples);
                       static const std::array<ms::SignalSlot, 1> slots{
                           ms::SignalSlot{.streamSource = "ECG_LEAD_II", .ring = &ring, .style = unitStyle}
                       };
                       const ms::SignalBinding binding = requireBound(ms::SignalBinding::create(traceScreen, slots), "the binding");

                       draw::DrawList list  = state->scratch.list();
                       const auto     frame = ms::render(traceScreen, list, {}, {}, binding);
                       if (frame.has_value()) {
                           throw speclab::core::AssertionFailure("the oversized trace was accepted", std::source_location::current());
                       }
                       state->error = frame.error();
                       state->kept  = list.vertices().size();
                   })
            .When("the list is inspected", [] {})
            .Then("the frame is TraceTooLong and whole rather than partial",
                  [state] {
                      // The cap refusal, seen from the caller rather than from the expansion - and
                      // the rollback that makes it a *frame* property. A partial frame on a medical
                      // display is the worst outcome available, because it looks like a reading.
                      mdux::spec::Checks checks;
                      checks.expect(state->error == ms::ScreenError::TraceTooLong, "the refusal names the cap");
                      checks.expect(state->kept == 0, std::format("the frame was rolled back whole, got {} vertices", state->kept));
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
