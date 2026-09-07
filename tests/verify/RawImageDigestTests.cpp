/**
 * @file RawImageDigestTests.cpp
 * @brief BDD scenarios for `rawImageDigest()` and the `RawImageExpectation` that feeds it.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: this suite links MduX::Core only)
 * @compliance ADR-014 What rendered-truth verification checks, and what it cannot
 * @compliance ADR-015 Versioned sibling observations (decision 2)
 * @compliance ADR-016 Locally versioned observation profiles for rendered checks
 *
 * `rawImageDigest()` is the observation TrustSC's `ColorHash` actually performs - a SHA-256 over a
 * rectangle's tightly packed row-major RGBA8 - named separately here so the two implementations'
 * `ColorHash` results are never compared as though they measured the same thing (ADR-015 D2).
 *
 * Three things these scenarios establish.
 *
 * **Every production call reports `NoBaseline`, and that is not a pass.** ADR-016 keeps a committed
 * baseline out of the byte-compared bundle, so `create()` (no baseline) resolves to `NoBaseline`,
 * which `held()` is false for - the way TrustSC's own `NoBaseline` behaves.
 *
 * **With a baseline the digest is exact.** A hand-computed SHA-256 over the ROI holds; a single
 * flipped channel is `DigestMismatch` - the smallest wrong answer, the one a tolerance would wave
 * through, which is the same discipline `tests/render/PixelTests.cpp` applies to a whole frame.
 *
 * **The digest is over tightly packed row-major RGBA8, scoped to the ROI.** It does not depend on
 * the framebuffer's row stride, and a pixel changed outside the ROI does not change the result.
 *
 * The frames are painted by hand with `SyntheticFrame.hpp`'s `Canvas`; no GPU is involved, which is
 * ADR-014 decision 1's first consequence made mechanical.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.core.units;
import mdux.evidence.digest;
import mdux.medui.schema;
import mdux.verify;

#include "../framework/SpecLabBridge.hpp"
#include "SyntheticFrame.hpp"

namespace {

namespace ms = mdux::medui;
namespace mv = mdux::verify;

using mdux::core::ColorRgba8;
using mdux::core::Px;
using mdux::test::verify::Canvas;

/// What a fixture clears to.
constexpr ColorRgba8 ground{.r = 10, .g = 10, .b = 10, .a = 255};

/// SHA-256 over `rect`'s pixels of `canvas`, row by row, four bytes per pixel, no stride padding -
/// the same "tightly packed row-major RGBA8" `rawImageDigest()` hashes, computed independently here
/// so a scenario checks the check rather than a shared helper.
[[nodiscard]] mdux::evidence::Digest digestOf(const Canvas& canvas, ms::NodeRect rect) {
    std::vector<std::byte> packed;
    packed.reserve(static_cast<std::size_t>(rect.width) * static_cast<std::size_t>(rect.height) * 4U);
    for (Px y = rect.y; y < rect.y + rect.height; ++y) {
        for (Px x = rect.x; x < rect.x + rect.width; ++x) {
            const ColorRgba8 pixel = canvas.at(x, y);
            packed.push_back(std::byte{pixel.r});
            packed.push_back(std::byte{pixel.g});
            packed.push_back(std::byte{pixel.b});
            packed.push_back(std::byte{pixel.a});
        }
    }
    return mdux::evidence::sha256(packed);
}

/// Builds the expectation or fails the scenario, so a `Then` reads as the claim.
[[nodiscard]] mv::RawImageExpectation withBaseline(ms::NodeRect roi, mdux::evidence::Digest baseline) {
    auto made = mv::RawImageExpectation::createWithBaseline("readout", mv::RenderScope::localeFree(), roi, baseline);
    if (!made.has_value()) {
        throw speclab::core::AssertionFailure(std::string{"the expectation was refused: "} + std::string{mv::describe(made.error())},
                                              std::source_location::current());
    }
    return *made;
}

[[nodiscard]] mv::RawImageExpectation noBaseline(ms::NodeRect roi) {
    auto made = mv::RawImageExpectation::create("readout", mv::RenderScope::localeFree(), roi);
    if (!made.has_value()) {
        throw speclab::core::AssertionFailure(std::string{"the expectation was refused: "} + std::string{mv::describe(made.error())},
                                              std::source_location::current());
    }
    return *made;
}

constexpr ms::NodeRect roi{.x = 4, .y = 4, .width = 8, .height = 6};

}  // namespace

const mdux::spec::Register aMatchingBaselineHolds{
    "A region whose SHA-256 is the committed baseline holds, and names the raw-image-digest profile",
    "evidence-unit",
    [] {
        return speclab::Test("verify-rawdigest-matches-baseline")
            .Given("a painted frame and the digest of its ROI computed by hand", [] {})
            .When("rawImageDigest runs with that digest as the baseline", [] {})
            .Then("it holds, and the outcome carries mdux.local/raw-image-digest v1",
                  [] {
                      mdux::spec::Checks checks;

                      Canvas canvas{16, 20, ground};
                      canvas.fill(roi, ColorRgba8{.r = 33, .g = 184, .b = 107, .a = 255});
                      canvas.set(6, 6, ColorRgba8{.r = 200, .g = 12, .b = 40, .a = 255});

                      const mv::CheckOutcome outcome = mv::rawImageDigest(canvas.view(), withBaseline(roi, digestOf(canvas, roi)));

                      checks.expect(outcome.held(), std::format("the digest matches: {}", mv::describe(outcome.finding)));
                      checks.expect(outcome.check == "RawImageDigest", "the outcome names the check");
                      checks.expect(outcome.profile == mv::rawImageDigestProfile, "and its observation profile");
                      checks.expect(outcome.profile.id() == "mdux.local/raw-image-digest", "which is the raw-image-digest id");
                      checks.expect(outcome.profile.version() == 1, "at version 1");
                      checks.expect(outcome.nodeId == "readout" && outcome.scope == mv::localeFreeScopeName, "and the node and scope");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register oneFlippedChannelIsAMismatch{
    "A single flipped channel in the region is a DigestMismatch, not a pass",
    "evidence-unit",
    [] {
        return speclab::Test("verify-rawdigest-mismatch")
            .Given("a baseline taken from one frame", [] {})
            .When("one channel of one ROI pixel is changed and the check runs", [] {})
            .Then("it reports DigestMismatch",
                  [] {
                      mdux::spec::Checks checks;

                      Canvas original{16, 20, ground};
                      original.fill(roi, ColorRgba8{.r = 33, .g = 184, .b = 107, .a = 255});
                      const mdux::evidence::Digest baseline = digestOf(original, roi);

                      Canvas altered{16, 20, ground};
                      altered.fill(roi, ColorRgba8{.r = 33, .g = 184, .b = 107, .a = 255});
                      altered.set(7, 7, ColorRgba8{.r = 34, .g = 184, .b = 107, .a = 255});  // +1 on red

                      const mv::CheckOutcome outcome = mv::rawImageDigest(altered.view(), withBaseline(roi, baseline));
                      checks.expect(outcome.finding == mv::Finding::DigestMismatch, std::format("smallest wrong answer is a mismatch: {}", mv::describe(outcome.finding)));
                      checks.expect(!outcome.held(), "which is a failure");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register anAbsentBaselineNeverPasses{
    "With no committed baseline the region cannot discharge anything: NoBaseline, never Held",
    "evidence-unit",
    [] {
        return speclab::Test("verify-rawdigest-absent-baseline")
            .Given("a painted frame and an expectation built without a baseline", [] {})
            .When("rawImageDigest runs", [] {})
            .Then("it reports NoBaseline, which is not a pass, and still names the node, scope and profile",
                  [] {
                      mdux::spec::Checks checks;

                      Canvas canvas{16, 20, ground};
                      canvas.fill(roi, ColorRgba8{.r = 60, .g = 107, .b = 44, .a = 255});

                      const mv::CheckOutcome outcome = mv::rawImageDigest(canvas.view(), noBaseline(roi));
                      checks.expect(outcome.finding == mv::Finding::NoBaseline, std::format("no baseline to compare against: {}", mv::describe(outcome.finding)));
                      checks.expect(!outcome.held(), "and NoBaseline never discharges an obligation");
                      checks.expect(outcome.nodeId == "readout", "the outcome still names the node");
                      checks.expect(outcome.scope == mv::localeFreeScopeName, "and the scope");
                      checks.expect(outcome.profile == mv::rawImageDigestProfile, "and the observation profile");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aRegionOffTheFrameFails{
    "A region that is not inside the frame fails rather than reading past it",
    "evidence-unit",
    [] {
        return speclab::Test("verify-rawdigest-region-outside-frame")
            .Given("an ROI wider than the frame handed in", [] {})
            .When("rawImageDigest runs", [] {})
            .Then("it fails as a region outside the frame",
                  [] {
                      mdux::spec::Checks checks;

                      Canvas small{8, 8, ground};
                      const mv::CheckOutcome outcome = mv::rawImageDigest(small.view(), noBaseline(roi));
                      checks.expect(outcome.finding == mv::Finding::RegionOutsideFrame, "the rectangle leaves the frame");
                      checks.expect(!outcome.held(), "which is a failure, not a skip");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theDigestIsRowMajorAndStrideIndependent{
    "The digest is over tightly packed RGBA8: the framebuffer's row stride does not change it",
    "evidence-unit",
    [] {
        return speclab::Test("verify-rawdigest-row-major-independent-of-stride")
            .Given("one image as a packed view and as a padded-stride view", [] {})
            .When("rawImageDigest runs over each against the packed image's own digest", [] {})
            .Then("both hold: padding between rows is not part of the observation",
                  [] {
                      mdux::spec::Checks checks;

                      constexpr Px          width   = 6;
                      constexpr Px          height  = 4;
                      constexpr std::size_t widthSz = static_cast<std::size_t>(width);
                      std::vector<ColorRgba8> pixels(widthSz * static_cast<std::size_t>(height), ground);
                      for (std::size_t y = 0; y < static_cast<std::size_t>(height); ++y) {
                          for (std::size_t x = 0; x < widthSz; ++x) {
                              pixels[y * widthSz + x] = ColorRgba8{.r = static_cast<std::uint8_t>(x * 8),
                                                                  .g = static_cast<std::uint8_t>(y * 8),
                                                                  .b = 42,
                                                                  .a = 255};
                          }
                      }

                      const auto packed = mv::FramebufferView::createPacked(pixels, width, height);
                      if (!packed.has_value()) {
                          throw speclab::core::AssertionFailure("the packed view was refused", std::source_location::current());
                      }

                      // The same pixels with 8 padding bytes per row.
                      constexpr std::size_t paddedStride = widthSz * 4 + 8;
                      std::vector<std::byte> paddedBytes(paddedStride * static_cast<std::size_t>(height), std::byte{0xEE});
                      for (std::size_t y = 0; y < static_cast<std::size_t>(height); ++y) {
                          for (std::size_t x = 0; x < widthSz; ++x) {
                              const ColorRgba8  pixel = pixels[y * widthSz + x];
                              const std::size_t base  = y * paddedStride + x * 4;
                              paddedBytes[base + 0]   = std::byte{pixel.r};
                              paddedBytes[base + 1]   = std::byte{pixel.g};
                              paddedBytes[base + 2]   = std::byte{pixel.b};
                              paddedBytes[base + 3]   = std::byte{pixel.a};
                          }
                      }
                      const auto padded = mv::FramebufferView::create(paddedBytes, width, height, paddedStride, mv::PixelFormat::Rgba8Unorm);
                      if (!padded.has_value()) {
                          throw speclab::core::AssertionFailure(std::string{"the padded view was refused: "} + std::string{mv::describe(padded.error())},
                                                                std::source_location::current());
                      }

                      // The digest of the whole image, packed.
                      std::vector<std::byte> tight;
                      for (const ColorRgba8& pixel : pixels) {
                          tight.push_back(std::byte{pixel.r});
                          tight.push_back(std::byte{pixel.g});
                          tight.push_back(std::byte{pixel.b});
                          tight.push_back(std::byte{pixel.a});
                      }
                      const mdux::evidence::Digest baseline = mdux::evidence::sha256(tight);
                      const ms::NodeRect whole{.x = 0, .y = 0, .width = width, .height = height};

                      checks.expect(mv::rawImageDigest(*packed, withBaseline(whole, baseline)).held(), "the packed view matches");
                      checks.expect(mv::rawImageDigest(*padded, withBaseline(whole, baseline)).held(), "and so does the padded one");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theDigestIsScopedToTheRoi{
    "The digest covers only the ROI: a pixel changed outside it does not change the result",
    "evidence-unit",
    [] {
        return speclab::Test("verify-rawdigest-roi-scoped")
            .Given("a baseline for a sub-rectangle of the frame", [] {})
            .When("a pixel outside that rectangle is changed", [] {})
            .Then("the ROI digest still holds, and the whole-frame digest would not",
                  [] {
                      mdux::spec::Checks checks;

                      Canvas canvas{16, 20, ground};
                      canvas.fill(roi, ColorRgba8{.r = 33, .g = 184, .b = 107, .a = 255});
                      const mdux::evidence::Digest roiBaseline   = digestOf(canvas, roi);
                      const ms::NodeRect           whole{.x = 0, .y = 0, .width = 16, .height = 20};
                      const mdux::evidence::Digest wholeBaseline = digestOf(canvas, whole);

                      canvas.set(0, 0, ColorRgba8{.r = 255, .g = 255, .b = 255, .a = 255});  // outside the ROI

                      checks.expect(mv::rawImageDigest(canvas.view(), withBaseline(roi, roiBaseline)).held(), "the ROI digest is unchanged");
                      checks.expect(mv::rawImageDigest(canvas.view(), withBaseline(whole, wholeBaseline)).finding == mv::Finding::DigestMismatch,
                                    "while the whole-frame digest is not");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theProfileIsImplementationLocalAndVersioned{
    "The raw-image-digest profile is implementation-local and immutably versioned",
    "evidence-unit",
    [] {
        return speclab::Test("verify-rawdigest-profile-identity")
            .Given("the rawImageDigestProfile constant", [] {})
            .When("its id and version are read", [] {})
            .Then("the id carries the implementation-local prefix and the version is 1",
                  [] {
                      mdux::spec::Checks checks;
                      checks.expect(mv::rawImageDigestProfile.id() == "mdux.local/raw-image-digest", "the id is mdux.local/raw-image-digest");
                      checks.expect(mv::rawImageDigestProfile.id().starts_with(mv::localProfilePrefix), "which is implementation-local (ADR-016)");
                      checks.expect(mv::rawImageDigestProfile.version() == 1, "at version 1");
                      checks.expect(mv::rawImageDigestProfile.valid(), "and it names a profile");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aDegenerateRoiIsRefused{
    "An expectation over a non-positive rectangle is refused before any check runs",
    "evidence-unit",
    [] {
        return speclab::Test("verify-rawdigest-roi-degenerate-refused")
            .Given("a region of interest with zero width", [] {})
            .When("an expectation is built from it", [] {})
            .Then("it is refused as a degenerate ROI",
                  [] {
                      mdux::spec::Checks checks;

                      const auto made = mv::RawImageExpectation::create("readout", mv::RenderScope::localeFree(),
                                                                        ms::NodeRect{.x = 4, .y = 4, .width = 0, .height = 6});
                      checks.expect(!made.has_value(), "the expectation is not built");
                      if (!made.has_value()) {
                          checks.expect(made.error() == mv::VerifyError::DigestRoiDegenerate, "and the reason names the degenerate ROI");
                      }
                      checks.raise();
                  })
            .Execute();
    }};
