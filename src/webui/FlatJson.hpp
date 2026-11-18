#pragma once

#include <cstddef>
#include <map>
#include <string>

/**
 * @file FlatJson.hpp
 * @brief Minimal reader for a flat JSON object whose values are all strings, such as {"size":"1.5","shape":"hex"}.
 *
 * Only what the WebUI callbacks need. Numbers, booleans, null, arrays and nested objects are rejected.
 * Accepted escapes: \" \\ \/ \b \f \n \r \t and \uXXXX, with surrogate pairs. Strings are returned as UTF-8.
 */
namespace OpenModel::FlatJson {

namespace detail {

/** @brief Position in the text being parsed. */
struct Cursor {
    const std::string& text;
    std::size_t pos = 0;

    /** @brief Skips JSON whitespace. */
    void skipSpace() {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r'))
            ++pos;
    }
    /** @brief True when the next character is c. */
    bool at(char c) const { return pos < text.size() && text[pos] == c; }
};

/**
 * @brief Reads four hexadecimal digits.
 * @param text the text
 * @param pos  position of the first digit
 * @param out  receives the value
 * @return false when there are fewer than four digits or one is not hexadecimal
 */
inline bool readHex4(const std::string& text, std::size_t pos, unsigned& out) {
    if (pos + 4 > text.size()) return false;
    out = 0;
    for (std::size_t k = 0; k < 4; ++k) {
        const char c = text[pos + k];
        unsigned digit = 0;
        if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
        else return false;
        out = out * 16 + digit;
    }
    return true;
}

/**
 * @brief Appends a code point to a string as UTF-8.
 * @param out the string
 * @param cp  the code point, at most 0x10FFFF
 */
inline void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

/**
 * @brief Reads a string literal starting at its opening quote.
 * @param c     cursor; left after the closing quote on success
 * @param out   receives the decoded string
 * @param error receives a short description on failure
 * @return true when a complete string was read
 */
inline bool readString(Cursor& c, std::string& out, std::string& error) {
    if (!c.at('"')) { error = "expected a string"; return false; }
    ++c.pos;
    out.clear();
    while (c.pos < c.text.size()) {
        const unsigned char ch = static_cast<unsigned char>(c.text[c.pos]);
        if (ch == '"') { ++c.pos; return true; }
        if (ch < 0x20) { error = "control character inside a string"; return false; }
        if (ch != '\\') { out += static_cast<char>(ch); ++c.pos; continue; }
        if (c.pos + 1 >= c.text.size()) break;
        const char escape = c.text[c.pos + 1];
        c.pos += 2;
        switch (escape) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
            unsigned cp = 0;
            if (!readHex4(c.text, c.pos, cp)) { error = "invalid \\u escape"; return false; }
            c.pos += 4;
            if (cp >= 0xDC00 && cp <= 0xDFFF) { error = "unpaired surrogate in \\u escape"; return false; }
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                unsigned low = 0;
                const bool paired = c.pos + 1 < c.text.size() && c.text[c.pos] == '\\' && c.text[c.pos + 1] == 'u' &&
                                    readHex4(c.text, c.pos + 2, low) && low >= 0xDC00 && low <= 0xDFFF;
                if (!paired) { error = "unpaired surrogate in \\u escape"; return false; }
                c.pos += 6;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            }
            appendUtf8(out, cp);
            break;
        }
        default:
            error = "invalid escape sequence";
            return false;
        }
    }
    error = "unterminated string";
    return false;
}

}  // namespace detail

/**
 * @brief Parses a flat JSON object whose keys and values are strings.
 * @param text   the JSON text
 * @param fields receives the key/value pairs; cleared first. A repeated key keeps its last value.
 * @param error  receives a short description when parsing fails
 * @return true when the whole text is one valid flat object (an empty object is valid)
 */
inline bool parseStringObject(const std::string& text, std::map<std::string, std::string>& fields, std::string& error) {
    fields.clear();
    detail::Cursor c{text};
    c.skipSpace();
    if (!c.at('{')) { error = "expected '{'"; return false; }
    ++c.pos;
    c.skipSpace();
    if (c.at('}')) {
        ++c.pos;
    } else {
        for (;;) {
            std::string key;
            std::string value;
            c.skipSpace();
            if (!detail::readString(c, key, error)) return false;
            c.skipSpace();
            if (!c.at(':')) { error = "expected ':' after key '" + key + "'"; return false; }
            ++c.pos;
            c.skipSpace();
            if (!detail::readString(c, value, error)) { error = "value of '" + key + "': " + error; return false; }
            fields[key] = value;
            c.skipSpace();
            if (c.at(',')) { ++c.pos; continue; }
            if (c.at('}')) { ++c.pos; break; }
            error = "expected ',' or '}'";
            return false;
        }
    }
    c.skipSpace();
    if (c.pos != text.size()) { error = "unexpected text after the object"; return false; }
    return true;
}

}  // namespace OpenModel::FlatJson
