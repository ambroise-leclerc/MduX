/**
 * @file GeneratedScreenHeaderConsumer.cpp
 * @brief Reaches the generated screen through its header form.
 *
 * The other half of #197's acceptance. The header exists for a translation unit that cannot import
 * a named module - a device's entry point may include Vulkan and platform headers before importing
 * anything, precisely because mixing the two orders has produced GCC ICEs in this repository.
 *
 * Note the include order: `import std;` and `import mdux.medui.schema;` first, then the generated
 * header, which assumes both. That is the contract the shader emitter's header form has too.
 */
import std;
import mdux.medui.schema;

#include "GeneratedScreenConsumers.hpp"
#include "screen_every_component.hpp"

namespace mdux::test::generated {

mdux::medui::ScreenPackage screenFromHeader() noexcept {
    return mdux::medui::generated::screen_every_component::package();
}

}  // namespace mdux::test::generated
