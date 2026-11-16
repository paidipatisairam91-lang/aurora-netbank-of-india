#pragma once


#include <cctype>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace aurora {

class Json {
public:
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    Json() : value_(nullptr) {}
    Json(std::nullptr_t) : value_(nullptr) {}
    Json(bool value) : value_(value) {}
    Json(int value) : value_(static_cast<double>(value)) {}
    Json(long value) : value_(static_cast<double>(value)) {}
    Json(long long value) : value_(static_cast<double>(value)) {}
    Json(unsigned value) : value_(static_cast<double>(value)) {}
    Json(double value) : value_(std::isfinite(value) ? value : 0.0) {}
    Json(const char* value) : value_(std::string(value ? value : "")) {}
    Json(const std::string& value) : value_(value) {}
    Json(std::string&& value) : value_(std::move(value)) {}
    Json(const Array& value) : value_(value) {}
    Json(Array&& value) : value_(std::move(value)) {}
    Json(const Object& value) : value_(value) {}
    Json(Object&& value) : value_(std::move(value)) {}

    static Json object() { return Object{}; }
    static Json array() { return Array{}; }

    bool is_null() const { return std::holds_alternative<std::nullptr_t>(value_); }
    bool is_bool() const { return std::holds_alternative<bool>(value_); }
    bool is_number() const { return std::holds_alternative<double>(value_); }
    bool is_string() const { return std::holds_alternative<std::string>(value_); }
    bool is_array() const { return std::holds_alternative<Array>(value_); }
    bool is_object() const { return std::holds_alternative<Object>(value_); }

    bool as_bool(bool fallback = false) const {
        if (is_bool()) return std::get<bool>(value_);
        if (is_number()) return as_number() != 0;
        if (is_string()) return !as_string().empty();
        return fallback;
    }

    double as_number(double fallback = 0.0) const {
        if (is_number()) return std::get<double>(value_);
        if (is_bool()) return std::get<bool>(value_) ? 1.0 : 0.0;
        if (is_string()) {
            try { size_t used = 0; double n = std::stod(as_string(), &used); return used ? n : fallback; }
            catch (...) { return fallback; }
        }
        return fallback;
    }

    long long as_int(long long fallback = 0) const {
        const double number = as_number(static_cast<double>(fallback));
        if (!std::isfinite(number)) return fallback;
        return static_cast<long long>(number);
    }

    std::string as_string(const std::string& fallback = "") const {
        if (is_string()) return std::get<std::string>(value_);
        if (is_number()) {
            std::ostringstream out;
            out << std::setprecision(15) << std::get<double>(value_);
            return out.str();
        }
        if (is_bool()) return std::get<bool>(value_) ? "true" : "false";
        return fallback;
    }

    Array& as_array() {
        if (!is_array()) value_ = Array{};
        return std::get<Array>(value_);
    }
    const Array& as_array() const {
        static const Array empty;
        return is_array() ? std::get<Array>(value_) : empty;
    }
    Object& as_object() {
        if (!is_object()) value_ = Object{};
        return std::get<Object>(value_);
    }
    const Object& as_object() const {
        static const Object empty;
        return is_object() ? std::get<Object>(value_) : empty;
    }

    Json& operator[](const std::string& key) { return as_object()[key]; }
    const Json& operator[](const std::string& key) const {
        static const Json nullValue;
        if (!is_object()) return nullValue;
        const auto it = as_object().find(key);
        return it == as_object().end() ? nullValue : it->second;
    }
    Json& operator[](size_t index) {
        auto& values = as_array();
        if (index >= values.size()) values.resize(index + 1);
        return values[index];
    }
    const Json& operator[](size_t index) const {
        static const Json nullValue;
        const auto& values = as_array();
        return index < values.size() ? values[index] : nullValue;
    }

    bool contains(const std::string& key) const {
        return is_object() && as_object().find(key) != as_object().end();
    }
    bool empty() const {
        if (is_array()) return as_array().empty();
        if (is_object()) return as_object().empty();
        if (is_string()) return as_string().empty();
        return is_null();
    }
    size_t size() const {
        if (is_array()) return as_array().size();
        if (is_object()) return as_object().size();
        if (is_string()) return as_string().size();
        return 0;
    }
    void push_back(const Json& value) { as_array().push_back(value); }
    void push_back(Json&& value) { as_array().push_back(std::move(value)); }
    void erase(const std::string& key) { if (is_object()) as_object().erase(key); }

