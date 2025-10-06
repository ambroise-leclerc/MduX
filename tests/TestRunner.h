/**
 * @file TestRunner.h
 * @brief Common test runner for MduX unit tests
 *
 * Provides a simple test framework for running unit tests with timing
 * and detailed result reporting.
 */

#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <functional>
#include <chrono>
#include <algorithm>

namespace mdux::test {

/**
 * @brief Simple test runner for unit tests
 */
class TestRunner {
public:
    struct TestResult {
        std::string testName;
        bool passed;
        std::string errorMessage;
        std::chrono::microseconds duration;
    };

    /**
     * @brief Run a test function with timing and error handling
     *
     * @param name Test name for reporting
     * @param testFunc Test function to execute
     */
    void runTest(const std::string& name, std::function<void()> testFunc) {
        auto start = std::chrono::high_resolution_clock::now();

        try {
            testFunc();
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

            results.push_back({
                .testName = name,
                .passed = true,
                .errorMessage = "",
                .duration = duration
            });

            std::cout << "✓ PASS: " << name << " ("
                      << duration.count() << " μs)\n";
        } catch (const std::exception& e) {
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

            results.push_back({
                .testName = name,
                .passed = false,
                .errorMessage = e.what(),
                .duration = duration
            });

            std::cout << "❌ FAIL: " << name << "\n";
            std::cout << "   Error: " << e.what() << "\n";
        }
    }

    /**
     * @brief Print test summary and statistics
     */
    void printSummary() const {
        size_t passed = static_cast<size_t>(std::count_if(results.begin(), results.end(),
                                                          [](const TestResult& r) { return r.passed; }));
        size_t failed = results.size() - passed;

        std::cout << "\n=============================================================================\n";
        std::cout << "Test Summary\n";
        std::cout << "=============================================================================\n";
        std::cout << "Total:  " << results.size() << " tests\n";
        std::cout << "Passed: " << passed << " (" << (passed * 100 / results.size()) << "%)\n";
        std::cout << "Failed: " << failed << "\n";

        if (failed > 0) {
            std::cout << "\nFailed Tests:\n";
            for (const auto& result : results) {
                if (!result.passed) {
                    std::cout << "  - " << result.testName << ": "
                              << result.errorMessage << "\n";
                }
            }
        }

        // Performance statistics
        auto totalTime = std::chrono::microseconds::zero();
        for (const auto& result : results) {
            totalTime += result.duration;
        }

        std::cout << "\nPerformance:\n";
        std::cout << "  Total time: " << totalTime.count() << " μs\n";
        std::cout << "  Average:    " << (results.empty() ? 0 : totalTime.count() / static_cast<long>(results.size())) << " μs/test\n";
        std::cout << "=============================================================================\n";
    }

    /**
     * @brief Get exit code based on test results
     *
     * @return 0 if all tests passed, 1 if any failed
     */
    int getExitCode() const {
        for (const auto& result : results) {
            if (!result.passed) {
                return 1;
            }
        }
        return 0;
    }

    /**
     * @brief Get all test results
     */
    const std::vector<TestResult>& getResults() const {
        return results;
    }

    /**
     * @brief Check if all tests passed
     *
     * @return true if all tests passed, false otherwise
     */
    bool allTestsPassed() const {
        return std::all_of(results.begin(), results.end(),
                          [](const TestResult& r) { return r.passed; });
    }

private:
    std::vector<TestResult> results;
};

/**
 * @brief Simple test runner for basic boolean tests (legacy compatibility)
 */
class SimpleTestRunner {
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

private:
    int passed = 0;
    int failed = 0;
};

} // namespace mdux::test
