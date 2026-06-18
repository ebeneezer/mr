#include "../lsp/MRLspSession.hpp"

#include <iostream>
#include <poll.h>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string &name, std::string &failureReason) {
	if (condition) return true;
	failureReason = name;
	return false;
}

bool startShaper(mr::lsp::LspSession &session, const std::string &scenario, std::string &failureReason) {
	mr::lsp::LspSessionSpec spec;
	std::string errorMessage;

	spec.process.executablePath = "./regression/mr_lsp_protocol_shaper";
	if (!scenario.empty()) {
		spec.process.arguments.push_back("--scenario");
		spec.process.arguments.push_back(scenario);
	}
	return expect(session.start(spec, errorMessage), "shaper start: " + errorMessage, failureReason);
}

bool pollUntilMessages(mr::lsp::LspSession &session, std::vector<mr::lsp::LspInboundMessage> &messages, std::string &failureReason) {
	std::string errorMessage;

	for (int index = 0; index < 60; ++index) {
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

bool pollUntilMatchedResponse(mr::lsp::LspSession &session, const std::string &method, mr::lsp::LspInboundMessage &response, std::string &failureReason) {
	std::vector<mr::lsp::LspInboundMessage> messages;

	for (int index = 0; index < 60; ++index) {
		if (!pollUntilMessages(session, messages, failureReason)) return false;
		for (std::size_t messageIndex = 0; messageIndex < messages.size(); ++messageIndex) {
			const mr::lsp::LspInboundMessage &message = messages[messageIndex];
			if (!message.matchedPendingRequest) continue;
			if (message.pendingRequest.method != method) continue;
			response = message;
			return true;
		}
	}
	failureReason = "matched response not observed: " + method;
	return false;
}

bool sendInitialize(mr::lsp::LspSession &session, std::string &failureReason) {
	mr::lsp::JsonRpcPendingRequest request;
	mr::lsp::LspInboundMessage response;
	std::string errorMessage;

	if (!expect(session.sendRequest("initialize", "{\"processId\":null,\"rootUri\":null,\"capabilities\":{}}", request, errorMessage), "initialize send: " + errorMessage, failureReason)) return false;
	if (!pollUntilMatchedResponse(session, "initialize", response, failureReason)) return false;
	return expect(response.payload.find("\"completionProvider\"") != std::string::npos, "initialize capabilities", failureReason);
}

bool sendShutdownAndExit(mr::lsp::LspSession &session, std::string &failureReason) {
	mr::lsp::JsonRpcPendingRequest request;
	mr::lsp::LspInboundMessage response;
	std::string errorMessage;
	int exitStatus = -1;

	if (!expect(session.sendRequest("shutdown", "null", request, errorMessage), "shutdown send: " + errorMessage, failureReason)) return false;
	if (!pollUntilMatchedResponse(session, "shutdown", response, failureReason)) return false;
	if (!expect(response.payload.find("\"result\":null") != std::string::npos, "shutdown result", failureReason)) return false;
	if (!expect(session.sendNotification("exit", "null", errorMessage), "exit send: " + errorMessage, failureReason)) return false;
	if (!expect(session.wait(1000, exitStatus), "wait after exit", failureReason)) return false;
	return expect(exitStatus == 0, "exit status", failureReason);
}

bool expectNotification(mr::lsp::LspSession &session, const std::string &method, const std::string &payloadNeedle, std::string &failureReason) {
	std::vector<mr::lsp::LspInboundMessage> messages;

	for (int index = 0; index < 60; ++index) {
		if (!pollUntilMessages(session, messages, failureReason)) return false;
		for (std::size_t messageIndex = 0; messageIndex < messages.size(); ++messageIndex) {
			const mr::lsp::LspInboundMessage &message = messages[messageIndex];
			if (message.envelope.method != method) continue;
			if (message.payload.find(payloadNeedle) == std::string::npos) continue;
			return true;
		}
	}
	failureReason = "notification not observed: " + method + " / " + payloadNeedle;
	return false;
}

bool expectDiagnosticsPair(mr::lsp::LspSession &session, const std::string &firstNeedle, const std::string &secondNeedle, std::string &failureReason) {
	std::vector<mr::lsp::LspInboundMessage> messages;
	bool sawFirst = false;
	bool sawSecond = false;

	for (int index = 0; index < 60; ++index) {
		if (!pollUntilMessages(session, messages, failureReason)) return false;
		for (std::size_t messageIndex = 0; messageIndex < messages.size(); ++messageIndex) {
			const mr::lsp::LspInboundMessage &message = messages[messageIndex];
			if (message.envelope.method != "textDocument/publishDiagnostics") continue;
			if (message.payload.find(firstNeedle) != std::string::npos) sawFirst = true;
			if (message.payload.find(secondNeedle) != std::string::npos) sawSecond = true;
		}
		if (sawFirst && sawSecond) return true;
	}
	failureReason = "diagnostics pair not observed: " + firstNeedle + " / " + secondNeedle;
	return false;
}

bool requestAndExpectPayload(mr::lsp::LspSession &session, const std::string &method, const std::string &paramsJson, const std::string &payloadNeedle, std::string &failureReason) {
	mr::lsp::JsonRpcPendingRequest request;
	mr::lsp::LspInboundMessage response;
	std::string errorMessage;

	if (!expect(session.sendRequest(method, paramsJson, request, errorMessage), method + " send: " + errorMessage, failureReason)) return false;
	if (!pollUntilMatchedResponse(session, method, response, failureReason)) return false;
	return expect(response.payload.find(payloadNeedle) != std::string::npos, method + " payload", failureReason);
}

bool testProtocolChannels(std::string &failureReason) {
	mr::lsp::LspSession session;
	std::string errorMessage;
	const std::string uri = "file:///tmp/mr-lsp-shaper.c";
	const std::string documentParams = "{\"textDocument\":{\"uri\":\"file:///tmp/mr-lsp-shaper.c\",\"languageId\":\"c\",\"version\":1,\"text\":\"int main(void){return 0;}\\n\"}}";
	const std::string changedParams = "{\"textDocument\":{\"uri\":\"file:///tmp/mr-lsp-shaper.c\",\"version\":2},\"contentChanges\":[{\"text\":\"int main(void){return 1;}\\n\"}]}";
	const std::string requestParams = "{\"textDocument\":{\"uri\":\"file:///tmp/mr-lsp-shaper.c\"},\"position\":{\"line\":0,\"character\":4}}";
	const std::string codeActionParams = "{\"textDocument\":{\"uri\":\"file:///tmp/mr-lsp-shaper.c\"},\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":1}},\"context\":{\"diagnostics\":[]}}";

	if (!startShaper(session, "", failureReason)) return false;
	if (!sendInitialize(session, failureReason)) return false;
	if (!expect(session.sendNotification("initialized", "{}", errorMessage), "initialized send: " + errorMessage, failureReason)) return false;
	if (!expect(session.sendNotification("textDocument/didOpen", documentParams, errorMessage), "didOpen send: " + errorMessage, failureReason)) return false;
	if (!expectNotification(session, "textDocument/publishDiagnostics", "protocol shaper opened document", failureReason)) return false;
	if (!expect(session.sendNotification("textDocument/didChange", changedParams, errorMessage), "didChange send: " + errorMessage, failureReason)) return false;
	if (!expectDiagnosticsPair(session, "protocol shaper stale diagnostic", "protocol shaper changed document", failureReason)) return false;
	if (!requestAndExpectPayload(session, "textDocument/definition", requestParams, uri, failureReason)) return false;
	if (!requestAndExpectPayload(session, "textDocument/references", requestParams, "\"result\":[", failureReason)) return false;
	if (!requestAndExpectPayload(session, "textDocument/hover", requestParams, "mr protocol shaper hover", failureReason)) return false;
	if (!requestAndExpectPayload(session, "textDocument/completion", requestParams, "shaperCompletionOne", failureReason)) return false;
	if (!requestAndExpectPayload(session, "textDocument/codeAction", codeActionParams, "protocol shaper quick fix", failureReason)) return false;
	if (!expect(session.sendNotification("textDocument/didClose", "{\"textDocument\":{\"uri\":\"file:///tmp/mr-lsp-shaper.c\"}}", errorMessage), "didClose send: " + errorMessage, failureReason)) return false;
	if (!expectNotification(session, "textDocument/publishDiagnostics", "protocol shaper closed document", failureReason)) return false;
	return sendShutdownAndExit(session, failureReason);
}