    static Json parse(const std::string& input) {
        Parser parser(input);
        Json result = parser.parse_value();
        parser.skip_space();
        if (!parser.done()) throw std::runtime_error("Unexpected characters after JSON value");
        return result;
    }

    std::string dump(int indent = -1) const {
        std::ostringstream output;
        write_value(output, *this, indent, 0);
        return output.str();
    }

private:
    Storage value_;

    class Parser {
    public:
        explicit Parser(const std::string& source) : source_(source) {}
        bool done() const { return position_ >= source_.size(); }
        void skip_space() {
            while (!done() && std::isspace(static_cast<unsigned char>(source_[position_]))) ++position_;
        }

        Json parse_value() {
            skip_space();
            if (done()) fail("Unexpected end of JSON");
            const char ch = source_[position_];
            if (ch == '{') return parse_object();
            if (ch == '[') return parse_array();
            if (ch == '"') return Json(parse_string());
            if (ch == 't') { expect_word("true"); return Json(true); }
            if (ch == 'f') { expect_word("false"); return Json(false); }
            if (ch == 'n') { expect_word("null"); return Json(nullptr); }
            if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) return parse_number();
            fail("Invalid JSON value");
            return Json();
        }

    private:
        const std::string& source_;
        size_t position_ = 0;

        [[noreturn]] void fail(const std::string& message) const {
            throw std::runtime_error(message + " at byte " + std::to_string(position_));
        }
        char take() {
            if (done()) fail("Unexpected end of JSON");
            return source_[position_++];
        }
        bool consume(char expected) {
            if (!done() && source_[position_] == expected) { ++position_; return true; }
            return false;
        }
        void expect(char expected) {
            if (!consume(expected)) fail(std::string("Expected '") + expected + "'");
        }
        void expect_word(const char* word) {
            while (*word) if (take() != *word++) fail("Invalid JSON literal");
        }
        Json parse_object() {
            expect('{');
            Json result = Json::object();
            skip_space();
            if (consume('}')) return result;
            while (true) {
                skip_space();
                if (done() || source_[position_] != '"') fail("Object key must be a string");
                std::string key = parse_string();
                skip_space(); expect(':');
                result[key] = parse_value();
                skip_space();
                if (consume('}')) break;
                expect(',');
            }
            return result;
        }
        Json parse_array() {
            expect('[');
            Json result = Json::array();
            skip_space();
            if (consume(']')) return result;
            while (true) {
                result.push_back(parse_value());
                skip_space();
                if (consume(']')) break;
                expect(',');
            }
            return result;
        }
        static void append_utf8(std::string& output, uint32_t codepoint) {
            if (codepoint <= 0x7F) output.push_back(static_cast<char>(codepoint));
            else if (codepoint <= 0x7FF) {
                output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
                output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            } else if (codepoint <= 0xFFFF) {
                output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
                output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            } else {
                output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
                output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
                output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
        }
        uint32_t parse_hex4() {
            uint32_t value = 0;
            for (int i = 0; i < 4; ++i) {
                const char ch = take();
                value <<= 4;
                if (ch >= '0' && ch <= '9') value |= static_cast<uint32_t>(ch - '0');
                else if (ch >= 'a' && ch <= 'f') value |= static_cast<uint32_t>(ch - 'a' + 10);
                else if (ch >= 'A' && ch <= 'F') value |= static_cast<uint32_t>(ch - 'A' + 10);
                else fail("Invalid unicode escape");
            }
            return value;
        }
        std::string parse_string() {
            expect('"');
            std::string output;
            while (!done()) {
                const unsigned char ch = static_cast<unsigned char>(take());
                if (ch == '"') return output;
                if (ch < 0x20) fail("Control character in JSON string");
                if (ch != '\\') { output.push_back(static_cast<char>(ch)); continue; }
                const char escaped = take();
                switch (escaped) {
                    case '"': output.push_back('"'); break;
                    case '\\': output.push_back('\\'); break;
                    case '/': output.push_back('/'); break;
                    case 'b': output.push_back('\b'); break;
                    case 'f': output.push_back('\f'); break;
                    case 'n': output.push_back('\n'); break;
                    case 'r': output.push_back('\r'); break;
                    case 't': output.push_back('\t'); break;
                    case 'u': {
                        uint32_t cp = parse_hex4();
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            if (take() != '\\' || take() != 'u') fail("Missing low surrogate");
                            const uint32_t low = parse_hex4();
                            if (low < 0xDC00 || low > 0xDFFF) fail("Invalid low surrogate");
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        }
                        append_utf8(output, cp);
                        break;
                    }
                    default: fail("Invalid JSON escape");
                }
            }
            fail("Unterminated JSON string");
            return output;
        }
        Json parse_number() {
            const size_t start = position_;
            consume('-');
            if (consume('0')) {
                if (!done() && std::isdigit(static_cast<unsigned char>(source_[position_]))) fail("Leading zero in number");
            } else {
                if (done() || !std::isdigit(static_cast<unsigned char>(source_[position_]))) fail("Invalid number");
                while (!done() && std::isdigit(static_cast<unsigned char>(source_[position_]))) ++position_;
            }
            if (consume('.')) {
                if (done() || !std::isdigit(static_cast<unsigned char>(source_[position_]))) fail("Invalid fraction");
                while (!done() && std::isdigit(static_cast<unsigned char>(source_[position_]))) ++position_;
            }
            if (consume('e') || consume('E')) {
                if (!done() && (source_[position_] == '+' || source_[position_] == '-')) ++position_;
                if (done() || !std::isdigit(static_cast<unsigned char>(source_[position_]))) fail("Invalid exponent");
                while (!done() && std::isdigit(static_cast<unsigned char>(source_[position_]))) ++position_;
            }
            try { return Json(std::stod(source_.substr(start, position_ - start))); }
            catch (...) { fail("Number is out of range"); }
            return Json();
        }
    };

    static void write_string(std::ostringstream& output, const std::string& value) {
        output << '"';
        for (unsigned char ch : value) {
            switch (ch) {
                case '"': output << "\\\""; break;
                case '\\': output << "\\\\"; break;
                case '\b': output << "\\b"; break;
                case '\f': output << "\\f"; break;
                case '\n': output << "\\n"; break;
                case '\r': output << "\\r"; break;
                case '\t': output << "\\t"; break;
                default:
                    if (ch < 0x20) output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch) << std::dec;
                    else output << static_cast<char>(ch);
            }
        }
        output << '"';
    }

    static void write_indent(std::ostringstream& output, int indent, int depth) {
        if (indent >= 0) output << '\n' << std::string(static_cast<size_t>(indent * depth), ' ');
    }

    static void write_value(std::ostringstream& output, const Json& value, int indent, int depth) {
        if (value.is_null()) { output << "null"; return; }
        if (value.is_bool()) { output << (value.as_bool() ? "true" : "false"); return; }
        if (value.is_number()) {
            const double n = value.as_number();
            if (std::floor(n) == n && std::abs(n) < 9.0e15) output << static_cast<long long>(n);
            else output << std::setprecision(15) << n;
            return;
        }
        if (value.is_string()) { write_string(output, value.as_string()); return; }
        if (value.is_array()) {
            output << '[';
            const auto& items = value.as_array();
            for (size_t i = 0; i < items.size(); ++i) {
                if (i) output << ',';
                if (indent >= 0) write_indent(output, indent, depth + 1);
                write_value(output, items[i], indent, depth + 1);
            }
            if (!items.empty() && indent >= 0) write_indent(output, indent, depth);
            output << ']';
            return;
        }
        output << '{';
        const auto& members = value.as_object();
        size_t index = 0;
        for (const auto& member : members) {
            if (index++) output << ',';
            if (indent >= 0) write_indent(output, indent, depth + 1);
            write_string(output, member.first);
            output << (indent >= 0 ? ": " : ":");
            write_value(output, member.second, indent, depth + 1);
        }
        if (!members.empty() && indent >= 0) write_indent(output, indent, depth);
        output << '}';
    }
};

inline std::string string_value(const Json& value, const std::string& key, const std::string& fallback = "") {
    return value[key].as_string(fallback);
}

inline double number_value(const Json& value, const std::string& key, double fallback = 0.0) {
    return value[key].as_number(fallback);
}

inline bool has_value(const Json& value, const std::string& key) {
    return value.contains(key) && !value[key].is_null();
}

}
