#include "MRBentoBox.hpp"

#include "../app/commands/MRWindowCommands.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
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

void applyFileCompareEditorGutters(MRFileEditor &editor, MRBentoPaneRole role, const std::vector<unsigned char> &lineKinds, const std::vector<MRFileCompareMiniMapSlice> *miniMapSlices, const char *warmupReason) {
	const FileCompareGutterSettings gutters = fileCompareGutterSettingsForRole(role);
	const bool miniMapConfigured = fileCompareGuttersContain(gutters.leading, 'M') || fileCompareGuttersContain(gutters.trailing, 'M');

	editor.setMiniMapSuppressed(!miniMapConfigured);
	editor.setFileCompareGutters(gutters.leading, gutters.trailing);
	if (miniMapSlices != nullptr)
		editor.setFileCompareLineKinds(lineKinds, *miniMapSlices);
	else
		editor.setFileCompareLineKinds(lineKinds);
	editor.setFileCompareGutterVisible(true);
	editor.updateMetrics();
	editor.continueComputeWarmupIfNeeded(warmupReason);
}

void markFileCompareLineRange(std::vector<unsigned char> &lineKinds, std::size_t startLine, std::size_t lineCount, unsigned char lineKind) {
	for (std::size_t i = 0; i < lineCount && startLine + i < lineKinds.size(); ++i)
		lineKinds[startLine + i] = lineKind;
}

void markFileCompareAnchorLine(std::vector<unsigned char> &lineKinds, std::size_t lineIndex, unsigned char lineKind) {
	if (lineKinds.empty()) return;
	lineKinds[std::min(lineIndex, lineKinds.size() - 1)] = lineKind;
}

std::size_t fileCompareLineTextLength(const std::vector<std::string> &lines, std::size_t startLine, std::size_t lineCount) noexcept {
	std::size_t length = 0;

	for (std::size_t i = 0; i < lineCount && startLine + i < lines.size(); ++i)
		length += lines[startLine + i].size();
	return length;
}

std::string diffDisplayTitle(const MRBentoCompareSource &source, MRBentoPaneRole role) {
	return source.title.empty() ? std::string(fileComparePaneTitle(role)) : source.title;
}

void appendDiffDisplayLine(std::string &text, std::vector<unsigned char> *lineKinds, std::size_t &displayLineCount, const std::string &line, unsigned char lineKind) {
	if (displayLineCount > 0) text.push_back('\n');
	text += line;
	if (lineKinds != nullptr) lineKinds->push_back(lineKind);
	++displayLineCount;
}
} // namespace

std::string MRBentoBox::fileCompareTextForRole(MRBentoPaneRole role, std::vector<unsigned char> *lineKinds) const {
	if (role != bprDiffOriginal && role != bprDiffCompare) return std::string();
	if (lineKinds != nullptr) lineKinds->clear();
	if (!fileCompareDiffReady) {
		return role == bprDiffOriginal ? fileCompareSetup.original.text : fileCompareSetup.compare.text;
	}

	std::string text;
	std::size_t displayLineCount = 0;

	switch (role) {
		case bprDiffOriginal:
			for (const mr::diff::MRDiffHunk &hunk : fileCompareHunks) {
				switch (hunk.op) {
					case mr::diff::MRDiffOp::Equal:
					case mr::diff::MRDiffOp::Delete:
						for (std::size_t i = 0; i < hunk.count; ++i) {
							const std::size_t originalIndex = hunk.leftStart + i;
							if (originalIndex < fileCompareOriginalLines.size()) appendDiffDisplayLine(text, lineKinds, displayLineCount, fileCompareOriginalLines[originalIndex], hunk.op == mr::diff::MRDiffOp::Equal ? mrfclkEqual : mrfclkMissing);
						}
						break;
					case mr::diff::MRDiffOp::Insert:
						for (std::size_t i = 0; i < hunk.count; ++i)
							appendDiffDisplayLine(text, lineKinds, displayLineCount, std::string(), mrfclkOffset);
						break;
					default:
						break;
				}
			}
			break;
		case bprDiffCompare:
			for (const mr::diff::MRDiffHunk &hunk : fileCompareHunks) {
				switch (hunk.op) {
					case mr::diff::MRDiffOp::Equal:
					case mr::diff::MRDiffOp::Insert:
						for (std::size_t i = 0; i < hunk.count; ++i) {
							const std::size_t compareIndex = hunk.rightStart + i;
							if (compareIndex < fileCompareCompareLines.size()) appendDiffDisplayLine(text, lineKinds, displayLineCount, fileCompareCompareLines[compareIndex], hunk.op == mr::diff::MRDiffOp::Equal ? mrfclkEqual : mrfclkInsert);
						}
						break;
					case mr::diff::MRDiffOp::Delete:
						for (std::size_t i = 0; i < hunk.count; ++i)
							appendDiffDisplayLine(text, lineKinds, displayLineCount, std::string(), mrfclkOffset);
						break;
					default:
						break;
				}
			}
			break;
		default:
			break;
	}
	return text;
}

