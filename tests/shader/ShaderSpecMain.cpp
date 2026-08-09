/**
 * @file ShaderSpecMain.cpp
 * @brief Entry point for the shader schema and baker SpecLab BDD executable.
 *
 * @compliance ADR-004 Trust zones in C++ (governed + host-tools zones)
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * Runs SpecLab scenarios under the rest of the suite's discovery contract: `--list-tests` prints
 * one `name<TAB>labels` line per scenario, and `--run=<name>` executes exactly one. The shader_spec
 * target carries the suites converted to BDD (issue #141): the governed `mdux.shader.schema`, the
 * host-only SPIR-V reflector and baker, and the generated-module consumers.
 */

import std;
import speclab;

#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX Shader Spec");
}
