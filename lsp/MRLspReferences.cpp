#include "MRLspReferences.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace mr::lsp {
namespace {
bool setError(std::string &errorMessage, const std::string &message) {
	errorMessage = message;
	return false;
}

void skipWhitespace(const std::string &text, std::size_t &pos) noexcept {
	while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0)
		++pos;
}

bool findKeyValueStart(const std::string &text, const std::string &key, std::size_t startPos, std::size_t &valueStart) {
	const std::string quotedKey = "\"" + key + "\"";
	const std::size_t keyPos = text.find(quotedKey, startPos);

	if (keyPos == std::string::npos) return false;
	std::size_t pos = keyPos + quotedKey.size();
	skipWhitespace(text, pos);
	if (pos >= text.size() || text[pos] != ':') return false;
	++pos;
	skipWhitespace(text, pos);
	valueStart = pos;
	return true;
}

bool parseJsonStringAt(const std::string &text, std::size_t &pos, std::string &value) {
	value.clear();
	if (pos >= text.size() || text[pos] != '"') return false;
	++pos;
	while (pos < text.size()) {
		const char ch = text[pos++];
		if (ch == '"') return true;
		if (ch != '\\') {
			value.push_back(ch);
			continue;
		}
		if (pos >= text.size()) return false;
		const char escaped = text[pos++];
		switch (escaped) {
			case '"':
			case '\\':
			case '/':
				value.push_back(escaped);
				break;
			case 'n':
				value.push_back('\n');
				break;
			case 'r':
				value.push_back('\r');
				break;
			case 't':
				value.push_back('\t');
				break;
			default:
				return false;
		}
	}
	return false;
}

bool parseIntAt(const std::string &text, std::size_t &pos, int &value) {
	char *end = nullptr;
	const char *start = text.c_str() + pos;
	const long parsed = std::strtol(start, &end, 10);

	if (end == start) return false;
	value = static_cast<int>(parsed);
	pos = static_cast<std::size_t>(end - text.c_str());
	return true;
}

bool extractStringValue(const std::string &text, const std::string &key, std::size_t startPos, std::string &value) {
	std::size_t pos = 0;

	if (!findKeyValueStart(text, key, startPos, pos)) return false;
	return parseJsonStringAt(text, pos, value);
}

bool extractIntValue(const std::string &text, const std::string &key, std::size_t startPos, int &value) {
	std::size_t pos = 0;

	if (!findKeyValueStart(text, key, startPos, pos)) return false;
	return parseIntAt(text, pos, value);
}

bool findMatchingBracket(const std::string &text, std::size_t openPos, char openChar, char closeChar, std::size_t &closePos) {
	int depth = 0;
	bool inString = false;
	bool escaped = false;

	for (std::size_t pos = openPos; pos < text.size(); ++pos) {
		const char ch = text[pos];
		if (inString) {
			if (escaped) escaped = false;
			else if (ch == '\\') escaped = true;
			else if (ch == '"') inString = false;
			continue;
		}
		if (ch == '"') {
			inString = true;
			continue;
		}
		if (ch == openChar) {
			++depth;
			continue;
		}
		if (ch == closeChar) {
			--depth;
			if (depth == 0) {
				closePos = pos;
				return true;
			}
		}
	}
	return false;
}

std::string jsonString(const std::string &value) {
	std::string out = "\"";

	for (std::size_t index = 0; index < value.size(); ++index) {
		const char ch = value[index];
		if (ch == '"' || ch == '\\') {
			out.push_back('\\');
			out.push_back(ch);
		} else if (ch == '\n') {
			out += "\\n";
		} else if (ch == '\r') {
			out += "\\r";
		} else if (ch == '\t') {
			out += "\\t";
		} else {
			out.push_back(ch);
		}
	}
	out.push_back('"');
	return out;
}

std::string buildReferencesRequestPayload(const LspReferencesRequest &request) {
	std::ostringstream out;

	out << "{\"jsonrpc\":\"2.0\",\"id\":" << request.idText << ",\"method\":\"textDocument/references\",\"params\":{\"textDocument\":{\"uri\":";
	out << jsonString(request.uri);
	out << "},\"position\":{\"line\":" << request.position.line << ",\"character\":" << request.position.character << "},\"context\":{\"includeDeclaration\":";
	out << (request.includeDeclaration ? "true" : "false");
	out << "}}}";
	return out.str();
}

bool parsePositionObject(const std::string &text, LspTextPosition &position) {
	return extractIntValue(text, "line", 0, position.line) && extractIntValue(text, "character", 0, position.character);
}

