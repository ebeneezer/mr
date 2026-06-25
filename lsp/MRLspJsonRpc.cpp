#include "MRLspJsonRpc.hpp"

#include <cctype>
#include <limits>
#include <sstream>

namespace mr::lsp {
namespace {
static const std::size_t kMaxHeaderBytes = 64 * 1024;

struct JsonRpcTopLevelMembers {
	bool validObject = false;
	bool method = false;
	bool result = false;
	bool error = false;
	JsonRpcIdKind idKind = JsonRpcIdKind::None;
	std::string idText;
	std::string methodText;
};

bool asciiCaseEquals(const std::string &left, const std::string &right) {
	if (left.size() != right.size()) return false;
	for (std::size_t i = 0; i < left.size(); ++i) {
		const unsigned char leftChar = static_cast<unsigned char>(left[i]);
		const unsigned char rightChar = static_cast<unsigned char>(right[i]);
		if (std::tolower(leftChar) != std::tolower(rightChar)) return false;
	}
	return true;
}

std::string trimAscii(const std::string &value) {
	std::size_t start = 0;
	std::size_t end = value.size();

	while (start < end && std::isspace(static_cast<unsigned char>(value[start])) != 0)
		++start;
	while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
		--end;
	return value.substr(start, end - start);
}

void skipJsonWhitespace(const std::string &text, std::size_t &pos) {
	while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0)
		++pos;
}

bool parseJsonString(const std::string &text, std::size_t &pos, std::string *outValue) {
	std::string value;

	if (pos >= text.size() || text[pos] != '"') return false;
	++pos;
	while (pos < text.size()) {
		const char ch = text[pos++];
		if (ch == '"') {
			if (outValue != nullptr) *outValue = value;
			return true;
		}
		if (ch == '\\') {
			if (pos >= text.size()) return false;
			value.push_back(text[pos++]);
			continue;
		}
		value.push_back(ch);
	}
	return false;
}

bool skipJsonValue(const std::string &text, std::size_t &pos) {
	int objectDepth = 0;
	int arrayDepth = 0;

	skipJsonWhitespace(text, pos);
	if (pos >= text.size()) return false;
	if (text[pos] == '"') return parseJsonString(text, pos, nullptr);
	while (pos < text.size()) {
		const char ch = text[pos];
		if (ch == '"') {
			if (!parseJsonString(text, pos, nullptr)) return false;
			continue;
		}
		if (ch == '{') {
			++objectDepth;
			++pos;
			continue;
		}
		if (ch == '[') {
			++arrayDepth;
			++pos;
			continue;
		}
		if (ch == '}') {
			if (objectDepth == 0 && arrayDepth == 0) return true;
			--objectDepth;
			++pos;
			continue;
		}
		if (ch == ']') {
			if (arrayDepth == 0) return false;
			--arrayDepth;
			++pos;
			continue;
		}
		if (ch == ',' && objectDepth == 0 && arrayDepth == 0) return true;
		++pos;
	}
	return objectDepth == 0 && arrayDepth == 0;
}

bool parseJsonNumberValue(const std::string &text, std::size_t &pos) {
	const std::size_t start = pos;

	if (pos < text.size() && text[pos] == '-') ++pos;
	if (pos >= text.size() || text[pos] < '0' || text[pos] > '9') return false;
	if (text[pos] == '0') ++pos;
	else {
		while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
			++pos;
	}
	if (pos < text.size() && text[pos] == '.') {
		++pos;
		if (pos >= text.size() || text[pos] < '0' || text[pos] > '9') return false;
		while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
			++pos;
	}
	if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
		++pos;
		if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) ++pos;
		if (pos >= text.size() || text[pos] < '0' || text[pos] > '9') return false;
		while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
			++pos;
	}
	return pos > start;
}

bool parseJsonLiteral(const std::string &text, std::size_t &pos, const std::string &literal) {
	if (pos + literal.size() > text.size()) return false;
	if (text.substr(pos, literal.size()) != literal) return false;
	pos += literal.size();
	return true;
}

