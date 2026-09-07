/**
 * @file RenderedCheckTests.cpp
 * @brief The pinned `MEDUI-PROFILE-RENDERED` observation vectors, run against `mdux.verify`.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-014 What rendered-truth verification checks, and what it cannot
 * @compliance ADR-016 Locally versioned observation profiles
 *
 * `spec/profiles.md` rules R01-R04. Every `conformance/profiles` vector whose
 * `inputs.operation` is `rendered-check` is a portable observation - a golden rectangle, a measured
 * ink rectangle, RGB samples, or packed RGBA8 plus a baseline digest - with a required `outcome`.
 * The adapters below run each vector's public data through the exact predicates `goldenBounds()`,
 * `colorHash()` and `rawImageDigest()` use (`mdux::verify::rectContainedBy` / `inflate` /
 * `couldBeBlend` / `mdux::evidence::sha256`), so a MedUI amendment to a rule's meaning shows up
 * here rather than in a harness copy of the arithmetic.
 *
 * `medui-conformance.toml` claims `MEDUI-PROFILE-RENDERED`, so this must run every one of its
 * vectors: a missing corpus is a hard failure under `CI`, and a claimed rule with no vector fails
 * the coverage check.
 */

import std;
import speclab;
import mdux.core.units;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.medui.schema;
import mdux.tools.toml;
import mdux.verify;

#include "../framework/SpecLabBridge.hpp"
#include "../conformance/CorpusFixture.hpp"

