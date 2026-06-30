#include "MRLspAppService.hpp"

namespace mr::services {
namespace {
bool workspaceContainsDocument(const MRWorkspaceServiceSnapshot &workspace, const MRWorkspaceDocumentSnapshot &document) noexcept {
	const std::string documentPath = normalizeWorkspaceServicePath(document.path);

	for (const MRWorkspaceDocumentSnapshot &candidate : workspace.documents) {
		if (candidate.path != documentPath) continue;
		if (candidate.documentId != 0 && document.documentId != 0 && candidate.documentId != document.documentId) continue;
		if (candidate.documentVersion != document.documentVersion) continue;
		return true;
	}
	return false;
}

bool workspaceContainsPath(const MRWorkspaceServiceSnapshot &workspace, const std::string &path) {
	const std::string documentPath = normalizeWorkspaceServicePath(path);

	if (documentPath.empty()) return false;
	for (const MRWorkspaceDocumentSnapshot &document : workspace.documents)
		if (document.path == documentPath) return true;
	return false;
}

void setLspCommandAvailability(const MRLspServiceSession &session, bool hasWorkspaceRoot, bool hasCodeActionContext, MRLspCommandAvailability &commands) noexcept {
	const bool capabilitiesKnown = session.runtimeCapabilitiesKnown();

	commands.requestDefinition = !capabilitiesKnown || session.supportsRequestKind(MRLspServiceRequestKind::Definition);
	commands.requestReferences = !capabilitiesKnown || session.supportsRequestKind(MRLspServiceRequestKind::References);
	commands.requestHover = !capabilitiesKnown || session.supportsRequestKind(MRLspServiceRequestKind::Hover);
	commands.requestCompletion = !capabilitiesKnown || session.supportsRequestKind(MRLspServiceRequestKind::Completion);
	commands.requestDocumentHighlight = !capabilitiesKnown || session.supportsRequestKind(MRLspServiceRequestKind::DocumentHighlight);
	commands.requestDocumentSymbols = !capabilitiesKnown || session.supportsRequestKind(MRLspServiceRequestKind::DocumentSymbols);
	commands.requestWorkspaceSymbols = hasWorkspaceRoot && (!capabilitiesKnown || session.supportsRequestKind(MRLspServiceRequestKind::WorkspaceSymbols));
	commands.requestSignatureHelp = !capabilitiesKnown || session.supportsRequestKind(MRLspServiceRequestKind::SignatureHelp);
	commands.requestRename = !capabilitiesKnown || session.supportsRequestKind(MRLspServiceRequestKind::Rename);
	commands.requestCodeActions = hasCodeActionContext && (!capabilitiesKnown || session.supportsCodeActions());
	commands.applyCodeActions = hasCodeActionContext;
}
} // namespace

void MRLspAppService::clearMainFile() noexcept {
	workspaceContext.clearMainFile();
}

void MRLspAppService::setMainFileByBufferId(int bufferId) noexcept {
	workspaceContext.setMainFileByBufferId(bufferId);
}

void MRLspAppService::setMainFileByPath(const std::string &path) {
	workspaceContext.setMainFileByPath(path);
}

MRWorkspaceMainFileState MRLspAppService::configuredMainFile() const {
	return buildCurrentWorkspaceSnapshot().mainFile;
}

MRWorkspaceServiceSnapshot MRLspAppService::buildWorkspaceSnapshot(const std::vector<MRWorkspaceDocumentSnapshot> &documents) const {
	return workspaceContext.buildSnapshot(documents);
}

MRWorkspaceServiceSnapshot MRLspAppService::buildCurrentWorkspaceSnapshot() const {
	return buildCurrentWorkspaceServiceSnapshot(workspaceContext);
}

MRLspDocumentServiceSnapshot MRLspAppService::documentServiceSnapshot(const MRWorkspaceServiceSnapshot &workspace, const MRWorkspaceDocumentSnapshot &document) const {
	MRLspDocumentServiceSnapshot snapshot;
	MRWorkspaceDocumentSnapshot normalizedDocument = document;

	normalizedDocument.path = normalizeWorkspaceServicePath(normalizedDocument.path);
	snapshot.runtimeActive = runtimeActive();
	snapshot.mainFile = workspace.mainFile;
	snapshot.documentInWorkspace = workspaceContainsDocument(workspace, normalizedDocument);
	snapshot.results = session.results().currentResultsForDocument(normalizedDocument);
	if (snapshot.documentInWorkspace) {
		setLspCommandAvailability(session, workspace.root.hasRoot, snapshot.results.current.diagnostics > 0, snapshot.commands);
		snapshot.commands.applyCodeActions = snapshot.results.current.codeActions > 0;
	}
	return snapshot;
}

MRLspDocumentServiceSnapshot MRLspAppService::currentDocumentServiceSnapshot(const MRWorkspaceDocumentSnapshot &document) const {
	return documentServiceSnapshot(buildCurrentWorkspaceSnapshot(), document);
}

MRLspPositionServiceSnapshot MRLspAppService::documentPositionServiceSnapshot(const MRWorkspaceServiceSnapshot &workspace, const MRWorkspaceDocumentSnapshot &document, MRServiceTextPosition position) const {
	MRLspPositionServiceSnapshot snapshot;
	MRWorkspaceDocumentSnapshot normalizedDocument = document;

	normalizedDocument.path = normalizeWorkspaceServicePath(normalizedDocument.path);
	snapshot.runtimeActive = runtimeActive();
	snapshot.mainFile = workspace.mainFile;
	snapshot.documentInWorkspace = workspaceContainsDocument(workspace, normalizedDocument);
	snapshot.results = session.results().currentResultsForDocumentPosition(normalizedDocument, position);
	if (snapshot.documentInWorkspace) {
		setLspCommandAvailability(session, workspace.root.hasRoot, !snapshot.results.diagnostics.empty(), snapshot.commands);
		snapshot.commands.applyCodeActions = !snapshot.results.codeActions.empty();
	}
	return snapshot;
}

MRLspPositionServiceSnapshot MRLspAppService::currentDocumentPositionServiceSnapshot(const MRWorkspaceDocumentSnapshot &document, MRServiceTextPosition position) const {
	return documentPositionServiceSnapshot(buildCurrentWorkspaceSnapshot(), document, position);
}

bool MRLspAppService::requestEditorCommand(
	const MRLspServerProfile &profile,
	const MRWorkspaceServiceSnapshot &workspace,
	const MRWorkspaceDocumentSnapshot &document,
	const MRFileEditor &editor,
	MRLspServiceCommandId command,
	mr::lsp::LspTextPosition position,
	const std::string &completionTriggerCandidate,
	std::string &errorMessage) {
	if (workspace.documents.empty()) {
		errorMessage = "LSP app service workspace has no documents.";
		return false;
	}

	return session.requestEditorDocumentServiceCommand(workspace, profile, document, editor, command, position, completionTriggerCandidate, errorMessage);
}

bool MRLspAppService::syncEditorDocument(
	const MRLspServerProfile &profile,
	const MRWorkspaceServiceSnapshot &workspace,
	const MRWorkspaceDocumentSnapshot &document,
	const MRFileEditor &editor,
	std::string &errorMessage) {
	if (workspace.documents.empty()) {
		errorMessage = "LSP app service workspace has no documents.";
		return false;
	}
	if (!session.ensureRuntime(workspace, profile, errorMessage)) return false;
	return session.syncEditorDocument(workspace, document, editor, errorMessage);
}

bool MRLspAppService::requestCurrentEditorCommand(
	const MRLspServerProfile &profile,
	const MRWorkspaceDocumentSnapshot &document,
	const MRFileEditor &editor,
	MRLspServiceCommandId command,
	mr::lsp::LspTextPosition position,
	const std::string &completionTriggerCandidate,
	std::string &errorMessage) {
	MRWorkspaceServiceSnapshot workspace = buildCurrentWorkspaceSnapshot();

	return requestEditorCommand(profile, workspace, document, editor, command, position, completionTriggerCandidate, errorMessage);
}

bool MRLspAppService::requestWorkspaceSymbols(
	const MRLspServerProfile &profile,
	const MRWorkspaceServiceSnapshot &workspace,
	const MRWorkspaceDocumentSnapshot &document,
	const MRFileEditor &editor,
	const std::string &query,
	std::string &errorMessage) {
	if (workspace.documents.empty()) {
		errorMessage = "LSP app service workspace has no documents.";
		return false;
	}
	if (!session.ensureRuntime(workspace, profile, errorMessage)) return false;
	if (!session.syncEditorDocument(workspace, document, editor, errorMessage)) return false;
	return session.requestWorkspaceSymbols(query, errorMessage);
}

bool MRLspAppService::requestRename(
	const MRLspServerProfile &profile,
	const MRWorkspaceServiceSnapshot &workspace,
	const MRWorkspaceDocumentSnapshot &document,
	const MRFileEditor &editor,
	mr::lsp::LspTextPosition position,
	const std::string &newName,
	std::string &errorMessage) {
	if (workspace.documents.empty()) {
		errorMessage = "LSP app service workspace has no documents.";
		return false;
	}
	if (!session.ensureRuntime(workspace, profile, errorMessage)) return false;
	if (!session.syncEditorDocument(workspace, document, editor, errorMessage)) return false;
	return session.requestRename(position, newName, errorMessage);
}

bool MRLspAppService::syncCurrentEditorDocument(
	const MRLspServerProfile &profile,
	const MRWorkspaceDocumentSnapshot &document,
	const MRFileEditor &editor,
	std::string &errorMessage) {
	MRWorkspaceServiceSnapshot workspace = buildCurrentWorkspaceSnapshot();

	return syncEditorDocument(profile, workspace, document, editor, errorMessage);
}

bool MRLspAppService::requestCodeActionsForDiagnostic(const MRServiceDiagnosticResult &diagnosticResult, const MRServiceDiagnosticEntry &diagnostic, std::string &errorMessage) {
	return session.requestCodeActionsForDiagnostic(diagnosticResult, diagnostic, errorMessage);
}

bool MRLspAppService::resolveCompletionItem(const MRServiceCompletionItem &item, MRServiceCompletionItem &resolvedItem, std::string &errorMessage) {
	return session.resolveCompletionItem(item, resolvedItem, errorMessage);
}

bool MRLspAppService::closeActiveDocumentIfMissingFromWorkspace(const MRWorkspaceServiceSnapshot &workspace, bool &closedDocument, std::string &errorMessage) {
	closedDocument = false;
	if (!session.documentOpen()) {
		errorMessage.clear();
		return true;
	}
	if (workspaceContainsPath(workspace, session.activeDocumentPath())) {
		errorMessage.clear();
		return true;
	}
	if (!session.closeDocument(errorMessage)) return false;
	closedDocument = true;
	errorMessage.clear();
	return true;
}

bool MRLspAppService::poll(std::string &errorMessage) {
	return session.poll(errorMessage);
}

bool MRLspAppService::shutdown(std::string &errorMessage) {
	if (!session.runtimeActive()) {
		errorMessage.clear();
		return true;
	}
	return session.shutdown(errorMessage);
}

void MRLspAppService::close() {
	session.close();
}

const MRServiceResultStore &MRLspAppService::results() const noexcept {
	return session.results();
}

bool MRLspAppService::runtimeActive() const noexcept {
	return session.runtimeActive();
}

bool MRLspAppService::documentOpen() const noexcept {
	return session.documentOpen();
}

std::string MRLspAppService::activeHoverRequestId() const {
	return session.activeHoverRequestId();
}

std::string MRLspAppService::activeSignatureHelpRequestId() const {
	return session.activeSignatureHelpRequestId();
}

} // namespace mr::services
