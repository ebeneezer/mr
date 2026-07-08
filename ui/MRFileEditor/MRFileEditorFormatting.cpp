#include "MRFileEditor.hpp"
#include "../../config/settings/MRSettingsStorage.hpp"
#include "../MREditWindow.hpp"

#include <algorithm>
#include <string_view>

namespace {
std::size_t leadingWhitespaceBytes(std::string_view text) noexcept {
	std::size_t bytes = 0;

	while (bytes < text.size() && (text[bytes] == ' ' || text[bytes] == '\t')) ++bytes;
	return bytes;
}

bool containsNonWhitespace(std::string_view text) noexcept {
	for (char ch : text)
		if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') return true;
	return false;
}
} // namespace

bool MRFileEditor::formatParagraph(int rightMargin) {
	return formatParagraph(effectiveEditSetupSettings().leftMargin, rightMargin);
}

std::string MRFileEditor::buildFormattedParagraphText(std::string_view paragraphText, int leftMargin, int rightMargin) const {
	return MRTextFormatting::formatParagraphText(paragraphText, leftMargin, rightMargin);
}

bool MRFileEditor::formatParagraph(int leftMargin, int rightMargin) {
	if (mReadOnly) return false;

	std::size_t start = mBufferModel.cursor();
	std::size_t end = start;
	while (start > 0) {
		std::size_t prevLineStart = mBufferModel.lineStart(mBufferModel.prevLine(start));
		if (isBlankString(mBufferModel.lineText(prevLineStart))) break;
		start = prevLineStart;
	}
	while (end < mBufferModel.length()) {
		std::size_t nextLineStart = mBufferModel.nextLine(end);
		if (isBlankString(mBufferModel.lineText(end))) break;
		end = nextLineStart;
	}
	if (start == end) return true;

	std::string paragraphText;
	paragraphText.reserve(end - start);
	std::size_t current = start;
	while (current < end) {
		std::string chunk = mBufferModel.document().lineText(current);
		if (!paragraphText.empty()) paragraphText.push_back('\n');
		paragraphText += chunk;
		current = mBufferModel.document().nextLine(current);
	}
	std::string formattedText = buildFormattedParagraphText(paragraphText, leftMargin, rightMargin);
	if (formattedText.empty()) return true;

	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "format-paragraph");
	transaction.replace(MRTextBufferModel::Range(start, end), formattedText);
	return applyStagedTransaction(transaction, start, start, start, true).applied();
}

bool MRFileEditor::formatDocument(int leftMargin, int rightMargin) {
	std::string formattedText;
	const std::size_t length = mBufferModel.length();
	const std::size_t cursor = mBufferModel.cursor();
	std::size_t current = 0;

	if (mReadOnly) return false;
	while (current < length) {
		if (isBlankString(mBufferModel.lineText(current))) {
			formattedText.push_back('\n');
			current = mBufferModel.nextLine(current);
			continue;
		}
		std::string paragraphText;
		const std::size_t paragraphStart = current;
		std::size_t paragraphEnd = current;
		while (paragraphEnd < length && !isBlankString(mBufferModel.lineText(paragraphEnd))) {
			if (!paragraphText.empty()) paragraphText.push_back('\n');
			paragraphText += mBufferModel.document().lineText(paragraphEnd);
			paragraphEnd = mBufferModel.document().nextLine(paragraphEnd);
		}
		if (!formattedText.empty() && formattedText.back() != '\n') formattedText.push_back('\n');
		formattedText += buildFormattedParagraphText(paragraphText, leftMargin, rightMargin);
		current = paragraphEnd;
		if (current == paragraphStart) break;
	}
	return replaceWholeBuffer(formattedText, std::min(cursor, formattedText.size()));
}

bool MRFileEditor::justifyParagraph(int leftMargin, int rightMargin) {
	if (mReadOnly) return false;

	std::size_t start = mBufferModel.cursor();
	std::size_t end = start;
	std::string paragraphText;
	while (start > 0) {
		std::size_t prevLineStart = mBufferModel.lineStart(mBufferModel.prevLine(start));
		if (isBlankString(mBufferModel.lineText(prevLineStart))) break;
		start = prevLineStart;
	}
	while (end < mBufferModel.length()) {
		std::size_t nextLineStart = mBufferModel.nextLine(end);
		if (isBlankString(mBufferModel.lineText(end))) break;
		end = nextLineStart;
	}
	if (start == end) return true;
	paragraphText.reserve(end - start);
	for (std::size_t current = start; current < end; current = mBufferModel.document().nextLine(current)) {
		const std::string chunk = mBufferModel.document().lineText(current);

		if (!paragraphText.empty()) paragraphText.push_back('\n');
		paragraphText += chunk;
	}
	std::string justifiedText = MRTextFormatting::justifyParagraphText(paragraphText, leftMargin, rightMargin);
	if (justifiedText.empty()) return true;
	return replaceRangeAndSelect(static_cast<uint>(start), static_cast<uint>(end), justifiedText.data(), static_cast<uint>(justifiedText.size()));
}

