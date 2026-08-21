/**
 * @brief BDD scenarios for the compiled-screen document and its canonical JSON (issue #197).
 * @file PackageTests.cpp
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine (canonical form, byte-identity)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * Two properties carry most of these scenarios. The first is that the writer and the reader agree:
 * they are separate code with a member name written on each side, so the round trip runs over a
 * fixture carrying all eleven payloads and executes both directions rather than asserting one.
 * The second is that the bytes are exactly determined - one scenario spells a whole small package
 * out, because a byte-compared artifact's format is worth stating in a form a reviewer can read.
 */

import std;
import speclab;
import mdux.draw;
import mdux.medui.schema;
import mdux.tools.cli;
import mdux.tools.medui.ast;
import mdux.tools.medui.goldens;
import mdux.tools.medui.layout;
import mdux.tools.medui.package;
import mdux.tools.medui.parser;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace md  = mdux::tools::medui;
namespace ms  = mdux::medui;
namespace cli = mdux::tools::cli;

/// The budget every scenario compiles with. Declared rather than computed, as `PackageInputs` says.
constexpr mdux::draw::DrawBudget testBudget{.maxVertices = 4096, .maxIndices = 6144, .maxCommands = 256};

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

[[nodiscard]] md::LayoutResult layoutOf(std::string_view source, std::int64_t width, std::int64_t height) {
    md::ParseResult parsed = md::parse(source, "package.medui");
    if (!parsed.screen || !parsed.diagnostics.empty()) {
        throw speclab::core::AssertionFailure("package test source did not parse", std::source_location::current());
    }
    md::LayoutResult resolved = md::resolveLayout(*parsed.screen, "package.medui", {.surfaceWidth = width, .surfaceHeight = height});
    if (!resolved.ok()) {
        throw speclab::core::AssertionFailure(std::format("package test source did not resolve: {}", resolved.diagnostics.front().message),
                                              std::source_location::current());
    }
    return resolved;
}

/// The screen carrying all eleven payloads, compiled.
[[nodiscard]] md::ScreenDocument everyComponent() {
    return md::buildPackage(layoutOf(fixture("accepted-every-component.medui"), 800, 700), {.id = "every-component", .budget = testBudget});
}

/// One Label on a small surface: the screen the byte-exact scenario spells out.
[[nodiscard]] std::string tinyScreen() {
    return "Screen Tiny {\n"
           "    layout: Vertical { spacing: 0px; padding: 0px; }\n"
           "    surface: 200px, 100px;\n"
           "\n"
           "    Label {\n"
           "        id: title;\n"
           "        width: 200px;\n"
           "        height: 40px;\n"
           "        text: t(\"STR-TITLE\");\n"
           "        color: Theme.Colors.Title;\n"
           "    }\n"
           "}\n";
}

[[nodiscard]] md::ScreenDocument tinyDocument() {
    return md::buildPackage(layoutOf(tinyScreen(), 200, 100), {.id = "tiny", .budget = testBudget});
}

[[nodiscard]] const ms::CompiledNode& node(const ms::ScreenPackage& package, std::string_view id) {
    const ms::CompiledNode* found = package.find(id);
    if (found == nullptr) {
        throw speclab::core::AssertionFailure(std::format("the compiled screen has no node '{}'", id), std::source_location::current());
    }
    return *found;
}

/// Replaces the first occurrence of `what`, failing the scenario if the bytes do not contain it -
/// an edit that silently did nothing would make a rejection scenario pass for the wrong reason.
[[nodiscard]] std::string editing(std::string text, std::string_view what, std::string_view with) {
    const std::size_t at = text.find(what);
    if (at == std::string::npos) {
        throw speclab::core::AssertionFailure(std::format("the package bytes do not contain '{}'", what), std::source_location::current());
    }
    return text.replace(at, what.size(), with);
}

[[nodiscard]] std::string firstCode(const md::PackageReadResult& result) {
    return result.diagnostics.empty() ? std::string{"<none>"} : result.diagnostics.front().code;
}

}  // namespace

