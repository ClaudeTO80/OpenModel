#include <OpenModel/Fatigue/Json.hpp>

#include <cmath>
#include <cstdint>
#include <locale>
#include <sstream>
#include <utility>

/**
 * @file Json.cpp
 * @brief Recursive-descent parser behind OpenModel::Fatigue::Json::parse().
 */

namespace OpenModel {
namespace Fatigue {
namespace Json {

const Value* Value::find(const std::string& key) const noexcept {
    if (type_ != Type::Object) return nullptr;
    for (std::size_t i = 0; i < keys_.size(); ++i) {
        if (keys_[i] == key) return &items_[i];
    }
    return nullptr;
}

Value Value::makeNull() { return Value(); }

Value Value::makeBool(bool value) {
    Value v;
    v.type_ = Type::Bool;
    v.bool_ = value;
    return v;
}

Value Value::makeNumber(double value) {
    Value v;
    v.type_ = Type::Number;
    v.number_ = value;
    return v;
}

Value Value::makeString(std::string value) {
    Value v;
    v.type_ = Type::String;
    v.string_ = std::move(value);
    return v;
}

Value Value::makeArray(std::vector<Value> items) {
    Value v;
    v.type_ = Type::Array;
    v.items_ = std::move(items);
    return v;
}

Value Value::makeObject(std::vector<std::string> keys, std::vector<Value> values) {
    Value v;
    v.type_ = Type::Object;
    v.keys_ = std::move(keys);
    v.items_ = std::move(values);
    return v;
}

namespace {

constexpr int kMaxDepth = 128;

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    ParseResult run() {
        ParseResult result;
        skipSpace();
        Value value;
        if (!parseValue(value, 0)) {
            result.error = error_;
            return result;
        }
        skipSpace();
        if (pos_ != text_.size()) {
            fail("unexpected text after the JSON value");
            result.error = error_;
            return result;
        }
        result.ok = true;
        result.value = std::move(value);
        return result;
    }

private:
    const std::string& text_;
    std::size_t pos_ = 0;
    std::string error_;

    bool fail(const std::string& message) {
        if (error_.empty()) {
            std::size_t line = 1, column = 1;
            for (std::size_t i = 0; i < pos_ && i < text_.size(); ++i) {
                if (text_[i] == '\n') { ++line; column = 1; }
                else ++column;
            }
            error_ = "line " + std::to_string(line) + ", column " + std::to_string(column) + ": " + message;
        }
        return false;
    }

    bool atEnd() const { return pos_ >= text_.size(); }
    char peek() const { return atEnd() ? '\0' : text_[pos_]; }

    void skipSpace() {
        while (!atEnd() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool expectLiteral(const char* literal) {
        const std::string word(literal);
        if (text_.compare(pos_, word.size(), word) != 0) return fail("invalid literal, expected '" + word + "'");
        pos_ += word.size();
        return true;
    }

    bool parseValue(Value& out, int depth) {
        if (depth >= kMaxDepth) return fail("nesting deeper than 128 levels");
        if (atEnd()) return fail("unexpected end of input, expected a value");
        switch (peek()) {
        case '{': return parseObject(out, depth);
        case '[': return parseArray(out, depth);
        case '"': {
            std::string s;
            if (!parseString(s)) return false;
            out = Value::makeString(std::move(s));
            return true;
        }
        case 't': if (!expectLiteral("true")) return false; out = Value::makeBool(true); return true;
        case 'f': if (!expectLiteral("false")) return false; out = Value::makeBool(false); return true;
        case 'n': if (!expectLiteral("null")) return false; out = Value::makeNull(); return true;
        default:
            if (peek() == '-' || (peek() >= '0' && peek() <= '9')) return parseNumber(out);
            return fail(std::string("unexpected character '") + peek() + "'");
        }
    }

    bool parseObject(Value& out, int depth) {
        ++pos_; // '{'
        std::vector<std::string> keys;
        std::vector<Value> values;
        skipSpace();
        if (peek() == '}') {
            ++pos_;
            out = Value::makeObject(std::move(keys), std::move(values));
            return true;
        }
        while (true) {
            skipSpace();
            if (peek() != '"') return fail("expected a string key in object");
            std::string key;
            if (!parseString(key)) return false;
            for (const auto& existing : keys) {
                if (existing == key) return fail("duplicate key '" + key + "'");
            }
            skipSpace();
            if (peek() != ':') return fail("expected ':' after key '" + key + "'");
            ++pos_;
            skipSpace();
            Value member;
            if (!parseValue(member, depth + 1)) return false;
            keys.push_back(std::move(key));
            values.push_back(std::move(member));
            skipSpace();
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == '}') { ++pos_; break; }
            return fail("expected ',' or '}' in object");
        }
        out = Value::makeObject(std::move(keys), std::move(values));
        return true;
    }

    bool parseArray(Value& out, int depth) {
        ++pos_; // '['
        std::vector<Value> items;
        skipSpace();
        if (peek() == ']') {
            ++pos_;
            out = Value::makeArray(std::move(items));
            return true;
        }
        while (true) {
            skipSpace();
            Value item;
            if (!parseValue(item, depth + 1)) return false;
            items.push_back(std::move(item));
            skipSpace();
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == ']') { ++pos_; break; }
            return fail("expected ',' or ']' in array");
        }
        out = Value::makeArray(std::move(items));
        return true;
    }

