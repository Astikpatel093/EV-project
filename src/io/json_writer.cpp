// json_writer.cpp - indentation-aware JSON output.
#include "ev/io/json_writer.hpp"

#include <cmath>
#include <cstdio>

namespace ev {

void JsonWriter::key(const char *k) {
    if (!first_.back()) out_ += ',';
    first_.back() = 0;
    if (!out_.empty()) out_ += '\n';
    out_.append(2 * (first_.size() - 1), ' ');
    if (k) {
        out_ += '"';
        out_ += k;
        out_ += "\": ";
    }
}

void JsonWriter::open(const char *k, char c) {
    if (!out_.empty()) key(k);
    out_ += c;
    first_.push_back(1);
}

void JsonWriter::close(char c) {
    const bool empty = first_.back();
    first_.pop_back();
    if (!empty) {
        out_ += '\n';
        out_.append(2 * (first_.size() - 1), ' ');
    }
    out_ += c;
    if (first_.size() == 1) out_ += '\n';
}

void JsonWriter::number(const char *k, double v) {
    key(k);
    if (!std::isfinite(v)) { out_ += "null"; return; }
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    out_ += buf;
}

void JsonWriter::integer(const char *k, std::int64_t v) {
    key(k);
    out_ += std::to_string(v);
}

void JsonWriter::boolean(const char *k, bool v) {
    key(k);
    out_ += v ? "true" : "false";
}

void JsonWriter::string(const char *k, const std::string &v) {
    key(k);
    out_ += '"';
    for (char ch : v) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == '"' || c == '\\') { out_ += '\\'; out_ += ch; }
        else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", c);
            out_ += buf;
        } else out_ += ch;
    }
    out_ += '"';
}

} // namespace ev
