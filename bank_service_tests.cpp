#include "../src/bank_service.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace aurora;

static HttpResponse call(BankService& bank, const std::string& method,
                         const std::string& path, const std::string& body = "",
                         const std::string& token = "") {
    HttpRequest request;
    request.method = method;
    request.path = path;
    request.body = body;
    if (!token.empty()) request.headers["authorization"] = "Bearer " + token;
    return bank.handle(request);
}

int main() {
    namespace fs = std::filesystem;
    const fs::path source = fs::current_path();
    const fs::path test_root = fs::temp_directory_path() / "aurora-bank-cpp-tests";
    fs::remove_all(test_root);
    fs::create_directories(test_root / "database");
    fs::copy_file(source / "database" / "bank.json", test_root / "database" / "bank.json");

    BankService bank(test_root.string());

    const auto branch_response = call(bank, "GET", "/api/branches");
    assert(branch_response.status == 200);
    assert(Json::parse(branch_response.body)["branches"].size() == 10);

    const auto signup = call(bank, "POST", "/api/accounts",
        "{\"name\":\"Demo Test User\",\"phone\":\"9876543210\",\"email\":\"demo@example.com\",\"address\":\"Test Street\",\"city\":\"Guntur\",\"pincode\":\"522001\",\"pin\":\"2468\",\"type\":\"Savings\",\"initialDeposit\":1000}");
    assert(signup.status == 201);
    const Json signup_data = Json::parse(signup.body);
    assert(signup_data["account"]["city"].as_string() == "Guntur");
    assert(signup_data["account"]["balance"].as_number() == 1000);
    assert(!signup_data["account"].contains("pin"));

    const auto invalid_login = call(bank, "POST", "/api/auth/login", "{\"accountNumber\":1001,\"pin\":\"0000\"}");
    assert(invalid_login.status == 401);

    const auto login = call(bank, "POST", "/api/auth/login", "{\"accountNumber\":1001,\"pin\":\"1234\"}");
    assert(login.status == 200);
    const Json login_data = Json::parse(login.body);
    const std::string token = login_data["token"].as_string();
    assert(!token.empty());
    assert(!login_data["account"].contains("pin"));

    const auto profile = call(bank, "GET", "/api/me", "", token);
    assert(profile.status == 200);
    const double before = Json::parse(profile.body)["account"]["balance"].as_number();

    const auto no_session = call(bank, "GET", "/api/me");
    assert(no_session.status == 401);

    const auto deposit = call(bank, "POST", "/api/deposit", "{\"amount\":123.45}", token);
    assert(deposit.status == 200);
    const double after_deposit = Json::parse(deposit.body)["account"]["balance"].as_number();
    assert(std::abs(after_deposit - (before + 123.45)) < 0.001);

    const auto bad_withdrawal = call(bank, "POST", "/api/withdraw", "{\"amount\":999999999}", token);
    assert(bad_withdrawal.status == 400);

    const auto blocked_transfer = call(bank, "POST", "/api/transfer",
        "{\"toAccount\":1002,\"amount\":400000,\"note\":\"test\"}", token);
    assert(blocked_transfer.status == 403);
    assert(Json::parse(blocked_transfer.body)["fraud"]["status"].as_string() == "BLOCK");

    const auto transactions = call(bank, "GET", "/api/transactions", "", token);
    assert(transactions.status == 200);
    assert(Json::parse(transactions.body)["transactions"].size() >= 1);

    const auto cards = call(bank, "GET", "/api/cards", "", token);
    assert(cards.status == 200);
    const auto card_toggle = call(bank, "POST", "/api/cards/toggle-block", "{\"cardType\":\"debit\"}", token);
    assert(card_toggle.status == 200);
    assert(Json::parse(card_toggle.body)["cards"]["debit"]["status"].as_string() == "BLOCKED");

    fs::remove_all(test_root);
    std::cout << "Bank service tests passed.\n";
    return 0;
}
