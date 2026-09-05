/**
 * @file ImageToolsSpecMain.cpp
 * @brief Host-only QOI baker test entry point.
 */
import std;
import speclab;
#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX Image Tools Spec");
}