JsonRpcIdKind parseJsonRpcIdValue(const std::string &text, std::size_t &pos, std::string &idText) {
	const std::size_t start = pos;
	std::string stringValue;

	if (pos >= text.size()) return JsonRpcIdKind::Invalid;
	if (text[pos] == '"') {
		if (!parseJsonString(text, pos, &stringValue)) return JsonRpcIdKind::Invalid;
		idText = text.substr(start, pos - start);
		return JsonRpcIdKind::String;
	}
	if (text[pos] == 'n') {
		if (!parseJsonLiteral(text, pos, "null")) return JsonRpcIdKind::Invalid;
		idText = text.substr(start, pos - start);
		return JsonRpcIdKind::Null;
	}
	if (text[pos] == '-' || (text[pos] >= '0' && text[pos] <= '9')) {
		if (!parseJsonNumberValue(text, pos)) return JsonRpcIdKind::Invalid;
		idText = text.substr(start, pos - start);
		return JsonRpcIdKind::Number;
	}
	return JsonRpcIdKind::Invalid;
}

bool scanJsonRpcTopLevelMembers(const std::string &payload, JsonRpcTopLevelMembers &members) {
	std::size_t pos = 0;

	skipJsonWhitespace(payload, pos);
	if (pos >= payload.size() || payload[pos] != '{') return true;
	++pos;
	members.validObject = true;
	skipJsonWhitespace(payload, pos);
	if (pos < payload.size() && payload[pos] == '}') {
		++pos;
		skipJsonWhitespace(payload, pos);
		return pos == payload.size();
	}
	for (;;) {
		std::string key;

		if (!parseJsonString(payload, pos, &key)) return false;
		skipJsonWhitespace(payload, pos);
		if (pos >= payload.size() || payload[pos] != ':') return false;
		++pos;
		skipJsonWhitespace(payload, pos);
		if (key == "id") {
			members.idKind = parseJsonRpcIdValue(payload, pos, members.idText);
			if (members.idKind == JsonRpcIdKind::Invalid) return false;
		} else if (key == "method") {
			std::string methodValue;
			if (!parseJsonString(payload, pos, &methodValue)) return false;
			members.method = true;
			members.methodText = methodValue;
		} else {
			if (key == "result")
				members.result = true;
			else if (key == "error")
				members.error = true;
			if (!skipJsonValue(payload, pos)) return false;
		}
		skipJsonWhitespace(payload, pos);
		if (pos >= payload.size()) return false;
		if (payload[pos] == '}') {
			++pos;
			skipJsonWhitespace(payload, pos);
			return pos == payload.size();
		}
		if (payload[pos] != ',') return false;
		++pos;
		skipJsonWhitespace(payload, pos);
	}
}

bool parseContentLength(const std::string &value, std::size_t &contentLength) {
	std::size_t parsed = 0;
	const std::string trimmedValue = trimAscii(value);

	if (trimmedValue.empty()) return false;
	for (std::size_t index = 0; index < trimmedValue.size(); ++index) {
		const char ch = trimmedValue[index];
		if (ch < '0' || ch > '9') return false;
		const std::size_t digit = static_cast<std::size_t>(ch - '0');
		if (parsed > (std::numeric_limits<std::size_t>::max() - digit) / 10) return false;
		parsed = parsed * 10 + digit;
	}
	contentLength = parsed;
	return true;
}

std::string makeFrameHeader(std::size_t length) {
	std::ostringstream out;

	out << "Content-Length: " << length << "\r\n\r\n";
	return out.str();
}
} // namespace

bool JsonRpcFramer::feed(const std::string &bytes) {
	if (failed()) return false;
	buffer.append(bytes);
	return processBuffer();
}

bool JsonRpcFramer::hasMessage() const noexcept {
	return !messages.empty();
}

JsonRpcMessage JsonRpcFramer::popMessage() {
	JsonRpcMessage message;

	if (messages.empty()) return message;
	message = messages.front();
	messages.erase(messages.begin());
	return message;
}

bool JsonRpcFramer::failed() const noexcept {
	return !error.empty();
}

const std::string &JsonRpcFramer::errorMessage() const noexcept {
	return error;
}

void JsonRpcFramer::clear() {
	buffer.clear();
	messages.clear();
	error.clear();
}

