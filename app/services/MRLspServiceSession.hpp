#ifndef MRLSPSERVICESESSION_HPP
#define MRLSPSERVICESESSION_HPP

#include "MRServiceResults.hpp"

#include "../../lsp/MRLspCodeAction.hpp"
#include "../../lsp/MRLspCompletion.hpp"
#include "../../lsp/MRLspDiagnostics.hpp"
#include "../../lsp/MRLspDocumentService.hpp"
#include "../../lsp/MRLspHover.hpp"
#include "../../lsp/MRLspReferences.hpp"

#include <string>
#include <vector>

class MRFileEditor;

namespace mr::services {

struct MRLspServerProfile {
	std::string profileName;
	std::string executablePath;
	std::vector<std::string> arguments;
	std::string workingDirectory;
};

enum class MRLspServiceRequestKind {
	Definition = 0,
	References,
	Hover,
	Completion
};

enum class MRLspServiceCommandId {
	GoToDefinition = 0,
	FindReferences,
	ShowHover,
	Complete
};

struct MRLspServiceCommandSpec {
	MRLspServiceCommandId command = MRLspServiceCommandId::GoToDefinition;
	MRLspServiceRequestKind requestKind = MRLspServiceRequestKind::Definition;
	bool includeDeclaration = false;
	const char *actionId = "";
	const char *title = "";
};

[[nodiscard]] bool buildLspInitializeSpecFromWorkspace(const MRWorkspaceServiceSnapshot &workspace, const mr::lsp::LspSessionSpec &sessionSpec, mr::lsp::LspInitializeSpec &spec, std::string &errorMessage);
[[nodiscard]] bool buildLspInitializeSpecFromServerProfile(const MRWorkspaceServiceSnapshot &workspace, const MRLspServerProfile &profile, mr::lsp::LspInitializeSpec &spec, std::string &errorMessage);
[[nodiscard]] bool lspServiceCommandSpec(MRLspServiceCommandId command, MRLspServiceCommandSpec &spec) noexcept;

class MRLspServiceSession {
public:
	MRLspServiceSession() noexcept;
	MRLspServiceSession(const MRLspServiceSession &) = delete;
	MRLspServiceSession &operator=(const MRLspServiceSession &) = delete;

	bool start(const mr::lsp::LspInitializeSpec &spec, std::string &errorMessage);
	bool start(const MRWorkspaceServiceSnapshot &workspace, const MRLspServerProfile &profile, std::string &errorMessage);
	bool startRuntime(const MRWorkspaceServiceSnapshot &workspace, const MRLspServerProfile &profile, std::string &errorMessage);
	bool ensureRuntime(const MRWorkspaceServiceSnapshot &workspace, const MRLspServerProfile &profile, std::string &errorMessage);
	bool sendInitialized(std::string &errorMessage);
	bool openDocument(const MRWorkspaceServiceSnapshot &workspace, const mr::lsp::LspDocumentSourceSnapshot &source, std::string &errorMessage);
	bool changeDocument(const MRWorkspaceServiceSnapshot &workspace, const mr::lsp::LspDocumentSourceSnapshot &source, std::string &errorMessage);
	bool openEditorDocument(const MRWorkspaceServiceSnapshot &workspace, const MRWorkspaceDocumentSnapshot &document, const MRFileEditor &editor, std::string &errorMessage);
	bool changeEditorDocument(const MRWorkspaceServiceSnapshot &workspace, const MRWorkspaceDocumentSnapshot &document, const MRFileEditor &editor, std::string &errorMessage);
	bool syncEditorDocument(const MRWorkspaceServiceSnapshot &workspace, const MRWorkspaceDocumentSnapshot &document, const MRFileEditor &editor, std::string &errorMessage);
	bool syncEditorDocumentAndRequest(
		const MRWorkspaceServiceSnapshot &workspace,
		const MRWorkspaceDocumentSnapshot &document,
		const MRFileEditor &editor,
		MRLspServiceRequestKind requestKind,
		mr::lsp::LspTextPosition position,
		bool includeDeclaration,
		std::string &errorMessage);
	bool requestEditorDocumentService(
		const MRWorkspaceServiceSnapshot &workspace,
		const MRLspServerProfile &profile,
		const MRWorkspaceDocumentSnapshot &document,
		const MRFileEditor &editor,
		MRLspServiceRequestKind requestKind,
		mr::lsp::LspTextPosition position,
		bool includeDeclaration,
		std::string &errorMessage);
	bool requestEditorDocumentServiceCommand(
		const MRWorkspaceServiceSnapshot &workspace,
		const MRLspServerProfile &profile,
		const MRWorkspaceDocumentSnapshot &document,
		const MRFileEditor &editor,
		MRLspServiceCommandId command,
		mr::lsp::LspTextPosition position,
		std::string &errorMessage);
	bool poll(std::string &errorMessage);
	bool requestDefinition(mr::lsp::LspTextPosition position, std::string &errorMessage);
	bool requestReferences(mr::lsp::LspTextPosition position, bool includeDeclaration, std::string &errorMessage);
	bool requestHover(mr::lsp::LspTextPosition position, std::string &errorMessage);
	bool requestCompletion(mr::lsp::LspTextPosition position, std::string &errorMessage);
	bool requestCodeActionsForDiagnostic(const MRServiceDiagnosticResult &diagnosticResult, const MRServiceDiagnosticEntry &diagnostic, std::string &errorMessage);
	bool closeDocument(std::string &errorMessage);
	bool shutdown(std::string &errorMessage);
	void close();

	[[nodiscard]] const MRServiceResultStore &results() const noexcept;
	[[nodiscard]] bool runtimeActive() const noexcept;

private:
	bool pollUntilState(mr::lsp::LspLifecycleState expectedState, std::string &errorMessage);
	bool acceptWorkspaceForSource(const MRWorkspaceServiceSnapshot &workspace, const mr::lsp::LspDocumentSourceSnapshot &source, std::string &errorMessage);
	bool consumeInboundMessage(const mr::lsp::LspInboundMessage &message, std::string &errorMessage);
	bool runtimeMatches(const MRWorkspaceServiceSnapshot &workspace, const MRLspServerProfile &profile) const;
	void clearRequests() noexcept;
	void clearRuntimeBinding() noexcept;

	mr::lsp::LspLifecycle lifecycle;
	mr::lsp::LspDocumentService documentService;
	mr::lsp::LspDiagnosticsAdapter diagnosticsAdapter;
	mr::lsp::LspDefinitionAdapter definitionAdapter;
	mr::lsp::LspReferencesAdapter referencesAdapter;
	mr::lsp::LspHoverAdapter hoverAdapter;
	mr::lsp::LspCompletionAdapter completionAdapter;
	mr::lsp::LspCodeActionAdapter codeActionAdapter;
	mr::lsp::LspDefinitionRequest definitionRequest;
	mr::lsp::LspReferencesRequest referencesRequest;
	mr::lsp::LspHoverRequest hoverRequest;
	mr::lsp::LspCompletionRequest completionRequest;
	mr::lsp::LspCodeActionRequest codeActionRequest;
	std::size_t codeActionRequestVersion = 0;
	MRWorkspaceServiceSnapshot activeWorkspace;
	MRServiceResultStore resultStore;
	std::size_t activeEditorDocumentId = 0;
	std::size_t activeEditorDocumentVersion = 0;
	std::string activeEditorDocumentPath;
	bool hasActiveWorkspace = false;
	MRLspServerProfile activeServerProfile;
	std::string activeRuntimeRootPath;
	bool activeRuntimeHasRoot = false;
	bool hasActiveRuntime = false;
};

} // namespace mr::services

#endif
