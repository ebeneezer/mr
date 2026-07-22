#include "MRBentoBox.hpp"

#include "../../app/commands/MRWindowCommands.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../coprocessor/MRCoprocessor.hpp"
#include "../MRWindowSupport.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <utility>
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

std::string fileCompareJoinedLineRange(const std::vector<std::string> &lines, std::size_t startLine, std::size_t lineCount, bool prefixNewline, bool suffixNewline) {
	std::string text;

	if (lineCount == 0) return text;
	if (prefixNewline) text.push_back('\n');
	for (std::size_t i = 0; i < lineCount && startLine + i < lines.size(); ++i) {
		if (i != 0) text.push_back('\n');
		text += lines[startLine + i];
	}
	if (suffixNewline) text.push_back('\n');
	return text;
}

bool fileCompareEditorLineRange(const MRFileEditor &editor, std::size_t startLine, std::size_t lineCount, std::size_t &rangeStart, std::size_t &rangeEnd) noexcept {
	const MRTextBufferModel &model = editor.bufferModel();
	const std::size_t editorLineCount = model.lineCount();

	rangeStart = startLine < editorLineCount ? model.lineStartByIndex(startLine) : model.length();
	if (lineCount == 0) {
		rangeEnd = rangeStart;
		return true;
	}
	const std::size_t endLine = startLine + lineCount;
	rangeEnd = endLine < editorLineCount ? model.lineStartByIndex(endLine) : model.length();
	return rangeEnd >= rangeStart;
}

std::size_t mappedFileCompareLineForRole(const std::vector<mr::diff::MRDiffHunk> &hunks, MRBentoPaneRole sourceRole, std::size_t sourceLine) noexcept {
	for (const mr::diff::MRDiffHunk &hunk : hunks) {
		switch (hunk.op) {
			case mr::diff::MRDiffOp::Equal:
				if (sourceRole == bprDiffOriginal && sourceLine >= hunk.leftStart && sourceLine < hunk.leftStart + hunk.count) return hunk.rightStart + (sourceLine - hunk.leftStart);
				if (sourceRole == bprDiffCompare && sourceLine >= hunk.rightStart && sourceLine < hunk.rightStart + hunk.count) return hunk.leftStart + (sourceLine - hunk.rightStart);
				break;
			case mr::diff::MRDiffOp::Delete:
				if (sourceRole == bprDiffOriginal && sourceLine >= hunk.leftStart && sourceLine < hunk.leftStart + hunk.count) return hunk.rightStart;
				break;
			case mr::diff::MRDiffOp::Insert:
				if (sourceRole == bprDiffCompare && sourceLine >= hunk.rightStart && sourceLine < hunk.rightStart + hunk.count) return hunk.leftStart;
				break;
			default:
				break;
		}
	}
	return sourceLine;
}

void hideFileCompareSourceWindow(const MRBentoCompareSource &source) {
	MREditWindow *window = findEditWindowByBufferId(source.bufferId);

	if (window == nullptr) return;
	setWindowManuallyHidden(window, true);
	window->hide();
}

bool fileCompareSourceStillMatches(const MRBentoCompareSource &source) {
	MREditWindow *window = findEditWindowByBufferId(source.bufferId);

	return window != nullptr && window->documentId() == source.documentId && window->documentVersion() == source.version;
}

} // namespace

bool MRBentoBox::initializeFileCompare(MRBentoCompareSetup setup) {
	if (bentoMode != bbmFileCompare) return false;

	const bool compareFirst = configuredFileCompareStartConfiguration() == MRFileCompareStartConfiguration::CompareOriginal;
	const MRBentoPaneRole primaryRole = compareFirst ? bprDiffCompare : bprDiffOriginal;
	const MRBentoPaneRole secondaryRole = compareFirst ? bprDiffOriginal : bprDiffCompare;
	cancelFileComparePipeline();
	fileCompareSetup = std::move(setup);
	fileComparePipeline = MRBentoFileComparePipelineState();
	fileCompareSourcesRestored = false;
	fileCompareDiffReady = false;
	fileCompareStale = true;

	for (BentoLeaf &leaf : leaves) {
		if (leaf.id == 0) {
			leaf.role = primaryRole;
			leaf.spec = paneSpecForRole(primaryRole);
			leaf.title = fileComparePaneTitle(primaryRole);
		}
	}
	if (!loadTextBuffer("Comparing...\n", fileComparePaneTitle(primaryRole))) return false;
	setReadOnly(true);
	setFileChanged(false);
	if (leafIdForRole(secondaryRole) < 0) {
		if (splitLeafNode(0, bsoVertical, secondaryRole) < 0) return false;
	}
	refreshFileComparePanes();
	hideFileCompareSourceWindow(fileCompareSetup.original);
	hideFileCompareSourceWindow(fileCompareSetup.compare);
	static_cast<void>(mrActivateEditWindow(this));
	return true;
}

bool MRBentoBox::startFileCompareProjection() {
	if (bentoMode != bbmFileCompare) return false;
	MREditWindow *originalWindow = findEditWindowByBufferId(fileCompareSetup.original.bufferId);
	MREditWindow *compareWindow = findEditWindowByBufferId(fileCompareSetup.compare.bufferId);
	if (originalWindow == nullptr || compareWindow == nullptr || originalWindow->getEditor() == nullptr || compareWindow->getEditor() == nullptr)
		return false;

	refreshFileCompareCachedSnapshots(bprSource, false);
	cancelFileComparePipeline();
	if (fileComparePipeline.generationCounter == 0) fileComparePipeline.generationCounter = 1;
	fileComparePipeline.activeGeneration = fileComparePipeline.generationCounter++;
	fileCompareDiffReady = false;
	fileCompareStale = true;
	if (!submitFileCompareAcquisition(true) || !submitFileCompareAcquisition(false)) {
		cancelFileComparePipeline();
		return false;
	}
	refreshFileComparePanes();
	return true;
}

