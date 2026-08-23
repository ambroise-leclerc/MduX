/**
 * @file TemporaryDirectory.hpp
 * @brief A scratch directory a scenario owns for its own lifetime, and nobody else's.
 *
 * Extracted when the third copy appeared (#200). Three test files were carrying the same class, and
 * all three carried the same defect with it: a fixed name directly under the system temporary
 * directory, removed at construction *and* at destruction. Two concurrent `medui_tools_spec` runs -
 * `ctest -j`, or two builds on one machine - would share the path, and either could delete files
 * while the other process was using them. The failure that produces is an intermittent subprocess
 * error with nothing to do with the code under test, which is the worst kind to debug and the
 * easiest to blame on the tool being tested.
 *
 * The name now carries a random suffix, so two runs cannot collide however they are launched.
 * `std::random_device` rather than a process id because it needs no platform header - this tree
 * reaches the standard library through `import std`, and mixing that with `<unistd.h>` or
 * `<process.h>` is what the C5050 diagnostics elsewhere in this epic were about.
 */
#pragma once

namespace mdux::test {

/// A directory this scenario owns, removed when it is done with it.
class TemporaryDirectory {
public:
    explicit TemporaryDirectory(std::string_view name)
        : path_{std::filesystem::temp_directory_path() / std::format("{}-{:08x}", name, std::random_device{}())} {
        std::error_code code;
        std::filesystem::create_directories(path_, code);
    }

    TemporaryDirectory(const TemporaryDirectory&)            = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() {
        // Best effort, and deliberately not asserted: a scenario that has already failed should
        // report why it failed rather than that its scratch could not be swept up afterwards.
        std::error_code code;
        std::filesystem::remove_all(path_, code);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

}  // namespace mdux::test
