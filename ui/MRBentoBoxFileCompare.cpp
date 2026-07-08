#include "MRBentoBox.hpp"

#include "../app/commands/MRWindowCommands.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../coprocessor/MRCoprocessor.hpp"
#include "MRWindowSupport.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <stop_token>
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

bool fileCompareHunkCanExtend(const mr::diff::MRDiffHunk &hunk, mr::diff::MRDiffOp op, std::size_t leftStart, std::size_t rightStart) noexcept {
	if (hunk.op != op) return false;
	switch (op) {
		case mr::diff::MRDiffOp::Equal:
			return leftStart == hunk.leftStart + hunk.count && rightStart == hunk.rightStart + hunk.count;
		case mr::diff::MRDiffOp::Delete:
			return leftStart == hunk.leftStart + hunk.count && rightStart == hunk.rightStart;
		case mr::diff::MRDiffOp::Insert:
			return leftStart == hunk.leftStart && rightStart == hunk.rightStart + hunk.count;
		default:
			break;
	}
	return false;
}

void appendFileCompareHunk(std::vector<mr::diff::MRDiffHunk> &hunks, mr::diff::MRDiffOp op, std::size_t leftStart, std::size_t rightStart, std::size_t count) {
	if (count == 0) return;
	if (!hunks.empty() && fileCompareHunkCanExtend(hunks.back(), op, leftStart, rightStart)) {
		hunks.back().count += count;
		return;
	}
	hunks.push_back(mr::diff::MRDiffHunk(op, leftStart, rightStart, count));
}

void appendNormalizedFileCompareChangeGroup(std::vector<mr::diff::MRDiffHunk> &hunks, const std::vector<std::string> &originalLines, const std::vector<std::string> &compareLines, std::size_t originalStart, std::size_t compareStart, std::size_t deletedLineCount, std::size_t insertedLineCount) {
	if (deletedLineCount == 0) {
		appendFileCompareHunk(hunks, mr::diff::MRDiffOp::Insert, originalStart, compareStart, insertedLineCount);
		return;
	}
	if (insertedLineCount == 0) {
		appendFileCompareHunk(hunks, mr::diff::MRDiffOp::Delete, originalStart, compareStart, deletedLineCount);
		return;
	}

	const std::size_t maxLineCount = std::max(deletedLineCount, insertedLineCount);
	std::size_t runStart = 0;
	std::size_t i = 0;
	while (i < maxLineCount) {
		const bool equalPair = i < deletedLineCount && i < insertedLineCount && originalStart + i < originalLines.size() && compareStart + i < compareLines.size() && originalLines[originalStart + i] == compareLines[compareStart + i];
		if (!equalPair) {
			++i;
			continue;
		}

		if (runStart < i) {
			const std::size_t deletedRunCount = runStart < deletedLineCount ? std::min(i, deletedLineCount) - runStart : 0;
			const std::size_t insertedRunCount = runStart < insertedLineCount ? std::min(i, insertedLineCount) - runStart : 0;
			appendFileCompareHunk(hunks, mr::diff::MRDiffOp::Delete, originalStart + runStart, compareStart + std::min(runStart, insertedLineCount), deletedRunCount);
			appendFileCompareHunk(hunks, mr::diff::MRDiffOp::Insert, originalStart + std::min(runStart + deletedRunCount, deletedLineCount), compareStart + runStart, insertedRunCount);
		}
		appendFileCompareHunk(hunks, mr::diff::MRDiffOp::Equal, originalStart + i, compareStart + i, 1);
		++i;
		runStart = i;
	}
	if (runStart < maxLineCount) {
		const std::size_t deletedRunCount = runStart < deletedLineCount ? deletedLineCount - runStart : 0;
		const std::size_t insertedRunCount = runStart < insertedLineCount ? insertedLineCount - runStart : 0;
		appendFileCompareHunk(hunks, mr::diff::MRDiffOp::Delete, originalStart + runStart, compareStart + std::min(runStart, insertedLineCount), deletedRunCount);
		appendFileCompareHunk(hunks, mr::diff::MRDiffOp::Insert, originalStart + std::min(runStart + deletedRunCount, deletedLineCount), compareStart + runStart, insertedRunCount);
	}
}

