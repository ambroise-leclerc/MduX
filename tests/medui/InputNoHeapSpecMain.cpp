/**
 * @file InputNoHeapSpecMain.cpp
 * @brief Entry point for the input model's no-heap executable (#316).
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone)
 * @compliance ADR-018 Bounded input, application update order and action policy
 *
 * Separate from medui_spec and from medui_noheap_spec because this binary replaces the global
 * `operator new` family, and that replacement may be defined in exactly one translation unit per
 * binary. `ScreenNoHeapTests.cpp` already owns it in medui_noheap_spec, so the event queue and the
 * field editor get their own binary rather than sharing.
 */

import std;
import speclab;

#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX Input Model No-Heap Spec");
}
