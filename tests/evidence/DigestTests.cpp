/**
 * @file DigestTests.cpp
 * @brief Tests for the governed-zone mdux.evidence.digest module.
 *
 * @compliance ADR-007 Evidence pipeline doctrine
 *
 * Every expected digest below is a published FIPS 180-4 value or was produced by an
 * independent implementation, never by this one - a test that compares an implementation
 * against itself proves only that it is deterministic, which is the weaker half of what
 * this module has to be.
 */

import std;
import mdux.evidence.digest;
import mdux.test;

#include "../framework/MduXTest.hpp"

using namespace mdux::evidence;

namespace {

/// Hashes the bytes of an ASCII string literal, which is how every published vector is stated.
[[nodiscard]] Digest hashText(std::string_view text) noexcept {
    return sha256(std::as_bytes(std::span{text}));
}

[[nodiscard]] std::string hex(const Digest& digest) {
    const std::array<char, 64> chars = toHex(digest);
    return std::string{chars.data(), chars.size()};
}

/// n repetitions of `fill`, as bytes.
[[nodiscard]] std::vector<std::byte> repeated(char fill, std::size_t n) {
    return std::vector<std::byte>(n, static_cast<std::byte>(fill));
}

}  // namespace

// ---------------------------------------------------------------------------
// Published FIPS 180-4 vectors
// ---------------------------------------------------------------------------

