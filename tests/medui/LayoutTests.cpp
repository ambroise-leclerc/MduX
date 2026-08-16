/**
 * @file LayoutTests.cpp
 * @brief BDD scenarios for bounded integer `.medui` layout (issue #194).
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 */

import std;
import speclab;
import mdux.tools.cli;
import mdux.tools.medui.ast;
import mdux.tools.medui.diagnostics;
import mdux.tools.medui.layout;
import mdux.tools.medui.parser;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace md  = mdux::tools::medui;
namespace cli = mdux::tools::cli;

static_assert(std::integral<decltype(md::LayoutRect::x)>);
static_assert(std::integral<decltype(md::LayoutRect::y)>);
static_assert(std::integral<decltype(md::LayoutRect::width)>);
static_assert(std::integral<decltype(md::LayoutRect::height)>);

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

[[nodiscard]] md::LayoutResult layout(std::string_view source, std::int64_t width, std::int64_t height) {
    md::ParseResult parsed = md::parse(source, "layout.medui");
    if (!parsed.screen || !parsed.diagnostics.empty()) {
        throw speclab::core::AssertionFailure("layout test source did not parse", std::source_location::current());
    }
    return md::resolveLayout(*parsed.screen, "layout.medui", {.surfaceWidth = width, .surfaceHeight = height});
}

[[nodiscard]] const cli::Diagnostic* find(const md::LayoutResult& result, md::Code code) {
    const std::string_view wanted = md::id(code);
    const auto             found  = std::ranges::find_if(result.diagnostics, [wanted](const cli::Diagnostic& diagnostic) {
        return diagnostic.code == wanted;
    });
    return found == result.diagnostics.end() ? nullptr : &*found;
}

[[nodiscard]] std::string sourceWithBody(std::string_view body, std::int64_t width = 100, std::int64_t height = 100) {
    return std::format("Screen Test {{\n"
                       "    layout: Vertical {{ spacing: 0px; padding: 0px; }}\n"
                       "    surface: {}px, {}px;\n"
                       "{}"
                       "}}\n",
                       width,
                       height,
                       body);
}

}  // namespace

