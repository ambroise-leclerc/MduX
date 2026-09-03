/**
 * @file VerifySpecMain.cpp
 * @brief Entry point for the governed `mdux.verify` SpecLab BDD executable.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone)
 * @compliance ADR-014 What rendered-truth verification checks, and what it cannot
 *
 * Links MduX::Core only, and no GPU is involved anywhere in this binary. That is ADR-014 decision
 * 1's first consequence made mechanical: a framebuffer is an array, so a check can be built and
 * proved before a driver exists to produce one. A scenario here that needed Vulkan would mean the
 * checks had stopped being pure functions over pixels.
 */

import std;
import speclab;

#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX Rendered-Truth Verification Spec");
}
