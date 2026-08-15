/**
 * @file SharedConformanceTests.cpp
 * @brief The pinned `Compliatory/MedUI` conformance cases, run against this parser.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-011 The deterministic `.medui` compile boundary
 *
 * Every expectation is read from the pinned checkout's `case.json`, never from a copy in this
 * file. A hand-written copy is a second source of truth: the shared contract can change an
 * expected code or position and a copy keeps passing, which is the one thing pinning a contract
 * is supposed to prevent.
 *
 * `MEDUI-DEC-005` says a consumer "must not silently skip a claimed phase". Two rules follow:
 *
 * - CI has the checkout, so a missing one there is a failure rather than a skip. A developer
 *   without it gets a notice on stderr; `CI` in the environment turns that into a hard failure,
 *   so the run that produces evidence can never be vacuous.
 * - A capability named in `medui-conformance.toml` must be one this file can actually observe,
 *   and must be backed by a case that really executed. Claiming a phase with no adapter fails
 *   here instead of passing quietly.
 *
 * Only the parse phase is observable: `md::parse` answers acceptance, codes and positions.
 * Claiming `semantics`, `layout` or `safety` additionally needs the resolver (#193), the solver
 * (#194) and the golden pass (#196), plus a convention for the `inputs` member the case schema
 * declares and no case yet uses — so a claim to any of them is rejected below rather than
 * silently evaluated by a parser that cannot see those phases.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.evidence.json;
import mdux.tools.cli;
import mdux.tools.medui.diagnostics;
import mdux.tools.medui.parser;
import mdux.tools.toml;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace md = mdux::tools::medui;
namespace cli = mdux::tools::cli;
namespace json = mdux::evidence::json;
namespace toml = mdux::tools::toml;

/// Phases this file can genuinely observe. Anything else in `capabilities` is a claim with no
/// adapter behind it.
constexpr std::array<std::string_view, 1> runnableCapabilities{"syntax"};

[[noreturn]] void fail(std::string message,
                       std::source_location where = std::source_location::current()) {
    throw speclab::core::AssertionFailure(std::move(message), where);
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996) // Test-only environment lookup; no mutable buffer is involved.
#endif
[[nodiscard]] const char* env(const char* name) { return std::getenv(name); }
#ifdef _MSC_VER
#pragma warning(pop)
#endif

[[nodiscard]] bool isSet(const char* name) {
    const char* value = env(name);
    return value != nullptr && *value != '\0';
}

[[nodiscard]] std::string readFile(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) { fail(std::format("could not open {}", path.generic_string())); }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// ---------------------------------------------------------------------------
// medui-conformance.toml
// ---------------------------------------------------------------------------

struct Manifest {
    std::string commit;
    std::vector<std::string> capabilities;
};

[[nodiscard]] Manifest manifest() {
    const std::filesystem::path path =
        std::filesystem::path{MDUX_REPO_ROOT} / "medui-conformance.toml";
    const std::string text = readFile(path);
    const toml::Document document = toml::parse(text);
    const toml::Table& root = document.root();

    Manifest result{.commit = root.require("commit").asString(),
                    .capabilities = root.require("capabilities").asStringArray()};

    const bool wellFormed =
        result.commit.size() == 40 &&
        std::ranges::all_of(result.commit, [](unsigned char c) { return std::isxdigit(c) != 0; });
    if (!wellFormed) {
        fail(std::format("{} must pin a 40-character commit SHA, got '{}'", path.generic_string(),
                         result.commit));
    }
    if (result.capabilities.empty()) {
        // An empty claim would make this whole suite assert nothing while still looking green.
        fail(std::format("{} claims no capabilities, so nothing would be checked",
                         path.generic_string()));
    }
    return result;
}

// ---------------------------------------------------------------------------
// conformance/**/case.json
// ---------------------------------------------------------------------------

struct ExpectedDiagnostic {
    std::string code;
    std::size_t line{0};
    std::size_t column{0};
};

struct Case {
    std::string id;
    std::string phase;
    std::filesystem::path source;
    bool valid{false};
    std::vector<ExpectedDiagnostic> diagnostics;
};

[[nodiscard]] const json::Value& member(const json::Value& object, std::string_view key,
                                        const std::filesystem::path& path) {
    const json::Value* found = object.find(key);
    if (found == nullptr) {
        fail(std::format("{}: missing required member '{}'", path.generic_string(), key));
    }
    return *found;
}

