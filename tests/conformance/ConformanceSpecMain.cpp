/**
 * @file ConformanceSpecMain.cpp
 * @brief Entry point for the shared MedUI observation-profile conformance suite (#314).
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-014 What rendered-truth verification checks, and what it cannot
 *
 * Links `MduX::Core` (for `mdux.verify`'s exported rendered-check arithmetic) and the tools JSON/
 * TOML readers - no Vulkan, no compiler. The scenarios run MedUI's `MEDUI-PROFILE-RENDERED` vectors
 * and `conformance/contracts` manifest cases from the pinned checkout named by
 * `medui-conformance.toml`. Kept separate from `verify_spec` (which needs a framebuffer) and
 * `medui_tools_spec` (which links the compiler): this suite is pure data over one shared header.
 */

import std;
import speclab;

#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX MedUI Conformance Spec");
}
