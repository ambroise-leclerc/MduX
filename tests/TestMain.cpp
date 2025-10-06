/**
 * @brief Main test runner for MduX unit tests
 */

// Include all headers before module imports to avoid GCC 15 conflicts
#include "TestRunner.h"

// Use module imports for MduX modules only (not import std; due to GCC 15 issues)
import mdux;

using mdux::test::SimpleTestRunner;

// Forward declarations of test functions
bool testVersion();
bool testCompliance();

int main() {
    std::cout << "Running MduX Unit Tests..." << std::endl;

    SimpleTestRunner runner;

    // Run tests
    runner.runTest("Version Test", testVersion());
    runner.runTest("Compliance Test", testCompliance());

    return runner.getExitCode();
}