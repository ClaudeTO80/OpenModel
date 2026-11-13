#include <OpenModel/Fatigue/Json.hpp>

#include <cassert>
#include <string>

using namespace OpenModel::Fatigue::Json;

namespace {

bool fails(const std::string& text, const std::string& expectedInError = std::string()) {
    const auto result = parse(text);
    if (result.ok) return false;
    if (!expectedInError.empty()) {
        assert(result.error.find(expectedInError) != std::string::npos);
    }
    return true;
}

} // namespace

int main() {
    // Values and their types.
    {
        const auto r = parse(" {\"a\": [1, -2.5e2, 0.125, true, false, null, \"x\"], \"b\": {}} ");
        assert(r.ok);
        assert(r.value.isObject());
        assert(r.value.keys().size() == 2 && r.value.keys()[0] == "a" && r.value.keys()[1] == "b");
        const Value* a = r.value.find("a");
        assert(a && a->isArray() && a->asArray().size() == 7);
        assert(a->asArray()[0].asNumber() == 1.0);
        assert(a->asArray()[1].asNumber() == -250.0);
        assert(a->asArray()[2].asNumber() == 0.125);
        assert(a->asArray()[3].isBool() && a->asArray()[3].asBool());
        assert(a->asArray()[4].isBool() && !a->asArray()[4].asBool());
        assert(a->asArray()[5].isNull());
        assert(a->asArray()[6].asString() == "x");
        assert(r.value.find("b")->isObject());
        assert(r.value.find("missing") == nullptr);
        assert(a->find("x") == nullptr); // find() on a non-object
    }

    // Escapes, including UTF-8 from \u escapes and a surrogate pair.
    {
        const auto r = parse("\"a\\n\\t\\\"\\\\\\/\\u00e8\\ud83d\\ude00\"");
        assert(r.ok);
        assert(r.value.asString() == std::string("a\n\t\"\\/") + "\xc3\xa8" + "\xf0\x9f\x98\x80");
    }

    // Numbers keep the classic decimal point whatever the locale.
    {
        const auto r = parse("[0, -0.0, 1E3, 12.5e-1]");
        assert(r.ok);
        assert(r.value.asArray()[2].asNumber() == 1000.0);
        assert(r.value.asArray()[3].asNumber() == 1.25);
    }

    // Errors: each one is rejected, with its position in the message.
    assert(fails("", "unexpected end of input"));
    assert(fails("[1,]", "unexpected character ']'"));
    assert(fails("{\"a\":1,}", "expected a string key"));
    assert(fails("{\"a\" 1}", "expected ':'"));
    assert(fails("{\"a\":1 \"b\":2}", "expected ',' or '}'"));
    assert(fails("[1 2]", "expected ',' or ']'"));
    assert(fails("{\"a\":1,\"a\":2}", "duplicate key 'a'"));
    assert(fails("\"abc", "unterminated string"));
    assert(fails("\"a\x01\"", "control character"));
    assert(fails("\"\\q\"", "invalid escape"));
    assert(fails("\"\\u12G4\"", "invalid hexadecimal"));
    assert(fails("\"\\ud800\"", "lone high surrogate"));
    assert(fails("\"\\udc00\"", "lone low surrogate"));
    assert(fails("01", "unexpected text after the JSON value"));
    assert(fails("1.", "expected digits after '.'"));
    assert(fails("1e", "expected digits in exponent"));
    assert(fails("1e999", "number out of range"));
    assert(fails("tru", "invalid literal"));
    assert(fails("[1] x", "unexpected text after the JSON value"));
    assert(fails("{\n  \"a\": ,\n}", "line 2"));

    // Nesting limit: 128 levels are accepted, 129 rejected.
    {
        std::string ok(128, '['), deep(129, '[');
        ok += std::string(128, ']');
        deep += std::string(129, ']');
        assert(parse(ok).ok);
        assert(fails(deep, "nesting deeper than 128 levels"));
    }
    return 0;
}
