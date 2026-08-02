/**
 * @brief Entry point for the adapter-zone renderer test executable (MduXTest framework).
 *
 * @compliance ADR-004 Trust zones in C++ (adapter zone)
 * @compliance ADR-005 Error handling and exceptions policy
 *
 * Separate from draw_tests because these link MduX::MduX and therefore Vulkan, where mdux.draw is
 * governed and must build against MduX::Core alone. Keeping the two suites in different binaries
 * is itself a check that the boundary holds.
 */

import std;
import mdux.test;

#include "../framework/MduXTest.hpp"

MDUX_TEST_MAIN("MduX Renderer Tests")