bool MRBentoBox::isFileCompareBox() const noexcept {
	return bentoMode == bbmFileCompare;
}

bool MRBentoBox::fileCompareWorkspaceSourcePaths(std::string &originalPath, std::string &comparePath) const {
	MREditWindow *originalWindow = nullptr;
	MREditWindow *compareWindow = nullptr;
	MRFileEditor *originalEditor = nullptr;
	MRFileEditor *compareEditor = nullptr;

	originalPath.clear();
	comparePath.clear();
	if (bentoMode != bbmFileCompare) return false;

	originalWindow = findEditWindowByBufferId(fileCompareSetup.original.bufferId);
	compareWindow = findEditWindowByBufferId(fileCompareSetup.compare.bufferId);
	originalEditor = originalWindow != nullptr ? originalWindow->getEditor() : nullptr;
	compareEditor = compareWindow != nullptr ? compareWindow->getEditor() : nullptr;
	if (originalEditor == nullptr || compareEditor == nullptr) return false;
	originalPath = originalEditor->persistentFileName();
	comparePath = compareEditor->persistentFileName();
	return !originalPath.empty() && !comparePath.empty();
}

bool MRBentoBox::containsFileCompareSourceWindow(const MREditWindow *window) const noexcept {
	if (bentoMode != bbmFileCompare || window == nullptr) return false;
	return window->bufferId() == fileCompareSetup.original.bufferId || window->bufferId() == fileCompareSetup.compare.bufferId;
}

bool MRBentoBox::refreshFileCompareAfterEditorMutation(const MREditWindow *window) {
	if (bentoMode != bbmFileCompare || window == nullptr || !fileComparePanesEditable()) return false;
	for (const BentoLeaf &leaf : leaves) {
		if (!leaf.visible || !fileCompareRoleIsDiff(leaf.role)) continue;
		if ((leaf.id == 0 && window == this) || (leaf.id != 0 && window == leaf.pane)) {
			refreshFileCompareAfterSourceMutation(leaf.role);
			return true;
		}
	}
	if (window->bufferId() == fileCompareSetup.original.bufferId) {
		refreshFileCompareAfterSourceMutation(bprDiffOriginal);
		return true;
	}
	if (window->bufferId() == fileCompareSetup.compare.bufferId) {
		refreshFileCompareAfterSourceMutation(bprDiffCompare);
		return true;
	}
	return false;
}

bool MRBentoBox::fileComparePanesEditable() const noexcept {
	return bentoMode == bbmFileCompare && !configuredFileCompareComparePanelReadOnly();
}

void MRBentoBox::refreshFileCompareSourceSnapshot(MRBentoCompareSource &source, MREditWindow *window, bool force) {
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	const std::size_t nextDocumentId = window != nullptr ? window->documentId() : 0;
	const std::size_t nextVersion = window != nullptr ? window->documentVersion() : 0;

	if (window == nullptr || editor == nullptr) return;
	source.window = window;
	source.bufferId = window->bufferId();
	if (const char *title = window->getTitle(0); title != nullptr && *title != '\0') source.title = title;
	if (!force && source.documentId == nextDocumentId && source.version == nextVersion) return;
	source.documentId = nextDocumentId;
	source.version = nextVersion;
	source.snapshot = editor->readSnapshot();
}

void MRBentoBox::refreshFileCompareCachedSnapshots(MRBentoPaneRole changedRole, bool force) {
	MREditWindow *originalWindow = findEditWindowByBufferId(fileCompareSetup.original.bufferId);
	MREditWindow *compareWindow = findEditWindowByBufferId(fileCompareSetup.compare.bufferId);

	if (force || changedRole == bprSource || changedRole == bprDiffOriginal) refreshFileCompareSourceSnapshot(fileCompareSetup.original, originalWindow, force);
	if (force || changedRole == bprSource || changedRole == bprDiffCompare) refreshFileCompareSourceSnapshot(fileCompareSetup.compare, compareWindow, force);
}

MREditWindow *MRBentoBox::fileComparePaneWindow(bool original) const noexcept {
	const int leafId = leafIdForRole(original ? bprDiffOriginal : bprDiffCompare);
	if (leafId == 0) return const_cast<MRBentoBox *>(this);
	return leafId > 0 ? paneWindowForLeaf(leafId) : nullptr;
}

void MRBentoBox::cancelFileComparePipeline() noexcept {
	std::uint64_t *taskIds[] = {
		&fileComparePipeline.originalAcquisitionTaskId,
		&fileComparePipeline.compareAcquisitionTaskId,
		&fileComparePipeline.diffTaskId,
		&fileComparePipeline.originalProjectionTaskId,
		&fileComparePipeline.compareProjectionTaskId
	};
	for (std::uint64_t *taskId : taskIds) {
		if (*taskId == 0) continue;
		static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(*taskId));
		releaseCoprocessorTask(*taskId);
		*taskId = 0;
	}
	fileComparePipeline.activeGeneration = 0;
	fileComparePipeline.originalAcquisition.reset();
	fileComparePipeline.compareAcquisition.reset();
	fileComparePipeline.diff.reset();
}

