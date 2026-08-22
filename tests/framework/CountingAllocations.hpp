/**
 * @file CountingAllocations.hpp
 * @brief Replaces the global `operator new` family with counting versions, for the no-heap suites.
 *
 * Included by exactly one translation unit per binary, because replacing a global operator is a
 * whole-program property: two definitions do not link, and a suite that shared a binary with an
 * allocating one would measure the wrong program.
 *
 * Extracted from `tests/ml/NoHeapTests.cpp` when the screen runtime needed the same measurement
 * (issue #199). It is subtle enough - over-aligned allocation by hand, a scoped GCC diagnostic for a
 * mismatched-new-delete the compiler cannot see through - that a second copy would have been a
 * second thing to keep right.
 *
 * What it catches and what it misses is the argument for the other two layers, and belongs with the
 * suites that use it: this sees any allocation through the global `operator new` family, including
 * one hidden inside a `std` call nobody expected to allocate. It does not see a direct
 * `std::malloc`, a pool allocator, or anything else that bypasses those operators - the symbol scan
 * covers those.
 *
 * A suite using this must include at least one scenario that allocates deliberately and asserts the
 * counter moves. Without it the file would be a test that cannot fail: if the interposition ever
 * stopped taking effect, every scenario would pass on a counter that never moved.
 */
#pragma once

namespace {

/// Bumped by every allocating operator below. Not atomic-by-necessity - the scenarios are
/// single-threaded - but atomic anyway, so that a future threaded case cannot make this quietly
/// unreliable.
std::atomic<std::size_t> allocationCount{0};

[[nodiscard]] std::size_t allocations() noexcept {
    return allocationCount.load(std::memory_order_relaxed);
}

/**
 * @brief Over-aligned allocation built on plain malloc, with no platform branch.
 *
 * Neither standard route works here. `std::aligned_alloc` does not exist on MSVC at all, and
 * `_aligned_malloc` is declared in `<malloc.h>` - a header this translation unit cannot include,
 * because it reaches the standard library through `import std;` and mixing the two is what the
 * C5050 diagnostics elsewhere in this epic were about.
 *
 * So the alignment is done by hand: over-allocate, step the pointer up to the boundary, and stash
 * the original just below it for the matching free. Portable, needs nothing but `std::malloc`, and
 * keeps the counter covering over-aligned allocations - which matters, because leaving the aligned
 * operators unreplaced would leave a path through which predict() could allocate uncounted.
 */
[[nodiscard]] void* allocateAligned(std::size_t size, std::size_t alignment) {
    // The stash slot has to be addressable, so never align more loosely than a pointer.
    const std::size_t effective = alignment < alignof(void*) ? alignof(void*) : alignment;
    const std::size_t slack     = effective - 1 + sizeof(void*);

    void* raw = std::malloc(size + slack);
    if (raw == nullptr) {
        return nullptr;
    }

    auto address  = reinterpret_cast<std::uintptr_t>(raw) + sizeof(void*);
    address       = (address + effective - 1) & ~static_cast<std::uintptr_t>(effective - 1);
    auto* aligned = reinterpret_cast<void*>(address);

    // `aligned` is a multiple of effective >= alignof(void*), so the slot below it is aligned too.
    *(reinterpret_cast<void**>(aligned) - 1) = raw;
    return aligned;
}

void freeAligned(void* pointer) noexcept {
    if (pointer == nullptr) {
        return;
    }
    std::free(*(reinterpret_cast<void**>(pointer) - 1));
}

}  // namespace

// Every allocating form is replaced, not just the common one. A partial replacement would leave a
// path through which predict() could allocate uncounted, which is the failure this file exists to
// make impossible.

void* operator new(std::size_t size) {
    allocationCount.fetch_add(1, std::memory_order_relaxed);
    void* pointer = std::malloc(size == 0 ? 1 : size);
    if (pointer == nullptr) {
        throw std::bad_alloc{};
    }
    return pointer;
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    allocationCount.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(size == 0 ? 1 : size);
}

void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept {
    return ::operator new(size, tag);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    allocationCount.fetch_add(1, std::memory_order_relaxed);
    void* pointer = allocateAligned(size == 0 ? 1 : size, static_cast<std::size_t>(alignment));
    if (pointer == nullptr) {
        throw std::bad_alloc{};
    }
    return pointer;
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}

// GCC's -Wmismatched-new-delete sees std::free() applied to a pointer that came from
// `operator new` and reports a mismatch. It is right about the shape and wrong about the facts:
// the operator new above *is* std::malloc, so free is the correct counterpart. The warning cannot
// see that pairing because these are separate replaceable functions. Scoped to exactly the delete
// family below, so the diagnostic keeps working everywhere else in this file.
#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

void operator delete(void* pointer) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer, const std::nothrow_t&) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer, std::align_val_t) noexcept {
    freeAligned(pointer);
}

void operator delete[](void* pointer, std::align_val_t) noexcept {
    freeAligned(pointer);
}

void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept {
    freeAligned(pointer);
}

void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept {
    freeAligned(pointer);
}

#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic pop
#endif
