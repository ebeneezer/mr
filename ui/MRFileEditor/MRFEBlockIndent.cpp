#include "MRFEBlockOps.hpp"
#include "MRFileEditor.hpp"

#include <algorithm>

bool MRFEBlockOps::shiftCurrentBlockToTab(MRFileEditor &editor, bool indent, std::string *errorText) {
	if (errorText != nullptr) errorText->clear();
	normalize(editor);
	switch (mGeometry.mode) {
	case MRFEBlockMode::Line:
		return shiftCurrentLineBlockToTab(editor, indent, errorText);
	case MRFEBlockMode::Column:
		return shiftCurrentColumnBlockToTab(editor, indent, errorText);
	case MRFEBlockMode::Stream:
		return shiftCurrentStreamBlockToTab(editor, indent, errorText);
	case MRFEBlockMode::None:
		if (errorText != nullptr) *errorText = "Line, column or stream block required.";
		return false;
	}
	if (errorText != nullptr) *errorText = "Line, column or stream block required.";
	return false;
}

bool MRFEBlockOps::shiftCurrentLineBlockToTab(MRFileEditor &editor, bool indent, std::string *errorText) {
	if (errorText != nullptr) errorText->clear();
	if (editor.isReadOnly()) {
		if (errorText != nullptr) *errorText = "Editor is read-only.";
		return false;
	}
	normalize(editor);
	if (mGeometry.mode != MRFEBlockMode::Line) {
		if (errorText != nullptr) *errorText = "Line block required.";
		return false;
	}
	const std::size_t firstLineStart = mGeometry.rangeStart;
	const std::size_t lastLineStart = editor.bufferModel().lineStartByIndex(mGeometry.line2);
	std::size_t blockEnd = mGeometry.rangeEnd;
	if (firstLineStart >= blockEnd) {
		if (errorText != nullptr) *errorText = "Line block range is outside the editor buffer.";
		return false;
	}
	MREditSetupSettings settings;
	static_cast<void>(effectiveEditSetupSettingsForPath(editor.hasPersistentFileName() ? editor.persistentFileName() : "", settings));
	const MRTextBufferModel::ReadSnapshot snapshot = editor.readSnapshot();
	MRTextBufferModel::StagedTransaction transaction(snapshot, indent ? "indent-line-block" : "undent-line-block");
	int blockIndentColumn = 0;
	for (std::size_t lineStart = firstLineStart;; lineStart = snapshot.nextLine(lineStart)) {
		const std::string lineText = snapshot.lineText(lineStart);
		if (lineText.find_first_not_of(" \t") != std::string::npos) {
			const int column = editor.leadingIndentColumnForLine(lineStart);
			if (blockIndentColumn == 0 || column < blockIndentColumn) blockIndentColumn = column;
		}
		if (lineStart == lastLineStart) break;
	}
	int blockTargetColumn = blockIndentColumn;
	if (blockIndentColumn > 0) {
		if (indent)
			blockTargetColumn = resolvedEditFormatTabDisplayColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, blockIndentColumn);
		else {
			std::string normalizedFormatLine;
			int leftMargin = settings.leftMargin;
			int rightMargin = settings.rightMargin;
			static_cast<void>(normalizeEditFormatLine(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, normalizedFormatLine, &leftMargin, &rightMargin, nullptr));
			if (blockIndentColumn > rightMargin)
				blockTargetColumn = ((blockIndentColumn - 2) / clampEditFormatTabSize(settings.tabSize)) * clampEditFormatTabSize(settings.tabSize) + 1;
			else
				blockTargetColumn = prevResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, blockIndentColumn);
		}
	}
	const int blockIndentDelta = blockTargetColumn - blockIndentColumn;
	// Stage prefixes from the bottom so every range still addresses the snapshot.
	for (std::size_t lineStart = lastLineStart;; lineStart = snapshot.prevLine(lineStart)) {
		const std::string lineText = snapshot.lineText(lineStart);
		const std::size_t prefixBytes = lineText.find_first_not_of(" \t");
		if (prefixBytes != std::string::npos && blockIndentDelta != 0) {
			const int currentColumn = editor.leadingIndentColumnForLine(lineStart);
			const int targetColumn = std::max(1, currentColumn + blockIndentDelta);
			const std::string replacement = buildEditIndentFill(settings, 1, targetColumn, settings.tabExpand);
			if (lineText.compare(0, prefixBytes, replacement) != 0) {
				transaction.replace(MRTextBufferModel::Range(lineStart, lineStart + prefixBytes), replacement);
				blockEnd = blockEnd - prefixBytes + replacement.size();
			}
		}
		if (lineStart == firstLineStart) break;
	}
	if (!transaction.empty() && !editor.applyStagedTransaction(transaction, firstLineStart, firstLineStart, firstLineStart, true).applied()) {
		if (errorText != nullptr) *errorText = indent ? "Unable to indent line block." : "Unable to undent line block.";
		return false;
	}
	if (!setCommittedBlock(editor, MRFEBlockMode::Line, firstLineStart, blockEnd - 1)) return false;
	editor.setCursorOffset(firstLineStart);
	return true;
}

bool MRFEBlockOps::shiftCurrentStreamBlockToTab(MRFileEditor &editor, bool indent, std::string *errorText) {
	if (errorText != nullptr) errorText->clear();
	if (editor.isReadOnly()) {
		if (errorText != nullptr) *errorText = "Editor is read-only.";
		return false;
	}
	if (!hasVisibleBlock()) {
		if (errorText != nullptr) *errorText = "No visible block marked.";
		return false;
	}
	normalize(editor);
	if (mGeometry.mode != MRFEBlockMode::Stream) {
		if (errorText != nullptr) *errorText = "Stream block required.";
		return false;
	}
	if (mGeometry.rangeStart >= mGeometry.rangeEnd || !setCommittedBlock(editor, MRFEBlockMode::Line, mGeometry.rangeStart, mGeometry.rangeEnd - 1)) {
		if (errorText != nullptr) *errorText = "Unable to normalize stream block to lines.";
		return false;
	}
	return shiftCurrentLineBlockToTab(editor, indent, errorText);
}
