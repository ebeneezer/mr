#include <iostream>
#include <poll.h>
#include <string>
#include <vector>

#include "../lsp/MRLspHover.hpp"

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

bool pollHover(mr::lsp::LspLifecycle &lifecycle, const mr::lsp::LspDocumentService &service, mr::lsp::LspHoverAdapter &adapter, mr::lsp::LspHoverRequest &request, mr::lsp::LspHoverResult &result, std::string &failureReason) {
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
				failureReason = "hover consume failed: " + errorMessage;
				return false;
			}
			if (accepted) return true;
		}
		::poll(nullptr, 0, 20);
	}
	failureReason = "expected hover response not observed";
	return false;
}

bool testHoverHappyPath(std::string &failureReason) {
	mr::lsp::LspLifecycle lifecycle;
	mr::lsp::LspDocumentService service(lifecycle);
	mr::lsp::LspHoverAdapter adapter;
	mr::lsp::LspHoverRequest request;
	mr::lsp::LspHoverResult result;
	std::string errorMessage;

	if (!startLifecycle(lifecycle, failureReason)) return false;
	if (!expect(service.open(makeSourceSnapshot(1, "int main() { return 0; }\n"), errorMessage), "open: " + errorMessage, failureReason)) return false;
	if (!expect(adapter.requestHover(lifecycle, service, mr::lsp::LspTextPosition{3, 5}, request, errorMessage), "hover request: " + errorMessage, failureReason)) return false;
	if (!expect(request.pending, "hover request pending", failureReason)) return false;
	if (!expect(request.method == "textDocument/hover", "hover request method", failureReason)) return false;
	if (!pollHover(lifecycle, service, adapter, request, result, failureReason)) return false;
	if (!expect(!request.pending, "hover request still pending", failureReason)) return false;
	if (!expect(result.uri == service.documentUri(), "hover uri", failureReason)) return false;
	if (!expect(result.kind == "markdown", "hover kind", failureReason)) return false;
	if (!expect(result.value.find("Deterministic hover text.") != std::string::npos, "hover value", failureReason)) return false;
	if (!expect(service.close(errorMessage), "close: " + errorMessage, failureReason)) return false;
	return shutdownLifecycle(lifecycle, failureReason);
}

bool testHoverGuards(std::string &failureReason) {
	mr::lsp::LspLifecycle lifecycle;
	mr::lsp::LspDocumentService service(lifecycle);
	mr::lsp::LspHoverAdapter adapter;
	mr::lsp::LspHoverRequest request;
	std::string errorMessage;

	if (!expect(!adapter.requestHover(lifecycle, service, mr::lsp::LspTextPosition{0, 0}, request, errorMessage), "hover without document accepted", failureReason)) return false;
	if (!expect(!request.pending, "failed hover pending", failureReason)) return false;
	if (!startLifecycle(lifecycle, failureReason)) return false;
	if (!expect(service.open(makeSourceSnapshot(1, "one"), errorMessage), "guard open: " + errorMessage, failureReason)) return false;
	if (!expect(!adapter.requestHover(lifecycle, service, mr::lsp::LspTextPosition{0, -1}, request, errorMessage), "negative hover position accepted", failureReason)) return false;
	return shutdownLifecycle(lifecycle, failureReason);
}

bool runProbe(std::string &failureReason) {
	if (!testHoverHappyPath(failureReason)) return false;
	if (!testHoverGuards(failureReason)) return false;
	return true;
}
} // namespace

int main() {
	std::string failureReason;

	if (!runProbe(failureReason)) {
		std::cerr << "mr_lsp_hover_probe failed: " << failureReason << "\n";
		return 1;
	}

	std::cout << "mr_lsp_hover_probe passed\n";
	return 0;
}
