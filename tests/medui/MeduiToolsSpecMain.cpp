/**
 * @file MeduiToolsSpecMain.cpp
 * @brief Entry point for the .medui compiler host-tools SpecLab executable (issue #191).
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * Links MduX::MeduiLib, the host-tools-zone target. Separate from any governed medui suite for the
 * reason text_tools_spec is separate from text_spec: keeping the two apart is what preserves the
 * governed suite's standing evidence that it reaches nothing but std. There is no governed medui
 * module yet - mdux.medui.schema is #197 and the runtime #199 - so today this suite is the only
 * one, and it is named for the zone it links so that the split is already in place when the other
 * arrives.
 */

import std;
import speclab;

#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX MedUI Tools Spec");
}
