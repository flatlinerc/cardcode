#pragma once

// Tiny dependency-free JSON: a writer (just enough to emit protocol messages)
// and a permissive recursive-descent parser for incoming control messages.
// The payloads are small and fixed-shape, so this stays well under the cost of
// vendoring a JSON library and keeps the harness as dependency-free as the engine.

#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cardcode/source_span.hpp"

namespace cardcode::harness {

// ---- Writer -----------------------------------------------------------------

inline std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[(c >> 4) & 0xF];
                    out += hex[c & 0xF];
                } else {
                    out += c;
                }
        }
    }
    return out;
}

inline std::string jstr(std::string_view s) { return "\"" + json_escape(s) + "\""; }
inline std::string jnum(long long n) { return std::to_string(n); }
inline std::string jbool(bool b) { return b ? "true" : "false"; }

using JField = std::pair<std::string, std::string>; // key -> already-serialized JSON value

inline std::string jobj_v(const std::vector<JField>& fields) {
    std::string s = "{";
    bool first = true;
    for (const auto& f : fields) {
        if (!first) s += ",";
        first = false;
        s += jstr(f.first);
        s += ":";
        s += f.second;
    }
    s += "}";
    return s;
}

inline std::string jobj(std::initializer_list<JField> fields) {
    return jobj_v(std::vector<JField>(fields));
}

inline std::string jarr(const std::vector<std::string>& items) {
    std::string s = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) s += ",";
        s += items[i];
    }
    s += "]";
    return s;
}

inline std::string jspan(const SourceSpan& sp) {
    return jobj({
        {"startOffset", jnum(sp.start_offset)},
        {"endOffset",   jnum(sp.end_offset)},
        {"startLine",   jnum(sp.start_line)},
        {"startColumn", jnum(sp.start_column)},
        {"endLine",     jnum(sp.end_line)},
        {"endColumn",   jnum(sp.end_column)},
    });
}

// ---- Parser -----------------------------------------------------------------

struct JsonValue {
    enum class Type { Null, Bool, Num, Str, Arr, Obj };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string str;
    std::vector<JsonValue> arr;
    std::map<std::string, JsonValue> obj;

    bool is_obj() const { return type == Type::Obj; }
    const JsonValue* find(const std::string& key) const {
        if (type != Type::Obj) return nullptr;
        auto it = obj.find(key);
        return it == obj.end() ? nullptr : &it->second;
    }
    std::string as_str() const { return type == Type::Str ? str : std::string(); }
    long long as_int() const { return static_cast<long long>(number); }
    bool as_bool() const {
        if (type == Type::Bool) return boolean;
        if (type == Type::Num) return number != 0;
        return false;
    }
};

namespace detail {

struct JsonParser {
    std::string_view s;
    std::size_t i = 0;

    void skip_ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }

    bool parse_value(JsonValue& out) {
        skip_ws();
        if (i >= s.size()) return false;
        switch (s[i]) {
            case '{': return parse_object(out);
            case '[': return parse_array(out);
            case '"': return parse_string_value(out);
            case 't': case 'f': return parse_bool(out);
            case 'n': return parse_null(out);
            default:  return parse_number(out);
        }
    }

    bool parse_object(JsonValue& out) {
        out.type = JsonValue::Type::Obj;
        ++i; // {
        skip_ws();
        if (i < s.size() && s[i] == '}') { ++i; return true; }
        for (;;) {
            skip_ws();
            if (i >= s.size() || s[i] != '"') return false;
            std::string key;
            if (!parse_string(key)) return false;
            skip_ws();
            if (i >= s.size() || s[i] != ':') return false;
            ++i;
            JsonValue v;
            if (!parse_value(v)) return false;
            out.obj.emplace(std::move(key), std::move(v));
            skip_ws();
            if (i >= s.size()) return false;
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == '}') { ++i; return true; }
            return false;
        }
    }

    bool parse_array(JsonValue& out) {
        out.type = JsonValue::Type::Arr;
        ++i; // [
        skip_ws();
        if (i < s.size() && s[i] == ']') { ++i; return true; }
        for (;;) {
            JsonValue v;
            if (!parse_value(v)) return false;
            out.arr.push_back(std::move(v));
            skip_ws();
            if (i >= s.size()) return false;
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == ']') { ++i; return true; }
            return false;
        }
    }

    bool parse_string_value(JsonValue& out) {
        out.type = JsonValue::Type::Str;
        return parse_string(out.str);
    }

    bool parse_string(std::string& out) {
        if (i >= s.size() || s[i] != '"') return false;
        ++i;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c == '\\') {
                if (i >= s.size()) return false;
                char e = s[i++];
                switch (e) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'u': {
                        if (i + 4 > s.size()) return false;
                        unsigned cp = 0;
                        for (int k = 0; k < 4; ++k) {
                            char h = s[i++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= unsigned(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= unsigned(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= unsigned(h - 'A' + 10);
                            else return false;
                        }
                        // Encode the BMP code point as UTF-8 (enough for v1 source).
                        if (cp < 0x80) {
                            out += char(cp);
                        } else if (cp < 0x800) {
                            out += char(0xC0 | (cp >> 6));
                            out += char(0x80 | (cp & 0x3F));
                        } else {
                            out += char(0xE0 | (cp >> 12));
                            out += char(0x80 | ((cp >> 6) & 0x3F));
                            out += char(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: return false;
                }
            } else {
                out += c;
            }
        }
        return false; // unterminated
    }

    bool parse_bool(JsonValue& out) {
        if (s.compare(i, 4, "true") == 0) { out.type = JsonValue::Type::Bool; out.boolean = true; i += 4; return true; }
        if (s.compare(i, 5, "false") == 0) { out.type = JsonValue::Type::Bool; out.boolean = false; i += 5; return true; }
        return false;
    }

    bool parse_null(JsonValue& out) {
        if (s.compare(i, 4, "null") == 0) { out.type = JsonValue::Type::Null; i += 4; return true; }
        return false;
    }

    bool parse_number(JsonValue& out) {
        std::size_t start = i;
        while (i < s.size()) {
            char c = s[i];
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') ++i;
            else break;
        }
        if (i == start) return false;
        out.type = JsonValue::Type::Num;
        out.number = std::strtod(std::string(s.substr(start, i - start)).c_str(), nullptr);
        return true;
    }
};

} // namespace detail

inline std::optional<JsonValue> json_parse(std::string_view text) {
    detail::JsonParser p{text, 0};
    JsonValue v;
    if (!p.parse_value(v)) return std::nullopt;
    p.skip_ws();
    return v; // trailing junk is tolerated
}

} // namespace cardcode::harness
