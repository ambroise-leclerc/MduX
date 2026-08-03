/**
 * @brief Entry point for the no-heap SpecLab executable (issue #63).
 *
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * Separate from ml_spec because this binary replaces the global `operator new` family. Keeping
 * that replacement out of the main suite means the other scenarios run against the ordinary
 * allocator, and a failure here names the no-heap property rather than appearing as one line among
 * two dozen unrelated ones.
 */

import std;
import speclab;

#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX ML No-Heap Spec");
}
