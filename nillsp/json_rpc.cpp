#include "json_rpc.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>

namespace ny::json {
namespace {

struct Parser {
    std::string_view in;
    std::size_t i = 0;
    ParseError* err = nullptr;

    void set_error(std::string msg) {
        if (err && err->message.empty()) {
            err->message = std::move(msg);
            err->offset = i;
        }
    }

    bool eof() const { return i >= in.size(); }
    char peek() const { return eof() ? '\0' : in[i]; }

    void skip_ws() {
        while (!eof()) {
            const char c = in[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                i++;
            } else {
                break;
            }
        }
    }

    bool consume(char c) {
        if (peek() != c) return false;
        i++;
        return true;
    }

    bool consume_literal(std::string_view lit) {
        if (in.substr(i, lit.size()) != lit) return false;
        i += lit.size();
        return true;
    }

    std::optional<Value> parse_value() {
        skip_ws();
        if (eof()) {
            set_error("Unexpected end of input");
            return std::nullopt;
        }
        const char c = peek();
        if (c == 'n') return parse_null();
        if (c == 't' || c == 'f') return parse_bool();
        if (c == '"') return parse_string();
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
        if (c == '[') return parse_array();
        if (c == '{') return parse_object();
        set_error("Unexpected character");
        return std::nullopt;
    }

    std::optional<Value> parse_null() {
        if (!consume_literal("null")) {
            set_error("Invalid literal");
            return std::nullopt;
        }
        return Value(nullptr);
    }

    std::optional<Value> parse_bool() {
        if (consume_literal("true")) return Value(true);
        if (consume_literal("false")) return Value(false);
        set_error("Invalid literal");
        return std::nullopt;
    }

    static bool is_hex(char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    static int hex_value(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return 0;
    }

    static void append_utf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    std::optional<Value> parse_string() {
        if (!consume('"')) {
            set_error("Expected string");
            return std::nullopt;
        }
        std::string out;
        while (!eof()) {
            const char c = in[i++];
            if (c == '"') {
                return Value(std::move(out));
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                set_error("Invalid control character in string");
                return std::nullopt;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (eof()) {
                set_error("Unterminated escape sequence");
                return std::nullopt;
            }
            const char e = in[i++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (i + 4 > in.size()) {
                        set_error("Invalid unicode escape");
                        return std::nullopt;
                    }
                    uint32_t cp = 0;
                    for (int k = 0; k < 4; k++) {
                        const char h = in[i++];
                        if (!is_hex(h)) {
                            set_error("Invalid unicode escape");
                            return std::nullopt;
                        }
                        cp = (cp << 4) | static_cast<uint32_t>(hex_value(h));
                    }
                    if (cp >= 0xd800 && cp <= 0xdbff) {
                        if (i + 6 > in.size() || in[i] != '\\' || in[i + 1] != 'u') {
                            set_error("Missing low unicode surrogate");
                            return std::nullopt;
                        }
                        i += 2;
                        uint32_t low = 0;
                        for (int k = 0; k < 4; ++k) {
                            const auto h = in[i++];
                            if (!is_hex(h)) { set_error("Invalid unicode escape"); return std::nullopt; }
                            low = (low << 4) | static_cast<uint32_t>(hex_value(h));
                        }
                        if (low < 0xdc00 || low > 0xdfff) {
                            set_error("Invalid low unicode surrogate");
                            return std::nullopt;
                        }
                        cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                    } else if (cp >= 0xdc00 && cp <= 0xdfff) {
                        set_error("Unexpected low unicode surrogate");
                        return std::nullopt;
                    }
                    append_utf8(out, cp);
                } break;
                default:
                    set_error("Invalid escape sequence");
                    return std::nullopt;
            }
        }
        set_error("Unterminated string");
        return std::nullopt;
    }

    std::optional<Value> parse_number() {
        const std::size_t start = i;
        if (consume('-')) {
            if (eof()) {
                set_error("Invalid number");
                return std::nullopt;
            }
        }
        if (peek() == '0') {
            i++;
        } else {
            if (!(peek() >= '1' && peek() <= '9')) {
                set_error("Invalid number");
                return std::nullopt;
            }
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) i++;
        }
        if (consume('.')) {
            if (eof() || !std::isdigit(static_cast<unsigned char>(peek()))) {
                set_error("Invalid number");
                return std::nullopt;
            }
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) i++;
        }
        if (peek() == 'e' || peek() == 'E') {
            i++;
            if (peek() == '+' || peek() == '-') i++;
            if (eof() || !std::isdigit(static_cast<unsigned char>(peek()))) {
                set_error("Invalid number");
                return std::nullopt;
            }
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) i++;
        }
        const auto sv = in.substr(start, i - start);
        double d = 0.0;
        {
            std::string tmp(sv);
            char* endp = nullptr;
            d = std::strtod(tmp.c_str(), &endp);
            if (!endp || *endp != '\0') {
                set_error("Invalid number");
                return std::nullopt;
            }
        }
        return Value(d);
    }

