#include <iostream>
#include <poll.h>
#include <string>
#include <vector>

#include "../lsp/MRLspCompletion.hpp"

namespace {
bool expect(bool condition, const std::string &name, std::string &failureReason) {
	if (condition) return true;
	failureReason = name;
	return false;
}

mr::lsp::LspDocumentSourceSnapshot makeSourceSnapshot(std::int64_t version, const std::string &text) {
	mr::lsp::LspDocumentSourceSnapshot snapshot;

	snapshot.absolutePath = "/tmp/mr document.cpp";
	snapshot.languageId = "cpp";
	snapshot.version = version;
	snapshot.text = text;
	return snapshot;
}

bool pollLifecycleUntilState(mr::lsp::LspLifecycle &lifecycle, mr::lsp::LspLifecycleState expectedState, std::string &failureReason) {
	std::string errorMessage;
	std::vector<mr::lsp::LspInboundMessage> messages;
	std::size_t seenMessages = 0;
	std::string matchedText;

	for (int i = 0; i < 50; ++i) {
		if (!lifecycle.poll(messages, errorMessage)) {
			failureReason = "poll failed: " + errorMessage;
			return false;
		}
		seenMessages += messages.size();
		for (const mr::lsp::LspInboundMessage &message : messages) {
			if (!message.matchedPendingRequest) continue;
			if (!matchedText.empty()) matchedText += ";";
			matchedText += " matched=" + message.pendingRequest.method + "/" + message.pendingRequest.idText;
		}
		if (lifecycle.state() == expectedState) return true;
		::poll(nullptr, 0, 20);
	}
	failureReason = std::string("expected lifecycle state not observed, state=") + mr::lsp::lspLifecycleStateName(lifecycle.state()) + ", messages=" + std::to_string(seenMessages) + " " + matchedText;
	return false;
}

bool startLifecycle(mr::lsp::LspLifecycle &lifecycle, std::string &failureReason) {
	mr::lsp::LspInitializeSpec spec;
	std::string errorMessage;

	spec.session.process.executablePath = "./regression/mr_lsp_session_peer";
	spec.initializeParamsJson = "{\"processId\":null,\"rootUri\":null,\"capabilities\":{}}";
	if (!expect(lifecycle.start(spec, errorMessage), "lifecycle start: " + errorMessage, failureReason)) return false;
	if (!pollLifecycleUntilState(lifecycle, mr::lsp::LspLifecycleState::Initialized, failureReason)) {
		failureReason = "start initialize: " + failureReason;
		return false;
	}
	return expect(lifecycle.sendInitialized(errorMessage), "initialized send: " + errorMessage, failureReason);
}

bool shutdownLifecycle(mr::lsp::LspLifecycle &lifecycle, std::string &failureReason) {
	std::string errorMessage;
	int exitStatus = -1;

	if (!expect(lifecycle.shutdown(errorMessage), "shutdown: " + errorMessage, failureReason)) return false;
	if (!pollLifecycleUntilState(lifecycle, mr::lsp::LspLifecycleState::Shutdown, failureReason)) {
		failureReason = "shutdown wait: " + failureReason;
		return false;
	}
	if (!expect(lifecycle.exit(errorMessage), "exit: " + errorMessage, failureReason)) return false;
	if (!expect(lifecycle.wait(1000, exitStatus), "wait", failureReason)) return false;
	return expect(exitStatus == 0, "exit status", failureReason);
}

bool pollCompletion(mr::lsp::LspLifecycle &lifecycle, const mr::lsp::LspDocumentService &service, mr::lsp::LspCompletionAdapter &adapter, mr::lsp::LspCompletionRequest &request, mr::lsp::LspCompletionResult &result, std::string &failureReason) {
	std::string errorMessage;
	std::vector<mr::lsp::LspInboundMessage> messages;

	for (int i = 0; i < 50; ++i) {
		if (!lifecycle.poll(messages, errorMessage)) {
			failureReason = "poll failed: " + errorMessage;
			return false;
		}
		for (const mr::lsp::LspInboundMessage &message : messages) {
			bool accepted = false;
			if (!adapter.consume(message, service, request, result, accepted, errorMessage)) {
				failureReason = "completion consume failed: " + errorMessage;
				return false;
			}
			if (accepted) return true;
		}
		::poll(nullptr, 0, 20);
	}
	failureReason = "expected completion response not observed";
	return false;
}

bool pollCompletionResolve(mr::lsp::LspLifecycle &lifecycle, mr::lsp::LspCompletionAdapter &adapter, mr::lsp::LspCompletionResolveRequest &request, mr::lsp::LspCompletionResolveResult &result, std::string &failureReason) {
	std::string errorMessage;
	std::vector<mr::lsp::LspInboundMessage> messages;

	for (int i = 0; i < 50; ++i) {
		if (!lifecycle.poll(messages, errorMessage)) {
			failureReason = "poll failed: " + errorMessage;
			return false;
		}
		for (const mr::lsp::LspInboundMessage &message : messages) {
			bool accepted = false;
			if (!adapter.consumeResolve(message, request, result, accepted, errorMessage)) {
				failureReason = "completion resolve consume failed: " + errorMessage;
				return false;
			}
			if (accepted) return true;
		}
		::poll(nullptr, 0, 20);
	}
	failureReason = "expected completion resolve response not observed";
	return false;
}

bool testCompletionHappyPath(std::string &failureReason) {
	mr::lsp::LspLifecycle lifecycle;
	mr::lsp::LspDocumentService service(lifecycle);
	mr::lsp::LspCompletionAdapter adapter;
	mr::lsp::LspCompletionRequest request;
	mr::lsp::LspCompletionResult result;
	mr::lsp::LspCompletionResolveRequest resolveRequest;
	mr::lsp::LspCompletionResolveResult resolveResult;
	std::string errorMessage;

	if (!startLifecycle(lifecycle, failureReason)) return false;
	if (!expect(service.open(makeSourceSnapshot(1, "int main() { return 0; }\n"), errorMessage), "open: " + errorMessage, failureReason)) return false;
	if (!expect(adapter.requestCompletion(lifecycle, service, mr::lsp::LspTextPosition{3, 5}, std::string(), request, errorMessage), "completion request: " + errorMessage, failureReason)) return false;
	if (!expect(request.pending, "completion request pending", failureReason)) return false;
	if (!expect(request.method == "textDocument/completion", "completion request method", failureReason)) return false;
	if (!pollCompletion(lifecycle, service, adapter, request, result, failureReason)) return false;
	if (!expect(!request.pending, "completion request still pending", failureReason)) return false;
	if (!expect(result.uri == service.documentUri(), "completion uri", failureReason)) return false;
	if (!expect(result.items.size() == 3, "completion item count", failureReason)) return false;
	if (!expect(result.items[0].label == "main", "completion first label", failureReason)) return false;
	if (!expect(result.items[0].hasKind, "completion first kind missing", failureReason)) return false;
	if (!expect(result.items[0].kind == 3, "completion first kind", failureReason)) return false;
	if (!expect(result.items[0].detail == "int main()", "completion first detail", failureReason)) return false;
	if (!expect(result.items[0].insertText == "main", "completion first insert text", failureReason)) return false;
	if (!expect(result.items[0].hasTextEdit, "completion first text edit missing", failureReason)) return false;
	if (!expect(result.items[0].textEditStart.line == 0 && result.items[0].textEditStart.character == 1, "completion first text edit start", failureReason)) return false;
	if (!expect(result.items[0].textEditEnd.line == 0 && result.items[0].textEditEnd.character == 3, "completion first text edit end", failureReason)) return false;
	if (!expect(result.items[0].textEditNewText == "main", "completion first text edit new text", failureReason)) return false;
	if (!expect(result.items[1].label == "for", "completion snippet label", failureReason)) return false;
	if (!expect(result.items[1].hasInsertTextFormat, "completion snippet format missing", failureReason)) return false;
	if (!expect(result.items[1].insertTextFormat == 2, "completion snippet format", failureReason)) return false;
	if (!expect(result.items[1].insertText.find("${1:int i = 0}") != std::string::npos, "completion snippet insert text", failureReason)) return false;
	if (!expect(result.items[2].label == "macroValue", "completion third label", failureReason)) return false;
	if (!expect(result.items[2].insertText.empty(), "completion optional insert text", failureReason)) return false;
	if (!expect(adapter.requestResolve(lifecycle, result.items[2], resolveRequest, errorMessage), "completion resolve request: " + errorMessage, failureReason)) return false;
	if (!pollCompletionResolve(lifecycle, adapter, resolveRequest, resolveResult, failureReason)) return false;
	if (!expect(resolveResult.item.label == "macroValue", "completion resolve label", failureReason)) return false;
	if (!expect(resolveResult.item.documentation == "Resolved macro documentation.", "completion resolve documentation", failureReason)) return false;
	if (!expect(resolveResult.item.insertText == "macroValue", "completion resolve insert text", failureReason)) return false;
	if (!expect(adapter.requestCompletion(lifecycle, service, mr::lsp::LspTextPosition{4, 1}, "\\", request, errorMessage), "triggered completion request: " + errorMessage, failureReason)) return false;
	if (!expect(request.hasTriggerCharacter && request.triggerCharacter == "\\", "triggered completion metadata", failureReason)) return false;
	if (!pollCompletion(lifecycle, service, adapter, request, result, failureReason)) return false;
	if (!expect(result.items.size() == 3, "triggered completion item count", failureReason)) return false;
	if (!expect(service.close(errorMessage), "close: " + errorMessage, failureReason)) return false;
	return shutdownLifecycle(lifecycle, failureReason);
}

bool testCompletionGuards(std::string &failureReason) {
	mr::lsp::LspLifecycle lifecycle;
	mr::lsp::LspDocumentService service(lifecycle);
	mr::lsp::LspCompletionAdapter adapter;
	mr::lsp::LspCompletionRequest request;
	std::string errorMessage;

	if (!expect(!adapter.requestCompletion(lifecycle, service, mr::lsp::LspTextPosition{0, 0}, std::string(), request, errorMessage), "completion without document accepted", failureReason)) return false;
	if (!expect(!request.pending, "failed completion pending", failureReason)) return false;
	if (!startLifecycle(lifecycle, failureReason)) return false;
	if (!expect(service.open(makeSourceSnapshot(1, "one"), errorMessage), "guard open: " + errorMessage, failureReason)) return false;
	if (!expect(!adapter.requestCompletion(lifecycle, service, mr::lsp::LspTextPosition{0, -1}, std::string(), request, errorMessage), "negative completion position accepted", failureReason)) return false;
	return shutdownLifecycle(lifecycle, failureReason);
}

bool runProbe(std::string &failureReason) {
	if (!testCompletionHappyPath(failureReason)) return false;
	if (!testCompletionGuards(failureReason)) return false;
	return true;
}
} // namespace

int main() {
	std::string failureReason;

	if (!runProbe(failureReason)) {
		std::cerr << "mr_lsp_completion_probe failed: " << failureReason << "\n";
		return 1;
	}

	std::cout << "mr_lsp_completion_probe passed\n";
	return 0;
}
