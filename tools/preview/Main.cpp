/** @file Main.cpp
 * @brief Authenticated loopback HTTP transport for mdux-preview.
 */

// clang-format off
#include <algorithm>
#include <charconv>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <httplib.h>
#include "Preview.hpp"
// clang-format on
int main(int argc, char** argv) {
    try {
        std::filesystem::path root, tokenFile;
        int                   port = 0;
        for (int i = 1; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help") {
                (std::cout << "mdux-preview --root REPOSITORY --token-file FILE [--port PORT]\n");
                return 0;
            }
            if (i + 1 == argc)
                throw std::runtime_error("missing option value");
            std::string_view value = argv[++i];
            if (arg == "--root")
                root = value;
            else if (arg == "--token-file")
                tokenFile = value;
            else if (arg == "--port") {
                auto [p, ec] = std::from_chars(value.data(), value.data() + value.size(), port);
                if (ec != std::errc{} || p != value.data() + value.size() || port < 0 || port > 65535)
                    throw std::runtime_error("invalid port");
            } else
                throw std::runtime_error("unknown option");
        }
        if (root.empty() || tokenFile.empty())
            throw std::runtime_error("--root and --token-file are required");
        root                     = std::filesystem::canonical(root);
        tokenFile                = std::filesystem::canonical(tokenFile);
        const auto tokenRelative = tokenFile.lexically_relative(root);
        if (!tokenRelative.empty() && !tokenRelative.is_absolute() && *tokenRelative.begin() != "..")
            throw std::runtime_error("token file must be outside the repository root");
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
        httplib::Server server;
        std::mutex      work;
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
            auto response = mdux::tools::preview::handle(root, route, body);
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
        server.Post("/api/compile", [&](const auto& req, auto& res) {
            dispatch("compile", req, res);
        });
        server.Post("/api/frame", [&](const auto& req, auto& res) {
            dispatch("frame", req, res);
        });
        std::cout << "http://" << authority << std::endl;
        return server.listen_after_bind() ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "mdux-preview: " << e.what() << std::endl;
        return 1;
    }
}