bool MRFileEditor::prettifyBlockOrFile() {
	const MREditSetupSettings settings = effectiveEditSetupSettings();
	const std::string indentStyle = upperAscii(settings.indentStyle);
	const std::size_t length = mBufferModel.length();
	MRSyntaxLanguage operationLanguage = mBufferModel.language();
	std::string operationIndentStyle = indentStyle;
	std::size_t rangeStart = 0;
	std::size_t rangeEnd = length;

	if (mReadOnly) return false;
	if (operationLanguage != MRSyntaxLanguage::PlainText && operationIndentStyle != "SMART") operationIndentStyle = "SMART";
	if (operationIndentStyle != "AUTOMATIC" && operationIndentStyle != "SMART") return true;
	if (hasTextSelection()) {
		rangeStart = selectionStartOffset();
		rangeEnd = selectionEndOffset();
	} else {
		MREditWindow *window = dynamic_cast<MREditWindow *>(owner);
		if (window != nullptr && window->getEditor() == this && window->hasBlock()) {
			const int firstBlockLine = window->blockLine1();
			const int lastBlockLine = window->blockLine2();
			const std::size_t lineCount = mBufferModel.lineCount();
			if (firstBlockLine > 0 && lastBlockLine >= firstBlockLine && lineCount > 0) {
				const std::size_t firstLineIndex = std::min<std::size_t>(static_cast<std::size_t>(firstBlockLine - 1), lineCount - 1);
				const std::size_t lastLineIndex = std::min<std::size_t>(static_cast<std::size_t>(lastBlockLine - 1), lineCount - 1);
				rangeStart = mBufferModel.lineStartByIndex(firstLineIndex);
				rangeEnd = nextLineOffset(mBufferModel.lineStartByIndex(lastLineIndex));
			}
		}
	}
	if (length == 0) return true;

	rangeStart = std::min(rangeStart, length);
	rangeEnd = std::min(std::max(rangeEnd, rangeStart), length);
	const std::size_t firstLineStart = lineStartOffset(rangeStart);
	std::size_t lastProbe = rangeEnd;
	if (lastProbe > firstLineStart && lastProbe > 0) --lastProbe;
	else
		lastProbe = firstLineStart;
	const std::size_t lastLineStart = lineStartOffset(std::min(lastProbe, length));
	std::size_t editEnd = nextLineOffset(lastLineStart);
	if (editEnd <= firstLineStart) editEnd = length;

	const bool wholeFile = firstLineStart == 0 && editEnd >= length;
	bool haveNextSmartColumn = false;
	bool firstNonBlankLine = true;
	int nextSmartColumn = 1;
	std::vector<std::size_t> formattedLineStarts;
	std::vector<int> formattedColumns;
	std::string originalText;
	std::string formattedText;

	originalText.reserve(editEnd - firstLineStart);
	formattedText.reserve(editEnd - firstLineStart);
	for (std::size_t pos = firstLineStart; pos < editEnd; ++pos)
		originalText.push_back(charAtOffset(pos));

	for (std::size_t lineStart = firstLineStart; lineStart < editEnd;) {
		const std::size_t nextLineStart = nextLineOffset(lineStart);
		const std::string lineText = mBufferModel.lineText(lineStart);
		const bool hasContent = containsNonWhitespace(lineText);
		const std::size_t leadingBytes = leadingWhitespaceBytes(lineText);
		const std::size_t lineBodyEnd = std::min(lineStart + lineText.size(), nextLineStart);
		std::string replacement;
		std::string formattedLine;

		if (hasContent) {
			int targetColumn = leadingIndentColumnForLine(lineStart);
			if (operationIndentStyle == "SMART") {
				if (haveNextSmartColumn)
					targetColumn = nextSmartColumn;
				else if (firstNonBlankLine && wholeFile)
					targetColumn = 1;
				const int dedentColumn = smartDedentTargetColumnForLine(lineStart, targetColumn, operationLanguage, false, formattedLineStarts, formattedColumns);
				if (dedentColumn > 0) targetColumn = dedentColumn;
			}
			replacement = buildEditIndentFill(settings, 1, std::max(1, targetColumn), settings.tabExpand);
			formattedLineStarts.push_back(lineStart);
			formattedColumns.push_back(std::max(1, targetColumn));
			if (operationIndentStyle == "SMART") {
				nextSmartColumn = smartIndentTargetColumnForContext(lineStart, lineText.size(), targetColumn, operationLanguage);
				haveNextSmartColumn = true;
				firstNonBlankLine = false;
			}
		}

		formattedLine = replacement;
		formattedLine.append(lineText.data() + leadingBytes, lineText.size() - leadingBytes);
		formattedText += formattedLine;
		for (std::size_t pos = lineBodyEnd; pos < nextLineStart && pos < editEnd; ++pos)
			formattedText.push_back(charAtOffset(pos));
		lineStart = nextLineStart;
	}
	if (formattedText == originalText) return true;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "prettify-block-or-file");
	transaction.replace(MRTextBufferModel::Range(firstLineStart, editEnd), formattedText);
	return applyStagedTransaction(transaction, cursorOffset(), selectionStartOffset(), selectionEndOffset(), true).applied();
}

