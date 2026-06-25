#include <cstddef>
#include <cstdint>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <string>
#include <vector>

#include "../app/services/MRServiceResults.hpp"
#include "../lsp/MRLspCodeAction.hpp"
#include "../lsp/MRLspCompletion.hpp"
#include "../lsp/MRLspDiagnostics.hpp"
#include "../lsp/MRLspHover.hpp"
#include "../lsp/MRLspReferences.hpp"

namespace {
bool expect(bool condition, const std::string &name, std::string &failureReason) {
	if (condition) return true;
	failureReason = name;
	return false;
}

std::string timelineText(const std::vector<std::string> &timeline) {
	std::ostringstream text;

	for (std::size_t index = 0; index < timeline.size(); ++index)
		text << index << ": " << timeline[index] << "\n";
	return text.str();
}

mr::lsp::LspDocumentSourceSnapshot makeSourceSnapshot(std::int64_t version, const std::string &text) {
	mr::lsp::LspDocumentSourceSnapshot snapshot;

	snapshot.absolutePath = "/tmp/mr/project/src/main.cpp";
	snapshot.languageId = "cpp";
	snapshot.version = version;
	snapshot.text = text;
	return snapshot;
}

mr::services::MRWorkspaceServiceSnapshot makeWorkspace(std::size_t version) {
	mr::services::MRWorkspaceServiceSnapshot workspace;
	mr::services::MRWorkspaceDocumentSnapshot document;

	document.bufferId = 10;
	document.documentId = 100;
	document.documentVersion = version;
	document.path = "/tmp/mr/project/src/main.cpp";
	document.languageName = "cpp";
	document.mainFile = true;
	workspace.documents.push_back(document);
	workspace.mainFile.hasMainFile = true;
	workspace.mainFile.bufferId = document.bufferId;
	workspace.mainFile.path = document.path;
	workspace.root.hasRoot = true;
	workspace.root.rootPath = "/tmp/mr/project";
	workspace.root.reason = "probe root";
	return workspace;
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

	spec.session.process.executablePath = "./regression/mr_lsp_protocol_shaper";
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

bool pollDiagnostics(mr::lsp::LspLifecycle &lifecycle, const mr::lsp::LspDocumentService &service, mr::services::MRServiceResultStore &store, const mr::services::MRWorkspaceServiceSnapshot &workspace, int acceptedVersion, std::string &failureReason) {
	mr::lsp::LspDiagnosticsAdapter adapter;
	std::string errorMessage;
	std::vector<mr::lsp::LspInboundMessage> messages;

	for (int i = 0; i < 50; ++i) {
		if (!lifecycle.poll(messages, errorMessage)) {
			failureReason = "poll diagnostics failed: " + errorMessage;
			return false;
		}
		for (const mr::lsp::LspInboundMessage &message : messages) {
			mr::lsp::LspDiagnosticBatch batch;
			if (!adapter.consume(message, service, batch, errorMessage)) {
				failureReason = "diagnostics consume failed: " + errorMessage;
				return false;
			}
			if (batch.accepted || batch.stale || batch.rejected) {
				store.putDiagnostics(mr::services::buildServiceDiagnosticsFromLsp(workspace, batch));
				if (batch.accepted && batch.version == acceptedVersion) return true;
			}
		}
		::poll(nullptr, 0, 20);
	}
	failureReason = "expected diagnostics not observed";
	return false;
}

bool requestDefinition(mr::lsp::LspLifecycle &lifecycle, const mr::lsp::LspDocumentService &service, mr::services::MRServiceResultStore &store, const mr::services::MRWorkspaceServiceSnapshot &workspace, std::string &failureReason) {
	mr::lsp::LspDefinitionAdapter adapter;
	mr::lsp::LspDefinitionRequest request;
	mr::lsp::LspDefinitionResult definition;
	std::string errorMessage;
	std::vector<mr::lsp::LspInboundMessage> messages;

	if (!expect(adapter.requestDefinition(lifecycle, service, mr::lsp::LspTextPosition{3, 5}, request, errorMessage), "definition request: " + errorMessage, failureReason)) return false;
	if (!expect(request.method == "textDocument/definition", "definition method", failureReason)) return false;
	for (int i = 0; i < 50; ++i) {
		if (!lifecycle.poll(messages, errorMessage)) {
			failureReason = "poll definition failed: " + errorMessage;
			return false;
		}
		for (const mr::lsp::LspInboundMessage &message : messages) {
			bool accepted = false;
			if (!adapter.consume(message, service, request, definition, accepted, errorMessage)) {
				failureReason = "definition consume failed: " + errorMessage;
				return false;
			}
			if (accepted) {
				store.putLocations(mr::services::buildServiceDefinitionFromLsp(workspace, request.uri, workspace.documents.front().documentVersion, request.idText, definition));
				return true;
			}
		}
		::poll(nullptr, 0, 20);
	}
	failureReason = "expected definition response not observed";
	return false;
}

bool requestReferences(mr::lsp::LspLifecycle &lifecycle, const mr::lsp::LspDocumentService &service, mr::services::MRServiceResultStore &store, const mr::services::MRWorkspaceServiceSnapshot &workspace, std::string &failureReason) {
	mr::lsp::LspReferencesAdapter adapter;
	mr::lsp::LspReferencesRequest request;
	mr::lsp::LspReferencesResult references;
	std::string errorMessage;
	std::vector<mr::lsp::LspInboundMessage> messages;

	if (!expect(adapter.requestReferences(lifecycle, service, mr::lsp::LspTextPosition{3, 5}, true, request, errorMessage), "references request: " + errorMessage, failureReason)) return false;
	if (!expect(request.method == "textDocument/references", "references method", failureReason)) return false;
	for (int i = 0; i < 50; ++i) {
		if (!lifecycle.poll(messages, errorMessage)) {
			failureReason = "poll references failed: " + errorMessage;
			return false;
		}
		for (const mr::lsp::LspInboundMessage &message : messages) {
			bool accepted = false;
			if (!adapter.consume(message, service, request, references, accepted, errorMessage)) {
				failureReason = "references consume failed: " + errorMessage;
				return false;
			}
			if (accepted) {
				store.putLocations(mr::services::buildServiceReferencesFromLsp(workspace, request.uri, workspace.documents.front().documentVersion, request.idText, references));
				return true;
			}
		}
		::poll(nullptr, 0, 20);
	}
	failureReason = "expected references response not observed";
	return false;
}

bool requestHover(mr::lsp::LspLifecycle &lifecycle, const mr::lsp::LspDocumentService &service, mr::services::MRServiceResultStore &store, const mr::services::MRWorkspaceServiceSnapshot &workspace, std::string &failureReason) {
	mr::lsp::LspHoverAdapter adapter;
	mr::lsp::LspHoverRequest request;
	mr::lsp::LspHoverResult hover;
	std::string errorMessage;
	std::vector<mr::lsp::LspInboundMessage> messages;

	if (!expect(adapter.requestHover(lifecycle, service, mr::lsp::LspTextPosition{3, 5}, request, errorMessage), "hover request: " + errorMessage, failureReason)) return false;
	if (!expect(request.method == "textDocument/hover", "hover method", failureReason)) return false;
	for (int i = 0; i < 50; ++i) {
		if (!lifecycle.poll(messages, errorMessage)) {
			failureReason = "poll hover failed: " + errorMessage;
			return false;
		}
		for (const mr::lsp::LspInboundMessage &message : messages) {
			bool accepted = false;
			if (!adapter.consume(message, service, request, hover, accepted, errorMessage)) {
				failureReason = "hover consume failed: " + errorMessage;
				return false;
			}
			if (accepted) {
				store.putHover(mr::services::buildServiceHoverFromLsp(workspace, workspace.documents.front().documentVersion, request.idText, hover));
				return true;
			}
		}
		::poll(nullptr, 0, 20);
	}
	failureReason = "expected hover response not observed";
	return false;
}

bool requestCompletion(mr::lsp::LspLifecycle &lifecycle, const mr::lsp::LspDocumentService &service, mr::services::MRServiceResultStore &store, const mr::services::MRWorkspaceServiceSnapshot &workspace, std::string &failureReason) {
	mr::lsp::LspCompletionAdapter adapter;
	mr::lsp::LspCompletionRequest request;
	mr::lsp::LspCompletionResult completion;
	std::string errorMessage;
	std::vector<mr::lsp::LspInboundMessage> messages;

	if (!expect(adapter.requestCompletion(lifecycle, service, mr::lsp::LspTextPosition{3, 5}, std::string(), request, errorMessage), "completion request: " + errorMessage, failureReason)) return false;
	if (!expect(request.method == "textDocument/completion", "completion method", failureReason)) return false;
	for (int i = 0; i < 50; ++i) {
		if (!lifecycle.poll(messages, errorMessage)) {
			failureReason = "poll completion failed: " + errorMessage;
			return false;
		}
		for (const mr::lsp::LspInboundMessage &message : messages) {
			bool accepted = false;
			if (!adapter.consume(message, service, request, completion, accepted, errorMessage)) {
				failureReason = "completion consume failed: " + errorMessage;
				return false;
			}
			if (accepted) {
				store.putCompletion(mr::services::buildServiceCompletionFromLsp(workspace, request.uri, workspace.documents.front().documentVersion, request.idText, completion));
				return true;
			}
		}
		::poll(nullptr, 0, 20);
	}
	failureReason = "expected completion response not observed";
	return false;
}

bool requestCodeActions(mr::lsp::LspLifecycle &lifecycle, const mr::lsp::LspDocumentService &service, mr::services::MRServiceResultStore &store, const mr::services::MRWorkspaceServiceSnapshot &workspace, std::string &failureReason) {
	mr::lsp::LspCodeActionAdapter adapter;
	mr::lsp::LspCodeActionRequest request;
	mr::lsp::LspCodeActionResult codeActions;
	mr::lsp::LspCodeActionRange range;
	std::string errorMessage;
	std::vector<mr::lsp::LspInboundMessage> messages;
	const std::string diagnosticJson = "{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":1}},\"severity\":2,\"message\":\"protocol shaper changed document\"}";

