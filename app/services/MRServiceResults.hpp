#ifndef MRSERVICERESULTS_HPP
#define MRSERVICERESULTS_HPP

#include "MRWorkspaceServiceContext.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace mr::lsp {
struct LspCodeActionResult;
struct LspCompletionResult;
struct LspDefinitionResult;
struct LspDiagnosticBatch;
struct LspHoverResult;
struct LspLocation;
struct LspReferencesResult;
}

namespace mr::services {

enum class MRServiceResultSource {
	Unknown = 0,
	Lsp,
	Build,
	Git,
	Debugger
};

enum class MRServiceResultKind {
	Diagnostics = 0,
	Definition,
	References,
	Hover,
	Completion,
	CodeAction
};

enum class MRServiceResultState {
	Current = 0,
	Stale,
	Rejected,
	Error
};

struct MRServiceTextPosition {
	int line = 0;
	int character = 0;
};

struct MRServiceTextRange {
	MRServiceTextPosition start;
	MRServiceTextPosition end;
};

struct MRServiceDocumentIdentity {
	bool valid = false;
	int bufferId = 0;
	std::size_t documentId = 0;
	std::size_t documentVersion = 0;
	std::string path;
	std::string uri;
};

struct MRServiceResultHeader {
	MRServiceResultSource source = MRServiceResultSource::Unknown;
	MRServiceResultKind kind = MRServiceResultKind::Diagnostics;
	MRServiceResultState state = MRServiceResultState::Rejected;
	MRServiceDocumentIdentity identity;
	std::string requestId;
	std::string errorMessage;
};

struct MRServiceDiagnosticEntry {
	MRServiceTextRange reportedRange;
	MRServiceTextRange navigationRange;
	int severity = 0;
	std::string message;
	std::string rawLspDiagnosticJson;
};

struct MRServiceLocationTarget {
	std::string uri;
	std::string path;
	MRServiceTextRange range;
};

struct MRServiceHoverPayload {
	std::string markupKind;
	std::string value;
};

struct MRServiceCompletionItem {
	std::string label;
	bool hasKind = false;
	int kind = 0;
	std::string detail;
	std::string insertText;
	bool hasInsertTextFormat = false;
	int insertTextFormat = 1;
	bool hasTextEdit = false;
	MRServiceTextRange textEditRange;
	std::string textEditNewText;
};

struct MRServiceTextEdit {
	std::string uri;
	std::string path;
	MRServiceTextRange range;
	std::string newText;
};

struct MRServiceCodeActionItem {
	std::string title;
	std::string kind;
	bool hasEdit = false;
	bool hasCommand = false;
	std::vector<MRServiceTextEdit> edits;
	std::string rawLspCodeActionJson;
};

struct MRServiceDiagnosticResult {
	MRServiceResultHeader header;
	std::vector<MRServiceDiagnosticEntry> diagnostics;
};

struct MRServiceLocationResult {
	MRServiceResultHeader header;
	std::vector<MRServiceLocationTarget> locations;
};

struct MRServiceHoverResult {
	MRServiceResultHeader header;
	MRServiceHoverPayload hover;
};

struct MRServiceCompletionResult {
	MRServiceResultHeader header;
	bool hasRequestPosition = false;
	MRServiceTextPosition requestPosition;
	std::string rawLspResponseJson;
	std::vector<MRServiceCompletionItem> items;
};

struct MRServiceCodeActionResult {
	MRServiceResultHeader header;
	bool hasContextRange = false;
	MRServiceTextRange contextRange;
	std::vector<MRServiceCodeActionItem> items;
};

struct MRServiceResultCounts {
	std::size_t diagnostics = 0;
	std::size_t definitions = 0;
	std::size_t references = 0;
	std::size_t hovers = 0;
	std::size_t completions = 0;
	std::size_t codeActions = 0;
};

struct MRServiceDocumentResultsSnapshot {
	MRServiceDocumentIdentity identity;
	MRServiceResultCounts stored;
	MRServiceResultCounts current;
	std::vector<MRServiceDiagnosticResult> diagnostics;
	std::vector<MRServiceLocationResult> definitions;
	std::vector<MRServiceLocationResult> references;
	std::vector<MRServiceHoverResult> hovers;
	std::vector<MRServiceCompletionResult> completions;
	std::vector<MRServiceCodeActionResult> codeActions;
};

struct MRServicePositionResultsSnapshot {
	MRServiceTextPosition position;
	MRServiceDocumentResultsSnapshot document;
	std::vector<MRServiceDiagnosticResult> diagnostics;
	std::vector<MRServiceCodeActionResult> codeActions;
};

class MRServiceResultStore {
public:
	void clear() noexcept;
	void putDiagnostics(const MRServiceDiagnosticResult &result);
	void putLocations(const MRServiceLocationResult &result);
	void putHover(const MRServiceHoverResult &result);
	void putCompletion(const MRServiceCompletionResult &result);
	void putCodeActions(const MRServiceCodeActionResult &result);
	void markStaleAgainstWorkspace(const MRWorkspaceServiceSnapshot &workspace);

