/**
 * @file GeneratedEmptyScreenConsumer.cpp
 * @brief Reaches the generated form of a screen that resolves to no nodes.
 *
 * The degenerate case, and it has to compile: `ScreenPackage::validate()` permits a screen with no
 * nodes and an empty budget, and `medui-schema-budget` pins that. An earlier revision of the emitter
 * rendered it as `CompiledNode nodes[] = {}`, which is not valid C++ - so a screen the schema calls
 * valid could not be consumed in either emitted form. This translation unit compiling is what says
 * that is no longer true.
 *
 * Only the module form is reached here. The two forms share one rendered body, which
 * `medui-screen-emit-two-forms` asserts, so compiling one of them compiles the shape.
 */
import std;
import mdux.medui.schema;
import mdux.medui.generated.screen_empty_screen;

#include "GeneratedScreenConsumers.hpp"

namespace mdux::test::generated {

mdux::medui::ScreenPackage emptyScreenFromModule() noexcept {
    return mdux::medui::generated::screen_empty_screen::package();
}

}  // namespace mdux::test::generated
