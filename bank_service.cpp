#include "bank_service.hpp"
#include "fraud_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace aurora {
namespace {

struct Branch {
    const char* city;
    const char* state;
    const char* branch;
    const char* address;
    const char* ifsc;
};

const Branch kBranches[] = {
    {"Vijayawada", "Andhra Pradesh", "Aurora Bank - Vijayawada Branch", "MG Road, Vijayawada, Andhra Pradesh", "AURA0001001"},
    {"Guntur", "Andhra Pradesh", "Aurora Bank - Guntur Branch", "Arundelpet, Guntur, Andhra Pradesh", "AURA0001002"},
    {"Mangalagiri", "Andhra Pradesh", "Aurora Bank - Mangalagiri Branch", "Tadepalli Road, Mangalagiri, Andhra Pradesh", "AURA0001003"},
    {"Visakhapatnam", "Andhra Pradesh", "Aurora Bank - Visakhapatnam Branch", "Dwaraka Nagar, Visakhapatnam, Andhra Pradesh", "AURA0001004"},
    {"Tirupati", "Andhra Pradesh", "Aurora Bank - Tirupati Branch", "Air Bypass Road, Tirupati, Andhra Pradesh", "AURA0001005"},
    {"Rajahmundry", "Andhra Pradesh", "Aurora Bank - Rajahmundry Branch", "Danavaipeta, Rajahmundry, Andhra Pradesh", "AURA0001006"},
    {"Hyderabad", "Telangana", "Aurora Bank - Hyderabad Branch", "Hitech City, Hyderabad, Telangana", "AURA0001007"},
    {"Bengaluru", "Karnataka", "Aurora Bank - Bengaluru Branch", "Indiranagar, Bengaluru, Karnataka", "AURA0001008"},
    {"Chennai", "Tamil Nadu", "Aurora Bank - Chennai Branch", "T. Nagar, Chennai, Tamil Nadu", "AURA0001009"},
    {"Mumbai", "Maharashtra", "Aurora Bank - Mumbai Branch", "Andheri East, Mumbai, Maharashtra", "AURA0001010"}
};

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string now_iso() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t raw = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &raw);
#else
    gmtime_r(&raw, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S.000Z");
    return out.str();
}

long long parse_iso_epoch(const std::string& value) {
    if (value.size() < 19) return 0;
    std::tm parsed{};
    std::istringstream input(value.substr(0, 19));
    input >> std::get_time(&parsed, "%Y-%m-%dT%H:%M:%S");
    if (input.fail()) return 0;
#ifdef _WIN32
    return static_cast<long long>(_mkgmtime(&parsed));
#else
    return static_cast<long long>(timegm(&parsed));
#endif
}

long long now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

long long now_millis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string unique_id() {
    static std::mt19937_64 generator(std::random_device{}());
    static std::mutex random_mutex;
    std::lock_guard<std::mutex> lock(random_mutex);
    std::uniform_int_distribution<unsigned long long> distribution;
    const auto a = distribution(generator);
    const auto b = distribution(generator);
    std::ostringstream out;
    out << std::hex << std::setfill('0')
        << std::setw(8) << static_cast<unsigned>((a >> 32) & 0xffffffffULL) << '-'
        << std::setw(4) << static_cast<unsigned>((a >> 16) & 0xffffULL) << '-'
        << std::setw(4) << static_cast<unsigned>(a & 0xffffULL) << '-'
        << std::setw(4) << static_cast<unsigned>((b >> 48) & 0xffffULL) << '-'
        << std::setw(12) << (b & 0xffffffffffffULL);
    return out.str();
}

std::string timestamp_id(const std::string& prefix) {
    return prefix + std::to_string(now_millis());
}

double money(double value) {
    return std::round(value * 100.0) / 100.0;
}

bool digits(const std::string& value, size_t count) {
    return value.size() == count && std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c); });
}

bool allowed_account_type(const std::string& type) {
    return type == "Savings" || type == "Current";
}

double available_floor(const Json& account) {
    return string_value(account, "type") == "Savings"
        ? number_value(account, "minBalance", 500)
        : -number_value(account, "overdraftLimit", 1000);
}

