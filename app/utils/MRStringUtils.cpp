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

bool isBlankString(std::string_view value) {
    for (char c : value) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            return false;
        }
    }
    return true;
}

bool looksLikeUri(std::string_view value) {
    std::string trimmed = trimAscii(value);
    std::size_t schemeSep = trimmed.find("://");

    if (schemeSep == std::string::npos || schemeSep == 0)
        return false;
    for (std::size_t i = 0; i < schemeSep; ++i) {
        unsigned char ch = static_cast<unsigned char>(trimmed[i]);
        if (std::isalnum(ch) == 0 && ch != '+' && ch != '-' && ch != '.')
            return false;
    }
    return true;
}