	range.start.line = 0;
	range.start.character = 0;
	range.end.line = 0;
	range.end.character = 1;
	if (!expect(adapter.requestCodeActions(lifecycle, service, range, diagnosticJson, request, errorMessage), "codeAction request: " + errorMessage, failureReason)) return false;
	if (!expect(request.method == "textDocument/codeAction", "codeAction method", failureReason)) return false;
	for (int i = 0; i < 50; ++i) {
		if (!lifecycle.poll(messages, errorMessage)) {
			failureReason = "poll codeAction failed: " + errorMessage;
			return false;
		}
		for (const mr::lsp::LspInboundMessage &message : messages) {
			bool accepted = false;
			if (!adapter.consume(message, service, request, codeActions, accepted, errorMessage)) {
				failureReason = "codeAction consume failed: " + errorMessage;
				return false;
			}
			if (accepted) {
				store.putCodeActions(mr::services::buildServiceCodeActionsFromLsp(workspace, request.uri, workspace.documents.front().documentVersion, request.idText, codeActions));
				return true;
			}
		}
		::poll(nullptr, 0, 20);
	}
	failureReason = "expected codeAction response not observed";
	return false;
}

bool requestCompletionWhileDiagnosticsPending(mr::lsp::LspLifecycle &lifecycle, mr::lsp::LspDocumentService &service, std::string &failureReason) {
	mr::lsp::LspDiagnosticsAdapter diagnosticsAdapter;
	mr::lsp::LspCompletionAdapter completionAdapter;
	mr::lsp::LspCompletionRequest completionRequest;
	mr::lsp::LspCompletionResult completion;
	mr::services::MRServiceResultStore store;
	mr::services::MRWorkspaceServiceSnapshot workspace = makeWorkspace(1);
	std::string errorMessage;
	std::vector<mr::lsp::LspInboundMessage> messages;
	std::vector<std::string> timeline;

	if (!expect(service.open(makeSourceSnapshot(1, "int main() { return 0; }\n"), errorMessage), "timeline open: " + errorMessage, failureReason)) return false;
	timeline.push_back("tx didOpen v1");
	if (!pollDiagnostics(lifecycle, service, store, workspace, 1, failureReason)) {
		failureReason += "\n" + timelineText(timeline);
		return false;
	}
	timeline.push_back("rx diagnostic current v1");

	workspace = makeWorkspace(2);
	if (!expect(service.change(makeSourceSnapshot(2, "int main() { return 1; }\n"), errorMessage), "timeline change: " + errorMessage, failureReason)) return false;
	timeline.push_back("tx didChange v2");
	if (!expect(completionAdapter.requestCompletion(lifecycle, service, mr::lsp::LspTextPosition{3, 5}, std::string(), completionRequest, errorMessage), "timeline completion request: " + errorMessage, failureReason)) return false;
	timeline.push_back("tx completion " + completionRequest.idText);

	for (int i = 0; i < 50; ++i) {
		if (!lifecycle.poll(messages, errorMessage)) {
			failureReason = "timeline poll failed: " + errorMessage + "\n" + timelineText(timeline);
			return false;
		}
		for (const mr::lsp::LspInboundMessage &message : messages) {
			mr::lsp::LspDiagnosticBatch batch;
			bool completionAccepted = false;

			if (!diagnosticsAdapter.consume(message, service, batch, errorMessage)) {
				failureReason = "timeline diagnostics consume failed: " + errorMessage + "\n" + timelineText(timeline);
				return false;
			}
			if (batch.accepted || batch.stale || batch.rejected) {
				std::string line = "rx diagnostic ";

				if (batch.accepted) line += "current ";
				else if (batch.stale)
					line += "stale ";
				else
					line += "rejected ";
				line += "v" + std::to_string(batch.version);
				timeline.push_back(line);
				store.putDiagnostics(mr::services::buildServiceDiagnosticsFromLsp(workspace, batch));
			}

			if (!completionAdapter.consume(message, service, completionRequest, completion, completionAccepted, errorMessage)) {
				failureReason = "timeline completion consume failed: " + errorMessage + "\n" + timelineText(timeline);
				return false;
			}
			if (completionAccepted) {
				timeline.push_back("rx completion accepted " + completionRequest.idText);
				store.putCompletion(mr::services::buildServiceCompletionFromLsp(workspace, completionRequest.uri, workspace.documents.front().documentVersion, completionRequest.idText, completion));
				if (!expect(store.completionResults().size() == 1, "timeline completion result count\n" + timelineText(timeline), failureReason)) return false;
				if (!expect(store.completionResults()[0].items.size() == 3, "timeline completion item count\n" + timelineText(timeline), failureReason)) return false;
				std::cout << "LSP pending-diagnostics completion timeline:\n" << timelineText(timeline);
				return true;
			}
		}
		::poll(nullptr, 0, 20);
	}
	failureReason = "completion was not accepted while diagnostics were pending\n" + timelineText(timeline);
	return false;
}

