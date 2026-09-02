/**
 * @file Artifact.cpp
 * @brief Implementation of `verification.json` and the report members it adds.
 */
module;

module mdux.tools.verify.artifact;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.evidence.report;
import mdux.tools.verify.driver;
import mdux.verify;

namespace mdux::tools::verify {
namespace {

namespace evj = mdux::evidence::json;

using mdux::core::err;

/// Sets a member on a freshly built object, turning canonical JSON's refusal into this module's
/// error rather than dropping the field. Every key here is a distinct literal, so a duplicate is
/// impossible today; it is still checked, so a future edit that repeats one fails loudly.
[[nodiscard]] bool put(evj::Value& object, std::string key, evj::Value value) {
    return object.set(std::move(key), std::move(value)).has_value();
}

/// One bound artifact as the file records it: what it was to this run, which artifact it is, and
/// its digest. `locale` appears only where it means something, so a shader package does not carry
/// an empty locale that reads as "no locale was approved".
[[nodiscard]] std::optional<evj::Value> inputToJson(const BoundArtifact& input) {
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

/**
 * @brief One outcome as the file records it: the obligation, and whether it held.
 *
 * Four members and no fifth. `check` and `scope` are what keep a claim scoped to the obligation it
 * came from - a node verified in `en-US` and not in `de-DE` shows up as two entries with different
 * findings rather than as one summary - and `finding` is a named reason rather than a boolean so
 * that "nothing was drawn there" and "it was drawn in the wrong colour" stay distinguishable.
 */
[[nodiscard]] std::optional<evj::Value> outcomeToJson(const Outcome& outcome) {
    evj::Value entry = evj::Value::emptyObject();
    if (!put(entry, "check", evj::Value::string(outcome.check)) || !put(entry, "finding", evj::Value::string(std::string{mdux::verify::spell(outcome.finding)}))
        || !put(entry, "nodeId", evj::Value::string(outcome.nodeId)) || !put(entry, "scope", evj::Value::string(outcome.scope))) {
        return std::nullopt;
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
        case ArtifactError::MalformedReport:
            return "the screen bundle's report.json is not a bake report";
        case ArtifactError::ReportRewriteFailed:
            return "the extended bake report failed its own validation";
        case ArtifactError::SerializationFailed:
            return "canonical JSON refused a verification member";
    }
    return "unknown verification artifact error";
}

mdux::core::Result<std::string, ArtifactError> writeVerification(const RunResult& result, std::string_view screenId) {
    // Only a run that actually rendered and evaluated may be serialized. ADR-014 decision 3 makes a
    // skipped check a failure; #254 adds that a run which could not be made must fail artifact
    // production rather than commit a file that looks like a verification of nothing.
    if (result.state != RunState::Passed && result.state != RunState::ChecksFailed) {
        return err(ArtifactError::NotRun);
    }
    if (result.obligations.empty()) {
        return err(ArtifactError::NoObligations);
    }
    if (result.outcomes.size() != result.obligations.size()) {
        return err(ArtifactError::OutcomeMismatch);
    }
    // Equal counts are not coverage. The driver appends an outcome per obligation in one pass, so
    // these agree today - but this writer is what turns them into committed evidence, and an
    // outcome recorded against an obligation the run did not enumerate is exactly the claim the
    // artifact must not be able to make. Checking the pairing costs one comparison per obligation.
    for (std::size_t index = 0; index < result.obligations.size(); ++index) {
        const Obligation& obligation = result.obligations[index];
        const Outcome&    outcome    = result.outcomes[index];
        if (outcome.nodeId != obligation.nodeId || outcome.scope != obligation.scope || outcome.check != obligation.check) {
            return err(ArtifactError::OutcomeMismatch);
        }
    }

    std::vector<evj::Value> inputs;
    inputs.reserve(result.inputs.size());
    for (const BoundArtifact& input : result.inputs) {
        auto entry = inputToJson(input);
        if (!entry.has_value())
            return err(ArtifactError::SerializationFailed);
        inputs.push_back(std::move(*entry));
    }

    std::vector<evj::Value> outcomes;
    outcomes.reserve(result.outcomes.size());
    for (const Outcome& outcome : result.outcomes) {
        auto entry = outcomeToJson(outcome);
        if (!entry.has_value())
            return err(ArtifactError::SerializationFailed);
        outcomes.push_back(std::move(*entry));
    }

    // The render scopes, in the order the run rendered them, so a reader can see that every
    // approved locale was covered rather than inferring it from the outcome list.
    std::vector<evj::Value> scopes;
    for (const Obligation& obligation : result.obligations) {
        const bool seen = std::ranges::any_of(scopes, [&obligation](const evj::Value& scope) {
            const auto text = scope.asString();
            return text.has_value() && *text == obligation.scope;
        });
        if (!seen)
            scopes.push_back(evj::Value::string(obligation.scope));
    }

    const evidence::PackageHeader header{.schemaVersion = evidence::kSchemaVersion, .id = std::string{screenId}, .kind = "screen"};
    evj::Value                    document = evj::Value::emptyObject();
    if (!header.writeInto(document).has_value()) {
        return err(ArtifactError::SerializationFailed);
    }
    if (!put(document, "inputs", evj::Value::array(std::move(inputs))) || !put(document, "outcomes", evj::Value::array(std::move(outcomes)))
        || !put(document, "renderScopes", evj::Value::array(std::move(scopes)))) {
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
    for (const Obligation& obligation : result.obligations) {
        const bool seen = std::ranges::any_of(locales, [&obligation](const evj::Value& locale) {
            const auto text = locale.asString();
            return text.has_value() && *text == obligation.scope;
        });
        if (!seen)
            locales.push_back(evj::Value::string(obligation.scope));
    }

    evj::Value options = evj::Value::emptyObject();
    if (!put(options, "locales", evj::Value::array(std::move(locales)))) {
        return err(ArtifactError::SerializationFailed);
    }
    return options;
}

mdux::core::Result<std::string, ArtifactError> extendReport(std::string_view reportText, std::string_view verificationJson, const evj::Value& options) {
    auto report = evidence::BakeReport::parse(reportText);
    if (!report.has_value()) {
        return err(ArtifactError::MalformedReport);
    }

    // Appended rather than inserted: `mdux-meduic` writes `goldens.json` then `package.json`, so
    // appending `verification.json` keeps the list in the sorted order a reader expects. report.json
    // stays absent from its own outputs - a file cannot carry its own digest.
    const auto bytes = std::as_bytes(std::span{verificationJson.data(), verificationJson.size()});
    report->outputs.push_back({.path = std::string{verificationFileName}, .sha256 = evidence::sha256(bytes)});

    evj::Value resolved = report->options.kind() == evj::Value::Kind::Object ? report->options : evj::Value::emptyObject();
    if (!put(resolved, "verification", options)) {
        return err(ArtifactError::SerializationFailed);
    }
    report->options = std::move(resolved);

    auto text = report->write();
    if (!text.has_value()) {
        return err(ArtifactError::ReportRewriteFailed);
    }
    return *text;
}

}  // namespace mdux::tools::verify
