#define Uses_TDrawBuffer
#define Uses_TDeskTop
#define Uses_TButton
#define Uses_TDialog
#define Uses_TEvent
#define Uses_TGroup
#define Uses_TKeys
#define Uses_TProgram
#define Uses_TView
#define Uses_TWindow
#include <tvision/tv.h>

#include "MRSnippet.hpp"
#include "MRSidekickInternal.hpp"
#include "MREditWindow.hpp"
#include "MRFrame.hpp"
#include "MRFileEditor/MRFileEditor.hpp"
#include "MRWindowSupport.hpp"
#include "../app/MREditorApp.hpp"
#include "../app/MRCommands.hpp"
#include "../app/MRHelpTopics.generated.hpp"
#include "../app/commands/MRFileCommands.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../keymap/MRKeymapContext.hpp"
#include "../keymap/MRKeymapResolver.hpp"
#include "../keymap/MRKeymapToken.hpp"
#include "../mrmac/vm/MRVMSnippet.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

using mr::sidekick_internal::expandSidekickTabs;
using mr::sidekick_internal::sidekickColor;
using mr::sidekick_internal::sidekickMaxLineLength;
using mr::sidekick_internal::splitLines;

MRSnippet &MRSnippet::instance() noexcept {
	static MRSnippet snippet;
	return snippet;
}

TFrame *MRSnippet::frame(TRect bounds) {
	return new MRFrame(bounds);
}

class MRSnippet::HintGuard {
  public:
	HintGuard() {
		mrSetSnippetSidekickHintsActive(true);
	}

	~HintGuard() {
		mrSetSnippetSidekickHintsActive(false);
	}
};

class MRSnippet::HelpButton final : public TButton {
  public:
	explicit HelpButton(const TRect &bounds) noexcept : TButton(bounds, "~H~elp", cmHelp, bfNormal) {
	}

	void draw() override {
		TAttrPair color = getColor(0x0501);
		if ((state & sfDisabled) != 0) color = getColor(0x0404);
		else if ((state & sfActive) != 0) {
			if ((state & sfSelected) != 0) color = getColor(0x0703);
			else if (amDefault) color = getColor(0x0602);
		}
		TDrawBuffer buffer;
		buffer.moveChar(0, ' ', color[0], size.x);
		buffer.moveCStr(std::max(0, (size.x - cstrlen(title)) / 2), title, color, size.x);
		writeLine(0, 0, size.x, 1, buffer);
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evMouseDown) {
			const TRect hit = getExtent();
			bool inside = false;
			do {
				inside = hit.contains(makeLocal(event.mouse.where));
			} while (mouseEvent(event, evMouseMove));
			if (inside && (state & sfDisabled) == 0) press();
			clearEvent(event);
			return;
		}
		if (event.what == evKeyDown) {
			const char hot = hotKey(title);
			const bool activated = event.keyDown.keyCode != 0 &&
			                       (event.keyDown.keyCode == getAltCode(hot) ||
			                        (owner->phase == phPostProcess && hot != 0 && hot == std::toupper(static_cast<unsigned char>(event.keyDown.charScan.charCode))) ||
			                        ((state & sfFocused) != 0 && event.keyDown.charScan.charCode == ' '));
			if (activated) {
				if ((state & sfDisabled) == 0) press();
				clearEvent(event);
				return;
			}
		}
		if (event.what == evBroadcast && event.message.command == cmDefault && amDefault && (state & sfDisabled) == 0) {
			press();
			clearEvent(event);
			return;
		}
		TButton::handleEvent(event);
	}
};

