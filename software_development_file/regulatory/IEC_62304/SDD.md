# Software Design Description (SDD) — MduX

> Filled-in example for MduX itself. See `software_development_file/templates/IEC_62304/SDD.md`
> for the blank template.

## Document control

- **Software item(s) covered:** `MduXCore`'s governed modules, listed in the SAD's decomposition
  (see `SAD.md` §2)
- **Version:** see `CMakeLists.txt`'s `project(... VERSION ...)`

## 1. Purpose and scope

This SDD details the internal design of `MduXCore`'s governed modules, one level below the
architectural interfaces described in `SAD.md`.

## 2. Detailed design per software unit

> `IEC 62304:2006 §5.4.1 Refine the software architecture into a detailed design`

### Unit: `mdux.core.result`
- **Responsibility:** `Result<T, E>` (a `std::expected<T, E>` alias) and `ResultVoid<E>`, the
  return type every governed module uses instead of throwing (`docs/adr/ADR-005-error-handling-and-exceptions-policy.md`).

### Unit: `mdux.evidence.digest` / `mdux.evidence.json` / `mdux.evidence.report`
- **Responsibility:** hand-written SHA-256 (`Digest`, `Sha256`, `sha256()`), canonical
  MduX JSON (`json::Value`, sorted keys, floats as bit patterns, strict reader), and the shared
  `BakeReport`/`PackageHeader` shape every future baker will emit (`docs/adr/ADR-007-evidence-pipeline-doctrine.md`).
  No baker exists yet (issues #13-18 are still open) — these three modules are the shared kernel a
  baker will build on, not evidence of one already running.

### Unit: `mdux.governance`
- **Responsibility:** `Justification` (mirrors `docs/governance/schemas/justification.schema.json`
  field-for-field: `validate()`/`toJson()`/`write()`/`parse()`).

### Unit: `mdux.governance.compliance`
- **Responsibility:** `traceabilityMatrix()` and `releaseEvidenceSummary()` — pure functions over
  the `ComplianceProgram` that `mdux.governance` defines; no I/O in the module itself, and no
  types of its own. It declared its own `Requirement`/`VerificationCase`/`Hazard`/`ProblemReport`
  until those collided with `mdux.governance`'s; see that module's header for why the duplicates
  were removed rather than kept alongside. **State the gap plainly:** no `ComplianceProgram`
  has actually been populated for MduX's own release as of this document — only the types and
  their unit tests (in `evidence_tests`, synthetic fixture IDs like `REQ-001`/`HAZ-001`,
  not real MduX requirements) exist. A real, populated program for MduX itself is future work, not
  something this document should imply already exists.

## 3. Interface detailed design

> `IEC 62304:2006 §5.4.2 Develop a detailed design for interfaces`

Each unit's exported interface is its `.cppm` file — `include/mdux/core/Result.cppm`,
`include/mdux/evidence/{Digest,Json,Report}.cppm`, `include/mdux/governance/{Justification,Compliance}.cppm`
— cited directly rather than re-transcribed here, since the module *is* the interface
specification in a way a separate document describing it cannot stay in sync with as easily.

## 4. Detailed design verification

> `IEC 62304:2006 §5.4.3 Verify the detailed design`

Each governed module's tests run in `core_tests`/`evidence_tests` (`tests/CMakeLists.txt`), built
and executed by `.github/workflows/windows-build.yml` and `.github/workflows/linux-gcc16-build.yml`
on every pull request and on every push to `main` or `develop`. `mdux-evidence-lint`
(`tools/evidence-lint/mdux_evidence_lint.py`) additionally bans float-format specifiers under
`src/evidence/` and `tools/`, a static check specific to the canonical-JSON byte-identity property
`mdux.evidence.json` depends on.

## Justification records

```jsonc
{
  "justification_id": "JUS-016",
  "standard": "IEC 62304:2006",
  "clause_ref": "IEC 62304:2006 §5.4.1 Refine the software architecture into a detailed design",
  "rationale": "mdux.governance.compliance's traceabilityMatrix() does not gate on ComplianceProgram::validate() and does not omit a Requirement with no VerificationCase - it emits that row with an empty verification_cases array, so a coverage gap is a visible row rather than a silently missing one, which the ctest exercising this behavior asserts directly.",
  "evidence_refs": [
    "include/mdux/governance/Compliance.cppm",
    "src/governance/Compliance.cpp",
    "tests/governance/ComplianceTests.cpp"
  ]
}
```
