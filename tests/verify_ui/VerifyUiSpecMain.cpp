/**
 * @file VerifyUiSpecMain.cpp
 * @brief Entry point for host verification-driver scenarios.
 */
import std;
import speclab;

#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX Verify UI Driver Spec");
}
