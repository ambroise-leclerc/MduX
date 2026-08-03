/**
 * @brief Proves the SpecLab integration itself, before any suite is written against it.
 *
 * This file exists because the bridge has more moving parts than the tests it will carry: a
 * dependency resolved by CPM, a named module imported into a test target, namespace-scope
 * registration running before `main`, and a discovery contract implemented by hand. A failure in
 * any of those would otherwise surface as a converted suite that mysteriously does not run - and
 * "no tests were discovered" is a green CTest run unless something asserts otherwise.
 *
 * So the first scenario is deliberately trivial, and the second one deliberately fails when the
 * bridge reports a failing scenario as passing. Together they check the two directions that
 * matter: a passing scenario is reported as passing, and SpecLab's assertions actually throw.
 */
import std;
import speclab;
import mdux.core.units;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace core = mdux::core;

const mdux::spec::Register arithmetic{
    "The SpecLab bridge runs a passing scenario", "evidence-unit", [] {
        // Shared state, as the bridge header describes: created here, captured by each step,
        // and destroyed when Execute() returns.
        struct State {
            int sum{0};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("bridge-passing")
            .Given("two numbers", [state] { state->sum = 0; })
            .When("they are added", [state] { state->sum = 2 + 3; })
            .Then("the sum is five",
                  [state] {
                      if (state->sum != 5) {
                          throw speclab::AssertionFailure("2 + 3 did not produce 5",
                                                          std::source_location::current());
                      }
                  })
            .Execute();
    }};

const mdux::spec::Register assertionsFire{
    "SpecLab assertions fail a scenario rather than being ignored", "evidence-unit", [] {
        // A scenario whose Then throws is expected to come back not-passed. If SpecLab ever
        // swallowed that, every converted suite would pass unconditionally - which is the single
        // most dangerous way for a test framework to be wrong, so it is checked directly.
        struct State {
            bool threw{false};
        };
        auto state = std::make_shared<State>();

        const speclab::TestResult inner =
            speclab::Test("bridge-inner-failure")
                .Given("a scenario that will fail", [] {})
                .When("its assertion is evaluated", [] {})
                .Then("it throws",
                      [] {
                          throw speclab::AssertionFailure("deliberate",
                                                          std::source_location::current());
                      })
                .Execute();
        state->threw = !inner.passed();

        return speclab::Test("bridge-assertion-propagates")
            .Given("a deliberately failing inner scenario", [] {})
            .When("it has been executed", [] {})
            .Then("SpecLab reports it as not passed",
                  [state] {
                      if (!state->threw) {
                          throw speclab::AssertionFailure(
                              "a throwing Then was reported as passing",
                              std::source_location::current());
                      }
                  })
            .Execute();
    }};

const mdux::spec::Register governedTypesReachable{
    "A governed MduX type is usable from a SpecLab scenario", "evidence-unit", [] {
        // The integration has to work with MduX's own modules imported alongside SpecLab's, on
        // the same compiler and the same `import std`. That combination is what GCC 15 could not
        // do, and is why this project's floor is GCC 16.
        struct State {
            core::Rect rect{};
        };
        auto state = std::make_shared<State>();

        return speclab::Test("bridge-governed-types")
            .Given("a rectangle in pixels",
                   [state] {
                       state->rect = core::Rect{.x = 4, .y = 8, .width = 16, .height = 32};
                   })
            .When("its extent is read", [] {})
            .Then("the members are what was written",
                  [state] {
                      if (state->rect.width != 16 || state->rect.height != 32) {
                          throw speclab::AssertionFailure("Rect did not round-trip",
                                                          std::source_location::current());
                      }
                  })
            .Execute();
    }};

}  // namespace

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX SpecLab Bridge");
}