class MRSnippet::Dialog : public TDialog {
  public:
	Dialog(const TRect &bounds, int parentBufferId, std::size_t replaceStart, std::size_t replaceEnd, const std::string &text, const std::string &title, const std::vector<MRSidekickSpan> &placeholders)
	    : TWindowInit(MRSnippet::frame), TDialog(bounds, title.c_str()), mEditor(nullptr) {
		TButton *helpButton;

		flags |= wfMove | wfGrow | wfClose;
		growMode = gfGrowHiX | gfGrowHiY;
		helpCtx = hcDialogSnippetSidekick;
		mEditor = new MRSidekickEditor(TRect(1, 1, std::max<short>(2, size.x - 1), std::max<short>(2, size.y - 4)), parentBufferId, text, title, false, true);
		if (mEditor != nullptr) {
			mEditor->growMode = gfGrowHiX | gfGrowHiY;
			MRSnippet::instance().attach(mEditor, placeholders, replaceStart, replaceEnd);
			mEditor->insertInto(*this);
		}
		helpButton = new HelpButton(TRect(std::max<short>(2, size.x - 12), std::max<short>(2, size.y - 2), std::max<short>(3, size.x - 1), std::max<short>(3, size.y - 1)));
		helpButton->growMode = gfGrowAll;
		insert(helpButton);
		if (mEditor != nullptr) mEditor->select();
	}

	[[nodiscard]] MRSidekickEditor *snippetSidekick() const noexcept {
		return mEditor;
	}

	void sizeLimits(TPoint &min, TPoint &max) override {
		TDialog::sizeLimits(min, max);
		min.x = std::max<short>(min.x, std::min<short>(32, size.x));
		min.y = std::max<short>(min.y, std::min<short>(12, size.y));
	}

	TPalette &getPalette() const override {
		static TPalette palette(cpGrayDialog, sizeof(cpGrayDialog) - 1);

		for (uchar index = 1; index <= 8; ++index)
			palette[index] = MRSnippet::instance().dialogColor(index);
		return palette;
	}

	TColorAttr mapColor(uchar index) override {
		if (index >= 1 && index <= 8) return MRSnippet::instance().dialogColor(index);
		if (index == 15) {
			const TColorAttr background = MRSnippet::instance().dialogColor(1) & 0xF0;
			return background | (background >> 4);
		}
		return TDialog::mapColor(index);
	}

  private:
	MRSidekickEditor *mEditor;
};



void MRSnippet::attach(MRSidekickEditor *editor, const std::vector<MRSidekickSpan> &placeholders, std::size_t start, std::size_t end) {
	activeEditor = editor;
	spans = placeholders;
	touched.assign(spans.size(), 0);
	activeIndex = -1;
	endEdge = false;
	replaceStart = start;
	replaceEnd = end;
	if (!spans.empty()) selectPlaceholder(*editor, 1);
}

void MRSnippet::detach(const MRSidekickEditor *editor) noexcept {
	if (activeEditor != editor) return;
	activeEditor = nullptr;
	spans.clear();
	touched.clear();
	activeIndex = -1;
	endEdge = false;
	replaceStart = 0;
	replaceEnd = 0;
}

bool MRSnippet::isEditor(const MRSidekickEditor *editor) const noexcept {
	return activeEditor == editor && editor != nullptr;
}

bool MRSnippet::movePlaceholder(MRSidekickEditor &editor, int direction) {
	if (!isEditor(&editor) || editor.mReadOnly || spans.empty()) return false;
	selectPlaceholder(editor, direction);
	return true;
}

bool MRSnippet::moveForParent(const MREditWindow *parent, int direction) {
	if (parent == nullptr || activeEditor == nullptr || activeEditor->parentBufferId() != parent->bufferId()) return false;
	if (!movePlaceholder(*activeEditor, direction)) return false;
	activeEditor->drawView();
	return true;
}

