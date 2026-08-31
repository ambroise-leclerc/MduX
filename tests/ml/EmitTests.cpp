/**
 * @file EmitTests.cpp
 * @brief Host-tool scenarios for the constexpr model emitter (issue #153).
 */
import std;
import speclab;
import mdux.tools.cli;
import mdux.tools.ml.emit;

#include "../framework/SpecLabBridge.hpp"
#include "../framework/TemporaryDirectory.hpp"

namespace {

namespace cli  = mdux::tools::cli;
namespace emit = mdux::tools::ml;

/// Returns the committed demonstrator package consumed by these scenarios.
[[nodiscard]] std::filesystem::path committedPackagePath() {
    return std::filesystem::path{MDUX_REPO_ROOT} / "generated/model/ecg-demo/package.json";
}

/// Reads a complete fixture or generated source as binary text.
[[nodiscard]] std::string contentsOf(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        throw speclab::core::AssertionFailure(std::format("{} could not be opened", path.generic_string()), std::source_location::current());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// Writes exact binary text to a scenario-owned path.
void writeText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
        throw speclab::core::AssertionFailure(std::format("{} could not be written", path.generic_string()), std::source_location::current());
    }
}

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
                      state->outputs = emit::renderModel(committedPackagePath(), state->diagnostics);
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

const mdux::spec::Register writingIsIdempotent{
    "Writing the same model twice does not restamp either generated form",
    "evidence-unit",
    [] {
        return speclab::Test("ml-emit-write-is-idempotent")
            .Given("the committed package rendered into an empty directory", [] {})
            .When("both generated forms are written twice", [] {})
            .Then("the second write preserves their bytes and timestamps",
                  [] {
                      mdux::spec::Checks             checks;
                      std::vector<cli::Diagnostic>   diagnostics;
                      mdux::test::TemporaryDirectory scratch{"mdux-mlemit-write"};
                      const auto                     outputs = emit::renderModel(committedPackagePath(), diagnostics);
                      if (!outputs.has_value()) {
                          checks.expect(false, "the committed package renders");
                          checks.raise();
                          return;
                      }

                      checks.expect(emit::writeModel(*outputs, scratch.path(), diagnostics), "the first write succeeds");
                      const std::filesystem::path module = scratch.path() / "model_ecg_demo.cppm";
                      const std::filesystem::path header = scratch.path() / "model_ecg_demo.hpp";
                      checks.expect(contentsOf(module) == outputs->moduleSource, "the module holds the rendered source");
                      checks.expect(contentsOf(header) == outputs->headerSource, "the header holds the rendered source");

                      const auto sentinel = std::filesystem::file_time_type::clock::now() - std::chrono::hours{24};
                      std::filesystem::last_write_time(module, sentinel);
                      std::filesystem::last_write_time(header, sentinel);
                      const auto moduleStamp = std::filesystem::last_write_time(module);
                      const auto headerStamp = std::filesystem::last_write_time(header);

                      checks.expect(emit::writeModel(*outputs, scratch.path(), diagnostics), "the second write succeeds");
                      checks.expect(std::filesystem::last_write_time(module) == moduleStamp, "the unchanged module is not restamped");
                      checks.expect(std::filesystem::last_write_time(header) == headerStamp, "the unchanged header is not restamped");
                      checks.expect(diagnostics.empty(), "both writes report no diagnostics");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register unwritableOutputReported{"An output path that cannot be a directory is diagnosed", "evidence-unit", [] {
                                                        return speclab::Test("ml-emit-unwritable-output")
                                                            .Given("a regular file where the output directory must be", [] {})
                                                            .When("the generated forms are written there", [] {})
                                                            .Then("MLE002 is reported",
                                                                  [] {
                                                                      mdux::spec::Checks             checks;
                                                                      std::vector<cli::Diagnostic>   diagnostics;
                                                                      mdux::test::TemporaryDirectory scratch{"mdux-mlemit-unwritable"};
                                                                      const auto                     obstruction = scratch.path() / "not-a-directory";
                                                                      writeText(obstruction, "occupied");

                                                                      const emit::EmitOutputs outputs{.moduleName   = "mdux.ml.generated.model_test",
                                                                                                      .moduleSource = "module",
                                                                                                      .headerSource = "header",
                                                                                                      .stem         = "model_test"};
                                                                      checks.expect(!emit::writeModel(outputs, obstruction, diagnostics), "writing is refused");
                                                                      checks.expect(diagnostics.size() == 1, "one diagnostic");
                                                                      if (!diagnostics.empty()) {
                                                                          checks.expect(diagnostics.front().code == "MLE002", "the stable diagnostic code");
                                                                      }
                                                                      checks.raise();
                                                                  })
                                                            .Execute();
                                                    }};

const mdux::spec::Register reservedIdentifierReported{"A model id that maps to a reserved C++ identifier is diagnosed", "evidence-unit", [] {
                                                          return speclab::Test("ml-emit-reserved-identifier")
                                                              .Given("a package whose id has adjacent separators", [] {})
                                                              .When("the package is rendered", [] {})
                                                              .Then("MLE003 is reported before source is produced",
                                                                    [] {
                                                                        mdux::spec::Checks             checks;
                                                                        std::vector<cli::Diagnostic>   diagnostics;
                                                                        mdux::test::TemporaryDirectory scratch{"mdux-mlemit-reserved"};
                                                                        std::string                    package  = contentsOf(committedPackagePath());
                                                                        const std::string              original = "\"id\": \"ecg-demo\"";
                                                                        const std::size_t              at       = package.find(original);
                                                                        checks.expect(at != std::string::npos, "the committed package carries its id");
                                                                        if (at != std::string::npos) {
                                                                            package.replace(at, original.size(), "\"id\": \"a--b\"");
                                                                        }
                                                                        const auto packagePath = scratch.path() / "package.json";
                                                                        writeText(packagePath, package);

                                                                        const auto outputs = emit::renderModel(packagePath, diagnostics);
                                                                        checks.expect(!outputs.has_value(), "a reserved identifier renders nothing");
                                                                        checks.expect(diagnostics.size() == 1, "one diagnostic");
                                                                        if (!diagnostics.empty()) {
                                                                            checks.expect(diagnostics.front().code == "MLE003", "the stable diagnostic code");
                                                                        }
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