void MRBentoBox::fileCompareEditableLineKindsForRole(MRBentoPaneRole role, std::vector<unsigned char> &lineKinds, std::vector<MRFileCompareMiniMapSlice> *miniMapSlices) const {
	lineKinds.clear();
	if (miniMapSlices != nullptr) miniMapSlices->clear();
	if (role != bprDiffOriginal && role != bprDiffCompare) return;

	const std::vector<std::string> &originalLines = fileCompareOriginalLines;
	const std::vector<std::string> &compareLines = fileCompareCompareLines;
	lineKinds.assign(role == bprDiffOriginal ? originalLines.size() : compareLines.size(), mrfclkEqual);
	if (!fileCompareDiffReady) return;

	bool groupOpen = false;
	std::size_t groupOriginalStart = 0;
	std::size_t groupCompareStart = 0;
	std::size_t groupDeletedLineCount = 0;
	std::size_t groupInsertedLineCount = 0;
	auto appendFullMiniMapSlice = [&](std::size_t lineIndex, unsigned char lineKind) {
		if (miniMapSlices == nullptr || lineIndex >= lineKinds.size()) return;
		miniMapSlices->push_back(MRFileCompareMiniMapSlice{lineIndex, 0, 0, lineKind, true});
	};
	auto appendChangedMiniMapSlice = [&](std::size_t lineIndex, const std::string &baseLine, const std::string &changedLine, unsigned char lineKind) {
		if (miniMapSlices == nullptr || lineIndex >= lineKinds.size()) return;
		std::size_t prefix = 0;
		const std::size_t commonLimit = std::min(baseLine.size(), changedLine.size());
		while (prefix < commonLimit && baseLine[prefix] == changedLine[prefix])
			++prefix;
		std::size_t suffix = 0;
		while (suffix < commonLimit - prefix && baseLine[baseLine.size() - 1 - suffix] == changedLine[changedLine.size() - 1 - suffix])
			++suffix;
		std::size_t sliceStart = std::min(prefix, changedLine.size());
		std::size_t sliceEnd = changedLine.size() >= suffix ? changedLine.size() - suffix : changedLine.size();
		if (sliceEnd <= sliceStart && !changedLine.empty()) {
			sliceStart = sliceStart >= changedLine.size() ? changedLine.size() - 1 : sliceStart;
			sliceEnd = sliceStart + 1;
		}
		miniMapSlices->push_back(MRFileCompareMiniMapSlice{lineIndex, sliceStart, sliceEnd, lineKind, changedLine.empty()});
	};
	auto flushGroup = [&]() {
		if (!groupOpen) return;
		const bool replaceGroup = groupDeletedLineCount > 0 && groupInsertedLineCount > 0;

		if (role == bprDiffOriginal) {
			if (groupDeletedLineCount > 0) {
				markFileCompareLineRange(lineKinds, groupOriginalStart, groupDeletedLineCount, mrfclkMissing);
				for (std::size_t i = 0; i < groupDeletedLineCount && groupOriginalStart + i < lineKinds.size(); ++i) {
					const std::size_t originalIndex = groupOriginalStart + i;
					if (replaceGroup && i < groupInsertedLineCount && originalIndex < originalLines.size() && groupCompareStart + i < compareLines.size())
						appendChangedMiniMapSlice(originalIndex, compareLines[groupCompareStart + i], originalLines[originalIndex], mrfclkMissing);
					else
						appendFullMiniMapSlice(originalIndex, mrfclkMissing);
				}
			} else if (groupInsertedLineCount > 0) {
				markFileCompareAnchorLine(lineKinds, groupOriginalStart, mrfclkInsert);
				appendFullMiniMapSlice(std::min(groupOriginalStart, lineKinds.empty() ? 0 : lineKinds.size() - 1), mrfclkInsert);
			}
		} else {
			if (replaceGroup) {
				for (std::size_t i = 0; i < groupInsertedLineCount && groupCompareStart + i < lineKinds.size(); ++i) {
					const std::size_t originalLength = fileCompareLineTextLength(originalLines, groupOriginalStart + i, 1);
					const std::size_t compareLength = fileCompareLineTextLength(compareLines, groupCompareStart + i, 1);
					const unsigned char lineKind = compareLength < originalLength ? mrfclkMissing : mrfclkInsert;
					lineKinds[groupCompareStart + i] = lineKind;
					if (i < groupDeletedLineCount && groupOriginalStart + i < originalLines.size() && groupCompareStart + i < compareLines.size())
						appendChangedMiniMapSlice(groupCompareStart + i, originalLines[groupOriginalStart + i], compareLines[groupCompareStart + i], lineKind);
					else
						appendFullMiniMapSlice(groupCompareStart + i, lineKind);
				}
				if (groupDeletedLineCount > groupInsertedLineCount) {
					const std::size_t anchorLine = std::min(groupCompareStart + groupInsertedLineCount, lineKinds.empty() ? 0 : lineKinds.size() - 1);
					markFileCompareAnchorLine(lineKinds, groupCompareStart + groupInsertedLineCount, mrfclkMissing);
					appendFullMiniMapSlice(anchorLine, mrfclkMissing);
				}
			} else if (groupInsertedLineCount > 0) {
				markFileCompareLineRange(lineKinds, groupCompareStart, groupInsertedLineCount, mrfclkInsert);
				for (std::size_t i = 0; i < groupInsertedLineCount && groupCompareStart + i < lineKinds.size(); ++i)
					appendFullMiniMapSlice(groupCompareStart + i, mrfclkInsert);
			} else if (groupDeletedLineCount > 0) {
				markFileCompareAnchorLine(lineKinds, groupCompareStart, mrfclkMissing);
				appendFullMiniMapSlice(std::min(groupCompareStart, lineKinds.empty() ? 0 : lineKinds.size() - 1), mrfclkMissing);
			}
		}
		groupOpen = false;
		groupDeletedLineCount = 0;
		groupInsertedLineCount = 0;
	};

	for (const mr::diff::MRDiffHunk &hunk : fileCompareHunks) {
		switch (hunk.op) {
			case mr::diff::MRDiffOp::Equal:
				flushGroup();
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
}

void MRBentoBox::refreshFileComparePane(BentoLeaf &leaf) {
	MREditWindow *targetWindow = leaf.id == 0 ? static_cast<MREditWindow *>(this) : static_cast<MREditWindow *>(leaf.pane);
	MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
	std::string title;
	std::string text;
	std::vector<unsigned char> lineKinds;

	if (targetWindow == nullptr || !fileCompareRoleIsDiff(leaf.role)) return;
	leaf.spec = paneSpecForRole(leaf.role);
	if (leaf.id != 0 && leaf.pane != nullptr) leaf.pane->setPaneSpec(leaf.spec, getEditor());
	title = leaf.role == bprDiffOriginal ? diffDisplayTitle(fileCompareSetup.original, leaf.role) : diffDisplayTitle(fileCompareSetup.compare, leaf.role);
	if (fileComparePanesEditable()) {
		MREditWindow *sourceWindow = findEditWindowByBufferId(leaf.role == bprDiffOriginal ? fileCompareSetup.original.bufferId : fileCompareSetup.compare.bufferId);
		MRFileEditor *sourceEditor = sourceWindow != nullptr ? sourceWindow->getEditor() : nullptr;
		if (targetEditor != nullptr && sourceEditor != nullptr && targetEditor->documentId() != sourceEditor->documentId()) targetEditor->shareContentStateFrom(*sourceEditor);
		if (leaf.id != 0 && leaf.pane != nullptr) leaf.pane->layoutPaneChrome();
		if (targetEditor != nullptr) {
			if (leaf.role == bprDiffOriginal)
				applyFileCompareEditorGutters(*targetEditor, leaf.role, fileCompareOriginalLineKinds, &fileCompareOriginalMiniMapSlices, "file-compare-edit-refresh");
			else
				applyFileCompareEditorGutters(*targetEditor, leaf.role, fileCompareCompareLineKinds, &fileCompareCompareMiniMapSlices, "file-compare-edit-refresh");
		}
		targetWindow->setDisplayTitle(title.c_str());
		targetWindow->setReadOnly(false);
		leaf.title = fileComparePaneTitle(leaf.role);
		return;
	}
	text = fileCompareTextForRole(leaf.role, &lineKinds);
	if (fileCompareStale) {
		text = "[source changed while compare was running]\n\n" + text;
		lineKinds.insert(lineKinds.begin(), {mrfclkOffset, mrfclkOffset});
	}
	if (targetEditor != nullptr) {
		targetEditor->detachContentStateCopy();
		targetWindow->setCurrentFileName(nullptr);
	}
	static_cast<void>(targetWindow->replaceTextBuffer(text.c_str(), title.c_str()));
	if (leaf.id != 0 && leaf.pane != nullptr) leaf.pane->layoutPaneChrome();
	if (targetEditor != nullptr) applyFileCompareEditorGutters(*targetEditor, leaf.role, lineKinds, nullptr, "file-compare-refresh");
	targetWindow->setReadOnly(true);
	targetWindow->setFileChanged(false);
	leaf.title = fileComparePaneTitle(leaf.role);
}

void MRBentoBox::refreshFileComparePanes() {
	if (bentoMode != bbmFileCompare) return;
	for (BentoLeaf &leaf : leaves)
		if (leaf.visible) refreshFileComparePane(leaf);
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
		const int targetLineDelta = static_cast<int>(std::min(targetLine, static_cast<std::size_t>(std::numeric_limits<int>::max())));
		const int visualColumn = sourceEditor->displayedCursorColumn();
		const std::size_t targetOffset = targetEditor->lineMoveOffset(0, targetLineDelta, visualColumn);

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
