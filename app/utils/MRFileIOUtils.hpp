#pragma once
#include <string>
#include <string_view>

bool readTextFile(const std::string &path, std::string &out);
bool readTextFile(const std::string &path, std::string &out, std::string &outError);
bool writeTextFile(std::string_view path, std::string_view content);
