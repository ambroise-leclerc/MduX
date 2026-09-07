/**
 * @file CorpusFixture.hpp
 * @brief Shared machinery for running the pinned `Compliatory/MedUI` corpus against MduX.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-014 What rendered-truth verification checks, and what it cannot
 *
 * Two suites read the same pinned checkout: `medui_tools_spec`'s `SharedConformanceTests.cpp` runs
 * the compiler case corpus, and `conformance_spec` runs the `MEDUI-PROFILE-RENDERED` observation
 * vectors and the `conformance/contracts` manifest cases.
 * Everything both need lives here so there is one reader of `medui-conformance.toml`, one way to
 * resolve the checked-out revision, and one consumer-manifest validator.
 *
 * `MEDUI-DEC-005` forbids silently skipping a claimed phase or profile, so `corpusRootOrSkip()`
 * turns a missing checkout into a hard failure under `CI` and a stderr notice otherwise.
 *
 * Include after `import std;`, `import speclab;`, `import mdux.evidence.json;`,
 * `import mdux.tools.toml;` and `"../framework/SpecLabBridge.hpp"`. Needs `MDUX_REPO_ROOT` defined.
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
inline constexpr std::array<std::string_view, 1> runnableProfiles{"MEDUI-PROFILE-RENDERED"};

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

/// `nullopt` when `document` satisfies `schemas/consumer-manifest.schema.json`, otherwise the first
/// rule it breaks. One validator for MduX's own manifest (converted to this shape by `manifest()`)
/// and for the `conformance/contracts` `consumer-manifest` cases.
[[nodiscard]] inline std::optional<std::string> validateConsumerManifest(const json::Value& document) {
    if (document.kind() != json::Value::Kind::Object) {
        return "the manifest is not an object";
    }

    constexpr std::array<std::string_view, 6> known{"repository", "version", "commit", "capabilities", "positions", "profiles"};
    for (const json::Member& entry : document.members()) {
        if (std::ranges::find(known, entry.key) == known.end()) {
            return std::format("unknown key '{}'; the consumer manifest rejects keys it does not define", entry.key);
        }
    }
    for (std::string_view required : {std::string_view{"repository"}, std::string_view{"commit"},
                                     std::string_view{"capabilities"}, std::string_view{"positions"}}) {
        if (document.find(required) == nullptr) {
            return std::format("missing required key '{}'", required);
        }
    }

    const auto repository = document.find("repository")->asString();
    if (!repository || *repository != "https://github.com/Compliatory/MedUI") {
        return "repository must be \"https://github.com/Compliatory/MedUI\"";
    }
    const auto commit = document.find("commit")->asString();
    if (!commit || !isCommitSha(*commit)) {
        return "commit must be a 40-character lowercase hex SHA";
    }
    if (const json::Value* version = document.find("version"); version != nullptr) {
        const auto text = version->asString();
        if (!text || text->empty()) {
            return "version, when present, must be a non-empty string";
        }
    }

    const json::Value& capabilities = *document.find("capabilities");
    if (capabilities.kind() != json::Value::Kind::Array || capabilities.elements().empty()) {
        return "capabilities must be a non-empty array";
    }
    constexpr std::array<std::string_view, 4> capabilityEnum{"syntax", "semantics", "layout", "safety"};
    std::vector<std::string_view>             seenCapabilities;
    for (const json::Value& capability : capabilities.elements()) {
        const auto name = capability.asString();
        if (!name || std::ranges::find(capabilityEnum, *name) == capabilityEnum.end()) {
            return "capabilities must each be one of syntax, semantics, layout, safety";
        }
        if (std::ranges::find(seenCapabilities, *name) != seenCapabilities.end()) {
            return std::format("capability '{}' is claimed twice", *name);
        }
        seenCapabilities.push_back(*name);
    }

    const auto positions = document.find("positions")->asString();
    if (!positions || (*positions != "full" && *positions != "line-only" && *positions != "none")) {
        return "positions must be \"full\", \"line-only\" or \"none\"";
    }

    if (const json::Value* profiles = document.find("profiles"); profiles != nullptr) {
        if (profiles->kind() != json::Value::Kind::Array || profiles->elements().empty()) {
            return "profiles, when present, must be a non-empty array";
        }
        std::vector<std::string_view> seenProfiles;
        for (const json::Value& claim : profiles->elements()) {
            if (claim.kind() != json::Value::Kind::Object) {
                return "each profile claim is an { id, version } object";
            }
            for (const json::Member& field : claim.members()) {
                if (field.key != "id" && field.key != "version") {
                    return std::format("a profile claim carries an unexpected key '{}'", field.key);
                }
            }
            const json::Value* id      = claim.find("id");
            const json::Value* version = claim.find("version");
            if (id == nullptr || version == nullptr) {
                return "a profile claim needs both id and version";
            }
            const auto name = id->asString();
            if (!name || std::ranges::find(knownProfileIds, *name) == knownProfileIds.end()) {
                return "a profile claim names an id outside the contract's set";
            }
            const auto number = version->asInt();
            if (!number || *number != 1) {
                return std::format("profile '{}' claims version {}, but the contract fixes it at 1", *name, number ? *number : 0);
            }
            if (std::ranges::find(seenProfiles, *name) != seenProfiles.end()) {
                return std::format("profile '{}' is claimed twice", *name);
            }
            seenProfiles.push_back(*name);
        }
    }
    return std::nullopt;
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

}  // namespace mdux::conformance
