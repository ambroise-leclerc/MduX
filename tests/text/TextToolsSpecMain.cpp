/**
 * @file TextToolsSpecMain.cpp
 * @brief Entry point for the text baker host-tools SpecLab executable (issue #157).
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-010 No on-device text shaping
 *
 * Separate from text_spec, which links MduX::Core only. These scenarios exercise `mdux-textbake`'s
 * `parseRecipe()` / `run()` / `write()` / `verify()`, which live in the host-tools zone and may
 * throw - so a suite covering them has to link MduX::TextBakeLib, and keeping that out of
 * text_spec preserves text_spec's standing evidence that the governed `mdux.text.schema` reaches
 * nothing but std.
 */

import std;
import speclab;

#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX Text Tools Spec");
}
