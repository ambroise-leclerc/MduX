/**
 * @brief Entry point for the host-tools SpecLab BDD executable.
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * Runs SpecLab scenarios under the rest of the suite's discovery contract: `--list-tests` prints
 * one `name<TAB>labels` line per scenario, and `--run=<name>` executes exactly one. The tools_spec
 * target carries the tools that were converted to BDD; tools_tests keeps the pre-Wave-3 suites that
 * stayed on MduXTest.
 */

import std;
import speclab;

#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX Host Tools Spec");
}