#include "MRLspSession.hpp"

#include <sstream>

namespace mr::lsp {
namespace {
void appendJsonString(std::ostringstream &out, std::string_view text) {
	out << '"';
	for (char ch : text) {
		if (ch == '"' || ch == '\\') {
			out << '\\' << ch;
		} else if (ch == '\n') {
			out << "\\n";
		} else if (ch == '\r') {
			out << "\\r";
		} else if (ch == '\t') {
			out << "\\t";
		} else {
			out << ch;
		}
	}
	out << '"';
}

std::string normalizedParams(std::string_view paramsJson) {
	if (paramsJson.empty()) return "null";
	return std::string(paramsJson);
}

bool setError(std::string &errorMessage, const std::string &message) {
	errorMessage = message;
	return false;
}
} // namespace

LspSession::~LspSession() {
	close();
}

bool LspSession::start(const LspSessionSpec &spec, std::string &errorMessage) {
	framer.clear();
	requestTracker.clear();
	return process.start(spec.process, errorMessage);
}

bool LspSession::sendNotification(std::string_view method, std::string_view paramsJson, std::string &errorMessage) {
	return sendPayload(buildJsonRpcNotification(method, paramsJson), errorMessage);
}

bool LspSession::sendRequest(std::string_view method, std::string_view paramsJson, JsonRpcPendingRequest &request, std::string &errorMessage) {
	request = requestTracker.beginRequest(method);
	if (sendPayload(buildJsonRpcRequest(request, paramsJson), errorMessage)) return true;
	const bool canceled = requestTracker.cancelRequest(request.idText);
	(void)canceled;
	return false;
}

bool LspSession::poll(std::vector<LspInboundMessage> &messages, std::string &errorMessage) {
	std::string chunk;

	messages.clear();
	if (!process.readAvailable(chunk, errorMessage)) return false;
	if (!chunk.empty() && !framer.feed(chunk)) return setError(errorMessage, framer.errorMessage());
	while (framer.hasMessage()) {
		LspInboundMessage message;

		message.payload = framer.popMessage().payload;
		message.envelope = parseJsonRpcEnvelope(message.payload);
		message.matchedPendingRequest = requestTracker.completeResponse(message.envelope, message.pendingRequest);
		messages.push_back(message);
	}
	errorMessage.clear();
	return true;
}

void LspSession::requestStop() {
	process.requestStop();
}

bool LspSession::wait(int timeoutMs, int &exitStatus) {
	return process.wait(timeoutMs, exitStatus);
}

bool LspSession::running() const noexcept {
	return process.running();
}

std::size_t LspSession::pendingRequestCount() const noexcept {
	return requestTracker.pendingCount();
}

void LspSession::close() {
	process.close();
	framer.clear();
	requestTracker.clear();
}

bool LspSession::sendPayload(std::string_view payload, std::string &errorMessage) {
	if (!process.running()) return setError(errorMessage, "LSP session process is not running.");
	return process.writeStdin(buildJsonRpcFrame(payload), errorMessage);
}

std::string buildJsonRpcNotification(std::string_view method, std::string_view paramsJson) {
	std::ostringstream out;

	out << "{\"jsonrpc\":\"2.0\",\"method\":";
	appendJsonString(out, method);
	out << ",\"params\":" << normalizedParams(paramsJson) << "}";
	return out.str();
}

std::string buildJsonRpcRequest(const JsonRpcPendingRequest &request, std::string_view paramsJson) {
	std::ostringstream out;

	out << "{\"jsonrpc\":\"2.0\",\"id\":" << request.idText << ",\"method\":";
	appendJsonString(out, request.method);
	out << ",\"params\":" << normalizedParams(paramsJson) << "}";
	return out.str();
}

} // namespace mr::lsp
