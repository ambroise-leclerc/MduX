/** @file Proposal.hpp
 * @brief Git side of mdux-preview's reviewable change proposals, isolated from the served checkout.
 * Requires std::chrono, std::filesystem::path and std::string declarations from import std
 * (backend) or their standard headers (C++17 transport).
 */
#pragma once
namespace mdux::tools::preview {
/// Write configuration. Absent unless the service was started with a proposal git directory.
struct ProposalSettings {
    std::filesystem::path     gitDirectory;         ///< a repository the service owns, never the served root
    std::string               remote{"origin"};
    bool                      pullRequests{false};  ///< open a draft pull request with `gh` after pushing
    std::chrono::milliseconds networkTimeout{120000};
};
/// Everything a proposal commits, already validated and compiled by the backend.
struct ProposalPlan {
    std::string path;        ///< repository-relative source path, generic separators
    std::string base;        ///< `develop` or an issue branch
    std::string branchStem;  ///< `<issue>-<slug>`; the commit prefix is appended
    std::string source;      ///< canonical proposed source
    std::string title;
    std::string message;     ///< commit message
    std::string body;        ///< pull request body
};
struct ProposalOutcome {
    int                status{200};
    std::string        code;        ///< a PRV code when the step failed
    std::string        message;     ///< safe to return to a client: credentials in URLs are redacted
    std::string        baseCommit;
    std::string        baseSource;  ///< the source at `baseCommit`, for the stale-base comparison
    std::string        commit;
    std::string        branch;
    std::string        pullRequestUrl;
    std::string        warning;
    [[nodiscard]] bool ok() const noexcept {
        return code.empty();
    }
};
/// Fetches `base` from the remote into a private ref namespace and reads `path` at its tip.
[[nodiscard]] ProposalOutcome fetchProposalBase(const ProposalSettings& settings, const std::string& base, const std::string& path);
/// Commits `plan.source` on top of `baseCommit` without a working tree, pushes a new branch and, when
/// configured, opens a draft pull request. Never merges and never updates an existing branch.
[[nodiscard]] ProposalOutcome submitProposal(const ProposalSettings& settings, const ProposalPlan& plan, const std::string& baseCommit);
}  // namespace mdux::tools::preview
