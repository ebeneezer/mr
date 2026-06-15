#include "MRLspAppService.hpp"

namespace mr::services {

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
	return workspaceContext.configuredMainFile();
}

MRWorkspaceServiceSnapshot MRLspAppService::buildWorkspaceSnapshot(const std::vector<MRWorkspaceDocumentSnapshot> &documents) const {
	return workspaceContext.buildSnapshot(documents);
}

MRWorkspaceServiceSnapshot MRLspAppService::buildCurrentWorkspaceSnapshot() const {
	return buildCurrentWorkspaceServiceSnapshot(workspaceContext);
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

} // namespace mr::services
