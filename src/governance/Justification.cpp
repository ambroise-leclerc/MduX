/**
 * @file Justification.cpp
 * @brief Justification serialization and validation for the governed governance zone.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-005 Error handling and exceptions policy
 *
 * Every check here has a direct counterpart in docs/governance/schemas/justification.schema.json
 * - this file and that schema should be read side by side if either one changes.
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

namespace {

/// `JUS-` followed by three or more digits - matches JUSTIFICATION_ID_RE in
/// tools/docs-lint/mdux_docs_lint.py exactly.
[[nodiscard]] bool isWellFormedJustificationId(std::string_view id) noexcept {
    constexpr std::string_view prefix = "JUS-";
    if (!id.starts_with(prefix)) {
        return false;
    }
    const std::string_view digits = id.substr(prefix.size());
    if (digits.size() < 3) {
        return false;
    }
    return std::ranges::all_of(digits, [](char c) { return c >= '0' && c <= '9'; });
}

/// `REQ-` followed by at least one uppercase letter, digit, or hyphen - matches
/// REQUIRED_JUSTIFICATION_FIELDS' sibling pattern in the JSON Schema.
[[nodiscard]] bool isWellFormedRequirementId(std::string_view id) noexcept {
    constexpr std::string_view prefix = "REQ-";
    if (!id.starts_with(prefix)) {
        return false;
    }
    const std::string_view rest = id.substr(prefix.size());
    if (rest.empty()) {
        return false;
    }
    return std::ranges::all_of(rest, [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
    });
}

/// Extracts the standard from the exact `<Standard> §` prefix required by the schema.
/// In particular, this rejects a missing space or multiple spaces before `§`; accepting either
/// would make the C++ validator looser than the JSON Schema it claims to mirror.
[[nodiscard]] std::optional<std::string_view> clauseRefStandardPrefix(
    std::string_view clauseRef) noexcept {
    const std::size_t markerPos = clauseRef.find("§");
    if (markerPos == std::string_view::npos || markerPos < 2 || clauseRef[markerPos - 1] != ' ' ||
        clauseRef[markerPos - 2] == ' ') {
        return std::nullopt;
    }
    return clauseRef.substr(0, markerPos - 1);
}

/// A clause number is one or more digits, followed by zero or more (dot, one-or-more-digits)
/// groups - e.g. "5", "5.3", "5.1.2", but not ".3" or "5." or "5..3" - followed by a space and
/// at least one character of title text. Matches `§[0-9]+(\.[0-9]+)* .+` from
/// justification.schema.json exactly, group by group rather than as a looser "digits and dots"
/// scan, which is what let a leading-dot clause like "§.3 title" through in an earlier version
/// of this function (caught by a test, not by inspection). Deliberately no support for a clause
/// *range* (as `docs/<standard>/AI-Reference.md`'s generated index displays for a section
/// covering several clauses at once): a single Justification cites one clause, and the schema
/// this mirrors has never allowed otherwise.
[[nodiscard]] bool hasWellFormedClauseAndTitle(std::string_view afterMarker) noexcept {
    auto consumeDigits = [](std::string_view text, std::size_t& pos) -> bool {
        const std::size_t start = pos;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            ++pos;
        }
        return pos > start;
    };

    std::size_t i = 0;
    if (!consumeDigits(afterMarker, i)) {
        return false;
    }
    while (i < afterMarker.size() && afterMarker[i] == '.') {
        const std::size_t dotPos = i;
        ++i;  // consume '.'
        if (!consumeDigits(afterMarker, i)) {
            i = dotPos;  // no digits followed the dot - the dot belongs to what comes next, not us
            break;
        }
    }

    std::string_view remaining = afterMarker.substr(i);
    if (remaining.empty() || remaining.front() != ' ') {
        return false;
    }
    remaining.remove_prefix(1);
    return !remaining.empty();
}

}  // namespace

bool isApprovedStandard(std::string_view standard) noexcept {
    return std::ranges::find(kApprovedStandards, standard) != kApprovedStandards.end();
}

std::string_view describe(GovernanceError error) noexcept {
    switch (error) {
    case GovernanceError::EmptyJustificationId:      return "justificationId is empty";
    case GovernanceError::MalformedJustificationId:  return "justificationId does not match JUS-NNN";
    case GovernanceError::UnapprovedStandard:        return "standard is not an approved identifier";
    case GovernanceError::EmptyClauseRef:            return "clauseRef is empty";
    case GovernanceError::MalformedClauseRef:        return "clauseRef is not a complete citation key";
    case GovernanceError::ClauseRefStandardMismatch: return "clauseRef names a different standard than 'standard'";
    case GovernanceError::EmptyRationale:            return "rationale is empty";
    case GovernanceError::EmptyEvidenceRefs:         return "evidenceRefs is empty";
    case GovernanceError::EmptyEvidenceRef:          return "evidenceRefs contains an empty entry";
    case GovernanceError::DuplicateEvidenceRef:      return "evidenceRefs contains a duplicate entry";
    case GovernanceError::MalformedJustification:    return "Justification JSON has an unexpected shape";
    }
    return "unrecognized governance error";
}

ResultVoid<GovernanceError> Justification::validate() const noexcept {
    if (justificationId.empty()) {
        return err(GovernanceError::EmptyJustificationId);
    }
    if (!isWellFormedJustificationId(justificationId)) {
        return err(GovernanceError::MalformedJustificationId);
    }
    if (!isApprovedStandard(standard)) {
        return err(GovernanceError::UnapprovedStandard);
    }
    if (clauseRef.empty()) {
        return err(GovernanceError::EmptyClauseRef);
    }
    const auto clauseStandard = clauseRefStandardPrefix(clauseRef);
    if (!clauseStandard.has_value()) {
        return err(GovernanceError::MalformedClauseRef);
    }
    if (!isApprovedStandard(*clauseStandard)) {
        return err(GovernanceError::MalformedClauseRef);
    }
    const std::size_t markerPos = clauseRef.find("§");
    if (!hasWellFormedClauseAndTitle(clauseRef.substr(markerPos + std::string_view{"§"}.size()))) {
        return err(GovernanceError::MalformedClauseRef);
    }
    if (*clauseStandard != standard) {
        return err(GovernanceError::ClauseRefStandardMismatch);
    }
    if (rationale.empty()) {
        return err(GovernanceError::EmptyRationale);
    }
    if (requirementId.has_value() && !isWellFormedRequirementId(*requirementId)) {
        // Malformed requirementId reuses MalformedJustification: it is a distinct-enough shape
        // error that a dedicated code is not worth adding for an optional field with no schema
        // consumer yet (issue #34 predates any code that reads requirementId back).
        return err(GovernanceError::MalformedJustification);
    }
    if (evidenceRefs.empty()) {
        return err(GovernanceError::EmptyEvidenceRefs);
    }
    for (std::size_t i = 0; i < evidenceRefs.size(); ++i) {
        if (evidenceRefs[i].empty()) {
            return err(GovernanceError::EmptyEvidenceRef);
        }
        for (std::size_t k = i + 1; k < evidenceRefs.size(); ++k) {
            if (evidenceRefs[i] == evidenceRefs[k]) {
                return err(GovernanceError::DuplicateEvidenceRef);
            }
        }
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
    if (requirementId.has_value() && !setMember("requirement_id", json::Value::string(*requirementId))) {
        return err(GovernanceError::MalformedJustification);
    }

    json::Value refs = json::Value::array({});
    for (const std::string& ref : evidenceRefs) {
        if (auto pushed = refs.push(json::Value::string(ref)); !pushed.has_value()) {
            return err(GovernanceError::MalformedJustification);
        }
    }
    if (!setMember("evidence_refs", std::move(refs))) {
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

namespace {

[[nodiscard]] Result<std::string, GovernanceError> requireString(const json::Value& object,
                                                                  std::string_view key) noexcept {
    const auto member = object.require(key);
    if (!member.has_value()) {
        return err(GovernanceError::MalformedJustification);
    }
    const auto text = (*member)->asString();
    if (!text.has_value()) {
        return err(GovernanceError::MalformedJustification);
    }
    return std::string{*text};
}

}  // namespace

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
    if (document->members().size() != expectedMembers) {
        // Mirrors additionalProperties: false in justification.schema.json.
        return err(GovernanceError::MalformedJustification);
    }

    Justification result;

    auto jid = requireString(*document, "justification_id");
    if (!jid.has_value()) {
        return err(jid.error());
    }
    result.justificationId = std::move(*jid);

    auto standard = requireString(*document, "standard");
    if (!standard.has_value()) {
        return err(standard.error());
    }
    result.standard = std::move(*standard);

    auto clauseRef = requireString(*document, "clause_ref");
    if (!clauseRef.has_value()) {
        return err(clauseRef.error());
    }
    result.clauseRef = std::move(*clauseRef);

    auto rationale = requireString(*document, "rationale");
    if (!rationale.has_value()) {
        return err(rationale.error());
    }
    result.rationale = std::move(*rationale);

    if (const auto* member = document->find("requirement_id"); member != nullptr) {
        const auto text2 = member->asString();
        if (!text2.has_value()) {
            return err(GovernanceError::MalformedJustification);
        }
        result.requirementId = std::string{*text2};
    }

    const auto refsJson = document->require("evidence_refs");
    if (!refsJson.has_value()) {
        return err(GovernanceError::MalformedJustification);
    }
    if ((*refsJson)->kind() != json::Value::Kind::Array) {
        return err(GovernanceError::MalformedJustification);
    }
    for (const json::Value& element : (*refsJson)->elements()) {
        const auto elementText = element.asString();
        if (!elementText.has_value()) {
            return err(GovernanceError::MalformedJustification);
        }
        result.evidenceRefs.emplace_back(*elementText);
    }

    if (auto valid = result.validate(); !valid.has_value()) {
        return err(valid.error());
    }
    return result;
}

}  // namespace mdux::governance