	[[nodiscard]] const std::vector<MRServiceDiagnosticResult> &diagnosticResults() const noexcept;
	[[nodiscard]] const std::vector<MRServiceLocationResult> &locationResults() const noexcept;
	[[nodiscard]] const std::vector<MRServiceHoverResult> &hoverResults() const noexcept;
	[[nodiscard]] const std::vector<MRServiceCompletionResult> &completionResults() const noexcept;
	[[nodiscard]] const std::vector<MRServiceCodeActionResult> &codeActionResults() const noexcept;
	[[nodiscard]] MRServiceResultCounts resultCounts() const noexcept;
	[[nodiscard]] MRServiceDocumentResultsSnapshot currentResultsForDocument(const MRWorkspaceDocumentSnapshot &document) const;
	[[nodiscard]] MRServicePositionResultsSnapshot currentResultsForDocumentPosition(const MRWorkspaceDocumentSnapshot &document, MRServiceTextPosition position) const;

private:
	std::vector<MRServiceDiagnosticResult> diagnostics;
	std::vector<MRServiceLocationResult> locations;
	std::vector<MRServiceHoverResult> hovers;
	std::vector<MRServiceCompletionResult> completions;
	std::vector<MRServiceCodeActionResult> codeActions;
};

[[nodiscard]] bool serviceDocumentIdentityMatches(const MRWorkspaceServiceSnapshot &workspace, const MRServiceDocumentIdentity &identity) noexcept;
[[nodiscard]] bool serviceDocumentIdentityMatchesDocument(const MRWorkspaceDocumentSnapshot &document, const MRServiceDocumentIdentity &identity) noexcept;
[[nodiscard]] bool serviceTextRangeContainsPosition(const MRServiceTextRange &range, MRServiceTextPosition position) noexcept;
[[nodiscard]] bool serviceCodeActionAppliesToDocument(const MRServiceCodeActionItem &item, const MRServiceDocumentIdentity &identity) noexcept;
[[nodiscard]] MRServiceDiagnosticResult buildServiceDiagnosticsFromLsp(const MRWorkspaceServiceSnapshot &workspace, const mr::lsp::LspDiagnosticBatch &batch);
[[nodiscard]] MRServiceLocationResult buildServiceDefinitionFromLsp(const MRWorkspaceServiceSnapshot &workspace, const std::string &originUri, std::size_t originVersion, const std::string &requestId, const mr::lsp::LspDefinitionResult &definition);
[[nodiscard]] MRServiceLocationResult buildServiceReferencesFromLsp(const MRWorkspaceServiceSnapshot &workspace, const std::string &originUri, std::size_t originVersion, const std::string &requestId, const mr::lsp::LspReferencesResult &references);
[[nodiscard]] MRServiceHoverResult buildServiceHoverFromLsp(const MRWorkspaceServiceSnapshot &workspace, std::size_t originVersion, const std::string &requestId, const mr::lsp::LspHoverResult &hover);
[[nodiscard]] MRServiceCompletionResult buildServiceCompletionFromLsp(const MRWorkspaceServiceSnapshot &workspace, const std::string &originUri, std::size_t originVersion, const std::string &requestId, const mr::lsp::LspCompletionResult &completion);
[[nodiscard]] MRServiceCodeActionResult buildServiceCodeActionsFromLsp(const MRWorkspaceServiceSnapshot &workspace, const std::string &originUri, std::size_t originVersion, const std::string &requestId, const mr::lsp::LspCodeActionResult &codeActions);

} // namespace mr::services

#endif
