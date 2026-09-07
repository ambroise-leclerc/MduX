/**
 * @file MeduiEvidence.cpp
 * @brief Implementation of the MEDUI-PROFILE-RENDERED evidence-envelope deriver.
 */
module;

module mdux.tools.verify.medui_evidence;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.tools.toml;
import mdux.tools.verify.driver;
import mdux.verify;

namespace mdux::tools::verify {

namespace {

namespace json = mdux::evidence::json;
namespace toml = mdux::tools::toml;
using mdux::core::err;

/// The shared rendered-check id a local check reports under, or empty when the check has none
/// (`LocalizedTextPresence` / `mdux.local/ink-coverage` - implementation-local, ADR-016).
[[nodiscard]] std::string_view sharedCheckId(std::string_view localCheck) {
    if (localCheck == mdux::verify::spell(mdux::verify::CvCheck::Bounds)) {
        return "extent-equality";
    }
    if (localCheck == mdux::verify::spell(mdux::verify::CvCheck::ColorHash)) {
        return "tint-composition";
    }
    if (localCheck == mdux::verify::spell(mdux::verify::TextCheck::InkContainment)) {
        return "ink-containment";
    }
    return {};
}

[[nodiscard]] std::string_view rowOutcome(mdux::verify::Finding finding) {
    switch (finding) {
        case mdux::verify::Finding::Held:
            return "pass";
        case mdux::verify::Finding::NoBaseline:
            return "missing-baseline";
        default:
            return "fail";
    }
}

/// `evidence.schema.json`'s asset-id alphabet is `^[a-z0-9][a-z0-9./_-]*$` - lowercase only. A
/// locale such as `en-US` is spelled as an approved-locale name elsewhere but folds to `en-us`
/// here; every other id segment is already lowercase.
[[nodiscard]] std::string toAssetSegment(std::string_view value) {
    std::string folded{value};
    std::ranges::transform(folded, folded.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return folded;
}

/// The asset id for a bound-artifact role. `screenPackage` is the envelope's `artifact`, not an
/// asset; `goldens` is the screen's sidecar. The rest carry a logical resource.
[[nodiscard]] std::string assetId(const BoundArtifact& input) {
    if (input.role == "goldens") {
        return "goldens/" + toAssetSegment(input.id);
    }
    if (input.role == "shaderPackage") {
        return "shader/" + toAssetSegment(input.id);
    }
    if (input.role == "imagePackage") {
        return "image/" + toAssetSegment(input.id);
    }
    if (input.role == "fontPackage") {
        return "font/" + toAssetSegment(input.locale);
    }
    if (input.role == "textPackage") {
        return "text/" + toAssetSegment(input.locale);
    }
    return {};  // screenPackage, or a role added since
}

[[nodiscard]] bool setMember(json::Value& object, std::string key, json::Value value) {
    return object.set(std::move(key), std::move(value)).has_value();
}

/// The `{ id, digest }` asset array for one scope, sorted ascending and de-duplicated. `locale` is
/// the scope's approved locale, or empty for a locale-free scope: a per-locale package (font, text)
/// is included only for its own locale.
[[nodiscard]] std::optional<json::Value> assetsForScope(const RunResult& result, std::string_view locale) {
    std::vector<std::pair<std::string, std::string>> assets;  // id -> digest
    for (const BoundArtifact& input : result.inputs) {
        const std::string id = assetId(input);
        if (id.empty()) {
            continue;
        }
        if (!input.locale.empty() && input.locale != locale) {
            continue;
        }
        if (std::ranges::none_of(assets, [&id](const auto& entry) { return entry.first == id; })) {
            assets.emplace_back(id, input.sha256);
        }
    }
    std::ranges::sort(assets, {}, &std::pair<std::string, std::string>::first);

    std::vector<json::Value> entries;
    entries.reserve(assets.size());
    for (const auto& [id, digest] : assets) {
        json::Value entry = json::Value::emptyObject();
        if (!setMember(entry, "id", json::Value::string(id)) || !setMember(entry, "digest", json::Value::string(digest))) {
            return std::nullopt;
        }
        entries.push_back(std::move(entry));
    }
    return json::Value::array(std::move(entries));
}

/// The producer-scoped `configuration` token: SHA-256 of a canonical little JSON binding the
/// settings that determine what `mdux-verify-ui` renders - the backend, the surface and the
/// straight-RGBA8 readback format, plus the producer version so a contract change re-scopes it.
[[nodiscard]] std::string configurationToken(const RunResult& result) {
    json::Value config = json::Value::emptyObject();
    static_cast<void>(config.set("backend", json::Value::string(result.backend)));
    static_cast<void>(config.set("format", json::Value::string("Rgba8Unorm")));
    static_cast<void>(config.set("producerVersion", json::Value::string(MDUX_TOOL_VERSION)));
    std::vector<json::Value> surface;
    surface.push_back(json::Value::integer(result.surfaceWidth));
    surface.push_back(json::Value::integer(result.surfaceHeight));
    static_cast<void>(config.set("surface", json::Value::array(std::move(surface))));

    const auto text = json::write(config);
    if (!text.has_value()) {
        return std::string(64, '0');
    }
    const auto bytes  = std::as_bytes(std::span{*text});
    const auto digest = mdux::evidence::toHex(mdux::evidence::sha256(bytes));
    return std::string{digest.data(), digest.size()};
}

}  // namespace

std::string_view describe(EvidenceError error) noexcept {
    switch (error) {
        case EvidenceError::NotRun:
            return "the run did not render and evaluate, so there is no evidence to publish";
        case EvidenceError::NoMappableObligation:
            return "the run produced no outcome that maps to a shared rendered-check id";
        case EvidenceError::ManifestUnreadable:
            return "medui-conformance.toml could not be read for the contract revision";
        case EvidenceError::SerializationFailed:
            return "the evidence envelope could not be assembled";
    }
    return "unknown evidence error";
}

mdux::core::Result<RenderedEvidence, EvidenceError>
deriveRenderedEvidence(const RunResult& result, std::string_view screenId, const std::filesystem::path& manifestPath) {
    if (result.state != RunState::Passed && result.state != RunState::ChecksFailed) {
        return err(EvidenceError::NotRun);
    }

    std::string contract;
    try {
        std::ifstream    in{manifestPath, std::ios::binary};
        std::string      text{std::istreambuf_iterator<char>{in}, {}};
        const toml::Document document = toml::parse(text);
        contract                      = document.root().require("commit").asString();
    } catch (const std::exception&) {
        return err(EvidenceError::ManifestUnreadable);
    }

    const std::string configuration = configurationToken(result);
    const std::string artifactSha   = [&] {
        for (const BoundArtifact& input : result.inputs) {
            if (input.role == "screenPackage") {
                return input.sha256;
            }
        }
        return std::string{};
    }();

    std::vector<json::Value> obligations;
    std::vector<json::Value> rows;
    std::size_t              excluded = 0;

    for (std::size_t index = 0; index < result.outcomes.size(); ++index) {
        const Outcome& outcome = result.outcomes[index];
        const std::string_view checkId = sharedCheckId(outcome.check);
        if (checkId.empty()) {
            ++excluded;
            continue;
        }

        const std::string_view scope    = index < result.obligations.size() ? std::string_view{result.obligations[index].scope} : std::string_view{outcome.scope};
        const bool             localeFree = scope == mdux::verify::localeFreeScopeName;
        const std::string_view locale     = localeFree ? std::string_view{} : scope;

        std::optional<json::Value> assets = assetsForScope(result, locale);
        if (!assets.has_value()) {
            return err(EvidenceError::SerializationFailed);
        }

        json::Value identity = json::Value::emptyObject();

        json::Value producer = json::Value::emptyObject();
        json::Value profile  = json::Value::emptyObject();
        json::Value check    = json::Value::emptyObject();
        const bool  built =
            setMember(producer, "name", json::Value::string("mdux-verify-ui"))
            && setMember(producer, "version", json::Value::string(MDUX_TOOL_VERSION))
            && setMember(producer, "source", json::Value::string(MDUX_BUILD_DIAGNOSTIC_SHA))
            && setMember(profile, "id", json::Value::string("MEDUI-PROFILE-RENDERED"))
            && setMember(profile, "version", json::Value::unsignedInteger(1))
            && setMember(check, "id", json::Value::string(std::string{checkId}))
            && setMember(check, "version", json::Value::unsignedInteger(1))
            && setMember(identity, "contract", json::Value::string(contract))
            && setMember(identity, "producer", std::move(producer))
            && setMember(identity, "profile", std::move(profile))
            && setMember(identity, "artifact", json::Value::string(artifactSha))
            && setMember(identity, "assets", std::move(*assets))
            && setMember(identity, "screen", json::Value::string(std::string{screenId}))
            && setMember(identity, "node", json::Value::string(outcome.nodeId))
            && setMember(identity, "locale", localeFree ? json::Value::null() : json::Value::string(std::string{locale}))
            && setMember(identity, "scenario", json::Value::string("static"))
            && setMember(identity, "capture", json::Value::string(std::format("{}.{}", screenId, scope)))
            && setMember(identity, "frame", json::Value::unsignedInteger(0))
            && setMember(identity, "check", std::move(check))
            && setMember(identity, "backend", json::Value::string(result.backend))
            && setMember(identity, "configuration", json::Value::string(configuration));
        if (!built) {
            return err(EvidenceError::SerializationFailed);
        }

        json::Value row = json::Value::emptyObject();
        if (!setMember(row, "identity", json::Value{identity}) || !setMember(row, "outcome", json::Value::string(std::string{rowOutcome(outcome.finding)}))) {
            return err(EvidenceError::SerializationFailed);
        }
        obligations.push_back(std::move(identity));
        rows.push_back(std::move(row));
    }

    if (obligations.empty()) {
        return err(EvidenceError::NoMappableObligation);
    }

    json::Value envelope = json::Value::emptyObject();
    if (!setMember(envelope, "obligations", json::Value::array(std::move(obligations)))
        || !setMember(envelope, "rows", json::Value::array(std::move(rows)))) {
        return err(EvidenceError::SerializationFailed);
    }

    return RenderedEvidence{.envelope = std::move(envelope), .excludedOutcomes = excluded};
}

}  // namespace mdux::tools::verify
