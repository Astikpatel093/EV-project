// fmt.cpp - number and time formatting shared by the desktop app and the web server.
#include "fmt.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace ev33 {

/* ======================================================================== formatting */
std::string fint(double v) {
    long long n = std::llround(v);
    bool neg = n < 0;
    unsigned long long u = neg ? (unsigned long long)(-n) : (unsigned long long)n;
    std::string s = std::to_string(u), out;
    int cnt = 0;
    for (int i = (int)s.size() - 1; i >= 0; i--) {
        out.insert(out.begin(), s[(size_t)i]);
        if (++cnt % 3 == 0 && i > 0) out.insert(out.begin(), ',');
    }
    return neg ? "-" + out : out;
}
static std::string fdec(double v, int d) {
    double p = std::pow(10.0, d);
    double r = std::round(std::fabs(v) * p) / p;
    double ip = std::floor(r);
    char frac[16];
    std::snprintf(frac, sizeof frac, "%.*f", d, r - ip);
    std::string fs = frac;
    if (fs.size() > 1 && fs[0] == '1') { ip += 1; fs = fs.substr(1); } else fs = fs.substr(1);
    return (v < 0 && r != 0 ? "-" : "") + fint(ip) + fs;
}
std::string f1(double v) { return fdec(v, 1); }
std::string f2(double v) { return fdec(v, 2); }
std::string fms(double v) {
    if (v >= 1000) return f2(v / 1000) + " s";
    if (v >= 100) return fint(v) + " ms";
    if (v >= 10) return f1(v) + " ms";
    if (v >= 1) return f2(v) + " ms";
    if (v * 1000 < 1) return "<1 \xC2\xB5s";
    return fint(v * 1000) + " \xC2\xB5s";
}
std::string fclock(double min) {
    long m = ((std::lround(min) % 1440) + 1440) % 1440;
    char b[8];
    std::snprintf(b, sizeof b, "%02ld:%02ld", m / 60, m % 60);
    return b;
}
std::string fdur(double m) {
    if (m >= 120) return std::to_string((long)std::floor(m / 60)) + " h " + fint(std::fmod(m, 60)) + " min";
    return fint(m) + " min";
}
std::string fsci(double v) {
    if (v < 1e6) return fint(v);
    int e = (int)std::floor(std::log10(v));
    static const char *sup[] = { "\xE2\x81\xB0", "\xC2\xB9", "\xC2\xB2", "\xC2\xB3", "\xE2\x81\xB4",
                                 "\xE2\x81\xB5", "\xE2\x81\xB6", "\xE2\x81\xB7", "\xE2\x81\xB8", "\xE2\x81\xB9" };
    std::string es;
    for (char ch : std::to_string(e)) es += sup[ch - '0'];
    return f1(v / std::pow(10.0, e)) + "\xC3\x97" "10" + es;
}
std::string fbytes(double b) {
    if (b >= 1048576) return f1(b / 1048576) + " MB";
    if (b >= 1024) return f1(b / 1024) + " KB";
    return fint(b) + " B";
}
std::string rupees(double lakh) { return "\xE2\x82\xB9" + fint(lakh) + " lakh"; }

} // namespace ev33
