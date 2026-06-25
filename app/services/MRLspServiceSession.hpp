#ifndef MRLSPSERVICESESSION_HPP
#define MRLSPSERVICESESSION_HPP

#include "MRServiceResults.hpp"

#include "../../lsp/MRLspCodeAction.hpp"
#include "../../lsp/MRLspCompletion.hpp"
#include "../../lsp/MRLspDiagnostics.hpp"
#include "../../lsp/MRLspDocumentHighlight.hpp"
#include "../../lsp/MRLspDocumentService.hpp"
#include "../../lsp/MRLspDocumentSymbols.hpp"
#include "../../lsp/MRLspHover.hpp"
#include "../../lsp/MRLspReferences.hpp"
#include "../../lsp/MRLspSignatureHelp.hpp"

#include <string>
#include <vector>

class MRFileEditor;

namespace mr::services {

struct MRLspServerProfile {
	std::string profileName;
	std::string executablePath;
	std::vector<std::string> arguments;
	std::string workingDirectory;
	std::string lspMiddlewarePath;
};

enum class MRLspServiceRequestKind {
	Definition = 0,
	References,
	Hover,
	Completion,
	DocumentHighlight,
	DocumentSymbols,
	WorkspaceSymbols,
	SignatureHelp,
	Rename
};

enum class MRLspServiceCommandId {
	GoToDefinition = 0,
	FindReferences,
	ShowHover,
	Complete,
	DocumentHighlight,
	DocumentSymbols,
	WorkspaceSymbols,
	SignatureHelp,
	Rename
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
		const std::string &completionTriggerCandidate,
		std::string &errorMessage);
	bool requestEditorDocumentService(
		const MRWorkspaceServiceSnapshot &workspace,
		const MRLspServerProfile &profile,
		const MRWorkspaceDocumentSnapshot &document,
		const MRFileEditor &editor,
		MRLspServiceRequestKind requestKind,
		mr::lsp::LspTextPosition position,
		bool includeDeclaration,
		const std::string &completionTriggerCandidate,
		std::string &errorMessage);
	bool requestEditorDocumentServiceCommand(
		const MRWorkspaceServiceSnapshot &workspace,
		const MRLspServerProfile &profile,
		const MRWorkspaceDocumentSnapshot &document,
		const MRFileEditor &editor,
		MRLspServiceCommandId command,
		mr::lsp::LspTextPosition position,
		const std::string &completionTriggerCandidate,
		std::string &errorMessage);
	bool poll(std::string &errorMessage);
	bool requestDefinition(mr::lsp::LspTextPosition position, std::string &errorMessage);
	bool requestReferences(mr::lsp::LspTextPosition position, bool includeDeclaration, std::string &errorMessage);
	bool requestHover(mr::lsp::LspTextPosition position, std::string &errorMessage);
	bool requestCompletion(mr::lsp::LspTextPosition position, const std::string &triggerCandidate, std::string &errorMessage);
	bool requestDocumentHighlight(mr::lsp::LspTextPosition position, std::string &errorMessage);
	bool requestDocumentSymbols(std::string &errorMessage);
	bool requestWorkspaceSymbols(const std::string &query, std::string &errorMessage);
	bool requestSignatureHelp(mr::lsp::LspTextPosition position, std::string &errorMessage);
	bool requestRename(mr::lsp::LspTextPosition position, const std::string &newName, std::string &errorMessage);
	bool requestCodeActionsForDiagnostic(const MRServiceDiagnosticResult &diagnosticResult, const MRServiceDiagnosticEntry &diagnostic, std::string &errorMessage);
	bool resolveCompletionItem(const MRServiceCompletionItem &item, MRServiceCompletionItem &resolvedItem, std::string &errorMessage);
	bool closeDocument(std::string &errorMessage);
	bool shutdown(std::string &errorMessage);
	void close();