TEST_CASE("SHA-256 empty input matches the published vector", "evidence-unit") {
    CHECK(hex(hashText("")) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("SHA-256 'abc' matches the published vector", "evidence-unit") {
    CHECK(hex(hashText("abc")) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("SHA-256 448-bit vector matches", "evidence-unit") {
    // 56 bytes: one byte short of needing a second block once padding is added, so this
    // vector exercises the "no room for the length field" branch in finish().
    constexpr std::string_view message =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    REQUIRE(message.size() == 56);
    CHECK(hex(hashText(message)) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("SHA-256 896-bit vector matches", "evidence-unit") {
    constexpr std::string_view message =
        "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
        "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    REQUIRE(message.size() == 112);
    CHECK(hex(hashText(message)) ==
          "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

TEST_CASE("SHA-256 one million 'a' matches the published vector", "evidence-unit") {
    const std::vector<std::byte> message = repeated('a', 1'000'000);
    CHECK(hex(sha256(message)) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// ---------------------------------------------------------------------------
// Block-boundary behaviour
// ---------------------------------------------------------------------------

TEST_CASE("SHA-256 is correct either side of the 64-byte block boundary", "evidence-unit") {
    // 55/56 straddle the padding-length boundary; 63/64/65 straddle the block boundary;
    // 119/120 and 127/128 repeat both one block further along, where an off-by-one in the
    // multi-block loop rather than in finish() would show up.
    struct Vector {
        std::size_t length;
        std::string_view expected;
    };
    constexpr std::array<Vector, 10> vectors{
        Vector{55, "8963cc0afd622cc7574ac2011f93a3059b3d65548a77542a1559e3d202e6ab00"},
        Vector{56, "6ea719cefa4b31862035a7fa606b7cc3602f46231117d135cc7119b3c1412314"},
        Vector{57, "a00df74fbdadd9eb0e7742a019e5b2d77374de5417eba5b7a0730a60cce5e7bf"},
        Vector{63, "1b58d00f5b1fbd2a1884d666a2be33c2fa7463dff32cd60ef200c0f750a6b70f"},
        Vector{64, "d53eda7a637c99cc7fb566d96e9fa109bf15c478410a3f5eb4d4c4e26cd081f6"},
        Vector{65, "836203944f4c0280461ad73d31457c22ba19d1d99e232dc231000085899e00a2"},
        Vector{119, "17d2f0f7197a6612e311d141781f2b9539c4aef7affd729246c401890e000dde"},
        Vector{120, "a4f4256159ea6fb23b27eb8c5eb9cfb9083475985f355a85c78de8f2fef2b3ac"},
        Vector{127, "026134f6117e45a37c5c2dc2f330bdd274c6dc087526b91ecec4d6dac9bb7346"},
        Vector{128, "b6ac3cc10386331c765f04f041c147d0f278f2aed8eaa021e2d0057fc6f6ff9e"},
    };

    for (const Vector& vector : vectors) {
        const std::vector<std::byte> message = repeated('A', vector.length);
        CHECK_MESSAGE(hex(sha256(message)) == vector.expected,
                      "length " + std::to_string(vector.length) + " digest was " +
                          hex(sha256(message)));
    }
}

// ---------------------------------------------------------------------------
// Streaming equivalence
// ---------------------------------------------------------------------------

TEST_CASE("Streaming update() in awkward chunks equals the one-shot digest", "evidence-unit") {
    // 300 bytes is deliberately not a multiple of 64, and the chunk sizes below are chosen so
    // that chunk boundaries fall inside, on, and either side of block boundaries.
    std::vector<std::byte> message(300);
    for (std::size_t i = 0; i < message.size(); ++i) {
        message[i] = static_cast<std::byte>(i % 256);
    }
    const Digest expected = sha256(message);

    for (std::size_t chunk : {std::size_t{1}, std::size_t{7}, std::size_t{63}, std::size_t{64},
                              std::size_t{65}, std::size_t{128}, std::size_t{299}}) {
        Sha256 hasher;
        std::span<const std::byte> remaining{message};
        while (!remaining.empty()) {
            const std::size_t take = std::min(chunk, remaining.size());
            hasher.update(remaining.first(take));
            remaining = remaining.subspan(take);
        }
        CHECK_MESSAGE(hasher.finish() == expected,
                      "chunk size " + std::to_string(chunk) + " diverged from the one-shot digest");
    }
}

TEST_CASE("Streaming a multi-megabyte buffer equals the one-shot digest", "evidence-unit") {
    // 4 MiB is the order of magnitude issue #18's weight blobs reach, which is the reason the
    // streaming path exists at all - exercise it at that size rather than only on toy inputs.
    // The modulus is 251 (prime, < 256) so the pattern does not align with the 64-byte block.
    std::vector<std::byte> message(4 * 1024 * 1024);
    for (std::size_t i = 0; i < message.size(); ++i) {
        message[i] = static_cast<std::byte>(i % 251);
    }

    CHECK(hex(sha256(message)) ==
          "a117210941a0b00dcb2d8577e680d84b6fa0eaf760d2afc654c953b9859d54fa");

    Sha256 hasher;
    std::span<const std::byte> remaining{message};
    while (!remaining.empty()) {
        const std::size_t take = std::min(std::size_t{7919}, remaining.size());
        hasher.update(remaining.first(take));
        remaining = remaining.subspan(take);
    }
    CHECK(hasher.finish() == sha256(message));
}

TEST_CASE("An empty update() does not change the digest", "evidence-unit") {
    Sha256 hasher;
    hasher.update({});
    hasher.update(std::as_bytes(std::span{std::string_view{"abc"}}));
    hasher.update({});
    CHECK(hasher.finish() == hashText("abc"));
}

// ---------------------------------------------------------------------------
// API contract
// ---------------------------------------------------------------------------

TEST_CASE("finish() is non-destructive and update() may continue after it", "evidence-unit") {
    Sha256 hasher;
    hasher.update(std::as_bytes(std::span{std::string_view{"abc"}}));

    const Digest first = hasher.finish();
    CHECK(first == hashText("abc"));
    CHECK(hasher.finish() == first);  // repeatable, per the documented contract

    hasher.update(std::as_bytes(std::span{std::string_view{"def"}}));
    CHECK(hasher.finish() == hashText("abcdef"));
}

TEST_CASE("reset() returns the hasher to its initial state", "evidence-unit") {
    Sha256 hasher;
    hasher.update(std::as_bytes(std::span{std::string_view{"discard me"}}));
    hasher.reset();
    CHECK(hasher.finish() == hashText(""));
}

TEST_CASE("toHex is lowercase, 64 characters, and covers every nibble value", "evidence-unit") {
    Digest digest{};
    for (std::size_t i = 0; i < digest.size(); ++i) {
        digest[i] = static_cast<std::uint8_t>(i * 8);
    }
    const std::string encoded = hex(digest);

    REQUIRE(encoded.size() == 64);
    CHECK(encoded == "0008101820283038404850586068707880889098a0a8b0b8c0c8d0d8e0e8f0f8");
    CHECK(std::ranges::none_of(encoded, [](char c) { return std::isupper(static_cast<unsigned char>(c)) != 0; }));
}
