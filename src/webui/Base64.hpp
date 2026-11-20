#pragma once

/**
 * @file Base64.hpp
 * @brief Decoder for the base64 text the page sends with a dropped file.
 *
 * The browser cannot give the real path of a dropped file, so its content travels as base64 in one argument of the
 * native call. Header only, so that it can be tested without the WebUI or Open CASCADE dependencies.
 */

#include <cctype>
#include <cstdint>
#include <string>

/**
 * @brief Decodes standard base64 (RFC 4648: '+', '/', '=' padding; whitespace ignored).
 * @param text the encoded text
 * @param out receives the decoded bytes (cleared first, binary safe)
 * @return false when the text holds a character outside the alphabet, data after the padding, or a length that
 *         cannot come from an encoder; out is then unspecified
 */
inline bool decodeBase64(const std::string& text, std::string& out) {
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int table[256];
    for (int& entry : table) entry = -1;
    for (int i = 0; i < 64; ++i) table[static_cast<unsigned char>(kAlphabet[i])] = i;

    out.clear();
    out.reserve(text.size() / 4 * 3);
    std::uint32_t buffer = 0;   // only the low bits still needed are kept
    int bits = 0;               // bits of buffer not yet written out
    int symbols = 0;            // data characters, padding excluded
    int padding = 0;
    for (const char character : text) {
        const auto c = static_cast<unsigned char>(character);
        if (std::isspace(c)) continue;
        if (c == '=') { ++padding; continue; }
        if (padding > 0) return false;              // data after padding
        const int value = table[c];
        if (value < 0) return false;
        buffer = ((buffer << 6) | static_cast<std::uint32_t>(value)) & 0xFFFFFFu;
        bits += 6;
        ++symbols;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buffer >> bits) & 0xFFu));
        }
    }
    // Data symbols leave a remainder of 0, 2 or 3 (never 1). With padding, data and padding fill whole groups.
    if (symbols % 4 == 1) return false;
    if (padding > 2) return false;
    if (padding > 0 && (symbols + padding) % 4 != 0) return false;
    return true;
}
