/** @file Preview.hpp
 * @brief Host-only, stateless preview request boundary. Include after import std.
 */
#pragma once
namespace mdux::tools::preview {
struct Response {
    int         status{200};
    std::string body;
};
/// Handles a decoded route and JSON body. Owns all inputs and resources until readback completes.
[[nodiscard]] Response handle(const std::filesystem::path& root, std::string_view route, std::string_view body);
/// Confines an existing file to root, rejecting symlinks and traversal at every component.
[[nodiscard]] std::filesystem::path confined(const std::filesystem::path& root, const std::filesystem::path& relative);
}  // namespace mdux::tools::preview
