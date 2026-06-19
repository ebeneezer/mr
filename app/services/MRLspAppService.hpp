#ifndef MRLSPAPPSERVICE_HPP
#define MRLSPAPPSERVICE_HPP

#include "MRLspServiceSession.hpp"
#include "MRWorkspaceServiceContext.hpp"

#include <string>
#include <vector>

class MRFileEditor;

namespace mr::services {

struct MRLspCommandAvailability {
	bool requestDefinition = false;
	bool requestReferences = false;
	bool requestHover = false;
	bool requestCompletion = false;
	bool requestDocumentSymbols = false;
	bool requestWorkspaceSymbols = false;
	bool requestSignatureHelp = false;
	bool requestRename = false;
	bool requestCodeActions = false;
	bool applyCodeActions = false;
};

struct MRLspDocumentServiceSnapshot {
	bool runtimeActive = false;
	bool documentInWorkspace = false;
	MRWorkspaceMainFileState mainFile;
	MRServiceDocumentResultsSnapshot results;
	MRLspCommandAvailability commands;
};

struct MRLspPositionServiceSnapshot {
	bool runtimeActive = false;
	bool documentInWorkspace = false;
	MRWorkspaceMainFileState mainFile;
	MRServicePositionResultsSnapshot results;
	MRLspCommandAvailability commands;
};

class MRLspAppService {
public:
	MRLspAppService() noexcept = default;
	MRLspAppService(const MRLspAppService &) = delete;
	MRLspAppService &operator=(const MRLspAppService &) = delete;

	void clearMainFile() noexcept;
	void setMainFileByBufferId(int bufferId) noexcept;
	void setMainFileByPath(const std::string &path);

	[[nodiscard]] MRWorkspaceMainFileState configuredMainFile() const;
	[[nodiscard]] MRWorkspaceServiceSnapshot buildWorkspaceSnapshot(const std::vector<MRWorkspaceDocumentSnapshot> &documents) const;
	[[nodiscard]] MRWorkspaceServiceSnapshot buildCurrentWorkspaceSnapshot() const;
	[[nodiscard]] MRLspDocumentServiceSnapshot documentServiceSnapshot(const MRWorkspaceServiceSnapshot &workspace, const MRWorkspaceDocumentSnapshot &document) const;
	[[nodiscard]] MRLspDocumentServiceSnapshot currentDocumentServiceSnapshot(const MRWorkspaceDocumentSnapshot &document) const;
	[[nodiscard]] MRLspPositionServiceSnapshot documentPositionServiceSnapshot(const MRWorkspaceServiceSnapshot &workspace, const MRWorkspaceDocumentSnapshot &document, MRServiceTextPosition position) const;
	[[nodiscard]] MRLspPositionServiceSnapshot currentDocumentPositionServiceSnapshot(const MRWorkspaceDocumentSnapshot &document, MRServiceTextPosition position) const;

	bool requestEditorCommand(
		const MRLspServerProfile &profile,
		const MRWorkspaceServiceSnapshot &workspace,
		const MRWorkspaceDocumentSnapshot &document,
		const MRFileEditor &editor,
		MRLspServiceCommandId command,
		mr::lsp::LspTextPosition position,
		std::string &errorMessage);
	bool syncEditorDocument(
		const MRLspServerProfile &profile,
		const MRWorkspaceServiceSnapshot &workspace,
		const MRWorkspaceDocumentSnapshot &document,
		const MRFileEditor &editor,
		std::string &errorMessage);
	bool requestCurrentEditorCommand(
		const MRLspServerProfile &profile,
		const MRWorkspaceDocumentSnapshot &document,
		const MRFileEditor &editor,
		MRLspServiceCommandId command,
		mr::lsp::LspTextPosition position,
		std::string &errorMessage);
	bool requestWorkspaceSymbols(
		const MRLspServerProfile &profile,
		const MRWorkspaceServiceSnapshot &workspace,
		const MRWorkspaceDocumentSnapshot &document,
		const MRFileEditor &editor,
		const std::string &query,
		std::string &errorMessage);
	bool requestRename(
		const MRLspServerProfile &profile,
		const MRWorkspaceServiceSnapshot &workspace,
		const MRWorkspaceDocumentSnapshot &document,
		const MRFileEditor &editor,
		mr::lsp::LspTextPosition position,
		const std::string &newName,
		std::string &errorMessage);
	bool syncCurrentEditorDocument(
		const MRLspServerProfile &profile,
		const MRWorkspaceDocumentSnapshot &document,
		const MRFileEditor &editor,
		std::string &errorMessage);
	bool requestCodeActionsForDiagnostic(const MRServiceDiagnosticResult &diagnosticResult, const MRServiceDiagnosticEntry &diagnostic, std::string &errorMessage);
	bool poll(std::string &errorMessage);
	bool shutdown(std::string &errorMessage);
	void close();

	[[nodiscard]] const MRServiceResultStore &results() const noexcept;
	[[nodiscard]] bool runtimeActive() const noexcept;
	[[nodiscard]] std::string activeHoverRequestId() const;
	[[nodiscard]] std::string activeSignatureHelpRequestId() const;

private:
	MRWorkspaceServiceContext workspaceContext;
	MRLspServiceSession session;
};

} // namespace mr::services

#endif
