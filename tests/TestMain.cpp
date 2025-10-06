/**
 * @brief Main test runner for MduX unit tests
 */

#include "TestRunner.h"

import std;
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