void normalizeFileCompareHunks(const std::vector<std::string> &originalLines, const std::vector<std::string> &compareLines, std::vector<mr::diff::MRDiffHunk> &hunks) {
	std::vector<mr::diff::MRDiffHunk> normalizedHunks;
	bool groupOpen = false;
	std::size_t groupOriginalStart = 0;
	std::size_t groupCompareStart = 0;
	std::size_t groupDeletedLineCount = 0;
	std::size_t groupInsertedLineCount = 0;
	auto flushGroup = [&]() {
		if (!groupOpen) return;
		appendNormalizedFileCompareChangeGroup(normalizedHunks, originalLines, compareLines, groupOriginalStart, groupCompareStart, groupDeletedLineCount, groupInsertedLineCount);
		groupOpen = false;
		groupDeletedLineCount = 0;
		groupInsertedLineCount = 0;
	};

	normalizedHunks.reserve(hunks.size());
	for (const mr::diff::MRDiffHunk &hunk : hunks) {
		switch (hunk.op) {
			case mr::diff::MRDiffOp::Equal:
				flushGroup();
				appendFileCompareHunk(normalizedHunks, hunk.op, hunk.leftStart, hunk.rightStart, hunk.count);
				break;
			case mr::diff::MRDiffOp::Delete:
				if (!groupOpen) {
					groupOpen = true;
					groupOriginalStart = hunk.leftStart;
					groupCompareStart = hunk.rightStart;
				}
				groupDeletedLineCount += hunk.count;
				break;
			case mr::diff::MRDiffOp::Insert:
				if (!groupOpen) {
					groupOpen = true;
					groupOriginalStart = hunk.leftStart;
					groupCompareStart = hunk.rightStart;
				}
				groupInsertedLineCount += hunk.count;
				break;
			default:
				break;
		}
	}
	flushGroup();
	hunks.swap(normalizedHunks);
}
} // namespace

