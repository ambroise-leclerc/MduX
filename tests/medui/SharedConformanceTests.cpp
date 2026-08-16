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
 * Syntax and semantics are observable: `md::parse` answers syntax acceptance, and `md::analyze`
 * checks the portable theme-token and per-locale key views in semantic case inputs. Claiming
 * `layout` or `safety` still needs the solver (#194) and golden pass (#196), so a claim to either
 * is rejected below rather than silently evaluated by a stage that cannot see it.
 */

import std;
import speclab;
import mdux.core.result;
import mdux.evidence.json;
import mdux.text.schema;
import mdux.tools.cli;
import mdux.tools.medui.diagnostics;
import mdux.tools.medui.parser;
import mdux.tools.medui.semantic;
import mdux.tools.toml;

#include "../framework/SpecLabBridge.hpp"

namespace {

namespace md   = mdux::tools::medui;
namespace cli  = mdux::tools::cli;
namespace json = mdux::evidence::json;
namespace toml = mdux::tools::toml;

/// Phases this file can genuinely observe. Anything else in `capabilities` is a claim with no
/// adapter behind it.
constexpr std::array<std::string_view, 2> runnableCapabilities{"syntax", "semantics"};

[[noreturn]] void fail(std::string message, std::source_location where = std::source_location::current()) {
    throw speclab::core::AssertionFailure(std::move(message), where);
}

[[nodiscard]] const char* env(const char* name) {
    return std::getenv(name);
}

[[nodiscard]] bool isSet(const char* name) {
    const char* value = env(name);
    return value != nullptr && *value != '\0';
}

[[nodiscard]] std::string readFile(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        fail(std::format("could not open {}", path.generic_string()));
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

[[nodiscard]] std::string trim(std::string value) {
    const auto isWhitespace = [](unsigned char c) {
        return std::isspace(c) != 0;
    };
    const auto first = std::ranges::find_if_not(value, isWhitespace);
    const auto last  = std::ranges::find_if_not(value | std::views::reverse, isWhitespace).base();
    if (first >= last) {
        return {};
    }
    return std::string{first, last};
}

void rejectUnknownMembers(const json::Value&                      object,
                          std::initializer_list<std::string_view> known,
                          std::string_view                        context,
                          const std::filesystem::path&            path) {
    for (const json::Member& entry : object.members()) {
        if (std::ranges::find(known, entry.key) == known.end()) {
            fail(std::format("{}: unknown {} member '{}'; shared inputs are rejected unless this "
                             "adapter consumes them",
                             path.generic_string(),
                             context,
                             entry.key));
        }
    }
}

[[nodiscard]] bool isCommitSha(std::string_view value) {
    return value.size() == 40 && std::ranges::all_of(value, [](unsigned char c) {
               return std::isxdigit(c) != 0;
           });
}

[[nodiscard]] std::filesystem::path resolveGitDirectory(const std::filesystem::path& root) {
    const std::filesystem::path dotGit = root / ".git";
    if (std::filesystem::is_directory(dotGit)) {
        return dotGit;
    }
    if (!std::filesystem::is_regular_file(dotGit)) {
        fail(std::format("{} has no .git metadata; cannot verify the checked-out MedUI revision", root.generic_string()));
    }

    constexpr std::string_view prefix{"gitdir: "};
    const std::string          metadata = trim(readFile(dotGit));
    if (!metadata.starts_with(prefix)) {
        fail(std::format("{} has malformed gitdir metadata", dotGit.generic_string()));
    }
    std::filesystem::path directory{metadata.substr(prefix.size())};
    if (directory.is_relative()) {
        directory = root / directory;
    }
    return directory.lexically_normal();
}

[[nodiscard]] std::filesystem::path resolveCommonGitDirectory(const std::filesystem::path& gitDirectory) {
    const std::filesystem::path marker = gitDirectory / "commondir";
    if (!std::filesystem::is_regular_file(marker)) {
        return gitDirectory;
    }

    std::filesystem::path common{trim(readFile(marker))};
    if (common.is_relative()) {
        common = gitDirectory / common;
    }
    return common.lexically_normal();
}

[[nodiscard]] std::string checkoutRevision(const std::filesystem::path& root) {
    const std::filesystem::path gitDirectory = resolveGitDirectory(root);
    const std::string           head         = trim(readFile(gitDirectory / "HEAD"));
    if (isCommitSha(head)) {
        return head;
    }

    constexpr std::string_view prefix{"ref: "};
    if (!head.starts_with(prefix)) {
        fail(std::format("{} has malformed HEAD metadata", gitDirectory.generic_string()));
    }
    const std::string reference = head.substr(prefix.size());
    if (!reference.starts_with("refs/") || reference.contains("..") || reference.contains('\\')) {
        fail(std::format("{} names an unsafe HEAD reference '{}'", gitDirectory.generic_string(), reference));
    }

    const std::filesystem::path commonDirectory = resolveCommonGitDirectory(gitDirectory);
    for (const std::filesystem::path& directory : {gitDirectory, commonDirectory}) {
        const std::filesystem::path looseReference = directory / reference;
        if (std::filesystem::is_regular_file(looseReference)) {
            const std::string revision = trim(readFile(looseReference));
            if (isCommitSha(revision)) {
                return revision;
            }
            fail(std::format("{} does not contain a commit SHA", looseReference.generic_string()));
        }
    }

    const std::filesystem::path packedReferences = commonDirectory / "packed-refs";
    if (std::filesystem::is_regular_file(packedReferences)) {
        std::istringstream lines{readFile(packedReferences)};
        for (std::string line; std::getline(lines, line);) {
            const std::size_t separator = line.find(' ');
            if (separator != std::string::npos && line.substr(separator + 1) == reference) {
                const std::string revision = line.substr(0, separator);
                if (isCommitSha(revision)) {
                    return revision;
                }
                fail(std::format("{} has a malformed entry for '{}'", packedReferences.generic_string(), reference));
            }
        }
    }
    fail(std::format("could not resolve HEAD reference '{}' under {}", reference, gitDirectory.generic_string()));
}

// ---------------------------------------------------------------------------
// medui-conformance.toml
// ---------------------------------------------------------------------------

/// The declared diagnostic position precision from `spec/diagnostics.md`. MduX's lexer carries
/// exact columns, so it declares `full`; the other two exist because the manifest may name them
/// and a declaration this file ignored would be a declaration nothing checks.
enum class Positions : std::uint8_t { Full, LineOnly, None };

struct Manifest {
    std::string              commit;
    std::vector<std::string> capabilities;
    Positions                positions{Positions::Full};
};

/// `governance/versioning.md` makes an unknown key an error rather than something to ignore, so a
/// misspelled `capabilties` fails here instead of silently claiming nothing.
constexpr std::array<std::string_view, 5> knownManifestKeys{"repository", "version", "commit", "capabilities", "positions"};

[[nodiscard]] Manifest manifest() {
    const std::filesystem::path path     = std::filesystem::path{MDUX_REPO_ROOT} / "medui-conformance.toml";
    const std::string           text     = readFile(path);
    const toml::Document        document = toml::parse(text);
    const toml::Table&          root     = document.root();

    Manifest result{.commit = root.require("commit").asString(), .capabilities = root.require("capabilities").asStringArray(), .positions = Positions::Full};

    for (const auto& entry : root.entries()) {
        if (std::ranges::find(knownManifestKeys, entry.first) == knownManifestKeys.end()) {
            fail(std::format("{}: unknown key '{}'; the consumer manifest rejects keys it does "
                             "not define",
                             path.generic_string(),
                             entry.first));
        }
    }

    const std::string declared = root.require("positions").asString();
    if (declared == "full") {
        result.positions = Positions::Full;
    } else if (declared == "line-only") {
        result.positions = Positions::LineOnly;
    } else if (declared == "none") {
        result.positions = Positions::None;
    } else {
        fail(std::format("{}: positions must be \"full\", \"line-only\" or \"none\", got '{}'", path.generic_string(), declared));
    }

    if (!isCommitSha(result.commit)) {
        fail(std::format("{} must pin a 40-character commit SHA, got '{}'", path.generic_string(), result.commit));
    }
    if (result.capabilities.empty()) {
        // An empty claim would make this whole suite assert nothing while still looking green.
        fail(std::format("{} claims no capabilities, so nothing would be checked", path.generic_string()));
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

struct TextPackageInput {
    std::string              locale;
    std::vector<std::string> keys;
};

struct Case {
    std::string                     id;
    std::string                     phase;
    std::filesystem::path           source;
    bool                            valid{false};
    std::vector<ExpectedDiagnostic> diagnostics;
    std::vector<std::string>        themeTokens;
    std::vector<TextPackageInput>   textPackages;
};

[[nodiscard]] const json::Value& member(const json::Value& object, std::string_view key, const std::filesystem::path& path) {
    const json::Value* found = object.find(key);
    if (found == nullptr) {
        fail(std::format("{}: missing required member '{}'", path.generic_string(), key));
    }
    return *found;
}

[[nodiscard]] std::string requireString(const json::Value& object, std::string_view key, const std::filesystem::path& path) {
    const auto text = member(object, key, path).asString();
    if (!text) {
        fail(std::format("{}: member '{}' is not a string", path.generic_string(), key));
    }
    return std::string{*text};
}

[[nodiscard]] std::size_t requirePosition(const json::Value& object, std::string_view key, const std::filesystem::path& path) {
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
[[nodiscard]] std::span<const json::Value> requireArray(const json::Value& object, std::string_view key, const std::filesystem::path& path) {
    const json::Value& found = member(object, key, path);
    if (found.kind() != json::Value::Kind::Array) {
        fail(std::format("{}: member '{}' is not an array", path.generic_string(), key));
    }
    return found.elements();
}

[[nodiscard]] bool requireBool(const json::Value& object, std::string_view key, const std::filesystem::path& path) {
    const auto flag = member(object, key, path).asBool();
    if (!flag) {
        fail(std::format("{}: member '{}' is not a boolean", path.generic_string(), key));
    }
    return *flag;
}

[[nodiscard]] Case readCase(const std::filesystem::path& path) {
    const std::string text     = readFile(path);
    const auto        document = json::parse(text);
    if (!document) {
        fail(std::format("{}: is not valid JSON", path.generic_string()));
    }

    Case result;
    result.id    = requireString(*document, "id", path);
    result.phase = requireString(*document, "phase", path);

    const std::string source = requireString(*document, "source", path);
    if (source.contains('/') || source.contains('\\') || !source.ends_with(".medui")) {
        fail(std::format("{}: source must be a sibling .medui file, got '{}'", path.generic_string(), source));
    }
    result.source = path.parent_path() / source;

    // `case.schema.json` binds the phase to the directory holding the case, so a case filed under
    // the wrong phase is a contract error rather than something to route around.
    const std::string directoryPhase = path.parent_path().parent_path().filename().string();
    if (result.phase != directoryPhase) {
        fail(std::format("{}: declares phase '{}' but sits under '{}'", path.generic_string(), result.phase, directoryPhase));
    }

    if (const json::Value* inputs = document->find("inputs")) {
        if (inputs->kind() != json::Value::Kind::Object) {
            fail(std::format("{}: member 'inputs' is not an object", path.generic_string()));
        }
        rejectUnknownMembers(*inputs, {"themeTokens", "textPackages"}, "inputs", path);
        if (inputs->find("themeTokens") != nullptr) {
            for (const json::Value& token : requireArray(*inputs, "themeTokens", path)) {
                const auto value = token.asString();
                if (!value) {
                    fail(std::format("{}: themeTokens contains a non-string", path.generic_string()));
                }
                result.themeTokens.emplace_back(*value);
            }
        }
        if (inputs->find("textPackages") != nullptr) {
            for (const json::Value& package : requireArray(*inputs, "textPackages", path)) {
                if (package.kind() != json::Value::Kind::Object) {
                    fail(std::format("{}: textPackages contains a non-object", path.generic_string()));
                }
                rejectUnknownMembers(package, {"locale", "keys"}, "text package", path);
                TextPackageInput packageInput{.locale = requireString(package, "locale", path), .keys = {}};
                for (const json::Value& key : requireArray(package, "keys", path)) {
                    const auto value = key.asString();
                    if (!value) {
                        fail(std::format("{}: text package keys contains a non-string", path.generic_string()));
                    }
                    packageInput.keys.emplace_back(*value);
                }
                result.textPackages.push_back(std::move(packageInput));
            }
        }
    }

    const json::Value& expected = member(*document, "expected", path);
    result.valid                = requireBool(expected, "valid", path);
    for (const json::Value& entry : requireArray(expected, "diagnostics", path)) {
        result.diagnostics.push_back(
            {.code = requireString(entry, "code", path), .line = requirePosition(entry, "line", path), .column = requirePosition(entry, "column", path)});
    }
    return result;
}

/// @brief Compares the canonical contract dictionary with the executable transcription.
///
/// Checking both at the pinned revision prevents a new or renamed field from becoming an
/// unreviewed local dialect merely because implementation and unit tests copied the old table.
using DictionaryEntry = std::tuple<std::string, std::string, bool>;

[[nodiscard]] std::vector<std::string> backtickValues(std::string_view cell) {
    std::vector<std::string> values;
    std::size_t              cursor = 0;
    for (;;) {
        const std::size_t open = cell.find('`', cursor);
        if (open == std::string_view::npos) {
            break;
        }
        const std::size_t close = cell.find('`', open + 1);
        if (close == std::string_view::npos) {
            fail("spec/component-model.md contains an unterminated backtick value");
        }
        values.emplace_back(cell.substr(open + 1, close - open - 1));
        cursor = close + 1;
    }
    return values;
}

[[nodiscard]] std::vector<std::string_view> tableCells(std::string_view line) {
    std::vector<std::string_view> cells;
    std::size_t                   start = 1;
    for (;;) {
        const std::size_t separator = line.find('|', start);
        if (separator == std::string_view::npos) {
            break;
        }
        std::string_view cell = line.substr(start, separator - start);
        while (!cell.empty() && std::isspace(static_cast<unsigned char>(cell.front())) != 0) {
            cell.remove_prefix(1);
        }
        while (!cell.empty() && std::isspace(static_cast<unsigned char>(cell.back())) != 0) {
            cell.remove_suffix(1);
        }
        cells.push_back(cell);
        start = separator + 1;
    }
    return cells;
}

[[nodiscard]] std::set<DictionaryEntry> contractDictionary(const std::filesystem::path& checkout) {
    const std::filesystem::path path = checkout / "spec" / "component-model.md";
    std::istringstream          lines{readFile(path)};
    std::set<DictionaryEntry>   entries;
    bool                        inTable = false;
    for (std::string line; std::getline(lines, line);) {
        if (line.starts_with("| Construct |")) {
            inTable = true;
            continue;
        }
        if (!inTable) {
            continue;
        }
        if (!line.starts_with('|')) {
            break;
        }
        if (line.starts_with("|---")) {
            continue;
        }

        const std::vector<std::string_view> cells = tableCells(line);
        if (cells.size() != 3) {
            fail(std::format("{}: malformed component dictionary row '{}'", path.generic_string(), line));
        }
        const std::vector<std::string> component = backtickValues(cells[0]);
        if (component.size() != 1) {
            fail(std::format("{}: component dictionary row has no single component name", path.generic_string()));
        }
        for (const std::string& field : backtickValues(cells[1])) {
            entries.emplace(component.front(), field, true);
        }
        for (const std::string& field : backtickValues(cells[2])) {
            entries.emplace(component.front(), field, false);
        }
    }
    if (entries.empty()) {
        fail(std::format("{}: component dictionary table is empty or missing", path.generic_string()));
    }
    return entries;
}

[[nodiscard]] std::set<DictionaryEntry> implementedDictionary() {
    std::set<DictionaryEntry> entries;
    for (const md::ComponentRule& component : md::componentDictionary()) {
        for (const md::FieldRule& field : component.fields) {
            entries.emplace(component.name, field.name, field.required);
        }
    }
    return entries;
}

void checkComponentDictionary(const std::filesystem::path& checkout, mdux::spec::Checks& checks) {
    checks.expect(implementedDictionary() == contractDictionary(checkout),
                  "the implemented component/field/requiredness set exactly matches the pinned "
                  "spec/component-model.md table");
}

[[nodiscard]] std::vector<Case> loadCases(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> paths;
    const std::filesystem::path        directory = root / "conformance";
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
    for (const std::filesystem::path& path : paths) {
        cases.push_back(readCase(path));
    }
    return cases;
}

// ---------------------------------------------------------------------------
// Running one case
// ---------------------------------------------------------------------------

[[nodiscard]] std::string join(const std::vector<std::string>& parts) {
    std::string joined;
    for (const std::string& part : parts) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += part;
    }
    return joined;
}

[[nodiscard]] std::string codesOf(const std::vector<cli::Diagnostic>& diagnostics) {
    if (diagnostics.empty()) {
        return "no diagnostics";
    }
    std::string joined;
    for (const cli::Diagnostic& diagnostic : diagnostics) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += std::format("{} at {}:{}", diagnostic.code, diagnostic.line, diagnostic.column);
    }
    return joined;
}

/// `spec/diagnostics.md`, "Positions in conformance cases": a pinned position is matched as far as
/// the declared precision goes, and reporting more than was declared fails. The declaration is
/// checked rather than tolerated, so precision cannot move in either direction without a manifest
/// edit. MduX reports `0` for "unknown", which is how an absent position is spelled.
void checkPosition(const Case& item, const ExpectedDiagnostic& expected, const cli::Diagnostic& got, Positions positions, mdux::spec::Checks& checks) {
    switch (positions) {
        case Positions::Full:
            checks.expect(got.line == expected.line && got.column == expected.column,
                          std::format("{}: {} at {}:{}, got {}:{}", item.id, expected.code, expected.line, expected.column, got.line, got.column));
            return;
        case Positions::LineOnly:
            checks.expect(got.line == expected.line, std::format("{}: {} at line {}, got {}", item.id, expected.code, expected.line, got.line));
            checks.expect(got.column == 0,
                          std::format("{}: medui-conformance.toml declares positions = \"line-only\", "
                                      "but {} reported column {}. Raise the declaration to \"full\" so "
                                      "the pinned column {} is checked.",
                                      item.id,
                                      expected.code,
                                      got.column,
                                      expected.column));
            return;
        case Positions::None:
            checks.expect(got.line == 0 && got.column == 0,
                          std::format("{}: medui-conformance.toml declares positions = \"none\", but "
                                      "{} reported {}:{}. Raise the declaration so the pinned position "
                                      "{}:{} is checked.",
                                      item.id,
                                      expected.code,
                                      got.line,
                                      got.column,
                                      expected.line,
                                      expected.column));
            return;
    }
}

void runCase(const Case& item, Positions positions, mdux::spec::Checks& checks) {
    const std::string            source      = readFile(item.source);
    const std::string            file        = item.source.filename().string();
    const md::ParseResult        parsed      = md::parse(source, file);
    std::vector<cli::Diagnostic> diagnostics = parsed.diagnostics;

    if (item.phase == "semantics" && parsed.screen) {
        std::vector<std::string_view> themeTokens;
        themeTokens.reserve(item.themeTokens.size());
        for (const std::string& token : item.themeTokens) {
            themeTokens.push_back(token);
        }

        std::vector<mdux::text::TextPackage> packages;
        packages.reserve(item.textPackages.size());
        for (const TextPackageInput& input : item.textPackages) {
            mdux::text::TextPackage package;
            package.header.id   = std::format("shared-conformance-{}", input.locale);
            package.atlasId     = "shared-conformance-atlas";
            package.locale      = input.locale;
            package.sidecarPath = "runs.bin";
            for (const std::string& key : input.keys) {
                package.runs.push_back(mdux::text::TextRun{.id = key, .byteOffset = 0, .byteLength = 0, .sha256 = {}});
            }
            packages.push_back(std::move(package));
        }

        md::SemanticResult semantic = md::analyze(*parsed.screen, file, md::SemanticInputs{.themeTokens = themeTokens, .textPackages = packages});
        diagnostics.insert(diagnostics.end(), std::make_move_iterator(semantic.diagnostics.begin()), std::make_move_iterator(semantic.diagnostics.end()));
    }

    const bool accepted = parsed.screen.has_value() && diagnostics.empty();

    if (item.valid) {
        checks.expect(accepted, std::format("{}: accepted, got {}", item.id, codesOf(diagnostics)));
        return;
    }

    checks.expect(!accepted, std::format("{}: rejected, but the claimed phase accepted it", item.id));

    std::vector<bool> consumed(diagnostics.size(), false);
    for (const ExpectedDiagnostic& expected : item.diagnostics) {
        std::size_t match = diagnostics.size();
        for (std::size_t index = 0; index < diagnostics.size(); ++index) {
            if (!consumed[index] && diagnostics[index].code == expected.code) {
                match = index;
                break;
            }
        }
        const bool reported = match != diagnostics.size();
        checks.expect(reported, std::format("{}: {} reported, got {}", item.id, expected.code, codesOf(diagnostics)));
        if (reported) {
            consumed[match] = true;
            checkPosition(item, expected, diagnostics[match], positions, checks);
        }
    }

    checks.expect(std::ranges::all_of(consumed,
                                      [](bool value) {
                                          return value;
                                      }),
                  std::format("{}: every emitted diagnostic is expected, got {}", item.id, codesOf(diagnostics)));
}

}  // namespace

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------

const mdux::spec::Register claimsNameAPhaseThisSuiteCanObserve{"Every claimed capability is one this suite can actually check", "evidence-unit", [] {
                                                                   return speclab::Test("medui-shared-claims-are-observable")
                                                                       .Given("the capabilities named in medui-conformance.toml", [] {})
                                                                       .When("they are compared with what this suite can observe", [] {})
                                                                       .Then("no phase is claimed without an adapter behind it",
                                                                             [] {
                                                                                 mdux::spec::Checks checks;
                                                                                 for (const std::string& capability : manifest().capabilities) {
                                                                                     checks.expect(std::ranges::find(runnableCapabilities, capability)
                                                                                                       != runnableCapabilities.end(),
                                                                                                   std::format("'{}' is observable here; add the adapter that "
                                                                                                               "checks that phase before claiming it",
                                                                                                               capability));
                                                                                 }
                                                                                 checks.raise();
                                                                             })
                                                                       .Execute();
                                                               }};

const mdux::spec::Register pinnedCasesPass{"The parser satisfies every pinned case in a claimed phase", "evidence-unit", [] {
                                               return speclab::Test("medui-shared-conformance")
                                                   .Given("the exact checkout named by medui-conformance.toml", [] {})
                                                   .When("each case in a claimed phase is parsed", [] {})
                                                   .Then("acceptance, codes and positions match the pinned expectations",
                                                         [] {
                                                             mdux::spec::Checks checks;
                                                             const Manifest     pinned = manifest();

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

                                                             const std::filesystem::path checkout{root};
                                                             const std::string           actualCommit = checkoutRevision(checkout);
                                                             checks.expect(actualCommit == pinned.commit,
                                                                           std::format("MEDUI_CONFORMANCE_DIR is checked out at {}, but "
                                                                                       "medui-conformance.toml pins {}",
                                                                                       actualCommit,
                                                                                       pinned.commit));
                                                             checks.raise();

                                                             checkComponentDictionary(checkout, checks);
                                                             checks.raise();

                                                             const std::vector<Case> cases = loadCases(checkout);
                                                             checks.expect(!cases.empty(), "the pinned checkout contains conformance cases");

                                                             std::set<std::string>    executed;
                                                             std::vector<std::string> unclaimed;
                                                             for (const Case& item : cases) {
                                                                 if (std::ranges::find(pinned.capabilities, item.phase) != pinned.capabilities.end()) {
                                                                     runCase(item, pinned.positions, checks);
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

                                                             std::cerr << std::format("MedUI conformance: {} case(s) at {}; unclaimed and not asserted: {}\n",
                                                                                      cases.size(),
                                                                                      pinned.commit,
                                                                                      unclaimed.empty() ? std::string{"none"} : join(unclaimed));
                                                             checks.raise();
                                                         })
                                                   .Execute();
                                           }};
