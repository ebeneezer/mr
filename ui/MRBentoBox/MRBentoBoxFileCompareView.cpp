#include "MRBentoBox.hpp"

#include "../../app/commands/MRWindowCommands.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

bool fileCompareRoleIsDiff(MRBentoPaneRole role) noexcept {
	return role == bprDiffOriginal || role == bprDiffCompare;
}

MRBentoPaneRole fileCompareOppositeRole(MRBentoPaneRole role) noexcept {
	return role == bprDiffOriginal ? bprDiffCompare : bprDiffOriginal;
}

const char *fileComparePaneTitle(MRBentoPaneRole role) noexcept {
	switch (role) {
		case bprDiffOriginal:
			return "Diff Original";
		case bprDiffCompare:
			return "Diff Compare";
		default:
			return "Pane";
	}
}

struct FileCompareGutterSettings {
	std::string leading;
	std::string trailing;
};

bool fileCompareGuttersContain(const std::string &gutters, char marker) noexcept {
	for (char ch : gutters)
		if (static_cast<char>(std::toupper(static_cast<unsigned char>(ch))) == marker) return true;
	return false;
}

FileCompareGutterSettings fileCompareGutterSettingsForRole(MRBentoPaneRole role) {
	FileCompareGutterSettings settings;

	switch (role) {
		case bprDiffOriginal:
			settings.leading = configuredFileCompareOriginalLeadingGutters();
			settings.trailing = configuredFileCompareOriginalTrailingGutters();
			break;
		case bprDiffCompare:
			settings.leading = configuredFileCompareCompareLeadingGutters();
			settings.trailing = configuredFileCompareCompareTrailingGutters();
			break;
		default:
			break;
	}
	return settings;
}

void configureFileCompareEditorGutters(MRFileEditor &editor, MRBentoPaneRole role) {
	const FileCompareGutterSettings gutters = fileCompareGutterSettingsForRole(role);
	const bool miniMapConfigured = fileCompareGuttersContain(gutters.leading, 'M') ||
	                               fileCompareGuttersContain(gutters.trailing, 'M');

	editor.setMiniMapSuppressed(!miniMapConfigured);
	editor.setFileCompareGutters(gutters.leading, gutters.trailing);
	editor.setFileCompareGutterVisible(true);
}

void applyFileCompareEditorGutters(
	MRFileEditor &editor,
	const std::shared_ptr<const std::vector<unsigned char>> &lineKinds,
	const std::shared_ptr<const std::vector<MRFileCompareMiniMapSlice>> &miniMapSlices,
	const char *warmupReason) {
	editor.adoptFileCompareLineKinds(lineKinds, miniMapSlices);
	editor.updateMetrics();
	editor.continueComputeWarmupIfNeeded(warmupReason);
}

std::string diffDisplayTitle(const MRBentoCompareSource &source, MRBentoPaneRole role) {
	return source.title.empty() ? std::string(fileComparePaneTitle(role)) : source.title;
}

} // namespace

void MRBentoBox::refreshFileComparePane(BentoLeaf &leaf) {
	MREditWindow *targetWindow = leaf.id == 0 ? static_cast<MREditWindow *>(this) : static_cast<MREditWindow *>(leaf.pane);
	MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
	const bool original = leaf.role == bprDiffOriginal;
	std::shared_ptr<const MRBentoFileComparePaneProjectionPayload> projection =
		original ? fileComparePipeline.originalProjection : fileComparePipeline.compareProjection;
	const MRBentoCompareSource &source = original ? fileCompareSetup.original : fileCompareSetup.compare;
	const std::string title = diffDisplayTitle(source, leaf.role);

	if (targetWindow == nullptr || targetEditor == nullptr || !fileCompareRoleIsDiff(leaf.role)) return;
	if (projection != nullptr && projection->editable != fileComparePanesEditable()) projection.reset();
	leaf.spec = paneSpecForRole(leaf.role);
	if (leaf.id != 0 && leaf.pane != nullptr) leaf.pane->setPaneSpec(leaf.spec, getEditor());
	configureFileCompareEditorGutters(*targetEditor, leaf.role);

	if (fileComparePanesEditable()) {
		MREditWindow *sourceWindow = findEditWindowByBufferId(source.bufferId);
		MRFileEditor *sourceEditor = sourceWindow != nullptr ? sourceWindow->getEditor() : nullptr;
		if (sourceEditor != nullptr && targetEditor->documentId() != sourceEditor->documentId())
			targetEditor->shareContentStateFrom(*sourceEditor);
		if (projection != nullptr)
			applyFileCompareEditorGutters(*targetEditor, projection->lineKinds,
			                              projection->miniMapSlices, "file-compare-edit-refresh");
		else
			targetEditor->clearFileCompareLineKinds();
		targetWindow->setDisplayTitle(title.c_str());
		targetWindow->setReadOnly(false);
	} else {
		targetEditor->detachContentStateCopy();
		targetWindow->setCurrentFileName(nullptr);
		if (projection != nullptr) {
			std::size_t &targetDocumentId = original ? fileComparePipeline.originalTargetDocumentId : fileComparePipeline.compareTargetDocumentId;
			std::size_t &targetVersion = original ? fileComparePipeline.originalTargetVersion : fileComparePipeline.compareTargetVersion;
			if (targetDocumentId != targetWindow->documentId() || targetVersion != targetWindow->documentVersion()) {
				const std::size_t expectedDocumentId = targetWindow->documentId();
				const std::size_t expectedVersion = targetWindow->documentVersion();
				if (targetWindow->adoptReadOnlyProjectionText(projection->text, expectedDocumentId, expectedVersion, title.c_str())) {
					targetDocumentId = targetWindow->documentId();
					targetVersion = targetWindow->documentVersion();
				}
			}
			applyFileCompareEditorGutters(*targetEditor, projection->lineKinds,
			                              projection->miniMapSlices, "file-compare-refresh");
		} else
			targetEditor->clearFileCompareLineKinds();
		targetWindow->setReadOnly(true);
		targetWindow->setFileChanged(false);
	}
	if (leaf.id != 0 && leaf.pane != nullptr) leaf.pane->layoutPaneChrome();
	leaf.title = fileComparePaneTitle(leaf.role);
}

