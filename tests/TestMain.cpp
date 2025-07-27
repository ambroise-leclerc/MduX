/**
 * @brief Main test runner for MduX unit tests
 */

import std;
import mdux;

// Simple test framework for basic testing
class TestRunner {
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
        std::cout << "\nTest Results: " << passed << " passed, " << failed << " failed"
                  << std::endl;
        return failed > 0 ? 1 : 0;
    }
};

// Forward declarations of test functions
bool testVersion();
bool testCompliance();

int main() {
    std::cout << "Running MduX Unit Tests..." << std::endl;

    TestRunner runner;

    // Run tests
    runner.runTest("Version Test", testVersion());
    runner.runTest("Compliance Test", testCompliance());

    return runner.getExitCode();
}