bool MRBentoBox::submitFileCompareAcquisition(bool original) {
	MRBentoCompareSource &source = original ? fileCompareSetup.original : fileCompareSetup.compare;
	MREditWindow *paneWindow = fileComparePaneWindow(original);
	if (paneWindow == nullptr || fileComparePipeline.activeGeneration == 0 ||
	    source.snapshot.documentId() != source.documentId || source.snapshot.version() != source.version)
		return false;
	const MRTextBufferModel::ReadSnapshot &snapshot = source.snapshot;
	const std::uint64_t generation = fileComparePipeline.activeGeneration;
	const char *label = original ? "file compare acquire original" : "file compare acquire compare";
	const std::uint64_t taskId = mr::coprocessor::globalCoprocessor().submitPacket(
		mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::FileCompare,
		source.documentId, source.version, mr::coprocessor::ExecutionOwnerKind::BentoPane,
		static_cast<std::size_t>(paneWindow->bufferId()), generation, mr::coprocessor::WorkDirection::None,
		0, static_cast<std::uint64_t>(snapshot.length()), label,
		[snapshot, generation, original](const mr::coprocessor::TaskInfo &task) {
			mr::coprocessor::Result result;
			result.task = task;
			result.payload = mrBuildBentoFileCompareAcquisition(snapshot, generation, original, task.cancelFlag.get());
			if (result.payload != nullptr)
				result.status = mr::coprocessor::TaskStatus::Completed;
			else if (task.cancelRequested())
				result.status = mr::coprocessor::TaskStatus::Cancelled;
			else {
				result.status = mr::coprocessor::TaskStatus::Failed;
				result.error = "file compare acquisition failed";
			}
			return result;
		});
	if (taskId == 0) return false;
	if (original)
		fileComparePipeline.originalAcquisitionTaskId = taskId;
	else
		fileComparePipeline.compareAcquisitionTaskId = taskId;
	trackCoprocessorTask(taskId, mr::coprocessor::TaskKind::FileCompare, label);
	return true;
}

bool MRBentoBox::submitFileCompareDiff() {
	if (fileComparePipeline.activeGeneration == 0 || fileComparePipeline.originalAcquisition == nullptr ||
	    fileComparePipeline.compareAcquisition == nullptr || fileComparePipeline.diffTaskId != 0)
		return false;
	MREditWindow *ownerWindow = fileComparePaneWindow(true);
	if (ownerWindow == nullptr) return false;
	const std::shared_ptr<const MRBentoFileCompareAcquisitionPayload> original = fileComparePipeline.originalAcquisition;
	const std::shared_ptr<const MRBentoFileCompareAcquisitionPayload> compare = fileComparePipeline.compareAcquisition;
	const std::uint64_t generation = fileComparePipeline.activeGeneration;
	const std::uint64_t lineCount = static_cast<std::uint64_t>(original->lines->size() + compare->lines->size());
	const char *label = "file compare ordered diff";
	const std::uint64_t taskId = mr::coprocessor::globalCoprocessor().submitPacket(
		mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::FileCompare,
		original->documentId, original->version, mr::coprocessor::ExecutionOwnerKind::BentoPane,
		static_cast<std::size_t>(ownerWindow->bufferId()), generation, mr::coprocessor::WorkDirection::None,
		0, lineCount, label, [original, compare](const mr::coprocessor::TaskInfo &task) {
			mr::coprocessor::Result result;
			std::string errorText;
			result.task = task;
			result.payload = mrBuildBentoFileCompareDiff(original, compare, task.cancelFlag.get(), errorText);
			if (result.payload != nullptr)
				result.status = mr::coprocessor::TaskStatus::Completed;
			else if (task.cancelRequested())
				result.status = mr::coprocessor::TaskStatus::Cancelled;
			else {
				result.status = mr::coprocessor::TaskStatus::Failed;
				result.error = errorText.empty() ? "file compare diff failed" : errorText;
			}
			return result;
		});
	if (taskId == 0) return false;
	fileComparePipeline.diffTaskId = taskId;
	trackCoprocessorTask(taskId, mr::coprocessor::TaskKind::FileCompare, label);
	return true;
}

bool MRBentoBox::submitFileComparePaneProjection(bool original) {
	if (fileComparePipeline.activeGeneration == 0 || fileComparePipeline.diff == nullptr) return false;
	MREditWindow *paneWindow = fileComparePaneWindow(original);
	if (paneWindow == nullptr) return false;
	const std::shared_ptr<const MRBentoFileCompareDiffPayload> diff = fileComparePipeline.diff;
	const bool editable = fileComparePanesEditable();
	const char *label = original ? "file compare project original" : "file compare project compare";
	const std::uint64_t lineCount = static_cast<std::uint64_t>(original ? diff->originalLineCount : diff->compareLineCount);
	const std::uint64_t taskId = mr::coprocessor::globalCoprocessor().submitPacket(
		mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::FileCompare,
		original ? diff->originalDocumentId : diff->compareDocumentId,
		original ? diff->originalBaseVersion : diff->compareBaseVersion,
		mr::coprocessor::ExecutionOwnerKind::BentoPane, static_cast<std::size_t>(paneWindow->bufferId()),
		diff->generation, mr::coprocessor::WorkDirection::None, 0, lineCount, label,
		[diff, original, editable](const mr::coprocessor::TaskInfo &task) {
			mr::coprocessor::Result result;
			result.task = task;
			result.payload = mrBuildBentoFileComparePaneProjection(diff, original, editable, task.cancelFlag.get());
			if (result.payload != nullptr)
				result.status = mr::coprocessor::TaskStatus::Completed;
			else if (task.cancelRequested())
				result.status = mr::coprocessor::TaskStatus::Cancelled;
			else {
				result.status = mr::coprocessor::TaskStatus::Failed;
				result.error = "file compare pane projection failed";
			}
			return result;
		});
	if (taskId == 0) return false;
	if (original)
		fileComparePipeline.originalProjectionTaskId = taskId;
	else
		fileComparePipeline.compareProjectionTaskId = taskId;
	trackCoprocessorTask(taskId, mr::coprocessor::TaskKind::FileCompare, label);
	return true;
}

