/** @file Proposal.cpp
 * @brief Builds, pushes and optionally opens proposal branches with git plumbing and no working tree.
 *
 * Every git command names the service-owned repository with `--git-dir`, so the checkout being served
 * is never read by git, locked, indexed or given a ref. The commit is assembled in a temporary index:
 * `read-tree` of the fetched base, `update-index` of the one proposed blob, `write-tree`, then
 * `commit-tree`. The push uses an empty force-with-lease expectation, which creates the branch or fails;
 * it cannot move a branch someone else already has.
 */
import std;
import mdux.tools.verify.artifacts;
#include "Proposal.hpp"

#include "Process.hpp"

namespace mdux::tools::preview {
namespace {
constexpr std::chrono::milliseconds localTimeout{30000};

std::string trimmed(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
        text.pop_back();
    return text;
}

/// Removes `user:secret@` from any URL a git or gh message quotes.
std::string redacted(std::string text) {
    for (std::size_t at = text.find("://"); at != std::string::npos; at = text.find("://", at + 3)) {
        const auto start = at + 3;
        const auto end   = text.find_first_of("/ \t\r\n'\"", start);
        const auto user  = text.find('@', start);
        if (user != std::string::npos && (end == std::string::npos || user < end))
            text.erase(start, user + 1 - start);
    }
    return text;
}

ProposalOutcome failure(std::string message) {
    ProposalOutcome outcome;
    outcome.status  = 502;
    outcome.code    = "PRV012";
    outcome.message = redacted(std::move(message));
    return outcome;
}

struct Git {
    const ProposalSettings& settings;
    std::string             indexFile;
    ProcessResult           run(std::vector<std::string> arguments, std::string input = {}, bool network = false) const {
        ProcessRequest request;
        request.arguments = {"git", "--git-dir=" + settings.gitDirectory.string()};
        request.arguments.insert(request.arguments.end(), arguments.begin(), arguments.end());
        request.environment = {
            {"GIT_TERMINAL_PROMPT",     "0"},
            {    "GCM_INTERACTIVE", "never"},
            { "GIT_OPTIONAL_LOCKS",     "0"},
            {             "LC_ALL",     "C"},
        };
        if (!indexFile.empty())
            request.environment.emplace_back("GIT_INDEX_FILE", indexFile);
        request.input   = std::move(input);
        request.timeout = network ? settings.networkTimeout : localTimeout;
        return runProcess(request);
    }
    std::optional<std::string> value(std::vector<std::string> arguments, std::string& error, std::string input = {}, bool network = false) const {
        const auto what   = arguments.front();
        auto       result = run(std::move(arguments), std::move(input), network);
        if (result.succeeded())
            return trimmed(std::move(result.output));
        error = !result.started   ? "git could not be started; install git and put it on PATH"
                : result.timedOut ? std::format("git {} timed out", what)
                                  : std::format("git {} failed: {}", what, trimmed(result.errors));
        return std::nullopt;
    }
};

/// A scratch directory created exclusively, so a pre-existing path can never redirect the index.
std::optional<std::filesystem::path> scratch() {
    std::random_device random;
    for (int attempt = 0; attempt < 8; ++attempt) {
        auto            path = std::filesystem::temp_directory_path() / std::format("mdux-preview-proposal-{:08x}{:08x}", random(), random());
        std::error_code error;
        if (std::filesystem::create_directory(path, error))
            return path;
    }
    return std::nullopt;
}

std::string repositorySlug(std::string url) {
    url = trimmed(std::move(url));
    if (url.ends_with(".git"))
        url.resize(url.size() - 4);
    const auto host = url.find("github.com");
    if (host == std::string::npos)
        return {};
    auto tail = url.substr(host + std::string_view{"github.com"}.size());
    if (tail.empty() || (tail.front() != '/' && tail.front() != ':'))
        return {};
    tail.erase(0, 1);
    const auto slash = tail.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 == tail.size() || tail.find('/', slash + 1) != std::string::npos)
        return {};
    return tail;
}
}  // namespace

