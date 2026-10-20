// money.hpp - installation costs and budgets as exact integers.
//
// Costs are written in the data files as decimal rupees ("8700000" or "8700000.50"). They are
// parsed digit by digit into an integer number of paise (1 rupee = 100 paise), never through a
// double, so a budget check "total <= budget" is exact and can never be broken by rounding.
#pragma once

#include <cstdint>
#include <string>

namespace ev {

using Paise = std::int64_t;

constexpr Paise kPaisePerRupee = 100;
constexpr Paise kPaisePerLakh = 100 * 100000;   // 1 lakh = 1,00,000 rupees

// Parses "1234", "1234.5", "1234.56" (optionally with surrounding spaces). Rejects signs, more
// than two decimals, empty strings, other characters and values above 10^15 rupees.
// Throws std::invalid_argument with a short reason.
Paise parse_rupees(const std::string &text);

// "INR 1,23,45,678" (Indian digit grouping; paise shown only when non-zero: "INR 12.50").
std::string format_inr(Paise amount);

// Short form for tables and maps: "87 L" for 87 lakh, "1.25 Cr" for 1.25 crore, "INR 45,000" below a lakh.
std::string format_inr_short(Paise amount);

} // namespace ev
