/**
 * @file ImageBakeTests.cpp
 * @brief Committed image baker integration scenario.
 */
import std;
import speclab;
import mdux.image.schema;
import mdux.tools.cli;
import mdux.tools.imagebake;
#include "../framework/SpecLabBridge.hpp"

const mdux::spec::Register imageBakerReproducesCommittedPackage{
    "The image baker reproduces the committed QOI package",
    "evidence-unit",
    [] {
        return speclab::Test("image-baker-committed")
            .Given("the committed brand-mark recipe and QOI source", [] {})
            .When("the image baker rebuilds the package", [] {})
            .Then("fresh bytes match the committed package",
                  [] {
                      namespace bake = mdux::tools::imagebake;
                      const std::filesystem::path root{MDUX_REPO_ROOT};
                      const auto                  recipeBytes = bake::readFile(root / "recipes/image/brand-mark.toml");
                      if (!recipeBytes.has_value())
                          throw speclab::core::AssertionFailure("recipe unreadable", std::source_location::current());
                      const std::string_view                    recipeText{reinterpret_cast<const char*>(recipeBytes->data()), recipeBytes->size()};
                      std::vector<mdux::tools::cli::Diagnostic> diagnostics;
                      const auto                                recipe = bake::parseRecipe(recipeText, "recipes/image/brand-mark.toml", diagnostics);
                      if (!recipe.has_value())
                          throw speclab::core::AssertionFailure("recipe rejected", std::source_location::current());
                      const auto output = bake::run(*recipe, "recipes/image/brand-mark.toml", *recipeBytes, root, diagnostics);
                      if (!output.has_value())
                          throw speclab::core::AssertionFailure("bake failed", std::source_location::current());
                      const auto committed = bake::readFile(root / "generated/image/brand-mark/package.json");
                      if (!committed.has_value())
                          throw speclab::core::AssertionFailure("committed package unreadable", std::source_location::current());
                      const std::string_view committedText{reinterpret_cast<const char*>(committed->data()), committed->size()};
                      const auto             parsed = mdux::image::ImagePackage::parse(output->packageJson);
                      mdux::spec::Checks     checks;
                      checks.expect(output->packageJson == committedText, "package bytes match");
                      checks.expect(parsed.has_value(), "fresh package parses");
                      if (parsed.has_value())
                          checks.expect(parsed->width == 240 && parsed->height == 72, "intrinsic extent survives");
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register imageBakerRejectsReservedSidecars{
    "The image recipe cannot name an artifact file as its pixel sidecar",
    "evidence-unit",
    [] {
        return speclab::Test("image-baker-reserved-sidecar")
            .Given("reserved artifact names and their Windows-equivalent spellings", [] {})
            .When("each name is used as an image sidecar", [] {})
            .Then("package.json and report.json are rejected before baking",
                  [] {
                      namespace bake = mdux::tools::imagebake;
                      mdux::spec::Checks checks;
                      for (const std::string_view reserved : {"package.json", "report.json", "REPORT.JSON", "package.json.", "report.json "}) {
                          const std::string recipe = std::format("[package]\nid = \"fixture\"\nsource = \"fixture.qoi\"\nsidecar = \"{}\"\n", reserved);
                          std::vector<mdux::tools::cli::Diagnostic> diagnostics;
                          checks.expect(!bake::parseRecipe(recipe, "fixture.toml", diagnostics).has_value(), std::format("{} is refused", reserved));
                          checks.expect(!diagnostics.empty(), "the refusal carries a diagnostic");
                      }
                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register imageBakerRejectsSymlinkEscape{
    "The image source must resolve inside the repository root",
    "evidence-unit",
    [] {
        return speclab::Test("image-baker-symlink-escape")
            .Given("an isolated root containing a link to an external QOI", [] {})
            .When("the linked source and an in-root control are baked", [] {})
            .Then("a symlink to an external QOI is rejected before its bytes enter the report",
                  [] {
                      namespace bake          = mdux::tools::imagebake;
                      const auto      nonce   = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
                      const auto      root    = std::filesystem::temp_directory_path() / ("mdux-imagebake-root-" + nonce);
                      const auto      outside = std::filesystem::temp_directory_path() / ("mdux-imagebake-outside-" + nonce);
                      std::error_code error;
                      std::filesystem::create_directories(root, error);
                      const bool rootCreated = !error;
                      error.clear();
                      std::filesystem::create_directories(outside, error);
                      const bool outsideCreated = !error;

                      const std::filesystem::path committedSource = std::filesystem::path{MDUX_REPO_ROOT} / "recipes/image/brand-mark/brand-mark.qoi";
                      error.clear();
                      std::filesystem::copy_file(committedSource, root / "inside.qoi", error);
                      const bool insideCopied = !error;
                      error.clear();
                      std::filesystem::copy_file(committedSource, outside / "outside.qoi", error);
                      const bool outsideCopied = !error;
                      error.clear();
                      std::filesystem::create_symlink(outside / "outside.qoi", root / "escape.qoi", error);
                      const bool symlinkCreated = !error;

                      const auto codeFor = [&](std::string source) {
                          const bake::Recipe                        recipe{.id = "fixture", .source = std::move(source), .sidecar = "pixels.rgba"};
                          constexpr std::string_view                recipeText = "fixture";
                          std::vector<mdux::tools::cli::Diagnostic> diagnostics;
                          const auto                                result =
                              bake::run(recipe, "fixture.toml", std::as_bytes(std::span{recipeText.data(), recipeText.size()}), root, diagnostics);
                          return std::pair{result.has_value(), diagnostics.empty() ? std::string{"none"} : diagnostics.front().code};
                      };

                      mdux::spec::Checks checks;
                      checks.expect(rootCreated && outsideCreated && insideCopied && outsideCopied, "the isolated source fixtures were created");
                      if (symlinkCreated && rootCreated && outsideCreated && insideCopied && outsideCopied) {
                          const auto escaped = codeFor("escape.qoi");
                          const auto control = codeFor("inside.qoi");
                          checks.expect(!escaped.first && escaped.second == "IMB009", std::format("the escaping symlink is IMB009, got {}", escaped.second));
                          checks.expect(control.first && control.second == "none", std::format("the in-root control bakes, got {}", control.second));
                      }

                      std::filesystem::remove_all(root, error);
                      error.clear();
                      std::filesystem::remove_all(outside, error);
                      checks.raise();
                  })
            .Execute();
    }};
