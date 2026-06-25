#include "MRLspDefinition.hpp"

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

std::string buildDefinitionRequestPayload(const LspDefinitionRequest &request) {
	std::ostringstream out;

	out << "{\"jsonrpc\":\"2.0\",\"id\":" << request.idText << ",\"method\":\"textDocument/definition\",\"params\":{\"textDocument\":{\"uri\":";
	out << jsonString(request.uri);
	out << "},\"position\":{\"line\":" << request.position.line << ",\"character\":" << request.position.character << "}}}";
	return out.str();
}

bool parsePositionObject(const std::string &text, LspTextPosition &position) {
	return extractIntValue(text, "line", 0, position.line) && extractIntValue(text, "character", 0, position.character);
}

bool parseRangeObject(const std::string &object, LspLocation &location) {
	std::size_t rangeStart = 0;
	std::size_t rangeEnd = 0;
	std::size_t startStart = 0;
	std::size_t startEnd = 0;
	std::size_t endStart = 0;
	std::size_t endEnd = 0;

	if (!findKeyValueStart(object, "range", 0, rangeStart) || rangeStart >= object.size() || object[rangeStart] != '{') return false;
	if (!findMatchingBracket(object, rangeStart, '{', '}', rangeEnd)) return false;
	const std::string range = object.substr(rangeStart, rangeEnd - rangeStart + 1);
	if (!findKeyValueStart(range, "start", 0, startStart) || startStart >= range.size() || range[startStart] != '{') return false;
	if (!findMatchingBracket(range, startStart, '{', '}', startEnd)) return false;
	if (!findKeyValueStart(range, "end", 0, endStart) || endStart >= range.size() || range[endStart] != '{') return false;
	if (!findMatchingBracket(range, endStart, '{', '}', endEnd)) return false;
	return parsePositionObject(range.substr(startStart, startEnd - startStart + 1), location.start) && parsePositionObject(range.substr(endStart, endEnd - endStart + 1), location.end);
}

bool parseNamedRangeObject(const std::string &object, const std::string &rangeKey, LspLocation &location) {
	std::size_t rangeStart = 0;
	std::size_t rangeEnd = 0;

	if (!findKeyValueStart(object, rangeKey, 0, rangeStart) || rangeStart >= object.size() || object[rangeStart] != '{') return false;
	if (!findMatchingBracket(object, rangeStart, '{', '}', rangeEnd)) return false;
	const std::string rangeObject = "{\"range\":" + object.substr(rangeStart, rangeEnd - rangeStart + 1) + "}";
	return parseRangeObject(rangeObject, location);
}

bool parseLocationObject(const std::string &object, LspLocation &location) {
	if (!extractStringValue(object, "uri", 0, location.uri)) return false;
	return parseRangeObject(object, location);
}

bool parseLocationLinkObject(const std::string &object, LspLocation &location) {
	if (!extractStringValue(object, "targetUri", 0, location.uri)) return false;
	if (parseNamedRangeObject(object, "targetSelectionRange", location)) return true;
	return parseNamedRangeObject(object, "targetRange", location);
}

bool parseDefinitionObject(const std::string &object, LspLocation &location) {
	if (parseLocationObject(object, location)) return true;
	return parseLocationLinkObject(object, location);
}

bool parseDefinitionResultArray(const std::string &arrayText, std::vector<LspLocation> &locations) {
	std::size_t arrayEnd = 0;
	std::size_t pos = 1;

	if (arrayText.empty() || arrayText[0] != '[') return false;
	if (!findMatchingBracket(arrayText, 0, '[', ']', arrayEnd)) return false;
	while (pos < arrayEnd) {
		skipWhitespace(arrayText, pos);
		if (pos >= arrayEnd) break;
		if (arrayText[pos] == ',') {
			++pos;
			continue;
		}
		if (arrayText[pos] != '{') return false;
		std::size_t objectEnd = 0;
		if (!findMatchingBracket(arrayText, pos, '{', '}', objectEnd) || objectEnd > arrayEnd) return false;
		LspLocation location;
		if (!parseDefinitionObject(arrayText.substr(pos, objectEnd - pos + 1), location)) return false;
		locations.push_back(location);
		pos = objectEnd + 1;
	}
	return true;
}

bool parseDefinitionResult(const std::string &payload, std::vector<LspLocation> &locations) {
	std::size_t resultStart = 0;
	std::size_t resultEnd = 0;

	locations.clear();
	if (!findKeyValueStart(payload, "result", 0, resultStart) || resultStart >= payload.size()) return false;
	if (payload.compare(resultStart, 4, "null") == 0) return true;
	if (payload[resultStart] == '{') {
		if (!findMatchingBracket(payload, resultStart, '{', '}', resultEnd)) return false;
		LspLocation location;
		if (!parseDefinitionObject(payload.substr(resultStart, resultEnd - resultStart + 1), location)) return false;
		locations.push_back(location);
		return true;
	}
	if (payload[resultStart] == '[') {
		if (!findMatchingBracket(payload, resultStart, '[', ']', resultEnd)) return false;
		return parseDefinitionResultArray(payload.substr(resultStart, resultEnd - resultStart + 1), locations);
	}
	return false;
}
} // namespace

bool LspDefinitionAdapter::requestDefinition(LspLifecycle &lifecycle, const LspDocumentService &documentService, LspTextPosition position, LspDefinitionRequest &request, std::string &errorMessage) {
	LspDefinitionRequest candidate;

	if (!documentService.isOpen()) return setError(errorMessage, "LSP document service has no open document.");
	if (position.line < 0 || position.character < 0) return setError(errorMessage, "LSP definition position is negative.");
	candidate.idText = jsonString("mr-definition-" + std::to_string(nextRequestId));
	candidate.method = "textDocument/definition";
	candidate.uri = documentService.documentUri();
	candidate.position = position;
	candidate.pending = true;
	if (!lifecycle.sendInitializedPayload(buildDefinitionRequestPayload(candidate), errorMessage)) return false;
	request = candidate;
	++nextRequestId;
	errorMessage.clear();
	return true;
}

bool LspDefinitionAdapter::consume(const LspInboundMessage &message, const LspDocumentService &documentService, LspDefinitionRequest &request, LspDefinitionResult &result, bool &accepted, std::string &errorMessage) {
	accepted = false;
	result = LspDefinitionResult();
	if (!request.pending) {
		errorMessage.clear();
		return true;
	}
	if (message.envelope.kind != JsonRpcMessageKind::Response || message.envelope.idText != request.idText) {
		errorMessage.clear();
		return true;
	}
	if (request.method != "textDocument/definition") return setError(errorMessage, "LSP definition request method mismatch.");
	if (request.uri != documentService.documentUri()) {
		request.pending = false;
		errorMessage.clear();
		return true;
	}
	result.originUri = request.uri;
	if (message.envelope.hasError) {
		request.pending = false;
		accepted = true;
		errorMessage.clear();
		return true;
	}
	if (!parseDefinitionResult(message.payload, result.locations)) return setError(errorMessage, "LSP definition response location is malformed.");
	request.pending = false;
	accepted = true;
	errorMessage.clear();
	return true;
}

} // namespace mr::lsp