bool MRSnippet::handleEditorKey(MREditWindow *parent, const TEvent &event) {
	if (parent == nullptr || parent->getEditor() == nullptr || parent->isReadOnly()) return false;
	const ushort modifiers = event.keyDown.controlKeyState;
	const ushort keyCode = event.keyDown.keyCode;
	// Konsole's Win32 input mode reports Ctrl+Space with the Space scan code and a NUL character.
	const bool ctrlSpace = (modifiers & kbCtrlShift) != 0 && (modifiers & (kbAltShift | kbSuperShift | kbPaste)) == 0 &&
	                       (keyCode == kbNoKey || keyCode == static_cast<ushort>(' ') || keyCode == static_cast<ushort>('@') ||
	                        (event.keyDown.charScan.scanCode == 0x39 && (event.keyDown.charScan.charCode == 0 || event.keyDown.charScan.charCode == ' ')));
	if (!ctrlSpace) return false;
	MREditSetupSettings editSettings;
	effectiveEditSetupSettingsForPath(parent->currentFileName(), editSettings);
	if (!editSettings.snippets) return false;
	static_cast<void>(mrvmOpenSnippetSidekick(parent));
	return true;
}

bool MRSnippet::wordByte(char ch) noexcept {
	const unsigned char value = static_cast<unsigned char>(ch);
	return std::isalnum(value) != 0 || ch == '_';
}

std::size_t MRSnippet::wordLeftOffset(const std::string &value, std::size_t offset) const noexcept {
	std::size_t pos = std::min(offset, value.size());
	if (pos == 0) return 0;
	--pos;
	while (pos > 0 && !wordByte(value[pos])) --pos;
	while (pos > 0 && wordByte(value[pos - 1])) --pos;
	return pos;
}

std::size_t MRSnippet::wordRightOffset(const std::string &value, std::size_t offset) const noexcept {
	std::size_t pos = std::min(offset, value.size());
	while (pos < value.size() && wordByte(value[pos])) ++pos;
	while (pos < value.size() && !wordByte(value[pos])) ++pos;
	return pos;
}

TColorAttr MRSnippet::dialogColor(uchar index) const noexcept {
	const TColorAttr frame = sidekickColor(kMrPaletteSnippetSidekickFrame, 0x3F);
	const TColorAttr text = sidekickColor(kMrPaletteSnippetSidekickText, 0x30);
	const TColorAttr selected = sidekickColor(kMrPaletteSnippetActivePlaceholder, 0xE0);

	switch (index) {
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
			return frame;
		case 6:
			return text;
		case 7:
			return selected;
		case 8:
			return text;
		default:
			return frame;
	}
}

TRect MRSnippet::boundsFor(MREditWindow *parent, const std::string &text, std::size_t replaceStart, int anchorViewColumn, int anchorViewRow) const {
	MRFileEditor *editor = parent != nullptr ? parent->getEditor() : nullptr;
	TRect desktop = TProgram::deskTop != nullptr ? TProgram::deskTop->getExtent() : TRect(0, 0, 80, 25);
	const std::vector<std::string> lines = splitLines(text);
	const int desktopWidth = std::max(1, desktop.b.x - desktop.a.x);
	const int desktopHeight = std::max(1, desktop.b.y - desktop.a.y);
	const int maxWidth = std::max(1, desktopWidth - 2);
	const int maxHeight = std::max(1, desktopHeight - 2);
	const int minWidth = std::min(48, maxWidth);
	const int minHeight = std::min(12, maxHeight);
	int wantedWidth = std::clamp(sidekickMaxLineLength(lines) + 8, minWidth, maxWidth);
	int wantedHeight = std::clamp<int>(static_cast<int>(lines.size()) + 8, minHeight, maxHeight);
	int x = desktop.a.x + 2;
	int y = desktop.a.y + 2;

	if (editor != nullptr) {
		const TPoint editorGlobal = editor->makeGlobal(TPoint(0, 0));
		const TRect textViewport = editor->visibleTextViewportBounds();
		const std::size_t lineIndex = editor->lineIndexOfOffset(replaceStart);
		const std::size_t visibleLine = editor->visibleLineForDocumentLine(lineIndex);
		const std::size_t lineStart = editor->lineStartOffset(replaceStart);
		const int literalViewColumn = editor->charColumn(lineStart, replaceStart) - editor->delta.x + 1;
		const int literalViewRow = static_cast<int>(visibleLine) - editor->delta.y + 1;

		anchorViewColumn = literalViewColumn > 0 ? literalViewColumn : anchorViewColumn;
		anchorViewRow = literalViewRow > 0 ? literalViewRow : anchorViewRow;
		x = editorGlobal.x + textViewport.a.x + std::max(0, anchorViewColumn - 1) - 1;
		y = editorGlobal.y + textViewport.a.y + std::max(0, anchorViewRow - 1) - 2;
	}
	x = std::clamp(x, desktop.a.x, std::max(desktop.a.x, desktop.b.x - wantedWidth));
	if (y + wantedHeight > desktop.b.y) y = y - wantedHeight - 1;
	y = std::clamp(y, desktop.a.y, std::max(desktop.a.y, desktop.b.y - wantedHeight));
	return TRect(x, y, x + wantedWidth, y + wantedHeight);
}

