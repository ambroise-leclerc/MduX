/**
 * @file MeduiSpecMain.cpp
 * @brief Entry point for the governed `mdux.medui` SpecLab BDD executable.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone)
 * @compliance ADR-012 What a compiled screen emits, and which parts are committed
 *
 * Deliberately a separate binary from `medui_tools_spec`, which links the host-tools compiler.
 * This one links MduX::Core only, so it is standing evidence that the compiled-screen schema - the
 * one type a device holds - reaches nothing but `std`. A scenario here that needed the parser or
 * the layout solver would mean the boundary had moved.
 */

import std;
import speclab;

#include "../framework/SpecLabBridge.hpp"

int main(int argc, char** argv) {
    return mdux::spec::main(argc, argv, "MduX MedUI Schema Spec");
}
