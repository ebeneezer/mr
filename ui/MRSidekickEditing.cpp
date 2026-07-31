#define Uses_TEvent
#define Uses_TGroup
#define Uses_TKeys
#define Uses_TProgram
#include <tvision/tv.h>

#include "MRSidekickEditor.hpp"
#include "MRSidekickInternal.hpp"
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

	if (replaceActivePlaceholder(std::string(1, ch))) return;
	line.insert(static_cast<std::size_t>(mCursorCol), 1, ch);
	++mCursorCol;
	adjustPlaceholdersAfterInsert(offset, 1);
	resizeSnippetSidekickForContent();
}

void MRSidekickEditor::insertTextAtCursor(const std::string &value) {
	const std::size_t offset = cursorOffset();
	std::string current;

	if (value.empty()) return;
	if (replaceActivePlaceholder(value)) return;
	current = text();
	if (offset > current.size()) return;
	current.insert(offset, value);
	adjustPlaceholdersAfterInsert(offset, value.size());
	setText(std::move(current));
	setCursorFromOffset(offset + value.size());
	resizeSnippetSidekickForContent();
}

void MRSidekickEditor::insertNewLine() {
	const std::size_t offset = cursorOffset();
	std::string &line = mLines[static_cast<std::size_t>(mCursorRow)];
	std::string tail = line.substr(static_cast<std::size_t>(mCursorCol));

	if (replaceActivePlaceholder("\n")) return;
	line.erase(static_cast<std::size_t>(mCursorCol));
	mLines.insert(mLines.begin() + mCursorRow + 1, tail);
	++mCursorRow;
	mCursorCol = 0;
	adjustPlaceholdersAfterInsert(offset, 1);
	resizeSnippetSidekickForContent();
}

void MRSidekickEditor::eraseBackward() {
	if (mCursorCol > 0) {
		const std::size_t eraseOffset = cursorOffset() - 1;
		std::string &line = mLines[static_cast<std::size_t>(mCursorRow)];
		line.erase(static_cast<std::size_t>(mCursorCol - 1), 1);
		--mCursorCol;
		adjustPlaceholdersAfterErase(eraseOffset, 1);
		return;
	}
	if (mCursorRow <= 0) return;
	const std::size_t eraseOffset = cursorOffset() - 1;
	const int previousLength = static_cast<int>(mLines[static_cast<std::size_t>(mCursorRow - 1)].size());
	mLines[static_cast<std::size_t>(mCursorRow - 1)] += mLines[static_cast<std::size_t>(mCursorRow)];
	mLines.erase(mLines.begin() + mCursorRow);
	--mCursorRow;
	mCursorCol = previousLength;
	adjustPlaceholdersAfterErase(eraseOffset, 1);
}

void MRSidekickEditor::eraseForward() {
	const std::size_t eraseOffset = cursorOffset();
	std::string &line = mLines[static_cast<std::size_t>(mCursorRow)];
	if (mCursorCol < static_cast<int>(line.size())) {
		line.erase(static_cast<std::size_t>(mCursorCol), 1);
		adjustPlaceholdersAfterErase(eraseOffset, 1);
		return;
	}
	if (mCursorRow + 1 >= static_cast<int>(mLines.size())) return;
	line += mLines[static_cast<std::size_t>(mCursorRow + 1)];
	mLines.erase(mLines.begin() + mCursorRow + 1);
	adjustPlaceholdersAfterErase(eraseOffset, 1);
}

void MRSidekickEditor::eraseWordBackward() {
	const std::string current = text();
	const std::size_t end = cursorOffset();
	const std::size_t start = snippetSidekickWordLeftOffset(current, end);
	std::string next = current;

	if (start >= end || end > next.size()) return;
	next.erase(start, end - start);
	adjustPlaceholdersAfterErase(start, end - start);
	setText(std::move(next));
	setCursorFromOffset(start);
}