void MRBentoBox::refreshFileCompareAfterSourceMutation(MRBentoPaneRole changedRole) {
	if (!fileComparePanesEditable()) return;
	refreshFileCompareCachedSnapshots(changedRole, false);
	static_cast<void>(startFileCompareProjection());
}

void MRBentoBox::refreshFileCompareConfiguration() {
	if (bentoMode != bbmFileCompare) return;
	const bool editable = fileComparePanesEditable();
	if ((fileComparePipeline.originalProjection != nullptr && fileComparePipeline.originalProjection->editable != editable) ||
	    (fileComparePipeline.compareProjection != nullptr && fileComparePipeline.compareProjection->editable != editable)) {
		static_cast<void>(startFileCompareProjection());
		return;
	}
	refreshFileComparePanes();
}

void MRBentoBox::restoreFileCompareSources() noexcept {
	if (bentoMode != bbmFileCompare || fileCompareSourcesRestored) return;
	fileCompareSourcesRestored = true;

	MREditWindow *originalWindow = findEditWindowByBufferId(fileCompareSetup.original.bufferId);
	if (originalWindow != nullptr) {
		setWindowManuallyHidden(originalWindow, fileCompareSetup.original.wasManuallyHidden);
		if (fileCompareSetup.original.wasVisible && !fileCompareSetup.original.wasManuallyHidden) originalWindow->show();
	}
	MREditWindow *compareWindow = findEditWindowByBufferId(fileCompareSetup.compare.bufferId);
	if (compareWindow != nullptr) {
		setWindowManuallyHidden(compareWindow, fileCompareSetup.compare.wasManuallyHidden);
		if (fileCompareSetup.compare.wasVisible && !fileCompareSetup.compare.wasManuallyHidden) compareWindow->show();
	}
}

std::size_t MRBentoBox::fileCompareGroupStartLineForRole(const MRBentoFileCompareChangeGroup &group, MRBentoPaneRole role, bool editablePanes) const noexcept {
	if (!editablePanes) return group.displayStartLine;
	switch (role) {
		case bprDiffOriginal:
			return group.originalStartLine;
		case bprDiffCompare:
			return group.compareStartLine;
		default:
			return group.displayStartLine;
	}
}

std::size_t MRBentoBox::fileCompareGroupLineCountForRole(const MRBentoFileCompareChangeGroup &group, MRBentoPaneRole role, bool editablePanes) const noexcept {
	if (!editablePanes) return std::max<std::size_t>(1, group.displayLineCount);
	switch (role) {
		case bprDiffOriginal:
			return std::max<std::size_t>(1, group.deletedLineCount);
		case bprDiffCompare:
			return std::max<std::size_t>(1, group.insertedLineCount);
		default:
			return std::max<std::size_t>(1, group.displayLineCount);
	}
}

std::size_t MRBentoBox::fileCompareGroupEffectiveLineCountForRole(const MRBentoFileCompareChangeGroup &group, MRBentoPaneRole role, bool editablePanes) const noexcept {
	std::size_t lineCount = fileCompareGroupLineCountForRole(group, role, editablePanes);

	if (!editablePanes) return lineCount;
	switch (role) {
		case bprDiffOriginal:
			if (group.insertedLineCount > group.deletedLineCount) lineCount = std::max<std::size_t>(lineCount, group.deletedLineCount + 1);
			break;
		case bprDiffCompare:
			if (group.deletedLineCount > group.insertedLineCount) lineCount = std::max<std::size_t>(lineCount, group.insertedLineCount + 1);
			break;
		default:
			break;
	}
	return lineCount;
}

std::size_t MRBentoBox::fileCompareGroupNavigationLineForRole(const MRBentoFileCompareChangeGroup &group, MRBentoPaneRole role, const MRFileEditor &editor, bool editablePanes) const {
	const std::size_t documentLineCount = std::max<std::size_t>(1, editor.bufferModel().lineCount());
	std::size_t targetLine = fileCompareGroupStartLineForRole(group, role, editablePanes);
	if (!editablePanes || !fileCompareRoleIsDiff(role)) return std::min(targetLine, documentLineCount - 1);

	const std::shared_ptr<const MRBentoFileComparePaneProjectionPayload> &projection =
		role == bprDiffOriginal ? fileComparePipeline.originalProjection : fileComparePipeline.compareProjection;
	if (projection == nullptr || projection->lineKinds == nullptr || projection->lineKinds->empty())
		return std::min(targetLine, documentLineCount - 1);
	const std::vector<unsigned char> &lineKinds = *projection->lineKinds;

	const std::size_t lineLimit = std::min(lineKinds.size(), documentLineCount);
	if (lineLimit == 0) return 0;
	if (targetLine >= lineLimit) targetLine = lineLimit - 1;

	const std::size_t groupLineCount = fileCompareGroupEffectiveLineCountForRole(group, role, editablePanes);
	const std::size_t scanEndLine = std::min(lineLimit, targetLine + std::max<std::size_t>(1, groupLineCount) + 1);
	for (std::size_t line = targetLine; line < scanEndLine; ++line)
		if (lineKinds[line] != mrfclkEqual && lineKinds[line] != mrfclkNone) return line;
	return targetLine;
}

