#include "MRStringUtils.hpp"
#include <algorithm>
#include <cctype>

std::string trimAscii(std::string_view value) {
    if (value.empty()) return "";
    auto start = value.begin();
    while (start != value.end() && std::isspace(static_cast<unsigned char>(*start))) {
        ++start;
    }
    if (start == value.end()) return "";
    auto end = value.end() - 1;
    while (end > start && std::isspace(static_cast<unsigned char>(*end))) {
        --end;
    }
    return std::string(start, end + 1);
}

std::string upperAscii(std::string value) {
    for (char &c : value) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return value;
}