	[[nodiscard]] const MRServiceResultStore &results() const noexcept;
	[[nodiscard]] bool runtimeActive() const noexcept;
	[[nodiscard]] bool runtimeCapabilitiesKnown() const noexcept;
	[[nodiscard]] bool supportsRequestKind(MRLspServiceRequestKind requestKind) const noexcept;
	[[nodiscard]] bool supportsCodeActions() const noexcept;
	[[nodiscard]] std::string activeHoverRequestId() const;
	[[nodiscard]] std::string activeSignatureHelpRequestId() const;

private:
	bool pollUntilState(mr::lsp::LspLifecycleState expectedState, std::string &errorMessage);
	bool acceptWorkspaceForSource(const MRWorkspaceServiceSnapshot &workspace, const mr::lsp::LspDocumentSourceSnapshot &source, std::string &errorMessage);
	bool consumeInboundMessage(const mr::lsp::LspInboundMessage &message, std::string &errorMessage);
	bool runtimeMatches(const MRWorkspaceServiceSnapshot &workspace, const MRLspServerProfile &profile) const;
	bool requestKindSupported(MRLspServiceRequestKind requestKind, std::string &errorMessage) const;
	bool completionTriggerCharacterAccepted(const std::string &candidate, std::string &triggerCharacter) const;
	void updateCapabilitiesFromInitializeResponse(const std::string &payload) noexcept;
	void clearRequests() noexcept;
	void clearRuntimeBinding() noexcept;

	mr::lsp::LspLifecycle lifecycle;
	mr::lsp::LspDocumentService documentService;
	mr::lsp::LspDiagnosticsAdapter diagnosticsAdapter;
	mr::lsp::LspDefinitionAdapter definitionAdapter;
	mr::lsp::LspReferencesAdapter referencesAdapter;
	mr::lsp::LspHoverAdapter hoverAdapter;
	mr::lsp::LspCompletionAdapter completionAdapter;
	mr::lsp::LspDocumentHighlightAdapter documentHighlightAdapter;
	mr::lsp::LspDocumentSymbolsAdapter documentSymbolsAdapter;
	mr::lsp::LspSignatureHelpAdapter signatureHelpAdapter;
	mr::lsp::LspCodeActionAdapter codeActionAdapter;
	mr::lsp::LspRenameAdapter renameAdapter;
	mr::lsp::LspDefinitionRequest definitionRequest;
	mr::lsp::LspReferencesRequest referencesRequest;
	mr::lsp::LspHoverRequest hoverRequest;
	mr::lsp::LspCompletionRequest completionRequest;
	mr::lsp::LspDocumentHighlightRequest documentHighlightRequest;
	mr::lsp::LspDocumentSymbolsRequest documentSymbolsRequest;
	mr::lsp::LspWorkspaceSymbolsRequest workspaceSymbolsRequest;
	mr::lsp::LspSignatureHelpRequest signatureHelpRequest;
	mr::lsp::LspCodeActionRequest codeActionRequest;
	mr::lsp::LspRenameRequest renameRequest;
	std::size_t definitionRequestVersion = 0;
	std::size_t referencesRequestVersion = 0;
	std::size_t hoverRequestVersion = 0;
	std::size_t completionRequestVersion = 0;
	std::size_t documentHighlightRequestVersion = 0;
	std::size_t documentSymbolsRequestVersion = 0;
	std::size_t signatureHelpRequestVersion = 0;
	MRServiceTextRange codeActionRequestRange;
	std::size_t codeActionRequestVersion = 0;
	std::size_t renameRequestVersion = 0;
	MRWorkspaceServiceSnapshot activeWorkspace;
	MRServiceResultStore resultStore;
	std::size_t activeEditorDocumentId = 0;
	std::size_t activeEditorDocumentVersion = 0;
	std::string activeEditorDocumentPath;
	bool hasActiveWorkspace = false;
	MRLspServerProfile activeServerProfile;
	std::string activeRuntimeRootPath;
	std::string activeRuntimeCompileContextFingerprint;
	bool activeRuntimeHasRoot = false;
	bool hasActiveRuntime = false;
	bool supportsDefinition = false;
	bool supportsReferences = false;
	bool supportsHover = false;
	bool supportsCompletion = false;
	bool supportsDocumentHighlight = false;
	bool supportsDocumentSymbols = false;
	bool supportsWorkspaceSymbols = false;
	bool supportsSignatureHelp = false;
	bool supportsRename = false;
	bool supportsCodeAction = false;
	bool supportsCompletionResolve = false;
	std::vector<std::string> completionTriggerCharacters;
};

} // namespace mr::services

#endif
