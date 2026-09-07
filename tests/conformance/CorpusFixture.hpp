/**
 * @file CorpusFixture.hpp
 * @brief Shared machinery for running the pinned `Compliatory/MedUI` corpus against MduX.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-014 What rendered-truth verification checks, and what it cannot
 *
 * Two suites read the same pinned checkout: `medui_tools_spec`'s `SharedConformanceTests.cpp` runs
 * the compiler case corpus, and `conformance_spec` runs the observation-profile vectors and the
 * `conformance/contracts` documents. Everything both need lives here so there is one reader of
 * `medui-conformance.toml`, one checkout-revision resolver, one fieldwise JSON comparison, and one
 * document validator (`mdux.tools.schema` over the schemas the contract ships).
 *
 * `MEDUI-DEC-005` forbids silently skipping a claimed phase or profile, so `corpusRootOrSkip()`
 * turns a missing checkout into a hard failure under `CI` and a stderr notice otherwise.
 *
 * Include after `import std;`, `import speclab;`, `import mdux.evidence.json;`,
 * `import mdux.tools.toml;`, `import mdux.tools.schema;` and `"../framework/SpecLabBridge.hpp"`.
 * Needs `MDUX_REPO_ROOT` defined.
 */
#pragma once

namespace mdux::conformance {

namespace json = mdux::evidence::json;
namespace toml = mdux::tools::toml;

// ---------------------------------------------------------------------------
// Diagnostics and environment
// ---------------------------------------------------------------------------

[[noreturn]] inline void fail(std::string message, std::source_location where = std::source_location::current()) {
    throw speclab::core::AssertionFailure(std::move(message), where);
}

[[nodiscard]] inline const char* env(const char* name) {
    return std::getenv(name);
}

[[nodiscard]] inline bool isSet(const char* name) {
    const char* value = env(name);
    return value != nullptr && *value != '\0';
}

[[nodiscard]] inline std::string readFile(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        fail(std::format("could not open {}", path.generic_string()));
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

[[nodiscard]] inline std::string trim(std::string value) {
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

// ---------------------------------------------------------------------------
// JSON reading helpers, shared with the case reader
// ---------------------------------------------------------------------------

[[nodiscard]] inline const json::Value& member(const json::Value& object, std::string_view key, const std::filesystem::path& path) {
    const json::Value* found = object.find(key);
    if (found == nullptr) {
        fail(std::format("{}: missing required member '{}'", path.generic_string(), key));
    }
    return *found;
}

[[nodiscard]] inline std::string requireString(const json::Value& object, std::string_view key, const std::filesystem::path& path) {
    const auto text = member(object, key, path).asString();
    if (!text) {
        fail(std::format("{}: member '{}' is not a string", path.generic_string(), key));
    }
    return std::string{*text};
}

[[nodiscard]] inline std::size_t requirePosition(const json::Value& object, std::string_view key, const std::filesystem::path& path) {
    const auto number = member(object, key, path).asInt();
    if (!number) {
        fail(std::format("{}: member '{}' is not an integer", path.generic_string(), key));
    }
    if (*number < 0) {
        fail(std::format("{}: member '{}' is negative", path.generic_string(), key));
    }
    return static_cast<std::size_t>(*number);
}

/// `Value::elements()` yields an empty span for anything that is not an array, so a member of the
/// wrong shape would silently read as "empty" and assert nothing. Check the kind instead.
[[nodiscard]] inline std::span<const json::Value> requireArray(const json::Value& object, std::string_view key, const std::filesystem::path& path) {
    const json::Value& found = member(object, key, path);
    if (found.kind() != json::Value::Kind::Array) {
        fail(std::format("{}: member '{}' is not an array", path.generic_string(), key));
    }
    return found.elements();
}

[[nodiscard]] inline bool requireBool(const json::Value& object, std::string_view key, const std::filesystem::path& path) {
    const auto flag = member(object, key, path).asBool();
    if (!flag) {
        fail(std::format("{}: member '{}' is not a boolean", path.generic_string(), key));
    }
    return *flag;
}

inline void rejectUnknownMembers(const json::Value&                      object,
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

/// Fieldwise JSON equality, key-order independent - the contract's "identity comparison". Lives in
/// `mdux.tools.schema` so the validator, the E01 aggregate adapter and R04's identity check all run
/// one implementation; re-exported here so an includer writes `jsonEqual(...)` unqualified.
using mdux::tools::schema::jsonEqual;

// ---------------------------------------------------------------------------
// Resolving the checked-out MedUI revision from .git metadata alone
// ---------------------------------------------------------------------------

[[nodiscard]] inline bool isCommitSha(std::string_view value) {
    // Lowercase only, matching `schemas/consumer-manifest.schema.json`'s `^[0-9a-f]{40}$` and git's
    // own object-name spelling: an uppercase SHA is not what the contract pins.
    return value.size() == 40 && std::ranges::all_of(value, [](unsigned char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

[[nodiscard]] inline std::filesystem::path resolveGitDirectory(const std::filesystem::path& root) {
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

[[nodiscard]] inline std::filesystem::path resolveCommonGitDirectory(const std::filesystem::path& gitDirectory) {
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

[[nodiscard]] inline std::string checkoutRevision(const std::filesystem::path& root) {
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

/// The pinned checkout root, or `nullopt` after recording the "CI must have it" expectation and
/// printing a notice. Every corpus scenario begins with this so a missing checkout is a hard
/// failure in CI and a visible skip offline - never a quiet pass.
[[nodiscard]] inline std::optional<std::filesystem::path> corpusRootOrSkip(mdux::spec::Checks& checks) {
    const char* root = env("MEDUI_CONFORMANCE_DIR");
    if (root == nullptr || *root == '\0') {
        checks.expect(!isSet("CI"),
                      "MEDUI_CONFORMANCE_DIR is set in CI, so the capabilities and profiles claimed "
                      "in medui-conformance.toml are actually substantiated");
        std::cerr << "MedUI conformance: SKIPPED - set MEDUI_CONFORMANCE_DIR to a checkout of the "
                     "pinned commit to run the shared corpus locally.\n";
        return std::nullopt;
    }
    return std::filesystem::path{root};
}

// ---------------------------------------------------------------------------
// medui-conformance.toml — the consumer manifest
// ---------------------------------------------------------------------------

/// Declared diagnostic position precision, `spec/diagnostics.md`. MduX's lexer carries exact
/// columns, so it declares `full`; the other two exist because the manifest may name them.
enum class Positions : std::uint8_t { Full, LineOnly, None };

/// Phases `SharedConformanceTests.cpp` can genuinely observe.
inline constexpr std::array<std::string_view, 4> runnableCapabilities{"syntax", "semantics", "layout", "safety"};

/// The closed set of profile ids, matching `schemas/consumer-manifest.schema.json`'s enum.
inline constexpr std::array<std::string_view, 6> knownProfileIds{"MEDUI-PROFILE-RENDERED",
                                                                 "MEDUI-PROFILE-EVIDENCE",
                                                                 "MEDUI-PROFILE-INTERACTION",
                                                                 "MEDUI-PROFILE-BINDING",
                                                                 "MEDUI-PROFILE-PRESENTATION",
                                                                 "MEDUI-PROFILE-PIXELS"};

/// Profiles `conformance_spec` has adapters for. A claim outside this fails the "runnable" check
/// the same way an unobservable capability does.
inline constexpr std::array<std::string_view, 2> runnableProfiles{"MEDUI-PROFILE-RENDERED", "MEDUI-PROFILE-EVIDENCE"};

/// The claimed profile ids this suite cannot substantiate - none of them are in `runnableProfiles`.
/// Empty means every claim is backed by an adapter here; a non-empty result must fail the gate.
[[nodiscard]] inline std::vector<std::string> unrunnableProfiles(std::span<const std::string> claimed) {
    std::vector<std::string> unmatched;
    for (const std::string& id : claimed) {
        if (std::ranges::find(runnableProfiles, id) == runnableProfiles.end()) {
            unmatched.push_back(id);
        }
    }
    return unmatched;
}

struct Manifest {
    std::string              commit;
    std::vector<std::string> capabilities;
    Positions                positions{Positions::Full};
    std::vector<std::string> profiles;  ///< claimed profile ids; the contract fixes version at 1
};

/// Reads and parses `<checkout>/schemas/<name>.schema.json` from the pinned MedUI checkout.
[[nodiscard]] inline json::Value pinnedSchema(const std::filesystem::path& checkout, std::string_view name) {
    const std::filesystem::path path     = checkout / "schemas" / std::format("{}.schema.json", name);
    auto                        document = json::parse(readFile(path));
    if (!document) {
        fail(std::format("{}: is not valid JSON", path.generic_string()));
    }
    return std::move(*document);
}

/// `schemas/consumer-manifest.schema.json`, verbatim, embedded so `manifest()` and the negative
/// scenarios need no checkout. `medui-consumer-manifest-contract` asserts this still equals the
/// pinned file byte-structure, so it cannot drift.
[[nodiscard]] inline const json::Value& consumerManifestSchema() {
    static const json::Value schema = [] {
        auto parsed = json::parse(R"JSON({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://github.com/Compliatory/MedUI/schemas/0.3/consumer-manifest.schema.json",
  "title": "MedUI consumer manifest",
  "description": "The parsed structure of a consumer's medui-conformance.toml. The file on disk is TOML; this describes what a harness sees after parsing it.",
  "type": "object",
  "additionalProperties": false,
  "required": ["repository", "commit", "capabilities", "positions"],
  "properties": {
    "repository": {
      "const": "https://github.com/Compliatory/MedUI",
      "description": "The contract repository being pinned, not the consumer's own."
    },
    "version": {
      "type": "string",
      "minLength": 1,
      "description": "Informational label for the pinned revision. Never consulted; `commit` identifies the contract."
    },
    "commit": { "type": "string", "pattern": "^[0-9a-f]{40}$" },
    "capabilities": {
      "type": "array",
      "minItems": 1,
      "uniqueItems": true,
      "items": { "enum": ["syntax", "semantics", "layout", "safety"] }
    },
    "positions": { "enum": ["full", "line-only", "none"] },
    "profiles": {
      "type": "array",
      "items": {
        "type": "object",
        "additionalProperties": false,
        "required": ["id", "version"],
        "properties": {
          "id": {
            "enum": [
              "MEDUI-PROFILE-RENDERED",
              "MEDUI-PROFILE-EVIDENCE",
              "MEDUI-PROFILE-INTERACTION",
              "MEDUI-PROFILE-BINDING",
              "MEDUI-PROFILE-PRESENTATION",
              "MEDUI-PROFILE-PIXELS"
            ]
          },
          "version": { "const": 1 }
        }
      },
      "minItems": 1,
      "uniqueItems": true
    }
  }
})JSON");
        return parsed ? std::move(*parsed) : json::Value::null();
    }();
    return schema;
}

/// `nullopt` when `document` satisfies the consumer-manifest schema, otherwise the joined list of
/// rules it breaks. One validator - `mdux.tools.schema` over the schema the contract ships - for
/// MduX's own manifest (converted to this shape by `manifest()`) and the `conformance/contracts`
/// `consumer-manifest` cases.
[[nodiscard]] inline std::optional<std::string> validateConsumerManifest(const json::Value& document) {
    const std::vector<std::string> problems = mdux::tools::schema::validate(document, consumerManifestSchema());
    if (problems.empty()) {
        return std::nullopt;
    }
    std::string joined;
    for (const std::string& problem : problems) {
        joined += (joined.empty() ? "" : "; ") + problem;
    }
    return joined;
}

/// Rebuilds a parsed `medui-conformance.toml` as the JSON shape `schemas/consumer-manifest.schema.json`
/// (and `validateConsumerManifest()`) speak: `capabilities`/`profiles` string arrays become arrays,
/// `profiles` entries are expanded to `{ id, version: 1 }`. `label` names the source in errors.
[[nodiscard]] inline json::Value consumerManifestView(const toml::Table& root, std::string_view label) {
    json::Value view = json::Value::emptyObject();
    const auto  put  = [&](std::string key, json::Value value) {
        if (!view.set(std::move(key), std::move(value)).has_value()) {
            fail(std::format("{}: could not assemble the manifest for validation", label));
        }
    };
    for (const auto& entry : root.entries()) {
        const std::string& key = entry.first;
        const toml::Value&  raw = entry.second;
        if (key == "capabilities") {
            std::vector<json::Value> items;
            for (const std::string& name : raw.asStringArray()) {
                items.push_back(json::Value::string(name));
            }
            put("capabilities", json::Value::array(std::move(items)));
        } else if (key == "profiles") {
            std::vector<json::Value> claims;
            for (const std::string& id : raw.asStringArray()) {
                json::Value claim = json::Value::emptyObject();
                if (!claim.set("id", json::Value::string(id)).has_value()
                    || !claim.set("version", json::Value::integer(1)).has_value()) {
                    fail(std::format("{}: could not assemble a profile claim for validation", label));
                }
                claims.push_back(std::move(claim));
            }
            put("profiles", json::Value::array(std::move(claims)));
        } else if (raw.kind() == toml::Value::Kind::String) {
            put(key, json::Value::string(raw.asString()));
        } else {
            // A non-string scalar where the schema wants a string (`version = 1`, `commit = true`):
            // pass a JSON null through so validateConsumerManifest() reports the type error rather
            // than a placeholder string accidentally satisfying "non-empty".
            put(key, json::Value::null());
        }
    }
    return view;
}

/// Reads MduX's own `medui-conformance.toml`, validates it through `validateConsumerManifest()`,
/// and returns the typed view the scenarios use. Does not need the checkout.
[[nodiscard]] inline Manifest manifest() {
    const std::filesystem::path path     = std::filesystem::path{MDUX_REPO_ROOT} / "medui-conformance.toml";
    const toml::Document        document = toml::parse(readFile(path));
    const toml::Table&          root     = document.root();

    if (const std::optional<std::string> error = validateConsumerManifest(consumerManifestView(root, path.generic_string()))) {
        fail(std::format("{}: {}", path.generic_string(), *error));
    }

    Manifest result;
    result.commit       = std::string{root.require("commit").asString()};
    result.capabilities = root.require("capabilities").asStringArray();
    const std::string declared = std::string{root.require("positions").asString()};
    result.positions = declared == "line-only" ? Positions::LineOnly : (declared == "none" ? Positions::None : Positions::Full);
    if (const toml::Value* profiles = root.find("profiles")) {
        result.profiles = profiles->asStringArray();
    }
    return result;
}

/// The rules `profiles/registry.json` lists for `profileId`. Empty when the registry names no such
/// profile - a claim the "every claimed rule ran" check must then fail on.
[[nodiscard]] inline std::vector<std::string> registryRulesFor(const std::filesystem::path& checkout, std::string_view profileId) {
    const std::filesystem::path path     = checkout / "profiles" / "registry.json";
    const auto                  document = json::parse(readFile(path));
    if (!document) {
        fail(std::format("{}: is not valid JSON", path.generic_string()));
    }
    std::vector<std::string> rules;
    for (const json::Value& entry : requireArray(*document, "profiles", path)) {
        if (requireString(entry, "id", path) == profileId) {
            for (const json::Value& rule : requireArray(entry, "rules", path)) {
                rules.emplace_back(rule.asString().value_or("?"));
            }
        }
    }
    return rules;
}

// ---------------------------------------------------------------------------
// MEDUI-PROFILE-EVIDENCE aggregation (spec/profiles.md E01-E03)
// ---------------------------------------------------------------------------

/// The `$defs/identity` sub-schema of a parsed `evidence.schema.json`, or nullptr when absent.
[[nodiscard]] inline const json::Value* identitySchemaFromEvidence(const json::Value& evidenceSchema) {
    const json::Value* defs = evidenceSchema.find("$defs");
    return defs != nullptr ? defs->find("identity") : nullptr;
}

struct Aggregate {
    std::string              outcome;      ///< "pass", "fail" or "not-run"
    std::vector<std::string> rowOutcomes;  ///< each row's own outcome, in order
};

/// `spec/profiles.md` E01: a malformed identity fails aggregation. Two layers - the structural
/// shape (`evidence.schema.json`'s `$defs/identity`, when `identitySchema` is supplied), and the
/// constraint the schema delegates to the harness: `assets` ids unique and strictly ascending in
/// ASCII byte order.
[[nodiscard]] inline bool identityMalformed(const json::Value& identity, const json::Value* identitySchema) {
    if (identitySchema != nullptr && !mdux::tools::schema::validate(identity, *identitySchema).empty()) {
        return true;
    }
    const json::Value* assets = identity.find("assets");
    if (assets == nullptr) {
        return false;
    }
    if (assets->kind() != json::Value::Kind::Array) {
        return true;  // `assets` present but not an array is a malformed identity
    }
    std::string_view previous;
    for (std::size_t i = 0; i < assets->elements().size(); ++i) {
        const json::Value*              id = assets->elements()[i].find("id");
        std::optional<std::string_view> text;
        if (id != nullptr) {
            if (const auto value = id->asString()) {
                text = *value;
            }
        }
        if (!text) {
            return true;
        }
        if (i > 0 && !(previous < *text)) {  // strictly ascending also rules out a duplicate id
            return true;
        }
        previous = *text;
    }
    return false;
}

/// E01-E03 over `obligations` (each an identity) and `rows` (each `{ identity, outcome }`).
/// `identitySchema` is `evidence.schema.json`'s `$defs/identity`; each identity is validated
/// against it before any matching (E01). `label` names the source in any parse error.
[[nodiscard]] inline Aggregate aggregateEvidence(std::span<const json::Value> obligations,
                                                 std::span<const json::Value> rows,
                                                 const json::Value*           identitySchema,
                                                 const std::filesystem::path& label) {
    Aggregate aggregate;
    for (const json::Value& row : rows) {
        aggregate.rowOutcomes.emplace_back(requireString(row, "outcome", label));
    }

    // E03: an empty obligation set with no rows is not-run, never pass.
    if (obligations.empty() && rows.empty()) {
        aggregate.outcome = "not-run";
        return aggregate;
    }

    // E01: a malformed identity fails aggregation before any comparison.
    for (const json::Value& obligation : obligations) {
        if (identityMalformed(obligation, identitySchema)) {
            aggregate.outcome = "fail";
            return aggregate;
        }
    }
    for (const json::Value& row : rows) {
        if (identityMalformed(member(row, "identity", label), identitySchema)) {
            aggregate.outcome = "fail";
            return aggregate;
        }
    }

    // E02: exactly one matching row per obligation, and exactly one matching obligation per row.
    bool complete = obligations.size() == rows.size();
    if (complete) {
        for (const json::Value& obligation : obligations) {
            const auto matches = std::ranges::count_if(rows, [&](const json::Value& row) {
                return jsonEqual(member(row, "identity", label), obligation);
            });
            complete = complete && matches == 1;
        }
        for (const json::Value& row : rows) {
            const auto matches = std::ranges::count_if(obligations, [&](const json::Value& obligation) {
                return jsonEqual(member(row, "identity", label), obligation);
            });
            complete = complete && matches == 1;
        }
    }
    if (!complete) {
        aggregate.outcome = "fail";
        return aggregate;
    }

    // E03: pass only when every row's own outcome is pass.
    const bool allPassed = !rows.empty() && std::ranges::all_of(aggregate.rowOutcomes, [](const std::string& outcome) {
        return outcome == "pass";
    });
    aggregate.outcome = allPassed ? "pass" : "fail";
    return aggregate;
}

}  // namespace mdux::conformance