bool MRBentoBox::initializeFileCompare(const MRBentoCompareSetup &setup) {
	if (bentoMode != bbmFileCompare) return false;

	const bool compareFirst = configuredFileCompareStartConfiguration() == MRFileCompareStartConfiguration::CompareOriginal;
	const MRBentoPaneRole primaryRole = compareFirst ? bprDiffCompare : bprDiffOriginal;
	const MRBentoPaneRole secondaryRole = compareFirst ? bprDiffOriginal : bprDiffCompare;
	fileCompareSetup = setup;
	fileCompareHunks.clear();
	fileCompareChangeGroups.clear();
	fileCompareOriginalLines.clear();
	fileCompareCompareLines.clear();
	fileCompareOriginalLineKinds.clear();
	fileCompareCompareLineKinds.clear();
	fileCompareOriginalMiniMapSlices.clear();
	fileCompareCompareMiniMapSlices.clear();
	fileCompareTaskId = 0;
	fileCompareSourcesRestored = false;
	fileCompareDiffReady = false;
	fileCompareStale = false;
	refreshFileCompareCachedSnapshots(bprSource, true);
	if (fileCompareOriginalLines.empty() && !fileCompareSetup.original.text.empty()) mr::diff::mrSplitTextLinesForDiff(fileCompareSetup.original.text, fileCompareOriginalLines);
	if (fileCompareCompareLines.empty() && !fileCompareSetup.compare.text.empty()) mr::diff::mrSplitTextLinesForDiff(fileCompareSetup.compare.text, fileCompareCompareLines);

	for (BentoLeaf &leaf : leaves) {
		if (leaf.id == 0) {
			leaf.role = primaryRole;
			leaf.spec = paneSpecForRole(primaryRole);
			leaf.title = fileComparePaneTitle(primaryRole);
		}
	}
	if (!loadTextBuffer(fileCompareTextForRole(primaryRole, nullptr).c_str(), fileComparePaneTitle(primaryRole))) return false;
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

void MRBentoBox::refreshFileCompareSourceSnapshot(MRBentoCompareSource &source, MREditWindow *window, std::vector<std::string> &lineCache, bool force) {
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
	source.text = editor->snapshotText();
	mr::diff::mrSplitTextLinesForDiff(source.text, lineCache);
}

void MRBentoBox::refreshFileCompareCachedSnapshots(MRBentoPaneRole changedRole, bool force) {
	MREditWindow *originalWindow = findEditWindowByBufferId(fileCompareSetup.original.bufferId);
	MREditWindow *compareWindow = findEditWindowByBufferId(fileCompareSetup.compare.bufferId);

	if (force || changedRole == bprSource || changedRole == bprDiffOriginal) refreshFileCompareSourceSnapshot(fileCompareSetup.original, originalWindow, fileCompareOriginalLines, force);
	if (force || changedRole == bprSource || changedRole == bprDiffCompare) refreshFileCompareSourceSnapshot(fileCompareSetup.compare, compareWindow, fileCompareCompareLines, force);
}

void MRBentoBox::rebuildFileCompareProjectionCache() {
	fileCompareOriginalLineKinds.clear();
	fileCompareCompareLineKinds.clear();
	fileCompareOriginalMiniMapSlices.clear();
	fileCompareCompareMiniMapSlices.clear();
	fileCompareEditableLineKindsForRole(bprDiffOriginal, fileCompareOriginalLineKinds, &fileCompareOriginalMiniMapSlices);
	fileCompareEditableLineKindsForRole(bprDiffCompare, fileCompareCompareLineKinds, &fileCompareCompareMiniMapSlices);
}

void MRBentoBox::refreshFileCompareAfterSourceMutation(MRBentoPaneRole changedRole) {
	if (!fileComparePanesEditable()) return;
	MREditWindow *originalWindow = findEditWindowByBufferId(fileCompareSetup.original.bufferId);
	MREditWindow *compareWindow = findEditWindowByBufferId(fileCompareSetup.compare.bufferId);
	MRFileEditor *originalEditor = originalWindow != nullptr ? originalWindow->getEditor() : nullptr;
	MRFileEditor *compareEditor = compareWindow != nullptr ? compareWindow->getEditor() : nullptr;
	std::uint64_t taskId;

	if (originalWindow == nullptr || compareWindow == nullptr || originalEditor == nullptr || compareEditor == nullptr) return;

	if (fileCompareTaskId != 0) {
		static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(fileCompareTaskId));
		releaseCoprocessorTask(fileCompareTaskId);
		fileCompareTaskId = 0;
	}

	refreshFileCompareCachedSnapshots(changedRole, false);

	fileCompareStale = true;
	taskId = mr::coprocessor::globalCoprocessor().submit(mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::FileCompare, fileCompareSetup.original.documentId, fileCompareSetup.original.version, "file compare", [originalLines = fileCompareOriginalLines, compareLines = fileCompareCompareLines, originalDocumentId = fileCompareSetup.original.documentId, originalVersion = fileCompareSetup.original.version, compareDocumentId = fileCompareSetup.compare.documentId, compareVersion = fileCompareSetup.compare.version](const mr::coprocessor::TaskInfo &task, std::stop_token stopToken) {
		mr::coprocessor::Result result;
		std::vector<mr::diff::MRDiffHunk> hunks;
		std::string errorText;

		result.task = task;
		if (!mr::diff::mrComputeMyersDiff(originalLines, compareLines, hunks, &errorText, stopToken)) {
			result.status = stopToken.stop_requested() ? mr::coprocessor::TaskStatus::Cancelled : mr::coprocessor::TaskStatus::Failed;
			result.error = errorText;
			return result;
		}
		result.status = mr::coprocessor::TaskStatus::Completed;
		result.payload = std::make_shared<mr::coprocessor::FileComparePayload>(originalDocumentId, originalVersion, compareDocumentId, compareVersion, originalLines.size(), compareLines.size(), std::move(hunks));
		return result;
	});
	if (taskId != 0) {
		setFileCompareTask(taskId);
		trackCoprocessorTask(taskId, mr::coprocessor::TaskKind::FileCompare, "file compare");
	}
}

void MRBentoBox::refreshFileCompareConfiguration() {
	if (bentoMode != bbmFileCompare) return;
	refreshFileComparePanes();
}

