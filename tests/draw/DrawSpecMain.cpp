/**
 * @file DrawSpecMain.cpp
 * @brief Entry point for the mdux.draw SpecLab BDD executable.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone)
 *
 * Runs SpecLab scenarios under the rest of the suite's discovery contract: `--list-tests` prints
 * one `name<TAB>labels` line per scenario, and `--run=<name>` executes exactly one. Like
 * the MduXTest suites it replaces, draw_spec links MduX::Core only - mdux.draw contains no Vulkan
 * handle, so a scenario that needed MduX::MduX to build would mean the trust-zone boundary had
 * moved.
 */

import std;
import speclab;

#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX Draw Spec");
}
