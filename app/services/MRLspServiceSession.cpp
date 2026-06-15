#include "MRLspServiceSession.hpp"

#include "MRLspEditorSource.hpp"
#include "../../lsp/MRLspUri.hpp"

#include <poll.h>
#include <vector>

namespace {

void appendJsonString(std::string &out, const std::string &text) {
	out.push_back('"');
	for (char ch : text) {
		if (ch == '"' || ch == '\\') {
			out.push_back('\\');
			out.push_back(ch);
		} else if (ch == '\n') {
			out += "\\n";
		} else if (ch == '\r') {
			out += "\\r";
		} else if (ch == '\t') {
			out += "\\t";
		} else {
			out.push_back(ch);
		}
	}
	out.push_back('"');
}

std::string workspaceFolderNameForRootPath(const std::string &rootPath) {
	const std::size_t slash = rootPath.find_last_of('/');

	if (rootPath.empty()) return "mr-workspace";
	if (slash == std::string::npos) return rootPath;
	if (slash + 1 < rootPath.size()) return rootPath.substr(slash + 1);
	return rootPath;
}

const mr::services::MRLspServiceCommandSpec lspServiceCommandTable[] = {
	{ mr::services::MRLspServiceCommandId::GoToDefinition, mr::services::MRLspServiceRequestKind::Definition, false, "MR_LSP_GOTO_DEFINITION", "LSP Go To Definition" },
	{ mr::services::MRLspServiceCommandId::FindReferences, mr::services::MRLspServiceRequestKind::References, true, "MR_LSP_FIND_REFERENCES", "LSP Find References" },
	{ mr::services::MRLspServiceCommandId::ShowHover, mr::services::MRLspServiceRequestKind::Hover, false, "MR_LSP_SHOW_HOVER", "LSP Show Hover" },
	{ mr::services::MRLspServiceCommandId::Complete, mr::services::MRLspServiceRequestKind::Completion, false, "MR_LSP_COMPLETE", "LSP Complete" },
};

mr::lsp::LspCodeActionRange codeActionRangeFromServiceRange(const mr::services::MRServiceTextRange &range) {
	mr::lsp::LspCodeActionRange codeActionRange;

	codeActionRange.start.line = range.start.line;
	codeActionRange.start.character = range.start.character;
	codeActionRange.end.line = range.end.line;
	codeActionRange.end.character = range.end.character;
	return codeActionRange;
}

} // namespace

