/**
 * @file ScreenNoHeapSpecMain.cpp
 * @brief Entry point for the screen runtime's no-heap executable (issue #199).
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone)
 *
 * Separate from medui_spec because this binary replaces the global `operator new` family, exactly as
 * ml_noheap_spec is separate from ml_spec. Keeping the replacement out of the main suite means the
 * other scenarios run against the ordinary allocator, and a failure here names the no-heap property
 * rather than appearing as one line among two dozen unrelated ones.
 */

import std;
import speclab;

#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX Screen Runtime No-Heap Spec");
}
