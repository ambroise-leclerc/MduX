/**
 * @brief Version-related tests for MduX library
 */

import std;
import mdux;
import mdux.test;

#include "framework/MduXTest.hpp"

TEST_CASE("Version Test") {
    CHECK(mdux::Version::major == 0);
    CHECK(mdux::Version::minor == 1);
    CHECK(mdux::Version::patch == 0);
    CHECK(mdux::Version::getString() == "0.1.0");
}
