/**
 * @brief Entry point for the mdux.font.schema SpecLab BDD executable.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone)
 * @compliance ADR-010 No on-device text shaping
 *
 * Links `MduX::Core` only, like the other governed-zone specs. That is standing evidence rather
 * than convention: `mdux.font.schema` describes an atlas and a charset without naming a Vulkan
 * handle, so a scenario that needed `MduX::MduX` to build would mean the trust-zone boundary had
 * moved and the configure-time check had missed it.
 */

import std;
import speclab;

#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX Font Spec");
}
