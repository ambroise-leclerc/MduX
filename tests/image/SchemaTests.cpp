/**
 * @file SchemaTests.cpp
 * @brief Image package schema scenarios.
 */
import std;
import speclab;
import mdux.evidence.digest;
import mdux.image.schema;
#include "../framework/SpecLabBridge.hpp"

namespace {
namespace image = mdux::image;

[[nodiscard]] image::ImagePackage validPackage() {
    constexpr std::array<std::byte, 8> pixels{};
    image::ImagePackage                package;
    package.header.id         = "two-pixels";
    package.width             = 2;
    package.height            = 1;
    package.sidecarByteLength = pixels.size();
    package.sidecarSha256     = mdux::evidence::sha256(pixels);
    return package;
}
}  // namespace

const mdux::spec::Register imageSchemaRoundTrip{"An image package round-trips through canonical JSON", "evidence-unit", [] {
                                                    return speclab::Test("image-schema-round-trip")
                                                        .Given("a valid two-pixel image package", [] {})
                                                        .When("it is serialized and parsed", [] {})
                                                        .Then("every field and canonical digest survives",
                                                              [] {
                                                                  image::ImagePackage package = validPackage();
                                                                  const auto          written = package.write();
                                                                  if (!written.has_value())
                                                                      throw speclab::core::AssertionFailure("write failed", std::source_location::current());
                                                                  const auto parsed = image::ImagePackage::parse(*written);
                                                                  if (!parsed.has_value())
                                                                      throw speclab::core::AssertionFailure("parse failed", std::source_location::current());
                                                                  mdux::spec::Checks checks;
                                                                  checks.expect(parsed->header.id == package.header.id, "id survives");
                                                                  checks.expect(parsed->width == 2 && parsed->height == 1, "extent survives");
                                                                  checks.expect(parsed->sidecarSha256 == package.sidecarSha256, "sidecar digest survives");
                                                                  checks.expect(parsed->canonicalSha256() == package.canonicalSha256(),
                                                                                "canonical digest survives");
                                                                  checks.raise();
                                                              })
                                                        .Execute();
                                                }};

const mdux::spec::Register imageSchemaRejectsSizeDrift{"An image package refuses a sidecar length that is not RGBA8", "evidence-unit", [] {
                                                           return speclab::Test("image-schema-size")
                                                               .Given("a valid image package with a drifted sidecar length", [] {})
                                                               .When("the governed schema validates it", [] {})
                                                               .Then("validation identifies the size mismatch",
                                                                     [] {
                                                                         image::ImagePackage package = validPackage();
                                                                         ++package.sidecarByteLength;
                                                                         const auto         valid = package.validate();
                                                                         mdux::spec::Checks checks;
                                                                         checks.expect(!valid.has_value(), "invalid package is refused");
                                                                         if (!valid.has_value())
                                                                             checks.expect(valid.error() == image::SchemaError::SidecarSizeMismatch,
                                                                                           "error names sidecar size");
                                                                         checks.raise();
                                                                     })
                                                               .Execute();
                                                       }};

const mdux::spec::Register imageSchemaRejectsReservedSidecars{
    "Image sidecars cannot overwrite the package or report artifact",
    "evidence-unit",
    [] {
        return speclab::Test("image-schema-reserved-sidecar")
            .Given("reserved artifact names and one unrelated sidecar name", [] {})
            .When("each package is validated", [] {})
            .Then("both reserved artifact names are refused by the governed schema",
                  [] {
                      mdux::spec::Checks checks;
                      for (const std::string_view reserved : {"package.json", "report.json", "PACKAGE.JSON", "package.json.", "report.json "}) {
                          image::ImagePackage package = validPackage();
                          package.sidecarPath         = reserved;
                          const auto valid            = package.validate();
                          checks.expect(!valid.has_value() && valid.error() == image::SchemaError::ReservedSidecarPath, std::format("{} is refused", reserved));
                      }
                      image::ImagePackage unrelated = validPackage();
                      unrelated.sidecarPath         = "pixels.rgba";
                      checks.expect(unrelated.validate().has_value(), "an unrelated portable sidecar remains valid");
                      checks.raise();
                  })
            .Execute();
    }};
