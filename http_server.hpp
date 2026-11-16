#pragma once
#include "bank_service.hpp"
#include <cstdint>
#include <string>

namespace aurora {

class HttpServer {
public:
    HttpServer(BankService& service, int port);
    void run();
private:
    BankService& service_;
    int port_;
    void handle_client(std::intptr_t client_socket);
    HttpResponse serve_static(const std::string& path);
};

}
