#include "bank_service.hpp"
#include "http_server.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char* argv[]) {
    try {
        std::string root = std::filesystem::current_path().string();
        if (argc > 1) root = argv[1];
        root = std::filesystem::absolute(root).string();

        int port = 3000;
        if (const char* configured_port = std::getenv("PORT")) {
            try { port = std::stoi(configured_port); }
            catch (...) { throw std::runtime_error("PORT must be a valid number."); }
        }
        if (port < 1 || port > 65535) throw std::runtime_error("PORT must be between 1 and 65535.");

        aurora::BankService bank(root);
        aurora::HttpServer server(bank, port);
        server.run();
    } catch (const std::exception& error) {
        std::cerr << "Aurora Bank could not start: " << error.what() << std::endl;
        return 1;
    }
    return 0;
}
