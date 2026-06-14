#include "MRLspHover.hpp"

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

bool extractStringValue(const std::string &text, const std::string &key, std::size_t startPos, std::string &value) {
	std::size_t pos = 0;

	if (!findKeyValueStart(text, key, startPos, pos)) return false;
	return parseJsonStringAt(text, pos, value);
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

std::string buildHoverRequestPayload(const LspHoverRequest &request) {
	std::ostringstream out;

	out << "{\"jsonrpc\":\"2.0\",\"id\":" << request.idText << ",\"method\":\"textDocument/hover\",\"params\":{\"textDocument\":{\"uri\":";
	out << jsonString(request.uri);
	out << "},\"position\":{\"line\":" << request.position.line << ",\"character\":" << request.position.character << "}}}";
	return out.str();
}

bool parseHoverContents(const std::string &payload, LspHoverResult &result) {
	std::size_t resultStart = 0;
	std::size_t resultEnd = 0;
	std::size_t contentsStart = 0;

	if (!findKeyValueStart(payload, "result", 0, resultStart) || resultStart >= payload.size() || payload[resultStart] != '{') return false;
	if (!findMatchingBracket(payload, resultStart, '{', '}', resultEnd)) return false;
	const std::string resultObject = payload.substr(resultStart, resultEnd - resultStart + 1);
	if (!findKeyValueStart(resultObject, "contents", 0, contentsStart)) return false;
	if (contentsStart < resultObject.size() && resultObject[contentsStart] == '"') {
		result.kind = "plaintext";
		return parseJsonStringAt(resultObject, contentsStart, result.value);
	}
	if (contentsStart < resultObject.size() && resultObject[contentsStart] == '{') {
		std::size_t contentsEnd = 0;
		if (!findMatchingBracket(resultObject, contentsStart, '{', '}', contentsEnd)) return false;
		const std::string contentsObject = resultObject.substr(contentsStart, contentsEnd - contentsStart + 1);
		if (!extractStringValue(contentsObject, "kind", 0, result.kind)) return false;
		return extractStringValue(contentsObject, "value", 0, result.value);
	}
	return false;
}
} // namespace

bool LspHoverAdapter::requestHover(LspLifecycle &lifecycle, const LspDocumentService &documentService, LspTextPosition position, LspHoverRequest &request, std::string &errorMessage) {
	LspHoverRequest candidate;

	if (!documentService.isOpen()) return setError(errorMessage, "LSP document service has no open document.");
	if (position.line < 0 || position.character < 0) return setError(errorMessage, "LSP hover position is negative.");
	candidate.idText = jsonString("mr-hover-" + std::to_string(nextRequestId));
	candidate.method = "textDocument/hover";
	candidate.uri = documentService.documentUri();
	candidate.position = position;
	candidate.pending = true;
	if (!lifecycle.sendInitializedPayload(buildHoverRequestPayload(candidate), errorMessage)) return false;
	request = candidate;
	++nextRequestId;
	errorMessage.clear();
	return true;
}

bool LspHoverAdapter::consume(const LspInboundMessage &message, const LspDocumentService &documentService, LspHoverRequest &request, LspHoverResult &result, bool &accepted, std::string &errorMessage) {
	accepted = false;
	result = LspHoverResult();
	if (!request.pending) {
		errorMessage.clear();
		return true;
	}
	if (message.envelope.kind != JsonRpcMessageKind::Response || message.envelope.idText != request.idText) {
		errorMessage.clear();
		return true;
	}
	if (request.method != "textDocument/hover") return setError(errorMessage, "LSP hover request method mismatch.");
	if (request.uri != documentService.documentUri()) {
		request.pending = false;
		errorMessage.clear();
		return true;
	}
	if (!parseHoverContents(message.payload, result)) return setError(errorMessage, "LSP hover response contents are malformed.");
	result.uri = request.uri;
	request.pending = false;
	accepted = true;
	errorMessage.clear();
	return true;
}

} // namespace mr::lsp
