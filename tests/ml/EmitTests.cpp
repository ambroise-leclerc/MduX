/**
 * @file EmitTests.cpp
 * @brief Host-tool scenarios for the constexpr model emitter (issue #153).
 */
import std;
import speclab;
import mdux.tools.cli;
import mdux.tools.ml.emit;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace cli  = mdux::tools::cli;
namespace emit = mdux::tools::ml;

const mdux::spec::Register committedPackageRenders{
    "The committed ECG package renders both generated forms",
    "evidence-unit",
    [] {
        struct State {
            std::vector<cli::Diagnostic>     diagnostics;
            std::optional<emit::EmitOutputs> outputs;
        };
        auto state = std::make_shared<State>();
        return speclab::Test("ml-emit-committed-package-renders")
            .Given("the committed ECG package", [] {})
            .When("the model emitter renders it",
                  [state] {
                      state->outputs = emit::renderModel(std::filesystem::path{MDUX_REPO_ROOT} / "generated/model/ecg-demo/package.json", state->diagnostics);
                  })
            .Then("a module, header, and compile-time schema assertion are present",
                  [state] {
                      if (!state->outputs.has_value()) {
                          throw speclab::core::AssertionFailure("renderModel() produced no output", std::source_location::current());
                      }
                      mdux::spec::Checks checks;
                      checks.expect(state->diagnostics.empty(), "no diagnostics");
                      checks.expect(state->outputs->stem == "model_ecg_demo", "the file stem");
                      checks.expect(state->outputs->moduleName == "mdux.ml.generated.model_ecg_demo", "the module name");
                      checks.expect(state->outputs->moduleSource.contains("static_assert(model.validate().has_value()"),
                                    "the module validates the emitted package at compile time");
                      checks.expect(state->outputs->headerSource.contains("static_assert(model.validate().has_value()"),
                                    "the header validates the emitted package at compile time");
                      checks.expect(!state->outputs->moduleSource.contains("weights.bin"), "weight bytes are not emitted");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register unreadablePackageReported{
    "An unreadable model package is diagnosed",
    "evidence-unit",
    [] {
        struct State {
            std::vector<cli::Diagnostic> diagnostics;
            bool                         produced{true};
        };
        auto state = std::make_shared<State>();
        return speclab::Test("ml-emit-unreadable-package")
            .Given("a package path that does not exist", [] {})
            .When("the emitter tries to read it",
                  [state] {
                      state->produced = emit::renderModel("does-not-exist/package.json", state->diagnostics).has_value();
                  })
            .Then("MLE001 is reported and nothing is rendered",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(!state->produced, "no outputs");
                      checks.expect(state->diagnostics.size() == 1, "one diagnostic");
                      if (!state->diagnostics.empty()) {
                          checks.expect(state->diagnostics.front().code == "MLE001", "the stable diagnostic code");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register identifierParity{
    "The CMake and C++ model identifier rules agree",
    "evidence-unit",
    [] {
        struct State {
            std::size_t              compared{0};
            std::vector<std::string> mismatches;
        };
        auto state = std::make_shared<State>();
        return speclab::Test("ml-emit-identifier-parity")
            .Given("the identifier answers CMake wrote at configure time", [] {})
            .When("each is compared with identifierForModel()",
                  [state] {
                      std::ifstream parity{MDUX_MODEL_IDENTIFIER_PARITY_FILE};
                      if (!parity.is_open()) {
                          throw speclab::core::AssertionFailure("the model identifier parity file could not be opened", std::source_location::current());
                      }
                      std::string line;
                      while (std::getline(parity, line)) {
                          const std::size_t tab = line.find('\t');
                          if (tab == std::string::npos) {
                              throw speclab::core::AssertionFailure("a model identifier parity line has no tab", std::source_location::current());
                          }
                          const std::string id        = line.substr(0, tab);
                          const std::string fromCMake = line.substr(tab + 1);
                          const std::string fromCpp   = emit::identifierForModel(id);
                          if (fromCpp != fromCMake) {
                              state->mismatches.push_back(std::format("'{}': CMake '{}', C++ '{}'", id, fromCMake, fromCpp));
                          }
                          ++state->compared;
                      }
                  })
            .Then("all configured examples match and the corpus is non-empty",
                  [state] {
                      mdux::spec::Checks checks;
                      checks.expect(state->compared == 8, "all eight identifiers were compared");
                      checks.expect(state->mismatches.empty(), "the rules agree");
                      for (const std::string& mismatch : state->mismatches) {
                          checks.expect(false, mismatch);
                      }
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
