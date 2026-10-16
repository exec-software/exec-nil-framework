#pragma once

#include <cstdint>
#include <istream>
#include <map>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ny::json {

class Value {
public:
    using Object = std::map<std::string, Value>;
    using Array = std::vector<Value>;

    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    Value();
    Value(std::nullptr_t);
    Value(bool b);
    Value(double n);
    Value(int64_t n);
    Value(std::string s);
    Value(const char* s);
    Value(Array a);
    Value(Object o);

    Type type() const;

    bool is_null() const;
    bool is_bool() const;
    bool is_number() const;
    bool is_string() const;
    bool is_array() const;
    bool is_object() const;

    bool as_bool(bool default_value = false) const;
    double as_number(double default_value = 0.0) const;
    int64_t as_i64(int64_t default_value = 0) const;
    const std::string& as_string() const;
    const Array& as_array() const;
    const Object& as_object() const;

    Array& mut_array();
    Object& mut_object();
    std::string& mut_string();

    const Value* get(std::string_view key) const;
    Value* get_mut(std::string_view key);

private:
    Type type_ = Type::Null;
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> data_;
};

struct ParseError {
    std::string message;
    std::size_t offset = 0;
};

std::optional<Value> parse(std::string_view input, ParseError* err);
std::string stringify(const Value& v);

Value object(std::initializer_list<std::pair<std::string, Value>> fields);
Value array(std::initializer_list<Value> items);

std::optional<std::string> get_string(const Value& v, std::string_view key);
std::optional<int64_t> get_i64(const Value& v, std::string_view key);
const Value* get_value(const Value& v, std::string_view key);

}

namespace ny::rpc {

struct Id {
    std::variant<std::monostate, int64_t, std::string> value;

    static std::optional<Id> from_json(const ny::json::Value& v);
    ny::json::Value to_json() const;
    bool is_null() const;
};

struct Error {
    int code = -32603;
    std::string message;
    std::optional<ny::json::Value> data;
};

class JsonRpcConnection {
public:
    JsonRpcConnection(std::istream& in, std::ostream& out);

    bool read_message(ny::json::Value& out_message, std::string& out_error);

    void send_message(const ny::json::Value& message);
    void send_response(const Id& id, const ny::json::Value& result);
    void send_error(const Id& id, const Error& error);
    void send_notification(const std::string& method, const ny::json::Value& params);

private:
    bool read_framed_body(std::string& out_body, std::string& out_error);

    std::istream& in_;
    std::ostream& out_;
    std::mutex write_mutex_;
};

}