    bool parseHex4(unsigned& code) {
        if (pos_ + 4 > text_.size()) return fail("truncated \\u escape");
        code = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            code <<= 4;
            if (c >= '0' && c <= '9') code |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') code |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') code |= static_cast<unsigned>(c - 'A' + 10);
            else return fail("invalid hexadecimal digit in \\u escape");
        }
        return true;
    }

    static void appendUtf8(std::string& s, unsigned code) {
        if (code < 0x80) {
            s += static_cast<char>(code);
        } else if (code < 0x800) {
            s += static_cast<char>(0xC0 | (code >> 6));
            s += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            s += static_cast<char>(0xE0 | (code >> 12));
            s += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            s += static_cast<char>(0xF0 | (code >> 18));
            s += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            s += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    bool parseString(std::string& out) {
        ++pos_; // opening quote
        out.clear();
        while (true) {
            if (atEnd()) return fail("unterminated string");
            const unsigned char c = static_cast<unsigned char>(text_[pos_]);
            if (c == '"') { ++pos_; return true; }
            if (c < 0x20) return fail("control character in string");
            if (c != '\\') { out += static_cast<char>(c); ++pos_; continue; }
            ++pos_;
            if (atEnd()) return fail("unterminated escape");
            const char e = text_[pos_++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned code = 0;
                if (!parseHex4(code)) return false;
                if (code >= 0xD800 && code <= 0xDBFF) {
                    unsigned low = 0;
                    if (text_.compare(pos_, 2, "\\u") != 0) return fail("lone high surrogate in \\u escape");
                    pos_ += 2;
                    if (!parseHex4(low)) return false;
                    if (low < 0xDC00 || low > 0xDFFF) return fail("invalid low surrogate in \\u escape");
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                } else if (code >= 0xDC00 && code <= 0xDFFF) {
                    return fail("lone low surrogate in \\u escape");
                }
                appendUtf8(out, code);
                break;
            }
            default: return fail(std::string("invalid escape '\\") + e + "'");
            }
        }
    }

    bool parseNumber(Value& out) {
        const std::size_t start = pos_;
        if (peek() == '-') ++pos_;
        if (peek() == '0') {
            ++pos_;
        } else if (peek() >= '1' && peek() <= '9') {
            while (peek() >= '0' && peek() <= '9') ++pos_;
        } else {
            return fail("invalid number");
        }
        if (peek() == '.') {
            ++pos_;
            if (!(peek() >= '0' && peek() <= '9')) return fail("expected digits after '.'");
            while (peek() >= '0' && peek() <= '9') ++pos_;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') ++pos_;
            if (!(peek() >= '0' && peek() <= '9')) return fail("expected digits in exponent");
            while (peek() >= '0' && peek() <= '9') ++pos_;
        }
        // The classic locale keeps '.' as the decimal separator whatever the user's locale is.
        std::istringstream stream(text_.substr(start, pos_ - start));
        stream.imbue(std::locale::classic());
        double value = 0.0;
        stream >> value;
        if (stream.fail() || !std::isfinite(value)) {
            pos_ = start;
            return fail("number out of range");
        }
        out = Value::makeNumber(value);
        return true;
    }
};

} // namespace

ParseResult parse(const std::string& text) {
    return Parser(text).run();
}

} // namespace Json
} // namespace Fatigue
} // namespace OpenModel
