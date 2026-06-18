#include <cstdint>
#include <iostream>
#include <poll.h>
#include <string>
#include <vector>

#include "../lsp/MRLspCodeAction.hpp"

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

bool pollCodeActions(
	mr::lsp::LspLifecycle &lifecycle,
	const mr::lsp::LspDocumentService &service,
	mr::lsp::LspCodeActionAdapter &adapter,
	mr::lsp::LspCodeActionRequest &request,
	mr::lsp::LspCodeActionResult &result,
	std::string &failureReason) {
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
				failureReason = "codeAction consume failed: " + errorMessage;
				return false;
			}
			if (accepted) return true;
		}
		::poll(nullptr, 0, 20);
	}
	failureReason = "expected codeAction response not observed";
	return false;
}

bool testCodeActionHappyPath(std::string &failureReason) {
	mr::lsp::LspLifecycle lifecycle;
	mr::lsp::LspDocumentService service(lifecycle);
	mr::lsp::LspCodeActionAdapter adapter;
	mr::lsp::LspCodeActionRequest request;
	mr::lsp::LspCodeActionResult result;
	mr::lsp::LspCodeActionRange range;
	std::string errorMessage;
	const std::string diagnosticJson = "{\"range\":{\"start\":{\"line\":0,\"character\":1},\"end\":{\"line\":0,\"character\":4}},\"severity\":1,\"message\":\"probe diagnostic\"}";

	range.start.line = 0;
	range.start.character = 1;
	range.end.line = 0;
	range.end.character = 4;
	if (!startLifecycle(lifecycle, failureReason)) return false;
	if (!expect(service.open(makeSourceSnapshot(1, "int main() { return 0 }\n"), errorMessage), "open: " + errorMessage, failureReason)) return false;
	if (!expect(adapter.requestCodeActions(lifecycle, service, range, diagnosticJson, request, errorMessage), "codeAction request: " + errorMessage, failureReason)) return false;
	if (!expect(request.pending, "codeAction request pending", failureReason)) return false;
	if (!expect(request.method == "textDocument/codeAction", "codeAction request method", failureReason)) return false;
	if (!pollCodeActions(lifecycle, service, adapter, request, result, failureReason)) return false;
	if (!expect(!request.pending, "codeAction request still pending", failureReason)) return false;
	if (!expect(result.uri == service.documentUri(), "codeAction uri", failureReason)) return false;
	if (!expect(result.items.size() == 2, "codeAction item count", failureReason)) return false;
	if (!expect(result.items[0].title == "Insert semicolon", "codeAction first title", failureReason)) return false;
	if (!expect(result.items[0].kind == "quickfix", "codeAction first kind", failureReason)) return false;
	if (!expect(result.items[0].hasEdit, "codeAction first edit missing", failureReason)) return false;
	if (!expect(!result.items[0].hasCommand, "codeAction first command", failureReason)) return false;
	if (!expect(result.items[0].edits.size() == 1, "codeAction first edit count", failureReason)) return false;
	if (!expect(result.items[0].edits[0].uri == service.documentUri(), "codeAction first edit uri", failureReason)) return false;
	if (!expect(result.items[0].edits[0].range.start.line == 0 && result.items[0].edits[0].range.start.character == 4, "codeAction first edit start", failureReason)) return false;
	if (!expect(result.items[0].edits[0].newText == ";", "codeAction first edit newText", failureReason)) return false;
	if (!expect(result.items[0].rawJson.find("\"newText\":\";\"") != std::string::npos, "codeAction raw edit", failureReason)) return false;
	if (!expect(result.items[1].title == "Show diagnostic", "codeAction second title", failureReason)) return false;
	if (!expect(result.items[1].hasCommand, "codeAction second command missing", failureReason)) return false;
	if (!expect(service.close(errorMessage), "close: " + errorMessage, failureReason)) return false;
	return shutdownLifecycle(lifecycle, failureReason);
}

bool testCodeActionGuards(std::string &failureReason) {
	mr::lsp::LspLifecycle lifecycle;
	mr::lsp::LspDocumentService service(lifecycle);
	mr::lsp::LspCodeActionAdapter adapter;
	mr::lsp::LspCodeActionRequest request;
	mr::lsp::LspCodeActionRange range;
	std::string errorMessage;

	range.start.line = 0;
	range.start.character = 0;
	range.end.line = 0;
	range.end.character = 1;
	if (!expect(!adapter.requestCodeActions(lifecycle, service, range, "{}", request, errorMessage), "codeAction without document accepted", failureReason)) return false;
	if (!expect(!request.pending, "failed codeAction pending", failureReason)) return false;
	if (!startLifecycle(lifecycle, failureReason)) return false;
	if (!expect(service.open(makeSourceSnapshot(1, "one"), errorMessage), "guard open: " + errorMessage, failureReason)) return false;
	range.start.character = -1;
	if (!expect(!adapter.requestCodeActions(lifecycle, service, range, "{}", request, errorMessage), "negative codeAction range accepted", failureReason)) return false;
	range.start.character = 0;
	if (!expect(!adapter.requestCodeActions(lifecycle, service, range, "[bad]", request, errorMessage), "malformed codeAction diagnostic accepted", failureReason)) return false;
	return shutdownLifecycle(lifecycle, failureReason);
}

bool runProbe(std::string &failureReason) {
	if (!testCodeActionHappyPath(failureReason)) return false;
	if (!testCodeActionGuards(failureReason)) return false;
	return true;
}
} // namespace

int main() {
	std::string failureReason;

	if (!runProbe(failureReason)) {
		std::cerr << "mr_lsp_code_action_probe failed: " << failureReason << "\n";
		return 1;
	}

	std::cout << "mr_lsp_code_action_probe passed\n";
	return 0;
}
