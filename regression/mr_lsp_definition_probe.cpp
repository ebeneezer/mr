#include <iostream>
#include <poll.h>
#include <string>
#include <vector>

#include "../lsp/MRLspDefinition.hpp"

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

bool pollDefinition(mr::lsp::LspLifecycle &lifecycle, const mr::lsp::LspDocumentService &service, mr::lsp::LspDefinitionAdapter &adapter, mr::lsp::LspDefinitionRequest &request, mr::lsp::LspLocation &location, std::string &failureReason) {
	std::string errorMessage;
	std::vector<mr::lsp::LspInboundMessage> messages;

	for (int i = 0; i < 50; ++i) {
		if (!lifecycle.poll(messages, errorMessage)) {
			failureReason = "poll failed: " + errorMessage;
			return false;
		}
		for (const mr::lsp::LspInboundMessage &message : messages) {
			bool accepted = false;
			if (!adapter.consume(message, service, request, location, accepted, errorMessage)) {
				failureReason = "definition consume failed: " + errorMessage;
				return false;
			}
			if (accepted) return true;
		}
		::poll(nullptr, 0, 20);
	}
	failureReason = "expected definition response not observed";
	return false;
}

bool testDefinitionHappyPath(std::string &failureReason) {
	mr::lsp::LspLifecycle lifecycle;
	mr::lsp::LspDocumentService service(lifecycle);
	mr::lsp::LspDefinitionAdapter adapter;
	mr::lsp::LspDefinitionRequest request;
	mr::lsp::LspLocation location;
	std::string errorMessage;

	if (!startLifecycle(lifecycle, failureReason)) return false;
	if (!expect(service.open(makeSourceSnapshot(1, "int main() { return 0; }\n"), errorMessage), "open: " + errorMessage, failureReason)) return false;
	if (!expect(adapter.requestDefinition(lifecycle, service, mr::lsp::LspTextPosition{3, 5}, request, errorMessage), "definition request: " + errorMessage, failureReason)) return false;
	if (!expect(request.pending, "definition request pending", failureReason)) return false;
	if (!expect(request.method == "textDocument/definition", "definition request method", failureReason)) return false;
	if (!pollDefinition(lifecycle, service, adapter, request, location, failureReason)) return false;
	if (!expect(!request.pending, "definition request still pending", failureReason)) return false;
	if (!expect(location.uri == service.documentUri(), "definition uri", failureReason)) return false;
	if (!expect(location.start.line == 4, "definition start line", failureReason)) return false;
	if (!expect(location.start.character == 2, "definition start character", failureReason)) return false;
	if (!expect(location.end.line == 4, "definition end line", failureReason)) return false;
	if (!expect(location.end.character == 9, "definition end character", failureReason)) return false;
	if (!expect(service.close(errorMessage), "close: " + errorMessage, failureReason)) return false;
	return shutdownLifecycle(lifecycle, failureReason);
}

bool testDefinitionGuards(std::string &failureReason) {
	mr::lsp::LspLifecycle lifecycle;
	mr::lsp::LspDocumentService service(lifecycle);
	mr::lsp::LspDefinitionAdapter adapter;
	mr::lsp::LspDefinitionRequest request;
	std::string errorMessage;

	if (!expect(!adapter.requestDefinition(lifecycle, service, mr::lsp::LspTextPosition{0, 0}, request, errorMessage), "definition without document accepted", failureReason)) return false;
	if (!expect(!request.pending, "failed request pending", failureReason)) return false;
	if (!startLifecycle(lifecycle, failureReason)) return false;
	if (!expect(service.open(makeSourceSnapshot(1, "one"), errorMessage), "guard open: " + errorMessage, failureReason)) return false;
	if (!expect(!adapter.requestDefinition(lifecycle, service, mr::lsp::LspTextPosition{-1, 0}, request, errorMessage), "negative position accepted", failureReason)) return false;
	return shutdownLifecycle(lifecycle, failureReason);
}

bool runProbe(std::string &failureReason) {
	if (!testDefinitionHappyPath(failureReason)) return false;
	if (!testDefinitionGuards(failureReason)) return false;
	return true;
}
} // namespace

int main() {
	std::string failureReason;

	if (!runProbe(failureReason)) {
		std::cerr << "mr_lsp_definition_probe failed: " << failureReason << "\n";
		return 1;
	}

	std::cout << "mr_lsp_definition_probe passed\n";
	return 0;
}
