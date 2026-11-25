#pragma once

/**
 * @file Base64.hpp
 * @brief Decoder for the base64 text the page sends with a dropped file.
 *
 * The browser cannot give the real path of a dropped file, so its content travels as base64 in one argument of the
 * native call. The decoding is done by TK4CPP::Encode::Base64 with its default options (RFC 4648; the unused bits of
 * the last symbol are not checked, as the former decoder did). Header only, so that it can be tested on its own.
 */

#include <TK4CPP/Encode/Base64.h>

#include <cctype>
#include <string>

/**
 * @brief Decodes standard base64 (RFC 4648: '+', '/', '=' padding; whitespace ignored).
 * @param text the encoded text
 * @param out receives the decoded bytes (cleared first, binary safe)
 * @return false when the text holds a character outside the alphabet, data after the padding or a length that cannot
 *         come from an encoder; out is then cleared
 */
inline bool decodeBase64(const std::string& text, std::string& out) {
    std::string compact;
    compact.reserve(text.size());
    for (const char character : text) {
        if (!std::isspace(static_cast<unsigned char>(character))) compact.push_back(character);
    }
    const auto decoded = TK4CPP::Encode::Base64::decodeDefault(compact);
    if (!decoded) {
        out.clear();
        return false;
    }
    out.assign(reinterpret_cast<const char*>(decoded->data()), decoded->size());
    return true;
}