void MRFileEditor::effectiveFormatMargins(const MREditSetupSettings &settings, int &leftMargin, int &rightMargin) const noexcept {
	MRTextFormatting::effectiveMargins(settings, leftMargin, rightMargin);
}

bool MRFileEditor::persistVisibleEditSetupSettings(const MREditSetupSettings &settings, const std::string &errorPrefix) {
	MREditSetupSettings previousSettings = configuredEditSetupSettings();
	MRSettingsWriteReport writeReport;
	std::string errorText;

	if (!setConfiguredEditSetupSettings(settings, &errorText)) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorPrefix + errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return false;
	}
	if (!persistConfiguredSettingsSnapshot(&errorText, &writeReport)) {
		static_cast<void>(setConfiguredEditSetupSettings(previousSettings, nullptr));
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorPrefix + errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return false;
	}
	return true;
}

bool MRFileEditor::previewVisibleEditSetupSettings(const MREditSetupSettings &settings, const std::string &errorPrefix) {
	std::string errorText;

	if (!setConfiguredEditSetupSettings(settings, &errorText)) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorPrefix + errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return false;
	}
	refreshConfiguredVisualSettings();
	return true;
}

bool MRFileEditor::finalizeVisibleEditSetupPreview(const MREditSetupSettings &previousSettings, const std::string &errorPrefix) {
	MRSettingsWriteReport writeReport;
	std::string errorText;

	if (persistConfiguredSettingsSnapshot(&errorText, &writeReport)) return true;
	static_cast<void>(setConfiguredEditSetupSettings(previousSettings, nullptr));
	refreshConfiguredVisualSettings();
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorPrefix + errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
	return false;
}


bool MRFileEditor::wrapCurrentLineOnce(int leftMargin, int rightMargin) {
	const std::size_t cursor = cursorOffset();
	const std::size_t lineStart = lineStartOffset(cursor);
	const std::size_t lineEnd = lineEndOffset(cursor);
	const int safeLeftMargin = std::max(1, leftMargin);
	const int safeRightMargin = std::max(safeLeftMargin, rightMargin);
	const int lineWidth = charColumn(lineStart, lineEnd);
	const std::string indent(static_cast<std::size_t>(safeLeftMargin - 1), ' ');
	const std::string replacement = "\n" + indent;
	std::size_t limitOffset = std::min(charPtrOffset(lineStart, safeRightMargin), lineEnd);
	std::size_t replaceStart = limitOffset;
	std::size_t replaceEnd = limitOffset;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "live-word-wrap-line");
	std::size_t newCursor = cursor;

	if (lineWidth <= safeRightMargin) return false;
	for (std::size_t probe = limitOffset; probe > lineStart; probe = prevCharOffset(probe)) {
		const std::size_t candidate = prevCharOffset(probe);
		const char ch = charAtOffset(candidate);

		if (ch != ' ' && ch != '\t') continue;
		replaceStart = candidate;
		replaceEnd = probe;
		while (replaceStart > lineStart) {
			const std::size_t previous = prevCharOffset(replaceStart);
			const char previousChar = charAtOffset(previous);
			if (previousChar != ' ' && previousChar != '\t') break;
			replaceStart = previous;
		}
		while (replaceEnd < lineEnd) {
			const char nextChar = charAtOffset(replaceEnd);
			if (nextChar != ' ' && nextChar != '\t') break;
			replaceEnd = nextCharOffset(replaceEnd);
		}
		break;
	}
	transaction.replace(MRTextBufferModel::Range(replaceStart, replaceEnd), replacement);
	if (cursor <= replaceStart) newCursor = cursor;
	else if (cursor >= replaceEnd)
		newCursor = cursor - (replaceEnd - replaceStart) + replacement.size();
	else
		newCursor = replaceStart + replacement.size();
	return applyStagedTransaction(transaction, newCursor, newCursor, newCursor, true).applied();
}

void MRFileEditor::applyLiveWordWrapAfterTextInput() {
	MREditSetupSettings settings = effectiveEditSetupSettings();
	int leftMargin = 1;
	int rightMargin = 78;

	if (mWordWrapSuppressed) settings.wordWrap = false;
	if (mReadOnly || !settings.wordWrap) return;
	effectiveFormatMargins(settings, leftMargin, rightMargin);
	for (int wraps = 0; wraps < 64; ++wraps)
		if (!wrapCurrentLineOnce(leftMargin, rightMargin)) break;
}
