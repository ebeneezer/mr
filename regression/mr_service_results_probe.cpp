#include <iostream>
#include <string>
#include <vector>

#include "../app/services/MRServiceResults.hpp"
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

	const mr::services::MRServiceDiagnosticResult result = mr::services::buildServiceDiagnosticsFromLsp(workspace, batch);
	if (!expect(result.header.state == mr::services::MRServiceResultState::Current, "diagnostics state", failureReason)) return false;
	if (!expect(result.header.identity.valid, "diagnostics identity", failureReason)) return false;
	if (!expect(result.header.identity.bufferId == 10, "diagnostics buffer", failureReason)) return false;
	if (!expect(result.header.identity.documentVersion == 5, "diagnostics version", failureReason)) return false;
	if (!expect(result.diagnostics.size() == 1, "diagnostics count", failureReason)) return false;
	if (!expect(result.diagnostics[0].message == "deterministic diagnostic", "diagnostics message", failureReason)) return false;
	if (!expect(result.diagnostics[0].range.start.character == 3, "diagnostics range", failureReason)) return false;
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

	mr::lsp::LspDefinitionResult definitionResult;
	definitionResult.originUri = mainUri;
	definitionResult.locations.push_back(makeLocation(mainUri, 4));
	const mr::services::MRServiceLocationResult definition = mr::services::buildServiceDefinitionFromLsp(workspace, mainUri, 5, "mr-definition-1", definitionResult);
	const mr::services::MRServiceLocationResult refs = mr::services::buildServiceReferencesFromLsp(workspace, mainUri, 5, "mr-references-1", references);
	const mr::services::MRServiceHoverResult serviceHover = mr::services::buildServiceHoverFromLsp(workspace, 5, "mr-hover-1", hover);
	const mr::services::MRServiceCompletionResult serviceCompletion = mr::services::buildServiceCompletionFromLsp(workspace, mainUri, 5, "mr-completion-1", completion);

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

bool runProbe(std::string &failureReason) {
	if (!testDiagnosticsConversion(failureReason)) return false;
	if (!testStaleAndRejectedDiagnostics(failureReason)) return false;
	if (!testNavigationHoverCompletionConversion(failureReason)) return false;
	if (!testStoreReplacementAndStaleMarking(failureReason)) return false;
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
