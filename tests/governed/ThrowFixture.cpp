/**
 * @file ThrowFixture.cpp
 * @brief A deliberately non-conforming source, used to prove the governed no-throw scan fails.
 *
 * @compliance ADR-005 Error handling and exceptions policy
 *
 * This file is **not** part of `MduXCore` and is never linked into anything that ships. It exists
 * so that `governed.noThrow.symbolScan.negative` has something to fail on.
 *
 * A check that only ever runs against conforming code proves very little: it passes identically
 * when it is working and when it is scanning an empty list, matching no symbol, or reading the
 * wrong objects. `MduXNoHeapScan.cmake` guards the empty-list and missing-object cases with its own
 * `FATAL_ERROR`s. This covers the case those cannot - the scan running correctly over a real
 * object and simply failing to recognise the thing it is looking for.
 *
 * The `throw` below is what the compiler turns into a `__cxa_throw` reference, the exact symbol
 * the `governed-throw` profile forbids. Narrow that forbidden set or break the symbol matching and
 * this stops failing, which CTest reports as an error because the test carries `WILL_FAIL`.
 *
 * Plain includes rather than `import std`, and no module declaration: the fixture is compiled as
 * an object library outside the module graph, so that a file whose entire purpose is to be
 * non-conforming cannot participate in anyone's module dependency scan.
 */
#include <stdexcept>

namespace mdux::test {

/// Throws unconditionally. Never called; the reference in the emitted object is the whole point.
[[noreturn]] void alwaysThrows() {
    throw std::runtime_error{"this function exists to be found by a symbol scan"};
}

}  // namespace mdux::test
