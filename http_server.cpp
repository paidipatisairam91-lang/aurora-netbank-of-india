#include "http_server.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "Ws2_32.lib")
    using AuroraSocket = SOCKET;
    static constexpr AuroraSocket kInvalidSocket = INVALID_SOCKET;
    static void close_socket(AuroraSocket socket) { closesocket(socket); }
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #include <cerrno>
    using AuroraSocket = int;
    static constexpr AuroraSocket kInvalidSocket = -1;
    static void close_socket(AuroraSocket socket) { close(socket); }
#endif

namespace aurora {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string trim_header(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string mime_type(const std::filesystem::path& path) {
    const std::string extension = lower(path.extension().string());
    if (extension == ".html") return "text/html; charset=utf-8";
    if (extension == ".css") return "text/css; charset=utf-8";
    if (extension == ".js") return "application/javascript; charset=utf-8";
    if (extension == ".svg") return "image/svg+xml";
    if (extension == ".png") return "image/png";
    if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
    if (extension == ".webp") return "image/webp";
    if (extension == ".ico") return "image/x-icon";
    return "application/octet-stream";
}

std::string reason_phrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

bool send_all(AuroraSocket socket, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
#ifdef _WIN32
        const int count = ::send(socket, data.data() + sent, static_cast<int>(data.size() - sent), 0);
#else
        const ssize_t count = ::send(socket, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#endif
        if (count <= 0) return false;
        sent += static_cast<size_t>(count);
    }
    return true;
}

void send_response(AuroraSocket socket, const HttpResponse& response) {
    std::ostringstream header;
    header << "HTTP/1.1 " << response.status << ' ' << reason_phrase(response.status) << "\r\n";
    header << "Content-Type: " << response.content_type << "\r\n";
    header << "Content-Length: " << response.body.size() << "\r\n";
    header << "Connection: close\r\n";
    header << "X-Content-Type-Options: nosniff\r\n";
    header << "Referrer-Policy: strict-origin-when-cross-origin\r\n";
    header << "Cache-Control: no-store\r\n";
    for (const auto& entry : response.headers) header << entry.first << ": " << entry.second << "\r\n";
    header << "\r\n";
    send_all(socket, header.str());
    send_all(socket, response.body);
}

HttpResponse plain_response(int status, const std::string& message) {
    HttpResponse response;
    response.status = status;
    response.content_type = "text/plain; charset=utf-8";
    response.body = message;
    return response;
}

}

HttpServer::HttpServer(BankService& service, int port) : service_(service), port_(port) {}

HttpResponse HttpServer::serve_static(const std::string& requested_path) {
    std::string relative = requested_path;
    const auto query = relative.find('?');
    if (query != std::string::npos) relative.erase(query);
    if (relative.empty() || relative == "/") relative = "/index.html";
    if (relative == "/favicon.ico") return plain_response(404, "Not found");

    std::filesystem::path relative_path = std::filesystem::path(relative).relative_path();
    for (const auto& part : relative_path) {
        if (part == ".." || part == ".") return plain_response(404, "Not found");
    }
    const std::filesystem::path root = std::filesystem::weakly_canonical(service_.root());
    const std::filesystem::path candidate = std::filesystem::weakly_canonical(root / relative_path);
    auto root_it = root.begin();
    auto candidate_it = candidate.begin();
    for (; root_it != root.end() && candidate_it != candidate.end(); ++root_it, ++candidate_it) {
        if (*root_it != *candidate_it) return plain_response(404, "Not found");
    }
    if (root_it != root.end()) return plain_response(404, "Not found");

    std::ifstream input(candidate, std::ios::binary);
    if (!input || !std::filesystem::is_regular_file(candidate)) return plain_response(404, "Not found");
    std::ostringstream content;
    content << input.rdbuf();
    HttpResponse response;
    response.status = 200;
    response.content_type = mime_type(candidate);
    response.body = content.str();
    return response;
}

void HttpServer::handle_client(std::intptr_t raw_socket) {
    const AuroraSocket socket = static_cast<AuroraSocket>(raw_socket);
    try {
        std::string request_data;
        request_data.reserve(8192);
        size_t header_end = std::string::npos;
        char buffer[8192];
        while ((header_end = request_data.find("\r\n\r\n")) == std::string::npos) {
#ifdef _WIN32
            const int received = recv(socket, buffer, sizeof(buffer), 0);
#else
            const ssize_t received = recv(socket, buffer, sizeof(buffer), 0);
#endif
            if (received <= 0) { close_socket(socket); return; }
            request_data.append(buffer, static_cast<size_t>(received));
            if (request_data.size() > 64 * 1024) {
                send_response(socket, plain_response(431, "Request headers too large"));
                close_socket(socket);
                return;
            }
        }

        const std::string header_text = request_data.substr(0, header_end);
        std::istringstream header_stream(header_text);
        std::string request_line;
        std::getline(header_stream, request_line);
        if (!request_line.empty() && request_line.back() == '\r') request_line.pop_back();
        std::istringstream first_line(request_line);
        std::string method, path, version;
        first_line >> method >> path >> version;
        if (method.empty() || path.empty() || version.rfind("HTTP/", 0) != 0) {
            send_response(socket, plain_response(400, "Malformed HTTP request"));
            close_socket(socket);
            return;
        }

        HttpRequest request;
        request.method = method;
        request.path = path;
        size_t content_length = 0;
        std::string line;
        while (std::getline(header_stream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            const std::string name = lower(trim_header(line.substr(0, colon)));
            const std::string value = trim_header(line.substr(colon + 1));
            request.headers[name] = value;
            if (name == "content-length") {
                try { content_length = static_cast<size_t>(std::stoull(value)); }
                catch (...) { content_length = 0; }
            }
        }
        if (content_length > 1024 * 1024) {
            send_response(socket, plain_response(413, "Request body exceeds 1 MB"));
            close_socket(socket);
            return;
        }

        const size_t body_start = header_end + 4;
        while (request_data.size() - body_start < content_length) {
#ifdef _WIN32
            const int received = recv(socket, buffer, sizeof(buffer), 0);
#else
            const ssize_t received = recv(socket, buffer, sizeof(buffer), 0);
#endif
            if (received <= 0) break;
            request_data.append(buffer, static_cast<size_t>(received));
        }
        if (request_data.size() - body_start < content_length) {
            send_response(socket, plain_response(400, "Incomplete request body"));
            close_socket(socket);
            return;
        }
        request.body = request_data.substr(body_start, content_length);

        HttpResponse response;
        if (request.path.rfind("/api/", 0) == 0) response = service_.handle(request);
        else if (request.method == "GET" || request.method == "HEAD") response = serve_static(request.path);
        else response = plain_response(405, "Method not allowed");
        if (request.method == "HEAD") response.body.clear();
        send_response(socket, response);
    } catch (const std::exception& exception) {
        send_response(socket, plain_response(500, std::string("Server error: ") + exception.what()));
    }
    close_socket(socket);
}

void HttpServer::run() {
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error("Could not initialize Windows sockets.");
#endif
    const AuroraSocket listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == kInvalidSocket) throw std::runtime_error("Could not create the HTTP server socket.");
    int reuse = 1;
#ifdef _WIN32
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<unsigned short>(port_));
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        close_socket(listener);
#ifdef _WIN32
        WSACleanup();
#endif
        throw std::runtime_error("Could not bind port " + std::to_string(port_) + ". Is another server already using it?");
    }
    if (listen(listener, 64) < 0) {
        close_socket(listener);
#ifdef _WIN32
        WSACleanup();
#endif
        throw std::runtime_error("Could not start listening for browser connections.");
    }

    std::cout << "Aurora Bank C++ server running at http://localhost:" << port_ << std::endl;
    std::cout << "Press Ctrl+C to stop the server." << std::endl;
    while (true) {
        sockaddr_in client_address{};
#ifdef _WIN32
        int address_length = sizeof(client_address);
#else
        socklen_t address_length = sizeof(client_address);
#endif
        const AuroraSocket client = accept(listener, reinterpret_cast<sockaddr*>(&client_address), &address_length);
        if (client == kInvalidSocket) continue;
        std::thread(&HttpServer::handle_client, this, static_cast<std::intptr_t>(client)).detach();
    }
}

}
