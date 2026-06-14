#include "MRLspServiceSession.hpp"

#include <poll.h>
#include <vector>

namespace mr::services {

MRLspServiceSession::MRLspServiceSession() noexcept
	: documentService(lifecycle) {
}

bool MRLspServiceSession::start(const mr::lsp::LspInitializeSpec &spec, std::string &errorMessage) {
	clearRequests();
	resultStore.clear();
	hasActiveWorkspace = false;
	return lifecycle.start(spec, errorMessage);
}

bool MRLspServiceSession::sendInitialized(std::string &errorMessage) {
	if (!pollUntilState(mr::lsp::LspLifecycleState::Initialized, errorMessage)) return false;
	return lifecycle.sendInitialized(errorMessage);
}

bool MRLspServiceSession::openDocument(const MRWorkspaceServiceSnapshot &workspace, const mr::lsp::LspDocumentSourceSnapshot &source, std::string &errorMessage) {
	if (!acceptWorkspaceForSource(workspace, source, errorMessage)) return false;
	if (!documentService.open(source, errorMessage)) return false;
	activeWorkspace = workspace;
	hasActiveWorkspace = true;
	clearRequests();
	return true;
}

bool MRLspServiceSession::changeDocument(const MRWorkspaceServiceSnapshot &workspace, const mr::lsp::LspDocumentSourceSnapshot &source, std::string &errorMessage) {
	if (!acceptWorkspaceForSource(workspace, source, errorMessage)) return false;
	if (!documentService.change(source, errorMessage)) return false;
	activeWorkspace = workspace;
	hasActiveWorkspace = true;
	resultStore.markStaleAgainstWorkspace(activeWorkspace);
	return true;
}

bool MRLspServiceSession::poll(std::string &errorMessage) {
	std::vector<mr::lsp::LspInboundMessage> messages;

	if (!lifecycle.poll(messages, errorMessage)) return false;
	for (const mr::lsp::LspInboundMessage &message : messages)
		if (!consumeInboundMessage(message, errorMessage)) return false;
	errorMessage.clear();
	return true;
}

bool MRLspServiceSession::requestDefinition(mr::lsp::LspTextPosition position, std::string &errorMessage) {
	if (!hasActiveWorkspace) {
		errorMessage = "LSP service session has no active workspace.";
		return false;
	}
	return definitionAdapter.requestDefinition(lifecycle, documentService, position, definitionRequest, errorMessage);
}

bool MRLspServiceSession::requestReferences(mr::lsp::LspTextPosition position, bool includeDeclaration, std::string &errorMessage) {
	if (!hasActiveWorkspace) {
		errorMessage = "LSP service session has no active workspace.";
		return false;
	}
	return referencesAdapter.requestReferences(lifecycle, documentService, position, includeDeclaration, referencesRequest, errorMessage);
}

bool MRLspServiceSession::requestHover(mr::lsp::LspTextPosition position, std::string &errorMessage) {
	if (!hasActiveWorkspace) {
		errorMessage = "LSP service session has no active workspace.";
		return false;
	}
	return hoverAdapter.requestHover(lifecycle, documentService, position, hoverRequest, errorMessage);
}

bool MRLspServiceSession::requestCompletion(mr::lsp::LspTextPosition position, std::string &errorMessage) {
	if (!hasActiveWorkspace) {
		errorMessage = "LSP service session has no active workspace.";
		return false;
	}
	return completionAdapter.requestCompletion(lifecycle, documentService, position, completionRequest, errorMessage);
}

bool MRLspServiceSession::closeDocument(std::string &errorMessage) {
	if (!documentService.isOpen()) {
		errorMessage.clear();
		return true;
	}
	if (!documentService.close(errorMessage)) return false;
	clearRequests();
	hasActiveWorkspace = false;
	activeWorkspace = MRWorkspaceServiceSnapshot();
	return true;
}

bool MRLspServiceSession::shutdown(std::string &errorMessage) {
	int exitStatus = -1;

	if (!lifecycle.shutdown(errorMessage)) return false;
	if (!pollUntilState(mr::lsp::LspLifecycleState::Shutdown, errorMessage)) return false;
	if (!lifecycle.exit(errorMessage)) return false;
	if (!lifecycle.wait(1000, exitStatus)) {
		errorMessage = "LSP service session wait failed.";
		return false;
	}
	if (exitStatus != 0) {
		errorMessage = "LSP service session exited with status " + std::to_string(exitStatus) + ".";
		return false;
	}
	errorMessage.clear();
	return true;
}

void MRLspServiceSession::close() {
	clearRequests();
	documentService.clear();
	lifecycle.close();
	hasActiveWorkspace = false;
	activeWorkspace = MRWorkspaceServiceSnapshot();
}

const MRServiceResultStore &MRLspServiceSession::results() const noexcept {
	return resultStore;
}

bool MRLspServiceSession::pollUntilState(mr::lsp::LspLifecycleState expectedState, std::string &errorMessage) {
	for (int i = 0; i < 50; ++i) {
		if (!poll(errorMessage)) return false;
		if (lifecycle.state() == expectedState) return true;
		::poll(nullptr, 0, 20);
	}
	errorMessage = "LSP service session did not reach expected lifecycle state.";
	return false;
}

bool MRLspServiceSession::acceptWorkspaceForSource(const MRWorkspaceServiceSnapshot &workspace, const mr::lsp::LspDocumentSourceSnapshot &source, std::string &errorMessage) {
	const std::string sourcePath = normalizeWorkspaceServicePath(source.absolutePath);

	if (sourcePath.empty()) {
		errorMessage = "LSP service source path is empty.";
		return false;
	}
	for (const MRWorkspaceDocumentSnapshot &document : workspace.documents) {
		if (document.path != sourcePath) continue;
		if (document.documentVersion != static_cast<std::size_t>(source.version)) {
			errorMessage = "LSP service source version does not match workspace document version.";
			return false;
		}
		errorMessage.clear();
		return true;
	}
	errorMessage = "LSP service source document is not part of the workspace.";
	return false;
}

bool MRLspServiceSession::consumeInboundMessage(const mr::lsp::LspInboundMessage &message, std::string &errorMessage) {
	mr::lsp::LspDiagnosticBatch batch;
	mr::lsp::LspLocation definitionLocation;
	mr::lsp::LspReferencesResult references;
	mr::lsp::LspHoverResult hover;
	mr::lsp::LspCompletionResult completion;
	bool accepted = false;

	if (hasActiveWorkspace) {
		if (!diagnosticsAdapter.consume(message, documentService, batch, errorMessage)) return false;
		if (batch.accepted || batch.stale || batch.rejected) resultStore.putDiagnostics(buildServiceDiagnosticsFromLsp(activeWorkspace, batch));
	}

	if (!definitionAdapter.consume(message, documentService, definitionRequest, definitionLocation, accepted, errorMessage)) return false;
	if (accepted) {
		resultStore.putLocations(buildServiceDefinitionFromLsp(activeWorkspace, definitionRequest.uri, activeWorkspace.documents.front().documentVersion, definitionRequest.idText, definitionLocation));
		return true;
	}

	if (!referencesAdapter.consume(message, documentService, referencesRequest, references, accepted, errorMessage)) return false;
	if (accepted) {
		resultStore.putLocations(buildServiceReferencesFromLsp(activeWorkspace, referencesRequest.uri, activeWorkspace.documents.front().documentVersion, referencesRequest.idText, references));
		return true;
	}

	if (!hoverAdapter.consume(message, documentService, hoverRequest, hover, accepted, errorMessage)) return false;
	if (accepted) {
		resultStore.putHover(buildServiceHoverFromLsp(activeWorkspace, activeWorkspace.documents.front().documentVersion, hoverRequest.idText, hover));
		return true;
	}

	if (!completionAdapter.consume(message, documentService, completionRequest, completion, accepted, errorMessage)) return false;
	if (accepted) resultStore.putCompletion(buildServiceCompletionFromLsp(activeWorkspace, completionRequest.uri, activeWorkspace.documents.front().documentVersion, completionRequest.idText, completion));
	return true;
}

void MRLspServiceSession::clearRequests() noexcept {
	definitionRequest = mr::lsp::LspDefinitionRequest();
	referencesRequest = mr::lsp::LspReferencesRequest();
	hoverRequest = mr::lsp::LspHoverRequest();
	completionRequest = mr::lsp::LspCompletionRequest();
}

} // namespace mr::services
