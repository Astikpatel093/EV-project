// errors.hpp - exception types used across EV33.
//
// Policy (see docs/architecture.md, "Error handling"):
//  * Algorithms reject invalid arguments with std::invalid_argument (negative weights, mismatched
//    matrix dimensions, empty Huffman input, ...). Out-of-range vertex ids raise std::out_of_range.
//  * Loading data reports the first problem as ev::DataError carrying "file:line: reason".
//    A failed load never modifies the caller's existing Scenario object.
//  * Results that are legitimately empty (unreachable destination, nothing affordable) are NOT
//    exceptions; they are returned as status values so callers can explain them to the user.
#pragma once

#include <stdexcept>
#include <string>

namespace ev {

class DataError : public std::runtime_error {
public:
    DataError(const std::string &file, int line, const std::string &reason)
        : std::runtime_error(compose(file, line, reason)), file_(file), line_(line), reason_(reason) {}

    const std::string &file() const { return file_; }
    int line() const { return line_; }          // 0 when the problem is not tied to one line
    const std::string &reason() const { return reason_; }

private:
    static std::string compose(const std::string &file, int line, const std::string &reason) {
        std::string s = file;
        if (line > 0) s += ":" + std::to_string(line);
        return s + ": " + reason;
    }
    std::string file_;
    int line_;
    std::string reason_;
};

} // namespace ev
