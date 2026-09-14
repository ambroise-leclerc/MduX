/** @file Main.cpp
 * @brief Authenticated loopback HTTP transport for mdux-preview and its embedded Studio.
 */

// clang-format off
#include <algorithm>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <httplib.h>
#include "Proposal.hpp"
#include "Preview.hpp"
#include "StudioAssets.hpp"
// clang-format on
namespace {
// The pinned transport exposes a total read deadline through SocketStream. Reject
// reads after that deadline even when the peer keeps the socket continuously ready.
class RequestStream final : public httplib::Stream {
public:
    explicit RequestStream(socket_t descriptor)
        : stream(descriptor, 5, 0, 5, 0, 5000, std::chrono::steady_clock::now()), deadline(std::chrono::steady_clock::now() + std::chrono::seconds(5)) {}
    bool is_readable() const override {
        return withinDeadline() && stream.is_readable();
    }
    bool wait_readable() const override {
        return withinDeadline() && stream.wait_readable();
    }
    bool wait_writable() const override {
        return stream.wait_writable();
    }
    bool is_peer_alive() const override {
        return stream.is_peer_alive();
    }
    ssize_t read(char* data, size_t size) override {
        if (!withinDeadline()) {
            error_ = httplib::Error::Timeout;
            return -1;
        }
        const auto result = stream.read(data, size);
        error_            = stream.get_error();
        return result;
    }
    ssize_t write(const char* data, size_t size) override {
        return stream.write(data, size);
    }
    void get_remote_ip_and_port(std::string& ip, int& port) const override {
        stream.get_remote_ip_and_port(ip, port);
    }
    void get_local_ip_and_port(std::string& ip, int& port) const override {
        stream.get_local_ip_and_port(ip, port);
    }
    socket_t socket() const override {
        return stream.socket();
    }
    time_t duration() const override {
        return stream.duration();
    }

private:
    bool withinDeadline() const {
        return std::chrono::steady_clock::now() < deadline;
    }
    httplib::detail::SocketStream         stream;
    std::chrono::steady_clock::time_point deadline;
};
class PreviewServer final : public httplib::Server {
    bool process_and_close_socket(socket_t socket) override {
        const auto result = serve_guarded([&] {
            RequestStream stream(socket);
            bool          closed = false;
            // One request per connection: the header and body share a single deadline.
            return process_request(stream, "127.0.0.1", 0, "127.0.0.1", 0, true, closed, nullptr);
        });
        httplib::detail::drain_and_close_socket(socket);
        return result;
    }
};
/// True when `inner` names `outer` or something beneath it.
bool within(const std::filesystem::path& inner, const std::filesystem::path& outer) {
    const auto relative = inner.lexically_relative(outer);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}
}  // namespace