bool has_sufficient_balance(const Json& account, double amount) {
    return number_value(account, "balance") - amount >= available_floor(account);
}

std::string csv_escape(const std::string& input) {
    std::string result = "\"";
    for (char ch : input) {
        if (ch == '"') result += "\"\"";
        else result += ch;
    }
    return result + "\"";
}

Json make_object(std::initializer_list<std::pair<std::string, Json>> values) {
    Json result = Json::object();
    for (const auto& item : values) result[item.first] = item.second;
    return result;
}

Json* find_array_item(Json& array, const std::string& key, long long value) {
    for (auto& item : array.as_array()) {
        if (item[key].as_int(-1) == value) return &item;
    }
    return nullptr;
}

Json* find_array_item_string(Json& array, const std::string& key, const std::string& value) {
    for (auto& item : array.as_array()) {
        if (string_value(item, key) == value) return &item;
    }
    return nullptr;
}

}

BankService::BankService(std::string project_root)
    : project_root_(std::move(project_root)),
      database_file_((std::filesystem::path(project_root_) / "database" / "bank.json").string()) {
    load_database();
}

void BankService::load_database() {
    std::ifstream input(database_file_, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open database/bank.json. Start the program from the project folder.");
    std::ostringstream contents;
    contents << input.rdbuf();
    database_ = Json::parse(contents.str());
    for (auto& account : database_["accounts"].as_array()) initialize_account(account);
}

void BankService::save_database() {
    const std::filesystem::path temporary = database_file_ + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot write bank database.");
        output << database_.dump(2);
        if (!output.good()) throw std::runtime_error("Failed while saving bank database.");
    }
    std::error_code error;
    std::filesystem::rename(temporary, database_file_, error);
    if (error) {
        std::filesystem::remove(database_file_, error);
        error.clear();
        std::filesystem::rename(temporary, database_file_, error);
        if (error) throw std::runtime_error("Could not replace bank database: " + error.message());
    }
}

void BankService::initialize_account(Json& account) {
    if (!account.contains("payees") || account["payees"].is_null()) account["payees"] = Json::array();
    if (!account.contains("cards") || account["cards"].is_null()) {
        const std::string id = std::to_string(account["id"].as_int());
        const std::string last_four = id.substr(id.size() > 4 ? id.size() - 4 : 0);
        account["cards"] = make_object({
            {"debit", make_object({{"number", "4592 •••• •••• " + last_four}, {"status", "ACTIVE"}, {"dailyLimit", 50000}})},
            {"credit", make_object({{"number", "5241 •••• •••• " + last_four}, {"status", "ACTIVE"}, {"limit", 150000}, {"available", 124000}})}
        });
    }
    if (!account.contains("deposits") || account["deposits"].is_null()) account["deposits"] = Json::array();
    if (!account.contains("investments") || account["investments"].is_null()) {
        account["investments"] = make_object({{"ppf", nullptr}, {"demat", nullptr}, {"mutualFunds", Json::array()}});
    }
    if (!account.contains("loans") || account["loans"].is_null()) account["loans"] = Json::array();
    if (!account.contains("insurance") || account["insurance"].is_null()) account["insurance"] = Json::array();
    if (!account.contains("stoppedCheques") || account["stoppedCheques"].is_null()) account["stoppedCheques"] = Json::array();
    if (!account.contains("fraudReports") || account["fraudReports"].is_null()) account["fraudReports"] = Json::array();
    if (!account.contains("secondaryBalance") || account["secondaryBalance"].is_null()) account["secondaryBalance"] = 15000;
}

Json BankService::branches() const {
    Json result = Json::array();
    for (const auto& branch : kBranches) {
        result.push_back(make_object({
            {"city", branch.city}, {"state", branch.state}, {"branch", branch.branch},
            {"address", branch.address}, {"ifsc", branch.ifsc}
        }));
    }
    return result;
}

Json BankService::clean_account(const Json& account) const {
    Json safe = account;
    safe.erase("pin");
    return safe;
}