void MRBentoBox::setFileCompareTask(std::uint64_t taskId) noexcept {
	fileCompareTaskId = taskId;
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

void MRBentoBox::rebuildFileCompareChangeGroups() {
	fileCompareChangeGroups.clear();
	std::size_t displayLine = 0;
	bool groupOpen = false;

	for (const mr::diff::MRDiffHunk &hunk : fileCompareHunks) {
		switch (hunk.op) {
			case mr::diff::MRDiffOp::Equal:
				displayLine += hunk.count;
				groupOpen = false;
				break;
			case mr::diff::MRDiffOp::Delete:
				if (!groupOpen) {
					fileCompareChangeGroups.push_back(FileCompareChangeGroup());
					fileCompareChangeGroups.back().displayStartLine = displayLine;
					fileCompareChangeGroups.back().originalStartLine = hunk.leftStart;
					fileCompareChangeGroups.back().compareStartLine = hunk.rightStart;
					groupOpen = true;
				}
				fileCompareChangeGroups.back().displayLineCount += hunk.count;
				fileCompareChangeGroups.back().deletedLineCount += hunk.count;
				displayLine += hunk.count;
				break;
			case mr::diff::MRDiffOp::Insert:
				if (!groupOpen) {
					fileCompareChangeGroups.push_back(FileCompareChangeGroup());
					fileCompareChangeGroups.back().displayStartLine = displayLine;
					fileCompareChangeGroups.back().originalStartLine = hunk.leftStart;
					fileCompareChangeGroups.back().compareStartLine = hunk.rightStart;
					groupOpen = true;
				}
				fileCompareChangeGroups.back().displayLineCount += hunk.count;
				fileCompareChangeGroups.back().insertedLineCount += hunk.count;
				displayLine += hunk.count;
				break;
			default:
				break;
		}
	}
}

std::size_t MRBentoBox::fileCompareGroupStartLineForRole(const FileCompareChangeGroup &group, MRBentoPaneRole role, bool editablePanes) const noexcept {
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

std::size_t MRBentoBox::fileCompareGroupLineCountForRole(const FileCompareChangeGroup &group, MRBentoPaneRole role, bool editablePanes) const noexcept {
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

std::size_t MRBentoBox::fileCompareGroupEffectiveLineCountForRole(const FileCompareChangeGroup &group, MRBentoPaneRole role, bool editablePanes) const noexcept {
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

std::size_t MRBentoBox::fileCompareGroupNavigationLineForRole(const FileCompareChangeGroup &group, MRBentoPaneRole role, const MRFileEditor &editor, bool editablePanes) const {
	const std::size_t documentLineCount = std::max<std::size_t>(1, editor.bufferModel().lineCount());
	std::size_t targetLine = fileCompareGroupStartLineForRole(group, role, editablePanes);
	if (!editablePanes || !fileCompareRoleIsDiff(role)) return std::min(targetLine, documentLineCount - 1);

	const std::vector<unsigned char> &lineKinds = role == bprDiffOriginal ? fileCompareOriginalLineKinds : fileCompareCompareLineKinds;
	if (lineKinds.empty()) return std::min(targetLine, documentLineCount - 1);

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

	if (editablePanes && fileCompareDiffReady) {
		bool mappedInChangeGroup = false;
		for (const FileCompareChangeGroup &group : fileCompareChangeGroups) {
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
		if (!mappedInChangeGroup) targetLine = mappedFileCompareLineForRole(fileCompareHunks, sourceRole, sourceLine);
	}
	const std::size_t targetDocumentLines = std::max<std::size_t>(1, targetEditor.bufferModel().lineCount());
	return std::min(targetLine, targetDocumentLines - 1);
}

const MRBentoBox::FileCompareChangeGroup *MRBentoBox::fileCompareChangeGroupAtOrVisibleForRole(MRBentoPaneRole role, const MRFileEditor &editor, bool editablePanes) const noexcept {
	if (!fileCompareRoleIsDiff(role)) return nullptr;

	const int cursorGroupIndex = fileCompareChangeGroupIndexAtCursor(role, editor, editablePanes);
	if (cursorGroupIndex >= 0) return &fileCompareChangeGroups[static_cast<std::size_t>(cursorGroupIndex)];

	const std::size_t visibleStartLine = static_cast<std::size_t>(std::max(0, editor.delta.y));
	const std::size_t visibleEndLine = visibleStartLine + static_cast<std::size_t>(std::max(1, editor.visibleViewportRows()));
	for (const FileCompareChangeGroup &group : fileCompareChangeGroups) {
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
	if (!fileCompareRoleIsDiff(role)) return -1;

	for (std::size_t i = 0; i < fileCompareChangeGroups.size(); ++i) {
		const FileCompareChangeGroup &group = fileCompareChangeGroups[i];
		const std::size_t groupStart = fileCompareGroupStartLineForRole(group, role, editablePanes);
		const std::size_t groupLineCount = fileCompareGroupEffectiveLineCountForRole(group, role, editablePanes);
		const std::size_t groupEnd = groupStart + groupLineCount;
		if (line >= groupStart && line < groupEnd) return static_cast<int>(i);
	}
	return -1;
}

bool MRBentoBox::moveFileCompareEditorToGroup(MRFileEditor &editor, MRBentoPaneRole role, const FileCompareChangeGroup &group, bool editablePanes) {
	const std::size_t documentLineCount = std::max<std::size_t>(1, editor.bufferModel().lineCount());
	std::size_t targetLine = fileCompareGroupNavigationLineForRole(group, role, editor, editablePanes);

	if (targetLine >= documentLineCount) targetLine = documentLineCount - 1;

	editor.moveCursorToDocumentLineTop(targetLine, 0);
	return true;
}

std::string MRBentoBox::fileCompareStatusForLeaf(const BentoLeaf &leaf) const {
	if (bentoMode != bbmFileCompare || !fileCompareRoleIsDiff(leaf.role)) return std::string();
	if (fileCompareStale) return "stale";
	if (!fileCompareDiffReady) return fileCompareTaskId != 0 ? std::string("comparing") : std::string();

	const MREditWindow *targetWindow = leaf.id == 0 ? static_cast<const MREditWindow *>(this) : static_cast<const MREditWindow *>(leaf.pane);
	MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
	if (targetEditor == nullptr || fileCompareChangeGroups.empty()) return std::string();

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
	const FileCompareChangeGroup *activeGroup = fileCompareChangeGroupAtOrVisibleForRole(leaf.role, *targetEditor, editablePanes);
	if (activeGroup != nullptr) {
		activeChange = static_cast<std::size_t>(activeGroup - fileCompareChangeGroups.data()) + 1;
		activeDeletedLines = activeGroup->deletedLineCount;
		activeInsertedLines = activeGroup->insertedLineCount;
		activeDisplayStartLine = fileCompareGroupStartLineForRole(*activeGroup, leaf.role, editablePanes);
		hasActiveChange = true;
	}

	for (const mr::diff::MRDiffHunk &hunk : fileCompareHunks) {
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
			status += "/" + std::to_string(fileCompareChangeGroups.size());
			status += " -" + std::to_string(visibleDeletedLines) + "|+" + std::to_string(visibleInsertedLines);
			status += " -" + std::to_string(totalDeletedLines) + "|+" + std::to_string(totalInsertedLines);
		}
		if (hasActiveChange) {
			if (!status.empty()) status += " ";
			status += "@" + std::to_string(activeChange) + "/" + std::to_string(fileCompareChangeGroups.size());
			status += " -" + std::to_string(activeDeletedLines) + "|+" + std::to_string(activeInsertedLines);
			status += " L" + std::to_string(activeDisplayStartLine + 1);
		}
		return status;
	}
	return std::string();
}

bool MRBentoBox::jumpToFileCompareChange(bool next) {
	if (bentoMode != bbmFileCompare || fileCompareChangeGroups.empty()) return false;
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
		targetIndex = next ? (currentIndex + 1) % fileCompareChangeGroups.size() : (currentIndex == 0 ? fileCompareChangeGroups.size() - 1 : currentIndex - 1);
	} else {
		const std::size_t cursorLine = activeEditor->lineIndexOfOffset(activeEditor->cursorOffset());
		bool targetFound = false;

		if (next) {
			for (std::size_t i = 0; i < fileCompareChangeGroups.size(); ++i) {
				const std::size_t groupLine = fileCompareGroupNavigationLineForRole(fileCompareChangeGroups[i], activeRole, *activeEditor, editablePanes);
				if (groupLine > cursorLine) {
					targetIndex = i;
					targetFound = true;
					break;
				}
			}
			if (!targetFound) targetIndex = 0;
		} else {
			for (std::size_t i = fileCompareChangeGroups.size(); i > 0; --i) {
				const std::size_t groupLine = fileCompareGroupNavigationLineForRole(fileCompareChangeGroups[i - 1], activeRole, *activeEditor, editablePanes);
				if (groupLine < cursorLine) {
					targetIndex = i - 1;
					targetFound = true;
					break;
				}
			}
			if (!targetFound) targetIndex = fileCompareChangeGroups.size() - 1;
		}
	}

	if (!moveFileCompareEditorToGroup(*activeEditor, activeRole, fileCompareChangeGroups[targetIndex], editablePanes)) return false;
	if (activeWindow != nullptr) activeWindow->drawView();
	syncFileCompareLinkedPaneFrom(activeLeafId);
	return true;
}

bool MRBentoBox::navigateFileCompareChange(bool next) {
	if (!jumpToFileCompareChange(next)) return false;
	bentoProjectionDirty |= bpdContent | bpdChrome | bpdScrollBar | bpdOverlay;
	flushBentoProjection();
	return true;
}

bool MRBentoBox::applyFileCompareChange(bool originalToCompare) {
	if (bentoMode != bbmFileCompare || !fileCompareDiffReady || fileCompareStale || fileCompareChangeGroups.empty() || !fileComparePanesEditable()) return false;
	const MRBentoPaneRole activeRole = roleForLeaf(activeLeafId);
	if (!fileCompareRoleIsDiff(activeRole)) return false;

	MREditWindow *activeWindow = activeLeafId == 0 ? static_cast<MREditWindow *>(this) : static_cast<MREditWindow *>(paneWindowForLeaf(activeLeafId));
	MRFileEditor *activeEditor = activeWindow != nullptr ? activeWindow->getEditor() : nullptr;
	if (activeEditor == nullptr) return false;

	const FileCompareChangeGroup *activeGroup = fileCompareChangeGroupAtOrVisibleForRole(activeRole, *activeEditor, true);
	if (activeGroup == nullptr) return false;

	return applyFileCompareChangeGroup(originalToCompare, *activeGroup);
}

bool MRBentoBox::applyFileCompareChangeGroup(bool originalToCompare, const FileCompareChangeGroup &group) {
	if (bentoMode != bbmFileCompare || !fileCompareDiffReady || fileCompareStale || fileCompareChangeGroups.empty() || !fileComparePanesEditable()) return false;

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

	const std::vector<std::string> &sourceLines = originalToCompare ? fileCompareOriginalLines : fileCompareCompareLines;
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
	if (bentoMode != bbmFileCompare || fileCompareTaskId == 0 || result.task.id != fileCompareTaskId) return false;
	releaseCoprocessorTask(result.task.id);
	fileCompareTaskId = 0;

	if (result.failed()) {
		mrLogMessage((std::string("File compare failed: ") + result.error).c_str());
		return true;
	}
	if (result.cancelled()) return true;

	const mr::coprocessor::FileComparePayload *payload = dynamic_cast<const mr::coprocessor::FileComparePayload *>(result.payload.get());
	if (payload == nullptr) {
		mrLogMessage("File compare result discarded: missing payload.");
		return true;
	}
	if (payload->originalDocumentId != fileCompareSetup.original.documentId || payload->originalBaseVersion != fileCompareSetup.original.version || payload->compareDocumentId != fileCompareSetup.compare.documentId || payload->compareBaseVersion != fileCompareSetup.compare.version || !fileCompareSourceStillMatches(fileCompareSetup.original) || !fileCompareSourceStillMatches(fileCompareSetup.compare)) {
		fileCompareStale = true;
		fileCompareChangeGroups.clear();
		refreshFileComparePanes();
		mrLogMessage("File compare result discarded: source document changed.");
		return true;
	}

	fileCompareHunks = payload->hunks;
	normalizeFileCompareHunks(fileCompareOriginalLines, fileCompareCompareLines, fileCompareHunks);
	rebuildFileCompareChangeGroups();
	fileCompareDiffReady = true;
	rebuildFileCompareProjectionCache();
	fileCompareStale = false;
	refreshFileComparePanes();
	return true;
}
