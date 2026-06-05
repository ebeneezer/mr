#include "MRBentoBox.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <utility>

namespace {

static constexpr auto kOutlinePaneRefreshInterval = std::chrono::milliseconds(250);

MROutlineView outlineViewForBentoRole(MRBentoPaneRole role) noexcept {
	return role == bprFunctions ? mrovFunctions : mrovStructure;
}

const char *outlinePaneRoleTitle(MRBentoPaneRole role) noexcept {
	return role == bprFunctions ? "Functions" : "Structure";
}

const char *outlineKindLabel(MROutlineKind kind) noexcept {
	switch (kind) {
		case mrokModule:
			return "mod";
		case mrokNamespace:
			return "ns";
		case mrokClass:
			return "type";
		case mrokMethod:
			return "meth";
		case mrokFunction:
			return "func";
		case mrokSection:
			return "sec";
		case mrokMacro:
			return "macro";
		case mrokTarget:
			return "target";
		case mrokBlock:
			return "block";
		default:
			return "sym";
	}
}

std::uint64_t outlineTextHash(const std::string &text) noexcept {
	std::uint64_t hash = 1469598103934665603ULL;

	for (char ch : text) {
		hash ^= static_cast<unsigned char>(ch);
		hash *= 1099511628211ULL;
	}
	return hash;
}

std::string outlineCoverageStatus(const MROutlineSnapshot &snapshot, std::size_t itemCount) {
	char rangeText[64];
	std::string status = std::to_string(itemCount) + (itemCount == 1 ? " item" : " items");

	if (snapshot.complete) return status + ", full";
	if (snapshot.bottomLine <= snapshot.topLine) return status;
	std::snprintf(rangeText, sizeof(rangeText), "lines %zu-%zu", snapshot.topLine + 1, snapshot.bottomLine);
	return status + ", " + rangeText;
}

} // namespace

void MRBentoBox::refreshOutlinePanes(bool force) {
	static_cast<void>(refreshOutlinePane(bprStructure, force));
	static_cast<void>(refreshOutlinePane(bprFunctions, force));
}

