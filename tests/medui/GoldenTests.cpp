/**
 * @file GoldenTests.cpp
 * @brief BDD scenarios for golden references on safety-critical nodes (issue #196).
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * The predicate is the whole subject here: which nodes are selected, that a node matching both
 * rules is selected exactly once, and that what a golden pins for dynamic content is where it
 * appears rather than what it says. `accepted-goldens.medui` carries all of that in one screen an
 * author could open, which is why it is a fixture rather than a string literal - #200's
 * `mdux-medui-check` will be pointed at the same corpus.
 */

import std;
import speclab;
import mdux.text.schema;
import mdux.tools.cli;
import mdux.tools.medui.ast;
import mdux.tools.medui.diagnostics;
import mdux.tools.medui.goldens;
import mdux.tools.medui.layout;
import mdux.tools.medui.parser;
import mdux.tools.medui.semantic;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace md  = mdux::tools::medui;
namespace cli = mdux::tools::cli;

/// Reads one real authoring fixture from the repository corpus.
[[nodiscard]] std::string fixture(std::string_view name) {
    const std::filesystem::path path = std::filesystem::path{MDUX_REPO_ROOT} / "tests" / "medui" / "fixtures" / name;
    std::ifstream               in{path, std::ios::binary};
    if (!in) {
        throw speclab::core::AssertionFailure(std::format("fixture {} could not be opened at {}", name, path.generic_string()),
                                              std::source_location::current());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

[[nodiscard]] md::ast::Screen parseOrFail(std::string_view source) {
    md::ParseResult parsed = md::parse(source, "goldens.medui");
    if (!parsed.screen || !parsed.diagnostics.empty()) {
        throw speclab::core::AssertionFailure("golden test source did not parse", std::source_location::current());
    }
    return std::move(*parsed.screen);
}

/// Parses and resolves, failing the scenario rather than the assertion if the screen is malformed.
[[nodiscard]] md::LayoutResult layoutOf(std::string_view source, std::int64_t width, std::int64_t height) {
    const md::ast::Screen screen   = parseOrFail(source);
    md::LayoutResult      resolved = md::resolveLayout(screen, "goldens.medui", {.surfaceWidth = width, .surfaceHeight = height});
    if (!resolved.ok()) {
        throw speclab::core::AssertionFailure("golden test source did not resolve", std::source_location::current());
    }
    return resolved;
}

/// Wraps a component body in a screen whose declared surface matches the build surface.
[[nodiscard]] std::string screenWith(std::string_view body) {
    return std::format("Screen Test {{\n"
                       "    layout: Vertical {{ spacing: 0px; padding: 0px; }}\n"
                       "    surface: 200px, 100px;\n"
                       "{}"
                       "}}\n",
                       body);
}

/// One annotated Button, with the annotation argument the scenario is about.
[[nodiscard]] std::string annotatedButton(std::string_view annotation) {
    return screenWith(std::format("    {}\n"
                                  "    Button {{ id: action; width: 100px; height: 40px; label: t(\"STR-ACTION\"); "
                                  "color: Theme.Colors.PrimaryAction; source: \"ACTION\"; requirement: \"REQ-1\"; }}\n",
                                  annotation));
}

/// Spelled as a predicate rather than as a projected `find`: the projection yields `std::string`
/// and the needle is a `string_view`, and the range concepts do not owe us that comparison.
[[nodiscard]] const md::GoldenReference* find(std::span<const md::GoldenReference> references, std::string_view nodeId) {
    const auto found = std::ranges::find_if(references, [nodeId](const md::GoldenReference& reference) {
        return reference.nodeId == nodeId;
    });
    return found == references.end() ? nullptr : &*found;
}

/// How many entries name `nodeId`. One is the whole point for a node matching both rules.
[[nodiscard]] std::ptrdiff_t countFor(std::span<const md::GoldenReference> references, std::string_view nodeId) {
    return std::ranges::count_if(references, [nodeId](const md::GoldenReference& reference) {
        return reference.nodeId == nodeId;
    });
}

[[nodiscard]] const cli::Diagnostic* find(const md::SafetyResult& result, md::Code code) {
    const std::string_view wanted = md::id(code);
    const auto             found  = std::ranges::find_if(result.diagnostics, [wanted](const cli::Diagnostic& diagnostic) {
        return diagnostic.code == wanted;
    });
    return found == result.diagnostics.end() ? nullptr : &*found;
}

/// Renders a check list as `Bounds+ColorHash`, so a failure says what was emitted.
[[nodiscard]] std::string describe(std::span<const md::CvCheck> checks) {
    std::string rendered;
    for (const md::CvCheck check : checks) {
        if (!rendered.empty()) {
            rendered += '+';
        }
        rendered += md::spell(check);
    }
    return rendered.empty() ? std::string{"<none>"} : rendered;
}

}  // namespace

const mdux::spec::Register predicateSelectsTheRightNodes{
    "The predicate selects annotated and positioned nodes, and nothing else",
    "evidence-unit",
    [] {
        return speclab::Test("medui-goldens-predicate")
            .Given("a screen with an annotated node, a positioned node, one of each, and a plain one", [] {})
            .When("the golden set is derived from the resolved screen", [] {})
            .Then("four entries appear in source order and the plain node is absent",
                  [] {
                      mdux::spec::Checks checks;
                      const auto         references = md::collectGoldens(layoutOf(fixture("accepted-goldens.medui"), 400, 300));

                      const std::vector<std::string_view> expected{"action", "pinned", "score", "state"};
                      checks.expect(references.size() == expected.size(), std::format("four nodes are pinned, got {}", references.size()));
                      for (std::size_t index = 0; index < std::min(references.size(), expected.size()); ++index) {
                          checks.expect(references[index].nodeId == expected[index],
                                        std::format("entry {} is '{}', got '{}'", index, expected[index], references[index].nodeId));
                      }
                      checks.expect(find(references, "plain") == nullptr, "a node with neither an annotation nor a position is not pinned");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register positionAlonePinsBounds{
    "A declared position pins bounds without an annotation, and an annotation pins what it names",
    "evidence-unit",
    [] {
        return speclab::Test("medui-goldens-position-and-annotation")
            .Given("a positioned Label and an annotated Button naming only ColorHash", [] {})
            .When("their entries are read", [] {})
            .Then("the position yields Bounds and the annotation yields exactly what it asked for",
                  [] {
                      mdux::spec::Checks checks;
                      const auto         references = md::collectGoldens(layoutOf(fixture("accepted-goldens.medui"), 400, 300));

                      const md::GoldenReference* pinned = find(references, "pinned");
                      checks.expect(pinned != nullptr, "the positioned node is pinned");
                      if (pinned != nullptr) {
                          checks.expect(pinned->cvChecks == std::vector{md::CvCheck::Bounds},
                                        std::format("a declared position pins Bounds, got {}", describe(pinned->cvChecks)));
                          checks.expect(pinned->bounds == md::LayoutRect{250, 0, 100, 40}, "the pinned rectangle is the resolved one");
                          checks.expect(pinned->textKey == "STR-PINNED" && pinned->colorToken == "Theme.Colors.Title",
                                        "a static Label pins its key and its tint");
                      }

                      const md::GoldenReference* action = find(references, "action");
                      checks.expect(action != nullptr, "the annotated node is pinned");
                      if (action != nullptr) {
                          checks.expect(action->cvChecks == std::vector{md::CvCheck::ColorHash},
                                        std::format("an unpositioned annotation pins only what it names, got {}", describe(action->cvChecks)));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register bothRulesMergeIntoOneEntry{
    "A node matching both rules is pinned exactly once, with the checks merged",
    "evidence-unit",
    [] {
        return speclab::Test("medui-goldens-merged-entry")
            .Given("a NumericDisplay that is both @safety_critical(Bounds, ColorHash) and positioned", [] {})
            .When("the golden set is derived", [] {})
            .Then("there is one entry for it, with Bounds present once",
                  [] {
                      mdux::spec::Checks checks;
                      const auto         references = md::collectGoldens(layoutOf(fixture("accepted-goldens.medui"), 400, 300));

                      const std::ptrdiff_t count = countFor(references, "score");
                      checks.expect(count == 1, std::format("one entry, never two, got {}", count));

                      const md::GoldenReference* score = find(references, "score");
                      if (score != nullptr) {
                          checks.expect(score->cvChecks == std::vector{md::CvCheck::Bounds, md::CvCheck::ColorHash},
                                        std::format("the automatic Bounds merges with the annotation, got {}", describe(score->cvChecks)));
                          checks.expect(score->bounds == md::LayoutRect{250, 60, 120, 60}, "the merged entry keeps the resolved rectangle");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register dynamicContentPinsPlaceNotValue{
    "Dynamic content pins where it appears and in what tint, never the value that varies",
    "evidence-unit",
    [] {
        return speclab::Test("medui-goldens-dynamic-content")
            .Given("a NumericDisplay with a colour, and a StatusIndicator whose states and colours are lists", [] {})
            .When("their entries are read", [] {})
            .Then("neither pins a text key, and the one whose tint varies pins no colour either",
                  [] {
                      mdux::spec::Checks checks;
                      const auto         references = md::collectGoldens(layoutOf(fixture("accepted-goldens.medui"), 400, 300));

                      const md::GoldenReference* score = find(references, "score");
                      if (score != nullptr) {
                          checks.expect(score->textKey.empty(), "a numeric display pins no text");
                          checks.expect(score->colorToken == "Theme.Colors.ScoreDigits", "its single declared tint is pinned");
                      }

                      const md::GoldenReference* state = find(references, "state");
                      checks.expect(state != nullptr, "the positioned status indicator is pinned");
                      if (state != nullptr) {
                          checks.expect(state->textKey.empty(), "the state on screen is the varying part, so no key is pinned");
                          checks.expect(state->colorToken.empty(), "its tint varies per state, so no colour is pinned");
                          checks.expect(state->cvChecks == std::vector{md::CvCheck::Bounds}, "what remains checkable is where it appears");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register checksAreSortedAndDeduplicated{
    "Check lists are sorted and deduplicated, so one screen has one serialisation",
    "evidence-unit",
    [] {
        return speclab::Test("medui-goldens-canonical-checks")
            .Given("an annotation naming ColorHash before Bounds, and Bounds twice", [] {})
            .When("the entry is derived", [] {})
            .Then("the list reads Bounds, ColorHash exactly once each",
                  [] {
                      mdux::spec::Checks checks;
                      const auto         references = md::collectGoldens(
                          layoutOf(annotatedButton("@safety_critical(cv_check: [ColorHash, Bounds, Bounds])"), 200, 100));

                      checks.expect(references.size() == 1, "the annotated node is pinned");
                      if (references.size() == 1) {
                          checks.expect(references.front().cvChecks == std::vector{md::CvCheck::Bounds, md::CvCheck::ColorHash},
                                        std::format("checks are canonical, got {}", describe(references.front().cvChecks)));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register annotationWithoutChecksStillPinsBounds{
    "An annotation naming no check still pins bounds rather than emitting an empty golden",
    "evidence-unit",
    [] {
        return speclab::Test("medui-goldens-annotation-without-checks")
            .Given("@safety_critical with no cv_check argument", [] {})
            .When("the entry is derived", [] {})
            .Then("it pins Bounds, read from the rule that a declared position alone is enough",
                  [] {
                      mdux::spec::Checks checks;
                      const auto         references = md::collectGoldens(layoutOf(annotatedButton("@safety_critical"), 200, 100));

                      checks.expect(references.size() == 1, "the annotated node is pinned");
                      if (references.size() == 1) {
                          checks.expect(references.front().cvChecks == std::vector{md::CvCheck::Bounds},
                                        std::format("an entry a verifier can act on, got {}", describe(references.front().cvChecks)));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register annotatedContainerIsRejected{
    "A component the dictionary gives no requirement cannot be safety-critical",
    "evidence-unit",
    [] {
        return speclab::Test("medui-goldens-annotated-row")
            .Given("a Row carrying @safety_critical - Label, Clock and Image are in the same position", [] {})
            .When("the annotation rules are validated", [] {})
            .Then("MEDUI-E070 explains that the component takes no requirement at all",
                  [] {
                      mdux::spec::Checks checks;

                      // The pinned component model gives Row only id, height, spacing and
                      // background. It can therefore never carry a requirement:, so it can never
                      // satisfy the annotation rule - which makes an annotated Row a screen that
                      // does not compile rather than a golden over a container. An earlier revision
                      // of this suite pinned the opposite behaviour by reaching collectGoldens()
                      // without validating first; that path is unreachable in a real driver.
                      //
                      // Row is the clearest case rather than the only one: Label, Clock, Image,
                      // SignalTrace and VulkanViewport take no requirement either, so the same
                      // diagnostic answers an annotation on any of them.
                      const std::string source = screenWith(
                          "    @safety_critical(cv_check: [Bounds])\n"
                          "    Row { id: topbar; height: 40px; background: Theme.Colors.Title;\n"
                          "        Label { id: title; width: Fill; height: 40px; text: t(\"STR-TITLE\"); color: Theme.Colors.Title; }\n"
                          "    }\n");
                      const md::ast::Screen  screen = parseOrFail(source);
                      const md::SafetyResult result = md::validateSafetyAnnotations(screen, "goldens.medui");

                      const cli::Diagnostic* reported = find(result, md::Code::SafetyCriticalWithoutRequirement);
                      checks.expect(!result.ok(), "the screen is rejected");
                      checks.expect(reported != nullptr, "MEDUI-E070 is reported");
                      checks.expect(reported != nullptr && reported->message.find("takes no requirement") != std::string::npos,
                                    "the diagnostic names the real problem rather than asking for a field Row cannot have");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register onlyAuthoredNodesArePinned{"A Row's synthetic background is not a golden of its own", "evidence-unit", [] {
                                                          return speclab::Test("medui-goldens-synthetic-nodes")
                                                              .Given("a Row with a background and one positioned child", [] {})
                                                              .When("the golden set is derived", [] {})
                                                              .Then("the child is pinned and the synthetic background is not",
                                                                    [] {
                                                                        mdux::spec::Checks checks;
                                                                        const std::string  source = screenWith(
                                                                            "    Row { id: topbar; height: 40px; background: Theme.Colors.Title;\n"
                                                                             "        Label { id: title; width: 60px; height: 20px; position: 10px, 10px; "
                                                                             "text: t(\"STR-TITLE\"); color: Theme.Colors.Title; }\n"
                                                                             "    }\n");
                                                                        const auto references = md::collectGoldens(layoutOf(source, 200, 100));

                                                                        checks.expect(find(references, "topbar-background") == nullptr,
                                                                                      "a synthetic node describes nothing an author wrote");
                                                                        checks.expect(find(references, "title") != nullptr, "the positioned child is pinned");
                                                                        checks.expect(references.size() == 1,
                                                                                      std::format("exactly one entry, got {}", references.size()));
                                                                        checks.raise();
                                                                    })
                                                              .Execute();
                                                      }};

const mdux::spec::Register blankRequirementIsNotTraceable{
    "An empty requirement is no requirement, through the full analyze then validate order",
    "evidence-unit",
    [] {
        return speclab::Test("medui-goldens-blank-requirement")
            .Given("a @safety_critical Button whose requirement is the empty string", [] {})
            .When("semantic analysis accepts it and the annotation rules run", [] {})
            .Then("MEDUI-E070 rejects it anyway, because there is nothing to trace",
                  [] {
                      mdux::spec::Checks    checks;
                      const md::ast::Screen screen = parseOrFail(screenWith("    @safety_critical(cv_check: [Bounds])\n"
                                                                            "    Button { id: action; width: 100px; height: 40px; label: t(\"STR-ACTION\"); "
                                                                            "color: Theme.Colors.PrimaryAction; source: \"ACTION\"; requirement: \"\"; }\n"));

                      // The gap this covers is that `requirement:` is a String field and the
                      // semantic domain accepts every quoted string, `""` included - so the
                      // analyze step below really does pass, and the safety rule is the only thing
                      // standing between an empty requirement and a golden.
                      const std::array<std::string_view, 1> themeTokens{"Theme.Colors.PrimaryAction"};
                      mdux::text::TextPackage               package;
                      package.header.id   = "goldens-en-US";
                      package.atlasId     = "goldens-atlas";
                      package.locale      = "en-US";
                      package.sidecarPath = "runs.bin";
                      package.runs.push_back(mdux::text::TextRun{.id = "STR-ACTION", .byteOffset = 0, .byteLength = 0, .sha256 = {}});
                      const std::array textPackages{package};

                      const md::SemanticResult semantic = md::analyze(screen,
                                                                      "goldens.medui",
                                                                      md::SemanticInputs{.themeTokens = themeTokens, .textPackages = textPackages});
                      checks.expect(semantic.ok(),
                                    std::format("semantic analysis accepts the empty requirement, got {} diagnostics", semantic.diagnostics.size()));

                      const md::SafetyResult safety   = md::validateSafetyAnnotations(screen, "goldens.medui");
                      const cli::Diagnostic* reported = find(safety, md::Code::SafetyCriticalWithoutRequirement);
                      checks.expect(!safety.ok(), "the safety rule rejects it");
                      checks.expect(reported != nullptr && reported->message.find("empty") != std::string::npos,
                                    "the diagnostic says the requirement is empty rather than absent");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register colorHashNeedsATintToCompare{
    "ColorHash is refused where no single declared tint exists",
    "evidence-unit",
    [] {
        return speclab::Test("medui-goldens-colorhash-needs-a-tint")
            .Given("an annotated StatusIndicator whose colors: is a list, and a Clock with no colour", [] {})
            .When("each annotation is validated", [] {})
            .Then("MEDUI-E071 refuses the check rather than pinning one with nothing to compare against",
                  [] {
                      mdux::spec::Checks checks;

                      const md::ast::Screen indicator = parseOrFail(
                          screenWith("    @safety_critical(cv_check: [Bounds, ColorHash])\n"
                                     "    StatusIndicator { id: state; width: 120px; height: 40px; requirement: \"REQ-1\"; "
                                     "source: \"STATE\"; states: [t(\"STR-OK\"), t(\"STR-ALARM\")]; "
                                     "colors: [Theme.Colors.Title, Theme.Colors.ScoreDigits]; }\n"));
                      const md::SafetyResult indicatorResult = md::validateSafetyAnnotations(indicator, "goldens.medui");
                      const cli::Diagnostic* refused         = find(indicatorResult, md::Code::UnknownCvCheck);
                      checks.expect(!indicatorResult.ok(), "a per-state tint is not one a golden can pin");
                      checks.expect(refused != nullptr && refused->message.find("ColorHash") != std::string::npos, "the diagnostic names the check it refuses");

                      const md::ast::Screen clock = parseOrFail(screenWith("    @safety_critical(cv_check: [ColorHash])\n"
                                                                           "    Clock { id: now; width: 100px; height: 40px; format: HH_MM; }\n"));
                      checks.expect(find(md::validateSafetyAnnotations(clock, "goldens.medui"), md::Code::UnknownCvCheck) != nullptr,
                                    "a component with no colour field cannot be tint-checked either");

                      // The control: a single declared token is exactly what ColorHash needs, and
                      // the accepted fixture leans on that for both `action` and `score`.
                      const md::ast::Screen  button       = parseOrFail(annotatedButton("@safety_critical(cv_check: [ColorHash])"));
                      const md::SafetyResult buttonResult = md::validateSafetyAnnotations(button, "goldens.medui");
                      checks.expect(buttonResult.ok(), std::format("one declared tint is enough, got {} diagnostics", buttonResult.diagnostics.size()));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register safetyCriticalNeedsARequirement{
    "A @safety_critical node with no requirement is MEDUI-E070, reported at the annotation",
    "evidence-unit",
    [] {
        return speclab::Test("medui-goldens-missing-requirement")
            .Given("the rejected-safety-without-requirement fixture", [] {})
            .When("the annotation rules are validated", [] {})
            .Then("MEDUI-E070 names the component and points at the '@'",
                  [] {
                      mdux::spec::Checks     checks;
                      const md::ast::Screen  screen = parseOrFail(fixture("rejected-safety-without-requirement.medui"));
                      const md::SafetyResult result = md::validateSafetyAnnotations(screen, "rejected-safety-without-requirement.medui");

                      const cli::Diagnostic* reported = find(result, md::Code::SafetyCriticalWithoutRequirement);
                      checks.expect(!result.ok(), "the screen is rejected");
                      checks.expect(reported != nullptr, "MEDUI-E070 is reported");
                      if (reported != nullptr) {
                          checks.expect(reported->line == 11 && reported->column == 5,
                                        std::format("the diagnostic points at the annotation, got {}:{}", reported->line, reported->column));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register acceptedFixturePassesValidation{
    "Every annotation in the accepted fixture satisfies the rules",
    "evidence-unit",
    [] {
        return speclab::Test("medui-goldens-accepted-validates")
            .Given("the accepted-goldens fixture", [] {})
            .When("the annotation rules are validated", [] {})
            .Then("nothing is reported, so the golden scenarios measure a legal screen",
                  [] {
                      mdux::spec::Checks     checks;
                      const md::ast::Screen  screen = parseOrFail(fixture("accepted-goldens.medui"));
                      const md::SafetyResult result = md::validateSafetyAnnotations(screen, "accepted-goldens.medui");
                      checks.expect(result.ok(), std::format("the fixture validates, got {} diagnostics", result.diagnostics.size()));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register unknownCvCheckIsRejected{
    "A cv_check outside the closed set is MEDUI-E071, and a malformed one is MEDUI-E033",
    "evidence-unit",
    [] {
        return speclab::Test("medui-goldens-unknown-cv-check")
            .Given("cv_check: [Bounds, Wobble] and cv_check: [\"Bounds\"]", [] {})
            .When("each is validated", [] {})
            .Then("the unknown name and the wrong value form are reported separately",
                  [] {
                      mdux::spec::Checks checks;

                      const md::ast::Screen  unknown       = parseOrFail(annotatedButton("@safety_critical(cv_check: [Bounds, Wobble])"));
                      const md::SafetyResult unknownResult = md::validateSafetyAnnotations(unknown, "goldens.medui");
                      const cli::Diagnostic* reported      = find(unknownResult, md::Code::UnknownCvCheck);
                      checks.expect(reported != nullptr, "MEDUI-E071 is reported");
                      checks.expect(reported != nullptr && reported->message.find("Wobble") != std::string::npos,
                                    "the diagnostic names the check that does not exist");

                      const md::ast::Screen  malformed       = parseOrFail(annotatedButton("@safety_critical(cv_check: [\"Bounds\"])"));
                      const md::SafetyResult malformedResult = md::validateSafetyAnnotations(malformed, "goldens.medui");
                      checks.expect(find(malformedResult, md::Code::FieldValueKind) != nullptr, "a quoted check is the wrong value form, not an unknown name");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register collectionAssumesValidation{
    "Deriving goldens from an unvalidated screen throws rather than emitting one nobody can verify",
    "evidence-unit",
    [] {
        return speclab::Test("medui-goldens-gate-throws")
            .Given("a screen whose cv_check names a verification this compiler does not emit", [] {})
            .When("the golden set is derived without validating first", [] {})
            .Then("it is std::logic_error, not a golden with a check the verifier cannot run",
                  [] {
                      mdux::spec::Checks     checks;
                      const md::LayoutResult resolved = layoutOf(annotatedButton("@safety_critical(cv_check: [Wobble])"), 200, 100);

                      bool threw = false;
                      try {
                          [[maybe_unused]] const auto ignored = md::collectGoldens(resolved);
                      } catch (const std::logic_error&) {
                          threw = true;
                      }
                      checks.expect(threw, "the validation gate is assumed, and its absence is a programming error");
                      checks.raise();
                  })
            .Execute();
    }};
