/**
 * @brief Main test runner for MduX compliance tests
 */

#include <cassert>
#include <iostream>
#include <mdux/mdux.hpp>

// Simple test framework for compliance testing
class ComplianceTestRunner {
private:
    int passed = 0;
    int failed = 0;

public:
    void runTest(const char* name, bool result) {
        if (result) {
            std::cout << "[PASS] " << name << std::endl;
            passed++;
        } else {
            std::cout << "[FAIL] " << name << std::endl;
            failed++;
        }
    }

    int getExitCode() const {
        std::cout << "\nCompliance Test Results: " << passed << " passed, " << failed << " failed"
                  << std::endl;
        return failed > 0 ? 1 : 0;
    }
};

// Forward declarations of test functions
bool testRegulatoryCompliance();

int main() {
    std::cout << "Running MduX Medical Device Compliance Tests..." << std::endl;
    std::cout << "Testing compliance with IEC 62304 and IEC 62366 standards" << std::endl;

    ComplianceTestRunner runner;

    // Run compliance tests
    runner.runTest("Regulatory Compliance Test", testRegulatoryCompliance());

    return runner.getExitCode();
}