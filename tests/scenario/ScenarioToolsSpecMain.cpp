/**
 * @file ScenarioToolsSpecMain.cpp
 * @brief Entry point for the scenario host-tools SpecLab suite (#319, ADR-020).
 */
import std;
import speclab;

#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX Scenario Tools Spec");
}
