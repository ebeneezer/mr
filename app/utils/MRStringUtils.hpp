#pragma once
#include <string>
#include <string_view>

std::string trimAscii(std::string_view value);
std::string upperAscii(std::string value);
bool isBlankString(std::string_view value);
bool looksLikeUri(std::string_view value);
