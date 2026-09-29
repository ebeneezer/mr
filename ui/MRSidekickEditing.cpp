#define Uses_TEvent
#define Uses_TGroup
#define Uses_TKeys
#define Uses_TProgram
#include <tvision/tv.h>

#include "MRSidekickEditor.hpp"
#include "MRSidekickInternal.hpp"
#include "MRSnippet.hpp"
#include "MREditWindow.hpp"
#include "MRFileEditor/MRFileEditor.hpp"
#include "../app/MREditorApp.hpp"
#include "../app/MRCommands.hpp"
#include "../app/commands/MRFileCommands.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../keymap/MRKeymapContext.hpp"
#include "../keymap/MRKeymapResolver.hpp"
#include "../keymap/MRKeymapToken.hpp"

#include <algorithm>

using namespace mr::sidekick_internal;

void MRSidekickEditor::insertChar(char ch) {
	const std::size_t offset = cursorOffset();
	std::string &line = mLines[static_cast<std::size_t>(mCursorRow)];

	if (MRSnippet::instance().replacePlaceholder(*this, std::string(1, ch))) return;
	line.insert(static_cast<std::size_t>(mCursorCol), 1, ch);
	++mCursorCol;
	MRSnippet::instance().adjustAfterInsert(*this, offset, 1);
	MRSnippet::instance().resizeForContent(*this);
}

void MRSidekickEditor::insertTextAtCursor(const std::string &value) {
	const std::size_t offset = cursorOffset();
	std::string current;

	if (value.empty()) return;
	if (MRSnippet::instance().replacePlaceholder(*this, value)) return;
	current = text();
	if (offset > current.size()) return;
	current.insert(offset, value);
	MRSnippet::instance().adjustAfterInsert(*this, offset, value.size());
	setText(std::move(current));
	setCursorFromOffset(offset + value.size());
	MRSnippet::instance().resizeForContent(*this);
}

void MRSidekickEditor::insertNewLine() {
	const std::size_t offset = cursorOffset();
	std::string &line = mLines[static_cast<std::size_t>(mCursorRow)];
	std::string tail = line.substr(static_cast<std::size_t>(mCursorCol));

	if (MRSnippet::instance().replacePlaceholder(*this, "\n")) return;
	line.erase(static_cast<std::size_t>(mCursorCol));
	mLines.insert(mLines.begin() + mCursorRow + 1, tail);
	++mCursorRow;
	mCursorCol = 0;
	MRSnippet::instance().adjustAfterInsert(*this, offset, 1);
	MRSnippet::instance().resizeForContent(*this);
}

void MRSidekickEditor::eraseBackward() {
	if (mCursorCol > 0) {
		const std::size_t eraseOffset = cursorOffset() - 1;
		std::string &line = mLines[static_cast<std::size_t>(mCursorRow)];
		line.erase(static_cast<std::size_t>(mCursorCol - 1), 1);
		--mCursorCol;
		MRSnippet::instance().adjustAfterErase(*this, eraseOffset, 1);
		MRSnippet::instance().resizeForContent(*this);
		return;
	}
	if (mCursorRow <= 0) return;
	const std::size_t eraseOffset = cursorOffset() - 1;
	const int previousLength = static_cast<int>(mLines[static_cast<std::size_t>(mCursorRow - 1)].size());
	mLines[static_cast<std::size_t>(mCursorRow - 1)] += mLines[static_cast<std::size_t>(mCursorRow)];
	mLines.erase(mLines.begin() + mCursorRow);
	--mCursorRow;
	mCursorCol = previousLength;
	MRSnippet::instance().adjustAfterErase(*this, eraseOffset, 1);
	MRSnippet::instance().resizeForContent(*this);
}

void MRSidekickEditor::eraseForward() {
	const std::size_t eraseOffset = cursorOffset();
	std::string &line = mLines[static_cast<std::size_t>(mCursorRow)];
	if (mCursorCol < static_cast<int>(line.size())) {
		line.erase(static_cast<std::size_t>(mCursorCol), 1);
		MRSnippet::instance().adjustAfterErase(*this, eraseOffset, 1);
		MRSnippet::instance().resizeForContent(*this);
		return;
	}
	if (mCursorRow + 1 >= static_cast<int>(mLines.size())) return;
	line += mLines[static_cast<std::size_t>(mCursorRow + 1)];
	mLines.erase(mLines.begin() + mCursorRow + 1);
	MRSnippet::instance().adjustAfterErase(*this, eraseOffset, 1);
	MRSnippet::instance().resizeForContent(*this);
}

void MRSidekickEditor::eraseWordBackward() {
	const std::string current = text();
	const std::size_t end = cursorOffset();
	const std::size_t start = MRSnippet::instance().wordLeftOffset(current, end);
	std::string next = current;

	if (start >= end || end > next.size()) return;
	next.erase(start, end - start);
	MRSnippet::instance().adjustAfterErase(*this, start, end - start);
	setText(std::move(next));
	setCursorFromOffset(start);
	MRSnippet::instance().resizeForContent(*this);
}

