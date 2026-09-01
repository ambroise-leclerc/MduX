/**
 * @file FramebufferTests.cpp
 * @brief BDD scenarios for the read-only framebuffer view every check is given.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: this suite links MduX::Core only)
 * @compliance ADR-014 What rendered-truth verification checks, and what it cannot
 *
 * The view is the whole of the GPU's contribution to verification, so its refusals are what stand
 * between a check and somebody else's memory. Two of them are worth their own scenarios rather than
 * being implied by the checks that use them: a row stride that does not hold a row, and a span
 * shorter than the extent it claims. Both describe an image that does not exist, and a view that
 * accepted either would hand every check a plausible wrong answer instead of a failure.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.core.units;
import mdux.medui.schema;
import mdux.verify;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace ms = mdux::medui;
namespace mv = mdux::verify;

using mdux::core::ColorRgba8;

constexpr ColorRgba8 red{.r = 255, .g = 0, .b = 0, .a = 255};
constexpr ColorRgba8 blue{.r = 0, .g = 0, .b = 255, .a = 255};

}  // namespace

const mdux::spec::Register aPackedImageIsIndexedByRowAndColumn{
    "A packed image answers the pixel at a coordinate, and nothing outside itself",
    "evidence-unit",
    [] {
        return speclab::Test("verify-framebuffer-packed")
            .Given("a four-by-two image with one pixel deliberately different", [] {})
            .When("it is described as a framebuffer view", [] {})
            .Then("every coordinate reads back what was written, and the rest reads as nothing",
                  [] {
                      mdux::spec::Checks checks;

                      std::array<ColorRgba8, 8> pixels{};
                      pixels.fill(red);
                      pixels[5] = blue;  // (1, 1)

                      auto view = mv::FramebufferView::createPacked(pixels, 4, 2);
                      checks.expect(view.has_value(), "the view is accepted");
                      if (!view.has_value()) {
                          checks.raise();
                          return;
                      }

                      checks.expect(view->width() == 4 && view->height() == 2, "the extent is the one declared");
                      checks.expect(view->rowStride() == 16, "and a packed row is four pixels of four bytes");
                      checks.expect(view->pixelAt(1, 1) == blue, "the pixel written is the pixel read");
                      checks.expect(view->pixelAt(0, 1) == red, "and its neighbour is untouched");
                      // An optional rather than whatever was next in memory: an out-of-range read is
                      // a mistake in an expectation, and it should say so.
                      checks.expect(!view->pixelAt(4, 0).has_value(), "one past the last column is nothing");
                      checks.expect(!view->pixelAt(0, 2).has_value(), "and so is one past the last row");
                      checks.expect(!view->pixelAt(-1, 0).has_value(), "and so is a negative coordinate");

                      checks.expect(view->contains(ms::NodeRect{0, 0, 4, 2}), "the whole image is inside itself");
                      checks.expect(!view->contains(ms::NodeRect{0, 0, 5, 2}), "and one column more is not");
                      checks.expect(!view->contains(ms::NodeRect{0, 0, 4, 0}), "a degenerate rectangle is inside nothing");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aPaddedRowIsReadAtItsStride{
    "A padded readback is read at its stride rather than at its width",
    "evidence-unit",
    [] {
        return speclab::Test("verify-framebuffer-stride")
            .Given("a two-by-two image whose rows are padded to three pixels", [] {})
            .When("it is described with that stride", [] {})
            .Then("the second row starts after the padding, not after the pixels",
                  [] {
                      mdux::spec::Checks checks;

                      // A readback is not obliged to be tightly packed, and a check that assumed it
                      // was would read this row's padding as the next row's first pixel.
                      std::array<ColorRgba8, 6> storage{};
                      storage.fill(red);
                      storage[3] = blue;  // the first pixel of the second row, at stride 3

                      auto view = mv::FramebufferView::create(std::as_bytes(std::span{storage}), 2, 2, 12, mv::PixelFormat::Rgba8Unorm);
                      checks.expect(view.has_value(), "the padded view is accepted");
                      if (!view.has_value()) {
                          checks.raise();
                          return;
                      }
                      checks.expect(view->pixelAt(0, 1) == blue, "the second row begins at the stride");
                      checks.expect(view->pixelAt(1, 0) == red, "and the first row is unchanged");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register anImageThatCannotExistIsRefused{
    "A framebuffer whose description cannot hold its own image is refused",
    "evidence-unit",
    [] {
        return speclab::Test("verify-framebuffer-fails-closed")
            .Given("an empty extent, a stride too small for a row, and a span too small for the image", [] {})
            .When("each is described as a framebuffer view", [] {})
            .Then("each is refused, naming what was wrong with the description",
                  [] {
                      mdux::spec::Checks checks;

                      std::array<ColorRgba8, 8>        pixels{};
                      const std::span<const std::byte> bytes = std::as_bytes(std::span{pixels});

                      const auto error = [](auto&& made) {
                          return made.has_value() ? std::optional<mv::VerifyError>{} : std::optional<mv::VerifyError>{made.error()};
                      };

                      checks.expect(error(mv::FramebufferView::create(bytes, 0, 2, 16, mv::PixelFormat::Rgba8Unorm)) == mv::VerifyError::EmptyFramebuffer,
                                    "zero width is nothing to look at");
                      checks.expect(error(mv::FramebufferView::create(bytes, 4, 2, 12, mv::PixelFormat::Rgba8Unorm)) == mv::VerifyError::RowStrideTooSmall,
                                    "a stride that cannot hold a row");
                      checks.expect(error(mv::FramebufferView::create(bytes, 4, 3, 16, mv::PixelFormat::Rgba8Unorm)) == mv::VerifyError::FramebufferTooSmall,
                                    "and a span that cannot hold the image");
                      // The last row is allowed to be unpadded, which is what a readback of exactly
                      // width * height * 4 bytes looks like.
                      checks.expect(mv::FramebufferView::create(bytes, 4, 2, 16, mv::PixelFormat::Rgba8Unorm).has_value(),
                                    "an exactly-sized packed image is admitted");
                      checks.raise();
                  })
            .Execute();
    }};
