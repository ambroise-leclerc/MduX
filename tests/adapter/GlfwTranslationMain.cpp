/**
 * @file GlfwTranslationMain.cpp
 * @brief Entry point for the GLFW → `mdux.medui.input` translation spec (#317).
 *
 * Its own binary, linking `glfw` for the key constants only — no `glfwInit`, no window, no display.
 * `examples/support/GlfwEventTranslation.hpp` is the pure half of the ADR-019 adapter and this is
 * its unit coverage.
 */

import std;
import speclab;

#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX GLFW Input Translation Spec");
}
