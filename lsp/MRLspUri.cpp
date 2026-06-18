#include "MRLspUri.hpp"

#include <cctype>

namespace mr::lsp {
namespace {
static const char kHexDigits[] = "0123456789ABCDEF";

bool setError(std::string &errorMessage, const std::string &message) {
	errorMessage = message;
	return false;
}

bool isUnreservedUriByte(unsigned char ch) {
	return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == '_' || ch == '~' || ch == '/';
}

int hexValue(unsigned char ch) {
	if (ch >= '0' && ch <= '9') return static_cast<int>(ch - '0');
	if (ch >= 'A' && ch <= 'F') return static_cast<int>(ch - 'A') + 10;
	if (ch >= 'a' && ch <= 'f') return static_cast<int>(ch - 'a') + 10;
	return -1;
}

std::string percentEncodePath(const std::string &path) {
	std::string encoded;

	for (std::size_t index = 0; index < path.size(); ++index) {
		const unsigned char ch = static_cast<unsigned char>(path[index]);
		if (isUnreservedUriByte(ch)) {
			encoded.push_back(static_cast<char>(ch));
		} else {
			encoded.push_back('%');
			encoded.push_back(kHexDigits[ch >> 4]);
			encoded.push_back(kHexDigits[ch & 0x0f]);
		}
	}
	return encoded;
}

bool percentDecodePath(const std::string &encoded, std::string &decoded, std::string &errorMessage) {
	decoded.clear();
	for (std::size_t i = 0; i < encoded.size(); ++i) {
		const unsigned char ch = static_cast<unsigned char>(encoded[i]);
		if (ch != '%') {
			decoded.push_back(static_cast<char>(ch));
			continue;
		}
		if (i + 2 >= encoded.size()) return setError(errorMessage, "File URI contains incomplete percent escape.");
		const int high = hexValue(static_cast<unsigned char>(encoded[i + 1]));
		const int low = hexValue(static_cast<unsigned char>(encoded[i + 2]));
		if (high < 0 || low < 0) return setError(errorMessage, "File URI contains invalid percent escape.");
		const char decodedByte = static_cast<char>((high << 4) | low);
		if (decodedByte == '\0') return setError(errorMessage, "File URI decodes to a NUL byte.");
		decoded.push_back(decodedByte);
		i += 2;
	}
	return true;
}

bool startsWith(const std::string &text, const std::string &prefix) {
	return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}
} // namespace

bool pathToFileUri(const std::string &absolutePath, std::string &uri, std::string &errorMessage) {
	uri.clear();
	if (absolutePath.empty()) return setError(errorMessage, "Path is empty.");
	if (absolutePath.front() != '/') return setError(errorMessage, "Path is not absolute.");
	for (std::size_t index = 0; index < absolutePath.size(); ++index) {
		if (absolutePath[index] == '\0') return setError(errorMessage, "Path contains a NUL byte.");
	}
	uri = "file://";
	uri += percentEncodePath(absolutePath);
	errorMessage.clear();
	return true;
}

bool fileUriToPath(const std::string &uri, std::string &path, std::string &errorMessage) {
	path.clear();
	if (!startsWith(uri, "file://")) return setError(errorMessage, "URI is not a file URI.");
	std::string pathPart = uri.substr(7);
	if (startsWith(pathPart, "localhost/")) pathPart = pathPart.substr(9);
	if (pathPart.empty() || pathPart.front() != '/') return setError(errorMessage, "File URI is not local absolute.");
	if (pathPart.find('?') != std::string::npos || pathPart.find('#') != std::string::npos)
		return setError(errorMessage, "File URI contains query or fragment.");
	if (!percentDecodePath(pathPart, path, errorMessage)) return false;
	if (path.empty() || path.front() != '/') return setError(errorMessage, "File URI did not decode to an absolute path.");
	errorMessage.clear();
	return true;
}

} // namespace mr::lsp
