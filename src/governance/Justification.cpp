/**
 * @file Justification.cpp
 * @brief Justification serialization and validation for the governed governance zone.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-005 Error handling and exceptions policy
 *
 * Every check here has a direct counterpart in docs/governance/schemas/justification.schema.json
 * - this file and that schema should be read side by side if either one changes. The shape
 * checks themselves live in `detail` (src/governance/Governance.cpp), shared with the lifecycle
 * records so an id or a citation key means the same thing everywhere in this module.
 */
module;

module mdux.governance;

import std;
import mdux.core.result;
import mdux.evidence.json;

namespace mdux::governance {

using mdux::core::err;
using mdux::core::Result;
using mdux::core::ResultVoid;
namespace json = evidence::json;

ResultVoid<GovernanceError> Justification::validate() const noexcept {
    if (justificationId.empty()) {
        return err(GovernanceError::EmptyJustificationId);
    }
    if (!detail::isWellFormedJustificationId(justificationId)) {
        return err(GovernanceError::MalformedJustificationId);
    }
    if (!isApprovedStandard(standard)) {
        return err(GovernanceError::UnapprovedStandard);
    }
    if (clauseRef.empty()) {
        return err(GovernanceError::EmptyClauseRef);
    }
    const auto clauseStandard = detail::citationKeyStandard(clauseRef);
    if (!clauseStandard.has_value()) {
        return err(GovernanceError::MalformedClauseRef);
    }
    if (*clauseStandard != standard) {
        return err(GovernanceError::ClauseRefStandardMismatch);
    }
    if (rationale.empty()) {
        return err(GovernanceError::EmptyRationale);
    }
    if (requirementId.has_value() && !detail::isWellFormedId(*requirementId, "REQ-")) {
        return err(GovernanceError::MalformedRequirementId);
    }
    if (const auto refs = detail::checkRefList(evidenceRefs, GovernanceError::EmptyEvidenceRefs,
                                               GovernanceError::EmptyEvidenceRef,
                                               GovernanceError::DuplicateEvidenceRef)) {
        return err(*refs);
    }
    return {};
}

Result<json::Value, GovernanceError> Justification::toJson() const noexcept {
    if (auto valid = validate(); !valid.has_value()) {
        return err(valid.error());
    }

    json::Value object = json::Value::emptyObject();
    auto setMember = [&object](std::string key, json::Value value) -> bool {
        return object.set(std::move(key), std::move(value)).has_value();
    };

    if (!setMember("justification_id", json::Value::string(justificationId)) ||
        !setMember("standard", json::Value::string(standard)) ||
        !setMember("clause_ref", json::Value::string(clauseRef)) ||
        !setMember("rationale", json::Value::string(rationale))) {
        return err(GovernanceError::MalformedJustification);
    }
    if (requirementId.has_value() &&
        !setMember("requirement_id", json::Value::string(*requirementId))) {
        return err(GovernanceError::MalformedJustification);
    }

    auto refs = detail::stringArray(evidenceRefs, GovernanceError::MalformedJustification);
    if (!refs.has_value()) {
        return err(refs.error());
    }
    if (!setMember("evidence_refs", std::move(*refs))) {
        return err(GovernanceError::MalformedJustification);
    }

    return object;
}

Result<std::string, GovernanceError> Justification::write() const noexcept {
    auto object = toJson();
    if (!object.has_value()) {
        return err(object.error());
    }
    auto text = json::write(*object);
    if (!text.has_value()) {
        return err(GovernanceError::MalformedJustification);
    }
    return *text;
}

Result<Justification, GovernanceError> Justification::parse(std::string_view text) noexcept {
    const auto document = json::parse(text);
    if (!document.has_value()) {
        return err(GovernanceError::MalformedJustification);
    }
    if (document->kind() != json::Value::Kind::Object) {
        return err(GovernanceError::MalformedJustification);
    }
    const std::size_t expectedMembers =
        document->find("requirement_id") == nullptr ? std::size_t{5} : std::size_t{6};
    if (!detail::hasExactly(*document, expectedMembers)) {
        // Mirrors additionalProperties: false in justification.schema.json.
        return err(GovernanceError::MalformedJustification);
    }

    constexpr GovernanceError malformed = GovernanceError::MalformedJustification;
    Justification result;

    auto jid = detail::requireString(*document, "justification_id", malformed);
    if (!jid.has_value()) {
        return err(jid.error());
    }
    result.justificationId = std::move(*jid);

    auto standard = detail::requireString(*document, "standard", malformed);
    if (!standard.has_value()) {
        return err(standard.error());
    }
    result.standard = std::move(*standard);

    auto clauseRef = detail::requireString(*document, "clause_ref", malformed);
    if (!clauseRef.has_value()) {
        return err(clauseRef.error());
    }
    result.clauseRef = std::move(*clauseRef);

    auto rationale = detail::requireString(*document, "rationale", malformed);
    if (!rationale.has_value()) {
        return err(rationale.error());
    }
    result.rationale = std::move(*rationale);

    if (document->find("requirement_id") != nullptr) {
        auto requirementId = detail::requireString(*document, "requirement_id", malformed);
        if (!requirementId.has_value()) {
            return err(requirementId.error());
        }
        result.requirementId = std::move(*requirementId);
    }

    auto refs = detail::requireStringArray(*document, "evidence_refs", malformed);
    if (!refs.has_value()) {
        return err(refs.error());
    }
    result.evidenceRefs = std::move(*refs);

    if (auto valid = result.validate(); !valid.has_value()) {
        return err(valid.error());
    }
    return result;
}

}  // namespace mdux::governance
