// csv.hpp - strict CSV reading and writing.
//
// Format: first non-empty, non-comment line is the header; '#' lines are comments (the first ones
// are kept as provenance text); fields may be quoted with "..." and "" inside quotes is a quote.
// Every row must have exactly as many fields as the header. Columns are matched by name, so their
// order is free; unknown column names and missing required columns are errors (a typo in an
// optional column would otherwise be ignored silently).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ev {

class CsvTable {
public:
    // Parses `text`; `file` is used in error messages. Throws ev::DataError.
    static CsvTable parse(const std::string &text, const std::string &file,
                          const std::vector<std::string> &required, const std::vector<std::string> &optional);

    std::size_t rows() const { return rows_.size(); }
    int line(std::size_t row) const { return lines_[row]; }             // 1-based line in the file
    const std::string &file() const { return file_; }
    const std::vector<std::string> &comments() const { return comments_; }
    bool has_column(const std::string &name) const { return column(name) >= 0; }

    // Typed access; empty optional cells -> std::nullopt; malformed values throw ev::DataError
    // naming file:line and the column.
    std::string text(std::size_t row, const std::string &col) const;            // trimmed, may be empty
    std::int64_t integer(std::size_t row, const std::string &col) const;        // required
    std::optional<std::int64_t> opt_integer(std::size_t row, const std::string &col) const;
    double number(std::size_t row, const std::string &col) const;               // required, finite
    std::optional<double> opt_number(std::size_t row, const std::string &col) const;
    std::int64_t paise(std::size_t row, const std::string &col) const;          // rupees -> paise

    [[noreturn]] void fail(std::size_t row, const std::string &reason) const;

private:
    int column(const std::string &name) const;
    const std::string &cell(std::size_t row, const std::string &col) const;
    std::string file_;
    std::vector<std::string> header_, comments_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<int> lines_;
};

// Splits one CSV line into fields (quotes handled). Returns false on an unterminated quote.
bool split_csv_line(const std::string &line, std::vector<std::string> &out);
// Quotes a field when it contains a comma, quote or newline.
std::string csv_escape(const std::string &field);

// Strict numeric parsing helpers (whole string must be a number; no NaN/inf). Return false on error.
bool parse_int64(const std::string &s, std::int64_t &out);
bool parse_double(const std::string &s, double &out);

} // namespace ev
