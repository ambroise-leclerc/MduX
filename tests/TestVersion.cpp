/**
 * @file TestVersion.cpp
 * @brief Version-related tests for MduX library
 */

import std;
import mdux;
import mdux.test;

#include "framework/MduXTest.hpp"

TEST_CASE("Version Test") {
    CHECK(mdux::Version::major == 0);
    CHECK(mdux::Version::minor == 5);
    CHECK(mdux::Version::patch == 0);
    CHECK(mdux::Version::getString() == "0.5.0");
}

TEST_CASE("Version Test Sections") {
    // Exercises SECTION-lite: unlike Catch2, both siblings run in the same pass
    // rather than re-entering the TEST_CASE once per section, so a shared local
    // is expected to see both increments by the time the case ends.
    int sectionsRun = 0;

    SECTION("major and minor") {
        sectionsRun += 1;
        CHECK(mdux::Version::major == 0);
        CHECK(mdux::Version::minor == 5);
    }

    SECTION("patch and string") {
        sectionsRun += 1;
        CHECK(mdux::Version::patch == 0);
        CHECK(mdux::Version::getString() == "0.5.0");
    }

    CHECK(sectionsRun == 2);
}
