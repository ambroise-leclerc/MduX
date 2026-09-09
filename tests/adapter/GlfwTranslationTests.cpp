/**
 * @file GlfwTranslationTests.cpp
 * @brief BDD scenarios for the GLFW → `mdux.medui.input` translation table (#317, ADR-019 clause 3).
 *
 * @compliance ADR-004 Trust zones in C++ (examples zone shell, tested with MduX::Core only)
 * @compliance ADR-010 No on-device text shaping
 * @compliance ADR-019 The windowed presentation and input adapter
 *
 * Covers `glfwKeyToKeyCode`, `glfwButtonAction` and `charToTextEvent` exhaustively. No `glfwInit`,
 * no window, no display — the functions are pure and take plain `int` GLFW constants.
 *
 * The scenario this issue is judged on:
 *
 * - `medui-adapter-key-table-is-exactly-the-contract` — every `KeyCode` ADR-018 defines has one
 *   GLFW key that maps to it, Shift+Tab is `FocusPrev` while Tab is `FocusNext`, and an unmapped
 *   physical key yields nothing (the character comes through the char callback instead).
 */

import std;
import speclab;
import mdux.medui.input;

#include "../framework/SpecLabBridge.hpp"
#include "GlfwEventTranslation.hpp"

namespace {

namespace ms = mdux::medui;
namespace mx = mdux::examples;

const mdux::spec::Register keyTableIsExactlyTheContract{
    "The GLFW key table maps exactly ADR-018's KeyCode members, and nothing else",
    "evidence-unit",
    [] {
        return speclab::Test("medui-adapter-key-table-is-exactly-the-contract")
            .Given("the GLFW physical key constants", [] {})
            .When("each is passed through glfwKeyToKeyCode", [] {})
            .Then("every KeyCode member has one key, Shift+Tab differs from Tab, and an unmapped key is nothing",
                  [] {
                      mdux::spec::Checks checks;

                      const auto maps = [&](int key, int mods, ms::KeyCode expected, std::string_view what) {
                          const auto got = mx::glfwKeyToKeyCode(key, mods);
                          checks.expect(got == expected, std::string{what});
                      };

                      maps(GLFW_KEY_LEFT, 0, ms::KeyCode::CaretLeft, "Left -> CaretLeft");
                      maps(GLFW_KEY_RIGHT, 0, ms::KeyCode::CaretRight, "Right -> CaretRight");
                      maps(GLFW_KEY_HOME, 0, ms::KeyCode::CaretHome, "Home -> CaretHome");
                      maps(GLFW_KEY_END, 0, ms::KeyCode::CaretEnd, "End -> CaretEnd");
                      maps(GLFW_KEY_BACKSPACE, 0, ms::KeyCode::DeleteBack, "Backspace -> DeleteBack");
                      maps(GLFW_KEY_DELETE, 0, ms::KeyCode::DeleteForward, "Delete -> DeleteForward");
                      maps(GLFW_KEY_TAB, 0, ms::KeyCode::FocusNext, "Tab -> FocusNext");
                      maps(GLFW_KEY_TAB, GLFW_MOD_SHIFT, ms::KeyCode::FocusPrev, "Shift+Tab -> FocusPrev");
                      maps(GLFW_KEY_ENTER, 0, ms::KeyCode::Commit, "Enter -> Commit");
                      maps(GLFW_KEY_KP_ENTER, 0, ms::KeyCode::Commit, "keypad Enter -> Commit");
                      maps(GLFW_KEY_ESCAPE, 0, ms::KeyCode::Cancel, "Escape -> Cancel");

                      // Every non-Unspecified KeyCode is reachable through the table above.
                      static constexpr std::array everyKeyCode{
                          ms::KeyCode::CaretLeft,  ms::KeyCode::CaretRight, ms::KeyCode::CaretHome,
                          ms::KeyCode::CaretEnd,   ms::KeyCode::DeleteBack, ms::KeyCode::DeleteForward,
                          ms::KeyCode::FocusNext,  ms::KeyCode::FocusPrev,  ms::KeyCode::Commit,
                          ms::KeyCode::Cancel};
                      static constexpr std::array probeKeys{
                          std::pair{GLFW_KEY_LEFT, 0},       std::pair{GLFW_KEY_RIGHT, 0},
                          std::pair{GLFW_KEY_HOME, 0},       std::pair{GLFW_KEY_END, 0},
                          std::pair{GLFW_KEY_BACKSPACE, 0},  std::pair{GLFW_KEY_DELETE, 0},
                          std::pair{GLFW_KEY_TAB, 0},        std::pair{GLFW_KEY_TAB, GLFW_MOD_SHIFT},
                          std::pair{GLFW_KEY_ENTER, 0},      std::pair{GLFW_KEY_ESCAPE, 0}};
                      for (const ms::KeyCode wanted : everyKeyCode) {
                          const bool reachable = std::ranges::any_of(probeKeys, [&](const auto& probe) {
                              return mx::glfwKeyToKeyCode(probe.first, probe.second) == wanted;
                          });
                          checks.expect(reachable, std::format("KeyCode {} has a GLFW key", ms::toWire(wanted)));
                      }

                      // A letter, a digit, F1, space and an unknown code all yield nothing — the
                      // character (where there is one) arrives through the char callback.
                      for (const int unmapped : {GLFW_KEY_A, GLFW_KEY_5, GLFW_KEY_F1, GLFW_KEY_SPACE,
                                                 GLFW_KEY_LEFT_SHIFT, GLFW_KEY_UNKNOWN, 999999}) {
                          checks.expect(!mx::glfwKeyToKeyCode(unmapped, 0).has_value(),
                                        std::format("key {} maps to nothing", unmapped));
                      }

                      checks.raise();
                  })
            .Execute();
    }};

const mdux::spec::Register buttonAndCharTranslation{
    "A mouse-button action becomes a pointer transition, and a char callback becomes a raw scalar",
    "evidence-unit",
    [] {
        return speclab::Test("medui-adapter-button-and-char-translation")
            .Given("GLFW_PRESS / GLFW_RELEASE and a Unicode code point from the char callback", [] {})
            .When("each is translated", [] {})
            .Then("press is Down, release is Up, and the scalar crosses unchanged (ADR-010)",
                  [] {
                      mdux::spec::Checks checks;

                      checks.expect(mx::glfwButtonAction(GLFW_PRESS) == ms::PointerKind::Down, "PRESS -> Down");
                      checks.expect(mx::glfwButtonAction(GLFW_RELEASE) == ms::PointerKind::Up, "RELEASE -> Up");

                      checks.expect(mx::charToTextEvent(U'7').scalar == U'7', "an ASCII digit crosses unchanged");
                      checks.expect(mx::charToTextEvent(0x1F642).scalar == U'\U0001F642',
                                    "a non-BMP scalar crosses unchanged — no mapping, no composition");
                      checks.raise();
                  })
            .Execute();
    }};

}  // namespace