void MRSidekickEditor::eraseWordForward() {
	const std::string current = text();
	const std::size_t start = cursorOffset();
	const std::size_t end = snippetSidekickWordRightOffset(current, start);
	std::string next = current;

	if (start >= end || end > next.size()) return;
	next.erase(start, end - start);
	adjustPlaceholdersAfterErase(start, end - start);
	setText(std::move(next));
	setCursorFromOffset(start);
}

void MRSidekickEditor::eraseToLineStart() {
	const std::size_t end = cursorOffset();
	const std::size_t start = end - static_cast<std::size_t>(std::max(0, mCursorCol));
	std::string current = text();

	if (start >= end || end > current.size()) return;
	current.erase(start, end - start);
	adjustPlaceholdersAfterErase(start, end - start);
	setText(std::move(current));
	setCursorFromOffset(start);
}

void MRSidekickEditor::eraseToLineEnd() {
	const std::size_t start = cursorOffset();
	const std::size_t length = mCursorRow >= 0 && mCursorRow < static_cast<int>(mLines.size()) ? mLines[static_cast<std::size_t>(mCursorRow)].size() - static_cast<std::size_t>(std::max(0, mCursorCol)) : 0;
	std::string current = text();

	if (length == 0 || start + length > current.size()) return;
	current.erase(start, length);
	adjustPlaceholdersAfterErase(start, length);
	setText(std::move(current));
	setCursorFromOffset(start);
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
	adjustPlaceholdersAfterErase(start, length);
	setText(std::move(current));
	setCursorFromOffset(std::min(start, text().size()));
}

bool MRSidekickEditor::replaceActivePlaceholder(const std::string &replacement) {
	std::vector<MRSidekickSpan> placeholders;
	std::string value;
	MRSidekickSpan active{};
	std::size_t offset = 0;
	std::size_t oldLength = 0;
	std::size_t newLength = 0;

	if (mPlaceholderIndex < 0 || static_cast<std::size_t>(mPlaceholderIndex) >= mPlaceholders.size()) return false;
	if (static_cast<std::size_t>(mPlaceholderIndex) < mPlaceholderTouched.size() && mPlaceholderTouched[static_cast<std::size_t>(mPlaceholderIndex)] != 0) return false;
	active = mPlaceholders[static_cast<std::size_t>(mPlaceholderIndex)];
	offset = cursorOffset();
	if (offset < active.start || offset > active.end) return false;
	value = text();
	if (active.start > value.size() || active.end > value.size() || active.end < active.start) return false;
	oldLength = active.end - active.start;
	newLength = replacement.size();
	value.replace(active.start, oldLength, replacement);
	placeholders = mPlaceholders;
	for (std::size_t index = 0; index < placeholders.size(); ++index) {
		MRSidekickSpan &span = placeholders[index];

		if (index == static_cast<std::size_t>(mPlaceholderIndex)) {
			span.end = span.start + newLength;
		} else if (span.start >= active.end) {
			if (newLength >= oldLength) {
				span.start += newLength - oldLength;
				span.end += newLength - oldLength;
			} else {
				span.start -= std::min(span.start, oldLength - newLength);
				span.end -= std::min(span.end, oldLength - newLength);
			}
		}
	}
	mPlaceholders = std::move(placeholders);
	if (static_cast<std::size_t>(mPlaceholderIndex) < mPlaceholderTouched.size()) mPlaceholderTouched[static_cast<std::size_t>(mPlaceholderIndex)] = 1;
	setText(std::move(value));
	mPlaceholderEndEdge = true;
	setCursorFromOffset(active.start + newLength);
	resizeSnippetSidekickForContent();
	return true;
}

bool MRSidekickEditor::handleRuntimeKeymap(TEvent &event) {
	MRKeymapToken token(MRKeymapBaseKey::Esc, 0);

	if (!mSnippetSidekick || mReadOnly || event.what != evKeyDown) return false;
	if (!mrKeymapTokenFromEvent(event.keyDown.keyCode, event.keyDown.controlKeyState, token)) return false;
	const MRKeymapResolver::Result result = runtimeKeymapResolver().resolve(MRKeymapContext::Edit, token);
	switch (result.kind) {
		case MRKeymapResolver::ResultKind::NoMatch:
			return false;
		case MRKeymapResolver::ResultKind::Pending:
		case MRKeymapResolver::ResultKind::Invalid:
		case MRKeymapResolver::ResultKind::Aborted:
			clearEvent(event);
			return true;
		case MRKeymapResolver::ResultKind::Matched:
			clearEvent(event);
			if (result.target.type != MRKeymapBindingType::Action) return true;
			return handleSnippetSidekickAction(result.target.target);
	}
	return false;
}

