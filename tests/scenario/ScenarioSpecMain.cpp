/**
 * @file ScenarioSpecMain.cpp
 * @brief Entry point for the governed scenario spec (#319/#320). Links MduX::Core only.
 */
import std;
import speclab;

#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX Scenario Spec");
}
