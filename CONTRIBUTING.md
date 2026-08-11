# Contributing Guidelines

## Invitation-only contributions

This project accepts code and documentation contributions only from collaborators explicitly
invited by a maintainer. Opening an issue or pull request does not constitute an invitation, and
unsolicited pull requests may be closed without review.

Before contributing, an invited collaborator must accept the [Contributor Licence
Agreement](CLA.md) in the GitHub issue designated by the maintainer. Only collaborators with
repository access may submit changes for review. Every change remains subject to maintainer review;
an invitation does not guarantee merge.

## Coding Style

We follow the **C++ Core Guidelines** to ensure conformance with modern C++23 generic programming. Please refer to the [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) for detailed rules. Key points are summarized below:

### Naming Conventions

- **Classes/Structs:**
  - Use `UpperCamelCase` (e.g., `MyClass`, `DataProcessor`).
- **Functions/Methods:**
  - Use `lowerCamelCase` (e.g., `processData()`, `getValue()`).
- **Variables (including const, constexpr, and constinit variables):**
  - Use `lowerCamelCase` (e.g., `dataBuffer`, `isReady`, `maxSweepWork`).
  - No Hungarian notation, and **no `k` prefix on constants**: `kMaxSweepWork` is wrong,
    `maxSweepWork` is right. A constant is a variable and follows the same rule as one.
- **Namespaces:**
  - Use 'lowercase' (e.g., 'mui', 'backend').
- **Macros:**
  - Use `ALL_CAPS_WITH_UNDERSCORES`. But do not use macros.

### Formatting

- Indent with 4 spaces, no tabs.
- Place pointer/reference symbols next to the type (e.g., `int* ptr`, `const std::string& name`).
- Use `nullptr` instead of `NULL`.

### File Organization

- Header files: `.hpp`
- Source files: `.cpp`
- **File naming:** Use `UpperCamelCase` for `.hpp` and `.cpp` files (e.g., `Logger.hpp`, `DataProcessor.cpp`).

### Documentation

We use **Doxygen** syntax for code documentation. Follow these guidelines:

- **File-level documentation:** open the block with `@file`, naming the file **with its
  extension and no path**, then `@brief`. Still no `@author` tags.

  ```cpp
  /**
   * @file VulkanRenderer.cppm
   * @brief One sentence on what this file is.
   */
  ```

  The extension matters: `Draw.cppm` and `Draw.cpp` are different files with the same stem, and
  this tree has several such pairs.

  `@file` is what makes Doxygen attach the block to the *file*. Without it the block is not
  ignored — it silently becomes the documentation of whatever comes next. Measured on
  `tests/framework/RunRecords.hpp` with Doxygen 1.15: with the tag, the file page carries "The v1
  glyph-run record encoder, shared by…"; without it, the file page's brief is empty and that
  sentence turns up as the documentation for `namespace mdux::spec`. Wrong documentation, with no
  warning, rather than missing documentation.

  This rule previously said the opposite. It was never what the tree did — 82 files used `@file`
  against 56 that did not — and review bots cite this document, so the contradiction kept
  resurfacing on unrelated pull requests. #180 settled it in favour of the half that works.
- **Class documentation:** Include `@brief` with detailed description and usage examples.
- **Method documentation:**
  - Use compact notation `/** @brief Description */` for simple one-line descriptions.
  - Use full format with `@param`, `@return`, `@note` for complex methods.
  - Include usage examples with `@code` blocks when helpful.
- **`///` line comments carry no `@brief`.** The tag belongs to `/** */` blocks. A `///` comment
  *is* the brief — Doxygen already treats it as one, and prefixing it adds a word that says only
  "this is a comment". The tree is consistent on this: 485 `///` comments, none with `@brief`.
  Reviewers and review bots ask for it regularly; the answer is no, and this line is here so the
  question has somewhere to be answered once.
- **Template parameters:** Document with `@tparam` when non-obvious.
- **Private members:** Generally no documentation needed unless complex.

