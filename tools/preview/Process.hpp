/** @file Process.hpp
 * @brief Bounded, shell-free child process execution for mdux-preview's proposal workflow.
 * Requires std::chrono, std::pair, std::string and std::vector declarations from import std
 * (backend) or their standard headers (C++17 implementation).
 */
#pragma once
namespace mdux::tools::preview {
/// One program invocation. Arguments are passed as a vector and never interpreted by a shell.
struct ProcessRequest {
    std::vector<std::string>                         arguments;             ///< `arguments[0]` is looked up on PATH
    std::vector<std::pair<std::string, std::string>> environment;           ///< added to, or replacing, the inherited environment
    std::string                                      input;                 ///< written to standard input, which is then closed
    std::chrono::milliseconds                        timeout{10000};
    std::size_t                                      outputLimit{1048576};  ///< per stream; excess output is read and discarded
};
struct ProcessResult {
    bool               started{false};   ///< false when the program could not be launched
    bool               timedOut{false};  ///< the process tree was killed at the deadline
    int                exitCode{-1};
    std::string        output;
    std::string        errors;
    [[nodiscard]] bool succeeded() const noexcept {
        return started && !timedOut && exitCode == 0;
    }
};
/// Runs a program to completion or its deadline; a timeout kills the whole process tree. Children get
/// no terminal. Only the three standard streams are inherited on Windows (an explicit handle list),
/// Apple platforms (POSIX_SPAWN_CLOEXEC_DEFAULT) and glibc 2.34+ (closefrom); on other POSIX systems a
/// descriptor the service opened without close-on-exec can also reach the child.
[[nodiscard]] ProcessResult runProcess(const ProcessRequest& request);
}  // namespace mdux::tools::preview
