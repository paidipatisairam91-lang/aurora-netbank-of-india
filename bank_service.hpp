#pragma once

#include "json.hpp"
#include <mutex>
#include <string>
#include <unordered_map>

namespace aurora {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
};

struct HttpResponse {
    int status = 200;
    std::string content_type = "application/json; charset=utf-8";
    std::string body;
    std::unordered_map<std::string, std::string> headers;
};

class BankService {
public:
    explicit BankService(std::string project_root);
    HttpResponse handle(const HttpRequest& request);
    const std::string& root() const { return project_root_; }

private:
    std::string project_root_;
    std::string database_file_;
    Json database_;
    std::unordered_map<std::string, long long> sessions_;
    std::mutex mutex_;

    void load_database();
    void save_database();
    HttpResponse api(const HttpRequest& request);
    Json branches() const;
    Json clean_account(const Json& account) const;
    Json error_json(int status, const std::string& message) const;
    Json add_transaction(Json& account, const std::string& type, double amount, const std::string& note);
    void initialize_account(Json& account);
    Json* account_by_id(long long id);
    Json* current_account(const HttpRequest& request);
    std::string create_token(long long account_id);
};

}
