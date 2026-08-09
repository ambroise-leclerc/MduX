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

## Additional Notes

- Write clear, descriptive commit messages.
- Add or update documentation as needed.
- Run all tests locally before submitting your PR.
