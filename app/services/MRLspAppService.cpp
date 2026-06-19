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
		snapshot.commands.requestDefinition = true;
		snapshot.commands.requestReferences = true;
		snapshot.commands.requestHover = true;
		snapshot.commands.requestCompletion = true;
		snapshot.commands.requestDocumentSymbols = true;
		snapshot.commands.requestWorkspaceSymbols = workspace.root.hasRoot;
		snapshot.commands.requestSignatureHelp = true;
		snapshot.commands.requestRename = true;
		snapshot.commands.requestCodeActions = snapshot.results.current.diagnostics > 0;
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
		snapshot.commands.requestDefinition = true;
		snapshot.commands.requestReferences = true;
		snapshot.commands.requestHover = true;
		snapshot.commands.requestCompletion = true;
		snapshot.commands.requestDocumentSymbols = true;
		snapshot.commands.requestWorkspaceSymbols = workspace.root.hasRoot;
		snapshot.commands.requestSignatureHelp = true;
		snapshot.commands.requestRename = true;
		snapshot.commands.requestCodeActions = !snapshot.results.diagnostics.empty();
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
	std::string &errorMessage) {
	if (workspace.documents.empty()) {
		errorMessage = "LSP app service workspace has no documents.";
		return false;
	}

	return session.requestEditorDocumentServiceCommand(workspace, profile, document, editor, command, position, errorMessage);
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
	std::string &errorMessage) {
	MRWorkspaceServiceSnapshot workspace = buildCurrentWorkspaceSnapshot();

	return requestEditorCommand(profile, workspace, document, editor, command, position, errorMessage);
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

std::string MRLspAppService::activeHoverRequestId() const {
	return session.activeHoverRequestId();
}

std::string MRLspAppService::activeSignatureHelpRequestId() const {
	return session.activeSignatureHelpRequestId();
}

} // namespace mr::services
