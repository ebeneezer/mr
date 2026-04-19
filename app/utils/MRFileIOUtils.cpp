#include "MRFileIOUtils.hpp"
#include <fstream>
#include <sstream>

bool readTextFile(const std::string &path, std::string &out) {
    std::string ignoredError;
    return readTextFile(path, out, ignoredError);
}

bool readTextFile(const std::string &path, std::string &out, std::string &outError) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        outError = "Could not open file: " + path;
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    if (!file.good() && !file.eof()) {
        outError = "Error while reading file: " + path;
        return false;
    }
    out = buffer.str();
    outError.clear();
    return true;
}

bool writeTextFile(std::string_view path, std::string_view content) {
    std::ofstream file(std::string(path), std::ios::out | std::ios::trunc | std::ios::binary);
    if (!file.is_open()) return false;
    file << content;
    return file.good();
}

// ABI compatibility shim for objects compiled against the previous signature.
bool writeTextFile(const std::string &path, const std::string &content) {
    return writeTextFile(std::string_view(path), std::string_view(content));
}