bool MRSidekickEditor::handleSnippetSidekickAction(const std::string &actionId) {
	SnippetSidekickAction action = SnippetSidekickAction::CursorLeft;

	if (!snippetSidekickActionFromId(actionId, action)) return false;
	switch (action) {
		case SnippetSidekickAction::CursorLeft:
			moveLeft();
			return true;
		case SnippetSidekickAction::CursorRight:
			moveRight();
			return true;
		case SnippetSidekickAction::CursorUp:
			moveUp();
			return true;
		case SnippetSidekickAction::CursorDown:
			moveDown();
			return true;
		case SnippetSidekickAction::CursorHome:
			moveLineStart();
			return true;
		case SnippetSidekickAction::CursorEnd:
			moveLineEnd();
			return true;
		case SnippetSidekickAction::CursorWordLeft:
			moveWordLeft();
			return true;
		case SnippetSidekickAction::CursorWordRight:
			moveWordRight();
			return true;
		case SnippetSidekickAction::DeleteBackwardChar:
			eraseBackward();
			return true;
		case SnippetSidekickAction::DeleteForwardChar:
			eraseForward();
			return true;
		case SnippetSidekickAction::DeleteBackwardWord:
			eraseWordBackward();
			return true;
		case SnippetSidekickAction::DeleteForwardWord:
			eraseWordForward();
			return true;
		case SnippetSidekickAction::DeleteBackwardToHome:
			eraseToLineStart();
			return true;
		case SnippetSidekickAction::DeleteToEndOfLine:
			eraseToLineEnd();
			return true;
		case SnippetSidekickAction::DeleteLine:
			eraseLine();
			return true;
		case SnippetSidekickAction::LoadBlockFromFile:
			return loadBlockFromFileIntoSnippetSidekick();
		case SnippetSidekickAction::PlaceholderNext:
			moveToPlaceholder(1);
			return true;
		case SnippetSidekickAction::PlaceholderPrevious:
			moveToPlaceholder(-1);
			return true;
	}
	return false;
}

bool MRSidekickEditor::loadBlockFromFileIntoSnippetSidekick() {
	char fileName[MAXPATH] = {0};
	std::string resolvedPath;
	std::string content;
	std::string errorText;

	if (!promptForPath(MRDialogHistoryScope::BlockLoad, "LOAD BLOCK", fileName, sizeof(fileName))) return true;
	if (!resolveReadableExistingPath(MRDialogHistoryScope::BlockLoad, fileName, resolvedPath)) return true;
	if (!readTextFile(resolvedPath, content, errorText)) {
		mrLogMessage(errorText.empty() ? "Snippet SideKick block load failed." : errorText);
		return true;
	}
	insertTextAtCursor(expandSidekickTabs(content));
	rememberLoadDialogPath(MRDialogHistoryScope::BlockLoad, resolvedPath.c_str());
	return true;
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
	setCursorFromOffset(snippetSidekickWordLeftOffset(text(), cursorOffset()));
}

void MRSidekickEditor::moveWordRight() {
	setCursorFromOffset(snippetSidekickWordRightOffset(text(), cursorOffset()));
}

void MRSidekickEditor::moveToPlaceholder(int direction) {
	if (mPlaceholders.empty()) return;
	if (mPlaceholderIndex < 0) {
		mPlaceholderIndex = direction >= 0 ? 0 : static_cast<int>(mPlaceholders.size()) - 1;
	} else if (direction >= 0) {
		mPlaceholderIndex = (mPlaceholderIndex + 1) % static_cast<int>(mPlaceholders.size());
	} else {
		mPlaceholderIndex = (mPlaceholderIndex + static_cast<int>(mPlaceholders.size()) - 1) % static_cast<int>(mPlaceholders.size());
	}
	mPlaceholderEndEdge = false;
	setCursorFromActivePlaceholder();
}

