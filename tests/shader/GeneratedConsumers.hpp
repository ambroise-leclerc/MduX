/**
 * @file GeneratedConsumers.hpp
 * @brief Declares the two ways of reaching the same generated shader package.
 *
 * The point of #121's two outputs is that they describe identical bytes and an identical contract.
 * Asserting that needs both in one binary, and they cannot be in one translation unit: the module
 * form is attached to a named module and the header form is not, so a TU that both imported and
 * included would hold two distinct sets of entities with the same names.
 *
 * So there are two thin translation units, one per form, each returning a `PackageView`, and a
 * third that compares them. If the emitter ever renders them differently, the comparison fails
 * here rather than in whichever of #122 or #124 happened to use the other one.
 */
#pragma once

namespace mdux::test::generated {

/// The package as reached through `import mdux.shader.generated.mdux_ui;`.
[[nodiscard]] mdux::shader::PackageView fromModule() noexcept;

/// The package as reached through `#include "mdux_ui.hpp"`.
[[nodiscard]] mdux::shader::PackageView fromHeader() noexcept;

}  // namespace mdux::test::generated
