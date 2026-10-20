// csv.cpp - strict CSV parsing with file:line error messages.
#include "ev/io/csv.hpp"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

#include "ev/core/errors.hpp"
#include "ev/core/money.hpp"

namespace ev {

static std::string trim(const std::string &s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) b++;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) e--;
    return s.substr(b, e - b);
}

static std::string lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool split_csv_line(const std::string &line, std::vector<std::string> &out) {
    out.clear();
    std::string cur;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') { cur.push_back('"'); i++; }
                else quoted = false;
            } else {
                cur.push_back(c);
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return !quoted;
}

std::string csv_escape(const std::string &f) {
    if (f.find_first_of(",\"\n\r") == std::string::npos) return f;
    std::string o = "\"";
    for (char c : f) {
        if (c == '"') o += "\"\"";
        else o.push_back(c);
    }
    return o + "\"";
}

bool parse_int64(const std::string &s0, std::int64_t &out) {
    std::string s = trim(s0);
    if (s.empty()) return false;
    std::size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (i == s.size()) return false;
    for (std::size_t k = i; k < s.size(); k++)
        if (!std::isdigit(static_cast<unsigned char>(s[k]))) return false;
    errno = 0;
    long long v = std::strtoll(s.c_str(), nullptr, 10);
    if (errno == ERANGE) return false;
    out = v;
    return true;
}

bool parse_double(const std::string &s0, double &out) {
    std::string s = trim(s0);
    if (s.empty()) return false;
    // only plain decimal notation: digits, one '.', optional sign and exponent (no "inf", "nan", hex)
    for (char c : s)
        if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E'))
            return false;
    char *end = nullptr;
    errno = 0;
    double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size() || errno == ERANGE || !std::isfinite(v)) return false;
    out = v;
    return true;
}

CsvTable CsvTable::parse(const std::string &text, const std::string &file, const std::vector<std::string> &required,
                         const std::vector<std::string> &optional) {
    CsvTable t;
    t.file_ = file;
    std::size_t pos = 0;
    int lineno = 0;
    bool have_header = false;
    std::vector<std::string> fields;
    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
        lineno++;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (lineno == 1 && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF)
            line = line.substr(3);   // UTF-8 byte-order mark written by some spreadsheet programs
        std::string tl = trim(line);
        if (tl.empty()) continue;
        if (tl[0] == '#') {
            if (!have_header) t.comments_.push_back(trim(tl.substr(1)));
            continue;
        }
        if (!split_csv_line(line, fields)) throw DataError(file, lineno, "unterminated quoted field");
        if (!have_header) {
            for (std::string &f : fields) f = lower(trim(f));
            for (std::size_t i = 0; i < fields.size(); i++) {
                const std::string &h = fields[i];
                if (h.empty()) throw DataError(file, lineno, "empty column name in header");
                bool known = false;
                for (const auto &r : required) known = known || r == h;
                for (const auto &o : optional) known = known || o == h;
                if (!known) throw DataError(file, lineno, "unknown column '" + h + "'");
                for (std::size_t j = 0; j < i; j++)
                    if (fields[j] == h) throw DataError(file, lineno, "column '" + h + "' appears twice");
            }
            for (const auto &r : required) {
                bool found = false;
                for (const auto &f : fields) found = found || f == r;
                if (!found) throw DataError(file, lineno, "missing required column '" + r + "'");
            }
            t.header_ = fields;
            have_header = true;
            continue;
        }
        if (fields.size() != t.header_.size())
            throw DataError(file, lineno, "expected " + std::to_string(t.header_.size()) + " fields, found " +
                                              std::to_string(fields.size()));
        for (std::string &f : fields) f = trim(f);
        t.rows_.push_back(fields);
        t.lines_.push_back(lineno);
    }
    if (!have_header) throw DataError(file, 0, "file is empty (no header row)");
    return t;
}

int CsvTable::column(const std::string &name) const {
    for (std::size_t i = 0; i < header_.size(); i++)
        if (header_[i] == name) return static_cast<int>(i);
    return -1;
}

const std::string &CsvTable::cell(std::size_t row, const std::string &col) const {
    static const std::string empty;
    int c = column(col);
    if (c < 0) return empty;   // optional column not present
    return rows_[row][static_cast<std::size_t>(c)];
}

void CsvTable::fail(std::size_t row, const std::string &reason) const { throw DataError(file_, lines_[row], reason); }

std::string CsvTable::text(std::size_t row, const std::string &col) const { return cell(row, col); }

std::int64_t CsvTable::integer(std::size_t row, const std::string &col) const {
    const std::string &s = cell(row, col);
    if (s.empty()) fail(row, "missing value for '" + col + "'");
    std::int64_t v;
    if (!parse_int64(s, v)) fail(row, "'" + col + "' must be a whole number, got '" + s + "'");
    return v;
}

std::optional<std::int64_t> CsvTable::opt_integer(std::size_t row, const std::string &col) const {
    if (cell(row, col).empty()) return std::nullopt;
    return integer(row, col);
}

double CsvTable::number(std::size_t row, const std::string &col) const {
    const std::string &s = cell(row, col);
    if (s.empty()) fail(row, "missing value for '" + col + "'");
    double v;
    if (!parse_double(s, v)) fail(row, "'" + col + "' must be a number, got '" + s + "'");
    return v;
}

std::optional<double> CsvTable::opt_number(std::size_t row, const std::string &col) const {
    if (cell(row, col).empty()) return std::nullopt;
    return number(row, col);
}

std::int64_t CsvTable::paise(std::size_t row, const std::string &col) const {
    const std::string &s = cell(row, col);
    if (s.empty()) fail(row, "missing value for '" + col + "'");
    try {
        return parse_rupees(s);
    } catch (const std::invalid_argument &e) {
        fail(row, "'" + col + "': " + e.what() + " ('" + s + "')");
    }
}

} // namespace ev
