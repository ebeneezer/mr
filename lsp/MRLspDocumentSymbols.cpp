#include "MRLspDocumentSymbols.hpp"

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

bool extractOptionalStringValue(const std::string &text, const std::string &key, std::size_t startPos, std::string &value) {
	std::size_t pos = 0;

	value.clear();
	if (!findKeyValueStart(text, key, startPos, pos)) return true;
	if (pos < text.size() && text.compare(pos, 4, "null") == 0) return true;
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

std::string buildDocumentSymbolsRequestPayload(const LspDocumentSymbolsRequest &request) {
	std::ostringstream out;

	out << "{\"jsonrpc\":\"2.0\",\"id\":" << request.idText << ",\"method\":\"textDocument/documentSymbol\",\"params\":{\"textDocument\":{\"uri\":";
	out << jsonString(request.uri);
	out << "}}}";
	return out.str();
}

std::string buildWorkspaceSymbolsRequestPayload(const LspWorkspaceSymbolsRequest &request) {
	std::ostringstream out;

	out << "{\"jsonrpc\":\"2.0\",\"id\":" << request.idText << ",\"method\":\"workspace/symbol\",\"params\":{\"query\":";
	out << jsonString(request.query);
	out << "}}";
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

bool parseSelectionRangeObject(const std::string &object, LspLocation &location) {
	std::size_t rangeStart = 0;
	std::size_t rangeEnd = 0;
	const std::string rangeKey = "selectionRange";

	if (!findKeyValueStart(object, rangeKey, 0, rangeStart) || rangeStart >= object.size() || object[rangeStart] != '{') return false;
	if (!findMatchingBracket(object, rangeStart, '{', '}', rangeEnd)) return false;
	const std::string rangeObject = "{\"range\":" + object.substr(rangeStart, rangeEnd - rangeStart + 1) + "}";
	return parseRangeObject(rangeObject, location);
}

bool parseSymbolInformationObject(const std::string &object, LspDocumentSymbol &symbol) {
	std::size_t locationStart = 0;
	std::size_t locationEnd = 0;

	if (!extractStringValue(object, "name", 0, symbol.name)) return false;
	if (!extractIntValue(object, "kind", 0, symbol.kind)) return false;
	static_cast<void>(extractOptionalStringValue(object, "containerName", 0, symbol.detail));
	if (!findKeyValueStart(object, "location", 0, locationStart) || locationStart >= object.size() || object[locationStart] != '{') return false;
	if (!findMatchingBracket(object, locationStart, '{', '}', locationEnd)) return false;
	if (!extractStringValue(object.substr(locationStart, locationEnd - locationStart + 1), "uri", 0, symbol.location.uri)) return false;
	return parseRangeObject(object.substr(locationStart, locationEnd - locationStart + 1), symbol.location);
}

bool parseDocumentSymbolObject(const std::string &object, const std::string &uri, int depth, std::vector<LspDocumentSymbol> &symbols) {
	LspDocumentSymbol symbol;
	std::size_t childrenStart = 0;
	std::size_t childrenEnd = 0;

	if (!extractStringValue(object, "name", 0, symbol.name)) return false;
	if (!extractIntValue(object, "kind", 0, symbol.kind)) return false;
	static_cast<void>(extractOptionalStringValue(object, "detail", 0, symbol.detail));
	symbol.location.uri = uri;
	symbol.depth = depth;
	if (!parseSelectionRangeObject(object, symbol.location) && !parseRangeObject(object, symbol.location)) return false;
	symbols.push_back(symbol);
	if (!findKeyValueStart(object, "children", 0, childrenStart) || childrenStart >= object.size() || object[childrenStart] != '[') return true;
	if (!findMatchingBracket(object, childrenStart, '[', ']', childrenEnd)) return false;
	std::size_t pos = childrenStart + 1;
	while (pos < childrenEnd) {
		skipWhitespace(object, pos);
		if (pos >= childrenEnd) break;
		if (object[pos] == ',') {
			++pos;
			continue;
		}
		if (object[pos] != '{') return false;
		std::size_t childEnd = 0;
		if (!findMatchingBracket(object, pos, '{', '}', childEnd) || childEnd > childrenEnd) return false;
		if (!parseDocumentSymbolObject(object.substr(pos, childEnd - pos + 1), uri, depth + 1, symbols)) return false;
		pos = childEnd + 1;
	}
	return true;
}

bool parseDocumentSymbolsResult(const std::string &payload, const std::string &uri, std::vector<LspDocumentSymbol> &symbols, std::string &errorMessage) {
	std::size_t arrayStart = 0;
	std::size_t arrayEnd = 0;
	std::size_t pos = 0;

	symbols.clear();
	if (!findKeyValueStart(payload, "result", 0, arrayStart) || arrayStart >= payload.size()) return setError(errorMessage, "LSP documentSymbol response result is not an array.");
	if (payload.compare(arrayStart, 4, "null") == 0) return true;
	if (payload[arrayStart] != '[') return setError(errorMessage, "LSP documentSymbol response result is not an array.");
	if (!findMatchingBracket(payload, arrayStart, '[', ']', arrayEnd)) return setError(errorMessage, "LSP documentSymbol response array is malformed.");
	pos = arrayStart + 1;
	while (pos < arrayEnd) {
		skipWhitespace(payload, pos);
		if (pos >= arrayEnd) break;
		if (payload[pos] == ',') {
			++pos;
			continue;
		}
		if (payload[pos] != '{') return setError(errorMessage, "LSP documentSymbol entry is malformed.");
		std::size_t objectEnd = 0;
		if (!findMatchingBracket(payload, pos, '{', '}', objectEnd) || objectEnd > arrayEnd) return setError(errorMessage, "LSP documentSymbol object is malformed.");
		const std::string object = payload.substr(pos, objectEnd - pos + 1);
		LspDocumentSymbol symbol;
		if (parseSymbolInformationObject(object, symbol))
			symbols.push_back(symbol);
		else if (!parseDocumentSymbolObject(object, uri, 0, symbols))
			return setError(errorMessage, "LSP documentSymbol fields are malformed.");
		pos = objectEnd + 1;
	}
	return true;
}

bool parseWorkspaceSymbolsResult(const std::string &payload, std::vector<LspDocumentSymbol> &symbols, std::string &errorMessage) {
	std::size_t arrayStart = 0;
	std::size_t arrayEnd = 0;
	std::size_t pos = 0;

	symbols.clear();
	if (!findKeyValueStart(payload, "result", 0, arrayStart) || arrayStart >= payload.size()) return setError(errorMessage, "LSP workspace/symbol response result is not an array.");
	if (payload.compare(arrayStart, 4, "null") == 0) return true;
	if (payload[arrayStart] != '[') return setError(errorMessage, "LSP workspace/symbol response result is not an array.");
	if (!findMatchingBracket(payload, arrayStart, '[', ']', arrayEnd)) return setError(errorMessage, "LSP workspace/symbol response array is malformed.");
	pos = arrayStart + 1;
	while (pos < arrayEnd) {
		skipWhitespace(payload, pos);
		if (pos >= arrayEnd) break;
		if (payload[pos] == ',') {
			++pos;
			continue;
		}
		if (payload[pos] != '{') return setError(errorMessage, "LSP workspace/symbol entry is malformed.");
		std::size_t objectEnd = 0;
		if (!findMatchingBracket(payload, pos, '{', '}', objectEnd) || objectEnd > arrayEnd) return setError(errorMessage, "LSP workspace/symbol object is malformed.");
		LspDocumentSymbol symbol;
		if (!parseSymbolInformationObject(payload.substr(pos, objectEnd - pos + 1), symbol)) return setError(errorMessage, "LSP workspace/symbol fields are malformed.");
		symbols.push_back(symbol);
		pos = objectEnd + 1;
	}
	return true;
}
} // namespace

bool LspDocumentSymbolsAdapter::requestDocumentSymbols(LspLifecycle &lifecycle, const LspDocumentService &documentService, LspDocumentSymbolsRequest &request, std::string &errorMessage) {
	LspDocumentSymbolsRequest candidate;

	if (!documentService.isOpen()) return setError(errorMessage, "LSP document service has no open document.");
	candidate.idText = jsonString("mr-document-symbols-" + std::to_string(nextRequestId));
	candidate.method = "textDocument/documentSymbol";
	candidate.uri = documentService.documentUri();
	candidate.pending = true;
	if (!lifecycle.sendInitializedPayload(buildDocumentSymbolsRequestPayload(candidate), errorMessage)) return false;
	request = candidate;
	++nextRequestId;
	errorMessage.clear();
	return true;
}

bool LspDocumentSymbolsAdapter::consume(const LspInboundMessage &message, const LspDocumentService &documentService, LspDocumentSymbolsRequest &request, LspDocumentSymbolsResult &result, bool &accepted, std::string &errorMessage) {
	accepted = false;
	result = LspDocumentSymbolsResult();
	if (!request.pending) {
		errorMessage.clear();
		return true;
	}
	if (message.envelope.kind != JsonRpcMessageKind::Response || message.envelope.idText != request.idText) {
		errorMessage.clear();
		return true;
	}
	if (request.method != "textDocument/documentSymbol") return setError(errorMessage, "LSP documentSymbol request method mismatch.");
	if (request.uri != documentService.documentUri()) {
		request.pending = false;
		errorMessage.clear();
		return true;
	}
	result.originUri = request.uri;
	if (!parseDocumentSymbolsResult(message.payload, request.uri, result.symbols, errorMessage)) return false;
	request.pending = false;
	accepted = true;
	errorMessage.clear();
	return true;
}

bool LspDocumentSymbolsAdapter::requestWorkspaceSymbols(LspLifecycle &lifecycle, const std::string &query, LspWorkspaceSymbolsRequest &request, std::string &errorMessage) {
	LspWorkspaceSymbolsRequest candidate;

	candidate.idText = jsonString("mr-workspace-symbols-" + std::to_string(nextRequestId));
	candidate.method = "workspace/symbol";
	candidate.query = query;
	candidate.pending = true;
	if (!lifecycle.sendInitializedPayload(buildWorkspaceSymbolsRequestPayload(candidate), errorMessage)) return false;
	request = candidate;
	++nextRequestId;
	errorMessage.clear();
	return true;
}

bool LspDocumentSymbolsAdapter::consumeWorkspaceSymbols(const LspInboundMessage &message, LspWorkspaceSymbolsRequest &request, LspWorkspaceSymbolsResult &result, bool &accepted, std::string &errorMessage) {
	accepted = false;
	result = LspWorkspaceSymbolsResult();
	if (!request.pending) {
		errorMessage.clear();
		return true;
	}
	if (message.envelope.kind != JsonRpcMessageKind::Response || message.envelope.idText != request.idText) {
		errorMessage.clear();
		return true;
	}
	if (request.method != "workspace/symbol") return setError(errorMessage, "LSP workspace/symbol request method mismatch.");
	result.query = request.query;
	if (!parseWorkspaceSymbolsResult(message.payload, result.symbols, errorMessage)) return false;
	request.pending = false;
	accepted = true;
	errorMessage.clear();
	return true;
}

} // namespace mr::lsp