[[nodiscard]] std::string requireString(const json::Value& object, std::string_view key,
                                        const std::filesystem::path& path) {
    const auto text = member(object, key, path).asString();
    if (!text) { fail(std::format("{}: member '{}' is not a string", path.generic_string(), key)); }
    return std::string{*text};
}

[[nodiscard]] std::size_t requirePosition(const json::Value& object, std::string_view key,
                                          const std::filesystem::path& path) {
    const auto number = member(object, key, path).asInt();
    if (!number) {
        fail(std::format("{}: member '{}' is not an integer", path.generic_string(), key));
    }
    if (*number < 0) {
        fail(std::format("{}: member '{}' is negative", path.generic_string(), key));
    }
    return static_cast<std::size_t>(*number);
}

/// `Value::elements()` yields an empty span for anything that is not an array, so a `diagnostics`
/// member of the wrong shape would silently read as "no expectations" and the case would assert
/// almost nothing. Check the kind instead of trusting the span.
[[nodiscard]] std::span<const json::Value> requireArray(const json::Value& object,
                                                        std::string_view key,
                                                        const std::filesystem::path& path) {
    const json::Value& found = member(object, key, path);
    if (found.kind() != json::Value::Kind::Array) {
        fail(std::format("{}: member '{}' is not an array", path.generic_string(), key));
    }
    return found.elements();
}

[[nodiscard]] bool requireBool(const json::Value& object, std::string_view key,
                               const std::filesystem::path& path) {
    const auto flag = member(object, key, path).asBool();
    if (!flag) { fail(std::format("{}: member '{}' is not a boolean", path.generic_string(), key)); }
    return *flag;
}

[[nodiscard]] Case readCase(const std::filesystem::path& path) {
    const std::string text = readFile(path);
    const auto document = json::parse(text);
    if (!document) { fail(std::format("{}: is not valid JSON", path.generic_string())); }

    Case result;
    result.id = requireString(*document, "id", path);
    result.phase = requireString(*document, "phase", path);

    const std::string source = requireString(*document, "source", path);
    if (source.contains('/') || !source.ends_with(".medui")) {
        fail(std::format("{}: source must be a sibling .medui file, got '{}'",
                         path.generic_string(), source));
    }
    result.source = path.parent_path() / source;

    // `case.schema.json` binds the phase to the directory holding the case, so a case filed under
    // the wrong phase is a contract error rather than something to route around.
    const std::string directoryPhase = path.parent_path().parent_path().filename().string();
    if (result.phase != directoryPhase) {
        fail(std::format("{}: declares phase '{}' but sits under '{}'", path.generic_string(),
                         result.phase, directoryPhase));
    }

    const json::Value& expected = member(*document, "expected", path);
    result.valid = requireBool(expected, "valid", path);
    for (const json::Value& entry : requireArray(expected, "diagnostics", path)) {
        result.diagnostics.push_back({.code = requireString(entry, "code", path),
                                      .line = requirePosition(entry, "line", path),
                                      .column = requirePosition(entry, "column", path)});
    }
    return result;
}

[[nodiscard]] std::vector<Case> loadCases(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> paths;
    const std::filesystem::path directory = root / "conformance";
    if (!std::filesystem::is_directory(directory)) {
        fail(std::format("{} has no conformance/ directory; is MEDUI_CONFORMANCE_DIR pointing at "
                         "a Compliatory/MedUI checkout?",
                         root.generic_string()));
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator{directory}) {
        if (entry.is_regular_file() && entry.path().filename() == "case.json") {
            paths.push_back(entry.path());
        }
    }
    std::ranges::sort(paths);

    std::vector<Case> cases;
    cases.reserve(paths.size());
    for (const std::filesystem::path& path : paths) { cases.push_back(readCase(path)); }
    return cases;
}

// ---------------------------------------------------------------------------
// Running one case
// ---------------------------------------------------------------------------

[[nodiscard]] std::string join(const std::vector<std::string>& parts) {
    std::string joined;
    for (const std::string& part : parts) {
        if (!joined.empty()) { joined += ", "; }
        joined += part;
    }
    return joined;
}