namespace {

namespace json = mdux::evidence::json;
namespace mv   = mdux::verify;
using namespace mdux::conformance;
using mdux::core::ColorRgba8;
using mdux::medui::NodeRect;

// ---------------------------------------------------------------------------
// Vector field readers
// ---------------------------------------------------------------------------

[[nodiscard]] std::int64_t scalarInt(const json::Value& value, std::string_view what, const std::filesystem::path& path) {
    const auto number = value.asInt();
    if (!number) {
        fail(std::format("{}: {} is not an integer", path.generic_string(), what));
    }
    return *number;
}

[[nodiscard]] std::int64_t intAt(const json::Value& array, std::size_t index, const std::filesystem::path& path) {
    if (array.kind() != json::Value::Kind::Array || index >= array.elements().size()) {
        fail(std::format("{}: expected an integer at index {} of an array", path.generic_string(), index));
    }
    const auto number = array.elements()[index].asInt();
    if (!number) {
        fail(std::format("{}: element {} is not an integer", path.generic_string(), index));
    }
    return *number;
}

/// One 8-bit channel value, rejecting anything outside [0, 255] rather than silently wrapping it.
[[nodiscard]] std::uint8_t byteAt(const json::Value& array, std::size_t index, const std::filesystem::path& path) {
    const std::int64_t value = intAt(array, index, path);
    if (value < 0 || value > 255) {
        fail(std::format("{}: channel {} is {}, outside [0, 255]", path.generic_string(), index, value));
    }
    return static_cast<std::uint8_t>(value);
}

[[nodiscard]] NodeRect rectOf(const json::Value& array, const std::filesystem::path& path) {
    if (array.kind() != json::Value::Kind::Array || array.elements().size() != 4) {
        fail(std::format("{}: a rectangle is [x, y, width, height]", path.generic_string()));
    }
    return NodeRect{.x      = static_cast<std::int32_t>(intAt(array, 0, path)),
                    .y      = static_cast<std::int32_t>(intAt(array, 1, path)),
                    .width  = static_cast<std::int32_t>(intAt(array, 2, path)),
                    .height = static_cast<std::int32_t>(intAt(array, 3, path))};
}

[[nodiscard]] ColorRgba8 rgbOf(const json::Value& array, const std::filesystem::path& path) {
    if (array.kind() != json::Value::Kind::Array || array.elements().size() != 3) {
        fail(std::format("{}: an RGB8 sample is [r, g, b]", path.generic_string()));
    }
    return ColorRgba8{.r = byteAt(array, 0, path), .g = byteAt(array, 1, path), .b = byteAt(array, 2, path), .a = 255};
}

[[nodiscard]] bool isNull(const json::Value* value) {
    return value == nullptr || value->kind() == json::Value::Kind::Null;
}

/// Fieldwise JSON equality, independent of object-key order - E01's identity-comparison rule, which
/// R04 requires between `captureIdentity` and `baselineIdentity` before a digest is compared.
[[nodiscard]] bool jsonEqual(const json::Value& left, const json::Value& right) {
    if (left.kind() != right.kind()) {
        // Int vs UInt are the same number written two ways; nothing else crosses kinds.
        const auto asNumber = [](const json::Value& value) -> std::optional<std::int64_t> {
            if (const auto i = value.asInt()) {
                return *i;
            }
            if (const auto u = value.asUInt()) {
                return static_cast<std::int64_t>(*u);
            }
            return std::nullopt;
        };
        const auto l = asNumber(left);
        const auto r = asNumber(right);
        return l && r && *l == *r;
    }
    switch (left.kind()) {
        case json::Value::Kind::Null:
            return true;
        case json::Value::Kind::Bool:
            return left.asBool().value_or(false) == right.asBool().value_or(true);
        case json::Value::Kind::Int:
        case json::Value::Kind::UInt:
            return left.asInt().value_or(0) == right.asInt().value_or(1);
        case json::Value::Kind::Float32:
            return left.asFloat32().value_or(0.0F) == right.asFloat32().value_or(1.0F);
        case json::Value::Kind::String:
            return left.asString().value_or("") == right.asString().value_or("x");
        case json::Value::Kind::Array: {
            if (left.elements().size() != right.elements().size()) {
                return false;
            }
            for (std::size_t i = 0; i < left.elements().size(); ++i) {
                if (!jsonEqual(left.elements()[i], right.elements()[i])) {
                    return false;
                }
            }
            return true;
        }
        case json::Value::Kind::Object: {
            if (left.members().size() != right.members().size()) {
                return false;
            }
            for (const json::Member& entry : left.members()) {
                const json::Value* other = right.find(entry.key);
                if (other == nullptr || !jsonEqual(entry.value, *other)) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// The four rule adapters. Each returns "pass", "fail" or "missing-baseline".
// ---------------------------------------------------------------------------

[[nodiscard]] std::string extentEquality(const json::Value& inputs, const std::filesystem::path& path) {
    const json::Value* ink = inputs.find("ink");
    if (isNull(ink)) {
        return "fail";  // R01: empty ink fails.
    }
    return rectOf(*ink, path) == rectOf(member(inputs, "golden", path), path) ? "pass" : "fail";
}

[[nodiscard]] std::string inkContainment(const json::Value& inputs, const std::filesystem::path& path) {
    const json::Value* ink = inputs.find("ink");
    if (isNull(ink)) {
        return "pass";  // R02: empty ink passes.
    }
    const NodeRect     golden = rectOf(member(inputs, "golden", path), path);
    const std::int64_t margin = scalarInt(member(inputs, "margin", path), "margin", path);
    if (margin < 0 || margin > std::numeric_limits<std::int32_t>::max()) {
        fail(std::format("{}: R02 margin is a nonnegative integer, got {}", path.generic_string(), margin));
    }
    return mv::rectContainedBy(rectOf(*ink, path), mv::inflate(golden, static_cast<std::int32_t>(margin))) ? "pass" : "fail";
}

[[nodiscard]] std::string tintComposition(const json::Value& inputs, const std::filesystem::path& path) {
    const json::Value* tint = inputs.find("tint");
    if (isNull(tint)) {
        return "fail";  // R03: absent tint fails.
    }
    const ColorRgba8 tintColor  = rgbOf(*tint, path);
    const auto       composites = scalarInt(member(inputs, "composites", path), "composites", path);

    if (requireArray(inputs, "samples", path).empty()) {
        return "fail";  // R03: empty samples fail.
    }

    bool anySampleIsTheTint = false;
    for (const json::Value& sample : inputs.find("samples")->elements()) {
        const ColorRgba8 background = rgbOf(member(sample, "background", path), path);
        const ColorRgba8 observed   = rgbOf(member(sample, "rgb", path), path);
        if (!mv::couldBeBlend(observed, background, tintColor, composites)) {
            return "fail";
        }
        anySampleIsTheTint = anySampleIsTheTint || observed == tintColor;
    }
    return anySampleIsTheTint ? "pass" : "fail";
}

[[nodiscard]] std::string rgba8Sha256(const json::Value& inputs, const std::filesystem::path& path) {
    const NodeRect surface{.x      = 0,
                           .y      = 0,
                           .width  = static_cast<std::int32_t>(intAt(member(inputs, "surface", path), 0, path)),
                           .height = static_cast<std::int32_t>(intAt(member(inputs, "surface", path), 1, path))};
    const NodeRect golden = rectOf(member(inputs, "golden", path), path);

    // The capture must contain the whole rectangle; a degenerate rectangle is allowed and hashes
    // the empty byte string.
    if (golden.x < 0 || golden.y < 0 || golden.width < 0 || golden.height < 0
        || golden.x + golden.width > surface.width || golden.y + golden.height > surface.height) {
        return "fail";
    }

    const json::Value& rgba8 = member(inputs, "rgba8", path);
    if (rgba8.kind() != json::Value::Kind::Array
        || rgba8.elements().size() != static_cast<std::size_t>(surface.width) * static_cast<std::size_t>(surface.height) * 4) {
        fail(std::format("{}: rgba8 must be width*height*4 bytes", path.generic_string()));
    }

    std::vector<std::byte> roi;
    roi.reserve(static_cast<std::size_t>(golden.width) * static_cast<std::size_t>(golden.height) * 4);
    for (std::int32_t row = golden.y; row < golden.y + golden.height; ++row) {
        for (std::int32_t col = golden.x; col < golden.x + golden.width; ++col) {
            const std::size_t base = (static_cast<std::size_t>(row) * static_cast<std::size_t>(surface.width) + static_cast<std::size_t>(col)) * 4;
            for (std::size_t channel = 0; channel < 4; ++channel) {
                roi.push_back(static_cast<std::byte>(byteAt(rgba8, base + channel, path)));
            }
        }
    }

    const json::Value* baseline = inputs.find("baseline");
    if (isNull(baseline)) {
        return "missing-baseline";  // R04: missing baseline, never pass.
    }

    // Profile/backend/configuration identity must match before comparison.
    if (!jsonEqual(member(inputs, "captureIdentity", path),
                   member(inputs, "baselineIdentity", path))) {
        return "fail";
    }

    const std::array<char, 64> hex      = mdux::evidence::toHex(mdux::evidence::sha256(roi));
    const auto                 expected = baseline->asString();
    if (!expected) {
        fail(std::format("{}: baseline must be a hex string or null", path.generic_string()));
    }
    return std::string_view{hex.data(), hex.size()} == *expected ? "pass" : "fail";
}

[[nodiscard]] std::string runRenderedCheck(const std::string& check, const json::Value& inputs, const std::filesystem::path& path) {
    if (check == "extent-equality") {
        return extentEquality(inputs, path);
    }
    if (check == "ink-containment") {
        return inkContainment(inputs, path);
    }
    if (check == "tint-composition") {
        return tintComposition(inputs, path);
    }
    if (check == "rgba8-sha256") {
        return rgba8Sha256(inputs, path);
    }
    fail(std::format("{}: unknown rendered-check '{}'", path.generic_string(), check));
}

// ---------------------------------------------------------------------------
// The scenario
// ---------------------------------------------------------------------------

const mdux::spec::Register renderedProfileVectors{
    "Every MEDUI-PROFILE-RENDERED vector holds against mdux.verify's own arithmetic",
    "conformance",
    [] {
        return speclab::Test("medui-profile-rendered-vectors")
            .Given("the rendered-check observation vectors in the pinned MedUI checkout", [] {})
            .When("each is run through the predicate the production check uses", [] {})
            .Then("the outcome equals the vector's expectation, and every claimed rule ran",
                  [] {
                      mdux::spec::Checks checks;

                      const std::optional<std::filesystem::path> root = corpusRootOrSkip(checks);
                      if (!root) {
                          checks.raise();
                          return;
                      }

                      const Manifest pinned = manifest();
                      checks.expect(checkoutRevision(*root) == pinned.commit,
                                    "the checkout is at the revision medui-conformance.toml pins");
                      checks.expect(std::ranges::find(pinned.profiles, "MEDUI-PROFILE-RENDERED") != pinned.profiles.end(),
                                    "medui-conformance.toml claims MEDUI-PROFILE-RENDERED");

                      const std::filesystem::path vectorsDir = *root / "conformance" / "profiles";
                      if (!std::filesystem::is_directory(vectorsDir)) {
                          checks.expect(false, "the pinned checkout has a conformance/profiles/ directory");
                          checks.raise();
                          return;
                      }

                      std::vector<std::filesystem::path> paths;
                      for (const auto& entry : std::filesystem::directory_iterator{vectorsDir}) {
                          if (entry.is_regular_file() && entry.path().extension() == ".json") {
                              paths.push_back(entry.path());
                          }
                      }
                      std::ranges::sort(paths);

                      std::set<std::string> rulesExercised;
                      std::size_t           renderedVectors = 0;

                      for (const std::filesystem::path& path : paths) {
                          const auto document = json::parse(readFile(path));
                          if (!document) {
                              checks.expect(false, std::format("{} is valid JSON", path.generic_string()));
                              continue;
                          }
                          const json::Value& inputs = member(*document, "inputs", path);
                          const std::string  operation = requireString(inputs, "operation", path);
                          if (operation != "rendered-check") {
                              continue;
                          }
                          ++renderedVectors;

                          const json::Value& profile = member(*document, "profile", path);
                          checks.expect(requireString(profile, "id", path) == "MEDUI-PROFILE-RENDERED",
                                        std::format("{} is a MEDUI-PROFILE-RENDERED vector", path.filename().string()));

                          for (const json::Value& rule : requireArray(*document, "rules", path)) {
                              rulesExercised.insert(std::string{rule.asString().value_or("?")});
                          }

                          const std::string check    = requireString(inputs, "check", path);
                          const std::string got      = runRenderedCheck(check, inputs, path);
                          const std::string expected = requireString(
                              member(*document, "expected", path), "outcome", path);
                          checks.expect(got == expected,
                                        std::format("{} ({}): expected {}, adapter said {}", path.filename().string(), check, expected, got));
                      }

                      checks.expect(renderedVectors == 33,
                                    std::format("all 33 pinned rendered-check vectors ran, saw {}", renderedVectors));
                      for (std::string_view rule : {"R01", "R02", "R03", "R04"}) {
                          checks.expect(rulesExercised.contains(std::string{rule}),
                                        std::format("rule {} is backed by at least one vector that ran", rule));
                      }

                      std::cerr << std::format("MedUI RENDERED profile: {} vector(s) at {}\n", renderedVectors, pinned.commit);
                      checks.raise();
                  })
            .Execute();
    }};

// ---------------------------------------------------------------------------
// Failure-mode fixtures: the gate must fail when it should
// ---------------------------------------------------------------------------

const mdux::spec::Register renderedGateCatchesAWrongOutcome{
    "The rendered gate fails a vector whose adapter disagrees with the stated outcome",
    "conformance",
    [] {
        return speclab::Test("medui-profile-rendered-negative-outcome")
            .Given("committed vectors whose expected.outcome is deliberately wrong", [] {})
            .When("each is run through the same adapter the real gate uses", [] {})
            .Then("the adapter's answer differs from the fixture's, so the gate would fail",
                  [] {
                      mdux::spec::Checks           checks;
                      const std::filesystem::path directory =
                          std::filesystem::path{MDUX_REPO_ROOT} / "tests" / "conformance" / "fixtures" / "rendered-wrong-outcome";
                      checks.expect(std::filesystem::is_directory(directory), "the negative fixture tree is committed");

                      std::vector<std::filesystem::path> paths;
                      for (const auto& entry : std::filesystem::directory_iterator{directory}) {
                          if (entry.is_regular_file() && entry.path().extension() == ".json") {
                              paths.push_back(entry.path());
                          }
                      }
                      std::ranges::sort(paths);
                      checks.expect(!paths.empty(), "there is at least one negative fixture");

                      for (const std::filesystem::path& path : paths) {
                          const auto document = json::parse(readFile(path));
                          if (!document) {
                              checks.expect(false, std::format("{} is valid JSON", path.generic_string()));
                              continue;
                          }
                          const json::Value& inputs = member(*document, "inputs", path);
                          const std::string  check  = requireString(inputs, "check", path);
                          const std::string  got    = runRenderedCheck(check, inputs, path);
                          const std::string  stated = requireString(member(*document, "expected", path), "outcome", path);
                          checks.expect(got != stated,
                                        std::format("{}: adapter says '{}', which differs from the wrong stated '{}'",
                                                    path.filename().string(), got, stated));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register anUnadaptedProfileCannotBeClaimed{
    "A profile with no adapter here is caught before it can be claimed",
    "conformance",
    [] {
        return speclab::Test("medui-profile-runnable-guard")
            .Given("the closed profile set and the profiles this suite has adapters for", [] {})
            .When("an EVIDENCE claim is checked against the runnable set", [] {})
            .Then("it is a known contract profile but not one this suite can substantiate",
                  [] {
                      mdux::spec::Checks checks;
                      checks.expect(std::ranges::find(knownProfileIds, "MEDUI-PROFILE-EVIDENCE") != knownProfileIds.end(),
                                    "EVIDENCE is a profile the contract defines");
                      checks.expect(std::ranges::find(runnableProfiles, "MEDUI-PROFILE-EVIDENCE") == runnableProfiles.end(),
                                    "but conformance_spec has no EVIDENCE adapter, so claiming it would fail the runnable check (#314c)");
                      checks.expect(std::ranges::find(runnableProfiles, "MEDUI-PROFILE-RENDERED") != runnableProfiles.end(),
                                    "RENDERED is runnable, which is why medui-conformance.toml may claim it");
                      // Every runnable profile must be a real contract profile.
                      for (std::string_view id : runnableProfiles) {
                          checks.expect(std::ranges::find(knownProfileIds, id) != knownProfileIds.end(),
                                        std::format("'{}' is in the contract's profile set", id));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
