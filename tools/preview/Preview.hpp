/** @file Preview.hpp
 * @brief Host-only, stateless preview request boundary.
 * Requires std::filesystem::path, std::optional, std::string and std::string_view declarations from
 * import std (backend) or their standard headers (C++17 transport).
 */
#pragma once
#include "Proposal.hpp"
namespace mdux::tools::preview {
struct Response {
    int         status{200};
    std::string body;
};
/// What one service instance may read and, when proposals are configured, write.
struct Service {
    std::filesystem::path           root;
    std::optional<ProposalSettings> proposals;  ///< absent: every proposal submission is refused
};
/// Handles a decoded route and JSON body. Owns all inputs and resources until readback completes.
[[nodiscard]] Response handle(const Service& service, std::string_view route, std::string_view body);
/// Confines an existing file to root, rejecting symlinks and traversal at every component.
[[nodiscard]] std::filesystem::path confined(const std::filesystem::path& root, const std::filesystem::path& relative);
}  // namespace mdux::tools::preview
