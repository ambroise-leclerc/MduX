/**
 * @brief Entry point for the host-tools test executable (MduXTest framework).
 *
 * @compliance ADR-004 Trust zones in C++ (host-tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * Separate from evidence_tests because these tests exercise the host-tools zone, which links
 * MduXToolsCommon rather than MduX::Core and is permitted to throw (ADR-005).
 */

import std;
import mdux.test;

#include "../framework/MduXTest.hpp"

MDUX_TEST_MAIN("MduX Host Tools Tests")