const mdux::spec::Register handDerivedRectangles{
    "Vertical and Row layout resolves to hand-derived absolute rectangles",
    "evidence-unit",
    [] {
        return speclab::Test("medui-layout-hand-derived")
            .Given("a 1280x720 screen with padding, spacing, fixed sizes and Fill", [] {})
            .When("the single-level Row and vertical flow are resolved", [] {})
            .Then("the Row is flat and every integer rectangle matches the derivation",
                  [] {
                      mdux::spec::Checks     checks;
                      const md::LayoutResult result = layout(fixture("accepted-layout.medui"), 1280, 720);
                      checks.expect(result.ok(), "layout succeeds");

                      const std::vector<std::pair<std::string_view, md::LayoutRect>> expected{
                          {"topbar-background",   {16, 16, 1248, 48}},
                          {            "title",    {16, 16, 340, 48}},
                          {            "clock",   {372, 16, 676, 48}},
                          {           "status",  {1064, 16, 200, 48}},
                          {            "score",  {16, 72, 1248, 120}},
                          {         "viewport", {16, 200, 1248, 504}},
                      };
                      checks.expect(result.nodes.size() == expected.size(), std::format("six flat entries, got {}", result.nodes.size()));
                      for (std::size_t index = 0; index < std::min(result.nodes.size(), expected.size()); ++index) {
                          checks.expect(result.nodes[index].id == expected[index].first, std::format("entry {} id is {}", index, expected[index].first));
                          checks.expect(result.nodes[index].bounds == expected[index].second,
                                        std::format("{} has its hand-derived rectangle", expected[index].first));
                          checks.expect(result.nodes[index].component != "Row", std::format("{} is not a Row container", expected[index].first));
                      }
                      if (!result.nodes.empty()) {
                          checks.expect(result.nodes.front().component == "Panel" && result.nodes.front().synthetic,
                                        "the Row background becomes a synthetic Panel underlay");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register positionedNodesLeaveFlow{"An explicitly positioned node does not advance the flow cursor", "evidence-unit", [] {
                                                        return speclab::Test("medui-layout-positioned-out-of-flow")
                                                            .Given("one positioned node followed by one ordinary flow node", [] {})
                                                            .When("the vertical layout is resolved", [] {})
                                                            .Then("the flow node still begins at the padded origin",
                                                                  [] {
                                                                      mdux::spec::Checks checks;
                                                                      const std::string  source = sourceWithBody(
                                                                          "    Label { id: pinned; width: 20px; height: 20px; position: 80px, 80px; "
                                                                           "text: t(\"STR-PINNED\"); color: Theme.Colors.Title; }\n"
                                                                           "    Label { id: flow; width: Fill; height: 20px; text: t(\"STR-FLOW\"); "
                                                                           "color: Theme.Colors.Title; }\n");
                                                                      const md::LayoutResult result = layout(source, 100, 100);
                                                                      checks.expect(result.ok(), "layout succeeds");
                                                                      checks.expect(result.nodes.size() == 2, "both leaves are emitted");
                                                                      if (result.nodes.size() == 2) {
                                                                          checks.expect(result.nodes[0].bounds == md::LayoutRect{80, 80, 20, 20},
                                                                                        "the positioned rectangle is absolute");
                                                                          checks.expect(result.nodes[1].bounds == md::LayoutRect{0, 0, 100, 20},
                                                                                        "the flow cursor was not advanced by the positioned node");
                                                                      }
                                                                      checks.raise();
                                                                  })
                                                            .Execute();
                                                    }};

const mdux::spec::Register positionedFillIsRejected{"A positioned component cannot use Fill", "evidence-unit", [] {
                                                        return speclab::Test("medui-layout-positioned-fill")
                                                            .Given("a component whose exact position contradicts a Fill width", [] {})
                                                            .When("layout preflight runs", [] {})
                                                            .Then("MEDUI-E051 points at position rather than inventing a rectangle",
                                                                  [] {
                                                                      mdux::spec::Checks     checks;
                                                                      const std::string      source     = sourceWithBody("    Label {\n"
                                                                                                                         "        id: title;\n"
                                                                                                                         "        width: Fill;\n"
                                                                                                                         "        height: 20px;\n"
                                                                                                                         "        position: 0px, 0px;\n"
                                                                                                                         "        text: t(\"STR-TITLE\");\n"
                                                                                                                         "        color: Theme.Colors.Title;\n"
                                                                                                                         "    }\n");
                                                                      const md::LayoutResult result     = layout(source, 100, 100);
                                                                      const cli::Diagnostic* diagnostic = find(result, md::Code::LayoutOverflow);
                                                                      checks.expect(!result.ok() && result.nodes.empty(), "no partial rectangle is returned");
                                                                      checks.expect(diagnostic != nullptr, "MEDUI-E051 is reported");
                                                                      if (diagnostic != nullptr) {
                                                                          checks.expect(
                                                                              diagnostic->line == 8 && diagnostic->column == 9,
                                                                              std::format("position is 8:9, got {}:{}", diagnostic->line, diagnostic->column));
                                                                      }
                                                                      checks.raise();
                                                                  })
                                                            .Execute();
                                                    }};

const mdux::spec::Register wideRowChildIsRejected{"A Row child wider than its parent is rejected rather than clamped", "evidence-unit", [] {
                                                      return speclab::Test("medui-layout-wide-row-child")
                                                          .Given("a 101px child in a 100px Row", [] {})
                                                          .When("the Row axis is resolved", [] {})
                                                          .Then("MEDUI-E051 is emitted and no rectangle is clamped",
                                                                [] {
                                                                    mdux::spec::Checks checks;
                                                                    const std::string  source = sourceWithBody(
                                                                        "    Row { id: row; height: 20px; Label { id: title; width: 101px; "
                                                                         "height: 20px; text: t(\"STR-TITLE\"); color: Theme.Colors.Title; } }\n");
                                                                    const md::LayoutResult result = layout(source, 100, 100);
                                                                    checks.expect(find(result, md::Code::LayoutOverflow) != nullptr, "MEDUI-E051 is reported");
                                                                    checks.expect(result.nodes.empty(), "no child rectangle is emitted");
                                                                    checks.raise();
                                                                })
                                                          .Execute();
                                                  }};

const mdux::spec::Register fillWithoutRoomIsRejected{"Fill with no remaining pixels is rejected rather than resolved to zero", "evidence-unit", [] {
                                                         return speclab::Test("medui-layout-fill-no-room")
                                                             .Given("a fixed-height item consuming the entire surface before a Fill item", [] {})
                                                             .When("the vertical axis is resolved", [] {})
                                                             .Then("MEDUI-E051 is emitted before any partial layout",
                                                                   [] {
                                                                       mdux::spec::Checks checks;
                                                                       const std::string  source = sourceWithBody(
                                                                           "    Label { id: fixed; width: Fill; height: 100px; text: t(\"STR-FIXED\"); "
                                                                            "color: Theme.Colors.Title; }\n"
                                                                            "    Label { id: fill; width: Fill; height: Fill; text: t(\"STR-FILL\"); "
                                                                            "color: Theme.Colors.Title; }\n");
                                                                       const md::LayoutResult result = layout(source, 100, 100);
                                                                       checks.expect(find(result, md::Code::LayoutOverflow) != nullptr,
                                                                                     "MEDUI-E051 is reported");
                                                                       checks.expect(result.nodes.empty(), "the failed axis produces no partial layout");
                                                                       checks.raise();
                                                                   })
                                                             .Execute();
                                                     }};

const mdux::spec::Register positionedOverlapIsRejected{"A positioned component cannot overlap a flow component", "evidence-unit", [] {
                                                           return speclab::Test("medui-layout-positioned-overlap")
                                                               .Given("a positioned rectangle crossing an ordinary flow rectangle", [] {})
                                                               .When("the flat layout is checked", [] {})
                                                               .Then("MEDUI-E051 rejects the overlap",
                                                                     [] {
                                                                         mdux::spec::Checks checks;
                                                                         const std::string  source = sourceWithBody(
                                                                             "    Label { id: flow; width: Fill; height: 30px; text: t(\"STR-FLOW\"); "
                                                                             "color: Theme.Colors.Title; }\n"
                                                                             "    Label { id: pinned; width: 20px; height: 20px; position: 10px, 10px; "
                                                                             "text: t(\"STR-PINNED\"); color: Theme.Colors.Title; }\n");
                                                                         const md::LayoutResult result = layout(source, 100, 100);
                                                                         checks.expect(find(result, md::Code::LayoutOverflow) != nullptr,
                                                                                       "MEDUI-E051 is reported");
                                                                         checks.expect(!result.ok(), "overlap is not accepted as a compiled layout");
                                                                         checks.raise();
                                                                     })
                                                               .Execute();
                                                       }};

const mdux::spec::Register syntheticIdsAreRechecked{"Synthetic Row background IDs participate in uniqueness", "evidence-unit", [] {
                                                        return speclab::Test("medui-layout-synthetic-id-uniqueness")
                                                            .Given("an authored id colliding with <row-id>-background", [] {})
                                                            .When("the Row background Panel is synthesized", [] {})
                                                            .Then("MEDUI-E014 rejects the duplicate resolved id",
                                                                  [] {
                                                                      mdux::spec::Checks checks;
                                                                      const std::string  source = sourceWithBody(
                                                                          "    Row { id: topbar; height: 20px; background: Theme.Colors.TopbarBackground; "
                                                                          "Label { id: title; width: Fill; height: 20px; text: t(\"STR-TITLE\"); "
                                                                          "color: Theme.Colors.Title; } }\n"
                                                                          "    Label { id: topbar-background; width: Fill; height: 20px; "
                                                                          "text: t(\"STR-OTHER\"); color: Theme.Colors.Title; }\n");
                                                                      const md::LayoutResult result = layout(source, 100, 100);
                                                                      checks.expect(find(result, md::Code::DuplicateNodeId) != nullptr,
                                                                                    "MEDUI-E014 is reported after synthesis");
                                                                      checks.expect(!result.ok(), "the colliding flat node set is rejected");
                                                                      checks.raise();
                                                                  })
                                                            .Execute();
                                                    }};

const mdux::spec::Register nestedRowInvariantIsChecked{
    "The solver checks the parser's single-Row-level invariant",
    "evidence-unit",
    [] {
        return speclab::Test("medui-layout-nested-row-invariant")
            .Given("a directly-mutated AST that bypasses the nested Row parser rejection", [] {})
            .When("the layout boundary receives it", [] {})
            .Then("it fails loudly in every build configuration",
                  [] {
                      mdux::spec::Checks checks;
                      md::ParseResult    parsed = md::parse(fixture("accepted-layout.medui"), "accepted-layout.medui");
                      bool               threw  = false;
                      if (parsed.screen && !parsed.screen->nodes.empty() && !parsed.screen->nodes.front().children.empty()) {
                          parsed.screen->nodes.front().children.front().component = "Row";
                          try {
                              static_cast<void>(md::resolveLayout(*parsed.screen, "accepted-layout.medui", {.surfaceWidth = 1280, .surfaceHeight = 720}));
                          } catch (const std::logic_error&) {
                              threw = true;
                          }
                      }
                      checks.expect(threw, "a nested Row cannot silently reach the flattening pass");
                      checks.raise();
                  })
            .Execute();
    }};