bool parseLocationObject(const std::string &object, LspLocation &location) {
	std::size_t rangeStart = 0;
	std::size_t rangeEnd = 0;
	std::size_t startStart = 0;
	std::size_t startEnd = 0;
	std::size_t endStart = 0;
	std::size_t endEnd = 0;

	if (!extractStringValue(object, "uri", 0, location.uri)) return false;
	if (!findKeyValueStart(object, "range", 0, rangeStart) || rangeStart >= object.size() || object[rangeStart] != '{') return false;
	if (!findMatchingBracket(object, rangeStart, '{', '}', rangeEnd)) return false;
	const std::string range = object.substr(rangeStart, rangeEnd - rangeStart + 1);
	if (!findKeyValueStart(range, "start", 0, startStart) || startStart >= range.size() || range[startStart] != '{') return false;
	if (!findMatchingBracket(range, startStart, '{', '}', startEnd)) return false;
	if (!findKeyValueStart(range, "end", 0, endStart) || endStart >= range.size() || range[endStart] != '{') return false;
	if (!findMatchingBracket(range, endStart, '{', '}', endEnd)) return false;
	return parsePositionObject(range.substr(startStart, startEnd - startStart + 1), location.start) && parsePositionObject(range.substr(endStart, endEnd - endStart + 1), location.end);
}

bool parseReferences(const std::string &payload, std::vector<LspLocation> &locations, std::string &errorMessage) {
	std::size_t arrayStart = 0;
	std::size_t arrayEnd = 0;
	std::size_t pos = 0;

	locations.clear();
	if (!findKeyValueStart(payload, "result", 0, arrayStart) || arrayStart >= payload.size()) return setError(errorMessage, "LSP references response result is not an array.");
	if (payload.compare(arrayStart, 4, "null") == 0) return true;
	if (payload[arrayStart] != '[') return setError(errorMessage, "LSP references response result is not an array.");
	if (!findMatchingBracket(payload, arrayStart, '[', ']', arrayEnd)) return setError(errorMessage, "LSP references response array is malformed.");
	pos = arrayStart + 1;
	while (pos < arrayEnd) {
		skipWhitespace(payload, pos);
		if (pos >= arrayEnd) break;
		if (payload[pos] == ',') {
			++pos;
			continue;
		}
		if (payload[pos] != '{') return setError(errorMessage, "LSP references location entry is malformed.");
		std::size_t objectEnd = 0;
		if (!findMatchingBracket(payload, pos, '{', '}', objectEnd) || objectEnd > arrayEnd) return setError(errorMessage, "LSP references location object is malformed.");
		LspLocation location;
		if (!parseLocationObject(payload.substr(pos, objectEnd - pos + 1), location)) return setError(errorMessage, "LSP references location fields are malformed.");
		locations.push_back(location);
		pos = objectEnd + 1;
	}
	return true;
}
} // namespace

bool LspReferencesAdapter::requestReferences(LspLifecycle &lifecycle, const LspDocumentService &documentService, LspTextPosition position, bool includeDeclaration, LspReferencesRequest &request, std::string &errorMessage) {
	LspReferencesRequest candidate;

	if (!documentService.isOpen()) return setError(errorMessage, "LSP document service has no open document.");
	if (position.line < 0 || position.character < 0) return setError(errorMessage, "LSP references position is negative.");
	candidate.idText = jsonString("mr-references-" + std::to_string(nextRequestId));
	candidate.method = "textDocument/references";
	candidate.uri = documentService.documentUri();
	candidate.position = position;
	candidate.includeDeclaration = includeDeclaration;
	candidate.pending = true;
	if (!lifecycle.sendInitializedPayload(buildReferencesRequestPayload(candidate), errorMessage)) return false;
	request = candidate;
	++nextRequestId;
	errorMessage.clear();
	return true;
}

bool LspReferencesAdapter::consume(const LspInboundMessage &message, const LspDocumentService &documentService, LspReferencesRequest &request, LspReferencesResult &result, bool &accepted, std::string &errorMessage) {
	accepted = false;
	result = LspReferencesResult();
	if (!request.pending) {
		errorMessage.clear();
		return true;
	}
	if (message.envelope.kind != JsonRpcMessageKind::Response || message.envelope.idText != request.idText) {
		errorMessage.clear();
		return true;
	}
	if (request.method != "textDocument/references") return setError(errorMessage, "LSP references request method mismatch.");
	if (request.uri != documentService.documentUri()) {
		request.pending = false;
		errorMessage.clear();
		return true;
	}
	result.originUri = request.uri;
	if (!parseReferences(message.payload, result.locations, errorMessage)) return false;
	request.pending = false;
	accepted = true;
	errorMessage.clear();
	return true;
}

} // namespace mr::lsp
