/**
 * @file web_base64_test.cpp
 * @brief Tests of the base64 decoder used for the files dropped in the WebUI (src/webui/Base64.hpp).
 */
#include "../src/webui/Base64.hpp"

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

#define CHECK_TRUE(cond)                                                                 \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                                \
        }                                                                                \
    } while (0)

/** Decodes and checks the result against the expected bytes. */
void expectDecodes(const std::string& encoded, const std::string& expected) {
    std::string out = "stale";
    const bool ok = decodeBase64(encoded, out);
    if (!ok || out != expected) {
        std::fprintf(stderr, "decode of '%s' gave ok=%d, %zu bytes\n", encoded.c_str(), ok ? 1 : 0, out.size());
        ++g_failures;
    }
}

void expectRejects(const std::string& encoded) {
    std::string out;
    if (decodeBase64(encoded, out)) {
        std::fprintf(stderr, "'%s' should be rejected\n", encoded.c_str());
        ++g_failures;
    }
    CHECK_TRUE(out.empty());
}

} // namespace

int main() {
    // RFC 4648 section 10 test vectors.
    expectDecodes("", "");
    expectDecodes("Zg==", "f");
    expectDecodes("Zm8=", "fo");
    expectDecodes("Zm9v", "foo");
    expectDecodes("Zm9vYg==", "foob");
    expectDecodes("Zm9vYmE=", "fooba");
    expectDecodes("Zm9vYmFy", "foobar");

    // Whitespace (line breaks from a MIME-wrapped encoder) is ignored.
    expectDecodes("Zm9v\nYmFy\r\n", "foobar");
    expectDecodes(" Zm9v YmFy ", "foobar");

    // Binary content, including zero bytes and bytes above 0x7F.
    expectDecodes("AP//", std::string("\x00\xff\xff", 3));
    expectDecodes("/w==", std::string("\xff", 1));

    // Unused bits of the last symbol are not checked (lenient default, as before): 'h' after "Z" gives 'f' with four bits left.
    expectDecodes("Zh==", "f");

    // Invalid input: a character outside the alphabet, data after padding, impossible lengths.
    expectRejects("Zm9v!mFy");
    expectRejects("Zg==Zg==");
    expectRejects("Z");
    expectRejects("Zm9vY");

    if (g_failures == 0) std::printf("web_base64_test: all checks passed\n");
    else std::printf("web_base64_test: %d check(s) failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