Json BankService::error_json(int status, const std::string& message) const {
    (void)status;
    return make_object({{"error", message}});
}

Json BankService::add_transaction(Json& account, const std::string& type, double amount, const std::string& note) {
    Json tx = make_object({
        {"id", unique_id()}, {"accountId", account["id"]}, {"type", type},
        {"amount", money(amount)}, {"note", note}, {"createdAt", now_iso()},
        {"balanceAfter", money(number_value(account, "balance"))}
    });
    auto& transactions = database_["transactions"].as_array();
    transactions.insert(transactions.begin(), tx);
    return tx;
}

Json* BankService::account_by_id(long long id) {
    return find_array_item(database_["accounts"], "id", id);
}

std::string BankService::create_token(long long account_id) {
    const std::string token = unique_id();
    sessions_[token] = account_id;
    return token;
}

Json* BankService::current_account(const HttpRequest& request) {
    auto header = request.headers.find("authorization");
    if (header == request.headers.end()) return nullptr;
    std::string token = header->second;
    const std::string prefix = "Bearer ";
    if (token.rfind(prefix, 0) == 0) token.erase(0, prefix.size());
    const auto session = sessions_.find(token);
    if (session == sessions_.end()) return nullptr;
    return account_by_id(session->second);
}

HttpResponse BankService::handle(const HttpRequest& request) {
    if (request.path.rfind("/api/", 0) == 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        try { return api(request); }
        catch (const std::exception& exception) {
            HttpResponse response;
            response.status = 400;
            response.body = error_json(400, exception.what()).dump();
            return response;
        }
    }
    HttpResponse response;
    response.status = 404;
    response.body = "Not found";
    response.content_type = "text/plain; charset=utf-8";
    return response;
}

