/**
 * @brief Entry point for the shader schema and baker test executable (MduXTest framework).
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * Covers both zones this slice spans: `mdux.shader.schema` is governed and lives in MduXCore,
 * while the SPIR-V reflector and the baker are host tools permitted to throw (ADR-005). They share
 * an executable because every test here is about one artifact format, and splitting them would
 * mean two suites that must be read together to understand either.
 */

import std;
import mdux.test;

#include "../framework/MduXTest.hpp"

MDUX_TEST_MAIN("MduX Shader Tests")