    std::optional<Value> parse_array() {
        if (!consume('[')) {
            set_error("Expected array");
            return std::nullopt;
        }
        Value::Array arr;
        skip_ws();
        if (consume(']')) return Value(std::move(arr));
        while (true) {
            auto v = parse_value();
            if (!v) return std::nullopt;
            arr.push_back(std::move(*v));
            skip_ws();
            if (consume(']')) break;
            if (!consume(',')) {
                set_error("Expected ',' in array");
                return std::nullopt;
            }
        }
        return Value(std::move(arr));
    }

    std::optional<Value> parse_object() {
        if (!consume('{')) {
            set_error("Expected object");
            return std::nullopt;
        }
        Value::Object obj;
        skip_ws();
        if (consume('}')) return Value(std::move(obj));
        while (true) {
            skip_ws();
            auto key = parse_string();
            if (!key || !key->is_string()) return std::nullopt;
            skip_ws();
            if (!consume(':')) {
                set_error("Expected ':' in object");
                return std::nullopt;
            }
            auto val = parse_value();
            if (!val) return std::nullopt;
            obj[key->as_string()] = std::move(*val);
            skip_ws();
            if (consume('}')) break;
            if (!consume(',')) {
                set_error("Expected ',' in object");
                return std::nullopt;
            }
        }
        return Value(std::move(obj));
    }
};

static void escape_string(std::string& out, std::string_view s) {
    out.push_back('"');
    for (unsigned char uc : s) {
        const char c = static_cast<char>(uc);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (uc < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[(uc >> 4) & 0x0F]);
                    out.push_back(hex[uc & 0x0F]);
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    out.push_back('"');
}

static void stringify_into(std::string& out, const Value& v);

static void stringify_object(std::string& out, const Value::Object& obj) {
    out.push_back('{');
    bool first = true;
    for (const auto& [k, val] : obj) {
        if (!first) out.push_back(',');
        first = false;
        escape_string(out, k);
        out.push_back(':');
        stringify_into(out, val);
    }
    out.push_back('}');
}

static void stringify_array(std::string& out, const Value::Array& arr) {
    out.push_back('[');
    bool first = true;
    for (const auto& it : arr) {
        if (!first) out.push_back(',');
        first = false;
        stringify_into(out, it);
    }
    out.push_back(']');
}

static void stringify_into(std::string& out, const Value& v) {
    switch (v.type()) {
        case Value::Type::Null:
            out += "null";
            return;
        case Value::Type::Bool:
            out += v.as_bool() ? "true" : "false";
            return;
        case Value::Type::Number: {
            std::ostringstream oss;
            oss.precision(17);
            oss << v.as_number();
            out += oss.str();
            return;
        }
        case Value::Type::String:
            escape_string(out, v.as_string());
            return;
        case Value::Type::Array:
            stringify_array(out, v.as_array());
            return;
        case Value::Type::Object:
            stringify_object(out, v.as_object());
            return;
    }
}

}

Value::Value() : type_(Type::Null), data_(nullptr) {}
Value::Value(std::nullptr_t) : type_(Type::Null), data_(nullptr) {}
Value::Value(bool b) : type_(Type::Bool), data_(b) {}
Value::Value(double n) : type_(Type::Number), data_(n) {}
Value::Value(int64_t n) : type_(Type::Number), data_(static_cast<double>(n)) {}
Value::Value(std::string s) : type_(Type::String), data_(std::move(s)) {}
Value::Value(const char* s) : type_(Type::String), data_(std::string(s ? s : "")) {}
Value::Value(Array a) : type_(Type::Array), data_(std::move(a)) {}
Value::Value(Object o) : type_(Type::Object), data_(std::move(o)) {}