bool testUnexpectedMessages(std::string &failureReason) {
	mr::lsp::LspSession session;
	std::vector<mr::lsp::LspInboundMessage> messages;
	std::string errorMessage;
	bool sawUnknownResponse = false;
	bool sawProgressNotification = false;

	if (!startShaper(session, "unexpected", failureReason)) return false;
	if (!sendInitialize(session, failureReason)) return false;
	if (!expect(session.sendNotification("initialized", "{}", errorMessage), "initialized send: " + errorMessage, failureReason)) return false;
	for (int index = 0; index < 60; ++index) {
		if (!pollUntilMessages(session, messages, failureReason)) return false;
		for (std::size_t messageIndex = 0; messageIndex < messages.size(); ++messageIndex) {
			const mr::lsp::LspInboundMessage &message = messages[messageIndex];
			if (message.envelope.kind == mr::lsp::JsonRpcMessageKind::Response && message.envelope.idText == "999" && !message.matchedPendingRequest)
				sawUnknownResponse = true;
			if (message.envelope.method == "$/progress")
				sawProgressNotification = true;
		}
		if (sawUnknownResponse && sawProgressNotification) break;
	}
	if (!expect(sawUnknownResponse, "unexpected response observed", failureReason)) return false;
	if (!expect(sawProgressNotification, "progress notification observed", failureReason)) return false;
	return sendShutdownAndExit(session, failureReason);
}

