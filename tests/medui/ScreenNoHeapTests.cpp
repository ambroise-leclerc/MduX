/**
 * @file ScreenNoHeapTests.cpp
 * @brief Layer 1 of issue #199: render() makes no `operator new` call, proved by interposition.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no allocation)
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * "The draw path does not allocate" is easy to claim and easy to break silently - a `std::vector` in
 * a helper, a `std::string` on an error path, a `std::function` somebody found convenient. This
 * binary replaces the global `operator new` family with counting versions and measures instead.
 *
 * The interposition is shared with the ML suite; `tests/framework/CountingAllocations.hpp` says what
 * it catches and what it does not, and why the symbol scan is not redundant with it.
 */

import std;
import speclab;
import mdux.core.units;
import mdux.evidence.digest;
import mdux.evidence.report;
import mdux.draw;
import mdux.font.schema;
import mdux.medui.schema;
import mdux.medui.reading;
import mdux.medui.screen;
import mdux.medui.trace;
import mdux.text.schema;

#include "../framework/SpecLabBridge.hpp"

// The interposition itself. Included by exactly one translation unit in this binary.
#include "../framework/CountingAllocations.hpp"

namespace {

namespace ms = mdux::medui;

constexpr mdux::draw::DrawBudget budget{.maxVertices = 512, .maxIndices = 768, .maxCommands = 16};

constexpr ms::PanelSpec panel{.colorToken = "Theme.Colors.TopbarBackground"};
constexpr ms::LabelSpec label{.textKey = "STR-TITLE", .colorToken = "Theme.Colors.Title"};
constexpr ms::ClockSpec clock{.format = ms::ClockFormat::TimeSeconds};

// Every alternative the walk can take: one it draws, and two it defers. A screen of panels alone
// would leave the deferred branch - the one that touches a variant it does not draw - unmeasured.
constexpr std::array<ms::CompiledNode, 3> nodes{
    ms::CompiledNode{ .id = "back",  .bounds = {0, 0, 400, 40}, .payload = panel},
    ms::CompiledNode{.id = "title",  .bounds = {8, 8, 200, 24}, .payload = label},
    ms::CompiledNode{.id = "clock", .bounds = {8, 40, 120, 24}, .payload = clock}
};

constexpr std::array defaultApprovals{
    ms::TextPackageApproval{.locale = "en-US", .packageId = "noheap-text", .packageSha256 = {1}}
};

constexpr ms::ScreenPackage screen{.id                   = "noheap",
                                   .schemaVersion        = mdux::evidence::kSchemaVersion,
                                   .surfaceWidth         = 400,
                                   .surfaceHeight        = 300,
                                   .approvedTextPackages = defaultApprovals,
                                   .nodes                = nodes,
                                   .budget               = budget};

static_assert(screen.validate().has_value(), "the screen under measurement must be one a device could hold");

}  // namespace

const mdux::spec::Register theCounterMoves{"The allocation counter moves when something allocates", "noheap", [] {
                                               return speclab::Test("medui-screen-noheap-selftest")
                                                   .Given("the interposed operator new", [] {})
                                                   .When("a deliberate allocation is made", [] {})
                                                   .Then("the counter records it",
                                                         [] {
                                                             mdux::spec::Checks checks;

                                                             // Without this scenario the file would be a test that cannot fail: if the
                                                             // interposition ever stopped taking effect - a linker change, a static
                                                             // runtime, an inlined allocation - the counter would simply never move and
                                                             // every scenario below would pass.
                                                             const std::size_t before = allocations();
                                                             // volatile prevents Clang's release optimiser from proving the allocation
                                                             // and matching delete have no observable effect and removing both.
                                                             auto* volatile leaked   = new std::array<std::byte, 64>{};
                                                             const std::size_t after = allocations();
                                                             delete leaked;

                                                             checks.expect(after > before, std::format("the counter moved, {} then {}", before, after));
                                                             checks.raise();
                                                         })
                                                   .Execute();
                                           }};

