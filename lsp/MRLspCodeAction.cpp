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

bool extractTopLevelIntValue(const std::string &object, const std::string &key, int &value) {
	std::size_t pos = 0;
	bool negative = false;
	int parsed = 0;

	value = 0;
	if (!findTopLevelObjectValueStart(object, key, pos)) return false;
	if (pos < object.size() && object[pos] == '-') {
		negative = true;
		++pos;
	}
	if (pos >= object.size() || object[pos] < '0' || object[pos] > '9') return false;
	while (pos < object.size() && object[pos] >= '0' && object[pos] <= '9') {
		parsed = parsed * 10 + static_cast<int>(object[pos] - '0');
		++pos;
	}
	value = negative ? -parsed : parsed;
	return true;
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

bool parsePositionObject(const std::string &object, LspTextPosition &position) {
	return extractTopLevelIntValue(object, "line", position.line) && extractTopLevelIntValue(object, "character", position.character);
}

bool parseRangeObject(const std::string &object, LspCodeActionRange &range) {
	std::size_t startPos = 0;
	std::size_t startEnd = 0;
	std::size_t endPos = 0;
	std::size_t endEnd = 0;

	if (!findTopLevelObjectValueStart(object, "start", startPos) || startPos >= object.size() || object[startPos] != '{') return false;
	if (!findMatchingBracket(object, startPos, '{', '}', startEnd)) return false;
	if (!findTopLevelObjectValueStart(object, "end", endPos) || endPos >= object.size() || object[endPos] != '{') return false;
	if (!findMatchingBracket(object, endPos, '{', '}', endEnd)) return false;
	return parsePositionObject(object.substr(startPos, startEnd - startPos + 1), range.start) && parsePositionObject(object.substr(endPos, endEnd - endPos + 1), range.end);
}

bool parseTextEditObject(const std::string &object, const std::string &uri, LspCodeActionTextEdit &edit) {
	std::size_t rangePos = 0;
	std::size_t rangeEnd = 0;

	edit = LspCodeActionTextEdit();
	edit.uri = uri;
	if (!findTopLevelObjectValueStart(object, "range", rangePos) || rangePos >= object.size() || object[rangePos] != '{') return false;
	if (!findMatchingBracket(object, rangePos, '{', '}', rangeEnd)) return false;
	if (!parseRangeObject(object.substr(rangePos, rangeEnd - rangePos + 1), edit.range)) return false;
	return extractTopLevelStringValue(object, "newText", edit.newText);
}

bool parseTextEditArray(const std::string &arrayText, const std::string &uri, std::vector<LspCodeActionTextEdit> &edits) {
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
		LspCodeActionTextEdit edit;
		if (!parseTextEditObject(arrayText.substr(pos, objectEnd - pos + 1), uri, edit)) return false;
		edits.push_back(edit);
		pos = objectEnd + 1;
	}
	return true;
}

bool parseChangesObject(const std::string &object, std::vector<LspCodeActionTextEdit> &edits) {
	std::size_t pos = 0;

	skipWhitespace(object, pos);
	if (pos >= object.size() || object[pos] != '{') return false;
	++pos;
	while (pos < object.size()) {
		std::string uri;
		std::size_t arrayEnd = 0;

		skipWhitespace(object, pos);
		if (pos < object.size() && object[pos] == '}') return true;
		if (!parseJsonStringAt(object, pos, uri)) return false;
		skipWhitespace(object, pos);
		if (pos >= object.size() || object[pos] != ':') return false;
		++pos;
		skipWhitespace(object, pos);
		if (pos >= object.size() || object[pos] != '[') return false;
		if (!findMatchingBracket(object, pos, '[', ']', arrayEnd)) return false;
		if (!parseTextEditArray(object.substr(pos, arrayEnd - pos + 1), uri, edits)) return false;
		pos = arrayEnd + 1;
		skipWhitespace(object, pos);
		if (pos < object.size() && object[pos] == ',') {
			++pos;
			continue;
		}
		if (pos < object.size() && object[pos] == '}') return true;
	}
	return false;
}

bool parseCodeActionEdits(const std::string &object, std::vector<LspCodeActionTextEdit> &edits) {
	std::size_t editPos = 0;
	std::size_t editEnd = 0;
	std::size_t changesPos = 0;
	std::size_t changesEnd = 0;
	std::string editObject;

	edits.clear();
	if (!findTopLevelObjectValueStart(object, "edit", editPos) || editPos >= object.size() || object[editPos] != '{') return true;
	if (!findMatchingBracket(object, editPos, '{', '}', editEnd)) return false;
	editObject = object.substr(editPos, editEnd - editPos + 1);
	if (!findTopLevelObjectValueStart(editObject, "changes", changesPos)) return true;
	if (changesPos >= editObject.size() || editObject[changesPos] != '{') return false;
	if (!findMatchingBracket(editObject, changesPos, '{', '}', changesEnd)) return false;
	return parseChangesObject(editObject.substr(changesPos, changesEnd - changesPos + 1), edits);
}

bool parseCodeActionObject(const std::string &object, LspCodeActionItem &item) {
	if (!extractTopLevelStringValue(object, "title", item.title)) return false;
	if (!extractOptionalTopLevelStringValue(object, "kind", item.kind)) return false;
	item.hasEdit = hasTopLevelObjectKey(object, "edit");
	item.hasCommand = hasTopLevelObjectKey(object, "command");
	if (!parseCodeActionEdits(object, item.edits)) return false;
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