ProposalOutcome fetchProposalBase(const ProposalSettings& settings, const std::string& base, const std::string& path) {
    const Git   git{settings, {}};
    std::string error;
    const auto  ref = "refs/mdux-preview/bases/" + base;
    if (!git.value({"fetch", "--no-tags", "--no-recurse-submodules", "--quiet", settings.remote, "+refs/heads/" + base + ":" + ref}, error, {}, true))
        return failure(std::move(error));
    auto commit = git.value({"rev-parse", "--verify", "--quiet", ref + "^{commit}"}, error);
    if (!commit)
        return failure(std::move(error));
    ProposalOutcome outcome;
    outcome.baseCommit = *commit;
    auto blob          = git.run({"cat-file", "blob", *commit + ":" + path});
    if (!blob.succeeded()) {
        outcome.status  = 409;
        outcome.code    = "PRV009";
        outcome.message = std::format("{} does not exist on {}; the proposal has no base to change", path, base);
        return outcome;
    }
    outcome.baseSource = std::move(blob.output);
    return outcome;
}

ProposalOutcome submitProposal(const ProposalSettings& settings, const ProposalPlan& plan, const std::string& baseCommit) {
    const auto directory = scratch();
    if (!directory)
        return failure("could not create a scratch directory for the proposal index");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{*directory};
    const Git   git{settings, (*directory / "index").string()};
    std::string error;
    auto        entry = git.value({"ls-tree", baseCommit, "--", plan.path}, error);
    if (!entry)
        return failure(std::move(error));
    const auto mode = entry->substr(0, entry->find(' '));
    if (mode != "100644" && mode != "100755")
        return failure(plan.path + " is not a regular file at the base commit");
    const auto blob     = git.value({"hash-object", "-w", "--stdin"}, error, plan.source);
    const auto read     = blob ? git.value({"read-tree", baseCommit}, error) : std::nullopt;
    const auto updated  = read ? git.value({"update-index", "--cacheinfo", mode + "," + *blob + "," + plan.path}, error) : std::nullopt;
    const auto tree     = updated ? git.value({"write-tree"}, error) : std::nullopt;
    const auto baseTree = tree ? git.value({"rev-parse", baseCommit + "^{tree}"}, error) : std::nullopt;
    if (!baseTree)
        return failure(std::move(error));
    if (*tree == *baseTree) {
        ProposalOutcome outcome;
        outcome.status  = 422;
        outcome.code    = "PRV002";
        outcome.message = "the proposal does not change the base commit";
        return outcome;
    }
    auto commit = git.value({"commit-tree", *tree, "-p", baseCommit, "-F", "-"}, error, plan.message);
    if (!commit)
        return failure(std::move(error));
    ProposalOutcome outcome;
    outcome.status     = 201;
    outcome.baseCommit = baseCommit;
    outcome.commit     = *commit;
    outcome.branch     = plan.branchStem + "-" + commit->substr(0, 8);
    const auto target  = "refs/heads/" + outcome.branch;
    if (!git.value({"push", "--quiet", "--no-verify", "--force-with-lease=" + target + ":", settings.remote, *commit + ":" + target}, error, {}, true))
        return failure(std::move(error));
    if (!settings.pullRequests)
        return outcome;

    const auto url  = git.value({"remote", "get-url", settings.remote}, error);
    const auto slug = url ? repositorySlug(*url) : std::string{};
    if (slug.empty()) {
        outcome.warning = std::format("branch {} was pushed, but remote '{}' is not a GitHub repository; open the pull request manually",
                                      outcome.branch,
                                      settings.remote);
        return outcome;
    }
    ProcessRequest request;
    request.arguments =
        {"gh", "pr", "create", "--repo", slug, "--base", plan.base, "--head", outcome.branch, "--draft", "--title", plan.title, "--body-file", "-"};
    request.environment = {
        {"GH_PROMPT_DISABLED", "1"},
        {          "NO_COLOR", "1"}
    };
    request.input   = plan.body;
    request.timeout = settings.networkTimeout;
    auto created    = runProcess(request);
    if (!created.succeeded()) {
        outcome.warning = redacted(std::format("branch {} was pushed, but no pull request was opened: {}",
                                               outcome.branch,
                                               !created.started   ? "gh could not be started"
                                               : created.timedOut ? "gh timed out"
                                                                  : trimmed(created.errors)));
        return outcome;
    }
    std::istringstream lines(created.output);
    for (std::string line; std::getline(lines, line);)
        if (line.starts_with("https://"))
            outcome.pullRequestUrl = trimmed(line);
    if (outcome.pullRequestUrl.empty())
        outcome.warning = "gh reported success but printed no pull request URL";
    return outcome;
}
}  // namespace mdux::tools::preview