bool MRSnippet::open(MREditWindow *parent, const std::string &text, const std::string &title, std::size_t replaceStart, std::size_t replaceEnd, const std::vector<MRSidekickSpan> &placeholders, int anchorViewColumn, int anchorViewRow, bool &committed) {
	if (parent == nullptr || parent->getEditor() == nullptr || TProgram::deskTop == nullptr) return false;
	const TRect bounds = boundsFor(parent, text, replaceStart, anchorViewColumn, anchorViewRow);

	committed = false;
	mrDropActiveSidekick();
	Dialog *dialog = new Dialog(bounds, parent->bufferId(), replaceStart, replaceEnd, text, title, placeholders);
	if (dialog == nullptr) return false;
	if (dialog->snippetSidekick() == nullptr) {
		TObject::destroy(dialog);
		return false;
	}
	ushort result = cmCancel;
	{
		HintGuard hintGuard;
		result = TProgram::deskTop->execView(dialog);
	}
	committed = result == cmOK;
	TObject::destroy(dialog);
	static_cast<void>(mrActivateEditWindow(parent));
	if (parent->getEditor() != nullptr) parent->getEditor()->select();
	return true;
}

bool mrOpenSnippetSidekickAt(MREditWindow *parent, const std::string &text, const std::string &title, std::size_t replaceStart, std::size_t replaceEnd, const std::vector<MRSidekickSpan> &placeholders, int anchorViewColumn, int anchorViewRow, bool &committed) {
	return MRSnippet::instance().open(parent, text, title, replaceStart, replaceEnd, placeholders, anchorViewColumn, anchorViewRow, committed);
}

bool mrMoveSnippetPlaceholderForParent(const MREditWindow *parent, int direction) {
	return MRSnippet::instance().moveForParent(parent, direction);
}

MRSnippet::DrawColors MRSnippet::drawColors(const MRSidekickEditor &editor, TColorAttr baseTextColor) const noexcept {
	DrawColors colors{baseTextColor, baseTextColor, baseTextColor};
	const bool snippetEditor = isEditor(&editor);
	if (editor.mPalette == MRSidekickPalette::Sidekick)
		colors.text = sidekickColor(snippetEditor ? kMrPaletteSnippetSidekickText : kMrPaletteSidekickEditorText, 0x30);
	if (snippetEditor) {
		colors.selected = sidekickColor(kMrPaletteSnippetActivePlaceholder, 0xE0);
		colors.placeholder = sidekickColor(kMrPaletteSnippetDefaultText, 0x38);
	} else {
		colors.selected = colors.text;
		colors.placeholder = colors.text;
	}
	return colors;
}

TColorAttr MRSnippet::placeholderColor(std::size_t offset, const DrawColors &colors) const noexcept {
	for (std::size_t index = 0; index < spans.size(); ++index) {
		const MRSidekickSpan &span = spans[index];
		if (offset < span.start || offset >= span.end) continue;
		if (index < touched.size() && touched[index] != 0) return colors.text;
		return static_cast<int>(index) == activeIndex ? colors.selected : colors.placeholder;
	}
	return colors.text;
}

