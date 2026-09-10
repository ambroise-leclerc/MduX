/**
 * @file ScenarioArtifact.cpp
 * @brief Implementation of `scenario-verification.json` and the report members it adds.
 */
module;

module mdux.tools.verify.scenario.artifact;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.tools.verify.scenario.driver;
import mdux.verify;

namespace mdux::tools::verify::scenario {
namespace {

namespace evj = mdux::evidence::json;

using mdux::core::err;

[[nodiscard]] bool put(evj::Value& object, std::string key, evj::Value value) {
    return object.set(std::move(key), std::move(value)).has_value();
}

[[nodiscard]] std::optional<evj::Value> inputToJson(const mdux::tools::verify::BoundArtifact& input) {
    evj::Value entry = evj::Value::emptyObject();
    if (!put(entry, "id", evj::Value::string(input.id)) || !put(entry, "role", evj::Value::string(input.role))
        || !put(entry, "sha256", evj::Value::string(input.sha256))) {
        return std::nullopt;
    }
    if (!input.locale.empty() && !put(entry, "locale", evj::Value::string(input.locale))) {
        return std::nullopt;
    }
    return entry;
}

/// One outcome as the file records it. `finding` is the governed `Finding` spelling for a rendered
/// obligation and a plain `Held`/`Failed` for a binding or capture one - the two are different
/// claims and are not folded into a boolean.
[[nodiscard]] std::optional<evj::Value> outcomeToJson(const Outcome& outcome) {
    evj::Value entry = evj::Value::emptyObject();
    if (!put(entry, "kind", evj::Value::string(std::string{toWire(outcome.kind)})) || !put(entry, "scope", evj::Value::string(outcome.scope))) {
        return std::nullopt;
    }

    switch (outcome.kind) {
        case ObligationKind::Binding:
            if (!put(entry, "step", evj::Value::unsignedInteger(outcome.stepIndex))
                || !put(entry, "expectKind", evj::Value::string(outcome.expectKind))
                || !put(entry, "finding", evj::Value::string(outcome.held ? "Held" : "Failed"))) {
                return std::nullopt;
            }
            break;
        case ObligationKind::Capture:
            if (!put(entry, "capture", evj::Value::string(outcome.capture))
                || !put(entry, "finding", evj::Value::string(outcome.held ? "Held" : "Failed"))) {
                return std::nullopt;
            }
            break;
        case ObligationKind::Rendered: {
            evj::Value profile = evj::Value::emptyObject();
            if (!put(profile, "id", evj::Value::string(std::string{outcome.profile.id()}))
                || !put(profile, "version", evj::Value::unsignedInteger(outcome.profile.version()))) {
                return std::nullopt;
            }
            if (!put(entry, "capture", evj::Value::string(outcome.capture)) || !put(entry, "nodeId", evj::Value::string(outcome.nodeId))
                || !put(entry, "check", evj::Value::string(outcome.check))
                || !put(entry, "finding", evj::Value::string(std::string{mdux::verify::spell(outcome.finding)}))
                || !put(entry, "observationProfile", std::move(profile))) {
                return std::nullopt;
            }
            if (const auto canonical = mdux::verify::canonicalRenderedCheckFor(outcome.profile); canonical.has_value()) {
                evj::Value candidateProfile = evj::Value::emptyObject();
                evj::Value candidateCheck   = evj::Value::emptyObject();
                if (!put(candidateProfile, "id", evj::Value::string(std::string{canonical->profileId}))
                    || !put(candidateProfile, "version", evj::Value::unsignedInteger(canonical->profileVersion))
                    || !put(candidateCheck, "id", evj::Value::string(std::string{canonical->checkId}))
                    || !put(candidateCheck, "version", evj::Value::unsignedInteger(canonical->checkVersion))) {
                    return std::nullopt;
                }
                evj::Value candidate = evj::Value::emptyObject();
                if (!put(candidate, "check", std::move(candidateCheck)) || !put(candidate, "profile", std::move(candidateProfile))
                    || !put(entry, "candidateProfile", std::move(candidate))) {
                    return std::nullopt;
                }
            }
            break;
        }
    }
    return entry;
}

}  // namespace

std::string_view describe(ArtifactError error) noexcept {
    switch (error) {
        case ArtifactError::NotRun:
            return "the verification run could not be made, so there are no outcomes to record";
        case ArtifactError::NoObligations:
            return "the run discharged no obligations, and a verification of nothing is not evidence";
        case ArtifactError::OutcomeMismatch:
            return "the run produced a different number of outcomes than it enumerated obligations";
        case ArtifactError::RenderedProfileInvalid:
            return "a rendered outcome carries no observation profile, or one that is not its check's";
        case ArtifactError::MalformedReport:
            return "the scenario bundle's report.json is not a bake report";
        case ArtifactError::ReportRewriteFailed:
            return "the extended bake report failed its own validation";
        case ArtifactError::SerializationFailed:
            return "canonical JSON refused a verification member";
        case ArtifactError::PublishFailed:
            return "the bundle could not be written; it has been left as it was";
    }
    return "unknown scenario verification artifact error";
}

mdux::core::Result<std::string, ArtifactError> writeScenarioVerification(const RunResult& result, std::string_view scenarioId) {
    if (result.state != RunState::Passed && result.state != RunState::ChecksFailed) {
        return err(ArtifactError::NotRun);
    }
    if (result.obligations.empty()) {
        return err(ArtifactError::NoObligations);
    }
    if (result.outcomes.size() != result.obligations.size()) {
        return err(ArtifactError::OutcomeMismatch);
    }
    // Every rendered outcome's profile must be exactly the one its check reports under, the same
    // "derive, don't trust" rule the screen bundle applies (ADR-014 D2, ADR-016).
    for (const Outcome& outcome : result.outcomes) {
        if (outcome.kind != ObligationKind::Rendered) {
            continue;
        }
        const auto expected = mdux::verify::profileForCheckName(outcome.check);
        if (!expected.has_value() || !outcome.profile.valid() || outcome.profile != *expected) {
            return err(ArtifactError::RenderedProfileInvalid);
        }
    }

    std::vector<evj::Value> inputs;
    inputs.reserve(result.inputs.size());
    for (const auto& input : result.inputs) {
        auto entry = inputToJson(input);
        if (!entry.has_value()) {
            return err(ArtifactError::SerializationFailed);
        }
        inputs.push_back(std::move(*entry));
    }

    std::vector<evj::Value> outcomes;
    outcomes.reserve(result.outcomes.size());
    for (const Outcome& outcome : result.outcomes) {
        auto entry = outcomeToJson(outcome);
        if (!entry.has_value()) {
            return err(ArtifactError::SerializationFailed);
        }
        outcomes.push_back(std::move(*entry));
    }

    std::vector<evj::Value> scopes;
    std::vector<evj::Value> captures;
    for (const Obligation& obligation : result.obligations) {
        const auto seen = [](std::vector<evj::Value>& into, std::string_view value) {
            const bool present = std::ranges::any_of(into, [value](const evj::Value& v) {
                const auto text = v.asString();
                return text.has_value() && *text == value;
            });
            if (!present && !value.empty()) {
                into.push_back(evj::Value::string(std::string{value}));
            }
        };
        seen(scopes, obligation.scope);
        seen(captures, obligation.capture);
    }

    const evidence::PackageHeader header{.schemaVersion = evidence::kSchemaVersion, .id = std::string{scenarioId}, .kind = "scenario"};
    evj::Value                    document = evj::Value::emptyObject();
    if (!header.writeInto(document).has_value()) {
        return err(ArtifactError::SerializationFailed);
    }
    if (!put(document, "captures", evj::Value::array(std::move(captures))) || !put(document, "inputs", evj::Value::array(std::move(inputs)))
        || !put(document, "outcomes", evj::Value::array(std::move(outcomes))) || !put(document, "renderScopes", evj::Value::array(std::move(scopes)))) {
        return err(ArtifactError::SerializationFailed);
    }

    auto text = evj::write(document);
    if (!text.has_value()) {
        return err(ArtifactError::SerializationFailed);
    }
    return *text;
}

mdux::core::Result<evj::Value, ArtifactError> verificationOptions(const RunResult& result) {
    std::vector<evj::Value> locales;
    std::vector<evj::Value> captures;
    for (const Obligation& obligation : result.obligations) {
        const auto seen = [](std::vector<evj::Value>& into, std::string_view value) {
            if (value.empty()) {
                return;
            }
            const bool present = std::ranges::any_of(into, [value](const evj::Value& v) {
                const auto text = v.asString();
                return text.has_value() && *text == value;
            });
            if (!present) {
                into.push_back(evj::Value::string(std::string{value}));
            }
        };
        seen(locales, obligation.scope);
        seen(captures, obligation.capture);
    }

    evj::Value options = evj::Value::emptyObject();
    if (!put(options, "captures", evj::Value::array(std::move(captures))) || !put(options, "locales", evj::Value::array(std::move(locales)))) {
        return err(ArtifactError::SerializationFailed);
    }
    return options;
}

mdux::core::Result<std::string, ArtifactError>
extendScenarioReport(std::string_view reportText, std::string_view verificationJson, const evj::Value& options, std::string_view toolVersion) {
    auto report = evidence::BakeReport::parse(reportText);
    if (!report.has_value()) {
        return err(ArtifactError::MalformedReport);
    }

    const auto bytes = std::as_bytes(std::span{verificationJson.data(), verificationJson.size()});
    report->outputs.push_back({.path = std::string{verificationFileName}, .sha256 = evidence::sha256(bytes)});

    evj::Value resolved = report->options.kind() == evj::Value::Kind::Object ? report->options : evj::Value::emptyObject();
    if (!put(resolved, "verification", options)) {
        return err(ArtifactError::SerializationFailed);
    }
    report->options = std::move(resolved);

    report->stages.push_back({.tool = std::string{artifactToolName}, .toolVersion = std::string{toolVersion}, .output = std::string{verificationFileName}});

    auto text = report->write();
    if (!text.has_value()) {
        return err(ArtifactError::ReportRewriteFailed);
    }
    return *text;
}

}  // namespace mdux::tools::verify::scenario
