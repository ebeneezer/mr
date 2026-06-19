#include "MRLspDocumentHighlight.hpp"

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

bool parseIntAt(const std::string &text, std::size_t &pos, int &value) {
	char *end = nullptr;
	const char *start = text.c_str() + pos;
	const long parsed = std::strtol(start, &end, 10);

	if (end == start) return false;
	value = static_cast<int>(parsed);
	pos = static_cast<std::size_t>(end - text.c_str());
	return true;
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

std::string buildDocumentHighlightRequestPayload(const LspDocumentHighlightRequest &request) {
	std::ostringstream out;

	out << "{\"jsonrpc\":\"2.0\",\"id\":" << request.idText << ",\"method\":\"textDocument/documentHighlight\",\"params\":{\"textDocument\":{\"uri\":";
	out << jsonString(request.uri);
	out << "},\"position\":{\"line\":" << request.position.line << ",\"character\":" << request.position.character << "}}}";
	return out.str();
}

bool parsePositionObject(const std::string &text, LspTextPosition &position) {
	return extractIntValue(text, "line", 0, position.line) && extractIntValue(text, "character", 0, position.character);
}

bool parseDocumentHighlightObject(const std::string &object, LspDocumentHighlightRange &highlight) {
	std::size_t rangeStart = 0;
	std::size_t rangeEnd = 0;
	std::size_t startStart = 0;
	std::size_t startEnd = 0;
	std::size_t endStart = 0;
	std::size_t endEnd = 0;
	int kind = 0;

	if (!findKeyValueStart(object, "range", 0, rangeStart) || rangeStart >= object.size() || object[rangeStart] != '{') return false;
	if (!findMatchingBracket(object, rangeStart, '{', '}', rangeEnd)) return false;
	const std::string range = object.substr(rangeStart, rangeEnd - rangeStart + 1);
	if (!findKeyValueStart(range, "start", 0, startStart) || startStart >= range.size() || range[startStart] != '{') return false;
	if (!findMatchingBracket(range, startStart, '{', '}', startEnd)) return false;
	if (!findKeyValueStart(range, "end", 0, endStart) || endStart >= range.size() || range[endStart] != '{') return false;
	if (!findMatchingBracket(range, endStart, '{', '}', endEnd)) return false;
	if (!parsePositionObject(range.substr(startStart, startEnd - startStart + 1), highlight.start) || !parsePositionObject(range.substr(endStart, endEnd - endStart + 1), highlight.end)) return false;
	if (extractIntValue(object, "kind", rangeEnd + 1, kind)) {
		highlight.hasKind = true;
		highlight.kind = kind;
	}
	return true;
}

bool parseDocumentHighlights(const std::string &payload, std::vector<LspDocumentHighlightRange> &highlights, std::string &errorMessage) {
	std::size_t arrayStart = 0;
	std::size_t arrayEnd = 0;
	std::size_t pos = 0;

	highlights.clear();
	if (!findKeyValueStart(payload, "result", 0, arrayStart) || arrayStart >= payload.size()) return setError(errorMessage, "LSP documentHighlight response result is not an array.");
	if (payload.compare(arrayStart, 4, "null") == 0) return true;
	if (payload[arrayStart] != '[') return setError(errorMessage, "LSP documentHighlight response result is not an array.");
	if (!findMatchingBracket(payload, arrayStart, '[', ']', arrayEnd)) return setError(errorMessage, "LSP documentHighlight response array is malformed.");
	pos = arrayStart + 1;
	while (pos < arrayEnd) {
		skipWhitespace(payload, pos);
		if (pos >= arrayEnd) break;
		if (payload[pos] == ',') {
			++pos;
			continue;
		}
		if (payload[pos] != '{') return setError(errorMessage, "LSP documentHighlight entry is malformed.");
		std::size_t objectEnd = 0;
		if (!findMatchingBracket(payload, pos, '{', '}', objectEnd) || objectEnd > arrayEnd) return setError(errorMessage, "LSP documentHighlight object is malformed.");
		LspDocumentHighlightRange highlight;
		if (!parseDocumentHighlightObject(payload.substr(pos, objectEnd - pos + 1), highlight)) return setError(errorMessage, "LSP documentHighlight fields are malformed.");
		highlights.push_back(highlight);
		pos = objectEnd + 1;
	}
	return true;
}
} // namespace

bool LspDocumentHighlightAdapter::requestDocumentHighlight(LspLifecycle &lifecycle, const LspDocumentService &documentService, LspTextPosition position, LspDocumentHighlightRequest &request, std::string &errorMessage) {
	LspDocumentHighlightRequest candidate;

	if (!documentService.isOpen()) return setError(errorMessage, "LSP document service has no open document.");
	if (position.line < 0 || position.character < 0) return setError(errorMessage, "LSP documentHighlight position is negative.");
	candidate.idText = jsonString("mr-document-highlight-" + std::to_string(nextRequestId));
	candidate.method = "textDocument/documentHighlight";
	candidate.uri = documentService.documentUri();
	candidate.position = position;
	candidate.pending = true;
	if (!lifecycle.sendInitializedPayload(buildDocumentHighlightRequestPayload(candidate), errorMessage)) return false;
	request = candidate;
	++nextRequestId;
	errorMessage.clear();
	return true;
}

bool LspDocumentHighlightAdapter::consume(const LspInboundMessage &message, const LspDocumentService &documentService, LspDocumentHighlightRequest &request, LspDocumentHighlightResult &result, bool &accepted, std::string &errorMessage) {
	accepted = false;
	result = LspDocumentHighlightResult();
	if (!request.pending) {
		errorMessage.clear();
		return true;
	}
	if (message.envelope.kind != JsonRpcMessageKind::Response || message.envelope.idText != request.idText) {
		errorMessage.clear();
		return true;
	}
	if (request.method != "textDocument/documentHighlight") return setError(errorMessage, "LSP documentHighlight request method mismatch.");
	if (request.uri != documentService.documentUri()) {
		request.pending = false;
		errorMessage.clear();
		return true;
	}
	result.uri = request.uri;
	if (!parseDocumentHighlights(message.payload, result.highlights, errorMessage)) return false;
	request.pending = false;
	accepted = true;
	errorMessage.clear();
	return true;
}

} // namespace mr::lsp
