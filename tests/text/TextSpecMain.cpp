/**
 * @file TextSpecMain.cpp
 * @brief Entry point for the mdux.text.schema SpecLab BDD executable.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone)
 * @compliance ADR-010 No on-device text shaping
 *
 * Runs SpecLab scenarios under the rest of the suite's discovery contract: `--list-tests` prints
 * one `name<TAB>labels` line per scenario, and `--run=<name>` executes exactly one. Like the
 * other governed-zone specs (shader, draw, ml), text_spec links `MduX::Core` only - mdux.text
 * contains no Vulkan handle, so a scenario that needed `MduX::MduX` to build would mean the
 * trust-zone boundary had moved.
 */

import std;
import speclab;

#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX Text Spec");
}
