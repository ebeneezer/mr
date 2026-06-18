#include <iostream>
#include <poll.h>
#include <string>
#include <vector>

#include "../lsp/MRLspDocumentService.hpp"

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

bool pollUntilPayloadContains(mr::lsp::LspLifecycle &lifecycle, const std::string &needle, std::string &failureReason) {
	std::string errorMessage;
	std::vector<mr::lsp::LspInboundMessage> messages;

	for (int i = 0; i < 50; ++i) {
		if (!lifecycle.poll(messages, errorMessage)) {
			failureReason = "poll failed: " + errorMessage;
			return false;
		}
		for (const mr::lsp::LspInboundMessage &message : messages)
			if (message.payload.find(needle) != std::string::npos) return true;
		::poll(nullptr, 0, 20);
	}
	failureReason = "expected payload not observed: " + needle;
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

bool queryDocumentCounts(mr::lsp::LspLifecycle &lifecycle, std::string &failureReason) {
	std::string errorMessage;
	const std::string request = "{\"jsonrpc\":\"2.0\",\"id\":9001,\"method\":\"mr/documentCounts\",\"params\":null}";

	if (!expect(lifecycle.sendInitializedPayload(request, errorMessage), "document count request: " + errorMessage, failureReason)) return false;
	if (!pollUntilPayloadContains(lifecycle, "\"didOpen\":1", failureReason)) return false;
	if (!expect(lifecycle.sendInitializedPayload(request, errorMessage), "document count request repeat: " + errorMessage, failureReason)) return false;
	if (!pollUntilPayloadContains(lifecycle, "\"didChange\":1", failureReason)) return false;
	if (!expect(lifecycle.sendInitializedPayload(request, errorMessage), "document count request final: " + errorMessage, failureReason)) return false;
	return pollUntilPayloadContains(lifecycle, "\"didClose\":1", failureReason);
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

bool testDocumentServiceHappyPath(std::string &failureReason) {
	mr::lsp::LspLifecycle lifecycle;
	mr::lsp::LspDocumentService service(lifecycle);
	std::string errorMessage;

	if (!startLifecycle(lifecycle, failureReason)) return false;
	if (!expect(service.open(makeSourceSnapshot(1, "int main() { return 0; }\n"), errorMessage), "open: " + errorMessage, failureReason)) return false;
	if (!expect(service.isOpen(), "service open state", failureReason)) return false;
	if (!expect(service.documentUri() == "file:///tmp/mr%20document.cpp", "service uri", failureReason)) return false;
	if (!expect(service.change(makeSourceSnapshot(2, "int main() { return 1; }\n"), errorMessage), "change: " + errorMessage, failureReason)) return false;
	if (!expect(service.sentVersion() == 2, "service sent version", failureReason)) return false;
	if (!expect(service.matchesSentVersion(2), "service version match", failureReason)) return false;
	if (!expect(service.isStaleForSentVersion(1), "service stale version", failureReason)) return false;
	if (!expect(!service.change(makeSourceSnapshot(2, "same version"), errorMessage), "same version accepted", failureReason)) return false;
	if (!expect(service.sentVersion() == 2, "same version mutated service", failureReason)) return false;
	if (!expect(service.close(errorMessage), "close: " + errorMessage, failureReason)) return false;
	if (!expect(!service.isOpen(), "service closed state", failureReason)) return false;
	if (!queryDocumentCounts(lifecycle, failureReason)) return false;
	return shutdownLifecycle(lifecycle, failureReason);
}

bool testDocumentServiceGuards(std::string &failureReason) {
	mr::lsp::LspLifecycle lifecycle;
	mr::lsp::LspDocumentService service(lifecycle);
	std::string errorMessage;

	if (!expect(!service.open(makeSourceSnapshot(1, "one"), errorMessage), "open before lifecycle accepted", failureReason)) return false;
	if (!expect(!service.isOpen(), "failed open mutated service", failureReason)) return false;
	lifecycle.close();
	if (!expect(startLifecycle(lifecycle, failureReason), "guard lifecycle start", failureReason)) return false;
	mr::lsp::LspDocumentSourceSnapshot invalid = makeSourceSnapshot(1, "one");
	invalid.absolutePath = "relative.cpp";
	if (!expect(!service.open(invalid, errorMessage), "relative path accepted", failureReason)) return false;
	if (!expect(!service.isOpen(), "relative path mutated service", failureReason)) return false;
	return shutdownLifecycle(lifecycle, failureReason);
}

bool runProbe(std::string &failureReason) {
	if (!testDocumentServiceHappyPath(failureReason)) return false;
	if (!testDocumentServiceGuards(failureReason)) return false;
	return true;
}
} // namespace

int main() {
	std::string failureReason;

	if (!runProbe(failureReason)) {
		std::cerr << "mr_lsp_document_service_probe failed: " << failureReason << "\n";
		return 1;
	}

	std::cout << "mr_lsp_document_service_probe passed\n";
	return 0;
}