void MRSidekickEditor::setCursorFromActivePlaceholder() {
	const MRSidekickSpan &placeholder = mPlaceholders[static_cast<std::size_t>(mPlaceholderIndex)];

	setCursorFromOffset(mPlaceholderEndEdge ? placeholder.end : placeholder.start);
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

void MRSidekickEditor::adjustPlaceholdersAfterInsert(std::size_t offset, std::size_t length) {
	for (MRSidekickSpan &span : mPlaceholders) {
		if (offset <= span.start) {
			span.start += length;
			span.end += length;
		} else if (offset < span.end)
			span.end += length;
	}
}

void MRSidekickEditor::adjustPlaceholdersAfterErase(std::size_t offset, std::size_t length) {
	const std::size_t eraseEnd = offset + length;

	for (MRSidekickSpan &span : mPlaceholders) {
		if (eraseEnd <= span.start) {
			span.start -= std::min(length, span.start);
			span.end -= std::min(length, span.end);
		} else if (offset < span.end) {
			const std::size_t overlapStart = std::max(offset, span.start);
			const std::size_t overlapEnd = std::min(eraseEnd, span.end);
			span.end -= overlapEnd > overlapStart ? overlapEnd - overlapStart : 0;
			if (offset < span.start) span.start = offset;
			if (span.end < span.start) span.end = span.start;
		}
	}
}

void MRSidekickEditor::resizeSnippetSidekickForContent() {
	if (!mSnippetSidekick || mReadOnly || owner == nullptr || TProgram::deskTop == nullptr) return;

	TRect desktop = TProgram::deskTop->getExtent();
	TRect bounds = owner->getBounds();
	const int desktopWidth = std::max(1, desktop.b.x - desktop.a.x);
	const int desktopHeight = std::max(1, desktop.b.y - desktop.a.y);
	const int maxWidth = std::max(32, desktopWidth - 2);
	const int maxHeight = std::max(6, desktopHeight - 2);
	const int wantedWidth = std::clamp(sidekickMaxLineLength(mLines) + 8, 32, maxWidth);
	const int wantedHeight = std::clamp<int>(static_cast<int>(mLines.size()) + 6, 10, maxHeight);
	const int currentWidth = std::max(1, bounds.b.x - bounds.a.x);
	const int currentHeight = std::max(1, bounds.b.y - bounds.a.y);
	const int newWidth = std::max(currentWidth, wantedWidth);
	const int newHeight = std::max(currentHeight, wantedHeight);

	if (newWidth == currentWidth && newHeight == currentHeight) return;
	bounds.b.x = bounds.a.x + newWidth;
	bounds.b.y = bounds.a.y + newHeight;
	if (bounds.b.x > desktop.b.x) bounds.move(desktop.b.x - bounds.b.x, 0);
	if (bounds.a.x < desktop.a.x) bounds.move(desktop.a.x - bounds.a.x, 0);
	if (bounds.b.y > desktop.b.y) bounds.move(0, desktop.b.y - bounds.b.y);
	if (bounds.a.y < desktop.a.y) bounds.move(0, desktop.a.y - bounds.a.y);
	owner->locate(bounds);
	TRect editorBounds(1, 1, std::max<short>(2, owner->size.x - 1), std::max<short>(2, owner->size.y - 3));
	locate(editorBounds);
	owner->drawView();
}

void MRSidekickEditor::clampCursor() noexcept {
	if (mLines.empty()) mLines.push_back(std::string());
	mCursorRow = std::clamp(mCursorRow, 0, static_cast<int>(mLines.size()) - 1);
	mCursorCol = std::clamp(mCursorCol, 0, static_cast<int>(mLines[static_cast<std::size_t>(mCursorRow)].size()));
}
