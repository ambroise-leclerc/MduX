/**
 * @brief Version-related tests for MduX library
 */

import std;
import mdux;

bool testVersion() {
    // Test version information
    bool versionValid =
        mdux::Version::major == 0 && mdux::Version::minor == 1 && mdux::Version::patch == 0;

    // Test version string
    bool versionStringValid = mdux::Version::getString() == "0.1.0";

    return versionValid && versionStringValid;
}