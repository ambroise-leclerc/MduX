/**
 * @file GeneratedHeaderConsumer.cpp
 * @brief Reaches the generated shader package through its header form.
 *
 * The other half of #121's acceptance. The header exists for a translation unit that cannot
 * import a named module - examples/VulkanSCTriangleExample.cpp includes Vulkan and GLFW headers
 * before importing anything, precisely because mixing the two orders has produced GCC ICEs in
 * this repository before.
 *
 * Note the include order: `import std;` and `import mdux.shader.schema;` first, then the
 * generated header, which assumes both. That is the same contract MduXTest.hpp has.
 */
import std;
import mdux.shader.schema;

#include "mdux_ui.hpp"

#include "GeneratedConsumers.hpp"

namespace mdux::test::generated {

mdux::shader::PackageView fromHeader() noexcept {
    return mdux::shader::generated::mdux_ui::package();
}

}  // namespace mdux::test::generated
