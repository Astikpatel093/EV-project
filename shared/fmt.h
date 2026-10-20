// fmt.h - number and time formatting shared by the desktop app and the web server.
#pragma once
#include <string>

namespace ev33 {

/* ---------------------------------------------------------------- number formatting */
std::string fint(double v);            // 1,234
std::string f1(double v);              // 1,234.5
std::string f2(double v);              // 1,234.56
std::string fms(double v);             // 12.3 ms, 450 µs, 1.20 s
std::string fclock(double min);        // 08:30
std::string fdur(double min);          // 45 min, 3 h 12 min
std::string fsci(double v);            // 1.2×10⁶
std::string fbytes(double b);          // 1.2 MB
std::string rupees(double lakh);       // ₹900 lakh

} // namespace ev33