const mdux::spec::Register renderingAllocatesNothing{
    "Rendering a frame allocates nothing",
    "noheap",
    [] {
        return speclab::Test("medui-screen-noheap-render")
            .Given("a screen and storage sized once from its budget", [] {})
            .When("frames are recorded", [] {})
            .Then("the allocation counter does not move",
                  [] {
                      mdux::spec::Checks checks;

                      // Storage first, and outside the measurement: sizing it is the caller's job
                      // and may allocate on a real device. What must not allocate is the frame.
                      static std::array<mdux::draw::UiVertex, 512>   vertices{};
                      static std::array<mdux::draw::Index, 768>      indices{};
                      static std::array<mdux::draw::DrawCommand, 16> commands{};

                      auto created = mdux::draw::DrawList::create(vertices, indices, commands, budget);
                      if (!created.has_value()) {
                          checks.expect(false, "the storage satisfies the budget");
                          checks.raise();
                          return;
                      }
                      mdux::draw::DrawList list = std::move(*created);

                      const std::size_t before = allocations();
                      for (int frame = 0; frame < 8; ++frame) {
                          list.reset();
                          const auto recorded = ms::render(screen, list);
                          checks.expect(recorded.has_value(), "each frame is recorded");
                      }
                      const std::size_t after = allocations();

                      // Eight frames rather than one, because a first frame could allocate once and
                      // hide it as setup. A per-frame allocation would show up as eight.
                      checks.expect(after == before, std::format("no allocation across eight frames, counter went {} to {}", before, after));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register refusingAFrameAllocatesNothing{
    "Refusing a frame allocates nothing either",
    "noheap",
    [] {
        return speclab::Test("medui-screen-noheap-refusal")
            .Given("a screen naming a colour the governed table does not define", [] {})
            .When("the frame is refused and rolled back", [] {})
            .Then("the error path allocates nothing",
                  [] {
                      mdux::spec::Checks checks;

                      // The path review misses. An error type carrying a message, or a diagnostic
                      // assembled on the way out, allocates exactly where nobody is looking.
                      static std::array<mdux::draw::UiVertex, 512>   vertices{};
                      static std::array<mdux::draw::Index, 768>      indices{};
                      static std::array<mdux::draw::DrawCommand, 16> commands{};

                      auto created = mdux::draw::DrawList::create(vertices, indices, commands, budget);
                      if (!created.has_value()) {
                          checks.expect(false, "the storage satisfies the budget");
                          checks.raise();
                          return;
                      }
                      mdux::draw::DrawList list = std::move(*created);

                      constexpr ms::PanelSpec                   unknown{.colorToken = "Theme.Colors.NotInTheTable"};
                      constexpr std::array<ms::CompiledNode, 1> refusedNodes{
                          ms::CompiledNode{.id = "wrong", .bounds = {0, 0, 400, 40}, .payload = unknown}
                      };
                      const ms::ScreenPackage refused{.id                   = "refused",
                                                      .schemaVersion        = mdux::evidence::kSchemaVersion,
                                                      .surfaceWidth         = 400,
                                                      .surfaceHeight        = 300,
                                                      .approvedTextPackages = {},
                                                      .nodes                = refusedNodes,
                                                      .budget               = budget};

                      const std::size_t before = allocations();
                      const auto        frame  = ms::render(refused, list);
                      const std::size_t after  = allocations();

                      checks.expect(!frame.has_value(), "the frame is refused");
                      if (!frame.has_value()) {
                          // describe() is on the path a caller takes after a refusal, so it is
                          // measured too: a string_view over a literal allocates nothing.
                          checks.expect(!ms::describe(frame.error()).empty(), "the error describes itself");
                      }
                      checks.expect(after == before, std::format("the error path allocates nothing, counter went {} to {}", before, after));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register drawingTextAllocatesNothing{
    "Rendering a frame that draws text allocates nothing",
    "noheap",
    [] {
        return speclab::Test("medui-screen-noheap-render-text")
            .Given("a screen whose label is joined to a font and text package", [] {})
            .When("frames are recorded", [] {})
            .Then(
                "the allocation counter does not move",
                [] {
                    mdux::spec::Checks checks;

                    // The packages are built before the measurement, and they *do* allocate - a
                    // FontPackage owns vectors. That is the caller's cost, paid once on a device
                    // at start-up or not at all if the packages are `constexpr`. What is measured
                    // is the join: `render()` reading them per frame.
                    static const mdux::font::FontPackage font = [] {
                        mdux::font::FontPackage built;
                        built.id                     = "noheap-ui";
                        built.unitsPerEm             = 1000;
                        built.pixelSize              = 10;
                        built.locales                = {"en-US"};
                        built.atlas.path             = "atlas.bin";
                        built.atlas.width            = 8;
                        built.atlas.height           = 8;
                        built.atlas.byteLength       = 64;
                        built.atlas.sha256           = std::string(64, 'a');
                        built.atlas.occupancyPercent = 25;
                        built.glyphs                 = {
                            {.codePoint       = U'A',
                             .glyphIndex      = 4,
                             .advanceWidth    = 700,
                             .leftSideBearing = 0,
                             .x               = 0,
                             .y               = 0,
                             .width           = 4,
                             .height          = 6,
                             .bitmapOriginX   = 0,
                             .bitmapOriginY   = 6}
                        };
                        built.restrictedCharset = {
                            {.first = U'A', .last = U'A'}
                        };
                        return built;
                    }();

                    // One record: glyph 0 at the run's own origin, little-endian, as
                    // `mdux::text::draw::decodeRecord()` reads it.
                    static const std::array<std::byte, 6> records{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};

                    static const mdux::text::TextPackage text = [] {
                        mdux::text::TextPackage built;
                        built.header.id         = "noheap-text";
                        built.header.kind       = std::string{mdux::text::packageKind};
                        built.atlasId           = "noheap-ui";
                        built.locale            = "en-US";
                        built.sidecarPath       = "runs.bin";
                        built.sidecarByteLength = records.size();
                        // The digests `create()` checks. Computed rather than written out, so the
                        // fixture cannot drift from the bytes above and start exercising the
                        // rejection path while claiming to measure the accepted one.
                        built.sidecarSha256 = mdux::evidence::sha256(records);
                        built.runs.push_back(
                            mdux::text::TextRun{.id = "STR-TITLE", .byteOffset = 0, .byteLength = records.size(), .sha256 = mdux::evidence::sha256(records)});
                        return built;
                    }();

                    // Through `create()`, which is the only way to obtain one - and which hashes
                    // the sidecar. That cost is the caller's, once, and sits outside the counter
                    // below on purpose: what this scenario measures is the frame, not the setup.
                    const auto canonical = text.write();
                    if (!canonical.has_value()) {
                        checks.expect(false, "the fixture package serializes");
                        checks.raise();
                        return;
                    }
                    const std::array approvals{
                        ms::TextPackageApproval{.locale        = text.locale,
                                                .packageId     = text.header.id,
                                                .packageSha256 = mdux::evidence::sha256(std::as_bytes(std::span{canonical->data(), canonical->size()}))}
                    };
                    ms::ScreenPackage boundScreen    = screen;
                    boundScreen.approvedTextPackages = approvals;

                    const auto made = ms::TextBinding::create(boundScreen, font, text, std::as_bytes(std::span{canonical->data(), canonical->size()}), records);
                    if (!made.has_value()) {
                        checks.expect(false, "the fixture binding is valid");
                        checks.raise();
                        return;
                    }
                    const ms::TextBinding binding = *made;

                    static std::array<mdux::draw::UiVertex, 512>   vertices{};
                    static std::array<mdux::draw::Index, 768>      indices{};
                    static std::array<mdux::draw::DrawCommand, 16> commands{};

                    auto created = mdux::draw::DrawList::create(vertices, indices, commands, budget);
                    if (!created.has_value()) {
                        checks.expect(false, "the storage satisfies the budget");
                        checks.raise();
                        return;
                    }
                    mdux::draw::DrawList list = std::move(*created);

                    // Nothing inside the measured loop may format a message, which is a rule this
                    // scenario learned by breaking it: `std::format` allocates, so a per-frame
                    // assertion carrying one would report the test's own allocation as the
                    // runtime's. The outcome is captured and asserted after the counter is read.
                    std::uint32_t lastRects   = 0;
                    bool          allRecorded = true;

                    const std::size_t before = allocations();
                    for (int frame = 0; frame < 8; ++frame) {
                        list.reset();
                        const auto recorded = ms::render(boundScreen, list, binding);
                        if (!recorded.has_value()) {
                            allRecorded = false;
                            continue;
                        }
                        lastRects = recorded->rects;
                    }
                    const std::size_t after = allocations();

                    checks.expect(allRecorded, "each frame is recorded");
                    // The label is drawn rather than deferred, so this scenario measures the text
                    // path rather than the same deferred walk the case above measures.
                    checks.expect(lastRects == 2, std::format("the panel and the glyph, got {}", lastRects));
                    checks.expect(after == before, std::format("no allocation across eight frames, counter went {} to {}", before, after));
                    checks.raise();
                })
            .Execute();
    }};

const mdux::spec::Register expandingATraceAllocatesNothing{
    "Expanding a bound waveform allocates nothing either",
    "noheap",
    [] {
        return speclab::Test("medui-screen-noheap-render-trace")
            .Given("a screen whose SignalTrace is bound to a caller-owned ring", [] {})
            .When("frames are recorded while the producer writes into the ring", [] {})
            .Then("the allocation counter does not move",
                  [] {
                      mdux::spec::Checks checks;

                      // The path #257 adds, and the one with the most ways to allocate quietly: a
                      // per-frame scratch for the expanded polyline is the obvious implementation of a
                      // waveform, and it is the implementation this measurement exists to refuse. The
                      // ring, the slots and the storage are all the caller's, made once, outside the
                      // counter.
                      constexpr ms::SignalTraceSpec                    trace{.streamSource = "ECG_LEAD_II", .colorToken = "Theme.Colors.Nominal"};
                      static constexpr std::array<ms::CompiledNode, 1> traceNodes{
                          ms::CompiledNode{.id = "ecg", .bounds = {0, 0, 200, 60}, .payload = trace}
                      };
                      static constexpr mdux::draw::DrawBudget traceBudget{.maxVertices = 512, .maxIndices = 768, .maxCommands = 16};
                      static constexpr ms::ScreenPackage      traceScreen{.id                   = "noheap-trace",
                                                                          .schemaVersion        = mdux::evidence::kSchemaVersion,
                                                                          .surfaceWidth         = 200,
                                                                          .surfaceHeight        = 60,
                                                                          .approvedTextPackages = {},
                                                                          .nodes                = traceNodes,
                                                                          .budget               = traceBudget};
                      static_assert(traceScreen.validate().has_value(), "the screen under measurement must be one a device could hold");

                      static std::array<float, 24>               samples{};
                      static ms::SampleRing                      ring{.storage = samples, .oldest = 0, .count = samples.size()};
                      static const std::array<ms::SignalSlot, 1> slots{
                          ms::SignalSlot{.streamSource = "ECG_LEAD_II",
                                         .ring         = &ring,
                                         .style        = ms::TraceStyle{.minimum = -1.0F, .maximum = 1.0F, .strokeWidth = 2}}
                      };

                      const auto made = ms::SignalBinding::create(traceScreen, slots);
                      if (!made.has_value()) {
                          checks.expect(false, "the fixture binding is valid");
                          checks.raise();
                          return;
                      }
                      const ms::SignalBinding binding = *made;

                      static std::array<mdux::draw::UiVertex, 512>   vertices{};
                      static std::array<mdux::draw::Index, 768>      indices{};
                      static std::array<mdux::draw::DrawCommand, 16> commands{};

                      auto created = mdux::draw::DrawList::create(vertices, indices, commands, traceBudget);
                      if (!created.has_value()) {
                          checks.expect(false, "the storage satisfies the budget");
                          checks.raise();
                          return;
                      }
                      mdux::draw::DrawList list = std::move(*created);

                      // Nothing inside the measured loop may format a message: `std::format`
                      // allocates, and a per-frame assertion carrying one would report the test's own
                      // allocation as the runtime's.
                      std::uint32_t lastTraces  = 0;
                      bool          allRecorded = true;

                      const std::size_t before = allocations();
                      for (int frame = 0; frame < 8; ++frame) {
                          // The producer, doing what a producer does between frames: writing one new
                          // sample and moving the ring's oldest index. A per-frame allocation hidden
                          // behind "the samples changed" would show up as eight.
                          samples[static_cast<std::size_t>(frame) % samples.size()] = 0.25F * static_cast<float>(frame % 5);
                          ring.oldest                                               = (ring.oldest + 1) % samples.size();

                          list.reset();
                          const auto recorded = ms::render(traceScreen, list, {}, {}, binding);
                          if (!recorded.has_value()) {
                              allRecorded = false;
                              continue;
                          }
                          lastTraces = recorded->traces;
                      }
                      const std::size_t after = allocations();

                      checks.expect(allRecorded, "each frame is recorded");
                      checks.expect(lastTraces == 1, std::format("the trace was expanded rather than left as a field, got {}", lastTraces));
                      checks.expect(after == before, std::format("no allocation across eight frames, counter went {} to {}", before, after));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register drawingAReadingAllocatesNothing{
    "Drawing a live reading and a clock allocates nothing either",
    "noheap",
    [] {
        return speclab::Test("medui-screen-noheap-render-reading")
            .Given("a screen whose NumericDisplay and Clock are bound to live values", [] {})
            .When("frames are recorded while the value and the time advance", [] {})
            .Then(
                "the allocation counter does not move",
                [] {
                    mdux::spec::Checks checks;

                    // The path #258 adds. Its obvious wrong implementation is a `std::string` for
                    // the formatted number - `std::format` into a scratch, then draw the characters
                    // - and that is exactly what this measurement refuses. The pattern is a
                    // `string_view` over static storage, the digits go into a fixed array, and the
                    // glyphs are recorded straight into the caller's list.
                    static const mdux::font::FontPackage font = [] {
                        mdux::font::FontPackage built;
                        built.id                     = "noheap-reading";
                        built.unitsPerEm             = 1000;
                        built.pixelSize              = 10;
                        built.locales                = {"en-US"};
                        built.atlas.path             = "atlas.bin";
                        built.atlas.width            = 64;
                        built.atlas.height           = 64;
                        built.atlas.byteLength       = 64 * 64;
                        built.atlas.sha256           = std::string(64, 'a');
                        built.atlas.occupancyPercent = 25;
                        // U+002E to U+003A: the decimal point, the ten digits and the colon - every
                        // character `##.#` and `HH:MM:SS` need, and no more.
                        for (char32_t point = U'.'; point <= U':'; ++point) {
                            const auto slot = static_cast<std::uint32_t>(built.glyphs.size());
                            built.glyphs.push_back(mdux::font::GlyphRecord{.codePoint       = point,
                                                                           .glyphIndex      = static_cast<std::uint16_t>(slot + 1),
                                                                           .advanceWidth    = 1000,
                                                                           .leftSideBearing = 0,
                                                                           .x               = slot * 4,
                                                                           .y               = 0,
                                                                           .width           = 4,
                                                                           .height          = 6,
                                                                           .bitmapOriginX   = 0,
                                                                           .bitmapOriginY   = 6});
                        }
                        built.restrictedCharset = {
                            {.first = U'.', .last = U':'}
                        };
                        return built;
                    }();

                    static const std::array<std::byte, 6> records{};
                    static const mdux::text::TextPackage  text = [] {
                        mdux::text::TextPackage built;
                        built.header.id         = "noheap-reading-text";
                        built.header.kind       = std::string{mdux::text::packageKind};
                        built.atlasId           = "noheap-reading";
                        built.locale            = "en-US";
                        built.sidecarPath       = "runs.bin";
                        built.sidecarByteLength = records.size();
                        built.sidecarSha256     = mdux::evidence::sha256(records);
                        built.runs.push_back(mdux::text::TextRun{
                            .id = "STR-UNUSED", .byteOffset = 0, .byteLength = records.size(), .sha256 = mdux::evidence::sha256(records)});
                        return built;
                    }();

                    const auto canonical = text.write();
                    if (!canonical.has_value()) {
                        checks.expect(false, "the fixture package serializes");
                        checks.raise();
                        return;
                    }
                    const std::array approvals{
                        ms::TextPackageApproval{.locale        = text.locale,
                                                .packageId     = text.header.id,
                                                .packageSha256 = mdux::evidence::sha256(std::as_bytes(std::span{canonical->data(), canonical->size()}))}
                    };

                    constexpr ms::NumericDisplaySpec pressure{
                        .requirement = "REQ-1", .templateId = "TPL-X", .source = "SRC", .colorToken = "Theme.Colors.ScoreDigits"};
                    constexpr ms::ClockSpec                  wall{.format = ms::ClockFormat::TimeSeconds};
                    static constexpr std::array<ms::CompiledNode, 2> readingNodes{
                        ms::CompiledNode{.id = "pressure", .bounds = {0, 0, 200, 20}, .payload = pressure},
                        ms::CompiledNode{   .id = "clock", .bounds = {0, 20, 200, 20}, .payload = wall}
                    };
                    ms::ScreenPackage readingScreen{.id                   = "noheap-reading",
                                                    .schemaVersion        = mdux::evidence::kSchemaVersion,
                                                    .surfaceWidth         = 200,
                                                    .surfaceHeight        = 60,
                                                    .approvedTextPackages = approvals,
                                                    .nodes                = readingNodes,
                                                    .budget               = budget};

                    const auto textBound =
                        ms::TextBinding::create(readingScreen, font, text, std::as_bytes(std::span{canonical->data(), canonical->size()}), records);
                    if (!textBound.has_value()) {
                        checks.expect(false, "the fixture text binding is valid");
                        checks.raise();
                        return;
                    }
                    const ms::TextBinding textBinding = *textBound;

                    // The value and the time a producer moves between frames. Static so the binding
                    // can point at them, exactly as a device's would.
                    static ms::CivilTime                 now{.year = 2026, .month = 9, .day = 5, .hour = 8, .minute = 0, .second = 0};
                    static std::array<ms::ReadingSlot, 1> slots{
                        ms::ReadingSlot{.nodeId = "pressure", .rendering = "##.#", .value = 0}
                    };

                    const auto made = ms::ReadingBinding::create(readingScreen, slots, &now, "Theme.Colors.Neutral");
                    if (!made.has_value()) {
                        checks.expect(false, "the fixture reading binding is valid");
                        checks.raise();
                        return;
                    }
                    const ms::ReadingBinding readingBinding = *made;

                    static std::array<mdux::draw::UiVertex, 512>   vertices{};
                    static std::array<mdux::draw::Index, 768>      indices{};
                    static std::array<mdux::draw::DrawCommand, 16> commands{};

                    auto created = mdux::draw::DrawList::create(vertices, indices, commands, budget);
                    if (!created.has_value()) {
                        checks.expect(false, "the storage satisfies the budget");
                        checks.raise();
                        return;
                    }
                    mdux::draw::DrawList list = std::move(*created);

                    // Nothing inside the measured loop may format a message: `std::format`
                    // allocates, and a per-frame assertion carrying one would report the test's own
                    // allocation as the runtime's.
                    std::uint32_t lastReadings = 0;
                    bool          allRecorded  = true;

                    const std::size_t before = allocations();
                    for (int frame = 0; frame < 8; ++frame) {
                        // The producer, between frames. A reading that changes its digit count is
                        // the case a cached formatting buffer would allocate for.
                        slots[0].value = static_cast<std::int64_t>(frame) * 13;
                        now.second     = static_cast<std::uint8_t>(frame * 7);

                        list.reset();
                        const auto recorded = ms::render(readingScreen, list, textBinding, {}, {}, readingBinding);
                        if (!recorded.has_value()) {
                            allRecorded = false;
                            continue;
                        }
                        lastReadings = recorded->readings;
                    }
                    const std::size_t after = allocations();

                    checks.expect(allRecorded, "each frame is recorded");
                    checks.expect(lastReadings == 2, std::format("the reading and the clock were both drawn, got {}", lastReadings));
                    checks.expect(after == before, std::format("no allocation across eight frames, counter went {} to {}", before, after));
                    checks.raise();
                })
            .Execute();
    }};

const mdux::spec::Register drawingAStateAllocatesNothing{
    "Drawing a bound status indicator allocates nothing, whichever state it is in",
    "noheap",
    [] {
        return speclab::Test("medui-screen-noheap-render-status")
            .Given("a screen whose StatusIndicator is bound to a state that changes between frames", [] {})
            .When("frames are recorded as the state moves through the closed list", [] {})
            .Then("the allocation counter does not move",
                  [] {
                      mdux::spec::Checks checks;

                      // The path #259 adds. Its obvious wrong implementation caches the state's run
                      // somewhere that grows - a map from node to `std::vector<std::byte>`, a
                      // `std::string` for the key - and a state change is what would make it allocate.
                      // What actually happens is a lookup in the bound package and a walk over the
                      // sidecar the binding already holds.
                      static const mdux::font::FontPackage font = [] {
                          mdux::font::FontPackage built;
                          built.id                     = "noheap-status";
                          built.unitsPerEm             = 1000;
                          built.pixelSize              = 10;
                          built.locales                = {"en-US"};
                          built.atlas.path             = "atlas.bin";
                          built.atlas.width            = 16;
                          built.atlas.height           = 16;
                          built.atlas.byteLength       = 16 * 16;
                          built.atlas.sha256           = std::string(64, 'a');
                          built.atlas.occupancyPercent = 25;
                          built.glyphs.push_back(mdux::font::GlyphRecord{.codePoint       = U'A',
                                                                         .glyphIndex      = 1,
                                                                         .advanceWidth    = 1000,
                                                                         .leftSideBearing = 0,
                                                                         .x               = 0,
                                                                         .y               = 0,
                                                                         .width           = 4,
                                                                         .height          = 6,
                                                                         .bitmapOriginX   = 0,
                                                                         .bitmapOriginY   = 6});
                          built.restrictedCharset = {
                              {.first = U'A', .last = U'A'}
                          };
                          return built;
                      }();

                      // Two runs of one v1 record each, little-endian: glyph 0 - the font's only one -
                      // at the run's own origin, then the same glyph four pixels along, so the two
                      // states differ in what is drawn rather than only in which key was looked up.
                      static const std::array<std::byte, 12> records{std::byte{0},
                                                                     std::byte{0},
                                                                     std::byte{0},
                                                                     std::byte{0},
                                                                     std::byte{0},
                                                                     std::byte{0},
                                                                     std::byte{0},
                                                                     std::byte{0},
                                                                     std::byte{4},
                                                                     std::byte{0},
                                                                     std::byte{0},
                                                                     std::byte{0}};

                      static const mdux::text::TextPackage text = [] {
                          mdux::text::TextPackage built;
                          built.header.id         = "noheap-status-text";
                          built.header.kind       = std::string{mdux::text::packageKind};
                          built.atlasId           = "noheap-status";
                          built.locale            = "en-US";
                          built.sidecarPath       = "runs.bin";
                          built.sidecarByteLength = records.size();
                          built.sidecarSha256     = mdux::evidence::sha256(records);
                          const auto slice        = [](std::size_t offset, std::size_t length) {
                              return std::span<const std::byte>{records}.subspan(offset, length);
                          };
                          built.runs.push_back(
                              mdux::text::TextRun{.id = "STR-OK", .byteOffset = 0, .byteLength = 6, .sha256 = mdux::evidence::sha256(slice(0, 6))});
                          built.runs.push_back(
                              mdux::text::TextRun{.id = "STR-ALARM", .byteOffset = 6, .byteLength = 6, .sha256 = mdux::evidence::sha256(slice(6, 6))});
                          return built;
                      }();

                      const auto canonical = text.write();
                      if (!canonical.has_value()) {
                          checks.expect(false, "the fixture package serializes");
                          checks.raise();
                          return;
                      }
                      const std::array approvals{
                          ms::TextPackageApproval{.locale        = text.locale,
                                                  .packageId     = text.header.id,
                                                  .packageSha256 = mdux::evidence::sha256(std::as_bytes(std::span{canonical->data(), canonical->size()}))}
                      };

                      static constexpr std::array              stateKeys{std::string_view{"STR-OK"}, std::string_view{"STR-ALARM"}};
                      static constexpr std::array              stateTints{std::string_view{"Theme.Colors.Nominal"}, std::string_view{"Theme.Colors.Fault"}};
                      static constexpr ms::StatusIndicatorSpec indicator{.requirement = "REQ-1",
                                                                         .source      = "STATE",
                                                                         .stateKeys   = stateKeys,
                                                                         .colorTokens = stateTints};
                      static constexpr std::array<ms::CompiledNode, 1> statusNodes{
                          ms::CompiledNode{.id = "state", .bounds = {0, 0, 200, 20}, .payload = indicator}
                      };
                      ms::ScreenPackage statusScreen{.id                   = "noheap-status",
                                                     .schemaVersion        = mdux::evidence::kSchemaVersion,
                                                     .surfaceWidth         = 200,
                                                     .surfaceHeight        = 60,
                                                     .approvedTextPackages = approvals,
                                                     .nodes                = statusNodes,
                                                     .budget               = budget};

                      const auto textBound =
                          ms::TextBinding::create(statusScreen, font, text, std::as_bytes(std::span{canonical->data(), canonical->size()}), records);
                      if (!textBound.has_value()) {
                          checks.expect(false, "the fixture text binding is valid");
                          checks.raise();
                          return;
                      }
                      const ms::TextBinding textBinding = *textBound;

                      // The state a producer moves between frames. Static so the binding can point at
                      // it, exactly as a device's would.
                      static std::array<ms::StatusSlot, 1> slots{
                          ms::StatusSlot{.nodeId = "state", .state = 0}
                      };

                      static std::array<mdux::draw::UiVertex, 512>   vertices{};
                      static std::array<mdux::draw::Index, 768>      indices{};
                      static std::array<mdux::draw::DrawCommand, 16> commands{};

                      auto created = mdux::draw::DrawList::create(vertices, indices, commands, budget);
                      if (!created.has_value()) {
                          checks.expect(false, "the storage satisfies the budget");
                          checks.raise();
                          return;
                      }
                      mdux::draw::DrawList list = std::move(*created);

                      std::uint32_t lastStates  = 0;
                      bool          allRecorded = true;

                      const std::size_t before = allocations();
                      for (int frame = 0; frame < 8; ++frame) {
                          slots[0].state = static_cast<std::uint32_t>(frame % 2);

                          // Rebuilt every frame, as a caller carrying a state by value must: the join
                          // is what would allocate if it held anything but spans.
                          const auto bound = ms::StatusBinding::create(statusScreen, slots);
                          if (!bound.has_value()) {
                              allRecorded = false;
                              continue;
                          }

                          list.reset();
                          const auto recorded = ms::render(statusScreen, list, textBinding, {}, {}, {}, *bound);
                          if (!recorded.has_value()) {
                              allRecorded = false;
                              continue;
                          }
                          lastStates = recorded->states;
                      }
                      const std::size_t after = allocations();

                      checks.expect(allRecorded, "each frame is recorded");
                      checks.expect(lastStates == 1, std::format("the indicator's state was drawn, got {}", lastStates));
                      checks.expect(after == before, std::format("no allocation across eight frames, counter went {} to {}", before, after));
                      checks.raise();
                  })
            .Execute();
    }};
