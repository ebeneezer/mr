#include "MRServiceResults.hpp"

#include "../../lsp/MRLspCodeAction.hpp"
#include "../../lsp/MRLspCompletion.hpp"
#include "../../lsp/MRLspDiagnostics.hpp"
#include "../../lsp/MRLspHover.hpp"
#include "../../lsp/MRLspReferences.hpp"
#include "../../lsp/MRLspUri.hpp"

namespace mr::services {
namespace {
MRServiceTextPosition servicePositionFromLsp(const mr::lsp::LspTextPosition &position) {
	MRServiceTextPosition servicePosition;

	servicePosition.line = position.line;
	servicePosition.character = position.character;
	return servicePosition;
}

MRServiceTextRange serviceRangeFromLsp(const mr::lsp::LspLocation &location) {
	MRServiceTextRange range;

	range.start = servicePositionFromLsp(location.start);
	range.end = servicePositionFromLsp(location.end);
	return range;
}

MRServiceTextRange serviceRangeFromLsp(const mr::lsp::LspDiagnosticRange &range) {
	MRServiceTextRange serviceRange;

	serviceRange.start.line = range.startLine;
	serviceRange.start.character = range.startCharacter;
	serviceRange.end.line = range.endLine;
	serviceRange.end.character = range.endCharacter;
	return serviceRange;
}

bool sameResultSlot(const MRServiceResultHeader &left, const MRServiceResultHeader &right) {
	if (left.source != right.source || left.kind != right.kind) return false;
	if (!left.requestId.empty() || !right.requestId.empty()) return left.requestId == right.requestId;
	if (!left.identity.path.empty() || !right.identity.path.empty()) return left.identity.path == right.identity.path;
	return left.identity.bufferId != 0 && left.identity.bufferId == right.identity.bufferId;
}

const MRWorkspaceDocumentSnapshot *findDocumentByPath(const MRWorkspaceServiceSnapshot &workspace, const std::string &path) noexcept {
	for (const MRWorkspaceDocumentSnapshot &document : workspace.documents)
		if (document.path == path) return &document;
	return nullptr;
}

MRServiceDocumentIdentity identityForDocument(const MRWorkspaceDocumentSnapshot &document, const std::string &uri) {
	MRServiceDocumentIdentity identity;

	identity.valid = true;
	identity.bufferId = document.bufferId;
	identity.documentId = document.documentId;
	identity.documentVersion = document.documentVersion;
	identity.path = document.path;
	identity.uri = uri;
	return identity;
}

MRServiceDocumentIdentity identityFromUri(const MRWorkspaceServiceSnapshot &workspace, const std::string &uri, std::size_t expectedVersion, MRServiceResultState &state, std::string &errorMessage) {
	MRServiceDocumentIdentity identity;
	std::string path;
	std::string uriError;

	identity.uri = uri;
	if (!mr::lsp::fileUriToPath(uri, path, uriError)) {
		state = MRServiceResultState::Error;
		errorMessage = uriError;
		return identity;
	}
	path = normalizeWorkspaceServicePath(path);
	identity.path = path;

	const MRWorkspaceDocumentSnapshot *document = findDocumentByPath(workspace, path);
	if (document == nullptr) {
		state = MRServiceResultState::Rejected;
		errorMessage = "service result document is not loaded";
		return identity;
	}

	identity = identityForDocument(*document, uri);
	if (expectedVersion != 0 && document->documentVersion != expectedVersion) {
		state = MRServiceResultState::Stale;
		errorMessage.clear();
		return identity;
	}

	state = MRServiceResultState::Current;
	errorMessage.clear();
	return identity;
}

MRServiceLocationTarget locationTargetFromLsp(const mr::lsp::LspLocation &location, MRServiceResultState &state, std::string &errorMessage) {
	MRServiceLocationTarget target;
	std::string path;
	std::string uriError;

	target.uri = location.uri;
	if (!mr::lsp::fileUriToPath(location.uri, path, uriError)) {
		state = MRServiceResultState::Error;
		errorMessage = uriError;
		return target;
	}
	target.path = normalizeWorkspaceServicePath(path);
	target.range = serviceRangeFromLsp(location);
	return target;
}

void applyBatchState(const mr::lsp::LspDiagnosticBatch &batch, MRServiceResultHeader &header) {
	if (header.state == MRServiceResultState::Error) return;
	if (batch.stale) {
		header.state = MRServiceResultState::Stale;
		return;
	}
	if (batch.rejected || !batch.accepted) {
		header.state = MRServiceResultState::Rejected;
		return;
	}
}
} // namespace

bool serviceDocumentIdentityMatches(const MRWorkspaceServiceSnapshot &workspace, const MRServiceDocumentIdentity &identity) noexcept {
	if (!identity.valid) return false;
	for (const MRWorkspaceDocumentSnapshot &document : workspace.documents) {
		if (identity.bufferId != 0 && document.bufferId != identity.bufferId) continue;
		if (!identity.path.empty() && document.path != identity.path) continue;
		if (identity.documentId != 0 && document.documentId != identity.documentId) continue;
		if (document.documentVersion != identity.documentVersion) return false;
		return true;
	}
	return false;
}

void MRServiceResultStore::clear() noexcept {
	diagnostics.clear();
	locations.clear();
	hovers.clear();
	completions.clear();
	codeActions.clear();
}

void MRServiceResultStore::putDiagnostics(const MRServiceDiagnosticResult &result) {
	for (MRServiceDiagnosticResult &existing : diagnostics) {
		if (sameResultSlot(existing.header, result.header)) {
			existing = result;
			return;
		}
	}
	diagnostics.push_back(result);
}

void MRServiceResultStore::putLocations(const MRServiceLocationResult &result) {
	for (MRServiceLocationResult &existing : locations) {
		if (sameResultSlot(existing.header, result.header)) {
			existing = result;
			return;
		}
	}
	locations.push_back(result);
}

void MRServiceResultStore::putHover(const MRServiceHoverResult &result) {
	for (MRServiceHoverResult &existing : hovers) {
		if (sameResultSlot(existing.header, result.header)) {
			existing = result;
			return;
		}
	}
	hovers.push_back(result);
}

void MRServiceResultStore::putCompletion(const MRServiceCompletionResult &result) {
	for (MRServiceCompletionResult &existing : completions) {
		if (sameResultSlot(existing.header, result.header)) {
			existing = result;
			return;
		}
	}
	completions.push_back(result);
}

void MRServiceResultStore::putCodeActions(const MRServiceCodeActionResult &result) {
	for (MRServiceCodeActionResult &existing : codeActions) {
		if (sameResultSlot(existing.header, result.header)) {
			existing = result;
			return;
		}
	}
	codeActions.push_back(result);
}

void MRServiceResultStore::markStaleAgainstWorkspace(const MRWorkspaceServiceSnapshot &workspace) {
	for (MRServiceDiagnosticResult &result : diagnostics)
		if (result.header.state == MRServiceResultState::Current && !serviceDocumentIdentityMatches(workspace, result.header.identity)) result.header.state = MRServiceResultState::Stale;
	for (MRServiceLocationResult &result : locations)
		if (result.header.state == MRServiceResultState::Current && !serviceDocumentIdentityMatches(workspace, result.header.identity)) result.header.state = MRServiceResultState::Stale;
	for (MRServiceHoverResult &result : hovers)
		if (result.header.state == MRServiceResultState::Current && !serviceDocumentIdentityMatches(workspace, result.header.identity)) result.header.state = MRServiceResultState::Stale;
	for (MRServiceCompletionResult &result : completions)
		if (result.header.state == MRServiceResultState::Current && !serviceDocumentIdentityMatches(workspace, result.header.identity)) result.header.state = MRServiceResultState::Stale;
	for (MRServiceCodeActionResult &result : codeActions)
		if (result.header.state == MRServiceResultState::Current && !serviceDocumentIdentityMatches(workspace, result.header.identity)) result.header.state = MRServiceResultState::Stale;
}

const std::vector<MRServiceDiagnosticResult> &MRServiceResultStore::diagnosticResults() const noexcept {
	return diagnostics;
}

const std::vector<MRServiceLocationResult> &MRServiceResultStore::locationResults() const noexcept {
	return locations;
}

const std::vector<MRServiceHoverResult> &MRServiceResultStore::hoverResults() const noexcept {
	return hovers;
}

const std::vector<MRServiceCompletionResult> &MRServiceResultStore::completionResults() const noexcept {
	return completions;
}

const std::vector<MRServiceCodeActionResult> &MRServiceResultStore::codeActionResults() const noexcept {
	return codeActions;
}

MRServiceDiagnosticResult buildServiceDiagnosticsFromLsp(const MRWorkspaceServiceSnapshot &workspace, const mr::lsp::LspDiagnosticBatch &batch) {
	MRServiceDiagnosticResult result;

	result.header.source = MRServiceResultSource::Lsp;
	result.header.kind = MRServiceResultKind::Diagnostics;
	result.header.identity = identityFromUri(workspace, batch.uri, batch.version > 0 ? static_cast<std::size_t>(batch.version) : 0, result.header.state, result.header.errorMessage);
	applyBatchState(batch, result.header);
	for (const mr::lsp::LspDiagnostic &diagnostic : batch.diagnostics) {
		MRServiceDiagnosticEntry entry;
		entry.reportedRange = serviceRangeFromLsp(diagnostic.range);
		entry.navigationRange = entry.reportedRange;
		entry.severity = diagnostic.severity;
		entry.message = diagnostic.message;
		entry.rawLspDiagnosticJson = diagnostic.rawJson;
		result.diagnostics.push_back(entry);
	}
	return result;
}

MRServiceLocationResult buildServiceDefinitionFromLsp(const MRWorkspaceServiceSnapshot &workspace, const std::string &originUri, std::size_t originVersion, const std::string &requestId, const mr::lsp::LspDefinitionResult &definition) {
	MRServiceLocationResult result;

	result.header.source = MRServiceResultSource::Lsp;
	result.header.kind = MRServiceResultKind::Definition;
	result.header.requestId = requestId;
	result.header.identity = identityFromUri(workspace, originUri, originVersion, result.header.state, result.header.errorMessage);
	if (result.header.state != MRServiceResultState::Current) return result;

	for (const mr::lsp::LspLocation &location : definition.locations) {
		MRServiceLocationTarget target = locationTargetFromLsp(location, result.header.state, result.header.errorMessage);
		if (result.header.state != MRServiceResultState::Current) {
			result.locations.clear();
			return result;
		}
		result.locations.push_back(target);
	}
	return result;
}

MRServiceLocationResult buildServiceReferencesFromLsp(const MRWorkspaceServiceSnapshot &workspace, const std::string &originUri, std::size_t originVersion, const std::string &requestId, const mr::lsp::LspReferencesResult &references) {
	MRServiceLocationResult result;

	result.header.source = MRServiceResultSource::Lsp;
	result.header.kind = MRServiceResultKind::References;
	result.header.requestId = requestId;
	result.header.identity = identityFromUri(workspace, originUri, originVersion, result.header.state, result.header.errorMessage);
	if (result.header.state != MRServiceResultState::Current) return result;

	for (const mr::lsp::LspLocation &location : references.locations) {
		MRServiceLocationTarget target = locationTargetFromLsp(location, result.header.state, result.header.errorMessage);
		if (result.header.state != MRServiceResultState::Current) {
			result.locations.clear();
			return result;
		}
		result.locations.push_back(target);
	}
	return result;
}

MRServiceHoverResult buildServiceHoverFromLsp(const MRWorkspaceServiceSnapshot &workspace, std::size_t originVersion, const std::string &requestId, const mr::lsp::LspHoverResult &hover) {
	MRServiceHoverResult result;

	result.header.source = MRServiceResultSource::Lsp;
	result.header.kind = MRServiceResultKind::Hover;
	result.header.requestId = requestId;
	result.header.identity = identityFromUri(workspace, hover.uri, originVersion, result.header.state, result.header.errorMessage);
	if (result.header.state == MRServiceResultState::Current) {
		result.hover.markupKind = hover.kind;
		result.hover.value = hover.value;
	}
	return result;
}

MRServiceCompletionResult buildServiceCompletionFromLsp(const MRWorkspaceServiceSnapshot &workspace, const std::string &originUri, std::size_t originVersion, const std::string &requestId, const mr::lsp::LspCompletionResult &completion) {
	MRServiceCompletionResult result;

	result.header.source = MRServiceResultSource::Lsp;
	result.header.kind = MRServiceResultKind::Completion;
	result.header.requestId = requestId;
	result.header.identity = identityFromUri(workspace, originUri.empty() ? completion.uri : originUri, originVersion, result.header.state, result.header.errorMessage);
	result.rawLspResponseJson = completion.rawResponseJson;
	if (result.header.state != MRServiceResultState::Current) return result;

	for (const mr::lsp::LspCompletionItem &item : completion.items) {
		MRServiceCompletionItem serviceItem;
		serviceItem.label = item.label;
		serviceItem.hasKind = item.hasKind;
		serviceItem.kind = item.kind;
		serviceItem.detail = item.detail;
		serviceItem.insertText = item.insertText;
		serviceItem.hasInsertTextFormat = item.hasInsertTextFormat;
		serviceItem.insertTextFormat = item.insertTextFormat;
		result.items.push_back(serviceItem);
	}
	return result;
}

MRServiceCodeActionResult buildServiceCodeActionsFromLsp(const MRWorkspaceServiceSnapshot &workspace, const std::string &originUri, std::size_t originVersion, const std::string &requestId, const mr::lsp::LspCodeActionResult &codeActions) {
	MRServiceCodeActionResult result;

	result.header.source = MRServiceResultSource::Lsp;
	result.header.kind = MRServiceResultKind::CodeAction;
	result.header.requestId = requestId;
	result.header.identity = identityFromUri(workspace, originUri.empty() ? codeActions.uri : originUri, originVersion, result.header.state, result.header.errorMessage);
	if (result.header.state != MRServiceResultState::Current) return result;

	for (const mr::lsp::LspCodeActionItem &item : codeActions.items) {
		MRServiceCodeActionItem serviceItem;
		serviceItem.title = item.title;
		serviceItem.kind = item.kind;
		serviceItem.hasEdit = item.hasEdit;
		serviceItem.hasCommand = item.hasCommand;
		serviceItem.rawLspCodeActionJson = item.rawJson;
		for (const mr::lsp::LspCodeActionTextEdit &edit : item.edits) {
			MRServiceTextEdit serviceEdit;
			std::string path;
			std::string uriError;

			serviceEdit.uri = edit.uri;
			if (mr::lsp::fileUriToPath(edit.uri, path, uriError)) serviceEdit.path = normalizeWorkspaceServicePath(path);
			serviceEdit.range.start = servicePositionFromLsp(edit.range.start);
			serviceEdit.range.end = servicePositionFromLsp(edit.range.end);
			serviceEdit.newText = edit.newText;
			serviceItem.edits.push_back(serviceEdit);
		}
		result.items.push_back(serviceItem);
	}
	return result;
}

} // namespace mr::services