const mdux::spec::Register everyComponentCompilesToItsOwnPayload{
    "Every component in the dictionary compiles to its own typed payload",
    "evidence-unit",
    [] {
        return speclab::Test("medui-package-payloads")
            .Given("a screen carrying all ten authored components and a Row with a background", [] {})
            .When("it is compiled to a screen package", [] {})
            .Then("eleven nodes appear, each naming the component it came from",
                  [] {
                      mdux::spec::Checks       checks;
                      const md::ScreenDocument document = everyComponent();
                      const ms::ScreenPackage  package  = document.package();

                      const std::vector<std::pair<std::string_view, std::string_view>> expected{
                          {"topbar-background",           "Panel"},
                          {            "title",           "Label"},
                          {       "wall-clock",           "Clock"},
                          {             "logo",           "Image"},
                          {        "endoscope",  "VulkanViewport"},
                          {              "ecg",     "SignalTrace"},
                          {           "freeze",          "Button"},
                          {             "halt",  "CriticalButton"},
                          {   "sedation-index",  "NumericDisplay"},
                          {            "state", "StatusIndicator"},
                          {       "patient-id",       "TextInput"}
                      };

                      checks.expect(package.nodes.size() == expected.size(), std::format("eleven compiled nodes, got {}", package.nodes.size()));
                      for (const auto& [id, kind] : expected) {
                          const ms::CompiledNode* compiled = package.find(id);
                          if (compiled == nullptr) {
                              checks.expect(false, std::format("node '{}' is compiled", id));
                              continue;
                          }
                          checks.expect(ms::kindName(*compiled) == kind, std::format("node '{}' is a {}, got '{}'", id, kind, ms::kindName(*compiled)));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register fieldsReachTheirOwnSpecMembers{
    "A component's fields reach the spec members the dictionary maps them to",
    "evidence-unit",
    [] {
        return speclab::Test("medui-package-spec-members")
            .Given("components whose fields share spellings across different meanings", [] {})
            .When("the screen is compiled", [] {})
            .Then("each field lands on its own component's member, and a synthetic Panel carries the Row's background",
                  [] {
                      mdux::spec::Checks       checks;
                      const md::ScreenDocument document = everyComponent();
                      const ms::ScreenPackage  package  = document.package();

                      // `text` on a Label and `label` on a Button are both a text key; `source` is a
                      // data stream on one component and an image package on another. That mapping
                      // is what a table keyed by field name could not express.
                      if (const auto* label = std::get_if<ms::LabelSpec>(&node(package, "title").payload)) {
                          checks.expect(label->textKey == "STR-TITLE", std::format("the Label's text key, got '{}'", label->textKey));
                      } else {
                          checks.expect(false, "the title node holds a Label spec");
                      }
                      if (const auto* button = std::get_if<ms::ButtonSpec>(&node(package, "freeze").payload)) {
                          checks.expect(button->labelKey == "STR-FREEZE", std::format("the Button's label key, got '{}'", button->labelKey));
                          checks.expect(button->source == "FREEZE", std::format("the Button's data source, got '{}'", button->source));
                          checks.expect(button->requirement == "REQ-EC-001", "the Button's optional requirement is carried");
                      } else {
                          checks.expect(false, "the freeze node holds a Button spec");
                      }
                      if (const auto* image = std::get_if<ms::ImageSpec>(&node(package, "logo").payload)) {
                          checks.expect(image->source == "IMG-LOGO", std::format("the Image's package id, got '{}'", image->source));
                      } else {
                          checks.expect(false, "the logo node holds an Image spec");
                      }
                      if (const auto* status = std::get_if<ms::StatusIndicatorSpec>(&node(package, "state").payload)) {
                          checks.expect(status->stateKeys.size() == 2, std::format("two states, got {}", status->stateKeys.size()));
                          checks.expect(status->colorTokens.size() == status->stateKeys.size(), "the per-state tints pair with the states");
                      } else {
                          checks.expect(false, "the state node holds a StatusIndicator spec");
                      }
                      if (const auto* input = std::get_if<ms::TextInputSpec>(&node(package, "patient-id").payload)) {
                          checks.expect(input->maxLength == 16, std::format("the TextInput's length bound, got {}", input->maxLength));
                          checks.expect(input->charset == "Ascii", std::format("the TextInput's charset, got '{}'", input->charset));
                      } else {
                          checks.expect(false, "the patient-id node holds a TextInput spec");
                      }
                      // The one payload no author can write: the solver synthesises it for a Row
                      // that declares a background, and it carries that background.
                      if (const auto* panel = std::get_if<ms::PanelSpec>(&node(package, "topbar-background").payload)) {
                          checks.expect(panel->colorToken == "Theme.Colors.TopbarBackground", std::format("the Row's background, got '{}'", panel->colorToken));
                      } else {
                          checks.expect(false, "the synthetic background holds a Panel spec");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register writingAndReadingAgree{
    "Canonical JSON round-trips every payload without loss",
    "evidence-unit",
    [] {
        return speclab::Test("medui-package-round-trip")
            .Given("a compiled screen carrying all eleven payloads", [] {})
            .When("it is written as canonical JSON and read back", [] {})
            .Then("the screen read back equals the screen written, node for node",
                  [] {
                      mdux::spec::Checks       checks;
                      const md::ScreenDocument original = everyComponent();
                      const std::string        written  = md::writePackage(original.package());

                      const md::PackageReadResult reread = md::readPackage(written, "package.json");
                      checks.expect(reread.ok(), std::format("the written bytes read back, first diagnostic '{}'", firstCode(reread)));
                      if (!reread.ok()) {
                          checks.raise();
                          return;
                      }

                      const ms::ScreenPackage before = original.package();
                      const ms::ScreenPackage after  = reread.document.package();
                      checks.expect(after.id == before.id, "the package id survives");
                      checks.expect(after.surfaceWidth == before.surfaceWidth && after.surfaceHeight == before.surfaceHeight, "the surface survives");
                      checks.expect(after.budget == before.budget, "the draw budget survives");
                      checks.expect(after.nodes.size() == before.nodes.size(),
                                    std::format("{} nodes survive, got {}", before.nodes.size(), after.nodes.size()));
                      for (std::size_t index = 0; index < std::min(before.nodes.size(), after.nodes.size()); ++index) {
                          checks.expect(after.nodes[index] == before.nodes[index], std::format("node '{}' survives unchanged", before.nodes[index].id));
                      }
                      // Writing what was read gives the same bytes: a round trip that lost a member
                      // would still compare equal if the reader dropped it on both sides.
                      checks.expect(md::writePackage(after) == written, "writing the screen read back reproduces the bytes");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theBytesAreExactlyDetermined{
    "A small screen has exactly these bytes",
    "evidence-unit",
    [] {
        return speclab::Test("medui-package-canonical-bytes")
            .Given("a screen with one Label", [] {})
            .When("it is written as canonical JSON", [] {})
            .Then("the bytes are sorted, two-space indented, integer-only, and end with a newline",
                  [] {
                      mdux::spec::Checks checks;
                      const std::string  written = md::writePackage(tinyDocument().package());

                      const std::string expected = R"({
  "budget": {
    "maxCommands": 256,
    "maxIndices": 6144,
    "maxVertices": 4096
  },
  "id": "tiny",
  "kind": "screen",
  "nodes": [
    {
      "bounds": {
        "height": 40,
        "width": 200,
        "x": 0,
        "y": 0
      },
      "id": "title",
      "kind": "Label",
      "spec": {
        "colorToken": "Theme.Colors.Title",
        "textKey": "STR-TITLE"
      }
    }
  ],
  "schemaVersion": 1,
  "surfaceHeight": 100,
  "surfaceWidth": 200
}
)";
                      checks.expect(written == expected, std::format("the package bytes are exactly as documented, got:\n{}", written));
                      // A screen package holds no float, so ADR-007's `{"bits": N}` encoding - the
                      // one thing that makes a float portable - never appears in one.
                      checks.expect(written.find("\"bits\"") == std::string::npos, "no floating-point value appears in a screen package");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register anUndefinedMemberIsRefused{"A member the format does not define is refused rather than ignored", "evidence-unit", [] {
                                                          return speclab::Test("medui-package-unknown-member")
                                                              .Given("committed bytes carrying a member no reader knows", [] {})
                                                              .When("they are read", [] {})
                                                              .Then("the read fails, naming the member",
                                                                    [] {
                                                                        mdux::spec::Checks checks;
                                                                        const std::string  written = md::writePackage(tinyDocument().package());

                                                                        // A misspelling is the realistic case, and the dangerous one: silently
                                                                        // ignoring `colourToken` would leave a reviewer believing a tint was pinned.
                                                                        const std::string edited = editing(written, "\"colorToken\"", "\"colourToken\"");
                                                                        const md::PackageReadResult result = md::readPackage(edited, "package.json");
                                                                        checks.expect(!result.ok(), "an undefined member is refused");
                                                                        checks.expect(firstCode(result) == "SCP003",
                                                                                      std::format("reported as SCP003, got '{}'", firstCode(result)));
                                                                        checks.raise();
                                                                    })
                                                              .Execute();
                                                      }};

const mdux::spec::Register anUnknownComponentIsRefused{"A node naming a component outside the dictionary is refused", "evidence-unit", [] {
                                                           return speclab::Test("medui-package-unknown-kind")
                                                               .Given("committed bytes naming a component that does not exist", [] {})
                                                               .When("they are read", [] {})
                                                               .Then("the read fails with the code for an unknown kind",
                                                                     [] {
                                                                         mdux::spec::Checks checks;
                                                                         const std::string  edited = editing(md::writePackage(tinyDocument().package()),
                                                                                                            "\"Label\"",
                                                                                                            "\"Marquee\"");
                                                                         const md::PackageReadResult result = md::readPackage(edited, "package.json");
                                                                         checks.expect(!result.ok(), "an unknown component is refused");
                                                                         checks.expect(firstCode(result) == "SCP004",
                                                                                       std::format("reported as SCP004, got '{}'", firstCode(result)));
                                                                         checks.raise();
                                                                     })
                                                               .Execute();
                                                       }};

const mdux::spec::Register aScreenTheSchemaRefusesIsRefused{
    "A hand-edited screen the schema refuses fails at the read, not at the device",
    "evidence-unit",
    [] {
        return speclab::Test("medui-package-schema-refusal")
            .Given("committed bytes whose surface no longer contains their nodes", [] {})
            .When("they are read", [] {})
            .Then("the read fails with the schema's own verdict",
                  [] {
                      mdux::spec::Checks checks;
                      // Shrinking the surface leaves the file canonical and the members known: only
                      // `validate()` can catch it, which is why the reader runs the same one a
                      // device would rather than repeating the rules itself.
                      const std::string edited = editing(md::writePackage(tinyDocument().package()), "\"surfaceWidth\": 200", "\"surfaceWidth\": 100");
                      const md::PackageReadResult result = md::readPackage(edited, "package.json");
                      checks.expect(!result.ok(), "a screen the schema refuses is refused here");
                      checks.expect(firstCode(result) == "SCP005", std::format("reported as SCP005, got '{}'", firstCode(result)));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register nonCanonicalBytesAreRefused{"Bytes that are not canonical JSON are refused", "evidence-unit", [] {
                                                           return speclab::Test("medui-package-non-canonical")
                                                               .Given("a package whose integer was rewritten as a decimal", [] {})
                                                               .When("it is read", [] {})
                                                               .Then("the read fails before any member is interpreted",
                                                                     [] {
                                                                         mdux::spec::Checks checks;
                                                                         // The canonical reader rejects any fraction or exponent outright: a decimal in
                                                                         // a generated file means it was hand-edited or written by another tool.
                                                                         const std::string edited = editing(md::writePackage(tinyDocument().package()),
                                                                                                            "\"height\": 40",
                                                                                                            "\"height\": 40.0");
                                                                         const md::PackageReadResult result = md::readPackage(edited, "package.json");
                                                                         checks.expect(!result.ok(), "non-canonical bytes are refused");
                                                                         checks.expect(firstCode(result) == "SCP001",
                                                                                       std::format("reported as SCP001, got '{}'", firstCode(result)));
                                                                         checks.raise();
                                                                     })
                                                               .Execute();
                                                       }};

const mdux::spec::Register goldensAreWrittenAsTheirOwnSidecar{
    "The golden references are written as a sidecar, empty array included",
    "evidence-unit",
    [] {
        return speclab::Test("medui-package-goldens-sidecar")
            .Given("a screen that pins safety-critical nodes, and one that pins none", [] {})
            .When("the sidecar is written for each", [] {})
            .Then("one carries the agreed member names and the other is an empty array",
                  [] {
                      mdux::spec::Checks checks;

                      const std::string pinned = md::writeGoldens(md::collectGoldens(layoutOf(fixture("accepted-goldens.medui"), 400, 300)));
                      for (const std::string_view member : {"\"bounds\"", "\"colorToken\"", "\"cvChecks\"", "\"nodeId\"", "\"textKey\""}) {
                          checks.expect(pinned.find(member) != std::string::npos, std::format("the sidecar spells {}", member));
                      }
                      checks.expect(pinned.find("node_id") == std::string::npos, "no snake_case member survives ADR-011's amendment");

                      // ADR-012 makes all three outputs unconditional: a baker that skipped this
                      // file for a screen with nothing to pin would break the build rather than
                      // mean "this screen pins nothing".
                      const std::string none = md::writeGoldens({});
                      checks.expect(none == "[]\n", std::format("a screen pinning nothing writes an empty array, got '{}'", none));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aBypassedGateIsARefusalNotADiagnostic{
    "Compiling a screen that did not resolve is a bypassed gate, not a diagnostic",
    "evidence-unit",
    [] {
        return speclab::Test("medui-package-bypassed-gate")
            .Given("a layout result carrying a diagnostic", [] {})
            .When("it is handed to the package stage anyway", [] {})
            .Then("the stage throws rather than compiling half a screen",
                  [] {
                      mdux::spec::Checks checks;
                      md::LayoutResult   unresolved;
                      cli::Diagnostic    overflow;
                      overflow.file = "package.medui";
                      overflow.code = "MEDUI-E051";
                      unresolved.diagnostics.push_back(std::move(overflow));

                      bool threw = false;
                      try {
                          const md::ScreenDocument document = md::buildPackage(unresolved, {.id = "tiny", .budget = testBudget});
                          checks.expect(document.package().nodes.empty(), "unreachable: the stage compiled a screen that did not resolve");
                      } catch (const std::logic_error&) {
                          threw = true;
                      }
                      checks.expect(threw, "an unresolved layout is refused as a bypassed gate");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register aMovedDocumentStillOwnsItsText{
    "A moved document still owns every name its package views",
    "evidence-unit",
    [] {
        return speclab::Test("medui-package-survives-a-move")
            .Given("a compiled screen whose names are short enough to live inside their strings", [] {})
            .When("the document is move-constructed and then move-assigned", [] {})
            .Then("every name reads back unchanged through the package",
                  [] {
                      mdux::spec::Checks checks;

                      // The small-string optimisation is the whole hazard: a name short enough to
                      // live inside its `std::string` moves with the object, so this asserts what
                      // the deque is chosen for rather than leaving it as an argument in a comment.
                      // `writePackage()` reads every name and every span, so comparing the bytes
                      // before and after a move exercises all of them at once.
                      const std::string before = md::writePackage(everyComponent().package());

                      // Initialising from the helper's return value would elide the move entirely
                      // and assert nothing, so the source is named first and moved from explicitly.
                      md::ScreenDocument source = everyComponent();
                      md::ScreenDocument moved{std::move(source)};
                      md::ScreenDocument assigned;
                      assigned = std::move(moved);  // the operation readPackage() performs

                      const ms::ScreenPackage package = assigned.package();
                      checks.expect(package.validate().has_value(), "the moved screen still satisfies its schema");
                      checks.expect(md::writePackage(package) == before, "every name and span survives both moves unchanged");

                      // A state key list is a span into storage the document owns separately from
                      // its text, so it is worth naming rather than trusting the byte comparison.
                      if (const auto* status = std::get_if<ms::StatusIndicatorSpec>(&node(package, "state").payload)) {
                          checks.expect(status->stateKeys.size() == 2 && status->stateKeys.front() == "STR-OK",
                                        "the per-state keys still read back after the move");
                      } else {
                          checks.expect(false, "the state node holds a StatusIndicator spec");
                      }
                      checks.raise();
                  })
            .Execute();
    }};
