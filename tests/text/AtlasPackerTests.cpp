/**
 * @file AtlasPackerTests.cpp
 * @brief BDD scenarios for the host-only shelf atlas packer (issue #160).
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * The properties asserted here are the ones a committed artifact depends on, and each is checked
 * directly rather than through a digest: dimensions are powers of two, no two glyphs overlap, no
 * glyph escapes the sheet, and the same input produces the same layout however it was ordered on
 * the way in. A digest would pin all four at once and tell you nothing about which had broken.
 *
 * Overlap in particular is worth testing explicitly rather than trusting: a packer that placed two
 * glyphs on top of each other would produce an atlas that looks plausible, packs efficiently, and
 * renders one of the two as garbage.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.tools.atlaspacker;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace ap = mdux::tools::atlas;
using ap::PackError;

/// A spread of sizes with the awkward cases a real font produces: a tall narrow 'l', a wide 'W',
/// square digits, and a blank space. Deliberately not uniform - a packer only shows its shelf
/// behaviour when heights differ.
[[nodiscard]] std::vector<ap::GlyphExtent> mixedGlyphs(std::uint32_t count = 40) {
    std::vector<ap::GlyphExtent> extents;
    extents.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        // A deterministic spread, not a random one: the layout is committed, so the fixture that
        // produces it has to be reproducible from its own source.
        const std::uint32_t w = 4 + (i * 7) % 23;
        const std::uint32_t h = 5 + (i * 11) % 17;
        extents.push_back(ap::GlyphExtent{.id = i, .width = w, .height = h});
    }
    extents.push_back(ap::GlyphExtent{.id = count, .width = 0, .height = 0});  // the space
    return extents;
}

[[nodiscard]] bool isPowerOfTwo(std::uint32_t v) noexcept {
    return v != 0 && (v & (v - 1)) == 0;
}

/// Reports the first overlapping pair, if any, as a readable message.
[[nodiscard]] std::optional<std::string> findOverlap(const ap::AtlasLayout& layout) {
    for (std::size_t a = 0; a < layout.slots.size(); ++a) {
        const auto& lhs = layout.slots[a];
        if (lhs.width == 0 || lhs.height == 0) {
            continue;
        }
        for (std::size_t b = a + 1; b < layout.slots.size(); ++b) {
            const auto& rhs = layout.slots[b];
            if (rhs.width == 0 || rhs.height == 0) {
                continue;
            }
            const bool apart = lhs.x + lhs.width <= rhs.x || rhs.x + rhs.width <= lhs.x || lhs.y + lhs.height <= rhs.y
                               || rhs.y + rhs.height <= lhs.y;
            if (!apart) {
                return std::format("glyph {} at ({},{},{}x{}) overlaps glyph {} at ({},{},{}x{})", lhs.id, lhs.x, lhs.y, lhs.width,
                                   lhs.height, rhs.id, rhs.x, rhs.y, rhs.width, rhs.height);
            }
        }
    }
    return std::nullopt;
}

}  // namespace

const mdux::spec::Register layoutIsSoundAndPowerOfTwo{
    "A packed sheet is power-of-two, contains every glyph, and overlaps none of them",
    "evidence-unit",
    [] {
        struct State {
            std::vector<ap::GlyphExtent> extents;
            ap::AtlasLayout              layout;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-atlas-layout-sound")
            .Given("forty glyphs of mixed size plus a blank", [state] { state->extents = mixedGlyphs(); })
            .When("they are packed",
                  [state] {
                      auto result = ap::pack(state->extents);
                      if (!result.has_value()) {
                          throw speclab::core::AssertionFailure(std::format("packing failed: {}", ap::describe(result.error())),
                                                                std::source_location::current());
                      }
                      state->layout = std::move(*result);
                  })
            .Then("both edges are powers of two, every glyph is inside, and none overlap",
                  [state] {
                      mdux::spec::Checks checks;
                      const auto&        layout = state->layout;
                      checks.expect(isPowerOfTwo(layout.width) && isPowerOfTwo(layout.height),
                                    std::format("{}x{} are both powers of two", layout.width, layout.height));
                      checks.expect(layout.slots.size() == state->extents.size(),
                                    std::format("every glyph got a slot: {} of {}", layout.slots.size(), state->extents.size()));
                      for (const auto& slot : layout.slots) {
                          checks.expect(slot.x + slot.width <= layout.width && slot.y + slot.height <= layout.height,
                                        std::format("glyph {} at ({},{}) {}x{} is inside the {}x{} sheet", slot.id, slot.x, slot.y,
                                                    slot.width, slot.height, layout.width, layout.height));
                      }
                      const auto overlap = findOverlap(layout);
                      checks.expect(!overlap.has_value(), overlap.value_or("no two glyphs overlap"));
                      // Sorted by id so a caller can binary-search and a committed diff stays
                      // readable when one glyph changes size.
                      checks.expect(std::ranges::is_sorted(layout.slots, {}, &ap::GlyphSlot::id), "slots are sorted by id");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register layoutIsIndependentOfInputOrder{
    "The same glyph set packs identically however it arrived",
    "evidence-unit",
    [] {
        // The property that makes a committed atlas reviewable. If placement depended on input
        // order, a recipe listing the same characters in a different order would produce a
        // different `atlas.bin` and every byte-identity check would be measuring the recipe's
        // formatting rather than the pipeline.
        //
        // This is also what the (height, width, id) sort exists for: height alone would leave
        // equal-height glyphs free to swap depending on whether std::sort happened to be stable.
        struct State {
            ap::AtlasLayout forward;
            ap::AtlasLayout reversed;
            ap::AtlasLayout shuffled;
        };
        auto state = std::make_shared<State>();

        return speclab::Test("text-atlas-order-independent")
            .Given("one glyph set in three different input orders", [] {})
            .When("each is packed",
                  [state] {
                      auto forward = mixedGlyphs();
                      auto reversed = forward;
                      std::ranges::reverse(reversed);
                      auto shuffled = forward;
                      // A fixed permutation, not a random shuffle: the fixture has to reproduce.
                      for (std::size_t i = 0; i + 1 < shuffled.size(); i += 2) {
                          std::swap(shuffled[i], shuffled[i + 1]);
                      }
                      const auto packOrThrow = [](std::span<const ap::GlyphExtent> in, std::string_view what) {
                          auto r = ap::pack(in);
                          if (!r.has_value()) {
                              throw speclab::core::AssertionFailure(std::format("{} failed: {}", what, ap::describe(r.error())),
                                                                    std::source_location::current());
                          }
                          return std::move(*r);
                      };
                      state->forward  = packOrThrow(forward, "forward");
                      state->reversed = packOrThrow(reversed, "reversed");
                      state->shuffled = packOrThrow(shuffled, "shuffled");
                  })
            .Then("all three layouts are identical",
                  [state] {
                      mdux::spec::Checks checks;
                      const auto same = [](const ap::AtlasLayout& a, const ap::AtlasLayout& b) {
                          if (a.width != b.width || a.height != b.height || a.slots.size() != b.slots.size()) {
                              return false;
                          }
                          for (std::size_t i = 0; i < a.slots.size(); ++i) {
                              const auto& l = a.slots[i];
                              const auto& r = b.slots[i];
                              if (l.id != r.id || l.x != r.x || l.y != r.y || l.width != r.width || l.height != r.height) {
                                  return false;
                              }
                          }
                          return true;
                      };
                      checks.expect(same(state->forward, state->reversed),
                                    std::format("reversed matches forward ({}x{} vs {}x{})", state->reversed.width,
                                                state->reversed.height, state->forward.width, state->forward.height));
                      checks.expect(same(state->forward, state->shuffled), "the swapped-pairs order matches forward");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register blankGlyphsCostNothing{
    "A blank glyph gets a slot but occupies no area",
    "evidence-unit",
    [] {
        // The space character, which every charset includes and no atlas should pay for. It still
        // needs a slot: the package records one entry per baked code point, and a missing entry
        // would read as "this character was not baked" rather than "this character is blank".
        return speclab::Test("text-atlas-blank-glyph")
            .Given("nothing", [] {})
            .When("nothing", [] {})
            .Then("blanks are recorded at zero size and do not grow the sheet",
                  [] {
                      mdux::spec::Checks              checks;
                      std::vector<ap::GlyphExtent>    solid{{0, 20, 20}, {1, 20, 20}};
                      std::vector<ap::GlyphExtent>    withBlanks = solid;
                      for (std::uint32_t i = 0; i < 50; ++i) {
                          withBlanks.push_back(ap::GlyphExtent{.id = 2 + i, .width = 0, .height = 0});
                      }
                      auto a = ap::pack(solid);
                      auto b = ap::pack(withBlanks);
                      checks.expect(a.has_value() && b.has_value(), "both sets pack");
                      if (a.has_value() && b.has_value()) {
                          checks.expect(a->width == b->width && a->height == b->height,
                                        std::format("fifty blanks did not change the sheet: {}x{} vs {}x{}", a->width, a->height,
                                                    b->width, b->height));
                          checks.expect(b->slots.size() == 52, "every blank still got a slot");
                          const bool allZero = std::ranges::all_of(b->slots, [](const ap::GlyphSlot& s) {
                              return s.id < 2 || (s.width == 0 && s.height == 0);
                          });
                          checks.expect(allZero, "blanks are zero-sized");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register packRejections{
    "pack() emits the right stable code per failure mode",
    "evidence-unit",
    [] {
        struct Case {
            std::string_view                            what;
            PackError                                   expected;
            std::function<std::vector<ap::GlyphExtent>()> build;
        };

        const std::vector<Case> cases{
            {"an empty glyph set", PackError::NoGlyphs,
             [] {
                 return std::vector<ap::GlyphExtent>{};
             }},
            {"two glyphs sharing an id", PackError::DuplicateGlyphId,
             [] {
                 return std::vector<ap::GlyphExtent>{{7, 10, 10}, {7, 12, 12}};
             }},
            {"one glyph wider than the maximum edge", PackError::GlyphTooLarge,
             [] {
                 // Distinct from AtlasBudgetExceeded on purpose: no sheet size can help, so the
                 // author needs to be told the glyph is the problem, not the budget.
                 return std::vector<ap::GlyphExtent>{{0, ap::maximumAtlasEdge + 1, 10}};
             }},
            {"a set that cannot fit the maximum sheet", PackError::AtlasBudgetExceeded,
             [] {
                 // Each glyph fits alone; together they cannot. 8192x8192 is 67.1M pixels, and
                 // 1200 glyphs of 4096x64 need 314M even before padding.
                 std::vector<ap::GlyphExtent> extents;
                 for (std::uint32_t i = 0; i < 1200; ++i) {
                     extents.push_back(ap::GlyphExtent{.id = i, .width = 4096, .height = 64});
                 }
                 return extents;
             }},
        };

        return speclab::Test("text-atlas-rejections")
            .Given("a corpus of unpackable glyph sets", [] {})
            .When("each is packed", [] {})
            .Then("each yields exactly the PackError identified in the corpus",
                  [&cases] {
                      mdux::spec::Checks checks;
                      for (const Case& entry : cases) {
                          const auto extents = entry.build();
                          auto       result  = ap::pack(extents);
                          checks.expect(!result.has_value(), std::format("{}: packing succeeded unexpectedly", entry.what));
                          if (!result.has_value()) {
                              checks.expect(result.error() == entry.expected,
                                            std::format("{}: got '{}', expected '{}'", entry.what, ap::describe(result.error()),
                                                        ap::describe(entry.expected)));
                          }
                      }
                      checks.raise();
                  })
            .Execute();
    }};
