/**
 * @file GeneratedScreenConsumers.hpp
 * @brief Declares the two ways of reaching the same generated screen.
 *
 * #197's acceptance is that both emitted forms describe one screen, and asserting it needs both in
 * one binary. They cannot share a translation unit: the module form is attached to a named module
 * and the header form is not, so a TU that both imported and included would hold two distinct sets
 * of entities with the same names.
 *
 * So there are two thin translation units, one per form, each returning a `ScreenPackage`, and a
 * third that compares them. Each of these files compiling is itself half the acceptance - the
 * generated module must be importable, and the generated header includable, by an ordinary
 * consumer that links no host-tools module.
 */
#pragma once

namespace mdux::test::generated {

/// The screen as reached through `import mdux.medui.generated.every_component;`.
[[nodiscard]] mdux::medui::ScreenPackage screenFromModule() noexcept;

/// The screen as reached through `#include "every_component.hpp"`.
[[nodiscard]] mdux::medui::ScreenPackage screenFromHeader() noexcept;

}  // namespace mdux::test::generated
