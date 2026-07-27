/**
 * @file Digest.cppm
 * @brief Governed-zone SHA-256: the digest every evidence artifact is identified by.
 *
 * @compliance ADR-004 Trust zones in C++ (governed zone: std only, no Vulkan, no windowing)
 * @compliance ADR-005 Error handling and exceptions policy (noexcept throughout, no throwing)
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * Part of MduXCore. Every `report.json` digest, every `package.json` sidecar reference, and
 * the weight-blob verification in the planned `Classifier1D::create()` (issue #18) resolve to
 * this implementation.
 *
 * Hand-written rather than taken as a dependency, which is the zero-SOUP position. SHA-256 is
 * one of the few algorithms where that is uncontroversial: a fixed specification, ~200 lines,
 * and an official test-vector suite to check against. There is no error path - a hash of any
 * byte sequence is defined - so nothing here returns a Result.
 *
 * No allocation anywhere: the streaming state is a fixed 64-byte block buffer plus eight words.
 */
module;

export module mdux.evidence.digest;

import std;

export namespace mdux::evidence {

/// A SHA-256 digest: 32 bytes, most significant byte first.
using Digest = std::array<std::uint8_t, 32>;

/**
 * @brief Streaming SHA-256, for inputs too large to hold in one span.
 *
 * Feed with any number of update() calls, then finish(). The digest of the concatenation of
 * every update() is identical to sha256() over the same bytes contiguously - the streaming
 * tests assert exactly that.
 *
 * finish() does not consume the object: it finalizes a copy of the internal state, so calling
 * it twice yields the same digest, and update() may continue afterwards to extend the message.
 * That costs one 96-byte copy and removes a whole class of misuse from a governed API.
 */
class Sha256 {
public:
    Sha256() noexcept = default;

    /// Absorbs `data` into the running hash.
    void update(std::span<const std::byte> data) noexcept;

    /// Returns the digest of everything absorbed so far. Non-destructive; see the class note.
    [[nodiscard]] Digest finish() const noexcept;

    /// Discards all absorbed input, returning the object to its freshly-constructed state.
    void reset() noexcept;

private:
    /// FIPS 180-4 initial hash value H(0): the first 32 bits of the fractional parts of the
    /// square roots of the first eight primes.
    std::array<std::uint32_t, 8> state_{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    std::array<std::byte, 64> block_{};
    std::size_t blockLen_{0};   ///< bytes currently buffered in block_, always < 64
    std::uint64_t totalBytes_{0};
};

/// One-shot SHA-256 over a contiguous byte range.
[[nodiscard]] Digest sha256(std::span<const std::byte> data) noexcept;

/// Lowercase hex encoding of `digest`. Exactly 64 characters, not NUL-terminated.
[[nodiscard]] std::array<char, 64> toHex(const Digest& digest) noexcept;

}  // namespace mdux::evidence
