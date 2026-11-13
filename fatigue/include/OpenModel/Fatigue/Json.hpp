#pragma once

#include <cstddef>
#include <string>
#include <vector>

/**
 * @file Json.hpp
 * @brief A small, strict JSON reader for the fatigue input files (materials, spectra).
 *
 * Supports the RFC 8259 grammar: objects, arrays, strings (with escapes, including \\uXXXX and surrogate
 * pairs, encoded as UTF-8), finite numbers, true, false and null. Duplicate keys, trailing text, control
 * characters in strings, non-finite numbers and nesting deeper than 128 levels are errors. No writer is
 * provided; the reports are written by FatigueReportWriter.
 */

namespace OpenModel {
namespace Fatigue {
namespace Json {

/** @brief One parsed JSON value. Accessors other than type() require the matching type. */
class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;

    Type type() const noexcept { return type_; }
    bool isNull() const noexcept { return type_ == Type::Null; }
    bool isBool() const noexcept { return type_ == Type::Bool; }
    bool isNumber() const noexcept { return type_ == Type::Number; }
    bool isString() const noexcept { return type_ == Type::String; }
    bool isArray() const noexcept { return type_ == Type::Array; }
    bool isObject() const noexcept { return type_ == Type::Object; }

    bool asBool() const noexcept { return bool_; }
    double asNumber() const noexcept { return number_; }
    const std::string& asString() const noexcept { return string_; }
    const std::vector<Value>& asArray() const noexcept { return items_; }

    /** Member of an object, or nullptr when this is not an object or the key is absent. */
    const Value* find(const std::string& key) const noexcept;
    /** Object member names in file order (empty for other types). */
    const std::vector<std::string>& keys() const noexcept { return keys_; }

    static Value makeNull();
    static Value makeBool(bool value);
    static Value makeNumber(double value);
    static Value makeString(std::string value);
    static Value makeArray(std::vector<Value> items);
    static Value makeObject(std::vector<std::string> keys, std::vector<Value> values);

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Value> items_;   // array elements, or object member values (parallel to keys_)
    std::vector<std::string> keys_;
};

/** @brief Result of parse(): ok and value, or ok false and an error with line and column. */
struct ParseResult {
    bool ok = false;
    Value value;
    std::string error;
    explicit operator bool() const noexcept { return ok; }
};

/** @brief Parses a complete JSON document. */
ParseResult parse(const std::string& text);

} // namespace Json
} // namespace Fatigue
} // namespace OpenModel
