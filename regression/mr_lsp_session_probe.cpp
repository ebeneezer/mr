#include <iostream>
#include <poll.h>
#include <string>
#include <vector>

#include "../lsp/MRLspSession.hpp"

namespace {
bool expect(bool condition, const std::string &name, std::string &failureReason) {
	if (condition) return true;
	failureReason = name;
	return false;
}

bool pollUntilMessages(mr::lsp::LspSession &session, std::vector<mr::lsp::LspInboundMessage> &messages, std::string &failureReason) {
	std::string errorMessage;

	for (int i = 0; i < 50; ++i) {
		if (!session.poll(messages, errorMessage)) {
			failureReason = "poll failed: " + errorMessage;
			return false;
		}
		if (!messages.empty()) return true;
		::poll(nullptr, 0, 20);
	}
	failureReason = "expected LSP message not observed";
	return false;
}

bool testSessionRequestResponse(std::string &failureReason) {
	mr::lsp::LspSession session;
	mr::lsp::LspSessionSpec spec;
	mr::lsp::JsonRpcPendingRequest request;
	std::vector<mr::lsp::LspInboundMessage> messages;
	std::string errorMessage;
	int exitStatus = -1;

	spec.process.executablePath = "./regression/mr_lsp_session_peer";
	if (!expect(session.start(spec, errorMessage), "session start: " + errorMessage, failureReason)) return false;
	if (!expect(session.running(), "session running", failureReason)) return false;
	if (!expect(session.sendRequest("initialize", "{}", request, errorMessage), "initialize send: " + errorMessage, failureReason)) return false;
	if (!expect(request.idText == "1", "initialize request id", failureReason)) return false;
	if (!expect(session.pendingRequestCount() == 1, "pending request count", failureReason)) return false;
	if (!pollUntilMessages(session, messages, failureReason)) return false;
	if (!expect(messages.size() == 1, "initialize response count", failureReason)) return false;
	if (!expect(messages[0].matchedPendingRequest, "initialize response matched", failureReason)) return false;
	if (!expect(messages[0].pendingRequest.method == "initialize", "initialize response method", failureReason)) return false;
	if (!expect(session.pendingRequestCount() == 0, "pending request cleared", failureReason)) return false;

	if (!expect(session.sendNotification("initialized", "{}", errorMessage), "initialized send: " + errorMessage, failureReason)) return false;
	if (!expect(session.sendRequest("mr/echo", "{\"value\":17}", request, errorMessage), "echo send: " + errorMessage, failureReason)) return false;
	if (!pollUntilMessages(session, messages, failureReason)) return false;
	if (!expect(messages[0].matchedPendingRequest, "echo response matched", failureReason)) return false;
	if (!expect(messages[0].pendingRequest.method == "mr/echo", "echo response method", failureReason)) return false;
	if (!expect(messages[0].payload.find("\"ok\":true") != std::string::npos, "echo response payload", failureReason)) return false;

	if (!expect(session.sendNotification("exit", "null", errorMessage), "exit send: " + errorMessage, failureReason)) return false;
	if (!expect(session.wait(1000, exitStatus), "session wait", failureReason)) return false;
	return expect(exitStatus == 0, "session exit status", failureReason);
}

bool testInvalidSessionPath(std::string &failureReason) {
	mr::lsp::LspSession session;
	mr::lsp::LspSessionSpec spec;
	std::string errorMessage;

	spec.process.executablePath = "./regression/not-an-lsp-peer";
	if (!expect(!session.start(spec, errorMessage), "invalid session path accepted", failureReason)) return false;
	return expect(!errorMessage.empty(), "invalid session path error", failureReason);
}

bool runProbe(std::string &failureReason) {
	if (!testSessionRequestResponse(failureReason)) return false;
	if (!testInvalidSessionPath(failureReason)) return false;
	return true;
}
} // namespace

int main() {
	std::string failureReason;

	if (!runProbe(failureReason)) {
		std::cerr << "mr_lsp_session_probe failed: " << failureReason << "\n";
		return 1;
	}

	std::cout << "mr_lsp_session_probe passed\n";
	return 0;
}
