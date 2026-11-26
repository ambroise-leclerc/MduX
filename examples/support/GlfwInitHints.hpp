/**
 * @file GlfwInitHints.hpp
 * @brief The GLFW init hints every windowed example sets before `glfwInit()` (#368).
 *
 * @compliance ADR-004 Trust zones in C++ (examples zone: GLFW lives here, never in MduXCore or MduX)
 *
 * One hint today, and it is a workaround for a GLFW 3.4 teardown defect rather than a preference.
 *
 * On Wayland, GLFW 3.4 loads `libdecor-0.so.0` for window decorations and, in
 * `_glfwTerminateWayland()`, unloads it **before** calling `wl_display_disconnect()`. libdecor
 * creates the window's `xdg_surface` / `xdg_toplevel` proxies, and the `wl_interface` tables those
 * proxies point at live in libdecor's own image. Any event for them still sitting undispatched in
 * the display's default queue - read off the socket by another reader, such as the Vulkan driver's
 * WSI while the swapchain is torn down - is released by `wl_display_disconnect()`, which reads the
 * event's `wl_message` from a library that is no longer mapped. That is the intermittent SIGSEGV in
 * `destroy_queued_closure()` under `glfwTerminate()` recorded in #368, at roughly one run in five
 * under CPU load on a GNOME Wayland session with lavapipe.
 *
 * Diagnosed, not guessed: with `LD_PRELOAD=libdecor-0.so.0`, which keeps the library mapped across
 * GLFW's `dlclose()`, the smoke test went from 25 failures in 40 runs to 0 in 40 under the same load.
 * GLFW 3.5 already frees its modules after the disconnect, so the hint is applied only when the
 * GLFW loaded at run time - not the one compiled against, since a system library can differ from
 * the headers - is older than 3.5.
 *
 * With libdecor disabled GLFW still runs on Wayland: it decorates through the `xdg-decoration`
 * protocol where the compositor offers it and through its own fallback decorations otherwise. So
 * this keeps the Wayland path covered, unlike forcing the X11 backend. The hint is ignored on X11,
 * Windows and macOS.
 */
#pragma once

#include <GLFW/glfw3.h>

namespace mdux::examples {

/**
 * @brief Sets the init hints a windowed example needs; call before `glfwInit()`.
 *
 * `glfwGetVersion()` and `glfwInitHint()` are both documented as callable before initialisation.
 */
inline void applyGlfwInitHints() noexcept {
    int major    = 0;
    int minor    = 0;
    int revision = 0;
    glfwGetVersion(&major, &minor, &revision);
    if (major == 3 && minor < 5) {
        glfwInitHint(GLFW_WAYLAND_LIBDECOR, GLFW_WAYLAND_DISABLE_LIBDECOR);
    }
}

}  // namespace mdux::examples
