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
    std::function<speclab::TestResult()> run;
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
    Register(std::string name, std::string labels, std::function<speclab::TestResult()> run) {
        registry().push_back(
            Scenario{.name = std::move(name), .labels = std::move(labels), .run = std::move(run)});
    }
};

/// Implements `--list-tests` and `--run=<name>`; with neither, runs everything.
///
/// Returns 0 when every selected scenario passed and 1 otherwise. A `--run` naming a scenario
/// that does not exist is an error rather than a silent pass: a stale generated CTest file would
/// otherwise report success for a test that no longer exists.
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
                return 2;
            }
            const speclab::TestResult result = found->run();
            if (!result.passed()) {
                std::println(std::cerr, "FAILED: {}\n  {}", found->name, result.message);
                return 1;
            }
            return 0;
        }
    }

    // No selection: run the lot, which is what a developer invoking the binary directly wants.
    std::size_t failed = 0;
    for (const Scenario& scenario : registry()) {
        const speclab::TestResult result = scenario.run();
        if (result.passed()) {
            std::println(std::cout, "PASS  {}", scenario.name);
        } else {
            std::println(std::cout, "FAIL  {}\n      {}", scenario.name, result.message);
            ++failed;
        }
    }
    std::println(std::cout, "\n{}: {} scenarios, {} failed", suiteName, registry().size(), failed);
    return failed == 0 ? 0 : 1;
}

}  // namespace mdux::spec
