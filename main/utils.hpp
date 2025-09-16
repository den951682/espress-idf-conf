#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace utils {

inline void trim(std::string &s) {
    while (!s.empty() && (s.front() == '\r' || s.front() == '\n')) {
        s.erase(s.begin());
    }
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) {
        s.pop_back();
    }
}

inline std::string toCleanString(const std::vector<uint8_t>& bytes) {
    std::string s(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    trim(s);
    return s;
}

} 