bool MRBentoBox::refreshOutlinePane(MRBentoPaneRole role, bool force) {
	MREditWindow *outlineWindow = nullptr;
	MRFileEditor *sourceEditor = getEditor();
	MRBentoOutlinePaneState *state = nullptr;
	std::vector<MRBentoOutlineEntry> *entries = nullptr;
	MROutlineSnapshot snapshot;
	std::vector<MRBentoOutlineEntry> nextEntries;
	std::string text;
	MRBentoOutlinePaneState nextState;
	std::string *status = nullptr;
	std::string nextStatus;
	MROutlineRequest request;
	bool completeWarmupRequested = false;
	bool ready = false;

	switch (role) {
		case bprStructure:
			outlineWindow = structurePane();
			state = &structureOutlineState;
			entries = &structureOutlineEntries;
			status = &structureOutlineStatus;
			break;
		case bprFunctions:
			outlineWindow = functionsPane();
			state = &functionsOutlineState;
			entries = &functionsOutlineEntries;
			status = &functionsOutlineStatus;
			break;
		default:
			return false;
	}
	if (outlineWindow == nullptr || sourceEditor == nullptr || state == nullptr || entries == nullptr || status == nullptr) return false;
	const std::size_t currentDocumentId = sourceEditor->documentId();
	const std::size_t currentVersion = sourceEditor->documentVersion();
	const auto now = std::chrono::steady_clock::now();
	if (!force && state->documentId == currentDocumentId && state->version == currentVersion) {
		if (state->complete) return true;
		if (state->lastRefresh != std::chrono::steady_clock::time_point() && now - state->lastRefresh < kOutlinePaneRefreshInterval) return true;
	}

	request.view = outlineViewForBentoRole(role);
	request.allowPartial = true;
	if (force) completeWarmupRequested = sourceEditor->requestCompleteFoldOutlineWarmup();
	ready = sourceEditor->buildFoldOutlineSnapshot(request, snapshot);
	if (!ready) {
		text = role == bprFunctions ? "Functions warming...\n" : "Structure warming...\n";
		nextStatus = completeWarmupRequested ? "warming full" : "warming";
		nextState.documentId = sourceEditor->documentId();
		nextState.version = sourceEditor->documentVersion();
		nextState.textHash = outlineTextHash(text);
		nextState.complete = false;
	} else if (snapshot.nodes.empty()) {
		text = role == bprFunctions ? "No functions available from MR outline.\n" : "No structure available from MR outline.\n";
		nextStatus = outlineCoverageStatus(snapshot, 0);
		nextState.documentId = snapshot.documentId;
		nextState.version = snapshot.version;
		nextState.textHash = outlineTextHash(text);
		nextState.complete = snapshot.complete;
	} else {
		for (std::size_t i = 0; i < snapshot.nodes.size(); ++i) {
			const MROutlineNode &node = snapshot.nodes[i];
			int depth = 0;
			std::uint32_t parent = node.parent;
			char lineNumber[32];
			MRBentoOutlineEntry entry;

			while (parent != MROutlineNode::npos && parent < snapshot.nodes.size() && depth < 20) {
				++depth;
				parent = snapshot.nodes[parent].parent;
			}
			entry.paneOffset = text.size();
			entry.sourceOffset = node.selectionRange.start.offset;
			entry.sourceSelectionEnd = sourceEditor->lineEndOffset(entry.sourceOffset);
			if (entry.sourceSelectionEnd < entry.sourceOffset) entry.sourceSelectionEnd = entry.sourceOffset;
			nextEntries.push_back(entry);

			std::snprintf(lineNumber, sizeof(lineNumber), "%6zu  ", node.selectionRange.start.line + 1);
			text.append(lineNumber);
			for (int level = 0; level < depth; ++level)
				text.append("  ");
			text.append(outlineKindLabel(node.kind));
			text.append("  ");
			if (node.nameOffset < snapshot.textPool.size()) text.append(snapshot.textPool.data() + node.nameOffset, std::min<std::size_t>(node.nameLength, snapshot.textPool.size() - node.nameOffset));
			if (node.confidence == mrocHeuristic) text.append("  ?");
			text.push_back('\n');
		}
		nextStatus = outlineCoverageStatus(snapshot, nextEntries.size());
		nextState.documentId = snapshot.documentId;
		nextState.version = snapshot.version;
		nextState.textHash = outlineTextHash(text);
		nextState.complete = snapshot.complete;
	}
	nextState.lastRefresh = now;

	*entries = std::move(nextEntries);
	if (!force && state->documentId == nextState.documentId && state->version == nextState.version && state->textHash == nextState.textHash && *status == nextStatus) {
		state->complete = nextState.complete;
		state->lastRefresh = nextState.lastRefresh;
		return true;
	}
	*state = nextState;
	*status = nextStatus;
	static_cast<void>(outlineWindow->replaceTextBuffer(text.c_str(), outlinePaneRoleTitle(role)));
	outlineWindow->setReadOnly(true);
	outlineWindow->setFileChanged(false);
	return true;
}

bool MRBentoBox::jumpToOutlineAtCursor(MRBentoPaneRole role) {
	MREditWindow *outlineWindow = nullptr;
	MRFileEditor *sourceEditor = getEditor();
	MRFileEditor *outlineEditor = nullptr;
	const std::vector<MRBentoOutlineEntry> *entries = nullptr;
	std::size_t cursorOffset = 0;
	const MRBentoOutlineEntry *selected = nullptr;

	switch (role) {
		case bprStructure:
			outlineWindow = structurePane();
			entries = &structureOutlineEntries;
			break;
		case bprFunctions:
			outlineWindow = functionsPane();
			entries = &functionsOutlineEntries;
			break;
		default:
			return false;
	}
	outlineEditor = outlineWindow != nullptr ? outlineWindow->getEditor() : nullptr;
	if (sourceEditor == nullptr || outlineEditor == nullptr || entries == nullptr) return false;
	cursorOffset = outlineEditor->cursorOffset();
	for (const MRBentoOutlineEntry &entry : *entries) {
		const std::size_t lineEnd = outlineEditor->lineEndOffset(entry.paneOffset);
		if (cursorOffset >= entry.paneOffset && cursorOffset <= lineEnd) {
			selected = &entry;
			break;
		}
	}
	if (selected == nullptr) return false;
	outlineEditor->setCursorOffset(selected->paneOffset);
	outlineEditor->setSelectionOffsets(selected->paneOffset, outlineEditor->lineEndOffset(selected->paneOffset));
	sourceEditor->setCursorOffset(selected->sourceOffset);
	sourceEditor->setSelectionOffsets(selected->sourceOffset, selected->sourceSelectionEnd);
	sourceEditor->revealCursor(True);
	activatePrimaryPane();
	return true;
}
