// json.h - a small JSON reader for the analysis the C engine writes (ev_analyze_fp).
// Values are kept as a tree; lookups on missing keys return a shared null value.
#pragma once
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ev33 {

class Json {
public:
    enum Type { Null, Bool, Number, String, Array, Object };

    Type type() const { return type_; }
    bool isNull() const { return type_ == Null; }
    bool has(const std::string &k) const { return type_ == Object && obj_.count(k) > 0; }
    double num(double fallback = 0) const { return type_ == Number ? num_ : type_ == Bool ? (b_ ? 1 : 0) : fallback; }
    bool truthy() const { return type_ == Bool ? b_ : type_ == Number ? num_ != 0 : false; }
    const std::string &str() const { return str_; }
    size_t size() const { return type_ == Array ? arr_.size() : type_ == Object ? obj_.size() : 0; }

    const Json &operator[](const std::string &k) const {
        if (type_ != Object) return none();
        auto it = obj_.find(k);
        return it == obj_.end() ? none() : *it->second;
    }
    const Json &operator[](size_t i) const { return type_ == Array && i < arr_.size() ? *arr_[i] : none(); }

    static Json parse(const std::string &text) {
        size_t p = 0;
        Json j;
        if (!parseValue(text, p, j)) return Json();
        return j;
    }

private:
    Type type_ = Null;
    bool b_ = false;
    double num_ = 0;
    std::string str_;
    std::vector<std::shared_ptr<Json>> arr_;
    std::map<std::string, std::shared_ptr<Json>> obj_;

    static const Json &none() { static const Json n; return n; }
    static void ws(const std::string &s, size_t &p) { while (p < s.size() && (s[p] == ' ' || s[p] == '\n' || s[p] == '\r' || s[p] == '\t')) p++; }

    static bool parseString(const std::string &s, size_t &p, std::string &out) {
        if (s[p] != '"') return false;
        p++;
        out.clear();
        while (p < s.size() && s[p] != '"') {
            char c = s[p++];
            if (c == '\\' && p < s.size()) {
                char e = s[p++];
                switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (p + 4 > s.size()) return false;
                    unsigned cp = (unsigned)std::strtoul(s.substr(p, 4).c_str(), nullptr, 16);
                    p += 4;
                    if (cp < 0x80) out += (char)cp;
                    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                    else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                    break;
                }
                default: out += e; break;
                }
            } else out += c;
        }
        if (p >= s.size()) return false;
        p++;
        return true;
    }

    static bool parseValue(const std::string &s, size_t &p, Json &out) {
        ws(s, p);
        if (p >= s.size()) return false;
        char c = s[p];
        if (c == '{') {
            out.type_ = Object;
            p++;
            ws(s, p);
            if (p < s.size() && s[p] == '}') { p++; return true; }
            for (;;) {
                ws(s, p);
                std::string key;
                if (p >= s.size() || !parseString(s, p, key)) return false;
                ws(s, p);
                if (p >= s.size() || s[p] != ':') return false;
                p++;
                auto v = std::make_shared<Json>();
                if (!parseValue(s, p, *v)) return false;
                out.obj_[key] = v;
                ws(s, p);
                if (p < s.size() && s[p] == ',') { p++; continue; }
                if (p < s.size() && s[p] == '}') { p++; return true; }
                return false;
            }
        }
        if (c == '[') {
            out.type_ = Array;
            p++;
            ws(s, p);
            if (p < s.size() && s[p] == ']') { p++; return true; }
            for (;;) {
                auto v = std::make_shared<Json>();
                if (!parseValue(s, p, *v)) return false;
                out.arr_.push_back(v);
                ws(s, p);
                if (p < s.size() && s[p] == ',') { p++; continue; }
                if (p < s.size() && s[p] == ']') { p++; return true; }
                return false;
            }
        }
        if (c == '"') { out.type_ = String; return parseString(s, p, out.str_); }
        if (s.compare(p, 4, "true") == 0) { out.type_ = Bool; out.b_ = true; p += 4; return true; }
        if (s.compare(p, 5, "false") == 0) { out.type_ = Bool; out.b_ = false; p += 5; return true; }
        if (s.compare(p, 4, "null") == 0) { out.type_ = Null; p += 4; return true; }
        const char *b = s.c_str() + p;
        char *e = nullptr;
        double v = std::strtod(b, &e);
        if (e == b) return false;
        out.type_ = Number;
        out.num_ = v;
        p += (size_t)(e - b);
        return true;
    }
};

} // namespace ev33
