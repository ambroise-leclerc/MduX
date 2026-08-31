/**
 * @file GeneratedModelTests.cpp
 * @brief Compares both compiled forms of the generated ECG model package (issue #153).
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: this suite links MduX::Core only)
 * @compliance ADR-007 Evidence pipeline doctrine
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * Compiling the two consumers executes the generated `static_assert`. Linking this test only to
 * MduX::Core is also evidence that consuming the package needs no host-tools parser.
 */
import std;
import speclab;
import mdux.ml.schema;

#include "../framework/SpecLabBridge.hpp"
#include "GeneratedModelConsumers.hpp"

namespace {

namespace ml        = mdux::ml;
namespace generated = mdux::test::generated;

}  // namespace

const mdux::spec::Register generatedFormsAgree{
    "The generated module and header forms describe the same model package",
    "evidence-unit",
    [] {
        return speclab::Test("ml-generated-model-forms-agree")
            .Given("the ECG package emitted in both supported C++ forms", [] {})
            .When("both forms are compiled into one Core-only binary", [] {})
            .Then("all metadata, layers, and golden vectors agree",
                  [] {
                      const ml::ModelPackage fromModule = generated::modelFromModule();
                      const ml::ModelPackage fromHeader = generated::modelFromHeader();
                      mdux::spec::Checks     checks;
                      checks.expect(fromModule.id == fromHeader.id, "the package id");
                      checks.expect(fromModule.schemaVersion == fromHeader.schemaVersion, "the schema version");
                      checks.expect(fromModule.weightsDigest == fromHeader.weightsDigest, "the weight digest");
                      checks.expect(fromModule.weightsByteLength == fromHeader.weightsByteLength, "the weight length");
                      checks.expect(fromModule.inputLength == fromHeader.inputLength, "the input length");
                      checks.expect(fromModule.outputLength == fromHeader.outputLength, "the output length");
                      checks.expect(fromModule.maxScratchFloats == fromHeader.maxScratchFloats, "the scratch budget");
                      checks.expect(std::ranges::equal(fromModule.layers, fromHeader.layers), "every layer descriptor");
                      checks.expect(fromModule.goldens.size() == fromHeader.goldens.size(), "the golden-vector count");
                      for (std::size_t index = 0; index < std::min(fromModule.goldens.size(), fromHeader.goldens.size()); ++index) {
                          checks.expect(std::ranges::equal(fromModule.goldens[index].inputBits, fromHeader.goldens[index].inputBits),
                                        std::format("golden {} input", index));
                          checks.expect(std::ranges::equal(fromModule.goldens[index].expectedOutputBits, fromHeader.goldens[index].expectedOutputBits),
                                        std::format("golden {} expected output", index));
                      }
                      checks.expect(fromModule.validate().has_value(), "the package also validates at runtime");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register generatedEcgContent{"The generated ECG package carries the committed model contract", "evidence-unit", [] {
                                                   return speclab::Test("ml-generated-model-content")
                                                       .Given("the compile-time validated ECG package", [] {})
                                                       .When("its bounded contract is read", [] {})
                                                       .Then("the expected demonstrator dimensions and goldens are present",
                                                             [] {
                                                                 const ml::ModelPackage package = generated::modelFromModule();
                                                                 mdux::spec::Checks     checks;
                                                                 checks.expect(package.id == "ecg-demo", "the committed package id");
                                                                 checks.expect(package.layers.size() == 5, "five layers");
                                                                 checks.expect(package.goldens.size() == 4, "four golden vectors");
                                                                 checks.expect(package.inputLength == 180, "the 180-sample input window");
                                                                 checks.expect(package.outputLength == 4, "the four-class output");
                                                                 checks.raise();
                                                             })
                                                       .Execute();
                                               }};
