/**
 * @file Schema.cppm
 * @brief A dependency-free validator for the JSON Schema subset MedUI's repository contracts use.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-007 Evidence pipeline doctrine (canonical JSON)
 *
 * A port of `Compliatory/MedUI`'s `tools/schema_check.py`: it validates *documents* against a
 * schema, not implementations, and it **fails closed** on any schema keyword it does not implement
 * so a constraint the contract adds cannot silently stop being checked. MduX uses it for the
 * `consumer-manifest` and `evidence` schema documents the pinned corpus ships, so one validator
 * covers `medui-conformance.toml`, the `conformance/contracts` manifest cases and the evidence
 * cases rather than three hand-coded readers drifting apart.
 *
 * Supported keywords: `$schema $id $defs $ref title description type properties additionalProperties
 * required items minItems maxItems uniqueItems minLength pattern minimum maximum enum const anyOf`.
 * `$ref` is local only (`#/...`). `pattern` is `std::regex` in ECMAScript mode, matched anywhere -
 * the contract schemas anchor themselves with `^`/`$` where the whole string matters.
 */
module;

export module mdux.tools.schema;

import std;
import mdux.evidence.json;

export namespace mdux::tools::schema {

/**
 * @brief The keywords `schema` uses that this subset does not implement, plus unresolvable local
 *        `$ref`s and structurally malformed schema objects.
 *
 * Empty means `schema` is one this validator can enforce in full. `validate()` reports the same
 * problems as violations, so a contract that outgrows this subset fails a gate rather than passing
 * a document nothing checked.
 */
[[nodiscard]] std::vector<std::string> checkSchema(const mdux::evidence::json::Value& schema);

/**
 * @brief The rules `value` breaks against `schema`. Empty means valid.
 *
 * If `schema` itself is outside the supported subset (`checkSchema` returns problems), those are
 * returned as violations prefixed `schema: ` and `value` is not examined - fail closed.
 */
[[nodiscard]] std::vector<std::string> validate(const mdux::evidence::json::Value& value,
                                                const mdux::evidence::json::Value& schema);

}  // namespace mdux::tools::schema
