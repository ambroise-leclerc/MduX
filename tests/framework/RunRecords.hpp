/**
 * @file RunRecords.hpp
 * @brief The v1 glyph-run record encoder, shared by the governed and rendered text suites.
 *
 * @compliance ADR-010 No on-device text shaping
 *
 * One encoder, because the six-byte layout is a format contract rather than a detail: the sidecar
 * is committed bytes compared across toolchains, so a test that encoded it differently from the
 * baker would agree with a decoder that was also wrong. `tests/text` and `tests/render` both need
 * to build records and are separate targets - governed-only and Vulkan-linked - so the encoder
 * lives here rather than in either.
 *
 * Written byte by byte in little-endian rather than memcpy'd from a struct, for the same reason
 * `mdux.text.draw` decodes it that way: a struct copy would inherit the host's padding and byte
 * order, and would then pass on both CI legs while meaning two different things.
 */
#pragma once

namespace mdux::spec {

/// Bytes per v1 record. Restated rather than imported so the test's idea of the layout is
/// independent of the module under test - a decoder that changed `recordSize` should break these
/// suites rather than move with them.
inline constexpr std::size_t runRecordSize = 6;

/// Appends one record: glyph index into the package's table, then x and y in pixels.
inline void appendRunRecord(std::vector<std::byte>& out, std::uint16_t packageIndex, std::int16_t x,
                            std::int16_t y) {
    const auto ux = std::bit_cast<std::uint16_t>(x);
    const auto uy = std::bit_cast<std::uint16_t>(y);
    out.push_back(static_cast<std::byte>(packageIndex & 0xFFu));
    out.push_back(static_cast<std::byte>((packageIndex >> 8) & 0xFFu));
    out.push_back(static_cast<std::byte>(ux & 0xFFu));
    out.push_back(static_cast<std::byte>((ux >> 8) & 0xFFu));
    out.push_back(static_cast<std::byte>(uy & 0xFFu));
    out.push_back(static_cast<std::byte>((uy >> 8) & 0xFFu));
}

/// One record on its own.
[[nodiscard]] inline std::vector<std::byte> runRecord(std::uint16_t packageIndex, std::int16_t x, std::int16_t y) {
    std::vector<std::byte> out;
    appendRunRecord(out, packageIndex, x, y);
    return out;
}

}  // namespace mdux::spec
