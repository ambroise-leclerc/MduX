/**
 * @file GlfwEventTranslation.hpp
 * @brief Native GLFW input → the bounded `mdux.medui.input` vocabulary (#317, ADR-019 clause 3).
 *
 * @compliance ADR-004 Trust zones in C++ (examples zone)
 * @compliance ADR-010 No on-device text shaping (a character crosses as the scalar the platform gave)
 * @compliance ADR-019 The windowed presentation and input adapter
 *
 * The pure half of the adapter: three free functions and the fixed `KeyCode` table, with no Vulkan
 * and no window state, so a unit test (`glfw_translation_spec`) covers them without a display.
 * `GlfwPresentationAdapter.hpp` includes this and adds the window and swapchain around it.
 *
 * Include **after** `import mdux.medui.input;` — it names those types without importing them.
 */
#pragma once

#include <GLFW/glfw3.h>

namespace mdux::examples {

/// The GLFW physical key → `mdux::medui::KeyCode` table. Exactly ADR-018's members and no more; a
/// character never arrives here (the GLFW char callback carries it as a `TextEvent`). `mods` picks
/// `FocusPrev` for Shift+Tab. Returns `std::nullopt` for every key this contract does not name —
/// which is additive: a later adapter extends `KeyCode` and this table rather than reinterpreting a
/// member.
[[nodiscard]] inline std::optional<mdux::medui::KeyCode> glfwKeyToKeyCode(int key, int mods) noexcept {
    switch (key) {
        case GLFW_KEY_LEFT:      return mdux::medui::KeyCode::CaretLeft;
        case GLFW_KEY_RIGHT:     return mdux::medui::KeyCode::CaretRight;
        case GLFW_KEY_HOME:      return mdux::medui::KeyCode::CaretHome;
        case GLFW_KEY_END:       return mdux::medui::KeyCode::CaretEnd;
        case GLFW_KEY_BACKSPACE: return mdux::medui::KeyCode::DeleteBack;
        case GLFW_KEY_DELETE:    return mdux::medui::KeyCode::DeleteForward;
        case GLFW_KEY_TAB:
            return (mods & GLFW_MOD_SHIFT) ? mdux::medui::KeyCode::FocusPrev
                                           : mdux::medui::KeyCode::FocusNext;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER:  return mdux::medui::KeyCode::Commit;
        case GLFW_KEY_ESCAPE:    return mdux::medui::KeyCode::Cancel;
        default:                 return std::nullopt;
    }
}

/// A GLFW mouse-button action (`GLFW_PRESS` / `GLFW_RELEASE`) → the pointer transition it is.
[[nodiscard]] inline mdux::medui::PointerKind glfwButtonAction(int action) noexcept {
    return action == GLFW_PRESS ? mdux::medui::PointerKind::Down : mdux::medui::PointerKind::Up;
}

/// One committed Unicode scalar from the GLFW char callback, unchanged (ADR-010: no mapping, no
/// composition, no shaping — the platform's keymap/IME already resolved it).
[[nodiscard]] inline mdux::medui::TextEvent charToTextEvent(unsigned int codepoint) noexcept {
    return mdux::medui::TextEvent{.scalar = static_cast<char32_t>(codepoint)};
}

}  // namespace mdux::examples