std::size_t MRBentoBox::fileCompareMappedLineForRole(MRBentoPaneRole sourceRole, std::size_t sourceLine, const MRFileEditor &targetEditor, bool editablePanes) const noexcept {
	std::size_t targetLine = sourceLine;
	if (!fileCompareRoleIsDiff(sourceRole)) return targetLine;
	const MRBentoPaneRole targetRole = fileCompareOppositeRole(sourceRole);

	if (editablePanes && fileCompareDiffReady && fileComparePipeline.diff != nullptr && fileComparePipeline.diff->changeGroups != nullptr) {
		bool mappedInChangeGroup = false;
		for (const MRBentoFileCompareChangeGroup &group : *fileComparePipeline.diff->changeGroups) {
			const std::size_t sourceStart = fileCompareGroupStartLineForRole(group, sourceRole, true);
			const std::size_t sourceLineCount = fileCompareGroupEffectiveLineCountForRole(group, sourceRole, true);
			if (sourceLine < sourceStart || sourceLine >= sourceStart + sourceLineCount) continue;

			const std::size_t targetStart = fileCompareGroupStartLineForRole(group, targetRole, true);
			const std::size_t targetLineCount = targetRole == bprDiffOriginal ? group.deletedLineCount : group.insertedLineCount;
			mappedInChangeGroup = true;
			if (targetLineCount == 0) {
				targetLine = targetStart;
				break;
			}
			const std::size_t relativeLine = sourceLine - sourceStart;
			targetLine = targetStart + std::min(relativeLine, targetLineCount - 1);
			break;
		}
		if (!mappedInChangeGroup) targetLine = mappedFileCompareLineForRole(fileComparePipeline.diff->hunks, sourceRole, sourceLine);
	}
	const std::size_t targetDocumentLines = std::max<std::size_t>(1, targetEditor.bufferModel().lineCount());
	return std::min(targetLine, targetDocumentLines - 1);
}

const MRBentoFileCompareChangeGroup *MRBentoBox::fileCompareChangeGroupAtOrVisibleForRole(MRBentoPaneRole role, const MRFileEditor &editor, bool editablePanes) const noexcept {
	if (!fileCompareRoleIsDiff(role) || fileComparePipeline.diff == nullptr || fileComparePipeline.diff->changeGroups == nullptr) return nullptr;
	const std::vector<MRBentoFileCompareChangeGroup> &groups = *fileComparePipeline.diff->changeGroups;

	const int cursorGroupIndex = fileCompareChangeGroupIndexAtCursor(role, editor, editablePanes);
	if (cursorGroupIndex >= 0) return &groups[static_cast<std::size_t>(cursorGroupIndex)];

	const std::size_t visibleStartLine = static_cast<std::size_t>(std::max(0, editor.delta.y));
	const std::size_t visibleEndLine = visibleStartLine + static_cast<std::size_t>(std::max(1, editor.visibleViewportRows()));
	for (const MRBentoFileCompareChangeGroup &group : groups) {
		const std::size_t groupStart = fileCompareGroupStartLineForRole(group, role, editablePanes);
		const std::size_t groupEnd = groupStart + fileCompareGroupEffectiveLineCountForRole(group, role, editablePanes);
		if (groupEnd > visibleStartLine && groupStart < visibleEndLine) return &group;
	}
	return nullptr;
}

int MRBentoBox::fileCompareChangeGroupIndexAtCursor(MRBentoPaneRole role, const MRFileEditor &editor, bool editablePanes) const noexcept {
	if (!fileCompareRoleIsDiff(role)) return -1;

	const std::size_t cursorLine = editor.lineIndexOfOffset(editor.cursorOffset());
	return fileCompareChangeGroupIndexAtLine(role, cursorLine, editablePanes);
}

int MRBentoBox::fileCompareChangeGroupIndexAtLine(MRBentoPaneRole role, std::size_t line, bool editablePanes) const noexcept {
	if (!fileCompareRoleIsDiff(role) || fileComparePipeline.diff == nullptr || fileComparePipeline.diff->changeGroups == nullptr) return -1;
	const std::vector<MRBentoFileCompareChangeGroup> &groups = *fileComparePipeline.diff->changeGroups;

	for (std::size_t i = 0; i < groups.size(); ++i) {
		const MRBentoFileCompareChangeGroup &group = groups[i];
		const std::size_t groupStart = fileCompareGroupStartLineForRole(group, role, editablePanes);
		const std::size_t groupLineCount = fileCompareGroupEffectiveLineCountForRole(group, role, editablePanes);
		const std::size_t groupEnd = groupStart + groupLineCount;
		if (line >= groupStart && line < groupEnd) return static_cast<int>(i);
	}
	return -1;
}

bool MRBentoBox::moveFileCompareEditorToGroup(MRFileEditor &editor, MRBentoPaneRole role, const MRBentoFileCompareChangeGroup &group, bool editablePanes) {
	const std::size_t documentLineCount = std::max<std::size_t>(1, editor.bufferModel().lineCount());
	std::size_t targetLine = fileCompareGroupNavigationLineForRole(group, role, editor, editablePanes);

	if (targetLine >= documentLineCount) targetLine = documentLineCount - 1;

	editor.moveCursorToDocumentLineTop(targetLine, 0);
	return true;
}

