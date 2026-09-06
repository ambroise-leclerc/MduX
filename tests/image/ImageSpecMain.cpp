/**
 * @file ImageSpecMain.cpp
 * @brief Governed baked-image schema test entry point.
 */
import std;
import speclab;
#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX Image Spec");
}