bool MRSnippet::replacePlaceholder(MRSidekickEditor &editor, const std::string &replacement) {
	std::vector<MRSidekickSpan> placeholders;
	std::string value;
	MRSidekickSpan active{};
	std::size_t offset = 0;
	std::size_t oldLength = 0;
	std::size_t newLength = 0;

	if (!isEditor(&editor) || activeIndex < 0 || static_cast<std::size_t>(activeIndex) >= spans.size()) return false;
	if (static_cast<std::size_t>(activeIndex) < touched.size() && touched[static_cast<std::size_t>(activeIndex)] != 0) return false;
	active = spans[static_cast<std::size_t>(activeIndex)];
	offset = editor.cursorOffset();
	if (offset < active.start || offset > active.end) return false;
	value = editor.text();
	if (active.start > value.size() || active.end > value.size() || active.end < active.start) return false;
	oldLength = active.end - active.start;
	newLength = replacement.size();
	value.replace(active.start, oldLength, replacement);
	placeholders = spans;
	for (std::size_t index = 0; index < placeholders.size(); ++index) {
		MRSidekickSpan &span = placeholders[index];

		if (index == static_cast<std::size_t>(activeIndex)) {
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
	spans = std::move(placeholders);
	if (static_cast<std::size_t>(activeIndex) < touched.size()) touched[static_cast<std::size_t>(activeIndex)] = 1;
	editor.setText(std::move(value));
	endEdge = true;
	editor.setCursorFromOffset(active.start + newLength);
	resizeForContent(editor);
	return true;
}

bool MRSnippet::handleRuntimeKeymap(MRSidekickEditor &editor, TEvent &event) {
	MRKeymapToken token(MRKeymapBaseKey::Esc, 0);

	if (!isEditor(&editor) || editor.mReadOnly || event.what != evKeyDown) return false;
	if (!mrKeymapTokenFromEvent(event.keyDown.keyCode, event.keyDown.controlKeyState, token)) return false;
	const MRKeymapResolver::Result result = runtimeKeymapResolver().resolve(MRKeymapContext::Edit, token);
	switch (result.kind) {
		case MRKeymapResolver::ResultKind::NoMatch:
			return false;
		case MRKeymapResolver::ResultKind::Pending:
		case MRKeymapResolver::ResultKind::Invalid:
		case MRKeymapResolver::ResultKind::Aborted:
			editor.clearEvent(event);
			return true;
		case MRKeymapResolver::ResultKind::Matched:
			editor.clearEvent(event);
			if (result.target.type != MRKeymapBindingType::Action) return true;
			return handleAction(editor, result.target.target);
	}
	return false;
}

bool MRSnippet::actionFromId(const std::string &actionId, Action &action) const noexcept {
	struct Entry {
		const char *id;
		Action action;
	};
	static constexpr Entry entries[] = {
		{"MRMAC_CURSOR_LEFT", Action::CursorLeft},
		{"MRMAC_CURSOR_RIGHT", Action::CursorRight},
		{"MRMAC_CURSOR_UP", Action::CursorUp},
		{"MRMAC_CURSOR_DOWN", Action::CursorDown},
		{"MRMAC_CURSOR_HOME", Action::CursorHome},
		{"MRMAC_CURSOR_END_OF_LINE", Action::CursorEnd},
		{"MRMAC_CURSOR_WORD_LEFT", Action::CursorWordLeft},
		{"MRMAC_CURSOR_WORD_RIGHT", Action::CursorWordRight},
		{"MRMAC_DELETE_BACKWARD_CHAR", Action::DeleteBackwardChar},
		{"MRMAC_DELETE_FORWARD_CHAR", Action::DeleteForwardChar},
		{"MRMAC_DELETE_FORWARD_CHAR_OR_BLOCK", Action::DeleteForwardChar},
		{"MRMAC_DELETE_BACKWARD_WORD", Action::DeleteBackwardWord},
		{"MRMAC_DELETE_FORWARD_WORD", Action::DeleteForwardWord},
		{"MRMAC_DELETE_BACKWARD_TO_HOME", Action::DeleteBackwardToHome},
		{"MRMAC_DELETE_TO_EOL", Action::DeleteToEndOfLine},
		{"MRMAC_DELETE_LINE", Action::DeleteLine},
		{"MR_LOAD_BLOCK_FROM_FILE", Action::LoadBlockFromFile},
		{"MR_SNIPPET_PLACEHOLDER_NEXT", Action::PlaceholderNext},
		{"MR_SNIPPET_PLACEHOLDER_PREVIOUS", Action::PlaceholderPrevious}
	};
	for (const Entry &entry : entries) {
		if (actionId == entry.id) {
			action = entry.action;
			return true;
		}
	}
	return false;
}

bool MRSnippet::handleAction(MRSidekickEditor &editor, const std::string &actionId) {
	Action action = Action::CursorLeft;

	if (!actionFromId(actionId, action)) return false;
	switch (action) {
		case Action::CursorLeft:
			editor.moveLeft();
			return true;
		case Action::CursorRight:
			editor.moveRight();
			return true;
		case Action::CursorUp:
			editor.moveUp();
			return true;
		case Action::CursorDown:
			editor.moveDown();
			return true;
		case Action::CursorHome:
			editor.moveLineStart();
			return true;
		case Action::CursorEnd:
			editor.moveLineEnd();
			return true;
		case Action::CursorWordLeft:
			editor.moveWordLeft();
			return true;
		case Action::CursorWordRight:
			editor.moveWordRight();
			return true;
		case Action::DeleteBackwardChar:
			editor.eraseBackward();
			return true;
		case Action::DeleteForwardChar:
			editor.eraseForward();
			return true;
		case Action::DeleteBackwardWord:
			editor.eraseWordBackward();
			return true;
		case Action::DeleteForwardWord:
			editor.eraseWordForward();
			return true;
		case Action::DeleteBackwardToHome:
			editor.eraseToLineStart();
			return true;
		case Action::DeleteToEndOfLine:
			editor.eraseToLineEnd();
			return true;
		case Action::DeleteLine:
			editor.eraseLine();
			return true;
		case Action::LoadBlockFromFile:
			return loadBlockFromFile(editor);
		case Action::PlaceholderNext:
			selectPlaceholder(editor, 1);
			return true;
		case Action::PlaceholderPrevious:
			selectPlaceholder(editor, -1);
			return true;
	}
	return false;
}

bool MRSnippet::loadBlockFromFile(MRSidekickEditor &editor) {
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
	editor.insertTextAtCursor(expandSidekickTabs(content));
	rememberLoadDialogPath(MRDialogHistoryScope::BlockLoad, resolvedPath.c_str());
	return true;
}

void MRSnippet::selectPlaceholder(MRSidekickEditor &editor, int direction) {
	if (spans.empty()) return;
	if (activeIndex < 0) {
		activeIndex = direction >= 0 ? 0 : static_cast<int>(spans.size()) - 1;
	} else if (direction >= 0) {
		activeIndex = (activeIndex + 1) % static_cast<int>(spans.size());
	} else {
		activeIndex = (activeIndex + static_cast<int>(spans.size()) - 1) % static_cast<int>(spans.size());
	}
	endEdge = false;
	setCursorFromActivePlaceholder(editor);
}

void MRSnippet::setCursorFromActivePlaceholder(MRSidekickEditor &editor) {
	const MRSidekickSpan &placeholder = spans[static_cast<std::size_t>(activeIndex)];

	editor.setCursorFromOffset(endEdge ? placeholder.end : placeholder.start);
}

void MRSnippet::adjustAfterInsert(MRSidekickEditor &editor, std::size_t offset, std::size_t length) {
	if (!isEditor(&editor)) return;
	for (MRSidekickSpan &span : spans) {
		if (offset <= span.start) {
			span.start += length;
			span.end += length;
		} else if (offset < span.end)
			span.end += length;
	}
}

void MRSnippet::adjustAfterErase(MRSidekickEditor &editor, std::size_t offset, std::size_t length) {
	if (!isEditor(&editor)) return;
	const std::size_t eraseEnd = offset + length;

	for (MRSidekickSpan &span : spans) {
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

void MRSnippet::resizeForContent(MRSidekickEditor &editor) {
	if (!isEditor(&editor) || editor.mReadOnly || editor.owner == nullptr || TProgram::deskTop == nullptr) return;

	TRect desktop = TProgram::deskTop->getExtent();
	TRect bounds = editor.owner->getBounds();
	const int desktopWidth = std::max(1, desktop.b.x - desktop.a.x);
	const int desktopHeight = std::max(1, desktop.b.y - desktop.a.y);
	const int maxWidth = std::max(32, desktopWidth - 2);
	const int maxHeight = std::max(6, desktopHeight - 2);
	const int minHeight = std::min(12, maxHeight);
	const int wantedWidth = std::clamp(sidekickMaxLineLength(editor.mLines) + 8, 32, maxWidth);
	const int wantedHeight = std::clamp<int>(static_cast<int>(editor.mLines.size()) + 8, minHeight, maxHeight);
	const int currentWidth = std::max(1, bounds.b.x - bounds.a.x);
	const int currentHeight = std::max(1, bounds.b.y - bounds.a.y);
	const int newWidth = std::max(currentWidth, wantedWidth);
	const int newHeight = std::max(currentHeight, wantedHeight);

	if (newWidth != currentWidth || newHeight != currentHeight) {
		bounds.b.x = bounds.a.x + newWidth;
		bounds.b.y = bounds.a.y + newHeight;
		if (bounds.b.x > desktop.b.x) bounds.move(desktop.b.x - bounds.b.x, 0);
		if (bounds.a.x < desktop.a.x) bounds.move(desktop.a.x - bounds.a.x, 0);
		if (bounds.b.y > desktop.b.y) bounds.move(0, desktop.b.y - bounds.b.y);
		if (bounds.a.y < desktop.a.y) bounds.move(0, desktop.a.y - bounds.a.y);
		editor.owner->locate(bounds);
	}
	TRect editorBounds(1, 1, std::max<short>(2, editor.owner->size.x - 1), std::max<short>(2, editor.owner->size.y - 4));
	editor.updateScrollBars(editorBounds);
}

void MRSnippet::commit(MRSidekickEditor &view) {
	if (!isEditor(&view)) return;
	MREditWindow *parent = findEditWindowByBufferId(view.mParentBufferId);
	MRFileEditor *editor = parent != nullptr ? parent->getEditor() : nullptr;
	if (editor != nullptr && !editor->isReadOnly()) {
		MREditSetupSettings settings;
		effectiveEditSetupSettingsForPath(parent->currentFileName(), settings);
		std::string replacement;
		int startColumn = editor->charColumn(editor->lineStartOffset(replaceStart), replaceStart) + 1;

		for (std::size_t lineIndex = 0; lineIndex < view.mLines.size(); ++lineIndex) {
			const std::string &line = view.mLines[lineIndex];
			if (lineIndex != 0) {
				replacement.push_back('\n');
				startColumn = 1;
			}
			int targetColumn = startColumn;
			std::size_t indentEnd = 0;
			while (indentEnd < line.size()) {
				if (line[indentEnd] == ' ')
					++targetColumn;
				else if (line[indentEnd] == '\t')
					targetColumn = resolvedEditFormatTabDisplayColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, targetColumn);
				else
					break;
				++indentEnd;
			}
			replacement += buildEditIndentFill(settings, startColumn, targetColumn, settings.tabExpand);
			replacement.append(line, indentEnd, std::string::npos);
		}

		if (editor->replaceRangeAndSelect(static_cast<uint>(replaceStart), static_cast<uint>(replaceEnd), replacement.c_str(), static_cast<uint>(replacement.size()))) {
			const std::size_t cursor = std::min<std::size_t>(replaceStart + replacement.size(), editor->bufferLength());
			editor->setSelectionOffsets(cursor, cursor, False);
		}
	}
	view.closeSidekick(cmOK);
}