[[nodiscard]] std::string codesOf(const std::vector<cli::Diagnostic>& diagnostics) {
    if (diagnostics.empty()) { return "no diagnostics"; }
    std::string joined;
    for (const cli::Diagnostic& diagnostic : diagnostics) {
        if (!joined.empty()) { joined += ", "; }
        joined += std::format("{} at {}:{}", diagnostic.code, diagnostic.line, diagnostic.column);
    }
    return joined;
}

void runCase(const Case& item, mdux::spec::Checks& checks) {
    const std::string source = readFile(item.source);
    const md::ParseResult result = md::parse(source, item.source.filename().string());

    if (item.valid) {
        checks.expect(result.ok(), std::format("{}: accepted, got {}", item.id,
                                               codesOf(result.diagnostics)));
        return;
    }

    checks.expect(!result.ok(),
                  std::format("{}: rejected, but the parser accepted it", item.id));

    for (const ExpectedDiagnostic& expected : item.diagnostics) {
        const auto match = std::ranges::find_if(
            result.diagnostics,
            [&expected](const cli::Diagnostic& d) { return d.code == expected.code; });
        const bool reported = match != result.diagnostics.end();
        checks.expect(reported, std::format("{}: {} reported, got {}", item.id, expected.code,
                                            codesOf(result.diagnostics)));
        if (reported) {
            checks.expect(match->line == expected.line && match->column == expected.column,
                          std::format("{}: {} at {}:{}, got {}:{}", item.id, expected.code,
                                      expected.line, expected.column, match->line, match->column));
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------

const mdux::spec::Register claimsNameAPhaseThisSuiteCanObserve{
    "Every claimed capability is one this suite can actually check",
    "evidence-unit",
    [] {
        return speclab::Test("medui-shared-claims-are-observable")
            .Given("the capabilities named in medui-conformance.toml", [] {})
            .When("they are compared with what this suite can observe", [] {})
            .Then("no phase is claimed without an adapter behind it", [] {
                mdux::spec::Checks checks;
                for (const std::string& capability : manifest().capabilities) {
                    checks.expect(std::ranges::find(runnableCapabilities, capability) != runnableCapabilities.end(),
                                  std::format("'{}' is observable here; add the adapter that "
                                              "checks that phase before claiming it",
                                              capability));
                }
                checks.raise();
            })
            .Execute();
    }};

const mdux::spec::Register pinnedCasesPass{
    "The parser satisfies every pinned case in a claimed phase",
    "evidence-unit",
    [] {
        return speclab::Test("medui-shared-conformance")
            .Given("the exact checkout named by medui-conformance.toml", [] {})
            .When("each case in a claimed phase is parsed", [] {})
            .Then("acceptance, codes and positions match the pinned expectations", [] {
                mdux::spec::Checks checks;
                const Manifest pinned = manifest();

                const char* root = env("MEDUI_CONFORMANCE_DIR");
                if (root == nullptr || *root == '\0') {
                    // Never a silent skip: in CI this is the failure that keeps the claim honest,
                    // and offline it says out loud that nothing shared was checked.
                    checks.expect(!isSet("CI"),
                                  "MEDUI_CONFORMANCE_DIR is set in CI, so the capabilities "
                                  "claimed in medui-conformance.toml are actually substantiated");
                    checks.raise();
                    std::cerr << "MedUI conformance: SKIPPED - set MEDUI_CONFORMANCE_DIR to a "
                                 "checkout of the pinned commit to run the shared cases locally.\n";
                    return;
                }

                const std::vector<Case> cases = loadCases(std::filesystem::path{root});
                checks.expect(!cases.empty(), "the pinned checkout contains conformance cases");

                std::set<std::string> executed;
                std::vector<std::string> unclaimed;
                for (const Case& item : cases) {
                    if (std::ranges::find(pinned.capabilities, item.phase) != pinned.capabilities.end()) {
                        runCase(item, checks);
                        executed.insert(item.phase);
                    } else {
                        unclaimed.push_back(std::format("{} ({})", item.id, item.phase));
                    }
                }

                for (const std::string& capability : pinned.capabilities) {
                    checks.expect(executed.contains(capability),
                                  std::format("capability '{}' is backed by a case that ran; "
                                              "either the pin is wrong or the claim is unsupported",
                                              capability));
                }

                std::cerr << std::format(
                    "MedUI conformance: {} case(s) at {}; unclaimed and not asserted: {}\n",
                    cases.size(), pinned.commit,
                    unclaimed.empty() ? std::string{"none"} : join(unclaimed));
                checks.raise();
            })
            .Execute();
    }};
