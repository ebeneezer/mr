#include <iostream>
#include <poll.h>
#include <string>
#include <vector>

#include "../lsp/MRLspDiagnostics.hpp"

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

bool pollDiagnostics(mr::lsp::LspLifecycle &lifecycle, const mr::lsp::LspDocumentService &service, bool &sawAccepted, bool &sawStale, std::string &acceptedMessage, std::string &failureReason) {
	mr::lsp::LspDiagnosticsAdapter adapter;
	std::string errorMessage;
	std::vector<mr::lsp::LspInboundMessage> messages;

	for (int i = 0; i < 50; ++i) {
		if (!lifecycle.poll(messages, errorMessage)) {
			failureReason = "poll failed: " + errorMessage;
			return false;
		}
		for (const mr::lsp::LspInboundMessage &message : messages) {
			mr::lsp::LspDiagnosticBatch batch;
			if (!adapter.consume(message, service, batch, errorMessage)) {
				failureReason = "diagnostics failed: " + errorMessage;
				return false;
			}
			if (batch.stale) sawStale = true;
			if (batch.accepted) {
				sawAccepted = true;
				if (!batch.diagnostics.empty()) acceptedMessage = batch.diagnostics.front().message;
			}
		}
		if (sawAccepted) return true;
		::poll(nullptr, 0, 20);
	}
	failureReason = "expected diagnostics not observed";
	return false;
}

bool testDiagnosticsVersionGate(std::string &failureReason) {
	mr::lsp::LspLifecycle lifecycle;
	mr::lsp::LspDocumentService service(lifecycle);
	std::string errorMessage;
	bool sawAccepted = false;
	bool sawStale = false;
	std::string acceptedMessage;

	if (!startLifecycle(lifecycle, failureReason)) return false;
	if (!expect(service.open(makeSourceSnapshot(1, "int main() { return 0; }\n"), errorMessage), "open: " + errorMessage, failureReason)) return false;
	if (!pollDiagnostics(lifecycle, service, sawAccepted, sawStale, acceptedMessage, failureReason)) return false;
	if (!expect(sawAccepted, "open diagnostics accepted", failureReason)) return false;
	if (!expect(!sawStale, "open diagnostics stale", failureReason)) return false;
	if (!expect(acceptedMessage == "opened diagnostic", "open diagnostics message", failureReason)) return false;
	if (!expect(service.change(makeSourceSnapshot(2, "int main() { return 1; }\n"), errorMessage), "change: " + errorMessage, failureReason)) return false;

	sawAccepted = false;
	sawStale = false;
	acceptedMessage.clear();
	if (!pollDiagnostics(lifecycle, service, sawAccepted, sawStale, acceptedMessage, failureReason)) return false;
	if (!expect(sawStale, "stale diagnostics not observed", failureReason)) return false;
	if (!expect(sawAccepted, "current diagnostics not accepted", failureReason)) return false;
	if (!expect(acceptedMessage == "changed diagnostic", "change diagnostics message", failureReason)) return false;
	if (!expect(service.close(errorMessage), "close: " + errorMessage, failureReason)) return false;
	return shutdownLifecycle(lifecycle, failureReason);
}

bool testDiagnosticsUriRejection(std::string &failureReason) {
	mr::lsp::LspLifecycle lifecycle;
	mr::lsp::LspDocumentService service(lifecycle);
	mr::lsp::LspDiagnosticsAdapter adapter;
	mr::lsp::LspInboundMessage inbound;
	mr::lsp::LspDiagnosticBatch batch;
	std::string errorMessage;

	if (!startLifecycle(lifecycle, failureReason)) return false;
	if (!expect(service.open(makeSourceSnapshot(1, "one"), errorMessage), "uri open: " + errorMessage, failureReason)) return false;
	inbound.payload = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"file:///tmp/other.cpp\",\"version\":1,\"diagnostics\":[]}}";
	inbound.envelope = mr::lsp::parseJsonRpcEnvelope(inbound.payload);
	if (!expect(adapter.consume(inbound, service, batch, errorMessage), "uri reject consume: " + errorMessage, failureReason)) return false;
	if (!expect(batch.rejected, "uri diagnostics not rejected", failureReason)) return false;
	return shutdownLifecycle(lifecycle, failureReason);
}

bool testDiagnosticsWithoutVersion(std::string &failureReason) {
	mr::lsp::LspLifecycle lifecycle;
	mr::lsp::LspDocumentService service(lifecycle);
	mr::lsp::LspDiagnosticsAdapter adapter;
	mr::lsp::LspInboundMessage inbound;
	mr::lsp::LspDiagnosticBatch batch;
	std::string errorMessage;

	if (!startLifecycle(lifecycle, failureReason)) return false;
	if (!expect(service.open(makeSourceSnapshot(1, "one"), errorMessage), "unversioned open: " + errorMessage, failureReason)) return false;
	inbound.payload = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"" + service.documentUri() +
	                  "\",\"diagnostics\":[{\"range\":{\"end\":{\"line\":0,\"character\":3},\"start\":{\"line\":0,\"character\":0}},\"severity\":2,\"message\":\"unversioned diagnostic\"}]}}";
	inbound.envelope = mr::lsp::parseJsonRpcEnvelope(inbound.payload);
	if (!expect(adapter.consume(inbound, service, batch, errorMessage), "unversioned consume: " + errorMessage, failureReason)) return false;
	if (!expect(batch.accepted, "unversioned diagnostics not accepted", failureReason)) return false;
	if (!expect(!batch.hasVersion, "unversioned diagnostics has version", failureReason)) return false;
	if (!expect(batch.version == 0, "unversioned diagnostics version", failureReason)) return false;
	if (!expect(batch.diagnostics.size() == 1, "unversioned diagnostics count", failureReason)) return false;
	if (!expect(batch.diagnostics[0].message == "unversioned diagnostic", "unversioned diagnostics message", failureReason)) return false;
	return shutdownLifecycle(lifecycle, failureReason);
}

bool runProbe(std::string &failureReason) {
	if (!testDiagnosticsVersionGate(failureReason)) return false;
	if (!testDiagnosticsUriRejection(failureReason)) return false;
	if (!testDiagnosticsWithoutVersion(failureReason)) return false;
	return true;
}
} // namespace

int main() {
	std::string failureReason;

	if (!runProbe(failureReason)) {
		std::cerr << "mr_lsp_diagnostics_probe failed: " << failureReason << "\n";
		return 1;
	}

	std::cout << "mr_lsp_diagnostics_probe passed\n";
	return 0;
}