Value::Type Value::type() const { return type_; }
bool Value::is_null() const { return type_ == Type::Null; }
bool Value::is_bool() const { return type_ == Type::Bool; }
bool Value::is_number() const { return type_ == Type::Number; }
bool Value::is_string() const { return type_ == Type::String; }
bool Value::is_array() const { return type_ == Type::Array; }
bool Value::is_object() const { return type_ == Type::Object; }

bool Value::as_bool(bool default_value) const {
    if (!is_bool()) return default_value;
    return std::get<bool>(data_);
}

double Value::as_number(double default_value) const {
    if (!is_number()) return default_value;
    return std::get<double>(data_);
}

int64_t Value::as_i64(int64_t default_value) const {
    if (!is_number()) return default_value;
    const double d = std::get<double>(data_);
    if (!std::isfinite(d)) return default_value;
    if (d < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
        d > static_cast<double>(std::numeric_limits<int64_t>::max())) {
        return default_value;
    }
    return static_cast<int64_t>(d);
}

const std::string& Value::as_string() const {
    static const std::string empty;
    if (!is_string()) return empty;
    return std::get<std::string>(data_);
}

const Value::Array& Value::as_array() const {
    static const Array empty;
    if (!is_array()) return empty;
    return std::get<Array>(data_);
}

const Value::Object& Value::as_object() const {
    static const Object empty;
    if (!is_object()) return empty;
    return std::get<Object>(data_);
}

Value::Array& Value::mut_array() {
    if (!is_array()) {
        type_ = Type::Array;
        data_ = Array{};
    }
    return std::get<Array>(data_);
}

Value::Object& Value::mut_object() {
    if (!is_object()) {
        type_ = Type::Object;
        data_ = Object{};
    }
    return std::get<Object>(data_);
}

std::string& Value::mut_string() {
    if (!is_string()) {
        type_ = Type::String;
        data_ = std::string{};
    }
    return std::get<std::string>(data_);
}

const Value* Value::get(std::string_view key) const {
    if (!is_object()) return nullptr;
    const auto& obj = std::get<Object>(data_);
    auto it = obj.find(std::string(key));
    if (it == obj.end()) return nullptr;
    return &it->second;
}

Value* Value::get_mut(std::string_view key) {
    if (!is_object()) return nullptr;
    auto& obj = std::get<Object>(data_);
    auto it = obj.find(std::string(key));
    if (it == obj.end()) return nullptr;
    return &it->second;
}

std::optional<Value> parse(std::string_view input, ParseError* err) {
    if (err) *err = {};
    Parser p{input, 0, err};
    auto v = p.parse_value();
    if (!v) return std::nullopt;
    p.skip_ws();
    if (!p.eof()) {
        p.set_error("Trailing characters");
        return std::nullopt;
    }
    return v;
}

std::string stringify(const Value& v) {
    std::string out;
    stringify_into(out, v);
    return out;
}

Value object(std::initializer_list<std::pair<std::string, Value>> fields) {
    Value::Object o;
    for (const auto& kv : fields) o[kv.first] = kv.second;
    return Value(std::move(o));
}

Value array(std::initializer_list<Value> items) {
    Value::Array a;
    a.reserve(items.size());
    for (const auto& it : items) a.push_back(it);
    return Value(std::move(a));
}

std::optional<std::string> get_string(const Value& v, std::string_view key) {
    const Value* p = get_value(v, key);
    if (!p || !p->is_string()) return std::nullopt;
    return p->as_string();
}

std::optional<int64_t> get_i64(const Value& v, std::string_view key) {
    const Value* p = get_value(v, key);
    if (!p || !p->is_number()) return std::nullopt;
    return p->as_i64();
}

const Value* get_value(const Value& v, std::string_view key) {
    if (!v.is_object()) return nullptr;
    return v.get(key);
}

}

