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

	for (int i = 0; i < 50; ++i) {
		if (!lifecycle.poll(messages, errorMessage)) {
			failureReason = "poll failed: " + errorMessage;
			return false;
		}
		if (lifecycle.state() == expectedState) return true;
		::poll(nullptr, 0, 20);
	}
	failureReason = "expected lifecycle state not observed";
	return false;
}

bool startLifecycle(mr::lsp::LspLifecycle &lifecycle, std::string &failureReason) {
	mr::lsp::LspInitializeSpec spec;
	std::string errorMessage;

	spec.session.process.executablePath = "./regression/mr_lsp_session_peer";
	spec.initializeParamsJson = "{\"processId\":null,\"rootUri\":null,\"capabilities\":{}}";
	if (!expect(lifecycle.start(spec, errorMessage), "lifecycle start: " + errorMessage, failureReason)) return false;
	if (!pollLifecycleUntilState(lifecycle, mr::lsp::LspLifecycleState::Initialized, failureReason)) return false;
	return expect(lifecycle.sendInitialized(errorMessage), "initialized send: " + errorMessage, failureReason);
}

bool shutdownLifecycle(mr::lsp::LspLifecycle &lifecycle, std::string &failureReason) {
	std::string errorMessage;
	int exitStatus = -1;

	if (!expect(lifecycle.shutdown(errorMessage), "shutdown: " + errorMessage, failureReason)) return false;
	if (!pollLifecycleUntilState(lifecycle, mr::lsp::LspLifecycleState::Shutdown, failureReason)) return false;
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

bool testCompletionHappyPath(std::string &failureReason) {
	mr::lsp::LspLifecycle lifecycle;
	mr::lsp::LspDocumentService service(lifecycle);
	mr::lsp::LspCompletionAdapter adapter;
	mr::lsp::LspCompletionRequest request;
	mr::lsp::LspCompletionResult result;
	std::string errorMessage;

	if (!startLifecycle(lifecycle, failureReason)) return false;
	if (!expect(service.open(makeSourceSnapshot(1, "int main() { return 0; }\n"), errorMessage), "open: " + errorMessage, failureReason)) return false;
	if (!expect(adapter.requestCompletion(lifecycle, service, mr::lsp::LspTextPosition{3, 5}, request, errorMessage), "completion request: " + errorMessage, failureReason)) return false;
	if (!expect(request.pending, "completion request pending", failureReason)) return false;
	if (!expect(request.method == "textDocument/completion", "completion request method", failureReason)) return false;
	if (!pollCompletion(lifecycle, service, adapter, request, result, failureReason)) return false;
	if (!expect(!request.pending, "completion request still pending", failureReason)) return false;
	if (!expect(result.uri == service.documentUri(), "completion uri", failureReason)) return false;
	if (!expect(result.items.size() == 2, "completion item count", failureReason)) return false;
	if (!expect(result.items[0].label == "main", "completion first label", failureReason)) return false;
	if (!expect(result.items[0].hasKind, "completion first kind missing", failureReason)) return false;
	if (!expect(result.items[0].kind == 3, "completion first kind", failureReason)) return false;
	if (!expect(result.items[0].detail == "int main()", "completion first detail", failureReason)) return false;
	if (!expect(result.items[0].insertText == "main", "completion first insert text", failureReason)) return false;
	if (!expect(result.items[1].label == "macroValue", "completion second label", failureReason)) return false;
	if (!expect(result.items[1].insertText.empty(), "completion optional insert text", failureReason)) return false;
	if (!expect(service.close(errorMessage), "close: " + errorMessage, failureReason)) return false;
	return shutdownLifecycle(lifecycle, failureReason);
}

bool testCompletionGuards(std::string &failureReason) {
	mr::lsp::LspLifecycle lifecycle;
	mr::lsp::LspDocumentService service(lifecycle);
	mr::lsp::LspCompletionAdapter adapter;
	mr::lsp::LspCompletionRequest request;
	std::string errorMessage;

	if (!expect(!adapter.requestCompletion(lifecycle, service, mr::lsp::LspTextPosition{0, 0}, request, errorMessage), "completion without document accepted", failureReason)) return false;
	if (!expect(!request.pending, "failed completion pending", failureReason)) return false;
	if (!startLifecycle(lifecycle, failureReason)) return false;
	if (!expect(service.open(makeSourceSnapshot(1, "one"), errorMessage), "guard open: " + errorMessage, failureReason)) return false;
	if (!expect(!adapter.requestCompletion(lifecycle, service, mr::lsp::LspTextPosition{0, -1}, request, errorMessage), "negative completion position accepted", failureReason)) return false;
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
