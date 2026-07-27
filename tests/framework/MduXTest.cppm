/**
 * @file MduXTest.cppm
 * @brief In-repo test framework runtime: registry, check recording, running, reporting.
 *
 * Macros (CHECK/REQUIRE/TEST_CASE) cannot be exported from a module - they live in
 * the companion header MduXTest.hpp, included by test files alongside `import mdux.test;`.
 * This module holds everything that is not a macro: the types the macros expand into.
 */
module;

#include <cstdio>

export module mdux.test;

import std;

export namespace mdux::test {

struct CheckOutcome {
    bool passed;
    std::string expression;
    std::string file;
    int line;
    std::string message;  ///< populated only on failure, e.g. "expected true, got false"
};

/// Thrown by a failed REQUIRE to abort the current test case without aborting the run.
struct RequireFailed {};

/// Accumulates CheckOutcomes for the test case currently executing. One instance
/// lives on the stack in Runner::runOne(); checkImpl() reaches it via current().
class TestContext {
public:
    void record(CheckOutcome outcome) { outcomes_.push_back(std::move(outcome)); }

    const std::vector<CheckOutcome>& outcomes() const { return outcomes_; }

    static TestContext*& currentSlot() {
        static TestContext* slot = nullptr;
        return slot;
    }
    static TestContext* current() { return currentSlot(); }

private:
    std::vector<CheckOutcome> outcomes_;
};

/// Called by the CHECK/REQUIRE macros. Returns the outcome so a macro could chain on
/// it if ever needed; the primary effect is recording into the current TestContext
/// and, for a failed fatal check, throwing RequireFailed to unwind to runOne().
inline bool checkImpl(bool condition, const char* exprText, const char* file, int line,
                       bool fatal, std::string message = {}) {
    TestContext* ctx = TestContext::current();
    CheckOutcome outcome{.passed = condition,
                          .expression = exprText,
                          .file = file,
                          .line = line,
                          .message = condition ? std::string{} : std::move(message)};
    if (ctx != nullptr) {
        ctx->record(outcome);
    }
    if (!condition && fatal) {
        throw RequireFailed{};
    }
    return condition;
}

using TestFn = void (*)();

struct TestCase {
    std::string name;
    std::vector<std::string> labels;
    TestFn fn;
    const char* file;
    int line;
};

/// Global registry, populated by static-initialization order via AutoRegister. One
/// Registry per test executable (translation-unit-local statics all resolve to the
/// same instance() within a single binary, which is all that's needed here).
class Registry {
public:
    static Registry& instance() {
        static Registry registry;
        return registry;
    }

    void add(TestCase testCase) { cases_.push_back(std::move(testCase)); }

    std::span<const TestCase> all() const { return cases_; }

private:
    std::vector<TestCase> cases_;
};

/// Constructing one of these at namespace scope is how TEST_CASE registers itself.
struct AutoRegister {
    AutoRegister(std::string name, std::vector<std::string> labels, TestFn fn, const char* file,
                 int line) {
        Registry::instance().add(TestCase{.name = std::move(name),
                                           .labels = std::move(labels),
                                           .fn = fn,
                                           .file = file,
                                           .line = line});
    }
};

struct TestResult {
    std::string name;
    bool passed;
    std::vector<CheckOutcome> checks;
    std::chrono::microseconds duration;
    std::string fatalError;  ///< populated if the test threw something other than a failed CHECK
};

class Runner {
public:
    TestResult runOne(const TestCase& testCase) const {
        TestContext context;
        TestContext*& slot = TestContext::currentSlot();
        TestContext* previous = slot;
        slot = &context;

        auto start = std::chrono::high_resolution_clock::now();
        std::string fatalError;
        try {
            testCase.fn();
        } catch (const RequireFailed&) {
            // Already recorded by checkImpl(); nothing further to capture.
        } catch (const std::exception& e) {
            fatalError = e.what();
        } catch (...) {
            fatalError = "unrecognized exception";
        }
        auto end = std::chrono::high_resolution_clock::now();
        slot = previous;

        bool passed = fatalError.empty() &&
                      std::ranges::all_of(context.outcomes(), [](const CheckOutcome& outcome) {
                          return outcome.passed;
                      });

        return TestResult{
            .name = testCase.name,
            .passed = passed,
            .checks = context.outcomes(),
            .duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start),
            .fatalError = fatalError,
        };
    }

    std::vector<TestResult> runAll(std::span<const TestCase> cases) const {
        std::vector<TestResult> results;
        results.reserve(cases.size());
        for (const TestCase& testCase : cases) {
            results.push_back(runOne(testCase));
        }
        return results;
    }

    /// Runs only cases whose name exactly matches `name`. Used by ctest registrations
    /// generated per test case (see cmake/MduXTestDiscovery.cmake) so `ctest -R
    /// <case>` and a direct `<exe> --run=<case>` invocation select one case.
    std::vector<TestResult> runNamed(std::span<const TestCase> cases, std::string_view name) const {
        std::vector<TestResult> results;
        for (const TestCase& testCase : cases) {
            if (testCase.name == name) {
                results.push_back(runOne(testCase));
            }
        }
        return results;
    }
};

