#include "MRFileIOUtils.hpp"
#include <fstream>
#include <sstream>

namespace {

bool readTextFileImpl(const std::string &path, std::string &out, std::string *outError) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        if (outError != nullptr)
            *outError = "Could not open file: " + path;
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    if (!file.good() && !file.eof()) {
        if (outError != nullptr)
            *outError = "Error while reading file: " + path;
        return false;
    }
    out = buffer.str();
    if (outError != nullptr)
        outError->clear();
    return true;
}

} // namespace

bool readTextFile(const std::string &path, std::string &out) {
    return readTextFileImpl(path, out, nullptr);
}

bool readTextFile(const std::string &path, std::string &out, std::string &outError) {
    return readTextFileImpl(path, out, &outError);
}

bool writeTextFile(std::string_view path, std::string_view content) {
    std::ofstream file(std::string(path), std::ios::out | std::ios::trunc | std::ios::binary);
    if (!file.is_open()) return false;
    file << content;
    return file.good();
}

bool writeTextFile(const std::string &path, const std::string &content) { return writeTextFile(std::string_view(path), std::string_view(content)); }