Example formats:
```cpp
/** @brief Simple one-line description */
void simpleFunction();

/**
 * @brief Complex function with detailed documentation
 *
 * Detailed description of what the function does, including
 * important implementation details and usage patterns.
 *
 * @param param1 Description of first parameter
 * @param param2 Description of second parameter
 * @return Description of return value
 *
 * @note Important notes about usage or behavior
 *
 * @code
 * // Usage example
 * auto result = complexFunction(value1, value2);
 * @endcode
 */
ReturnType complexFunction(Type1 param1, Type2 param2);
```

## Tooling

To ensure consistency, we use `.clang-format` and `.clang-tidy` to enforce our coding style. These configuration files are included in the root of the repository.

- **Clang-Format:** Automatically formats your code to match our style guidelines.
- **Clang-Tidy:** Detects and warns about style violations, bugs, and non-modern C++ practices.

Please run these tools on your code before submitting a pull request.

## Pull Requests

- **One commit per pull request.**
- The pull request (PR) title must reference the related issue or feature (e.g., `Add CameraManager class [#42]`).
- Provide a clear description of the changes and the motivation.
- Ensure your branch is up to date with `develop` before submitting.
- All code must pass CI checks and tests before merging.
- Request a review from at least one maintainer.
- Fill in [`.github/pull_request_template.md`](.github/pull_request_template.md) completely. Its
  fields are the policy below, in the place where it is cheapest to apply.

## Stacked delivery

An epic is delivered as a chain of pull requests, each on its own issue branch, each targeting its
predecessor rather than `develop` so a reviewer sees one issue's diff instead of the cumulative
one. The branch-naming rule that keeps CI attached to such a chain is in
[`AGENTS.md`](AGENTS.md) § 6; the rules below govern the *order* things merge in.

They exist because of a specific failure. During Wave 2, stacked pull requests lost CMake, module
and test wiring while their conflicts were being resolved, and two incompatible governance models
coexisted on separate branches until integration discovered them — which took a repair PR (#104)
and a reconciliation (#105) to undo. Every rule here is one of the things that would have caught
it earlier.

### Merge order

- **A successor cannot merge before its predecessor.** Not "should not" — a stacked PR whose base
  has not merged is proposing a diff against a branch that may still change under it.
- After the predecessor merges, **rebase the successor onto current `develop`** and re-request
  review of its final diff against `develop`. The diff a reviewer approved against the predecessor
  is not the diff that will land.
- **Required PR CI must be green, and then the resulting `develop` workflow must be green,**
  before the next dependent PR merges. A green PR check proves the branch builds; only the
  post-merge `develop` run proves the *integration* does. **Record that run** — its URL or id — in
  the successor's "Post-merge gate" section, so the gate leaves evidence rather than only an
  intention. A checkbox saying the author understood the rule is not a record that it was followed.

### Conflicts

- **Resolve a conflict in a shared registry as an explicit union, not a choice.** The registries
  that matter are listed in the PR template: the root `CMakeLists.txt`, `FILE_SET CXX_MODULES`
  lists, `tools/CMakeLists.txt`, `tests/CMakeLists.txt`, schemas, generated indexes and
  `generated/` artifacts. Taking one side of a conflict in a source list silently deletes the other
  side's target, which still compiles and still passes every test that was not the deleted one.
- If a conflict genuinely has to be resolved by taking one side, say so in the PR description and
  say why.

### Ordering within a chain

- **Canonical shared types and schemas land before their exports and consumers.** A successor
  imports the type its predecessor defined; it does not restate it. Two branches each defining
  their own version of "the same" type is how Wave 2 ended up with two governance models, and
  neither branch's CI could see the problem.

### Emergency merges

A merge that bypasses the gates above — a local merge, an API push, an administrative override —
requires a comment on the issue or PR naming **the exact head SHA that was incorporated** and
**the post-merge CI run that covered it**. An undocumented bypass leaves no way to tell later
which code was actually reviewed.

## Additional Notes

- Write clear, descriptive commit messages.
- Add or update documentation as needed.
- Run all tests locally before submitting your PR.
