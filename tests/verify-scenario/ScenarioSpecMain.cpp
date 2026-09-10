/**
 * @file ScenarioSpecMain.cpp
 * @brief Entry point for the dynamic scenario-capture verifier spec (#321).
 */
import std;
import speclab;

#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX Verify Scenario Driver Spec");
}
