/**
 * @file generate-brand-mark.cpp
 * @brief Deterministic source-asset generator for brand-mark.qoi.
 *
 * This is authoring support, not part of any CMake target. Re-run explicitly when the demonstrator
 * mark changes; mdux-imagebake only decodes and validates the resulting QOI file.
 */
#include <array>
#include <cstdint>
#include <fstream>
#include <string_view>
#include <vector>

namespace {

struct Pixel {
    std::uint8_t   r;
    std::uint8_t   g;
    std::uint8_t   b;
    std::uint8_t   a;
    constexpr bool operator==(const Pixel&) const = default;
};

constexpr std::uint32_t width  = 240;
constexpr std::uint32_t height = 72;
constexpr Pixel         background{209, 214, 219, 255};
constexpr Pixel         ink{26, 31, 41, 255};
constexpr Pixel         accent{33, 184, 107, 255};

[[nodiscard]] Pixel pixelAt(std::uint32_t x, std::uint32_t y) {
    const bool outer    = y >= 12 && y <= 60 && x >= 16 + (60 - y) / 2 && x <= 64 - (60 - y) / 2;
    const bool inner    = y >= 30 && y <= 54 && x >= 29 + (54 - y) / 3 && x <= 51 - (54 - y) / 3;
    const bool crossbar = y >= 43 && y <= 47 && x >= 24 && x <= 56;
    if ((outer && !inner) || crossbar || (x >= 82 && x <= 205 && y >= 20 && y <= 38))
        return ink;
    if (x >= 82 && x <= 221 && y >= 46 && y <= 55)
        return accent;
    return background;
}

void appendBe32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void flushRun(std::vector<std::uint8_t>& bytes, std::uint8_t& run) {
    if (run != 0) {
        bytes.push_back(static_cast<std::uint8_t>(0xc0u | (run - 1u)));
        run = 0;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    std::vector<std::uint8_t> bytes{'q', 'o', 'i', 'f'};
    appendBe32(bytes, width);
    appendBe32(bytes, height);
    bytes.push_back(4);  // RGBA
    bytes.push_back(0);  // sRGB with linear alpha

    Pixel        previous{0, 0, 0, 255};
    std::uint8_t run = 0;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const Pixel current = pixelAt(x, y);
            if (current == previous) {
                ++run;
                if (run == 62)
                    flushRun(bytes, run);
                continue;
            }
            flushRun(bytes, run);
            bytes.push_back(0xfe);  // QOI_OP_RGB; alpha is always opaque
            bytes.push_back(current.r);
            bytes.push_back(current.g);
            bytes.push_back(current.b);
            previous = current;
        }
    }
    flushRun(bytes, run);
    constexpr std::array<std::uint8_t, 8> end{0, 0, 0, 0, 0, 0, 0, 1};
    bytes.insert(bytes.end(), end.begin(), end.end());

    std::ofstream output{argv[1], std::ios::binary | std::ios::trunc};
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return output ? 0 : 1;
}