namespace ny::rpc {
namespace {

static ny::json::Value make_error_obj(const Error& e) {
    ny::json::Value::Object obj;
    obj["code"] = ny::json::Value(static_cast<int64_t>(e.code));
    obj["message"] = ny::json::Value(e.message);
    if (e.data) obj["data"] = *e.data;
    return ny::json::Value(std::move(obj));
}

static bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); i++) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

}

std::optional<Id> Id::from_json(const ny::json::Value& v) {
    if (v.is_string()) return Id{v.as_string()};
    if (v.is_number()) return Id{v.as_i64()};
    if (v.is_null()) return Id{std::monostate{}};
    return std::nullopt;
}

ny::json::Value Id::to_json() const {
    if (std::holds_alternative<int64_t>(value)) return ny::json::Value(std::get<int64_t>(value));
    if (std::holds_alternative<std::string>(value)) return ny::json::Value(std::get<std::string>(value));
    return ny::json::Value(nullptr);
}

bool Id::is_null() const { return std::holds_alternative<std::monostate>(value); }

JsonRpcConnection::JsonRpcConnection(std::istream& in, std::ostream& out) : in_(in), out_(out) {}

bool JsonRpcConnection::read_framed_body(std::string& out_body, std::string& out_error) {
    out_error.clear();
    std::string line;
    int64_t content_length = -1;
    bool saw_any_header = false;

    while (std::getline(in_, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        saw_any_header = true;

        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string_view key = std::string_view(line).substr(0, colon);
        std::string_view val = std::string_view(line).substr(colon + 1);
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.remove_prefix(1);
        if (iequals(key, "Content-Length")) {
            int64_t n = 0;
            auto* begin = val.data();
            auto* end = val.data() + val.size();
            auto res = std::from_chars(begin, end, n);
            if (res.ec != std::errc() || n < 0) {
                out_error = "Invalid Content-Length";
                return false;
            }
            content_length = n;
        }
    }

    if (!saw_any_header && in_.eof()) return false;
    if (content_length < 0) {
        out_error = "Missing Content-Length";
        return false;
    }
    out_body.assign(static_cast<std::size_t>(content_length), '\0');
    in_.read(out_body.data(), static_cast<std::streamsize>(content_length));
    if (in_.gcount() != static_cast<std::streamsize>(content_length)) {
        out_error = "Unexpected EOF while reading body";
        return false;
    }
    return true;
}

bool JsonRpcConnection::read_message(ny::json::Value& out_message, std::string& out_error) {
    std::string body;
    if (!read_framed_body(body, out_error)) {
        if (in_.eof()) return false;
        return false;
    }
    ny::json::ParseError perr;
    auto parsed = ny::json::parse(body, &perr);
    if (!parsed) {
        std::ostringstream oss;
        oss << "JSON parse error at " << perr.offset;
        if (!perr.message.empty()) oss << ": " << perr.message;
        out_error = oss.str();
        out_message = ny::json::Value(nullptr);
        return true;
    }
    out_message = std::move(*parsed);
    out_error.clear();
    return true;
}

void JsonRpcConnection::send_message(const ny::json::Value& message) {
    const std::string body = ny::json::stringify(message);
    std::lock_guard<std::mutex> lock(write_mutex_);
    out_ << "Content-Length: " << body.size() << "\r\n";
    out_ << "Content-Type: application/vscode-jsonrpc; charset=utf-8\r\n";
    out_ << "\r\n";
    out_.write(body.data(), static_cast<std::streamsize>(body.size()));
    out_.flush();
}

void JsonRpcConnection::send_response(const Id& id, const ny::json::Value& result) {
    send_message(ny::json::object({{"jsonrpc", "2.0"}, {"id", id.to_json()}, {"result", result}}));
}

void JsonRpcConnection::send_error(const Id& id, const Error& error) {
    send_message(ny::json::object({{"jsonrpc", "2.0"}, {"id", id.to_json()}, {"error", make_error_obj(error)}}));
}

void JsonRpcConnection::send_notification(const std::string& method, const ny::json::Value& params) {
    send_message(ny::json::object({{"jsonrpc", "2.0"}, {"method", method}, {"params", params}}));
}

}