std::string MRBentoBox::fileCompareStatusForLeaf(const BentoLeaf &leaf) const {
	if (bentoMode != bbmFileCompare || !fileCompareRoleIsDiff(leaf.role)) return std::string();
	const bool pipelineRunning = fileComparePipeline.originalAcquisitionTaskId != 0 || fileComparePipeline.compareAcquisitionTaskId != 0 ||
		fileComparePipeline.diffTaskId != 0 || fileComparePipeline.originalProjectionTaskId != 0 || fileComparePipeline.compareProjectionTaskId != 0;
	if (fileCompareStale) return pipelineRunning ? "comparing" : "stale";
	if (!fileCompareDiffReady || fileComparePipeline.diff == nullptr || fileComparePipeline.diff->changeGroups == nullptr) return pipelineRunning ? std::string("comparing") : std::string();
	const std::vector<MRBentoFileCompareChangeGroup> &groups = *fileComparePipeline.diff->changeGroups;

	const MREditWindow *targetWindow = leaf.id == 0 ? static_cast<const MREditWindow *>(this) : static_cast<const MREditWindow *>(leaf.pane);
	MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
	if (targetEditor == nullptr || groups.empty()) return std::string();

	const std::size_t visibleStartLine = static_cast<std::size_t>(std::max(0, targetEditor->delta.y));
	const std::size_t visibleLineCount = static_cast<std::size_t>(std::max(1, targetEditor->visibleViewportRows()));
	const std::size_t visibleEndLine = visibleStartLine + visibleLineCount;
	std::size_t firstVisibleChange = 0;
	std::size_t lastVisibleChange = 0;
	std::size_t visibleDeletedLines = 0;
	std::size_t visibleInsertedLines = 0;
	std::size_t totalDeletedLines = 0;
	std::size_t totalInsertedLines = 0;
	std::size_t activeChange = 0;
	std::size_t activeDeletedLines = 0;
	std::size_t activeInsertedLines = 0;
	std::size_t activeDisplayStartLine = 0;
	bool hasVisibleChange = false;
	bool hasActiveChange = false;

	std::size_t displayLine = 0;
	std::size_t currentChange = 0;
	bool groupOpen = false;

	const bool editablePanes = fileComparePanesEditable();
	const MRBentoFileCompareChangeGroup *activeGroup = fileCompareChangeGroupAtOrVisibleForRole(leaf.role, *targetEditor, editablePanes);
	if (activeGroup != nullptr) {
		activeChange = static_cast<std::size_t>(activeGroup - groups.data()) + 1;
		activeDeletedLines = activeGroup->deletedLineCount;
		activeInsertedLines = activeGroup->insertedLineCount;
		activeDisplayStartLine = fileCompareGroupStartLineForRole(*activeGroup, leaf.role, editablePanes);
		hasActiveChange = true;
	}

	for (const mr::diff::MRDiffHunk &hunk : fileComparePipeline.diff->hunks) {
		switch (hunk.op) {
			case mr::diff::MRDiffOp::Equal:
				displayLine += hunk.count;
				groupOpen = false;
				break;
			case mr::diff::MRDiffOp::Delete:
			case mr::diff::MRDiffOp::Insert: {
				if (!groupOpen) {
					++currentChange;
					groupOpen = true;
				}

				const std::size_t hunkStartLine = displayLine;
				const std::size_t hunkEndLine = hunkStartLine + hunk.count;
				const bool hunkVisible = hunkEndLine > visibleStartLine && hunkStartLine < visibleEndLine;

				if (hunk.op == mr::diff::MRDiffOp::Delete)
					totalDeletedLines += hunk.count;
				else
					totalInsertedLines += hunk.count;
				if (hunkVisible) {
					const std::size_t visibleHunkStartLine = std::max(hunkStartLine, visibleStartLine);
					const std::size_t visibleHunkEndLine = std::min(hunkEndLine, visibleEndLine);
					const std::size_t visibleHunkLineCount = visibleHunkEndLine - visibleHunkStartLine;

					if (!hasVisibleChange) {
						firstVisibleChange = currentChange;
						hasVisibleChange = true;
					}
					lastVisibleChange = currentChange;
					if (hunk.op == mr::diff::MRDiffOp::Delete)
						visibleDeletedLines += visibleHunkLineCount;
					else
						visibleInsertedLines += visibleHunkLineCount;
				}
				displayLine = hunkEndLine;
				break;
			}
			default:
				break;
		}
	}
	if (hasVisibleChange || hasActiveChange) {
		std::string status;

		if (hasVisibleChange) {
			if (firstVisibleChange == lastVisibleChange)
				status += std::to_string(firstVisibleChange);
			else
				status += std::to_string(firstVisibleChange) + "-" + std::to_string(lastVisibleChange);
			status += "/" + std::to_string(groups.size());
			status += " -" + std::to_string(visibleDeletedLines) + "|+" + std::to_string(visibleInsertedLines);
			status += " -" + std::to_string(totalDeletedLines) + "|+" + std::to_string(totalInsertedLines);
		}
		if (hasActiveChange) {
			if (!status.empty()) status += " ";
			status += "@" + std::to_string(activeChange) + "/" + std::to_string(groups.size());
			status += " -" + std::to_string(activeDeletedLines) + "|+" + std::to_string(activeInsertedLines);
			status += " L" + std::to_string(activeDisplayStartLine + 1);
		}
		return status;
	}
	return std::string();
}

bool MRBentoBox::jumpToFileCompareChange(bool next) {
	if (bentoMode != bbmFileCompare || fileComparePipeline.diff == nullptr || fileComparePipeline.diff->changeGroups == nullptr ||
	    fileComparePipeline.diff->changeGroups->empty())
		return false;
	const std::vector<MRBentoFileCompareChangeGroup> &groups = *fileComparePipeline.diff->changeGroups;
	const MRBentoPaneRole activeRole = roleForLeaf(activeLeafId);
	if (!fileCompareRoleIsDiff(activeRole)) return false;

	MREditWindow *activeWindow = activeLeafId == 0 ? static_cast<MREditWindow *>(this) : static_cast<MREditWindow *>(paneWindowForLeaf(activeLeafId));
	MRFileEditor *activeEditor = activeWindow != nullptr ? activeWindow->getEditor() : nullptr;
	if (activeEditor == nullptr) return false;

	const bool editablePanes = fileComparePanesEditable();
	const int cursorGroupIndex = fileCompareChangeGroupIndexAtCursor(activeRole, *activeEditor, editablePanes);
	std::size_t targetIndex = 0;

	if (cursorGroupIndex >= 0) {
		const std::size_t currentIndex = static_cast<std::size_t>(cursorGroupIndex);
		targetIndex = next ? (currentIndex + 1) % groups.size() : (currentIndex == 0 ? groups.size() - 1 : currentIndex - 1);
	} else {
		const std::size_t cursorLine = activeEditor->lineIndexOfOffset(activeEditor->cursorOffset());
		bool targetFound = false;

		if (next) {
			for (std::size_t i = 0; i < groups.size(); ++i) {
				const std::size_t groupLine = fileCompareGroupNavigationLineForRole(groups[i], activeRole, *activeEditor, editablePanes);
				if (groupLine > cursorLine) {
					targetIndex = i;
					targetFound = true;
					break;
				}
			}
			if (!targetFound) targetIndex = 0;
		} else {
			for (std::size_t i = groups.size(); i > 0; --i) {
				const std::size_t groupLine = fileCompareGroupNavigationLineForRole(groups[i - 1], activeRole, *activeEditor, editablePanes);
				if (groupLine < cursorLine) {
					targetIndex = i - 1;
					targetFound = true;
					break;
				}
			}
			if (!targetFound) targetIndex = groups.size() - 1;
		}
	}

	if (!moveFileCompareEditorToGroup(*activeEditor, activeRole, groups[targetIndex], editablePanes)) return false;
	if (activeWindow != nullptr) activeWindow->drawView();
	syncFileCompareLinkedPaneFrom(activeLeafId);
	return true;
}