void MRBentoBox::refreshFileComparePanes() {
	if (bentoMode != bbmFileCompare) return;
	const bool adoptionWasActive = bentoProjectionAdoptionActive;
	bentoProjectionAdoptionActive = true;
	for (BentoLeaf &leaf : leaves)
		if (leaf.visible) refreshFileComparePane(leaf);
	bentoProjectionAdoptionActive = adoptionWasActive;
	updateActivePaneFrame();
	bentoProjectionDirty |= bpdContent | bpdChrome | bpdScrollBar | bpdOverlay;
	flushBentoProjection();
}

void MRBentoBox::syncFileCompareLinkedPaneFrom(int sourceLeafId, bool syncCursor) {
	if (bentoMode != bbmFileCompare) return;

	const MRBentoPaneRole sourceRole = roleForLeaf(sourceLeafId);
	if (!fileCompareRoleIsDiff(sourceRole)) return;
	const MRBentoPaneRole targetRole = fileCompareOppositeRole(sourceRole);
	const int targetLeafId = leafIdForRole(targetRole);
	if (targetLeafId < 0 || targetLeafId == sourceLeafId) return;

	MREditWindow *sourceWindow = sourceLeafId == 0 ? static_cast<MREditWindow *>(this) : static_cast<MREditWindow *>(paneWindowForLeaf(sourceLeafId));
	MREditWindow *targetWindow = targetLeafId == 0 ? static_cast<MREditWindow *>(this) : static_cast<MREditWindow *>(paneWindowForLeaf(targetLeafId));
	MRFileEditor *sourceEditor = sourceWindow != nullptr ? sourceWindow->getEditor() : nullptr;
	MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
	if (sourceEditor == nullptr || targetEditor == nullptr) return;
	auto mappedTargetLine = [this, sourceRole, targetEditor](std::size_t sourceLine) {
		return fileCompareMappedLineForRole(sourceRole, sourceLine, *targetEditor, fileComparePanesEditable());
	};

	if (syncCursor) {
		const std::size_t sourceLine = sourceEditor->lineIndexOfOffset(sourceEditor->cursorOffset());
		const std::size_t targetLine = mappedTargetLine(sourceLine);
		const int visualColumn = sourceEditor->displayedCursorColumn();
		const std::size_t targetLineStart = targetEditor->bufferModel().lineStartByIndex(targetLine);
		const std::size_t targetOffset = targetEditor->charPtrOffset(targetLineStart, visualColumn);

		targetEditor->setCursorOffsetAtVisualColumn(targetOffset, visualColumn);
	}
	{
		const std::size_t sourceScrollLine = static_cast<std::size_t>(std::max(0, sourceEditor->delta.y));
		const std::size_t targetScrollLine = mappedTargetLine(sourceScrollLine);
		const int targetScrollY = static_cast<int>(std::min(targetScrollLine, static_cast<std::size_t>(std::numeric_limits<int>::max())));

		targetEditor->scrollTo(std::max(0, sourceEditor->delta.x), targetScrollY);
	}
	targetEditor->refreshViewState();
	if (targetWindow != nullptr) targetWindow->drawView();
}
