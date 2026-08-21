/**
 * @file GeneratedScreenModuleConsumer.cpp
 * @brief Reaches the generated screen through its module interface.
 *
 * This translation unit existing and compiling *is* half of #197's acceptance: the generated module
 * form must be importable by an ordinary consumer, and the `static_assert` the generated source
 * carries is evaluated here rather than on a device.
 */
import std;
import mdux.medui.schema;
import mdux.medui.generated.screen_every_component;

#include "GeneratedScreenConsumers.hpp"

namespace mdux::test::generated {

mdux::medui::ScreenPackage screenFromModule() noexcept {
    return mdux::medui::generated::screen_every_component::package();
}

}  // namespace mdux::test::generated
