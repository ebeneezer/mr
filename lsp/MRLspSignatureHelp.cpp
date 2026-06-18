#include "MRLspSignatureHelp.hpp"

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
			case 'u': {
				if (pos + 4 > text.size()) return false;
				const std::string hex = text.substr(pos, 4);
				char *end = nullptr;
				const unsigned long codepoint = std::strtoul(hex.c_str(), &end, 16);

				if (end == nullptr || *end != '\0') return false;
				pos += 4;
				if (codepoint <= 0x7Fu) {
					value.push_back(static_cast<char>(codepoint));
				} else if (codepoint <= 0x7FFu) {
					value.push_back(static_cast<char>(0xC0u | ((codepoint >> 6) & 0x1Fu)));
					value.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
				} else {
					value.push_back(static_cast<char>(0xE0u | ((codepoint >> 12) & 0x0Fu)));
					value.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
					value.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
				}
				break;
			}
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

bool extractOptionalIntValue(const std::string &text, const std::string &key, std::size_t startPos, int &value) {
	std::size_t pos = 0;

	if (!findKeyValueStart(text, key, startPos, pos)) return true;
	if (pos < text.size() && text.compare(pos, 4, "null") == 0) return true;
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

std::string payloadExcerpt(const std::string &payload) {
	const std::size_t maxLen = 240;

	if (payload.size() <= maxLen) return payload;
	return payload.substr(0, maxLen) + "...";
}

std::string buildSignatureHelpRequestPayload(const LspSignatureHelpRequest &request) {
	std::ostringstream out;

	out << "{\"jsonrpc\":\"2.0\",\"id\":" << request.idText << ",\"method\":\"textDocument/signatureHelp\",\"params\":{\"textDocument\":{\"uri\":";
	out << jsonString(request.uri);
	out << "},\"position\":{\"line\":" << request.position.line << ",\"character\":" << request.position.character << "}}}";
	return out.str();
}

bool parseMarkupDocumentation(const std::string &object, std::string &documentation) {
	std::size_t documentationStart = 0;
	std::size_t documentationEnd = 0;

	documentation.clear();
	if (!findKeyValueStart(object, "documentation", 0, documentationStart)) return true;
	if (documentationStart < object.size() && object[documentationStart] == '"') return parseJsonStringAt(object, documentationStart, documentation);
	if (documentationStart < object.size() && object.compare(documentationStart, 4, "null") == 0) return true;
	if (documentationStart >= object.size() || object[documentationStart] != '{') return false;
	if (!findMatchingBracket(object, documentationStart, '{', '}', documentationEnd)) return false;
	return extractOptionalStringValue(object.substr(documentationStart, documentationEnd - documentationStart + 1), "value", 0, documentation);
}

bool parseParameterObject(const std::string &object, LspSignatureParameter &parameter) {
	std::size_t labelStart = 0;

	if (!findKeyValueStart(object, "label", 0, labelStart)) return false;
	if (labelStart < object.size() && object[labelStart] == '"') return parseJsonStringAt(object, labelStart, parameter.label);
	if (labelStart < object.size() && object[labelStart] == '[') {
		std::size_t labelEnd = 0;

		if (!findMatchingBracket(object, labelStart, '[', ']', labelEnd)) return false;
		parameter.label = object.substr(labelStart, labelEnd - labelStart + 1);
		return true;
	}
	return false;
}

bool parseParametersArray(const std::string &signatureObject, LspSignatureInformation &signature) {
	std::size_t parametersStart = 0;
	std::size_t parametersEnd = 0;
	std::size_t pos = 0;

	if (!findKeyValueStart(signatureObject, "parameters", 0, parametersStart)) return true;
	if (parametersStart >= signatureObject.size() || signatureObject[parametersStart] != '[') return false;
	if (!findMatchingBracket(signatureObject, parametersStart, '[', ']', parametersEnd)) return false;
	pos = parametersStart + 1;
	while (pos < parametersEnd) {
		skipWhitespace(signatureObject, pos);
		if (pos >= parametersEnd) break;
		if (signatureObject[pos] == ',') {
			++pos;
			continue;
		}
		if (signatureObject[pos] != '{') return false;
		std::size_t parameterEnd = 0;
		LspSignatureParameter parameter;

		if (!findMatchingBracket(signatureObject, pos, '{', '}', parameterEnd) || parameterEnd > parametersEnd) return false;
		if (!parseParameterObject(signatureObject.substr(pos, parameterEnd - pos + 1), parameter)) return false;
		signature.parameters.push_back(parameter);
		pos = parameterEnd + 1;
	}
	return true;
}

bool parseSignatureObject(const std::string &object, LspSignatureInformation &signature) {
	if (!extractStringValue(object, "label", 0, signature.label)) return false;
	if (!parseMarkupDocumentation(object, signature.documentation)) return false;
	return parseParametersArray(object, signature);
}

bool parseSignatureHelpResult(const std::string &payload, LspSignatureHelpResult &result) {
	std::size_t resultStart = 0;
	std::size_t resultEnd = 0;
	std::size_t signaturesStart = 0;
	std::size_t signaturesEnd = 0;
	std::size_t pos = 0;

	if (!findKeyValueStart(payload, "result", 0, resultStart) || resultStart >= payload.size()) return false;
	if (payload.compare(resultStart, 4, "null") == 0) return true;
	if (payload[resultStart] != '{') return false;
	if (!findMatchingBracket(payload, resultStart, '{', '}', resultEnd)) return false;
	const std::string resultObject = payload.substr(resultStart, resultEnd - resultStart + 1);

	static_cast<void>(extractOptionalIntValue(resultObject, "activeSignature", 0, result.activeSignature));
	static_cast<void>(extractOptionalIntValue(resultObject, "activeParameter", 0, result.activeParameter));
	if (!findKeyValueStart(resultObject, "signatures", 0, signaturesStart) || signaturesStart >= resultObject.size()) return false;
	if (resultObject[signaturesStart] != '[') return false;
	if (!findMatchingBracket(resultObject, signaturesStart, '[', ']', signaturesEnd)) return false;
	pos = signaturesStart + 1;
	while (pos < signaturesEnd) {
		skipWhitespace(resultObject, pos);
		if (pos >= signaturesEnd) break;
		if (resultObject[pos] == ',') {
			++pos;
			continue;
		}
		if (resultObject[pos] != '{') return false;
		std::size_t signatureEnd = 0;
		LspSignatureInformation signature;

		if (!findMatchingBracket(resultObject, pos, '{', '}', signatureEnd) || signatureEnd > signaturesEnd) return false;
		if (!parseSignatureObject(resultObject.substr(pos, signatureEnd - pos + 1), signature)) return false;
		result.signatures.push_back(signature);
		pos = signatureEnd + 1;
	}
	return true;
}
} // namespace

