#include "MRLspCompletion.hpp"

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

bool extractOptionalStringValue(const std::string &text, const std::string &key, std::string &value) {
	std::size_t pos = 0;

	value.clear();
	if (!findKeyValueStart(text, key, 0, pos)) return true;
	return parseJsonStringAt(text, pos, value);
}

bool extractOptionalIntValue(const std::string &text, const std::string &key, bool &present, int &value) {
	std::size_t pos = 0;

	present = false;
	value = 0;
	if (!findKeyValueStart(text, key, 0, pos)) return true;
	present = true;
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

	for (char ch : value) {
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

std::string buildCompletionRequestPayload(const LspCompletionRequest &request) {
	std::ostringstream out;

	out << "{\"jsonrpc\":\"2.0\",\"id\":" << request.idText << ",\"method\":\"textDocument/completion\",\"params\":{\"textDocument\":{\"uri\":";
	out << jsonString(request.uri);
	out << "},\"position\":{\"line\":" << request.position.line << ",\"character\":" << request.position.character << "}}}";
	return out.str();
}

bool parseCompletionItemObject(const std::string &object, LspCompletionItem &item) {
	if (!extractStringValue(object, "label", 0, item.label)) return false;
	if (!extractOptionalIntValue(object, "kind", item.hasKind, item.kind)) return false;
	if (!extractOptionalStringValue(object, "detail", item.detail)) return false;
	if (!extractOptionalStringValue(object, "insertText", item.insertText)) return false;
	return true;
}

bool parseCompletionItemsArray(const std::string &arrayText, std::vector<LspCompletionItem> &items, std::string &errorMessage) {
	std::size_t arrayEnd = 0;
	std::size_t pos = 1;

	if (arrayText.empty() || arrayText[0] != '[') return setError(errorMessage, "LSP completion response items are not an array.");
	if (!findMatchingBracket(arrayText, 0, '[', ']', arrayEnd)) return setError(errorMessage, "LSP completion response array is malformed.");
	items.clear();
	while (pos < arrayEnd) {
		skipWhitespace(arrayText, pos);
		if (pos >= arrayEnd) break;
		if (arrayText[pos] == ',') {
			++pos;
			continue;
		}
		if (arrayText[pos] != '{') return setError(errorMessage, "LSP completion item entry is malformed.");
		std::size_t objectEnd = 0;
		if (!findMatchingBracket(arrayText, pos, '{', '}', objectEnd) || objectEnd > arrayEnd) return setError(errorMessage, "LSP completion item object is malformed.");
		LspCompletionItem item;
		if (!parseCompletionItemObject(arrayText.substr(pos, objectEnd - pos + 1), item)) return setError(errorMessage, "LSP completion item fields are malformed.");
		items.push_back(item);
		pos = objectEnd + 1;
	}
	return true;
}

bool parseCompletionResult(const std::string &payload, LspCompletionResult &result, std::string &errorMessage) {
	std::size_t resultStart = 0;
	std::size_t resultEnd = 0;

	if (!findKeyValueStart(payload, "result", 0, resultStart)) return setError(errorMessage, "LSP completion response has no result.");
	if (resultStart >= payload.size()) return setError(errorMessage, "LSP completion response result is malformed.");
	if (payload[resultStart] == '[') {
		if (!findMatchingBracket(payload, resultStart, '[', ']', resultEnd)) return setError(errorMessage, "LSP completion response result array is malformed.");
		return parseCompletionItemsArray(payload.substr(resultStart, resultEnd - resultStart + 1), result.items, errorMessage);
	}
	if (payload[resultStart] == '{') {
		std::size_t itemsStart = 0;
		std::size_t itemsEnd = 0;
		if (!findMatchingBracket(payload, resultStart, '{', '}', resultEnd)) return setError(errorMessage, "LSP completion response result object is malformed.");
		const std::string resultObject = payload.substr(resultStart, resultEnd - resultStart + 1);
		if (!findKeyValueStart(resultObject, "items", 0, itemsStart) || itemsStart >= resultObject.size() || resultObject[itemsStart] != '[')
			return setError(errorMessage, "LSP completion list has no items array.");
		if (!findMatchingBracket(resultObject, itemsStart, '[', ']', itemsEnd)) return setError(errorMessage, "LSP completion list items are malformed.");
		return parseCompletionItemsArray(resultObject.substr(itemsStart, itemsEnd - itemsStart + 1), result.items, errorMessage);
	}
	return setError(errorMessage, "LSP completion response result has unsupported shape.");
}
} // namespace

bool LspCompletionAdapter::requestCompletion(LspLifecycle &lifecycle, const LspDocumentService &documentService, LspTextPosition position, LspCompletionRequest &request, std::string &errorMessage) {
	LspCompletionRequest candidate;

	if (!documentService.isOpen()) return setError(errorMessage, "LSP document service has no open document.");
	if (position.line < 0 || position.character < 0) return setError(errorMessage, "LSP completion position is negative.");
	candidate.idText = jsonString("mr-completion-" + std::to_string(nextRequestId));
	candidate.method = "textDocument/completion";
	candidate.uri = documentService.documentUri();
	candidate.position = position;
	candidate.pending = true;
	if (!lifecycle.sendInitializedPayload(buildCompletionRequestPayload(candidate), errorMessage)) return false;
	request = candidate;
	++nextRequestId;
	errorMessage.clear();
	return true;
}

bool LspCompletionAdapter::consume(const LspInboundMessage &message, const LspDocumentService &documentService, LspCompletionRequest &request, LspCompletionResult &result, bool &accepted, std::string &errorMessage) {
	accepted = false;
	result = LspCompletionResult();
	if (!request.pending) {
		errorMessage.clear();
		return true;
	}
	if (message.envelope.kind != JsonRpcMessageKind::Response || message.envelope.idText != request.idText) {
		errorMessage.clear();
		return true;
	}
	if (request.method != "textDocument/completion") return setError(errorMessage, "LSP completion request method mismatch.");
	if (request.uri != documentService.documentUri()) {
		request.pending = false;
		errorMessage.clear();
		return true;
	}
	result.uri = request.uri;
	if (!parseCompletionResult(message.payload, result, errorMessage)) return false;
	request.pending = false;
	accepted = true;
	errorMessage.clear();
	return true;
}

} // namespace mr::lsp
