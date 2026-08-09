/**
 * @file MlSpecMain.cpp
 * @brief Entry point for the mdux.ml SpecLab BDD executable.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone)
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * Runs SpecLab scenarios under the rest of the suite's discovery contract: `--list-tests` prints
 * one `name<TAB>labels` line per scenario, and `--run=<name>` executes exactly one. Links
 * MduX::Core only - the ML schema, kernels and runtime are governed modules, so a scenario that
 * needed MduX::MduX to build one would mean the trust-zone boundary had moved.
 */

import std;
import speclab;

#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX ML Spec");
}