namespace mr::services {

MRLspServiceSession::MRLspServiceSession() noexcept
	: documentService(lifecycle) {
}

bool buildLspInitializeSpecFromWorkspace(const MRWorkspaceServiceSnapshot &workspace, const mr::lsp::LspSessionSpec &sessionSpec, mr::lsp::LspInitializeSpec &spec, std::string &errorMessage) {
	std::string rootUri;
	std::string params;
	const std::string rootPath = normalizeWorkspaceServicePath(workspace.root.rootPath);

	spec = mr::lsp::LspInitializeSpec();
	spec.session = sessionSpec;
	params = "{\"processId\":null,";
	if (workspace.root.hasRoot) {
		if (!mr::lsp::pathToFileUri(rootPath, rootUri, errorMessage)) return false;
		params += "\"rootPath\":";
		appendJsonString(params, rootPath);
		params += ",\"rootUri\":";
		appendJsonString(params, rootUri);
		params += ",\"workspaceFolders\":[{\"uri\":";
		appendJsonString(params, rootUri);
		params += ",\"name\":";
		appendJsonString(params, workspaceFolderNameForRootPath(rootPath));
		params += "}],";
	} else {
		params += "\"rootPath\":null,\"rootUri\":null,\"workspaceFolders\":null,";
	}
	params += "\"capabilities\":{}}";
	spec.initializeParamsJson = params;
	errorMessage.clear();
	return true;
}

bool buildLspInitializeSpecFromServerProfile(const MRWorkspaceServiceSnapshot &workspace, const MRLspServerProfile &profile, mr::lsp::LspInitializeSpec &spec, std::string &errorMessage) {
	mr::lsp::LspSessionSpec sessionSpec;

	if (profile.executablePath.empty()) {
		errorMessage = "LSP server profile executable path is empty.";
		return false;
	}
	sessionSpec.process.executablePath = profile.executablePath;
	sessionSpec.process.arguments = profile.arguments;
	if (!profile.workingDirectory.empty()) {
		sessionSpec.process.workingDirectory = normalizeWorkspaceServicePath(profile.workingDirectory);
	} else if (workspace.root.hasRoot) {
		sessionSpec.process.workingDirectory = normalizeWorkspaceServicePath(workspace.root.rootPath);
	}
	return buildLspInitializeSpecFromWorkspace(workspace, sessionSpec, spec, errorMessage);
}

bool lspServiceCommandSpec(MRLspServiceCommandId command, MRLspServiceCommandSpec &spec) noexcept {
	const std::size_t commandCount = sizeof(lspServiceCommandTable) / sizeof(lspServiceCommandTable[0]);

	for (std::size_t index = 0; index < commandCount; ++index) {
		if (lspServiceCommandTable[index].command != command) continue;
		spec = lspServiceCommandTable[index];
		return true;
	}
	spec = MRLspServiceCommandSpec();
	return false;
}

bool MRLspServiceSession::start(const mr::lsp::LspInitializeSpec &spec, std::string &errorMessage) {
	clearRequests();
	clearRuntimeBinding();
	resultStore.clear();
	hasActiveWorkspace = false;
	activeEditorDocumentId = 0;
	activeEditorDocumentVersion = 0;
	activeEditorDocumentPath.clear();
	return lifecycle.start(spec, errorMessage);
}

bool MRLspServiceSession::start(const MRWorkspaceServiceSnapshot &workspace, const MRLspServerProfile &profile, std::string &errorMessage) {
	mr::lsp::LspInitializeSpec spec;

	if (!buildLspInitializeSpecFromServerProfile(workspace, profile, spec, errorMessage)) return false;
	return start(spec, errorMessage);
}

bool MRLspServiceSession::startRuntime(const MRWorkspaceServiceSnapshot &workspace, const MRLspServerProfile &profile, std::string &errorMessage) {
	if (!start(workspace, profile, errorMessage)) return false;
	if (!sendInitialized(errorMessage)) {
		close();
		return false;
	}
	activeServerProfile = profile;
	activeRuntimeHasRoot = workspace.root.hasRoot;
	if (workspace.root.hasRoot) {
		activeRuntimeRootPath = normalizeWorkspaceServicePath(workspace.root.rootPath);
	} else {
		activeRuntimeRootPath.clear();
	}
	hasActiveRuntime = true;
	errorMessage.clear();
	return true;
}

bool MRLspServiceSession::ensureRuntime(const MRWorkspaceServiceSnapshot &workspace, const MRLspServerProfile &profile, std::string &errorMessage) {
	if (runtimeMatches(workspace, profile)) {
		errorMessage.clear();
		return true;
	}
	if (lifecycle.state() != mr::lsp::LspLifecycleState::Stopped && lifecycle.state() != mr::lsp::LspLifecycleState::Shutdown)
		close();
	return startRuntime(workspace, profile, errorMessage);
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

bool MRLspServiceSession::openEditorDocument(const MRWorkspaceServiceSnapshot &workspace, const MRWorkspaceDocumentSnapshot &document, const MRFileEditor &editor, std::string &errorMessage) {
	mr::lsp::LspDocumentSourceSnapshot source;

	if (!buildLspDocumentSourceSnapshotFromEditor(document, editor, source, errorMessage)) return false;
	if (!openDocument(workspace, source, errorMessage)) return false;
	activeEditorDocumentId = document.documentId;
	activeEditorDocumentVersion = document.documentVersion;
	activeEditorDocumentPath = normalizeWorkspaceServicePath(document.path);
	return true;
}

bool MRLspServiceSession::changeEditorDocument(const MRWorkspaceServiceSnapshot &workspace, const MRWorkspaceDocumentSnapshot &document, const MRFileEditor &editor, std::string &errorMessage) {
	mr::lsp::LspDocumentSourceSnapshot source;

	if (!buildLspDocumentSourceSnapshotFromEditor(document, editor, source, errorMessage)) return false;
	if (!changeDocument(workspace, source, errorMessage)) return false;
	activeEditorDocumentId = document.documentId;
	activeEditorDocumentVersion = document.documentVersion;
	activeEditorDocumentPath = normalizeWorkspaceServicePath(document.path);
	return true;
}

bool MRLspServiceSession::syncEditorDocument(const MRWorkspaceServiceSnapshot &workspace, const MRWorkspaceDocumentSnapshot &document, const MRFileEditor &editor, std::string &errorMessage) {
	mr::lsp::LspDocumentSourceSnapshot source;
	const std::string documentPath = normalizeWorkspaceServicePath(document.path);

	if (!buildLspDocumentSourceSnapshotFromEditor(document, editor, source, errorMessage)) return false;
	if (!documentService.isOpen()) {
		if (!openDocument(workspace, source, errorMessage)) return false;
		activeEditorDocumentId = document.documentId;
		activeEditorDocumentVersion = document.documentVersion;
		activeEditorDocumentPath = documentPath;
		return true;
	}
	if (activeEditorDocumentId == document.documentId && activeEditorDocumentPath == documentPath) {
		if (activeEditorDocumentVersion == document.documentVersion) {
			errorMessage.clear();
			return true;
		}
		if (!changeDocument(workspace, source, errorMessage)) return false;
		activeEditorDocumentVersion = document.documentVersion;
		return true;
	}
	if (!closeDocument(errorMessage)) return false;
	if (!openDocument(workspace, source, errorMessage)) return false;
	activeEditorDocumentId = document.documentId;
	activeEditorDocumentVersion = document.documentVersion;
	activeEditorDocumentPath = documentPath;
	return true;
}

bool MRLspServiceSession::syncEditorDocumentAndRequest(
	const MRWorkspaceServiceSnapshot &workspace,
	const MRWorkspaceDocumentSnapshot &document,
	const MRFileEditor &editor,
	MRLspServiceRequestKind requestKind,
	mr::lsp::LspTextPosition position,
	bool includeDeclaration,
	std::string &errorMessage) {
	if (!syncEditorDocument(workspace, document, editor, errorMessage)) return false;
	switch (requestKind) {
		case MRLspServiceRequestKind::Definition:
			return requestDefinition(position, errorMessage);
		case MRLspServiceRequestKind::References:
			return requestReferences(position, includeDeclaration, errorMessage);
		case MRLspServiceRequestKind::Hover:
			return requestHover(position, errorMessage);
		case MRLspServiceRequestKind::Completion:
			return requestCompletion(position, errorMessage);
	}
	errorMessage = "LSP service request kind is unknown.";
	return false;
}

bool MRLspServiceSession::requestEditorDocumentService(
	const MRWorkspaceServiceSnapshot &workspace,
	const MRLspServerProfile &profile,
	const MRWorkspaceDocumentSnapshot &document,
	const MRFileEditor &editor,
	MRLspServiceRequestKind requestKind,
	mr::lsp::LspTextPosition position,
	bool includeDeclaration,
	std::string &errorMessage) {
	if (!ensureRuntime(workspace, profile, errorMessage)) return false;
	return syncEditorDocumentAndRequest(workspace, document, editor, requestKind, position, includeDeclaration, errorMessage);
}

bool MRLspServiceSession::requestEditorDocumentServiceCommand(
	const MRWorkspaceServiceSnapshot &workspace,
	const MRLspServerProfile &profile,
	const MRWorkspaceDocumentSnapshot &document,
	const MRFileEditor &editor,
	MRLspServiceCommandId command,
	mr::lsp::LspTextPosition position,
	std::string &errorMessage) {
	MRLspServiceCommandSpec spec;

	if (!lspServiceCommandSpec(command, spec)) {
		errorMessage = "LSP service command is unknown.";
		return false;
	}
	return requestEditorDocumentService(workspace, profile, document, editor, spec.requestKind, position, spec.includeDeclaration, errorMessage);
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

bool MRLspServiceSession::requestCodeActionsForDiagnostic(const MRServiceDiagnosticResult &diagnosticResult, const MRServiceDiagnosticEntry &diagnostic, std::string &errorMessage) {
	if (!hasActiveWorkspace) {
		errorMessage = "LSP service session has no active workspace.";
		return false;
	}
	if (diagnosticResult.header.state != MRServiceResultState::Current) {
		errorMessage = "LSP codeAction diagnostic result is not current.";
		return false;
	}
	if (!serviceDocumentIdentityMatches(activeWorkspace, diagnosticResult.header.identity)) {
		errorMessage = "LSP codeAction diagnostic result no longer matches workspace.";
		return false;
	}
	if (diagnosticResult.header.identity.uri != documentService.documentUri()) {
		errorMessage = "LSP codeAction diagnostic document does not match open document.";
		return false;
	}
	if (!codeActionAdapter.requestCodeActions(lifecycle, documentService, codeActionRangeFromServiceRange(diagnostic.reportedRange), diagnostic.rawLspDiagnosticJson, codeActionRequest, errorMessage)) return false;
	codeActionRequestVersion = diagnosticResult.header.identity.documentVersion;
	return true;
}

bool MRLspServiceSession::closeDocument(std::string &errorMessage) {
	if (!documentService.isOpen()) {
		activeEditorDocumentId = 0;
		activeEditorDocumentVersion = 0;
		activeEditorDocumentPath.clear();
		errorMessage.clear();
		return true;
	}
	if (!documentService.close(errorMessage)) return false;
	clearRequests();
	hasActiveWorkspace = false;
	activeWorkspace = MRWorkspaceServiceSnapshot();
	activeEditorDocumentId = 0;
	activeEditorDocumentVersion = 0;
	activeEditorDocumentPath.clear();
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
	clearRuntimeBinding();
	documentService.clear();
	lifecycle.close();
	hasActiveWorkspace = false;
	activeWorkspace = MRWorkspaceServiceSnapshot();
	activeEditorDocumentId = 0;
	activeEditorDocumentVersion = 0;
	activeEditorDocumentPath.clear();
}

const MRServiceResultStore &MRLspServiceSession::results() const noexcept {
	return resultStore;
}

bool MRLspServiceSession::runtimeActive() const noexcept {
	return hasActiveRuntime && lifecycle.state() == mr::lsp::LspLifecycleState::Initialized;
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

bool MRLspServiceSession::runtimeMatches(const MRWorkspaceServiceSnapshot &workspace, const MRLspServerProfile &profile) const {
	std::string rootPath;

	if (!hasActiveRuntime) return false;
	if (lifecycle.state() != mr::lsp::LspLifecycleState::Initialized) return false;
	if (activeServerProfile.profileName != profile.profileName) return false;
	if (activeServerProfile.executablePath != profile.executablePath) return false;
	if (activeServerProfile.arguments != profile.arguments) return false;
	if (activeServerProfile.workingDirectory != profile.workingDirectory) return false;
	if (activeRuntimeHasRoot != workspace.root.hasRoot) return false;
	if (!workspace.root.hasRoot) return true;
	rootPath = normalizeWorkspaceServicePath(workspace.root.rootPath);
	return activeRuntimeRootPath == rootPath;
}

bool MRLspServiceSession::consumeInboundMessage(const mr::lsp::LspInboundMessage &message, std::string &errorMessage) {
	mr::lsp::LspDiagnosticBatch batch;
	mr::lsp::LspDefinitionResult definition;
	mr::lsp::LspReferencesResult references;
	mr::lsp::LspHoverResult hover;
	mr::lsp::LspCompletionResult completion;
	mr::lsp::LspCodeActionResult codeActions;
	bool accepted = false;

	if (hasActiveWorkspace) {
		if (!diagnosticsAdapter.consume(message, documentService, batch, errorMessage)) return false;
		if (batch.accepted || batch.stale || batch.rejected) resultStore.putDiagnostics(buildServiceDiagnosticsFromLsp(activeWorkspace, batch));
	}

	if (!definitionAdapter.consume(message, documentService, definitionRequest, definition, accepted, errorMessage)) return false;
	if (accepted) {
		resultStore.putLocations(buildServiceDefinitionFromLsp(activeWorkspace, definitionRequest.uri, activeWorkspace.documents.front().documentVersion, definitionRequest.idText, definition));
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
	if (!codeActionAdapter.consume(message, documentService, codeActionRequest, codeActions, accepted, errorMessage)) return false;
	if (accepted) resultStore.putCodeActions(buildServiceCodeActionsFromLsp(activeWorkspace, codeActionRequest.uri, codeActionRequestVersion, codeActionRequest.idText, codeActions));
	return true;
}

void MRLspServiceSession::clearRequests() noexcept {
	definitionRequest = mr::lsp::LspDefinitionRequest();
	referencesRequest = mr::lsp::LspReferencesRequest();
	hoverRequest = mr::lsp::LspHoverRequest();
	completionRequest = mr::lsp::LspCompletionRequest();
	codeActionRequest = mr::lsp::LspCodeActionRequest();
	codeActionRequestVersion = 0;
}

void MRLspServiceSession::clearRuntimeBinding() noexcept {
	activeServerProfile = MRLspServerProfile();
	activeRuntimeRootPath.clear();
	activeRuntimeHasRoot = false;
	hasActiveRuntime = false;
}

} // namespace mr::services