bool verifyResults(const mr::services::MRServiceResultStore &store, std::string &failureReason) {
	if (!expect(store.diagnosticResults().size() == 1, "diagnostics result count", failureReason)) return false;
	if (!expect(store.diagnosticResults()[0].header.state == mr::services::MRServiceResultState::Current, "diagnostics state", failureReason)) return false;
	if (!expect(store.diagnosticResults()[0].header.identity.documentVersion == 2, "diagnostics version", failureReason)) return false;
	if (!expect(store.diagnosticResults()[0].diagnostics.size() == 1, "diagnostics entry count", failureReason)) return false;
	if (!expect(store.diagnosticResults()[0].diagnostics[0].message == "protocol shaper changed document", "diagnostics message", failureReason)) return false;
	if (!expect(store.locationResults().size() == 2, "location result count", failureReason)) return false;
	if (!expect(store.locationResults()[0].header.kind == mr::services::MRServiceResultKind::Definition, "definition result kind", failureReason)) return false;
	if (!expect(store.locationResults()[0].locations.size() == 1, "definition target count", failureReason)) return false;
	if (!expect(store.locationResults()[0].locations[0].path == "/tmp/mr/project/src/main.cpp", "definition path", failureReason)) return false;
	if (!expect(store.locationResults()[1].header.kind == mr::services::MRServiceResultKind::References, "references result kind", failureReason)) return false;
	if (!expect(store.locationResults()[1].locations.size() == 2, "references target count", failureReason)) return false;
	if (!expect(store.locationResults()[1].locations[1].path == "/tmp/mr/project/src/main.cpp", "references second path", failureReason)) return false;
	if (!expect(store.hoverResults().size() == 1, "hover result count", failureReason)) return false;
	if (!expect(store.hoverResults()[0].hover.markupKind == "plaintext", "hover kind", failureReason)) return false;
	if (!expect(store.hoverResults()[0].hover.value == "mr protocol shaper hover", "hover value", failureReason)) return false;
	if (!expect(store.completionResults().size() == 1, "completion result count", failureReason)) return false;
	if (!expect(store.completionResults()[0].items.size() == 3, "completion item count", failureReason)) return false;
	if (!expect(store.completionResults()[0].items[0].label == "shaperCompletionOne", "completion first label", failureReason)) return false;
	if (!expect(store.completionResults()[0].items[0].insertText == "shaperCompletionOne", "completion first insertText", failureReason)) return false;
	if (!expect(store.completionResults()[0].items[1].label == "shaperFor", "completion snippet label", failureReason)) return false;
	if (!expect(store.completionResults()[0].items[1].insertTextFormat == 2, "completion snippet format", failureReason)) return false;
	if (!expect(store.completionResults()[0].items[2].label == "shaperCompletionTwo", "completion third label", failureReason)) return false;
	if (!expect(store.codeActionResults().size() == 1, "codeAction result count", failureReason)) return false;
	if (!expect(store.codeActionResults()[0].items.size() == 2, "codeAction item count", failureReason)) return false;
	if (!expect(store.codeActionResults()[0].items[0].title == "protocol shaper quick fix", "codeAction first title", failureReason)) return false;
	if (!expect(store.codeActionResults()[0].items[0].hasEdit, "codeAction first edit", failureReason)) return false;
	if (!expect(store.codeActionResults()[0].items[1].title == "protocol shaper command", "codeAction second title", failureReason)) return false;
	if (!expect(store.codeActionResults()[0].items[1].hasCommand, "codeAction second command", failureReason)) return false;
	return true;
}

