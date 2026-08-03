/**
 * @brief Entry point for the ML host-tools SpecLab executable (issue #60).
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * Separate from ml_spec, which links MduX::Core only. These scenarios exercise the safetensors
 * reader and the architecture validator, which live in the host-tools zone and may throw - so a
 * suite covering them has to link MduX::MlBakeLib, and keeping that out of ml_spec preserves
 * ml_spec's standing evidence that the governed ML modules need nothing but std.
 */

import std;
import speclab;

#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX ML Tools Spec");
}
