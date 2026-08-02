/**
 * @brief Entry point for the offscreen rendering suite, which needs a real Vulkan device.
 *
 * @compliance ADR-004 Trust zones in C++ (adapter zone)
 *
 * Exits 77 when no device is available, which CTest is configured to report as *Skipped*. That is
 * deliberately a different outcome from passing: a contributor without Vulkan installed should not
 * see a red test they cannot act on, and equally should not see a green one that ran nothing.
 *
 * CI does have a device - the Linux legs install mesa-vulkan-drivers and select lavapipe - so on
 * the machines whose result gates a merge, these tests run for real.
 */
#include <cstdint>

#include <vulkan/vulkan.h>

import std;
import mdux.test;

#include "../framework/MduXTest.hpp"
#include "HeadlessDevice.hpp"

int main(int argc, char** argv) {
    const mdux::test::HeadlessDevice& gpu = mdux::test::sharedDevice();
    if (!gpu.available()) {
        std::println(std::cout, "MduX Offscreen Tests: skipped - {}", gpu.reason());
        return mdux::test::skipExitCode;
    }
    // Printed unconditionally, and on one line CI can grep for. Whether the validation layers are
    // loaded decides whether the layout and synchronisation assertions in this suite check
    // anything at all - and a passing ctest run prints nothing, so without this the difference
    // between "the layers approved" and "there were no layers" is invisible in a green log.
    std::println(std::cout, "MduX Offscreen Tests: validation layers {}",
                 gpu.validationEnabled() ? "enabled" : "UNAVAILABLE");
    return mdux::test::runMain(argc, argv, "MduX Offscreen Tests");
}
