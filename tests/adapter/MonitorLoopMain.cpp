/**
 * @file MonitorLoopMain.cpp
 * @brief Entry point for the assembled monitor update spec - `examples/support/MonitorApp.hpp` (#318).
 *
 * Links `MduX::Core` and the committed `endoscope-monitor` screen module only: `updateMonitor()`
 * is `std` and `mdux.medui.*`, so its coverage needs no window, no Vulkan and no display.
 */

import std;
import speclab;

#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX Monitor Loop Spec");
}
