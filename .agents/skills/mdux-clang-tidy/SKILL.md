---
name: mdux-clang-tidy
description: Analyze MduX C++ changes with the repository clang-tidy configuration. Use when static analysis, clang-tidy findings, or warning remediation is requested; do not use as a substitute for the governed verification tests.
---

# MduX clang-tidy analysis

Read the root `.clang-tidy`, `CONTRIBUTING.md`, and the affected module before running analysis.
Use an existing out-of-source `compile_commands.json` whose compiler, preset, and source revision
match the files being checked. Do not invent flags or run automatic fixes across unrelated files.

Report the command, compilation database, analyzed files, and findings grouped by check name. Apply
only requested fixes, then rerun the focused analysis and the tests required by `AGENTS.md`.

Clang-tidy is advisory for C++ module interfaces in this repository: the current CI analysis uses
a GCC-generated compilation database that clang-tidy cannot fully parse and does not enforce its
result. Never present a clean clang-tidy run as proof of governed no-throw, safety, regulatory, or
release conformance; use the repository verification tests for those claims.