bool JsonRpcFramer::processBuffer() {
	for (;;) {
		const std::size_t headerEnd = buffer.find("\r\n\r\n");
		std::size_t contentLength = 0;
		bool sawContentLength = false;
		std::size_t lineStart = 0;

		if (headerEnd == std::string::npos) {
			if (buffer.size() > kMaxHeaderBytes) return fail("LSP JSON-RPC header exceeds maximum size.");
			return true;
		}

		while (lineStart < headerEnd) {
			const std::size_t lineEnd = buffer.find("\r\n", lineStart);
			const std::size_t effectiveLineEnd = lineEnd == std::string::npos || lineEnd > headerEnd ? headerEnd : lineEnd;
			const std::string line = buffer.substr(lineStart, effectiveLineEnd - lineStart);
			const std::size_t colon = line.find(':');

			if (colon == std::string::npos) return fail("LSP JSON-RPC header line is missing ':'.");
			const std::string name = trimAscii(line.substr(0, colon));
			const std::string value = line.substr(colon + 1);
			if (name.empty()) return fail("LSP JSON-RPC header name is empty.");
			if (asciiCaseEquals(name, "Content-Length")) {
				if (sawContentLength) return fail("LSP JSON-RPC header contains duplicate Content-Length.");
				if (!parseContentLength(value, contentLength)) return fail("LSP JSON-RPC Content-Length is invalid.");
				sawContentLength = true;
			}
			lineStart = effectiveLineEnd + 2;
		}

		if (!sawContentLength) return fail("LSP JSON-RPC header is missing Content-Length.");
		const std::size_t bodyStart = headerEnd + 4;
		if (contentLength > buffer.size() - bodyStart) return true;

		JsonRpcMessage message;
		message.payload = buffer.substr(bodyStart, contentLength);
		messages.push_back(message);
		buffer.erase(0, bodyStart + contentLength);
	}
}

bool JsonRpcFramer::fail(const std::string &message) {
	error = message;
	return false;
}

JsonRpcPendingRequest JsonRpcRequestTracker::beginRequest(const std::string &method) {
	JsonRpcPendingRequest request;

	request.idText = std::to_string(nextRequestId);
	request.method = method;
	pendingRequests[request.idText] = request;
	++nextRequestId;
	return request;
}

bool JsonRpcRequestTracker::completeResponse(const JsonRpcEnvelope &envelope, JsonRpcPendingRequest &outRequest) {
	if (envelope.kind != JsonRpcMessageKind::Response) return false;
	if (envelope.idKind != JsonRpcIdKind::Number && envelope.idKind != JsonRpcIdKind::String) return false;
	std::unordered_map<std::string, JsonRpcPendingRequest>::iterator it = pendingRequests.find(envelope.idText);
	if (it == pendingRequests.end()) return false;
	outRequest = it->second;
	pendingRequests.erase(it);
	return true;
}

bool JsonRpcRequestTracker::cancelRequest(const std::string &idText) {
	return pendingRequests.erase(idText) != 0;
}

bool JsonRpcRequestTracker::hasPending(const std::string &idText) const {
	return pendingRequests.find(idText) != pendingRequests.end();
}

std::size_t JsonRpcRequestTracker::pendingCount() const noexcept {
	return pendingRequests.size();
}

void JsonRpcRequestTracker::clear() {
	nextRequestId = 1;
	pendingRequests.clear();
}

std::string buildJsonRpcFrame(const std::string &json) {
	std::string frame = makeFrameHeader(json.size());

	frame.append(json);
	return frame;
}

JsonRpcMessageKind classifyJsonRpcPayload(const std::string &payload) {
	return parseJsonRpcEnvelope(payload).kind;
}

JsonRpcEnvelope parseJsonRpcEnvelope(const std::string &payload) {
	JsonRpcTopLevelMembers members;
	JsonRpcEnvelope envelope;

	if (!scanJsonRpcTopLevelMembers(payload, members)) {
		envelope.idKind = JsonRpcIdKind::Invalid;
		return envelope;
	}
	if (!members.validObject) return envelope;
	envelope.idKind = members.idKind;
	envelope.idText = members.idText;
	envelope.method = members.methodText;
	envelope.hasResult = members.result;
	envelope.hasError = members.error;
	if (members.idKind != JsonRpcIdKind::None && (members.result || members.error) && !members.method) envelope.kind = JsonRpcMessageKind::Response;
	else if ((members.idKind == JsonRpcIdKind::Number || members.idKind == JsonRpcIdKind::String) && members.method && !members.result && !members.error)
		envelope.kind = JsonRpcMessageKind::Request;
	else if (members.idKind == JsonRpcIdKind::None && members.method && !members.result && !members.error)
		envelope.kind = JsonRpcMessageKind::Notification;
	return envelope;
}

} // namespace mr::lsp
