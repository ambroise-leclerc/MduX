/**
 * @file MduXTest.hpp
 * @brief TEST_CASE/CHECK/REQUIRE macros for the MduXTest framework.
 *
 * A C++23 module cannot export a preprocessor macro, so the macros that need
 * __FILE__/__LINE__/stringification live here as a plain header. Pair with
 * `import mdux.test;` for the runtime types these macros expand into.
 *
 * Usage:
 *   import std;
 *   import mdux.test;
 *   import mdux.vulkansc.memory;
 *   #include "MduXTest.hpp"
 *
 *   TEST_CASE("MemoryPoolConfiguration Validity") {
 *       mdux::vulkansc::MemoryPoolConfiguration config;
 *       config.maxTotalMemory = 128 * 1024 * 1024;
 *       config.maxTotalAllocations = 100;
 *       REQUIRE(config.isValid());
 *   }
 *
 *   MDUX_TEST_MAIN("MemoryPoolManager Tests")
 */
#pragma once

#define MDUX_TEST_CONCAT_INNER(a, b) a##b
#define MDUX_TEST_CONCAT(a, b) MDUX_TEST_CONCAT_INNER(a, b)

/// Defines a test case function and registers it at static-init time. `labels...`
/// is an optional trailing list of string labels (e.g. "evidence", "pixel") that
/// CMake discovery propagates them to the corresponding CTest entry.
#define TEST_CASE(name, ...)                                                                     \
    static void MDUX_TEST_CONCAT(mduxTestFn_, __LINE__)();                                       \
    namespace {                                                                                  \
    ::mdux::test::AutoRegister MDUX_TEST_CONCAT(mduxTestReg_, __LINE__){                          \
        (name), {__VA_ARGS__}, &MDUX_TEST_CONCAT(mduxTestFn_, __LINE__), __FILE__, __LINE__};     \
    }                                                                                             \
    static void MDUX_TEST_CONCAT(mduxTestFn_, __LINE__)()

/// Records the outcome and continues the test case even if it fails.
#define CHECK(expr) ::mdux::test::checkImpl(static_cast<bool>(expr), #expr, __FILE__, __LINE__, false)

/// Records the outcome and, on failure, aborts the current test case (the
/// remaining body of the TEST_CASE does not run) without aborting the run.
#define REQUIRE(expr) ::mdux::test::checkImpl(static_cast<bool>(expr), #expr, __FILE__, __LINE__, true)

/// Same as CHECK/REQUIRE but with an explicit failure message, e.g.
/// REQUIRE_MESSAGE(x == y, "expected " + std::to_string(y)).
#define CHECK_MESSAGE(expr, msg) \
    ::mdux::test::checkImpl(static_cast<bool>(expr), #expr, __FILE__, __LINE__, false, (msg))
#define REQUIRE_MESSAGE(expr, msg) \
    ::mdux::test::checkImpl(static_cast<bool>(expr), #expr, __FILE__, __LINE__, true, (msg))

/// Creates a named, single-pass scope inside a test case. Unlike Catch2 sections,
/// SECTION-lite does not rerun the surrounding test for each sibling section.
#define SECTION(name)                                                                            \
    for ([[maybe_unused]] bool MDUX_TEST_CONCAT(mduxTestSection_, __LINE__) =                    \
             ((void)(name), true);                                                               \
         MDUX_TEST_CONCAT(mduxTestSection_, __LINE__);                                            \
         MDUX_TEST_CONCAT(mduxTestSection_, __LINE__) = false)

/// Defines main() for a test executable. Call once per executable, after every
/// TEST_CASE in that translation unit (or in any translation unit linked into it -
/// registration order across files does not matter).
#define MDUX_TEST_MAIN(suiteName)                                                                 \
    int main(int argc, char** argv) { return ::mdux::test::runMain(argc, argv, (suiteName)); }
