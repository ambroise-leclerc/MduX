/**
 * @brief Runs SpecLab scenarios under the test-discovery contract the rest of this suite uses.
 *
 * ## Why a bridge exists at all
 *
 * `cmake/MduXTestDiscoveryImpl.cmake` gives every test binary the same two-part contract:
 * `--list-tests` prints one `name<TAB>labels` line per test, and `--run=<name>` executes exactly
 * one and reports pass or fail through its exit status. That is what produces a separate CTest
 * entry per test, and it is what `ctest -L evidence` and `ctest -L pixel` select on.
 *
 * SpecLab has its own runner and its own reporters, and knows nothing about that contract. Left
 * to itself, a SpecLab binary would be a single CTest entry - so a failure would name the binary
 * rather than the scenario, and the labels the CI steps depend on would have nowhere to live.
 * This header is the adapter, and it is deliberately thin: SpecLab still executes the scenario
 * and still decides whether it passed.
 *
 * ## Registration is an object, not a macro
 *
 * A namespace-scope `Register` runs its constructor before `main`, which is the whole mechanism.
 * That is the same reason MduXTest's `TEST_CASE` exists, minus the macro - which is the point of
 * adopting SpecLab in the first place.
 *
 * ## State between Given, When and Then
 *
 * SpecLab's steps are `void()` callables, so anything a `Then` needs from a `When` has to outlive
 * both. The idiom used throughout these specs is a `std::shared_ptr` to a local state struct,
 * created inside the factory and captured by each step. `Execute()` is called inside that same
 * factory, so the state is alive for exactly as long as the steps can run and is destroyed
 * immediately afterwards - no globals, and no ordering between scenarios.
 *
 * Include after `import std;` and after `import speclab;`.
 */
#pragma once

namespace mdux::spec {

/// One registered scenario: what CTest will call it, how it is labelled, and how to run it.
struct Scenario {
    std::string name;
    std::string labels;  ///< comma-separated, as the discovery script expects
    std::function<speclab::core::TestResult()> run;
};

/// The registry. A function-local static so registration order across translation units cannot
/// depend on the initialisation order of a namespace-scope container.
inline std::vector<Scenario>& registry() {
    static std::vector<Scenario> scenarios;
    return scenarios;
}

/// Registers one scenario at namespace scope. Construct one per scenario; the object itself is
/// never used again.
struct Register {
    Register(std::string name, std::string labels, std::function<speclab::core::TestResult()> run) {
        registry().push_back(
            Scenario{.name = std::move(name), .labels = std::move(labels), .run = std::move(run)});
    }
};

/// Collects several failed expectations and reports them together.
///
/// SpecLab's assertions all throw, so the first failure inside a `Then` ends the scenario. That is
/// right for a scenario making one behavioural claim, and wrong for the ones being converted from
/// MduXTest, where `CHECK` is deliberately soft: a test asserting six things reports all six that
/// are wrong, and the reader fixes them in one pass rather than rerunning six times.
///
/// `Checks` restores that inside a single `Then`. Record as many expectations as the claim needs,
/// then call `raise()`; nothing is thrown unless at least one failed, and what is thrown names
/// every one of them.
///
///     Checks checks;
///     checks.expect(vertex.x == 4, "x");
///     checks.expect(vertex.y == 8, "y");
///     checks.raise();
class Checks {
public:
    /// Records `condition`. `what` should name the thing being checked, not restate the operator.
    void expect(bool condition, std::string_view what,
                std::source_location where = std::source_location::current()) {
        if (!condition) {
            failures_.push_back(
                std::format("{} ({}:{})", what, fileName(where.file_name()), where.line()));
        }
    }

    /// Throws a single AssertionFailure listing every failed expectation, or returns if none did.
    void raise(std::source_location where = std::source_location::current()) const {
        if (failures_.empty()) {
            return;
        }
        std::string message =
            std::format("{} expectation(s) failed:", failures_.size());
        for (const std::string& failure : failures_) {
            message += "\n    - " + failure;
        }
        throw speclab::core::AssertionFailure(message, where);
    }

    [[nodiscard]] bool anyFailed() const noexcept { return !failures_.empty(); }

private:
    /// Just the filename: a source_location carries an absolute build path, which is noise in a
    /// message and differs between machines for the same failure.
    [[nodiscard]] static std::string_view fileName(std::string_view path) noexcept {
        const std::size_t slash = path.find_last_of("/\\");
        return slash == std::string_view::npos ? path : path.substr(slash + 1);
    }

    std::vector<std::string> failures_;
};

/// Implements `--list-tests` and `--run=<name>`; with neither, runs everything.
///
/// Returns 0 when every selected scenario passed and 1 otherwise - including when `--run` names a
/// scenario that does not exist, which is an error rather than a silent pass: a stale generated
/// CTest file would otherwise report success for a test that no longer exists.
///
/// 1 rather than a distinct code, to match `mdux::test::runMain` (tests/framework/MduXTest.cppm),
/// which also answers 1 for a missing named case. Both binaries are driven by the same CTest
/// machinery, and the message on stderr is what distinguishes the two situations for a reader.
inline int main(int argc, char** argv, std::string_view suiteName) {
    const std::span<char*> args{argv, static_cast<std::size_t>(argc)};

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view argument{args[i]};

        if (argument == "--list-tests") {
            for (const Scenario& scenario : registry()) {
                std::println(std::cout, "{}\t{}", scenario.name, scenario.labels);
            }
            return 0;
        }

        if (argument.starts_with("--run=")) {
            const std::string_view wanted = argument.substr(std::string_view{"--run="}.size());
            const auto found = std::ranges::find_if(
                registry(), [wanted](const Scenario& s) { return s.name == wanted; });
            if (found == registry().end()) {
                std::println(std::cerr, "{}: no scenario named '{}'", suiteName, wanted);
                return 1;
            }
            const speclab::core::TestResult result = found->run();
            if (!result.passed()) {
                std::println(std::cerr, "FAILED: {}\n  {}", found->name, result.message);
                if (!result.errorDetails.empty()) {
                    std::println(std::cerr, "  {}", result.errorDetails);
                }
                return 1;
            }
            return 0;
        }
    }

    // No selection: run the lot, which is what a developer invoking the binary directly wants.
    std::size_t failed = 0;
    for (const Scenario& scenario : registry()) {
        const speclab::core::TestResult result = scenario.run();
        if (result.passed()) {
            std::println(std::cout, "PASS  {}", scenario.name);
        } else {
            std::println(std::cout, "FAIL  {}\n      {}", scenario.name, result.message);
            if (!result.errorDetails.empty()) {
                std::println(std::cout, "      {}", result.errorDetails);
            }
            ++failed;
        }
    }
    std::println(std::cout, "\n{}: {} scenarios, {} failed", suiteName, registry().size(), failed);
    return failed == 0 ? 0 : 1;
}

}  // namespace mdux::spec
