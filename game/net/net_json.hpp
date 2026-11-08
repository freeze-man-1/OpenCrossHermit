// ============================================================================
// net_json.hpp — just enough JSON for the Apinator protocol: a small value
// tree parser (objects, arrays, strings, numbers, true / false / null) and
// string quoting for the frames we write.
// ============================================================================
#pragma once
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ch::net::json {

struct Value {
    enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Value> items;
    std::map<std::string, Value, std::less<>> fields;

    [[nodiscard]] const Value* get(std::string_view k) const {
        if (kind != Kind::Object) return nullptr;
        const auto it = fields.find(k);
        return it == fields.end() ? nullptr : &it->second;
    }
    // The field as a string ("" when missing or not a string).
    [[nodiscard]] std::string text(std::string_view k) const {
        const Value* v = get(k);
        return v && v->kind == Kind::String ? v->str : std::string();
    }
    [[nodiscard]] double number(std::string_view k, double dflt = 0) const {
        const Value* v = get(k);
        return v && v->kind == Kind::Number ? v->num : dflt;
    }
    [[nodiscard]] bool flag(std::string_view k) const {
        const Value* v = get(k);
        return v && ((v->kind == Kind::Bool && v->b) || (v->kind == Kind::Number && v->num != 0));
    }
};

namespace detail {
struct Parser {
    std::string_view s;
    std::size_t at = 0;
    int depth = 0;
    bool ok = true;

    void ws() { while (at < s.size() && (s[at] == ' ' || s[at] == '\t' || s[at] == '\n' || s[at] == '\r')) ++at; }
    bool eat(char c) { ws(); if (at < s.size() && s[at] == c) { ++at; return true; } return false; }
    static void putUtf8(std::string& o, std::uint32_t cp) {
        if (cp < 0x80) { o += char(cp); }
        else if (cp < 0x800) { o += char(0xc0 | cp >> 6); o += char(0x80 | (cp & 0x3f)); }
        else if (cp < 0x10000) { o += char(0xe0 | cp >> 12); o += char(0x80 | (cp >> 6 & 0x3f)); o += char(0x80 | (cp & 0x3f)); }
        else { o += char(0xf0 | cp >> 18); o += char(0x80 | (cp >> 12 & 0x3f)); o += char(0x80 | (cp >> 6 & 0x3f)); o += char(0x80 | (cp & 0x3f)); }
    }
    std::uint32_t hex4() {
        if (at + 4 > s.size()) { ok = false; return 0; }
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s[at++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= std::uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v |= std::uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= std::uint32_t(c - 'A' + 10);
            else ok = false;
        }
        return v;
    }
    std::string string() {
        std::string o;
        if (!eat('"')) { ok = false; return o; }
        while (at < s.size() && ok) {
            const char c = s[at++];
            if (c == '"') return o;
            if (c != '\\') { o += c; continue; }
            if (at >= s.size()) break;
            switch (const char e = s[at++]) {
                case 'n': o += '\n'; break;
                case 't': o += '\t'; break;
                case 'r': o += '\r'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'u': {
                    std::uint32_t cp = hex4();
                    if (cp >= 0xd800 && cp < 0xdc00 && at + 6 <= s.size() && s[at] == '\\' && s[at + 1] == 'u') {
                        at += 2;
                        const std::uint32_t lo = hex4();
                        cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                    }
                    putUtf8(o, cp);
                    break;
                }
                default: o += e; break;
            }
        }
        ok = false;
        return o;
    }
    Value value() {
        Value v;
        ws();
        if (at >= s.size() || ++depth > 32) { ok = false; return v; }
        const char c = s[at];
        if (c == '{') {
            ++at;
            v.kind = Value::Kind::Object;
            if (!eat('}')) {
                do {
                    std::string k = string();
                    if (!ok || !eat(':')) { ok = false; break; }
                    v.fields[std::move(k)] = value();
                } while (ok && eat(','));
                if (ok && !eat('}')) ok = false;
            }
        } else if (c == '[') {
            ++at;
            v.kind = Value::Kind::Array;
            if (!eat(']')) {
                do v.items.push_back(value()); while (ok && eat(','));
                if (ok && !eat(']')) ok = false;
            }
        } else if (c == '"') {
            v.kind = Value::Kind::String;
            v.str = string();
        } else if (s.substr(at, 4) == "true") { v.kind = Value::Kind::Bool; v.b = true; at += 4; }
        else if (s.substr(at, 5) == "false") { v.kind = Value::Kind::Bool; at += 5; }
        else if (s.substr(at, 4) == "null") { at += 4; }
        else {
            const std::size_t from = at;
            while (at < s.size() && (s[at] == '-' || s[at] == '+' || s[at] == '.' || s[at] == 'e' || s[at] == 'E' ||
                                     (s[at] >= '0' && s[at] <= '9'))) ++at;
            if (at == from) { ok = false; return v; }
            v.kind = Value::Kind::Number;
            v.num = std::strtod(std::string(s.substr(from, at - from)).c_str(), nullptr);
        }
        --depth;
        return v;
    }
};
} // namespace detail

// False when `s` is not one JSON value.
[[nodiscard]] inline bool parse(std::string_view s, Value& out) {
    detail::Parser p{s};
    out = p.value();
    p.ws();
    return p.ok && p.at == s.size();
}

// `s` as a JSON string literal.
[[nodiscard]] inline std::string quote(std::string_view s) {
    std::string o = "\"";
    for (const char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (std::uint8_t(c) < 0x20) {
                    static constexpr char d[] = "0123456789abcdef";
                    o += "\\u00";
                    o += d[std::uint8_t(c) >> 4];
                    o += d[c & 15];
                } else {
                    o += c;
                }
        }
    }
    return o + '"';
}

} // namespace ch::net::json