int main(int argc, char** argv) {
    try {
#ifndef _WIN32
        // A child that exits before reading its input must not terminate the service (ADR-025).
        std::signal(SIGPIPE, SIG_IGN);
#endif
        std::filesystem::path                                 root, tokenFile;
        std::optional<mdux::tools::preview::ProposalSettings> proposals;
        std::string                                           remote       = "origin";
        bool                                                  pullRequests = false;
        int                                                   port         = 0;
        for (int i = 1; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help") {
                (std::cout << "mdux-preview --root REPOSITORY --token-file FILE [--port PORT]\n"
                              "             [--proposal-git-dir GIT_DIR [--proposal-remote NAME] [--proposal-pull-requests]]\n");
                return 0;
            }
            if (arg == "--proposal-pull-requests") {
                pullRequests = true;
                continue;
            }
            if (i + 1 == argc)
                throw std::runtime_error("missing option value");
            std::string_view value = argv[++i];
            if (arg == "--root")
                root = value;
            else if (arg == "--token-file")
                tokenFile = value;
            else if (arg == "--proposal-git-dir")
                proposals.emplace().gitDirectory = value;
            else if (arg == "--proposal-remote")
                remote = value;
            else if (arg == "--port") {
                auto [p, ec] = std::from_chars(value.data(), value.data() + value.size(), port);
                if (ec != std::errc{} || p != value.data() + value.size() || port < 0 || port > 65535)
                    throw std::runtime_error("invalid port");
            } else
                throw std::runtime_error("unknown option");
        }
        if (root.empty() || tokenFile.empty())
            throw std::runtime_error("--root and --token-file are required");
        root      = std::filesystem::canonical(root);
        tokenFile = std::filesystem::canonical(tokenFile);
        if (within(tokenFile, root) || tokenFile == root)
            throw std::runtime_error("token file must be outside the repository root");
        if (proposals) {
            auto& settings        = *proposals;
            settings.gitDirectory = std::filesystem::canonical(settings.gitDirectory);
            if (!std::filesystem::is_directory(settings.gitDirectory))
                throw std::runtime_error("--proposal-git-dir must be a git directory");
            // Git operations must never touch the checkout being served, its index or its refs.
            if (within(settings.gitDirectory, root) || within(root, settings.gitDirectory) || settings.gitDirectory == root)
                throw std::runtime_error("--proposal-git-dir must be outside the repository root");
            if (remote.empty() || remote.front() == '-' || remote.find_first_of(" \t\r\n") != std::string::npos)
                throw std::runtime_error("invalid --proposal-remote");
            settings.remote       = remote;
            settings.pullRequests = pullRequests;
        } else if (pullRequests || remote != "origin") {
            throw std::runtime_error("proposal options require --proposal-git-dir");
        }
        if (std::filesystem::file_size(tokenFile) > 258)
            throw std::runtime_error("token file is too large");
        std::ifstream file(tokenFile, std::ios::binary);
        std::string   token;
        std::getline(file, token);
        if (!token.empty() && token.back() == '\r')
            token.pop_back();
        if (token.size() < 32 || token.size() > 256 || !std::all_of(token.begin(), token.end(), [](unsigned char c) {
                return c >= 33 && c <= 126;
            }))
            throw std::runtime_error("token must contain 32..256 printable non-space characters");
        const mdux::tools::preview::Service service{root, proposals};
        PreviewServer                       server;
        std::mutex                          work;
        server.new_task_queue = [] {
            return new httplib::ThreadPool(2, 0, 8);
        };
        server.set_payload_max_length(4194304);
        server.set_read_timeout(5, 0);
        server.set_write_timeout(5, 0);
        server.set_keep_alive_timeout(1);
        auto bound = port == 0 ? server.bind_to_any_port("127.0.0.1") : (server.bind_to_port("127.0.0.1", port) ? port : -1);
        if (bound < 0)
            throw std::runtime_error("cannot bind loopback port");
        const auto authority = "127.0.0.1:" + std::to_string(bound);
        server.set_pre_routing_handler([&](const httplib::Request& req, httplib::Response& res) {
            res.set_header("Cache-Control", "no-store");
            res.set_header("X-Content-Type-Options", "nosniff");
            if (req.get_header_value("Host") != authority || (req.has_header("Origin") && req.get_header_value("Origin") != "http://" + authority)) {
                res.status = 403;
                return httplib::Server::HandlerResponse::Handled;
            }
            // The embedded Studio files are public build output and carry no repository data; a page
            // cannot send a bearer header when a browser loads it. Every API route needs the token.
            if (req.path.rfind("/api/", 0) != 0)
                return httplib::Server::HandlerResponse::Unhandled;
            auto     supplied   = req.get_header_value("Authorization");
            auto     expected   = "Bearer " + token;
            unsigned difference = static_cast<unsigned>(supplied.size() != expected.size());
            for (std::size_t i = 0; i < expected.size(); ++i)
                difference |= static_cast<unsigned>(static_cast<unsigned char>(i < supplied.size() ? supplied[i] : 0)
                                                    ^ static_cast<unsigned char>(expected[i]));
            if (difference != 0) {
                res.status = 401;
                return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });
        auto dispatch = [&](std::string route, const httplib::Request& req, httplib::Response& res) {
            std::unique_lock lock(work, std::try_to_lock);
            if (!lock.owns_lock()) {
                res.status = 503;
                res.set_content(
                    R"({"tool":"mdux-preview","findings":[{"file":"","line":0,"column":0,"code":"PRV007","severity":"error","message":"preview busy","fixHint":"retry after the current preview completes"}]})",
                    "application/json");
                return;
            }
            std::string body = req.body;
            if (route == "detail") {
                std::string escaped;
                for (char c : req.get_param_value("recipe")) {
                    if (c == '"' || c == '\\')
                        escaped += '\\';
                    if (static_cast<unsigned char>(c) < 32) {
                        res.status = 400;
                        return;
                    }
                    escaped += c;
                }
                body = "{\"schemaVersion\":1,\"recipe\":\"" + escaped + "\"}";
            }
            auto response = mdux::tools::preview::handle(service, route, body);
            res.status    = response.status;
            res.set_content(std::move(response.body), "application/json");
        };
        server.Get("/api/catalog", [&](const auto& req, auto& res) {
            dispatch("catalog", req, res);
        });
        server.Get("/api/screens", [&](const auto& req, auto& res) {
            dispatch("screens", req, res);
        });
        server.Get("/api/screens/detail", [&](const auto& req, auto& res) {
            dispatch("detail", req, res);
        });
        for (const char* route : {"document", "compile", "frame", "proposals"}) {
            server.Post(std::string("/api/") + route, [&, route](const auto& req, auto& res) {
                dispatch(route, req, res);
            });
        }
        for (std::size_t i = 0; i < mdux::tools::preview::studioAssetCount; ++i) {
            const auto& asset = mdux::tools::preview::studioAssets[i];
            server.Get(asset.route, [&asset](const httplib::Request&, httplib::Response& res) {
                res.set_header("Content-Security-Policy",
                               "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data: blob:; connect-src 'self'; "
                               "base-uri 'none'; form-action 'none'; frame-ancestors 'none'");
                res.set_header("Referrer-Policy", "no-referrer");
                res.set_header("X-Frame-Options", "DENY");
                res.set_content(reinterpret_cast<const char*>(asset.data), asset.size, asset.contentType);
            });
        }
        std::cout << "http://" << authority << std::endl;
        return server.listen_after_bind() ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "mdux-preview: " << e.what() << std::endl;
        return 1;
    }
}