HttpResponse BankService::api(const HttpRequest& request) {
    HttpResponse response;
    auto send_json = [&](int status, const Json& value) {
        response.status = status;
        response.content_type = "application/json; charset=utf-8";
        response.body = value.dump();
        return response;
    };
    auto fail = [&](int status, const std::string& message) {
        return send_json(status, error_json(status, message));
    };
    auto read_body = [&]() {
        if (request.body.empty()) return Json::object();
        return Json::parse(request.body);
    };

    if (request.method == "GET" && request.path == "/api/branches") {
        return send_json(200, make_object({{"branches", branches()}}));
    }

    if (request.method == "POST" && request.path == "/api/auth/login") {
        const Json body = read_body();
        const long long account_number = body["accountNumber"].as_int(-1);
        const std::string pin = body["pin"].as_string();
        Json* account = account_by_id(account_number);
        if (!account || string_value(*account, "pin") != pin) return fail(401, "Incorrect account number or PIN.");
        return send_json(200, make_object({{"token", create_token(account_number)}, {"account", clean_account(*account)}}));
    }

    if (request.method == "POST" && request.path == "/api/accounts") {
        const Json body = read_body();
        const std::string name = trim(string_value(body, "name"));
        const std::string phone = trim(string_value(body, "phone"));
        const std::string email = trim(string_value(body, "email"));
        const std::string address = trim(string_value(body, "address"));
        const std::string city = trim(string_value(body, "city"));
        const std::string pincode = body["pincode"].as_string();
        const std::string pin = body["pin"].as_string();
        const std::string type = string_value(body, "type");
        const double amount = money(number_value(body, "initialDeposit", std::numeric_limits<double>::quiet_NaN()));
        const Branch* selected = nullptr;
        for (const auto& branch : kBranches) if (city == branch.city) { selected = &branch; break; }
        if (name.empty() || phone.empty() || email.empty() || address.empty() || !selected ||
            !digits(pincode, 6) || !digits(pin, 4) || !allowed_account_type(type) || !std::isfinite(amount) || amount < 0) {
            return fail(400, "Enter valid details, select one of the 10 branch cities, and use a 4-digit PIN.");
        }
        if (type == "Savings" && amount < 500) return fail(400, "Savings accounts require min ₹500 balance.");

        const long long id = database_["nextAccountNumber"].as_int(1005);
        database_["nextAccountNumber"] = id + 1;
        Json account = make_object({
            {"id", id}, {"name", name}, {"phone", phone}, {"email", email}, {"address", address},
            {"city", selected->city}, {"state", selected->state}, {"pincode", pincode},
            {"createdAt", now_iso()}, {"isRegisteredCustomer", true}, {"branch", selected->branch},
            {"branchAddress", selected->address}, {"ifsc", selected->ifsc}, {"riskLevel", "Low"},
            {"pin", pin}, {"type", type}, {"balance", amount}
        });
        if (type == "Savings") { account["interestRate"] = 4; account["minBalance"] = 500; }
        else account["overdraftLimit"] = 1000;
        initialize_account(account);
        database_["accounts"].push_back(account);
        Json* saved_account = account_by_id(id);
        add_transaction(*saved_account, "DEPOSIT", amount, "Opening balance");
        save_database();
        return send_json(201, make_object({{"token", create_token(id)}, {"account", clean_account(*saved_account)}}));
    }

    Json* account_ptr = current_account(request);
    if (!account_ptr) return fail(401, "Please sign in to continue.");
    Json& account = *account_ptr;
    const long long account_id = account["id"].as_int();

    if (request.method == "GET" && request.path == "/api/dashboard/customer-stats") {
        long long registered = 0;
        for (const auto& item : database_["accounts"].as_array()) if (item["isRegisteredCustomer"].as_bool()) ++registered;
        const auto& seeds = database_["portfolioSeedCustomers"].as_array();
        Json city_counts = Json::array();
        for (const auto& branch : kBranches) {
            long long count = 0;
            for (const auto& customer : seeds) if (string_value(customer, "city") == branch.city) ++count;
            for (const auto& customer : database_["accounts"].as_array())
                if (customer["isRegisteredCustomer"].as_bool() && string_value(customer, "city") == branch.city) ++count;
            city_counts.push_back(make_object({
                {"city", branch.city}, {"state", branch.state}, {"branch", branch.branch}, {"customers", count}
            }));
        }
        return send_json(200, make_object({
            {"startingCustomers", static_cast<long long>(seeds.size())},
            {"totalCustomers", static_cast<long long>(seeds.size()) + registered},
            {"newCustomers", registered}, {"cityCustomers", city_counts}
        }));
    }

    if (request.method == "GET" && request.path == "/api/me") {
        const double interest = string_value(account, "type") == "Savings"
            ? money(number_value(account, "balance") * number_value(account, "interestRate", 4) / 100.0) : 0;
        return send_json(200, make_object({{"account", clean_account(account)}, {"interest", interest}}));
    }

    if (request.method == "GET" && request.path == "/api/statement/download") {
        std::ostringstream csv;
        csv << "Transaction ID,Date,Type,Amount (INR),Note,Balance After (INR)\r\n";
        for (const auto& tx : database_["transactions"].as_array()) {
            if (tx["accountId"].as_int() != account_id) continue;
            csv << csv_escape(string_value(tx, "id")) << ','
                << csv_escape(string_value(tx, "createdAt")) << ','
                << csv_escape(string_value(tx, "type")) << ','
                << csv_escape(tx["amount"].as_string()) << ','
                << csv_escape(string_value(tx, "note")) << ','
                << csv_escape(tx["balanceAfter"].as_string()) << "\r\n";
        }
        response.status = 200;
        response.content_type = "text/csv; charset=utf-8";
        response.headers["Content-Disposition"] = "attachment; filename=Aurora_Statement_" + std::to_string(account_id) + ".csv";
        response.body = csv.str();
        return response;
    }

    if (request.method == "GET" && request.path == "/api/transactions") {
        Json transactions = Json::array();
        for (const auto& tx : database_["transactions"].as_array()) if (tx["accountId"].as_int() == account_id) transactions.push_back(tx);
        return send_json(200, make_object({{"transactions", transactions}}));
    }

    if (request.method == "POST" && request.path == "/api/deposit") {
        const double amount = money(number_value(read_body(), "amount", std::numeric_limits<double>::quiet_NaN()));
        if (!std::isfinite(amount) || amount <= 0) return fail(400, "Invalid deposit amount.");
        account["balance"] = money(number_value(account, "balance") + amount);
        add_transaction(account, "DEPOSIT", amount, "Cash deposit");
        save_database();
        return send_json(200, make_object({{"account", clean_account(account)}}));
    }

    if (request.method == "POST" && request.path == "/api/withdraw") {
        const double amount = money(number_value(read_body(), "amount", std::numeric_limits<double>::quiet_NaN()));
        if (!std::isfinite(amount) || amount <= 0) return fail(400, "Invalid withdrawal amount.");
        if (!has_sufficient_balance(account, amount)) return fail(400, "Withdrawal violates balance limit.");
        account["balance"] = money(number_value(account, "balance") - amount);
        add_transaction(account, "WITHDRAW", amount, "Cash withdrawal");
        save_database();
        return send_json(200, make_object({{"account", clean_account(account)}}));
    }

    if (request.method == "POST" && request.path == "/api/transfer") {
        const Json body = read_body();
        const long long recipient_id = body["toAccount"].as_int(-1);
        const double amount = money(number_value(body, "amount", std::numeric_limits<double>::quiet_NaN()));
        Json* recipient = account_by_id(recipient_id);
        if (!recipient || recipient_id == account_id) return fail(400, "Invalid recipient account.");
        if (!std::isfinite(amount) || amount <= 0) return fail(400, "Transfer amount must be positive.");
        if (!has_sufficient_balance(account, amount)) return fail(400, "Insufficient balance for transfer.");

        int recent_count = 0;
        for (const auto& tx : database_["transactions"].as_array()) {
            if (tx["accountId"].as_int() != account_id) continue;
            const long long created = parse_iso_epoch(string_value(tx, "createdAt"));
            if (created > 0 && now_seconds() - created >= 0 && now_seconds() - created <= 3600) ++recent_count;
        }
        const FraudAssessment fraud = assess_transfer(amount, number_value(account, "balance"), recent_count);
        if (fraud.status == "BLOCK") {
            return send_json(403, make_object({{"error", "Transfer blocked by C++ fraud detection."}, {"fraud", fraud.to_json()}}));
        }
        account["lastFraudAssessment"] = fraud.to_json();
        account["balance"] = money(number_value(account, "balance") - amount);
        (*recipient)["balance"] = money(number_value(*recipient, "balance") + amount);
        const std::string note = trim(string_value(body, "note"));
        add_transaction(account, "TRANSFER OUT", amount, note.empty() ? "Transfer to " + string_value(*recipient, "name") : note);
        add_transaction(*recipient, "TRANSFER IN", amount, "Transfer from " + string_value(account, "name"));
        save_database();
        return send_json(200, make_object({{"account", clean_account(account)}}));
    }

    if (request.method == "POST" && request.path == "/api/transfer/own") {
        const Json body = read_body();
        const double amount = money(number_value(body, "amount", std::numeric_limits<double>::quiet_NaN()));
        const std::string direction = string_value(body, "direction");
        if (!std::isfinite(amount) || amount <= 0) return fail(400, "Invalid transfer amount.");
        if (direction == "TO_SECONDARY") {
            if (!has_sufficient_balance(account, amount)) return fail(400, "Insufficient balance in primary account.");
            account["balance"] = money(number_value(account, "balance") - amount);
            account["secondaryBalance"] = money(number_value(account, "secondaryBalance") + amount);
            add_transaction(account, "TRANSFER OUT", amount, "Transfer to Secondary Pocket");
        } else {
            if (number_value(account, "secondaryBalance") < amount) return fail(400, "Insufficient funds in secondary pocket.");
            account["secondaryBalance"] = money(number_value(account, "secondaryBalance") - amount);
            account["balance"] = money(number_value(account, "balance") + amount);
            add_transaction(account, "TRANSFER IN", amount, "Transfer from Secondary Pocket");
        }
        save_database();
        return send_json(200, make_object({{"account", clean_account(account)}}));
    }

    if (request.method == "GET" && request.path == "/api/payees") return send_json(200, make_object({{"payees", account["payees"]}}));

    if (request.method == "POST" && request.path == "/api/payees") {
        const Json body = read_body();
        const long long recipient_id = body["accountNumber"].as_int(-1);
        Json* target = account_by_id(recipient_id);
        if (!target) return fail(404, "Recipient account does not exist.");
        if (recipient_id == account_id) return fail(400, "Cannot add your own account.");
        for (const auto& payee : account["payees"].as_array())
            if (payee["accountNumber"].as_int() == recipient_id) return fail(400, "Payee already exists.");
        std::string name = trim(string_value(body, "name"));
        std::string nickname = trim(string_value(body, "nickname"));
        const std::string target_name = string_value(*target, "name");
        if (name.empty()) name = target_name;
        if (nickname.empty()) nickname = target_name.substr(0, target_name.find(' '));
        account["payees"].push_back(make_object({
            {"id", unique_id()}, {"accountNumber", recipient_id}, {"name", name},
            {"nickname", nickname}, {"addedAt", now_iso()}
        }));
        save_database();
        return send_json(201, make_object({{"message", "Payee added successfully."}, {"payees", account["payees"]}}));
    }

    if (request.method == "GET" && request.path == "/api/cards") return send_json(200, make_object({{"cards", account["cards"]}}));

    if (request.method == "POST" && request.path == "/api/cards/toggle-block") {
        const std::string type = string_value(read_body(), "cardType");
        if (type != "debit" && type != "credit") return fail(400, "Invalid card type.");
        Json& card = account["cards"][type];
        card["status"] = string_value(card, "status") == "ACTIVE" ? "BLOCKED" : "ACTIVE";
        save_database();
        return send_json(200, make_object({{"message", type + " card is now " + string_value(card, "status") + "."}, {"cards", account["cards"]}}));
    }

    if (request.method == "POST" && request.path == "/api/deposits/fd") {
        const Json body = read_body();
        const double amount = money(number_value(body, "amount", std::numeric_limits<double>::quiet_NaN()));
        const int months = static_cast<int>(number_value(body, "tenureMonths"));
        if (!std::isfinite(amount) || amount < 5000) return fail(400, "Minimum FD amount is ₹5,000.");
        if (!has_sufficient_balance(account, amount)) return fail(400, "Insufficient balance for FD.");
        const double rate = months >= 12 ? 7.1 : 6.0;
        const double maturity = money(amount + amount * (rate / 100.0) * (months / 12.0));
        account["balance"] = money(number_value(account, "balance") - amount);
        Json fd = make_object({
            {"id", timestamp_id("FD-")}, {"type", "Fixed Deposit"}, {"principal", amount},
            {"rate", rate}, {"months", months}, {"maturity", maturity}, {"createdAt", now_iso()}
        });
        account["deposits"].push_back(fd);
        add_transaction(account, "WITHDRAW", amount, "Booked FD #" + string_value(fd, "id") + " (" + std::to_string(months) + " mos @ " + std::to_string(rate) + "%)");
        save_database();
        return send_json(201, make_object({{"message", "FD booked successfully!"}, {"fd", fd}, {"account", clean_account(account)}}));
    }

    if (request.method == "POST" && request.path == "/api/deposits/rd") {
        const Json body = read_body();
        const double amount = money(number_value(body, "monthlyAmount", std::numeric_limits<double>::quiet_NaN()));
        const int months = static_cast<int>(number_value(body, "tenureMonths"));
        if (!std::isfinite(amount) || amount < 1000) return fail(400, "Minimum RD installment is ₹1,000.");
        if (!has_sufficient_balance(account, amount)) return fail(400, "Insufficient balance for RD.");
        account["balance"] = money(number_value(account, "balance") - amount);
        Json rd = make_object({
            {"id", timestamp_id("RD-")}, {"type", "Recurring Deposit"},
            {"monthlyInstallment", amount}, {"months", months}, {"rate", 6.8}, {"createdAt", now_iso()}
        });
        account["deposits"].push_back(rd);
        add_transaction(account, "WITHDRAW", amount, "Opened RD #" + string_value(rd, "id") + " (1st installment)");
        save_database();
        return send_json(201, make_object({{"message", "RD created successfully!"}, {"rd", rd}, {"account", clean_account(account)}}));
    }

    if (request.method == "POST" && request.path == "/api/invest/ppf") {
        if (!account["investments"]["ppf"].is_null()) return fail(400, "PPF account already exists.");
        const double amount = money(number_value(read_body(), "initialDeposit"));
        if (amount < 500) return fail(400, "Minimum PPF deposit is ₹500.");
        if (!has_sufficient_balance(account, amount)) return fail(400, "Insufficient balance for PPF.");
        account["balance"] = money(number_value(account, "balance") - amount);
        account["investments"]["ppf"] = make_object({
            {"ppfNumber", "PPF-IN-" + std::to_string(account_id)}, {"balance", amount},
            {"rate", 7.1}, {"openedAt", now_iso()}
        });
        add_transaction(account, "WITHDRAW", amount, "PPF Account initial contribution");
        save_database();
        return send_json(201, make_object({{"message", "PPF account activated."}, {"ppf", account["investments"]["ppf"]}, {"account", clean_account(account)}}));
    }

    if (request.method == "POST" && request.path == "/api/invest/demat") {
        if (!account["investments"]["demat"].is_null()) return fail(400, "Demat account is already active.");
        account["investments"]["demat"] = make_object({
            {"dpId", "IN300" + std::to_string(account_id)}, {"status", "ACTIVE"},
            {"linkedExchanges", Json::Array{Json("NSE"), Json("BSE")}}, {"createdAt", now_iso()}
        });
        save_database();
        return send_json(201, make_object({{"message", "Demat account created successfully."}, {"demat", account["investments"]["demat"]}}));
    }

    if (request.method == "POST" && request.path == "/api/invest/mf") {
        const Json body = read_body();
        const std::string fund = trim(string_value(body, "fundName"));
        const double amount = money(number_value(body, "amount"));
        if (fund.empty() || amount < 500) return fail(400, "Select a fund with min ₹500 SIP.");
        if (!has_sufficient_balance(account, amount)) return fail(400, "Insufficient balance for Mutual Fund.");
        account["balance"] = money(number_value(account, "balance") - amount);
        Json mf = make_object({
            {"id", timestamp_id("MF-")}, {"fundName", fund}, {"invested", amount},
            {"nav", 42.15}, {"units", std::round(amount / 42.15 * 1000.0) / 1000.0}, {"date", now_iso()}
        });
        account["investments"]["mutualFunds"].push_back(mf);
        add_transaction(account, "WITHDRAW", amount, "Mutual Fund SIP - " + fund);
        save_database();
        return send_json(201, make_object({{"message", "Mutual Fund investment recorded."}, {"mf", mf}, {"account", clean_account(account)}}));
    }

    if (request.method == "POST" && request.path == "/api/loans/apply") {
        const Json body = read_body();
        const std::string type = string_value(body, "loanType");
        const double amount = money(number_value(body, "amount"));
        const int years = static_cast<int>(number_value(body, "tenureYears"));
        if ((type != "Home Loan" && type != "Personal Loan" && type != "Vehicle Loan") || amount < 20000 || years < 1)
            return fail(400, "Invalid loan request.");
        const double rate = type == "Home Loan" ? 8.5 : 11.5;
        const double monthly_rate = rate / 1200.0;
        const int months = years * 12;
        const double factor = std::pow(1.0 + monthly_rate, months);
        const double emi = money(amount * monthly_rate * factor / (factor - 1.0));
        Json loan = make_object({
            {"id", timestamp_id("LN-")}, {"loanType", type}, {"principal", amount},
            {"rate", rate}, {"tenureYears", years}, {"emi", emi}, {"status", "APPROVED"}
        });
        account["loans"].push_back(loan);
        account["balance"] = money(number_value(account, "balance") + amount);
        add_transaction(account, "DEPOSIT", amount, type + " disbursement #" + string_value(loan, "id"));
        save_database();
        return send_json(201, make_object({{"message", type + " approved and credited!"}, {"loan", loan}, {"account", clean_account(account)}}));
    }

    if (request.method == "POST" && request.path == "/api/insurance/buy") {
        const Json body = read_body();
        const std::string plan = string_value(body, "plan");
        const double sum_assured = number_value(body, "sumAssured");
        const double premium = money(number_value(body, "premium"));
        if (premium <= 0 || number_value(account, "balance") < premium) return fail(400, "Insufficient balance for premium.");
        account["balance"] = money(number_value(account, "balance") - premium);
        Json policy = make_object({
            {"policyNumber", timestamp_id("POL-")}, {"plan", plan}, {"sumAssured", sum_assured},
            {"premium", premium}, {"issuedAt", now_iso()}
        });
        account["insurance"].push_back(policy);
        add_transaction(account, "WITHDRAW", premium, "Premium for " + plan + " #" + string_value(policy, "policyNumber"));
        save_database();
        return send_json(201, make_object({{"message", "Policy issued successfully."}, {"policy", policy}, {"account", clean_account(account)}}));
    }

    if (request.method == "POST" && request.path == "/api/bills/pay") {
        const Json body = read_body();
        const std::string category = string_value(body, "category");
        const std::string provider = trim(string_value(body, "provider"));
        const std::string identifier = trim(string_value(body, "identifier"));
        const double amount = money(number_value(body, "amount"));
        const std::vector<std::string> allowed = {"Mobile Prepaid", "Mobile Postpaid", "FASTag", "DTH", "Electricity"};
        if (std::find(allowed.begin(), allowed.end(), category) == allowed.end() || provider.empty() || identifier.empty() || amount <= 0)
            return fail(400, "Invalid bill details.");
        if (!has_sufficient_balance(account, amount)) return fail(400, "Insufficient balance for bill payment.");
        account["balance"] = money(number_value(account, "balance") - amount);
        add_transaction(account, "WITHDRAW", amount, category + " bill (" + provider + " - " + identifier + ")");
        save_database();
        return send_json(200, make_object({{"message", category + " payment of ₹" + std::to_string(amount) + " completed!"}, {"account", clean_account(account)}}));
    }

    if (request.method == "POST" && request.path == "/api/cheques/stop") {
        const Json body = read_body();
        const std::string cheque = body["chequeNumber"].as_string();
        if (!digits(cheque, 6)) return fail(400, "Enter a valid 6-digit cheque number.");
        const std::string reason = trim(string_value(body, "reason", "Reported lost"));
        account["stoppedCheques"].push_back(make_object({
            {"chequeNumber", cheque}, {"reason", reason.empty() ? "Reported lost" : reason}, {"date", now_iso()}
        }));
        save_database();
        return send_json(200, make_object({{"message", "Cheque #" + cheque + " stopped successfully."}, {"stoppedCheques", account["stoppedCheques"]}}));
    }

    if (request.method == "POST" && request.path == "/api/fraud/report") {
        const Json body = read_body();
        const std::string description = trim(string_value(body, "description"));
        if (description.empty()) return fail(400, "Please describe the incident.");
        Json ticket = make_object({
            {"id", timestamp_id("INC-")}, {"txId", string_value(body, "txId", "N/A")},
            {"description", description}, {"reportedAt", now_iso()}, {"status", "UNDER REVIEW"}
        });
        account["fraudReports"].push_back(ticket);
        save_database();
        return send_json(201, make_object({
            {"message", "Incident recorded under ticket #" + string_value(ticket, "id") + ". Fraud desk notified."},
            {"ticket", ticket}
        }));
    }

    if (request.method == "POST" && request.path == "/api/pin/reset") {
        const Json body = read_body();
        if (string_value(account, "pin") != body["currentPin"].as_string()) return fail(400, "Current PIN is incorrect.");
        const std::string new_pin = body["newPin"].as_string();
        if (!digits(new_pin, 4)) return fail(400, "New PIN must be 4 digits.");
        account["pin"] = new_pin;
        save_database();
        return send_json(200, make_object({{"message", "PIN updated successfully."}}));
    }

    return fail(404, "Endpoint not found.");
}

}
