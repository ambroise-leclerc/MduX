/**
 * @file Governance.cpp
 * @brief Vocabulary shared by every governance record: error descriptions, the approved-standard
 *        set, the closed enums' wire spellings, and the small shape checks the record types
 *        would otherwise each reimplement slightly differently.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-005 Error handling and exceptions policy
 *
 * Every check in `detail` has a counterpart in the JSON Schemas under docs/<standard>/schemas/
 * and in docs/governance/schemas/justification.schema.json. This file and those schemas should
 * be read side by side if either one changes; tools/docs-lint/check_schema_type_drift.py fails
 * the build when they stop agreeing.
 */
module;

module mdux.governance;

import std;
import mdux.core.result;
import mdux.evidence.json;

namespace mdux::governance {

using mdux::core::err;
using mdux::core::Result;
namespace json = evidence::json;

bool isApprovedStandard(std::string_view standard) noexcept {
    return std::ranges::find(kApprovedStandards, standard) != kApprovedStandards.end();
}

std::string_view describe(GovernanceError error) noexcept {
    switch (error) {
    // Justification
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

    // Requirement
    case GovernanceError::EmptyRequirementId:        return "requirement id is empty";
    case GovernanceError::MalformedRequirementId:    return "requirement id does not match REQ-*";
    case GovernanceError::EmptyRequirementTitle:     return "requirement title is empty";
    case GovernanceError::EmptySourceClause:         return "requirement sourceClause is empty";
    case GovernanceError::MalformedSourceClause:     return "requirement sourceClause is not a complete citation key";
    case GovernanceError::EmptyVerificationIntent:   return "requirement verificationIntent is empty";

    // Hazard
    case GovernanceError::EmptyHazardId:             return "hazard id is empty";
    case GovernanceError::MalformedHazardId:         return "hazard id does not match HAZ-*";
    case GovernanceError::EmptyHazardDescription:    return "hazard description is empty";
    case GovernanceError::EmptyControls:             return "hazard controlledBy is empty - the hazard has no recorded risk control";
    case GovernanceError::EmptyControlRef:           return "hazard controlledBy contains an empty entry";
    case GovernanceError::DuplicateControlRef:       return "hazard controlledBy contains a duplicate entry";

    // VerificationCase
    case GovernanceError::EmptyVerificationCaseId:   return "verification case id is empty";
    case GovernanceError::MalformedVerificationCaseId: return "verification case id does not match VER-*";
    case GovernanceError::EmptyVerifiedRequirementId: return "verification case requirementId is empty";

    // ProblemReport
    case GovernanceError::EmptyProblemReportId:      return "problem report id is empty";
    case GovernanceError::MalformedProblemReportId:  return "problem report id does not match PRB-*";
    case GovernanceError::EmptyProblemDescription:   return "problem report description is empty";

    // AuditEvent
    case GovernanceError::EmptyAuditTimestamp:       return "audit event timestamp is empty";
    case GovernanceError::MalformedAuditTimestamp:   return "audit event timestamp is not an ISO 8601 UTC instant";
    case GovernanceError::EmptyAuditSubject:         return "audit event subject is empty";

    // ComplianceProgram
    case GovernanceError::DuplicateRequirementId:    return "two requirements share an id";
    case GovernanceError::DuplicateHazardId:         return "two hazards share an id";
    case GovernanceError::DuplicateVerificationCaseId: return "two verification cases share an id";
    case GovernanceError::DuplicateProblemReportId:  return "two problem reports share an id";
    case GovernanceError::DuplicateJustificationId:  return "two justifications share an id";
    case GovernanceError::UnverifiedRequirement:     return "no verification case covers this requirement";
    case GovernanceError::UnresolvedVerifiedRequirement: return "verification case names a requirement that does not exist";
    case GovernanceError::UnresolvedHazardControl:   return "hazard control names a requirement that does not exist";
    case GovernanceError::MalformedComplianceProgram: return "ComplianceProgram JSON has an unexpected shape";
    }
    return "unrecognized governance error";
}

// ---------------------------------------------------------------------------
// Closed vocabularies
//
// The k*WireValues arrays are the single definition of each set; toWireString indexes into them
// and *FromWire searches them. Adding an enumerator without extending its array is a compile
// error in toWireString's switch, which is the point of writing it this way rather than as a
// pair of independently-maintained switches that can drift apart.
// ---------------------------------------------------------------------------

std::string_view toWireString(SafetyClass value) noexcept {
    switch (value) {
    case SafetyClass::A: return kSafetyClassWireValues[0];
    case SafetyClass::B: return kSafetyClassWireValues[1];
    case SafetyClass::C: return kSafetyClassWireValues[2];
    }
    return {};
}

std::string_view toWireString(VerificationMethod value) noexcept {
    switch (value) {
    case VerificationMethod::Test:       return kVerificationMethodWireValues[0];
    case VerificationMethod::Analysis:   return kVerificationMethodWireValues[1];
    case VerificationMethod::Inspection: return kVerificationMethodWireValues[2];
    case VerificationMethod::Review:     return kVerificationMethodWireValues[3];
    }
    return {};
}

std::string_view toWireString(AuditCategory value) noexcept {
    switch (value) {
    case AuditCategory::Lifecycle:    return kAuditCategoryWireValues[0];
    case AuditCategory::Verification: return kAuditCategoryWireValues[1];
    case AuditCategory::Change:       return kAuditCategoryWireValues[2];
    }
    return {};
}

namespace {

/// Shared by the three *FromWire functions: exact match against the closed set, returning the
/// enumerator whose numeric value is the matching index.
template <typename Enum, std::size_t N>
[[nodiscard]] std::optional<Enum> fromWire(const std::array<std::string_view, N>& values,
                                           std::string_view text) noexcept {
    const auto it = std::ranges::find(values, text);
    if (it == values.end()) {
        return std::nullopt;
    }
    return static_cast<Enum>(static_cast<std::uint8_t>(std::ranges::distance(values.begin(), it)));
}

}  // namespace

std::optional<SafetyClass> safetyClassFromWire(std::string_view text) noexcept {
    return fromWire<SafetyClass>(kSafetyClassWireValues, text);
}

std::optional<VerificationMethod> verificationMethodFromWire(std::string_view text) noexcept {
    return fromWire<VerificationMethod>(kVerificationMethodWireValues, text);
}

std::optional<AuditCategory> auditCategoryFromWire(std::string_view text) noexcept {
    return fromWire<AuditCategory>(kAuditCategoryWireValues, text);
}

// ---------------------------------------------------------------------------
// detail
// ---------------------------------------------------------------------------

namespace detail {

bool isWellFormedId(std::string_view id, std::string_view prefix) noexcept {
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

bool isWellFormedJustificationId(std::string_view id) noexcept {
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

namespace {

/// A clause number is one or more digits, followed by zero or more (dot, one-or-more-digits)
/// groups - e.g. "5", "5.3", "5.1.2", but not ".3" or "5." or "5..3" - followed by a space and
/// at least one character of title text. Matches `§[0-9]+(\.[0-9]+)* .+` from
/// justification.schema.json exactly, group by group rather than as a looser "digits and dots"
/// scan, which is what let a leading-dot clause like "§.3 title" through in an earlier version
/// of this function (caught by a test, not by inspection). Deliberately no support for a clause
/// *range* (as `docs/<standard>/AI-Reference.md`'s generated index displays for a section
/// covering several clauses at once): a single citation key names one clause, and the schema
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

/// Extracts the standard from the exact `<Standard> §` prefix required by the schema.
/// In particular, this rejects a missing space or multiple spaces before `§`; accepting either
/// would make the C++ validator looser than the JSON Schema it claims to mirror.
[[nodiscard]] std::optional<std::string_view> standardPrefix(std::string_view key) noexcept {
    const std::size_t markerPos = key.find("§");
    if (markerPos == std::string_view::npos || markerPos < 2 || key[markerPos - 1] != ' ' ||
        key[markerPos - 2] == ' ') {
        return std::nullopt;
    }
    return key.substr(0, markerPos - 1);
}

}  // namespace

std::optional<std::string_view> citationKeyStandard(std::string_view key) noexcept {
    const auto prefix = standardPrefix(key);
    if (!prefix.has_value() || !isApprovedStandard(*prefix)) {
        return std::nullopt;
    }
    const std::size_t markerPos = key.find("§");
    if (!hasWellFormedClauseAndTitle(key.substr(markerPos + std::string_view{"§"}.size()))) {
        return std::nullopt;
    }
    return prefix;
}

bool isWellFormedUtcTimestamp(std::string_view text) noexcept {
    // "YYYY-MM-DDThh:mm:ssZ" - 20 characters, fixed positions, no alternative spellings.
    constexpr std::string_view pattern = "NNNN-NN-NNTNN:NN:NNZ";
    if (text.size() != pattern.size()) {
        return false;
    }
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char expected = pattern[i];
        const char actual = text[i];
        if (expected == 'N') {
            if (actual < '0' || actual > '9') {
                return false;
            }
        } else if (actual != expected) {
            return false;
        }
    }

    auto number = [text](std::size_t at, std::size_t width) {
        int value = 0;
        for (std::size_t i = at; i < at + width; ++i) {
            value = value * 10 + (text[i] - '0');
        }
        return value;
    };

    const int month = number(5, 2);
    const int day = number(8, 2);
    const int hour = number(11, 2);
    const int minute = number(14, 2);
    const int second = number(17, 2);
    // Day is checked against 31 rather than against the month's real length: this is a shape
    // check on an audit string, not a calendar. Rejecting 2025-02-30 would need leap-year rules
    // that buy nothing here - a wrong-but-plausible date is a data-entry problem no validator
    // catches, while a 40th day is a corrupted field.
    return month >= 1 && month <= 12 && day >= 1 && day <= 31 && hour <= 23 && minute <= 59 &&
           second <= 59;
}

std::optional<GovernanceError> checkRefList(std::span<const std::string> refs,
                                            GovernanceError whenEmpty,
                                            GovernanceError whenEntryEmpty,
                                            GovernanceError whenDuplicate) noexcept {
    if (refs.empty()) {
        return whenEmpty;
    }
    for (std::size_t i = 0; i < refs.size(); ++i) {
        if (refs[i].empty()) {
            return whenEntryEmpty;
        }
        for (std::size_t k = i + 1; k < refs.size(); ++k) {
            if (refs[i] == refs[k]) {
                return whenDuplicate;
            }
        }
    }
    return std::nullopt;
}

Result<std::string, GovernanceError> requireString(const json::Value& object, std::string_view key,
                                                   GovernanceError malformed) noexcept {
    const auto member = object.require(key);
    if (!member.has_value()) {
        return err(malformed);
    }
    const auto text = (*member)->asString();
    if (!text.has_value()) {
        return err(malformed);
    }
    return std::string{*text};
}

Result<bool, GovernanceError> requireBool(const json::Value& object, std::string_view key,
                                          GovernanceError malformed) noexcept {
    const auto member = object.require(key);
    if (!member.has_value()) {
        return err(malformed);
    }
    const auto value = (*member)->asBool();
    if (!value.has_value()) {
        return err(malformed);
    }
    return *value;
}

Result<std::vector<std::string>, GovernanceError> requireStringArray(
    const json::Value& object, std::string_view key, GovernanceError malformed) noexcept {
    const auto member = object.require(key);
    if (!member.has_value()) {
        return err(malformed);
    }
    if ((*member)->kind() != json::Value::Kind::Array) {
        return err(malformed);
    }
    std::vector<std::string> result;
    for (const json::Value& element : (*member)->elements()) {
        const auto text = element.asString();
        if (!text.has_value()) {
            return err(malformed);
        }
        result.emplace_back(*text);
    }
    return result;
}

Result<json::Value, GovernanceError> stringArray(std::span<const std::string> values,
                                                 GovernanceError malformed) noexcept {
    json::Value array = json::Value::array({});
    for (const std::string& value : values) {
        if (auto pushed = array.push(json::Value::string(value)); !pushed.has_value()) {
            return err(malformed);
        }
    }
    return array;
}

bool hasExactly(const json::Value& object, std::size_t expected) noexcept {
    return object.kind() == json::Value::Kind::Object && object.members().size() == expected;
}

}  // namespace detail

}  // namespace mdux::governance