bool LspSignatureHelpAdapter::requestSignatureHelp(LspLifecycle &lifecycle, const LspDocumentService &documentService, LspTextPosition position, LspSignatureHelpRequest &request, std::string &errorMessage) {
	LspSignatureHelpRequest candidate;

	if (!documentService.isOpen()) return setError(errorMessage, "LSP document service has no open document.");
	if (position.line < 0 || position.character < 0) return setError(errorMessage, "LSP signatureHelp position is negative.");
	candidate.idText = jsonString("mr-signature-help-" + std::to_string(nextRequestId));
	candidate.method = "textDocument/signatureHelp";
	candidate.uri = documentService.documentUri();
	candidate.position = position;
	candidate.pending = true;
	if (!lifecycle.sendInitializedPayload(buildSignatureHelpRequestPayload(candidate), errorMessage)) return false;
	request = candidate;
	++nextRequestId;
	errorMessage.clear();
	return true;
}

bool LspSignatureHelpAdapter::consume(const LspInboundMessage &message, const LspDocumentService &documentService, LspSignatureHelpRequest &request, LspSignatureHelpResult &result, bool &accepted, std::string &errorMessage) {
	accepted = false;
	result = LspSignatureHelpResult();
	if (!request.pending) {
		errorMessage.clear();
		return true;
	}
	if (message.envelope.kind != JsonRpcMessageKind::Response || message.envelope.idText != request.idText) {
		errorMessage.clear();
		return true;
	}
	if (request.method != "textDocument/signatureHelp") return setError(errorMessage, "LSP signatureHelp request method mismatch.");
	if (request.uri != documentService.documentUri()) {
		request.pending = false;
		errorMessage.clear();
		return true;
	}
	if (!parseSignatureHelpResult(message.payload, result)) {
		request.pending = false;
		errorMessage = "LSP signatureHelp response is malformed; payload=" + payloadExcerpt(message.payload);
		return true;
	}
	result.uri = request.uri;
	request.pending = false;
	accepted = true;
	errorMessage.clear();
	return true;
}

} // namespace mr::lsp
