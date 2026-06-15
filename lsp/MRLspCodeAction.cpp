#include "MRLspCodeAction.hpp"

#include <cctype>
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

bool hasTopLevelObjectKey(const std::string &object, const std::string &key) {
	std::size_t pos = 0;

	return findTopLevelObjectValueStart(object, key, pos);
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

bool diagnosticJsonIsObject(const std::string &diagnosticJson) {
	std::size_t pos = 0;
	std::size_t end = 0;

	if (diagnosticJson.empty()) return true;
	skipWhitespace(diagnosticJson, pos);
	if (pos >= diagnosticJson.size() || diagnosticJson[pos] != '{') return false;
	if (!findMatchingBracket(diagnosticJson, pos, '{', '}', end)) return false;
	++end;
	skipWhitespace(diagnosticJson, end);
	return end == diagnosticJson.size();
}

void appendPosition(std::ostringstream &out, const LspTextPosition &position) {
	out << "{\"line\":" << position.line << ",\"character\":" << position.character << "}";
}

std::string buildCodeActionRequestPayload(const LspCodeActionRequest &request) {
	std::ostringstream out;

	out << "{\"jsonrpc\":\"2.0\",\"id\":" << request.idText << ",\"method\":\"textDocument/codeAction\",\"params\":{\"textDocument\":{\"uri\":";
	out << jsonString(request.uri);
	out << "},\"range\":{\"start\":";
	appendPosition(out, request.range.start);
	out << ",\"end\":";
	appendPosition(out, request.range.end);
	out << "},\"context\":{\"diagnostics\":[";
	if (!request.diagnosticJson.empty()) out << request.diagnosticJson;
	out << "]}}}";
	return out.str();
}

bool parseCodeActionObject(const std::string &object, LspCodeActionItem &item) {
	if (!extractTopLevelStringValue(object, "title", item.title)) return false;
	if (!extractOptionalTopLevelStringValue(object, "kind", item.kind)) return false;
	item.hasEdit = hasTopLevelObjectKey(object, "edit");
	item.hasCommand = hasTopLevelObjectKey(object, "command");
	item.rawJson = object;
	return true;
}

bool parseCodeActionResultArray(const std::string &arrayText, std::vector<LspCodeActionItem> &items, std::string &errorMessage) {
	std::size_t arrayEnd = 0;
	std::size_t pos = 1;

	if (arrayText.empty() || arrayText[0] != '[') return setError(errorMessage, "LSP codeAction response result is not an array.");
	if (!findMatchingBracket(arrayText, 0, '[', ']', arrayEnd)) return setError(errorMessage, "LSP codeAction response array is malformed.");
	items.clear();
	while (pos < arrayEnd) {
		skipWhitespace(arrayText, pos);
		if (pos >= arrayEnd) break;
		if (arrayText[pos] == ',') {
			++pos;
			continue;
		}
		if (arrayText[pos] != '{') return setError(errorMessage, "LSP codeAction item is malformed.");
		std::size_t objectEnd = 0;
		if (!findMatchingBracket(arrayText, pos, '{', '}', objectEnd) || objectEnd > arrayEnd) return setError(errorMessage, "LSP codeAction item object is malformed.");
		LspCodeActionItem item;
		if (!parseCodeActionObject(arrayText.substr(pos, objectEnd - pos + 1), item)) return setError(errorMessage, "LSP codeAction item fields are malformed.");
		items.push_back(item);
		pos = objectEnd + 1;
	}
	return true;
}

bool parseCodeActionResult(const std::string &payload, std::vector<LspCodeActionItem> &items, std::string &errorMessage) {
	std::size_t resultStart = 0;
	std::size_t resultEnd = 0;

	items.clear();
	if (!findKeyValueStart(payload, "result", 0, resultStart) || resultStart >= payload.size()) return setError(errorMessage, "LSP codeAction response is missing result.");
	if (payload.compare(resultStart, 4, "null") == 0) return true;
	if (payload[resultStart] != '[') return setError(errorMessage, "LSP codeAction response result is malformed.");
	if (!findMatchingBracket(payload, resultStart, '[', ']', resultEnd)) return setError(errorMessage, "LSP codeAction response result array is malformed.");
	return parseCodeActionResultArray(payload.substr(resultStart, resultEnd - resultStart + 1), items, errorMessage);
}
} // namespace

bool LspCodeActionAdapter::requestCodeActions(LspLifecycle &lifecycle, const LspDocumentService &documentService, LspCodeActionRange range, const std::string &diagnosticJson, LspCodeActionRequest &request, std::string &errorMessage) {
	LspCodeActionRequest candidate;

	if (!documentService.isOpen()) return setError(errorMessage, "LSP document service has no open document.");
	if (range.start.line < 0 || range.start.character < 0 || range.end.line < 0 || range.end.character < 0) return setError(errorMessage, "LSP codeAction range is negative.");
	if (!diagnosticJsonIsObject(diagnosticJson)) return setError(errorMessage, "LSP codeAction diagnostic JSON is malformed.");
	candidate.idText = jsonString("mr-code-action-" + std::to_string(nextRequestId));
	candidate.method = "textDocument/codeAction";
	candidate.uri = documentService.documentUri();
	candidate.range = range;
	candidate.diagnosticJson = diagnosticJson;
	candidate.pending = true;
	if (!lifecycle.sendInitializedPayload(buildCodeActionRequestPayload(candidate), errorMessage)) return false;
	request = candidate;
	++nextRequestId;
	errorMessage.clear();
	return true;
}

bool LspCodeActionAdapter::consume(const LspInboundMessage &message, const LspDocumentService &documentService, LspCodeActionRequest &request, LspCodeActionResult &result, bool &accepted, std::string &errorMessage) {
	accepted = false;
	result = LspCodeActionResult();
	if (!request.pending) {
		errorMessage.clear();
		return true;
	}
	if (message.envelope.kind != JsonRpcMessageKind::Response || message.envelope.idText != request.idText) {
		errorMessage.clear();
		return true;
	}
	if (request.method != "textDocument/codeAction") return setError(errorMessage, "LSP codeAction request method mismatch.");
	if (request.uri != documentService.documentUri()) {
		request.pending = false;
		return setError(errorMessage, "LSP codeAction response URI no longer matches document service.");
	}
	if (!parseCodeActionResult(message.payload, result.items, errorMessage)) {
		request.pending = false;
		return false;
	}
	result.uri = request.uri;
	request.pending = false;
	accepted = true;
	errorMessage.clear();
	return true;
}

} // namespace mr::lsp
