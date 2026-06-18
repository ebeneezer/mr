#include <iostream>
#include <string>
#include <vector>

#include "../app/services/MRServiceResults.hpp"
#include "../lsp/MRLspCodeAction.hpp"
#include "../lsp/MRLspCompletion.hpp"
#include "../lsp/MRLspDiagnostics.hpp"
#include "../lsp/MRLspHover.hpp"
#include "../lsp/MRLspReferences.hpp"
#include "../lsp/MRLspUri.hpp"

namespace {
bool expect(bool condition, const std::string &name, std::string &failureReason) {
	if (condition) return true;
	failureReason = name;
	return false;
}

mr::services::MRWorkspaceDocumentSnapshot document(int bufferId, std::size_t documentId, std::size_t version, const std::string &path) {
	mr::services::MRWorkspaceDocumentSnapshot snapshot;

	snapshot.bufferId = bufferId;
	snapshot.documentId = documentId;
	snapshot.documentVersion = version;
	snapshot.path = path;
	snapshot.languageName = "cpp";
	return snapshot;
}

bool makeUri(const std::string &path, std::string &uri, std::string &failureReason) {
	std::string errorMessage;

	if (mr::lsp::pathToFileUri(path, uri, errorMessage)) return true;
	failureReason = "uri build failed: " + errorMessage;
	return false;
}

mr::services::MRWorkspaceServiceSnapshot makeWorkspace(std::size_t version) {
	mr::services::MRWorkspaceServiceContext context;
	std::vector<mr::services::MRWorkspaceDocumentSnapshot> documents;

	documents.push_back(document(10, 100, version, "/tmp/mr/project/src/main.cpp"));
	documents.push_back(document(11, 101, 3, "/tmp/mr/project/include/main.hpp"));
	context.setMainFileByBufferId(10);
	return context.buildSnapshot(documents);
}

mr::lsp::LspLocation makeLocation(const std::string &uri, int line) {
	mr::lsp::LspLocation location;

	location.uri = uri;
	location.start.line = line;
	location.start.character = 2;
	location.end.line = line;
	location.end.character = 8;
	return location;
}

bool testDiagnosticsConversion(std::string &failureReason) {
	const mr::services::MRWorkspaceServiceSnapshot workspace = makeWorkspace(5);
	std::string uri;
	mr::lsp::LspDiagnosticBatch batch;

	if (!makeUri("/tmp/mr/project/src/main.cpp", uri, failureReason)) return false;
	batch.uri = uri;
	batch.version = 5;
	batch.accepted = true;
	batch.diagnostics.push_back(mr::lsp::LspDiagnostic());
	batch.diagnostics[0].range.startLine = 1;
	batch.diagnostics[0].range.startCharacter = 3;
	batch.diagnostics[0].range.endLine = 1;
	batch.diagnostics[0].range.endCharacter = 7;
	batch.diagnostics[0].severity = 2;
	batch.diagnostics[0].message = "deterministic diagnostic";
	batch.diagnostics[0].rawJson = "{\"message\":\"deterministic diagnostic\"}";

	const mr::services::MRServiceDiagnosticResult result = mr::services::buildServiceDiagnosticsFromLsp(workspace, batch);
	if (!expect(result.header.state == mr::services::MRServiceResultState::Current, "diagnostics state", failureReason)) return false;
	if (!expect(result.header.identity.valid, "diagnostics identity", failureReason)) return false;
	if (!expect(result.header.identity.bufferId == 10, "diagnostics buffer", failureReason)) return false;
	if (!expect(result.header.identity.documentVersion == 5, "diagnostics version", failureReason)) return false;
	if (!expect(result.diagnostics.size() == 1, "diagnostics count", failureReason)) return false;
	if (!expect(result.diagnostics[0].message == "deterministic diagnostic", "diagnostics message", failureReason)) return false;
	if (!expect(result.diagnostics[0].reportedRange.start.character == 3, "diagnostics reported range", failureReason)) return false;
	if (!expect(result.diagnostics[0].navigationRange.start.character == 3, "diagnostics navigation range", failureReason)) return false;
	if (!expect(result.diagnostics[0].rawLspDiagnosticJson.find("deterministic diagnostic") != std::string::npos, "diagnostics raw json", failureReason)) return false;
	return true;
}

bool testStaleAndRejectedDiagnostics(std::string &failureReason) {
	const mr::services::MRWorkspaceServiceSnapshot workspace = makeWorkspace(5);
	std::string uri;
	std::string otherUri;
	mr::lsp::LspDiagnosticBatch staleBatch;
	mr::lsp::LspDiagnosticBatch rejectedBatch;

	if (!makeUri("/tmp/mr/project/src/main.cpp", uri, failureReason)) return false;
	if (!makeUri("/tmp/mr/project/src/other.cpp", otherUri, failureReason)) return false;
	staleBatch.uri = uri;
	staleBatch.version = 4;
	staleBatch.accepted = true;
	rejectedBatch.uri = otherUri;
	rejectedBatch.version = 1;
	rejectedBatch.accepted = true;

	const mr::services::MRServiceDiagnosticResult stale = mr::services::buildServiceDiagnosticsFromLsp(workspace, staleBatch);
	const mr::services::MRServiceDiagnosticResult rejected = mr::services::buildServiceDiagnosticsFromLsp(workspace, rejectedBatch);
	if (!expect(stale.header.state == mr::services::MRServiceResultState::Stale, "stale diagnostics", failureReason)) return false;
	if (!expect(stale.header.identity.valid, "stale identity", failureReason)) return false;
	if (!expect(rejected.header.state == mr::services::MRServiceResultState::Rejected, "rejected diagnostics", failureReason)) return false;
	if (!expect(!rejected.header.identity.valid, "rejected identity", failureReason)) return false;
	return true;
}

bool testNavigationHoverCompletionConversion(std::string &failureReason) {
	const mr::services::MRWorkspaceServiceSnapshot workspace = makeWorkspace(5);
	std::string mainUri;
	std::string headerUri;
	mr::lsp::LspReferencesResult references;
	mr::lsp::LspHoverResult hover;
	mr::lsp::LspCompletionResult completion;
	mr::lsp::LspCodeActionResult codeActions;

	if (!makeUri("/tmp/mr/project/src/main.cpp", mainUri, failureReason)) return false;
	if (!makeUri("/tmp/mr/project/include/main.hpp", headerUri, failureReason)) return false;
	references.originUri = mainUri;
	references.locations.push_back(makeLocation(mainUri, 3));
	references.locations.push_back(makeLocation(headerUri, 9));
	hover.uri = mainUri;
	hover.kind = "markdown";
	hover.value = "**main**";
	completion.uri = mainUri;
	completion.items.push_back(mr::lsp::LspCompletionItem());
	completion.items[0].label = "main";
	completion.items[0].hasKind = true;
	completion.items[0].kind = 3;
	completion.items[0].detail = "int main()";
	completion.items[0].insertText = "main";
	completion.items[0].hasInsertTextFormat = true;
	completion.items[0].insertTextFormat = 1;
	codeActions.uri = mainUri;
	codeActions.items.push_back(mr::lsp::LspCodeActionItem());
	codeActions.items[0].title = "Insert semicolon";
	codeActions.items[0].kind = "quickfix";
	codeActions.items[0].hasEdit = true;
	codeActions.items[0].edits.push_back(mr::lsp::LspCodeActionTextEdit());
	codeActions.items[0].edits[0].uri = mainUri;
	codeActions.items[0].edits[0].range.start.line = 0;
	codeActions.items[0].edits[0].range.start.character = 4;
	codeActions.items[0].edits[0].range.end.line = 0;
	codeActions.items[0].edits[0].range.end.character = 4;
	codeActions.items[0].edits[0].newText = ";";
	codeActions.items[0].rawJson = "{\"title\":\"Insert semicolon\",\"kind\":\"quickfix\",\"edit\":{}}";

	mr::lsp::LspDefinitionResult definitionResult;
	definitionResult.originUri = mainUri;
	definitionResult.locations.push_back(makeLocation(mainUri, 4));
	const mr::services::MRServiceLocationResult definition = mr::services::buildServiceDefinitionFromLsp(workspace, mainUri, 5, "mr-definition-1", definitionResult);
	const mr::services::MRServiceLocationResult refs = mr::services::buildServiceReferencesFromLsp(workspace, mainUri, 5, "mr-references-1", references);
	const mr::services::MRServiceHoverResult serviceHover = mr::services::buildServiceHoverFromLsp(workspace, 5, "mr-hover-1", hover);
	const mr::services::MRServiceCompletionResult serviceCompletion = mr::services::buildServiceCompletionFromLsp(workspace, mainUri, 5, "mr-completion-1", completion);
	const mr::services::MRServiceCodeActionResult serviceCodeActions = mr::services::buildServiceCodeActionsFromLsp(workspace, mainUri, 5, "mr-code-action-1", codeActions);

	if (!expect(definition.header.state == mr::services::MRServiceResultState::Current, "definition state", failureReason)) return false;
	if (!expect(definition.locations.size() == 1, "definition count", failureReason)) return false;
	if (!expect(definition.locations[0].path == "/tmp/mr/project/src/main.cpp", "definition target path", failureReason)) return false;
	if (!expect(refs.locations.size() == 2, "references count", failureReason)) return false;
	if (!expect(refs.locations[1].path == "/tmp/mr/project/include/main.hpp", "references external path", failureReason)) return false;
	if (!expect(serviceHover.hover.markupKind == "markdown", "hover kind", failureReason)) return false;
	if (!expect(serviceHover.hover.value == "**main**", "hover value", failureReason)) return false;
	if (!expect(serviceCompletion.items.size() == 1, "completion count", failureReason)) return false;
	if (!expect(serviceCompletion.items[0].label == "main", "completion label", failureReason)) return false;
	if (!expect(serviceCompletion.items[0].insertText == "main", "completion insert text", failureReason)) return false;
	if (!expect(serviceCompletion.items[0].hasInsertTextFormat, "completion insert text format present", failureReason)) return false;
	if (!expect(serviceCompletion.items[0].insertTextFormat == 1, "completion insert text format", failureReason)) return false;
	if (!expect(serviceCodeActions.items.size() == 1, "codeAction count", failureReason)) return false;
	if (!expect(serviceCodeActions.items[0].title == "Insert semicolon", "codeAction title", failureReason)) return false;
	if (!expect(serviceCodeActions.items[0].kind == "quickfix", "codeAction kind", failureReason)) return false;
	if (!expect(serviceCodeActions.items[0].hasEdit, "codeAction edit", failureReason)) return false;
	if (!expect(serviceCodeActions.items[0].edits.size() == 1, "codeAction edit count", failureReason)) return false;
	if (!expect(serviceCodeActions.items[0].edits[0].path == "/tmp/mr/project/src/main.cpp", "codeAction edit path", failureReason)) return false;
	if (!expect(serviceCodeActions.items[0].edits[0].range.start.character == 4, "codeAction edit range", failureReason)) return false;
	if (!expect(serviceCodeActions.items[0].edits[0].newText == ";", "codeAction edit text", failureReason)) return false;
	if (!expect(serviceCodeActions.items[0].rawLspCodeActionJson.find("\"edit\"") != std::string::npos, "codeAction raw json", failureReason)) return false;
	return true;
}

bool testStoreReplacementAndStaleMarking(std::string &failureReason) {
	const mr::services::MRWorkspaceServiceSnapshot currentWorkspace = makeWorkspace(5);
	const mr::services::MRWorkspaceServiceSnapshot changedWorkspace = makeWorkspace(6);
	std::string uri;
	mr::lsp::LspDiagnosticBatch firstBatch;
	mr::lsp::LspDiagnosticBatch secondBatch;
	mr::services::MRServiceResultStore store;

	if (!makeUri("/tmp/mr/project/src/main.cpp", uri, failureReason)) return false;
	firstBatch.uri = uri;
	firstBatch.version = 5;
	firstBatch.accepted = true;
	firstBatch.diagnostics.push_back(mr::lsp::LspDiagnostic());
	firstBatch.diagnostics[0].message = "first";
	secondBatch = firstBatch;
	secondBatch.diagnostics[0].message = "second";

	store.putDiagnostics(mr::services::buildServiceDiagnosticsFromLsp(currentWorkspace, firstBatch));
	store.putDiagnostics(mr::services::buildServiceDiagnosticsFromLsp(currentWorkspace, secondBatch));
	if (!expect(store.diagnosticResults().size() == 1, "store replacement count", failureReason)) return false;
	if (!expect(store.diagnosticResults()[0].diagnostics[0].message == "second", "store replacement value", failureReason)) return false;
	if (!expect(mr::services::serviceDocumentIdentityMatches(currentWorkspace, store.diagnosticResults()[0].header.identity), "identity match current", failureReason)) return false;
	if (!expect(!mr::services::serviceDocumentIdentityMatches(changedWorkspace, store.diagnosticResults()[0].header.identity), "identity match changed", failureReason)) return false;

	store.markStaleAgainstWorkspace(changedWorkspace);
	if (!expect(store.diagnosticResults()[0].header.state == mr::services::MRServiceResultState::Stale, "store stale state", failureReason)) return false;
	return true;
}

bool testCurrentDocumentSnapshot(std::string &failureReason) {
	const mr::services::MRWorkspaceServiceSnapshot workspace = makeWorkspace(5);
	const mr::services::MRWorkspaceDocumentSnapshot document = workspace.documents[0];
	std::string mainUri;
	mr::lsp::LspDiagnosticBatch batch;
	mr::lsp::LspDefinitionResult definitionResult;
	mr::lsp::LspReferencesResult referencesResult;
	mr::lsp::LspHoverResult hoverResult;
	mr::lsp::LspCompletionResult completionResult;
	mr::lsp::LspCodeActionResult codeActionResult;
	mr::services::MRServiceResultStore store;

	if (!makeUri(document.path, mainUri, failureReason)) return false;
	batch.uri = mainUri;
	batch.version = 5;
	batch.accepted = true;
	batch.diagnostics.push_back(mr::lsp::LspDiagnostic());
	batch.diagnostics[0].message = "snapshot diagnostic";
	definitionResult.originUri = mainUri;
	definitionResult.locations.push_back(makeLocation(mainUri, 2));
	referencesResult.originUri = mainUri;
	referencesResult.locations.push_back(makeLocation(mainUri, 2));
	hoverResult.uri = mainUri;
	hoverResult.kind = "plaintext";
	hoverResult.value = "snapshot hover";
	completionResult.uri = mainUri;
	completionResult.items.push_back(mr::lsp::LspCompletionItem());
	completionResult.items[0].label = "snapshotCompletion";
	codeActionResult.uri = mainUri;
	codeActionResult.items.push_back(mr::lsp::LspCodeActionItem());
	codeActionResult.items[0].title = "Snapshot edit";
	codeActionResult.items[0].hasEdit = true;
	codeActionResult.items[0].edits.push_back(mr::lsp::LspCodeActionTextEdit());
	codeActionResult.items[0].edits[0].uri = mainUri;
	codeActionResult.items[0].edits[0].range.start.line = 0;
	codeActionResult.items[0].edits[0].range.end.line = 0;
	codeActionResult.items[0].edits[0].newText = ";";
	codeActionResult.items.push_back(mr::lsp::LspCodeActionItem());
	codeActionResult.items[1].title = "Snapshot command only";
	codeActionResult.items[1].hasCommand = true;

	store.putDiagnostics(mr::services::buildServiceDiagnosticsFromLsp(workspace, batch));
	store.putLocations(mr::services::buildServiceDefinitionFromLsp(workspace, mainUri, 5, "snapshot-definition", definitionResult));
	store.putLocations(mr::services::buildServiceReferencesFromLsp(workspace, mainUri, 5, "snapshot-references", referencesResult));
	store.putHover(mr::services::buildServiceHoverFromLsp(workspace, 5, "snapshot-hover", hoverResult));
	store.putCompletion(mr::services::buildServiceCompletionFromLsp(workspace, mainUri, 5, "snapshot-completion", completionResult));
	store.putCodeActions(mr::services::buildServiceCodeActionsFromLsp(workspace, mainUri, 5, "snapshot-code-action", codeActionResult));

	const mr::services::MRServiceResultCounts stored = store.resultCounts();
	const mr::services::MRServiceDocumentResultsSnapshot snapshot = store.currentResultsForDocument(document);
	if (!expect(stored.diagnostics == 1, "stored diagnostics count", failureReason)) return false;
	if (!expect(stored.definitions == 1, "stored definitions count", failureReason)) return false;
	if (!expect(stored.references == 1, "stored references count", failureReason)) return false;
	if (!expect(stored.hovers == 1, "stored hovers count", failureReason)) return false;
	if (!expect(stored.completions == 1, "stored completions count", failureReason)) return false;
	if (!expect(stored.codeActions == 1, "stored codeActions count", failureReason)) return false;
	if (!expect(snapshot.identity.valid, "snapshot identity", failureReason)) return false;
	if (!expect(snapshot.current.diagnostics == 1, "snapshot diagnostics count", failureReason)) return false;
	if (!expect(snapshot.current.definitions == 1, "snapshot definitions count", failureReason)) return false;
	if (!expect(snapshot.current.references == 1, "snapshot references count", failureReason)) return false;
	if (!expect(snapshot.current.hovers == 1, "snapshot hovers count", failureReason)) return false;
	if (!expect(snapshot.current.completions == 1, "snapshot completions count", failureReason)) return false;
	if (!expect(snapshot.current.codeActions == 1, "snapshot codeActions count", failureReason)) return false;
	if (!expect(snapshot.codeActions.size() == 1, "snapshot codeAction result count", failureReason)) return false;
	if (!expect(snapshot.codeActions[0].items.size() == 1, "snapshot usable codeAction item count", failureReason)) return false;
	if (!expect(snapshot.codeActions[0].items[0].title == "Snapshot edit", "snapshot usable codeAction title", failureReason)) return false;
	return true;
}

bool testCurrentDocumentPositionSnapshot(std::string &failureReason) {
	const mr::services::MRWorkspaceServiceSnapshot workspace = makeWorkspace(5);
	const mr::services::MRWorkspaceDocumentSnapshot document = workspace.documents[0];
	std::string mainUri;
	mr::lsp::LspDiagnosticBatch batch;
	mr::lsp::LspCodeActionResult codeActionResult;
	mr::services::MRServiceResultStore store;

	if (!makeUri(document.path, mainUri, failureReason)) return false;
	batch.uri = mainUri;
	batch.version = 5;
	batch.accepted = true;
	batch.diagnostics.push_back(mr::lsp::LspDiagnostic());
	batch.diagnostics[0].range.startLine = 3;
	batch.diagnostics[0].range.startCharacter = 2;
	batch.diagnostics[0].range.endLine = 3;
	batch.diagnostics[0].range.endCharacter = 6;
	batch.diagnostics[0].message = "position diagnostic";
	batch.diagnostics.push_back(mr::lsp::LspDiagnostic());
	batch.diagnostics[1].range.startLine = 3;
	batch.diagnostics[1].range.startCharacter = 7;
	batch.diagnostics[1].range.endLine = 3;
	batch.diagnostics[1].range.endCharacter = 9;
	batch.diagnostics[1].message = "adjacent diagnostic";
	batch.diagnostics.push_back(mr::lsp::LspDiagnostic());
	batch.diagnostics[2].range.startLine = 5;
	batch.diagnostics[2].range.startCharacter = 10;
	batch.diagnostics[2].range.endLine = 5;
	batch.diagnostics[2].range.endCharacter = 10;
	batch.diagnostics[2].message = "zero-width diagnostic";
	codeActionResult.uri = mainUri;
	codeActionResult.items.push_back(mr::lsp::LspCodeActionItem());
	codeActionResult.items[0].title = "Position edit";
	codeActionResult.items[0].hasEdit = true;
	codeActionResult.items[0].edits.push_back(mr::lsp::LspCodeActionTextEdit());
	codeActionResult.items[0].edits[0].uri = mainUri;
	codeActionResult.items[0].edits[0].newText = ";";

	store.putDiagnostics(mr::services::buildServiceDiagnosticsFromLsp(workspace, batch));
	{
		mr::services::MRServiceCodeActionResult serviceCodeActions = mr::services::buildServiceCodeActionsFromLsp(workspace, mainUri, 5, "position-code-action", codeActionResult);

		serviceCodeActions.hasContextRange = true;
		serviceCodeActions.contextRange.start.line = 3;
		serviceCodeActions.contextRange.start.character = 2;
		serviceCodeActions.contextRange.end.line = 3;
		serviceCodeActions.contextRange.end.character = 6;
		store.putCodeActions(serviceCodeActions);
	}

	const mr::services::MRServicePositionResultsSnapshot inside = store.currentResultsForDocumentPosition(document, mr::services::MRServiceTextPosition{3, 4});
	const mr::services::MRServicePositionResultsSnapshot before = store.currentResultsForDocumentPosition(document, mr::services::MRServiceTextPosition{3, 1});
	const mr::services::MRServicePositionResultsSnapshot adjacent = store.currentResultsForDocumentPosition(document, mr::services::MRServiceTextPosition{3, 7});
	const mr::services::MRServicePositionResultsSnapshot zeroBefore = store.currentResultsForDocumentPosition(document, mr::services::MRServiceTextPosition{5, 9});
	const mr::services::MRServicePositionResultsSnapshot zeroAfter = store.currentResultsForDocumentPosition(document, mr::services::MRServiceTextPosition{5, 11});
	const mr::services::MRServicePositionResultsSnapshot outside = store.currentResultsForDocumentPosition(document, mr::services::MRServiceTextPosition{4, 0});
	if (!expect(inside.document.current.diagnostics == 1, "position document diagnostic count", failureReason)) return false;
	if (!expect(inside.diagnostics.size() == 1, "position diagnostic hit count", failureReason)) return false;
	if (!expect(inside.diagnostics[0].diagnostics.size() == 1, "position diagnostic entry hit count", failureReason)) return false;
	if (!expect(before.diagnostics.size() == 1, "position diagnostic leading hit count", failureReason)) return false;
	if (!expect(before.diagnostics[0].diagnostics.size() == 1, "position diagnostic leading entry hit count", failureReason)) return false;
	if (!expect(before.diagnostics[0].diagnostics[0].message == "position diagnostic", "position diagnostic leading entry", failureReason)) return false;
	if (!expect(adjacent.diagnostics.size() == 1, "position adjacent diagnostic hit count", failureReason)) return false;
	if (!expect(adjacent.diagnostics[0].diagnostics.size() == 1, "position adjacent diagnostic entry count", failureReason)) return false;
	if (!expect(adjacent.diagnostics[0].diagnostics[0].message == "adjacent diagnostic", "position adjacent diagnostic entry", failureReason)) return false;
	if (!expect(zeroBefore.diagnostics.size() == 1, "zero diagnostic leading hit count", failureReason)) return false;
	if (!expect(zeroBefore.diagnostics[0].diagnostics[0].message == "zero-width diagnostic", "zero diagnostic leading entry", failureReason)) return false;
	if (!expect(zeroAfter.diagnostics.size() == 1, "zero diagnostic trailing hit count", failureReason)) return false;
	if (!expect(zeroAfter.diagnostics[0].diagnostics[0].message == "zero-width diagnostic", "zero diagnostic trailing entry", failureReason)) return false;
	if (!expect(inside.codeActions.size() == 1, "position codeAction hit count", failureReason)) return false;
	if (!expect(inside.codeActions[0].items.size() == 1, "position codeAction item count", failureReason)) return false;
	if (!expect(outside.document.current.diagnostics == 1, "outside document diagnostic count", failureReason)) return false;
	if (!expect(outside.diagnostics.empty(), "outside diagnostic miss count", failureReason)) return false;
	if (!expect(outside.codeActions.empty(), "outside codeAction miss count", failureReason)) return false;
	return true;
}

bool runProbe(std::string &failureReason) {
	if (!testDiagnosticsConversion(failureReason)) return false;
	if (!testStaleAndRejectedDiagnostics(failureReason)) return false;
	if (!testNavigationHoverCompletionConversion(failureReason)) return false;
	if (!testStoreReplacementAndStaleMarking(failureReason)) return false;
	if (!testCurrentDocumentSnapshot(failureReason)) return false;
	if (!testCurrentDocumentPositionSnapshot(failureReason)) return false;
	return true;
}
} // namespace

int main() {
	std::string failureReason;

	if (!runProbe(failureReason)) {
		std::cerr << "mr_service_results_probe failed: " << failureReason << "\n";
		return 1;
	}

	std::cout << "mr_service_results_probe passed\n";
	return 0;
}
