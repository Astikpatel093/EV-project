// money.cpp - exact rupee parsing and Indian-style formatting.
#include "ev/core/money.hpp"

#include <cstdio>
#include <stdexcept>

namespace ev {

Paise parse_rupees(const std::string &text) {
    std::size_t b = 0, e = text.size();
    while (b < e && (text[b] == ' ' || text[b] == '\t')) b++;
    while (e > b && (text[e - 1] == ' ' || text[e - 1] == '\t' || text[e - 1] == '\r')) e--;
    if (b == e) throw std::invalid_argument("empty amount");
    if (text[b] == '-') throw std::invalid_argument("amount must not be negative");
    if (text[b] == '+') throw std::invalid_argument("amount must be written without a sign");

    const Paise kMaxRupees = 1000000000000000LL;   // 10^15 rupees: far above any budget
    Paise rupees = 0;
    std::size_t i = b;
    int int_digits = 0;
    while (i < e && text[i] >= '0' && text[i] <= '9') {
        rupees = rupees * 10 + (text[i] - '0');
        if (rupees > kMaxRupees) throw std::invalid_argument("amount is too large");
        i++;
        int_digits++;
    }
    Paise paise = 0;
    if (i < e && text[i] == '.') {
        i++;
        int dec = 0;
        while (i < e && text[i] >= '0' && text[i] <= '9') {
            if (dec == 2) throw std::invalid_argument("amount has more than two decimal places");
            paise = paise * 10 + (text[i] - '0');
            dec++;
            i++;
        }
        if (dec == 0 && int_digits == 0) throw std::invalid_argument("not a number");
        if (dec == 1) paise *= 10;
    }
    if (int_digits == 0 && paise == 0 && (i == b || text[b] != '.')) throw std::invalid_argument("not a number");
    if (i != e) throw std::invalid_argument("not a number");
    return rupees * kPaisePerRupee + paise;
}

static std::string group_indian(Paise rupees) {
    std::string d = std::to_string(rupees);
    if (d.size() <= 3) return d;
    std::string head = d.substr(0, d.size() - 3), tail = d.substr(d.size() - 3);
    std::string out;
    // groups of two in the head (lakhs, crores ...)
    std::size_t first = head.size() % 2;
    if (first) out = head.substr(0, first);
    for (std::size_t k = first; k < head.size(); k += 2) {
        if (!out.empty()) out += ',';
        out += head.substr(k, 2);
    }
    return out + "," + tail;
}

std::string format_inr(Paise amount) {
    bool neg = amount < 0;
    Paise a = neg ? -amount : amount;
    std::string s = "INR " + std::string(neg ? "-" : "") + group_indian(a / kPaisePerRupee);
    Paise p = a % kPaisePerRupee;
    if (p) {
        char buf[8];
        std::snprintf(buf, sizeof buf, ".%02lld", static_cast<long long>(p));
        s += buf;
    }
    return s;
}

std::string format_inr_short(Paise amount) {
    char buf[64];
    double lakh = static_cast<double>(amount) / static_cast<double>(kPaisePerLakh);
    if (amount >= 100 * kPaisePerLakh) {
        std::snprintf(buf, sizeof buf, "%.2f Cr", lakh / 100.0);
        return buf;
    }
    if (amount >= kPaisePerLakh) {
        if (amount % kPaisePerLakh == 0) std::snprintf(buf, sizeof buf, "%lld L", static_cast<long long>(amount / kPaisePerLakh));
        else std::snprintf(buf, sizeof buf, "%.2f L", lakh);
        return buf;
    }
    return format_inr(amount);
}

} // namespace ev