bool runProbe(std::string &failureReason) {
	mr::lsp::LspLifecycle lifecycle;
	mr::lsp::LspDocumentService service(lifecycle);
	mr::services::MRServiceResultStore store;
	mr::services::MRWorkspaceServiceSnapshot workspace = makeWorkspace(1);
	std::string errorMessage;

	if (!startLifecycle(lifecycle, failureReason)) return false;
	if (!expect(service.open(makeSourceSnapshot(1, "int main() { return 0; }\n"), errorMessage), "open: " + errorMessage, failureReason)) return false;
	if (!pollDiagnostics(lifecycle, service, store, workspace, 1, failureReason)) return false;
	workspace = makeWorkspace(2);
	if (!expect(service.change(makeSourceSnapshot(2, "int main() { return 1; }\n"), errorMessage), "change: " + errorMessage, failureReason)) return false;
	if (!pollDiagnostics(lifecycle, service, store, workspace, 2, failureReason)) return false;
	if (!requestDefinition(lifecycle, service, store, workspace, failureReason)) return false;
	if (!requestReferences(lifecycle, service, store, workspace, failureReason)) return false;
	if (!requestHover(lifecycle, service, store, workspace, failureReason)) return false;
	if (!requestCompletion(lifecycle, service, store, workspace, failureReason)) return false;
	if (!requestCodeActions(lifecycle, service, store, workspace, failureReason)) return false;
	if (!verifyResults(store, failureReason)) return false;
	if (!expect(service.close(errorMessage), "close: " + errorMessage, failureReason)) return false;
	if (!requestCompletionWhileDiagnosticsPending(lifecycle, service, failureReason)) return false;
	if (!expect(service.close(errorMessage), "timeline close: " + errorMessage, failureReason)) return false;
	return shutdownLifecycle(lifecycle, failureReason);
}
} // namespace

int main() {
	std::string failureReason;

	if (!runProbe(failureReason)) {
		std::cerr << "mr_lsp_service_integration_probe failed: " << failureReason << "\n";
		return 1;
	}

	std::cout << "mr_lsp_service_integration_probe passed\n";
	return 0;
}
