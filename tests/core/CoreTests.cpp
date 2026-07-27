/**
 * @file CoreTests.cpp
 * @brief Tests for the governed-zone mdux.core.units and mdux.core.result modules.
 */

import std;
import mdux.core.units;
import mdux.core.result;
import mdux.test;

#include "../framework/MduXTest.hpp"

using namespace mdux::core;

TEST_CASE("Rect basic geometry") {
    Rect r{.x = 10, .y = 20, .width = 100, .height = 50};

    CHECK(r.right() == 110);
    CHECK(r.bottom() == 70);
    CHECK(r.contains(10, 20));       // top-left corner is inside
    CHECK(!r.contains(110, 70));     // bottom-right corner is exclusive
    CHECK(r.contains(109, 69));
    CHECK(!r.contains(0, 0));
}

TEST_CASE("Rect overlap detection") {
    Rect a{.x = 0, .y = 0, .width = 10, .height = 10};
    Rect b{.x = 5, .y = 5, .width = 10, .height = 10};
    Rect c{.x = 20, .y = 20, .width = 10, .height = 10};

    CHECK(a.overlaps(b));
    CHECK(b.overlaps(a));
    CHECK(!a.overlaps(c));
    CHECK(!c.overlaps(a));
}

TEST_CASE("Extent2D equality") {
    Extent2D a{.width = 1920, .height = 1080};
    Extent2D b{.width = 1920, .height = 1080};
    Extent2D c{.width = 800, .height = 600};

    CHECK(a == b);
    CHECK(!(a == c));
}

TEST_CASE("ColorRgba8 defaults to opaque") {
    ColorRgba8 color{.r = 255, .g = 0, .b = 0};
    CHECK(color.a == 255);
}

TEST_CASE("Result success carries a value") {
    Result<int, std::string_view> result = 42;

    REQUIRE(result.has_value());
    CHECK(result.value() == 42);
}

TEST_CASE("Result failure carries an error via err()") {
    Result<int, std::string_view> result = err<std::string_view>("something failed");

    CHECK(!result.has_value());
    REQUIRE(!result.has_value());
    CHECK(result.error() == "something failed");
}

TEST_CASE("ResultVoid distinguishes success from failure") {
    ResultVoid<int> ok = {};
    ResultVoid<int> failed = err(404);

    CHECK(ok.has_value());
    CHECK(!failed.has_value());
    CHECK(failed.error() == 404);
}

MDUX_TEST_MAIN("MduX Core Module Tests")