bool MRBentoBox::navigateFileCompareChange(bool next) {
	if (!jumpToFileCompareChange(next)) return false;
	updateActivePaneFrame();
	bentoProjectionDirty |= bpdContent | bpdChrome | bpdScrollBar | bpdOverlay;
	flushBentoProjection();
	return true;
}

bool MRBentoBox::applyFileCompareChange(bool originalToCompare) {
	if (bentoMode != bbmFileCompare || !fileCompareDiffReady || fileCompareStale || fileComparePipeline.diff == nullptr ||
	    fileComparePipeline.diff->changeGroups == nullptr || fileComparePipeline.diff->changeGroups->empty() || !fileComparePanesEditable())
		return false;
	const MRBentoPaneRole activeRole = roleForLeaf(activeLeafId);
	if (!fileCompareRoleIsDiff(activeRole)) return false;

	MREditWindow *activeWindow = activeLeafId == 0 ? static_cast<MREditWindow *>(this) : static_cast<MREditWindow *>(paneWindowForLeaf(activeLeafId));
	MRFileEditor *activeEditor = activeWindow != nullptr ? activeWindow->getEditor() : nullptr;
	if (activeEditor == nullptr) return false;

	const MRBentoFileCompareChangeGroup *activeGroup = fileCompareChangeGroupAtOrVisibleForRole(activeRole, *activeEditor, true);
	if (activeGroup == nullptr) return false;

	return applyFileCompareChangeGroup(originalToCompare, *activeGroup);
}

bool MRBentoBox::applyFileCompareChangeGroup(bool originalToCompare, const MRBentoFileCompareChangeGroup &group) {
	if (bentoMode != bbmFileCompare || !fileCompareDiffReady || fileCompareStale || fileComparePipeline.diff == nullptr ||
	    fileComparePipeline.diff->changeGroups == nullptr || fileComparePipeline.diff->changeGroups->empty() || !fileComparePanesEditable())
		return false;

	const MRBentoPaneRole targetRole = originalToCompare ? bprDiffCompare : bprDiffOriginal;
	const int targetLeafId = leafIdForRole(targetRole);
	MREditWindow *targetWindow = nullptr;
	if (targetLeafId == 0)
		targetWindow = this;
	else if (targetLeafId > 0)
		targetWindow = paneWindowForLeaf(targetLeafId);
	if (targetWindow == nullptr) targetWindow = findEditWindowByBufferId(originalToCompare ? fileCompareSetup.compare.bufferId : fileCompareSetup.original.bufferId);
	MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
	if (targetEditor == nullptr) return false;

	const std::vector<std::string> &sourceLines = originalToCompare ? *fileComparePipeline.diff->originalLines : *fileComparePipeline.diff->compareLines;
	const std::size_t sourceStartLine = originalToCompare ? group.originalStartLine : group.compareStartLine;
	const std::size_t sourceLineCount = originalToCompare ? group.deletedLineCount : group.insertedLineCount;
	const std::size_t targetStartLine = originalToCompare ? group.compareStartLine : group.originalStartLine;
	const std::size_t targetLineCount = originalToCompare ? group.insertedLineCount : group.deletedLineCount;
	if (sourceLineCount == 0 && targetLineCount == 0) return false;

	std::size_t rangeStart = 0;
	std::size_t rangeEnd = 0;
	if (!fileCompareEditorLineRange(*targetEditor, targetStartLine, targetLineCount, rangeStart, rangeEnd)) return false;

	const MRTextBufferModel &targetModel = targetEditor->bufferModel();
	const bool prefixNewline = sourceLineCount > 0 && targetLineCount == 0 && rangeStart == targetModel.length() && rangeStart > 0 && targetModel.charAt(rangeStart - 1) != '\n';
	const bool suffixNewline = sourceLineCount > 0 && rangeEnd < targetModel.length();
	const std::string replacement = fileCompareJoinedLineRange(sourceLines, sourceStartLine, sourceLineCount, prefixNewline, suffixNewline);
	const std::size_t uintMax = static_cast<std::size_t>(std::numeric_limits<unsigned int>::max());
	if (rangeStart > uintMax || rangeEnd > uintMax || replacement.size() > uintMax) return false;

	if (!targetEditor->replaceRangeAndSelect(static_cast<uint>(rangeStart), static_cast<uint>(rangeEnd), replacement.data(), static_cast<uint>(replacement.size()))) return false;
	const std::size_t selectionEnd = std::min<std::size_t>(rangeStart + replacement.size(), targetEditor->bufferModel().length());
	targetEditor->setSelectionOffsets(selectionEnd, selectionEnd, False);
	if (targetWindow != nullptr) targetWindow->setFileChanged(targetEditor->isDocumentModified());
	refreshFileCompareAfterSourceMutation(targetRole);
	if (targetLeafId >= 0) syncFileCompareLinkedPaneFrom(targetLeafId);
	bentoProjectionDirty |= bpdContent | bpdChrome | bpdScrollBar | bpdOverlay;
	flushBentoProjection();
	return true;
}

