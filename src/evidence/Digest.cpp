/**
 * @file Digest.cpp
 * @brief FIPS 180-4 SHA-256 implementation for the governed evidence zone.
 *
 * @compliance ADR-004 Trust zones in C++
 * @compliance ADR-005 Error handling and exceptions policy
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * Straight transcription of the FIPS 180-4 compression function - no SIMD, no table
 * generation at runtime, no allocation, no branching on message content. Deliberately
 * unclever: this is code a manufacturer may have to read, and its test vectors are
 * published, so clarity is worth more here than throughput.
 */
module;

module mdux.evidence.digest;

import std;

namespace mdux::evidence {

namespace {

/// FIPS 180-4 K constants: the first 32 bits of the fractional parts of the cube roots of
/// the first sixty-four primes.
constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

[[nodiscard]] constexpr std::uint32_t ch(std::uint32_t x, std::uint32_t y,
                                          std::uint32_t z) noexcept {
    return (x & y) ^ (~x & z);
}

[[nodiscard]] constexpr std::uint32_t maj(std::uint32_t x, std::uint32_t y,
                                           std::uint32_t z) noexcept {
    return (x & y) ^ (x & z) ^ (y & z);
}

[[nodiscard]] constexpr std::uint32_t bigSigma0(std::uint32_t x) noexcept {
    return std::rotr(x, 2) ^ std::rotr(x, 13) ^ std::rotr(x, 22);
}

[[nodiscard]] constexpr std::uint32_t bigSigma1(std::uint32_t x) noexcept {
    return std::rotr(x, 6) ^ std::rotr(x, 11) ^ std::rotr(x, 25);
}

[[nodiscard]] constexpr std::uint32_t smallSigma0(std::uint32_t x) noexcept {
    return std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3);
}

[[nodiscard]] constexpr std::uint32_t smallSigma1(std::uint32_t x) noexcept {
    return std::rotr(x, 17) ^ std::rotr(x, 19) ^ (x >> 10);
}

/// Big-endian load of the 32-bit word at `block[offset]`. Explicit shifts rather than
/// std::bit_cast or memcpy so the byte order is the algorithm's, not the host's - this
/// implementation must produce identical digests on a big-endian target.
[[nodiscard]] std::uint32_t loadBigEndian32(std::span<const std::byte, 64> block,
                                             std::size_t offset) noexcept {
    return (static_cast<std::uint32_t>(block[offset]) << 24) |
           (static_cast<std::uint32_t>(block[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(block[offset + 2]) << 8) |
           static_cast<std::uint32_t>(block[offset + 3]);
}

/// The FIPS 180-4 compression function: absorbs exactly one 64-byte block into `state`.
void compress(std::array<std::uint32_t, 8>& state,
              std::span<const std::byte, 64> block) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
        w[i] = loadBigEndian32(block, i * 4);
    }
    for (std::size_t i = 16; i < 64; ++i) {
        w[i] = smallSigma1(w[i - 2]) + w[i - 7] + smallSigma0(w[i - 15]) + w[i - 16];
    }

    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];

    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t t1 = h + bigSigma1(e) + ch(e, f, g) + kRoundConstants[i] + w[i];
        const std::uint32_t t2 = bigSigma0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

}  // namespace

void Sha256::update(std::span<const std::byte> data) noexcept {
    totalBytes_ += data.size();

    // Top off a partially-filled block first, so the fast path below always starts aligned.
    if (blockLen_ > 0) {
        const std::size_t wanted = 64 - blockLen_;
        const std::size_t taken = std::min(wanted, data.size());
        std::copy_n(data.begin(), taken, block_.begin() + static_cast<std::ptrdiff_t>(blockLen_));
        blockLen_ += taken;
        data = data.subspan(taken);
        if (blockLen_ < 64) {
            return;  // still short of a full block; nothing to compress yet
        }
        compress(state_, std::span<const std::byte, 64>{block_});
        blockLen_ = 0;
    }

    // Whole blocks straight from the caller's buffer, no copy through block_.
    while (data.size() >= 64) {
        compress(state_, data.first<64>());
        data = data.subspan(64);
    }

    // Retain the remainder for the next update() or for finish()'s padding.
    std::copy(data.begin(), data.end(), block_.begin());
    blockLen_ = data.size();
}

Digest Sha256::finish() const noexcept {
    // Finalize on copies so the caller's object is untouched: finish() twice returns the same
    // digest, and update() may legitimately continue after a finish().
    std::array<std::uint32_t, 8> state = state_;
    std::array<std::byte, 64> block = block_;
    std::size_t blockLen = blockLen_;

    // FIPS 180-4 padding: a single 0x80 byte, then zeros, then the message length in bits as a
    // 64-bit big-endian integer, arranged so the total is a whole number of 64-byte blocks.
    block[blockLen++] = std::byte{0x80};
    if (blockLen > 56) {
        // No room for the 8-byte length in this block: zero-fill, compress, continue in a new one.
        std::fill(block.begin() + static_cast<std::ptrdiff_t>(blockLen), block.end(),
                  std::byte{0});
        compress(state, std::span<const std::byte, 64>{block});
        blockLen = 0;
    }
    std::fill(block.begin() + static_cast<std::ptrdiff_t>(blockLen), block.end() - 8,
              std::byte{0});

    const std::uint64_t bitLength = totalBytes_ * 8;
    for (std::size_t i = 0; i < 8; ++i) {
        block[56 + i] = static_cast<std::byte>((bitLength >> (56 - 8 * i)) & 0xffu);
    }
    compress(state, std::span<const std::byte, 64>{block});

    Digest digest{};
    for (std::size_t i = 0; i < 8; ++i) {
        digest[i * 4] = static_cast<std::uint8_t>((state[i] >> 24) & 0xffu);
        digest[i * 4 + 1] = static_cast<std::uint8_t>((state[i] >> 16) & 0xffu);
        digest[i * 4 + 2] = static_cast<std::uint8_t>((state[i] >> 8) & 0xffu);
        digest[i * 4 + 3] = static_cast<std::uint8_t>(state[i] & 0xffu);
    }
    return digest;
}

void Sha256::reset() noexcept {
    *this = Sha256{};
}

Digest sha256(std::span<const std::byte> data) noexcept {
    Sha256 hasher;
    hasher.update(data);
    return hasher.finish();
}

std::array<char, 64> toHex(const Digest& digest) noexcept {
    constexpr std::array<char, 16> digits{'0', '1', '2', '3', '4', '5', '6', '7',
                                          '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::array<char, 64> hex{};
    for (std::size_t i = 0; i < digest.size(); ++i) {
        hex[i * 2] = digits[(digest[i] >> 4) & 0x0fu];
        hex[i * 2 + 1] = digits[digest[i] & 0x0fu];
    }
    return hex;
}

}  // namespace mdux::evidence
