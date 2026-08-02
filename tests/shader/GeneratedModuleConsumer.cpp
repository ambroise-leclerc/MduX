/**
 * @brief Reaches the generated shader package through its module interface.
 *
 * This translation unit existing and compiling *is* half of #121's acceptance: the generated
 * module form must be importable by an ordinary consumer.
 */
import std;
import mdux.shader.schema;
import mdux.shader.generated.mdux_ui;

#include "GeneratedConsumers.hpp"

namespace mdux::test::generated {

mdux::shader::PackageView fromModule() noexcept {
    return mdux::shader::generated::mdux_ui::package();
}

}  // namespace mdux::test::generated
