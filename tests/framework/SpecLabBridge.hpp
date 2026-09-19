/**
 * @file SpecLabBridge.hpp
 * @brief Runs SpecLab scenarios under the test-discovery contract the rest of this suite uses.
 *
 * `cmake/MduXTestDiscoveryImpl.cmake` gives every test binary the same two-part contract:
 * `--list-tests` prints one `name<TAB>labels` line per test, and `--run=<name>` executes exactly
 * one and reports pass or fail through its exit status. That is what produces a separate CTest
 * entry per test, and what `ctest -L evidence` and `ctest -L pixel` select on.
 *
 * This header used to implement that contract itself: the scenario registry, the runner and
 * `Checks`. Since SpecLab v0.2.0 (#361, ambroise-leclerc/SpecLab#28) SpecLab provides them with
 * the same contract and byte-identical output, so the names below only forward to it. The spec
 * files are unchanged: they still write `mdux::spec::Register`, `mdux::spec::Checks` and
 * `mdux::spec::main`. `bridge_spec` keeps testing this integration, now against SpecLab's
 * implementation.
 *
 * ## State between Given, When and Then
 *
 * SpecLab's plain steps are `void()` callables, so anything a `Then` needs from a `When` has to
 * outlive both. The idiom used throughout these specs is a `std::shared_ptr` to a local state
 * struct, created inside the factory and captured by each step. SpecLab v0.2.0 also offers
 * `speclab::Test<State>`, whose steps receive a shared `State&`. With MSVC 19.44 (VS 2022) that
 * State must be declared at namespace scope, not inside a function.
 *
 * Include after `import std;` and after `import speclab;`.
 */
#pragma once

namespace mdux::spec {

using Scenario = speclab::Scenario;
using speclab::registry;

/// Registers one scenario at namespace scope: `const Register name{"...", "labels", [] { ... }};`
using Register = speclab::Register;

/// Collects several failed expectations and reports them together (see speclab::core::Checks).
using Checks = speclab::core::Checks;

/// Implements `--list-tests` and `--run=<name>`; with neither, runs everything.
inline int main(int argc, char** argv, std::string_view suiteName) {
    return speclab::runMain(argc, argv, suiteName);
}

/**
 * @brief Never called. Works around a GCC 16 modules bug.
 *
 * When the including translation unit does not itself instantiate std::println on a std::string,
 * GCC 16 rejects an unrelated implicit destructor in tests/ml/GeneratedModelTests.cpp
 * ("use of deleted function ~ModelPackage()"). This header's own runner used to provide that
 * instantiation incidentally, and one call restores it. It was bisected in
 * ambroise-leclerc/SpecLab#28: neither the new SpecLab nor the aliases above trigger the error,
 * only the loss of that local instantiation does. Remove it when GCC no longer needs it.
 */
inline void gcc16ModulesWorkaround(const std::string& text) {
    std::println(std::cout, "{}", text);
}

}  // namespace mdux::spec
