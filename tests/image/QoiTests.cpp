/**
 * @file QoiTests.cpp
 * @brief Dependency-free QOI decoder scenarios.
 */
import std;
import speclab;
import mdux.tools.qoi;
#include "../framework/SpecLabBridge.hpp"

namespace {
[[nodiscard]] std::vector<std::byte> twoPixelQoi() {
    const std::array<unsigned char, 31> raw{'q', 'o', 'i', 'f', 0, 0, 0, 2, 0, 0, 0, 1, 4, 0, 0xfe, 10, 20, 30, 0xff, 40, 50, 60, 70, 0, 0, 0, 0, 0, 0, 0, 1};
    std::vector<std::byte>              bytes;
    bytes.reserve(raw.size());
    for (const unsigned char value : raw)
        bytes.push_back(static_cast<std::byte>(value));
    return bytes;
}

[[nodiscard]] std::vector<std::byte> opcodeQoi() {
    // Five pixels exercise the compact opcode families after one explicit RGB seed:
    //   RGB(10,20,30), DIFF(+1,-1,0), LUMA(+3,+2,+1), INDEX(seed), RUN(seed).
    const std::array<unsigned char, 31> raw{'q', 'o', 'i',  'f',  0,    0,    0,    5, 0, 0, 0, 1, 4, 0, 0xfe, 10,
                                            20,  30,  0x76, 0xa2, 0x97, 0x09, 0xc0, 0, 0, 0, 0, 0, 0, 0, 1};
    std::vector<std::byte>              bytes;
    bytes.reserve(raw.size());
    for (const unsigned char value : raw)
        bytes.push_back(static_cast<std::byte>(value));
    return bytes;
}

[[nodiscard]] std::vector<std::byte> oversizedQoi() {
    // 4097 x 4096 exceeds the decoder's explicit 64 MiB RGBA ceiling. No chunk is needed: extent
    // validation happens before reserve or decoding, which is the property this fixture pins.
    const std::array<unsigned char, 22> raw{'q', 'o', 'i', 'f', 0, 0, 0x10, 0x01, 0, 0, 0x10, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    std::vector<std::byte>              bytes;
    bytes.reserve(raw.size());
    for (const unsigned char value : raw)
        bytes.push_back(static_cast<std::byte>(value));
    return bytes;
}
}  // namespace

const mdux::spec::Register qoiDecodesRgbAndRgba{"QOI RGB and RGBA chunks decode to straight RGBA8", "evidence-unit", [] {
                                                    return speclab::Test("qoi-rgb-rgba")
                                                        .Given("a valid two-pixel QOI stream containing RGB and RGBA chunks", [] {})
                                                        .When("the complete stream is decoded", [] {})
                                                        .Then("the decoded pixels preserve channels and alpha",
                                                              [] {
                                                                  const auto decoded = mdux::tools::qoi::decode(twoPixelQoi());
                                                                  if (!decoded.has_value())
                                                                      throw speclab::core::AssertionFailure("decode failed", std::source_location::current());
                                                                  constexpr std::array<std::byte, 8> expected{std::byte{10},
                                                                                                              std::byte{20},
                                                                                                              std::byte{30},
                                                                                                              std::byte{255},
                                                                                                              std::byte{40},
                                                                                                              std::byte{50},
                                                                                                              std::byte{60},
                                                                                                              std::byte{70}};
                                                                  mdux::spec::Checks                 checks;
                                                                  checks.expect(decoded->width == 2 && decoded->height == 1, "extent decodes");
                                                                  checks.expect(std::ranges::equal(decoded->rgba, expected), "RGBA pixels decode");
                                                                  checks.raise();
                                                              })
                                                        .Execute();
                                                }};

const mdux::spec::Register qoiRejectsTrailingData{"QOI refuses bytes after the end marker", "evidence-unit", [] {
                                                      return speclab::Test("qoi-trailing-data")
                                                          .Given("a valid QOI stream followed by one extra byte", [] {})
                                                          .When("the complete-file decoder reads it", [] {})
                                                          .Then("the complete-file decoder rejects the stream",
                                                                [] {
                                                                    auto bytes = twoPixelQoi();
                                                                    bytes.push_back(std::byte{0});
                                                                    const auto         decoded = mdux::tools::qoi::decode(bytes);
                                                                    mdux::spec::Checks checks;
                                                                    checks.expect(!decoded.has_value(), "stream is refused");
                                                                    if (!decoded.has_value())
                                                                        checks.expect(decoded.error() == mdux::tools::qoi::DecodeError::BadEndMarker,
                                                                                      "error names displaced end marker");
                                                                    checks.raise();
                                                                })
                                                          .Execute();
                                                  }};

const mdux::spec::Register qoiDecodesCompactOpcodes{"QOI compact opcodes preserve their specified channel deltas", "evidence-unit", [] {
                                                        return speclab::Test("qoi-compact-opcodes")
                                                            .Given("a QOI stream containing INDEX, DIFF, LUMA and RUN", [] {})
                                                            .When("the compact chunks are decoded in sequence", [] {})
                                                            .Then("INDEX, DIFF, LUMA and RUN decode in sequence",
                                                                  [] {
                                                                      const auto decoded = mdux::tools::qoi::decode(opcodeQoi());
                                                                      if (!decoded.has_value())
                                                                          throw speclab::core::AssertionFailure("decode failed",
                                                                                                                std::source_location::current());
                                                                      constexpr std::array<std::byte, 20> expected{
                                                                          std::byte{10},  std::byte{20},  std::byte{30},  std::byte{255}, std::byte{11},
                                                                          std::byte{19},  std::byte{30},  std::byte{255}, std::byte{14},  std::byte{21},
                                                                          std::byte{31},  std::byte{255}, std::byte{10},  std::byte{20},  std::byte{30},
                                                                          std::byte{255}, std::byte{10},  std::byte{20},  std::byte{30},  std::byte{255}};
                                                                      mdux::spec::Checks checks;
                                                                      checks.expect(std::ranges::equal(decoded->rgba, expected), "compact opcodes decode");
                                                                      checks.raise();
                                                                  })
                                                            .Execute();
                                                    }};

const mdux::spec::Register qoiBoundsDecodedMemory{"QOI dimensions cannot request an unbounded RGBA allocation", "evidence-unit", [] {
                                                      return speclab::Test("qoi-decoded-memory-limit")
                                                          .Given("a QOI header whose extent exceeds the decoded-pixel ceiling", [] {})
                                                          .When("the decoder validates the extent before allocation", [] {})
                                                          .Then("a sheet just beyond 4096 squared is rejected before allocation",
                                                                [] {
                                                                    const auto         decoded = mdux::tools::qoi::decode(oversizedQoi());
                                                                    mdux::spec::Checks checks;
                                                                    checks.expect(!decoded.has_value()
                                                                                      && decoded.error() == mdux::tools::qoi::DecodeError::SizeOverflow,
                                                                                  "the explicit decoded-pixel ceiling is enforced");
                                                                    checks.raise();
                                                                })
                                                          .Execute();
                                                  }};