void MRSidekickEditor::eraseWordForward() {
	const std::string current = text();
	const std::size_t start = cursorOffset();
	const std::size_t end = MRSnippet::instance().wordRightOffset(current, start);
	std::string next = current;

	if (start >= end || end > next.size()) return;
	next.erase(start, end - start);
	MRSnippet::instance().adjustAfterErase(*this, start, end - start);
	setText(std::move(next));
	setCursorFromOffset(start);
	MRSnippet::instance().resizeForContent(*this);
}

void MRSidekickEditor::eraseToLineStart() {
	const std::size_t end = cursorOffset();
	const std::size_t start = end - static_cast<std::size_t>(std::max(0, mCursorCol));
	std::string current = text();

	if (start >= end || end > current.size()) return;
	current.erase(start, end - start);
	MRSnippet::instance().adjustAfterErase(*this, start, end - start);
	setText(std::move(current));
	setCursorFromOffset(start);
	MRSnippet::instance().resizeForContent(*this);
}

void MRSidekickEditor::eraseToLineEnd() {
	const std::size_t start = cursorOffset();
	const std::size_t length = mCursorRow >= 0 && mCursorRow < static_cast<int>(mLines.size()) ? mLines[static_cast<std::size_t>(mCursorRow)].size() - static_cast<std::size_t>(std::max(0, mCursorCol)) : 0;
	std::string current = text();

	if (length == 0 || start + length > current.size()) return;
	current.erase(start, length);
	MRSnippet::instance().adjustAfterErase(*this, start, length);
	setText(std::move(current));
	setCursorFromOffset(start);
	MRSnippet::instance().resizeForContent(*this);
}

void MRSidekickEditor::eraseLine() {
	std::size_t start = cursorOffset() - static_cast<std::size_t>(std::max(0, mCursorCol));
	std::size_t length = mCursorRow >= 0 && mCursorRow < static_cast<int>(mLines.size()) ? mLines[static_cast<std::size_t>(mCursorRow)].size() : 0;
	std::string current = text();

	if (mLines.size() > 1 && start + length < current.size()) ++length;
	else if (mLines.size() > 1 && start > 0) {
		--start;
		++length;
	}
	if (length == 0 || start + length > current.size()) return;
	current.erase(start, length);
	MRSnippet::instance().adjustAfterErase(*this, start, length);
	setText(std::move(current));
	setCursorFromOffset(std::min(start, text().size()));
	MRSnippet::instance().resizeForContent(*this);
}

void MRSidekickEditor::moveLeft() {
	if (mCursorCol > 0) {
		--mCursorCol;
		return;
	}
	if (mCursorRow > 0) {
		--mCursorRow;
		mCursorCol = static_cast<int>(mLines[static_cast<std::size_t>(mCursorRow)].size());
	}
}

void MRSidekickEditor::moveRight() {
	if (mCursorCol < static_cast<int>(mLines[static_cast<std::size_t>(mCursorRow)].size())) {
		++mCursorCol;
		return;
	}
	if (mCursorRow + 1 < static_cast<int>(mLines.size())) {
		++mCursorRow;
		mCursorCol = 0;
	}
}

void MRSidekickEditor::moveUp() {
	if (mCursorRow > 0) --mCursorRow;
	clampCursor();
}

void MRSidekickEditor::moveDown() {
	if (mCursorRow + 1 < static_cast<int>(mLines.size())) ++mCursorRow;
	clampCursor();
}

void MRSidekickEditor::moveLineStart() noexcept {
	mCursorCol = 0;
}

void MRSidekickEditor::moveLineEnd() noexcept {
	if (mCursorRow >= 0 && mCursorRow < static_cast<int>(mLines.size())) mCursorCol = static_cast<int>(mLines[static_cast<std::size_t>(mCursorRow)].size());
}

void MRSidekickEditor::moveWordLeft() {
	setCursorFromOffset(MRSnippet::instance().wordLeftOffset(text(), cursorOffset()));
}

void MRSidekickEditor::moveWordRight() {
	setCursorFromOffset(MRSnippet::instance().wordRightOffset(text(), cursorOffset()));
}

void MRSidekickEditor::setCursorFromOffset(std::size_t offset) {
	std::size_t lineStart = 0;

	for (std::size_t row = 0; row < mLines.size(); ++row) {
		const std::size_t lineLength = mLines[row].size();
		if (offset <= lineStart + lineLength) {
			mCursorRow = static_cast<int>(row);
			mCursorCol = static_cast<int>(offset - lineStart);
			clampCursor();
			return;
		}
		lineStart += lineLength + 1;
	}
	mCursorRow = static_cast<int>(mLines.size()) - 1;
	mCursorCol = static_cast<int>(mLines.back().size());
}

std::size_t MRSidekickEditor::cursorOffset() const noexcept {
	std::size_t offset = 0;

	for (int row = 0; row < mCursorRow && row < static_cast<int>(mLines.size()); ++row)
		offset += mLines[static_cast<std::size_t>(row)].size() + 1;
	return offset + static_cast<std::size_t>(std::max(0, mCursorCol));
}

void MRSidekickEditor::clampCursor() noexcept {
	if (mLines.empty()) mLines.push_back(std::string());
	mCursorRow = std::clamp(mCursorRow, 0, static_cast<int>(mLines.size()) - 1);
	mCursorCol = std::clamp(mCursorCol, 0, static_cast<int>(mLines[static_cast<std::size_t>(mCursorRow)].size()));
}
