/**
 * @file GeneratedScreenTests.cpp
 * @brief BDD scenarios for the generated screen, in both emitted forms (issue #197).
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: this suite links MduX::Core only)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * This is where #197's acceptance criterion is met: both emitted forms are compiled into one binary
 * and asserted to describe the same screen.
 *
 * Two properties are proved by this file existing at all, before any scenario runs. The generated
 * source carries `static_assert(screen.validate().has_value())`, so **compiling** the consumers
 * proves the emitted screen satisfies its schema - a malformed screen could not have got this far.
 * And this target links `MduX::Core` and no host-tools module, so a device build reaching a compiled
 * screen through generated code links no compiler and parses nothing at startup, which is the end
 * state #153 is pursuing for every package kind.
 */

import std;
import speclab;
import mdux.medui.schema;

#include "../framework/SpecLabBridge.hpp"
#include "GeneratedScreenConsumers.hpp"

namespace {

namespace ms = mdux::medui;
namespace tg = mdux::test::generated;

}  // namespace

const mdux::spec::Register bothFormsDescribeOneScreen{
    "The module form and the header form describe the same screen",
    "evidence-unit",
    [] {
        return speclab::Test("medui-generated-screen-forms-agree")
            .Given("a screen emitted as a module interface and as a header", [] {})
            .When("both are compiled into one binary and read", [] {})
            .Then("they agree on the id, the surface, the budget and every node",
                  [] {
                      mdux::spec::Checks      checks;
                      const ms::ScreenPackage fromModule = tg::screenFromModule();
                      const ms::ScreenPackage fromHeader = tg::screenFromHeader();

                      checks.expect(fromModule.id == fromHeader.id, "both forms name the same screen");
                      checks.expect(fromModule.schemaVersion == fromHeader.schemaVersion, "both forms declare one schema version");
                      checks.expect(fromModule.surfaceWidth == fromHeader.surfaceWidth && fromModule.surfaceHeight == fromHeader.surfaceHeight,
                                    "both forms declare one surface");
                      checks.expect(fromModule.budget == fromHeader.budget, "both forms declare one draw budget");
                      checks.expect(fromModule.approvedTextPackages.size() == fromHeader.approvedTextPackages.size(),
                                    std::format("both forms hold {} text approvals, header holds {}",
                                                fromModule.approvedTextPackages.size(),
                                                fromHeader.approvedTextPackages.size()));
                      for (std::size_t index = 0; index < std::min(fromModule.approvedTextPackages.size(), fromHeader.approvedTextPackages.size()); ++index) {
                          checks.expect(fromModule.approvedTextPackages[index] == fromHeader.approvedTextPackages[index],
                                        std::format("text approval {} is identical in both forms", index));
                      }
                      checks.expect(fromModule.nodes.size() == fromHeader.nodes.size(),
                                    std::format("both forms hold {} nodes, header holds {}", fromModule.nodes.size(), fromHeader.nodes.size()));

                      for (std::size_t index = 0; index < std::min(fromModule.nodes.size(), fromHeader.nodes.size()); ++index) {
                          checks.expect(fromModule.nodes[index] == fromHeader.nodes[index],
                                        std::format("node '{}' is identical in both forms", fromModule.nodes[index].id));
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register theGeneratedScreenIsTheCompiledOne{
    "The generated screen is the screen the compiler resolved",
    "evidence-unit",
    [] {
        return speclab::Test("medui-generated-screen-content")
            .Given("the screen emitted from the committed package", [] {})
            .When("its nodes are read through the schema", [] {})
            .Then("every component's own fields are there, including the ones a flat node could not hold",
                  [] {
                      mdux::spec::Checks      checks;
                      const ms::ScreenPackage screen = tg::screenFromModule();

                      checks.expect(screen.id == "every-component", std::format("the screen id, got '{}'", screen.id));
                      checks.expect(screen.nodes.size() == 11, std::format("eleven nodes, got {}", screen.nodes.size()));
                      checks.expect(screen.validate().has_value(), "the generated screen validates at run time as well as at compile time");

                      // The four components a flat node could not have held. Reading them back out
                      // of generated code is what says the emitted form carries them, not just the
                      // JSON the emitter read.
                      const ms::CompiledNode* clock = screen.find("wall-clock");
                      if (clock != nullptr) {
                          const auto* spec = std::get_if<ms::ClockSpec>(&clock->payload);
                          checks.expect(spec != nullptr && spec->format == "TimeSeconds", "the Clock's format survives emission");
                      } else {
                          checks.expect(false, "the generated screen holds the Clock");
                      }

                      const ms::CompiledNode* critical = screen.find("halt");
                      if (critical != nullptr) {
                          const auto* spec = std::get_if<ms::CriticalButtonSpec>(&critical->payload);
                          checks.expect(spec != nullptr && spec->onPress == "TriggerHalt", "the CriticalButton's action survives emission");
                          checks.expect(spec != nullptr && spec->requirement == "REQ-EC-002", "its requirement survives, for the trace");
                      } else {
                          checks.expect(false, "the generated screen holds the CriticalButton");
                      }

                      const ms::CompiledNode* status = screen.find("state");
                      if (status != nullptr) {
                          const auto* spec = std::get_if<ms::StatusIndicatorSpec>(&status->payload);
                          // The spans are the interesting part: they view namespace-scope arrays the
                          // emitter declared beside the node table, so this reads generated storage
                          // rather than a copy.
                          checks.expect(spec != nullptr && spec->stateKeys.size() == 2, "the StatusIndicator's states survive emission");
                          checks.expect(spec != nullptr && !spec->colorTokens.empty() && spec->colorTokens.size() == spec->stateKeys.size(),
                                        "its per-state tints pair with its states");
                      } else {
                          checks.expect(false, "the generated screen holds the StatusIndicator");
                      }

                      const ms::CompiledNode* viewport = screen.find("endoscope");
                      if (viewport != nullptr) {
                          const auto* spec = std::get_if<ms::VulkanViewportSpec>(&viewport->payload);
                          checks.expect(spec != nullptr && spec->streamSource == "ENDOSCOPE", "the VulkanViewport's stream survives emission");
                      } else {
                          checks.expect(false, "the generated screen holds the VulkanViewport");
                      }

                      // A traceability export walks this, so it is worth reading out of the emitted
                      // form rather than trusting that the schema function is shared.
                      std::size_t traced = 0;
                      for (const ms::CompiledNode& node : screen.nodes) {
                          if (!ms::requirementOf(node).empty()) {
                              ++traced;
                          }
                      }
                      checks.expect(traced == 5, std::format("five nodes carry a requirement, got {}", traced));
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register anEmptyScreenIsConsumable{"A screen with no nodes is generated code a consumer can hold", "evidence-unit", [] {
                                                         return speclab::Test("medui-generated-empty-screen")
                                                             .Given("the degenerate screen the schema permits: no nodes, empty budget", [] {})
                                                             .When("its generated form is compiled and read", [] {})
                                                             .Then("it holds no nodes and still validates",
                                                                   [] {
                                                                       mdux::spec::Checks      checks;
                                                                       const ms::ScreenPackage screen = tg::emptyScreenFromModule();

                                                                       // The compiling of GeneratedEmptyScreenConsumer.cpp is the assertion that
                                                                       // matters here; an emitter that rendered `CompiledNode nodes[] = {}` would
                                                                       // have failed to build rather than failed this scenario.
                                                                       checks.expect(screen.id == "empty-screen",
                                                                                     std::format("the screen id, got '{}'", screen.id));
                                                                       checks.expect(screen.nodes.empty(),
                                                                                     std::format("no nodes, got {}", screen.nodes.size()));
                                                                       checks.expect(screen.validate().has_value(),
                                                                                     "an empty screen with an empty budget is valid");
                                                                       checks.raise();
                                                                   })
                                                             .Execute();
                                                     }};
