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

int hexDigitValue(char ch) noexcept {
	if (ch >= '0' && ch <= '9') return static_cast<int>(ch - '0');
	if (ch >= 'a' && ch <= 'f') return 10 + static_cast<int>(ch - 'a');
	if (ch >= 'A' && ch <= 'F') return 10 + static_cast<int>(ch - 'A');
	return -1;
}

bool parseJsonUnicodeCodeUnit(const std::string &text, std::size_t &pos, unsigned int &codeUnit) {
	unsigned int value = 0;

	if (pos + 4 > text.size()) return false;
	for (int i = 0; i < 4; ++i) {
		const int digit = hexDigitValue(text[pos + static_cast<std::size_t>(i)]);
		if (digit < 0) return false;
		value = (value << 4) | static_cast<unsigned int>(digit);
	}
	pos += 4;
	codeUnit = value;
	return true;
}

void appendUtf8CodePoint(std::string &value, unsigned int codePoint) {
	if (codePoint <= 0x7f) {
		value.push_back(static_cast<char>(codePoint));
		return;
	}
	if (codePoint <= 0x7ff) {
		value.push_back(static_cast<char>(0xc0 | ((codePoint >> 6) & 0x1f)));
		value.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
		return;
	}
	if (codePoint <= 0xffff) {
		value.push_back(static_cast<char>(0xe0 | ((codePoint >> 12) & 0x0f)));
		value.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
		value.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
		return;
	}
	if (codePoint <= 0x10ffff) {
		value.push_back(static_cast<char>(0xf0 | ((codePoint >> 18) & 0x07)));
		value.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
		value.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
		value.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
	}
}

bool parseJsonUnicodeEscape(const std::string &text, std::size_t &pos, std::string &value) {
	unsigned int codeUnit = 0;
	unsigned int codePoint = 0;

	if (!parseJsonUnicodeCodeUnit(text, pos, codeUnit)) return false;
	codePoint = codeUnit;
	if (codeUnit >= 0xd800 && codeUnit <= 0xdbff) {
		unsigned int lowCodeUnit = 0;
		const std::size_t lowEscapePos = pos;

		if (pos + 6 <= text.size() && text[pos] == '\\' && text[pos + 1] == 'u') {
			pos += 2;
			if (!parseJsonUnicodeCodeUnit(text, pos, lowCodeUnit)) return false;
			if (lowCodeUnit >= 0xdc00 && lowCodeUnit <= 0xdfff)
				codePoint = 0x10000 + (((codeUnit - 0xd800) << 10) | (lowCodeUnit - 0xdc00));
			else
				pos = lowEscapePos;
		}
	}
	if (codePoint >= 0xd800 && codePoint <= 0xdfff) codePoint = '?';
	appendUtf8CodePoint(value, codePoint);
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
			case 'b':
				value.push_back('\b');
				break;
			case 'f':
				value.push_back('\f');
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
			case 'u':
				if (!parseJsonUnicodeEscape(text, pos, value)) return false;
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

bool skipJsonValue(const std::string &text, std::size_t &pos) {
	skipWhitespace(text, pos);
	if (pos >= text.size()) return false;
	if (text[pos] == '"') {
		std::string ignored;
		return parseJsonStringAt(text, pos, ignored);
	}
	if (text[pos] == '{') {
		std::size_t end = 0;
		if (!findMatchingBracket(text, pos, '{', '}', end)) return false;
		pos = end + 1;
		return true;
	}
	if (text[pos] == '[') {
		std::size_t end = 0;
		if (!findMatchingBracket(text, pos, '[', ']', end)) return false;
		pos = end + 1;
		return true;
	}
	while (pos < text.size() && text[pos] != ',' && text[pos] != '}')
		++pos;
	return true;
}

bool findTopLevelObjectValueStart(const std::string &object, const std::string &key, std::size_t &valueStart) {
	std::size_t pos = 0;

	valueStart = 0;
	skipWhitespace(object, pos);
	if (pos >= object.size() || object[pos] != '{') return false;
	++pos;
	for (;;) {
		std::string memberName;

		skipWhitespace(object, pos);
		if (pos >= object.size()) return false;
		if (object[pos] == '}') return false;
		if (!parseJsonStringAt(object, pos, memberName)) return false;
		skipWhitespace(object, pos);
		if (pos >= object.size() || object[pos] != ':') return false;
		++pos;
		skipWhitespace(object, pos);
		if (memberName == key) {
			valueStart = pos;
			return true;
		}
		if (!skipJsonValue(object, pos)) return false;
		skipWhitespace(object, pos);
		if (pos >= object.size()) return false;
		if (object[pos] == ',') {
			++pos;
			continue;
		}
		if (object[pos] == '}') return false;
		return false;
	}
}

bool extractTopLevelStringValue(const std::string &object, const std::string &key, std::string &value) {
	std::size_t pos = 0;

	if (!findTopLevelObjectValueStart(object, key, pos)) return false;
	return parseJsonStringAt(object, pos, value);
}

bool extractOptionalTopLevelStringValue(const std::string &object, const std::string &key, std::string &value) {
	std::size_t pos = 0;

	value.clear();
	if (!findTopLevelObjectValueStart(object, key, pos)) return true;
	if (pos >= object.size() || object[pos] != '"') return true;
	return parseJsonStringAt(object, pos, value);
}

bool extractOptionalTopLevelIntValue(const std::string &object, const std::string &key, bool &present, int &value) {
	std::size_t pos = 0;

	present = false;
	value = 0;
	if (!findTopLevelObjectValueStart(object, key, pos)) return true;
	if (pos >= object.size() || object[pos] < '0' || object[pos] > '9') return true;
	present = true;
	return parseIntAt(object, pos, value);
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

std::string buildCompletionRequestPayload(const LspCompletionRequest &request) {
	std::ostringstream out;

	out << "{\"jsonrpc\":\"2.0\",\"id\":" << request.idText << ",\"method\":\"textDocument/completion\",\"params\":{\"textDocument\":{\"uri\":";
	out << jsonString(request.uri);
	out << "},\"position\":{\"line\":" << request.position.line << ",\"character\":" << request.position.character << "}}}";
	return out.str();
}

bool parseCompletionItemObject(const std::string &object, LspCompletionItem &item) {
	if (!extractTopLevelStringValue(object, "label", item.label)) return false;
	if (!extractOptionalTopLevelIntValue(object, "kind", item.hasKind, item.kind)) return false;
	if (!extractOptionalTopLevelStringValue(object, "detail", item.detail)) return false;
	if (!extractOptionalTopLevelStringValue(object, "insertText", item.insertText)) return false;
	if (!extractOptionalTopLevelIntValue(object, "insertTextFormat", item.hasInsertTextFormat, item.insertTextFormat)) return false;
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
	result.rawResponseJson = message.payload;
	request.pending = false;
	accepted = true;
	errorMessage.clear();
	return true;
}

} // namespace mr::lsp