bool MRBentoBox::applyFileCompareResult(const mr::coprocessor::Result &result) {
	if (bentoMode != bbmFileCompare) return false;
	std::uint64_t *taskIds[] = {
		&fileComparePipeline.originalAcquisitionTaskId,
		&fileComparePipeline.compareAcquisitionTaskId,
		&fileComparePipeline.diffTaskId,
		&fileComparePipeline.originalProjectionTaskId,
		&fileComparePipeline.compareProjectionTaskId
	};
	std::size_t stage = sizeof(taskIds) / sizeof(taskIds[0]);
	for (std::size_t index = 0; index < sizeof(taskIds) / sizeof(taskIds[0]); ++index) {
		if (*taskIds[index] == result.task.id) {
			stage = index;
			break;
		}
	}
	if (stage == sizeof(taskIds) / sizeof(taskIds[0])) return false;
	releaseCoprocessorTask(result.task.id);
	*taskIds[stage] = 0;

	if (result.failed()) {
		mrLogMessage((std::string("File compare failed: ") + result.error).c_str());
		cancelFileComparePipeline();
		fileCompareDiffReady = false;
		fileCompareStale = true;
		refreshFileComparePanes();
		return true;
	}
	if (result.cancelled() || result.task.generation != fileComparePipeline.activeGeneration) return true;

	switch (stage) {
		case 0:
		case 1: {
			const std::shared_ptr<const MRBentoFileCompareAcquisitionPayload> payload =
				std::dynamic_pointer_cast<const MRBentoFileCompareAcquisitionPayload>(result.payload);
			const bool original = stage == 0;
			const MRBentoCompareSource &source = original ? fileCompareSetup.original : fileCompareSetup.compare;
			if (payload == nullptr || payload->generation != fileComparePipeline.activeGeneration || payload->original != original ||
			    payload->documentId != source.documentId || payload->version != source.version || !fileCompareSourceStillMatches(source)) {
				mrLogMessage("File compare acquisition discarded: source document changed.");
				cancelFileComparePipeline();
				fileCompareDiffReady = false;
				fileCompareStale = true;
				refreshFileComparePanes();
				return true;
			}
			if (original)
				fileComparePipeline.originalAcquisition = payload;
			else
				fileComparePipeline.compareAcquisition = payload;
			if (fileComparePipeline.originalAcquisition != nullptr && fileComparePipeline.compareAcquisition != nullptr &&
			    !submitFileCompareDiff()) {
				mrLogMessage("File compare failed: ordered diff worker could not be created.");
				cancelFileComparePipeline();
				refreshFileComparePanes();
			}
			return true;
		}
		case 2: {
			const std::shared_ptr<const MRBentoFileCompareDiffPayload> payload =
				std::dynamic_pointer_cast<const MRBentoFileCompareDiffPayload>(result.payload);
			if (payload == nullptr || payload->generation != fileComparePipeline.activeGeneration ||
			    payload->originalDocumentId != fileCompareSetup.original.documentId ||
			    payload->originalBaseVersion != fileCompareSetup.original.version ||
			    payload->compareDocumentId != fileCompareSetup.compare.documentId ||
			    payload->compareBaseVersion != fileCompareSetup.compare.version ||
			    !fileCompareSourceStillMatches(fileCompareSetup.original) || !fileCompareSourceStillMatches(fileCompareSetup.compare)) {
				mrLogMessage("File compare result discarded: source document changed.");
				cancelFileComparePipeline();
				fileCompareDiffReady = false;
				fileCompareStale = true;
				refreshFileComparePanes();
				return true;
			}
			fileComparePipeline.diff = payload;
			if (!submitFileComparePaneProjection(true) || !submitFileComparePaneProjection(false)) {
				mrLogMessage("File compare failed: pane projection worker could not be created.");
				cancelFileComparePipeline();
				refreshFileComparePanes();
			}
			return true;
		}
		case 3:
		case 4: {
			const std::shared_ptr<const MRBentoFileComparePaneProjectionPayload> payload =
				std::dynamic_pointer_cast<const MRBentoFileComparePaneProjectionPayload>(result.payload);
			const bool original = stage == 3;
			const MRBentoCompareSource &source = original ? fileCompareSetup.original : fileCompareSetup.compare;
			if (payload == nullptr || payload->generation != fileComparePipeline.activeGeneration || payload->original != original ||
			    payload->editable != fileComparePanesEditable() || payload->sourceDocumentId != source.documentId ||
			    payload->sourceVersion != source.version || !fileCompareSourceStillMatches(source)) {
				mrLogMessage("File compare pane projection discarded: source document changed.");
				cancelFileComparePipeline();
				fileCompareDiffReady = false;
				fileCompareStale = true;
				refreshFileComparePanes();
				return true;
			}
			if (original)
				fileComparePipeline.originalProjection = payload;
			else
				fileComparePipeline.compareProjection = payload;
			if (fileComparePipeline.originalProjection == nullptr || fileComparePipeline.compareProjection == nullptr)
				return true;
			fileComparePipeline.originalTargetDocumentId = 0;
			fileComparePipeline.originalTargetVersion = 0;
			fileComparePipeline.compareTargetDocumentId = 0;
			fileComparePipeline.compareTargetVersion = 0;
			fileCompareDiffReady = true;
			fileCompareStale = false;
			refreshFileComparePanes();
			return true;
		}
		default:
			break;
	}
	return true;
}