inline bool allPassed(const std::vector<TestResult>& results) {
    return std::ranges::all_of(results, [](const TestResult& r) { return r.passed; });
}

namespace detail {

inline void jsonEscape(std::string& out, std::string_view text) {
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
}

}  // namespace detail

inline void printText(const std::vector<TestResult>& results, std::string_view suiteName) {
    std::cout << "=============================================================================\n";
    std::cout << suiteName << '\n';
    std::cout << "=============================================================================\n\n";

    for (const TestResult& result : results) {
        if (result.passed) {
            std::cout << "✅ PASS: " << result.name << " (" << result.duration.count()
                       << " µs)\n";
            continue;
        }
        std::cout << "❌ FAIL: " << result.name << '\n';
        if (!result.fatalError.empty()) {
            std::cout << "   Unhandled exception: " << result.fatalError << '\n';
        }
        for (const CheckOutcome& check : result.checks) {
            if (!check.passed) {
                std::cout << "   " << check.file << ':' << check.line << ": failed: "
                          << check.expression;
                if (!check.message.empty()) {
                    std::cout << " (" << check.message << ')';
                }
                std::cout << '\n';
            }
        }
    }

    std::size_t passed = static_cast<std::size_t>(
        std::ranges::count_if(results, [](const TestResult& r) { return r.passed; }));
    std::size_t total = results.size();

    std::cout << "\n=============================================================================\n";
    std::cout << "Total:  " << total << " tests\n";
    if (total > 0) {
        std::cout << "Passed: " << passed << " (" << (passed * 100 / total) << "%)\n";
    }
    std::cout << "Failed: " << (total - passed) << '\n';
    std::cout << "=============================================================================\n";
}

inline void printJson(const std::vector<TestResult>& results, std::string_view suiteName) {
    std::string out = "{\n  \"suite\": \"";
    detail::jsonEscape(out, suiteName);
    out += "\",\n  \"results\": [\n";

    for (std::size_t i = 0; i < results.size(); ++i) {
        const TestResult& result = results[i];
        out += "    {\n      \"name\": \"";
        detail::jsonEscape(out, result.name);
        out += "\",\n      \"passed\": ";
        out += result.passed ? "true" : "false";
        out += ",\n      \"durationMicroseconds\": " + std::to_string(result.duration.count());
        if (!result.fatalError.empty()) {
            out += ",\n      \"fatalError\": \"";
            detail::jsonEscape(out, result.fatalError);
            out += '"';
        }
        out += ",\n      \"checks\": [";
        for (std::size_t j = 0; j < result.checks.size(); ++j) {
            const CheckOutcome& check = result.checks[j];
            out += "\n        {\"passed\": ";
            out += check.passed ? "true" : "false";
            out += ", \"expression\": \"";
            detail::jsonEscape(out, check.expression);
            out += "\", \"file\": \"";
            detail::jsonEscape(out, check.file);
            out += "\", \"line\": " + std::to_string(check.line);
            if (!check.message.empty()) {
                out += ", \"message\": \"";
                detail::jsonEscape(out, check.message);
                out += '"';
            }
            out += '}';
            if (j + 1 < result.checks.size()) {
                out += ',';
            }
        }
        out += "\n      ]\n    }";
        if (i + 1 < results.size()) {
            out += ',';
        }
        out += '\n';
    }

    out += "  ]\n}\n";
    std::cout << out;
}

/// Shared entry point for every test executable built on this framework.
/// Supported arguments:
///   --list-tests        print one test name per line and exit 0 (used by CMake's
///                        mdux_discover_tests() to register one ctest per case)
///   --run=<name>         run only the named case
///   --format=json|text   select output format (default: text)
inline int runMain(int argc, char** argv, std::string_view suiteName) {
    bool jsonFormat = false;
    std::string_view onlyName;
    bool listOnly = false;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--list-tests") {
            listOnly = true;
        } else if (arg.starts_with("--run=")) {
            onlyName = arg.substr(std::string_view("--run=").size());
        } else if (arg == "--format=json") {
            jsonFormat = true;
        } else if (arg == "--format=text") {
            jsonFormat = false;
        }
    }

    std::span<const TestCase> cases = Registry::instance().all();

    if (listOnly) {
        for (const TestCase& testCase : cases) {
            std::cout << testCase.name << '\n';
        }
        return 0;
    }

    Runner runner;
    std::vector<TestResult> results =
        onlyName.empty() ? runner.runAll(cases) : runner.runNamed(cases, onlyName);

    if (results.empty() && !onlyName.empty()) {
        std::cerr << "mdux-test: no test case named '" << onlyName << "'\n";
        return 1;
    }

    if (jsonFormat) {
        printJson(results, suiteName);
    } else {
        printText(results, suiteName);
    }

    return allPassed(results) ? 0 : 1;
}

}  // namespace mdux::test
