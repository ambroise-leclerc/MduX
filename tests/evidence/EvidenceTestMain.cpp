/**
 * @file EvidenceTestMain.cpp
 * @brief Entry point for the evidence-kernel test executable (MduXTest framework).
 *
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * One main() per executable, in its own translation unit, so each new evidence module can add a
 * test file without touching this one. Registration order across files does not matter.
 */

import std;
import mdux.test;

#include "../framework/MduXTest.hpp"

MDUX_TEST_MAIN("MduX Evidence Kernel Tests")
