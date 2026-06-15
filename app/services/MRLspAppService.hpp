#ifndef MRLSPAPPSERVICE_HPP
#define MRLSPAPPSERVICE_HPP

#include "MRLspServiceSession.hpp"
#include "MRWorkspaceServiceContext.hpp"

#include <string>
#include <vector>

class MRFileEditor;

namespace mr::services {

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

private:
	MRWorkspaceServiceContext workspaceContext;
	MRLspServiceSession session;
};

} // namespace mr::services

#endif