bool testMalformedFrame(std::string &failureReason) {
	mr::lsp::LspSession session;
	std::vector<mr::lsp::LspInboundMessage> messages;
	std::string errorMessage;

	if (!startShaper(session, "malformed", failureReason)) return false;
	for (int index = 0; index < 60; ++index) {
		if (!session.poll(messages, errorMessage))
			return expect(errorMessage.find("Content-Length") != std::string::npos, "malformed frame error text", failureReason);
		::poll(nullptr, 0, 20);
	}
	failureReason = "malformed frame was accepted";
	return false;
}

bool testCrashAfterInitialize(std::string &failureReason) {
	mr::lsp::LspSession session;
	int exitStatus = -1;

	if (!startShaper(session, "crash-after-initialize", failureReason)) return false;
	if (!sendInitialize(session, failureReason)) return false;
	if (!expect(session.wait(1000, exitStatus), "wait after shaper crash", failureReason)) return false;
	return expect(exitStatus == 23, "shaper crash exit status", failureReason);
}

bool testDelayedCompletion(std::string &failureReason) {
	mr::lsp::LspSession session;
	std::string errorMessage;
	const std::string documentParams = "{\"textDocument\":{\"uri\":\"file:///tmp/mr-lsp-shaper.c\",\"languageId\":\"c\",\"version\":1,\"text\":\"int main(void){return 0;}\\n\"}}";
	const std::string requestParams = "{\"textDocument\":{\"uri\":\"file:///tmp/mr-lsp-shaper.c\"},\"position\":{\"line\":0,\"character\":4}}";

	if (!startShaper(session, "delayed", failureReason)) return false;
	if (!sendInitialize(session, failureReason)) return false;
	if (!expect(session.sendNotification("initialized", "{}", errorMessage), "initialized send: " + errorMessage, failureReason)) return false;
	if (!expect(session.sendNotification("textDocument/didOpen", documentParams, errorMessage), "didOpen send: " + errorMessage, failureReason)) return false;
	if (!expectNotification(session, "textDocument/publishDiagnostics", "protocol shaper opened document", failureReason)) return false;
	if (!requestAndExpectPayload(session, "textDocument/completion", requestParams, "shaperCompletionTwo", failureReason)) return false;
	return sendShutdownAndExit(session, failureReason);
}

bool runProbe(std::string &failureReason) {
	if (!testProtocolChannels(failureReason)) {
		failureReason = "protocol channels: " + failureReason;
		return false;
	}
	if (!testUnexpectedMessages(failureReason)) {
		failureReason = "unexpected messages: " + failureReason;
		return false;
	}
	if (!testMalformedFrame(failureReason)) {
		failureReason = "malformed frame: " + failureReason;
		return false;
	}
	if (!testCrashAfterInitialize(failureReason)) {
		failureReason = "crash after initialize: " + failureReason;
		return false;
	}
	if (!testDelayedCompletion(failureReason)) {
		failureReason = "delayed completion: " + failureReason;
		return false;
	}
	return true;
}

} // namespace

int main() {
	std::string failureReason;

	if (!runProbe(failureReason)) {
		std::cerr << "mr_lsp_protocol_shaper_probe failed: " << failureReason << "\n";
		return 1;
	}

	std::cout << "mr_lsp_protocol_shaper_probe passed\n";
	return 0;
}
