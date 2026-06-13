#include "MRLspDiagnostics.hpp"

#include <cctype>
#include <cstdlib>

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

bool parseIntAt(const std::string &text, std::size_t &pos, std::int64_t &value) {
	char *end = nullptr;
	const char *start = text.c_str() + pos;

	if (pos >= text.size()) return false;
	value = std::strtoll(start, &end, 10);
	if (end == start) return false;
	pos = static_cast<std::size_t>(end - text.c_str());
	return true;
}

bool extractStringValue(const std::string &text, const std::string &key, std::size_t startPos, std::string &value) {
	std::size_t pos = 0;

	if (!findKeyValueStart(text, key, startPos, pos)) return false;
	return parseJsonStringAt(text, pos, value);
}

bool extractIntValue(const std::string &text, const std::string &key, std::size_t startPos, std::int64_t &value) {
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
			if (escaped) {
				escaped = false;
			} else if (ch == '\\') {
				escaped = true;
			} else if (ch == '"') {
				inString = false;
			}
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

bool parseDiagnosticObject(const std::string &text, LspDiagnostic &diagnostic) {
	std::int64_t value = 0;

	if (!extractIntValue(text, "line", 0, value)) return false;
	diagnostic.range.startLine = static_cast<int>(value);
	if (!extractIntValue(text, "character", 0, value)) return false;
	diagnostic.range.startCharacter = static_cast<int>(value);
	const std::size_t endPos = text.find("\"end\"");
	if (endPos == std::string::npos || !extractIntValue(text, "line", endPos, value)) return false;
	diagnostic.range.endLine = static_cast<int>(value);
	if (!extractIntValue(text, "character", endPos, value)) return false;
	diagnostic.range.endCharacter = static_cast<int>(value);
	if (!extractIntValue(text, "severity", 0, value)) return false;
	diagnostic.severity = static_cast<int>(value);
	return extractStringValue(text, "message", 0, diagnostic.message);
}

bool parseDiagnosticsArray(const std::string &payload, std::vector<LspDiagnostic> &diagnostics, std::string &errorMessage) {
	std::size_t arrayStart = 0;
	std::size_t arrayEnd = 0;
	std::size_t pos = 0;

	diagnostics.clear();
	if (!findKeyValueStart(payload, "diagnostics", 0, arrayStart) || arrayStart >= payload.size() || payload[arrayStart] != '[')
		return setError(errorMessage, "publishDiagnostics is missing diagnostics array.");
	if (!findMatchingBracket(payload, arrayStart, '[', ']', arrayEnd)) return setError(errorMessage, "publishDiagnostics diagnostics array is malformed.");
	pos = arrayStart + 1;
	while (pos < arrayEnd) {
		skipWhitespace(payload, pos);
		if (pos >= arrayEnd) break;
		if (payload[pos] == ',') {
			++pos;
			continue;
		}
		if (payload[pos] != '{') return setError(errorMessage, "publishDiagnostics diagnostic entry is malformed.");
		std::size_t objectEnd = 0;
		if (!findMatchingBracket(payload, pos, '{', '}', objectEnd) || objectEnd > arrayEnd) return setError(errorMessage, "publishDiagnostics diagnostic object is malformed.");
		LspDiagnostic diagnostic;
		if (!parseDiagnosticObject(payload.substr(pos, objectEnd - pos + 1), diagnostic)) return setError(errorMessage, "publishDiagnostics diagnostic fields are malformed.");
		diagnostics.push_back(diagnostic);
		pos = objectEnd + 1;
	}
	return true;
}
} // namespace

bool LspDiagnosticsAdapter::consume(const LspInboundMessage &message, const LspDocumentService &documentService, LspDiagnosticBatch &batch, std::string &errorMessage) const {
	std::int64_t version = 0;

	batch = LspDiagnosticBatch();
	if (message.envelope.kind != JsonRpcMessageKind::Notification || message.envelope.method != "textDocument/publishDiagnostics") {
		errorMessage.clear();
		return true;
	}
	if (!extractStringValue(message.payload, "uri", 0, batch.uri)) return setError(errorMessage, "publishDiagnostics is missing URI.");
	if (!extractIntValue(message.payload, "version", 0, version)) return setError(errorMessage, "publishDiagnostics is missing version.");
	batch.version = version;
	if (batch.uri != documentService.documentUri()) {
		batch.rejected = true;
		errorMessage.clear();
		return true;
	}
	if (documentService.isStaleForSentVersion(batch.version)) {
		batch.stale = true;
		errorMessage.clear();
		return true;
	}
	if (!documentService.matchesSentVersion(batch.version)) {
		batch.rejected = true;
		errorMessage.clear();
		return true;
	}
	if (!parseDiagnosticsArray(message.payload, batch.diagnostics, errorMessage)) return false;
	batch.accepted = true;
	errorMessage.clear();
	return true;
}

} // namespace mr::lsp
