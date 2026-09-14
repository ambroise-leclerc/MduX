/** @file Input.cppm
 * @brief Host input reader seam for request-owned, confined source snapshots.
 */
module;

export module mdux.tools.input;
import std;
export namespace mdux::tools {
/// Reads an input as owned bytes. An absent result means the input was refused or unreadable.
using InputReader = std::function<std::optional<std::vector<std::byte>>(const std::filesystem::path&)>;
}  // namespace mdux::tools
