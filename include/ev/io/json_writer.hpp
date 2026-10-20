// json_writer.hpp - minimal streaming JSON writer (objects, arrays, numbers, strings, booleans).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ev {

class JsonWriter {
public:
    JsonWriter() { first_.push_back(1); }

    void begin_object(const char *key = nullptr) { open(key, '{'); }
    void end_object() { close('}'); }
    void begin_array(const char *key = nullptr) { open(key, '['); }
    void end_array() { close(']'); }

    void number(const char *key, double v);
    void integer(const char *key, std::int64_t v);
    void string(const char *key, const std::string &v);
    void boolean(const char *key, bool v);

    const std::string &str() const { return out_; }

private:
    void key(const char *k);
    void open(const char *k, char c);
    void close(char c);
    std::string out_;
    std::vector<char> first_;   // one flag per open object/array: "no member written yet"
};

} // namespace ev
