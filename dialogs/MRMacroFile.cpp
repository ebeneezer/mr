#include "../app/utils/MRStringUtils.hpp"
#include "../app/utils/MRFileIOUtils.hpp"
#define Uses_Dialogs
#define Uses_MsgBox
#define Uses_TApplication
#define Uses_TButton
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TEvent
#define Uses_TFileDialog
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TListViewer
#define Uses_TObject
#define Uses_TProgram
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TStaticText
#include <tvision/tv.h>

#include "MRMacroFile.hpp"
#include "MRSetupCommon.hpp"

#include "../mrmac/MRMacroRunner.hpp"
#include "../mrmac/mrmac.h"
#include "../mrmac/MRVM.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../ui/MRFrame.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "../ui/MREditWindow.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
using mr::dialogs::ensureMrmacExtension;
using mr::dialogs::hasMrmacExtension;
using mr::dialogs::normalizeTvPathSeparators;

TFrame *initMrDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}

class MacroManagerActivationSink {
  public:
	virtual void activateFocusedEntry(bool fromAutoexecList) = 0;
	virtual void noteListFocus(bool autoexecListFocused) = 0;

  protected:
	~MacroManagerActivationSink() = default;
};

enum : ushort {
	cmMRMacroManagerCreate = 220,
	cmMRMacroManagerDelete,
	cmMRMacroManagerCopy,
	cmMRMacroManagerEdit,
	cmMRMacroManagerBind,
	cmMRMacroManagerAddAutoexec,
	cmMRMacroManagerRemoveAutoexec,
	cmMRMacroManagerPlayback,
	cmMRMacroManagerOpenEditor
};

constexpr int kMacroManagerVirtualWidth = 92;
constexpr int kMacroManagerVirtualHeight = 27;

void postMacroDialogError(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, text, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}

struct MacroFileEntry {
	std::string fileName;
	std::string path;
	std::string macroName;
	std::string keySpec;
	std::string compileError;
};

std::string baseNameOf(const std::string &path) {
	std::size_t pos = path.find_last_of('/');
	if (pos == std::string::npos) return path;
	return path.substr(pos + 1);
}

bool fileExists(const std::string &path) {
	return ::access(path.c_str(), F_OK) == 0;
}

bool copyFileBinary(const std::string &source, const std::string &dest) {
	std::ifstream in(source.c_str(), std::ios::in | std::ios::binary);
	std::ofstream out(dest.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!in || !out) return false;
	out << in.rdbuf();
	return in.good() && out.good();
}

std::string padRight(const std::string &value, std::size_t width) {
	if (value.size() >= width) return value.substr(0, width);
	return value + std::string(width - value.size(), ' ');
}

std::string firstMacroNameForSource(const std::string &source, std::string &keySpec, std::string &compileError) {
	size_t bytecodeSize = 0;
	unsigned char *bytecode = compile_macro_code(source.c_str(), &bytecodeSize);
	std::string macroName;

	keySpec.clear();
	compileError.clear();
	if (bytecode == nullptr) {
		const char *err = get_last_compile_error();
		compileError = err != nullptr ? err : "Compilation failed.";
		return std::string();
	}
	int count = get_compiled_macro_count();
	if (count > 0) {
		const char *name = get_compiled_macro_name(0);
		const char *key = get_compiled_macro_keyspec(0);
		macroName = name != nullptr ? name : std::string();
		keySpec = key != nullptr ? key : std::string();
	}
	std::free(bytecode);
	return macroName;
}

bool loadEntryMetadata(MacroFileEntry &entry) {
	std::string source;
	if (!readTextFile(entry.path, source)) return false;
	entry.macroName = firstMacroNameForSource(source, entry.keySpec, entry.compileError);
	return true;
}

bool fileNameLess(const MacroFileEntry &a, const MacroFileEntry &b) {
	std::string lhs = a.fileName;
	std::string rhs = b.fileName;
	for (char &lh : lhs)
		lh = static_cast<char>(std::tolower(static_cast<unsigned char>(lh)));
	for (char &rh : rhs)
		rh = static_cast<char>(std::tolower(static_cast<unsigned char>(rh)));
	return lhs < rhs;
}

std::vector<MacroFileEntry> scanMacroFilesInDirectory(const std::string &directoryPath) {
	std::vector<MacroFileEntry> entries;
	DIR *dir = ::opendir(directoryPath.c_str());

	if (dir == nullptr) return entries;
	for (;;) {
		dirent *de = ::readdir(dir);
		if (de == nullptr) break;
		if (std::strcmp(de->d_name, ".") == 0 || std::strcmp(de->d_name, "..") == 0) continue;

		std::string fileName = de->d_name;
		if (!hasMrmacExtension(fileName)) continue;

		MacroFileEntry entry;
		entry.fileName = fileName;
		entry.path = normalizeTvPathSeparators(directoryPath + "/" + fileName);
		loadEntryMetadata(entry);
		entries.push_back(entry);
	}
	::closedir(dir);
	std::sort(entries.begin(), entries.end(), fileNameLess);
	return entries;
}

std::string rowTextFor(const MacroFileEntry &entry) {
	std::string left = (entry.compileError.empty() ? "  " : "! ");
	left += entry.fileName;
	return padRight(left, 30) + " " + padRight(entry.keySpec.empty() ? "<no key>" : entry.keySpec, 14);
}

std::string autoexecRowTextFor(const std::string &fileName) {
	return fileName;
}

std::string sanitizeMacroIdentifier(const std::string &name) {
	std::string out;
	for (char i : name) {
		unsigned char ch = static_cast<unsigned char>(i);
		if (std::isalnum(ch) != 0 || ch == '_') out.push_back(static_cast<char>(ch));
		else if (ch == '-' || ch == ' ' || ch == '.')
			out.push_back('_');
	}
	if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0])) != 0) out.insert(out.begin(), 'M');
	return out;
}

std::string createMacroTemplateForPath(const std::string &path) {
	std::string base = baseNameOf(path);
	std::size_t dotPos = base.rfind('.');
	if (dotPos != std::string::npos) base = base.substr(0, dotPos);
	std::string macroName = sanitizeMacroIdentifier(base);

	std::string source;
	source += "$MACRO ";
	source += macroName;
	source += " FROM EDIT;\n";
	source += "; MRMAC source\n";
	source += "END_MACRO;\n";
	return source;
}

bool startsWithTokenInsensitive(const std::string &text, std::size_t pos, const char *token) {
	std::size_t i = 0;
	if (token == nullptr) return false;
	while (token[i] != '\0') {
		if (pos + i >= text.size()) return false;
		if (std::toupper(static_cast<unsigned char>(text[pos + i])) != std::toupper(static_cast<unsigned char>(token[i]))) return false;
		++i;
	}
	if (pos + i < text.size()) {
		unsigned char ch = static_cast<unsigned char>(text[pos + i]);
		if (std::isalnum(ch) != 0 || ch == '_') return false;
	}
	return true;
}

bool findFirstMacroHeader(const std::string &source, const std::string &firstMacroName, std::size_t &headerStart, std::size_t &headerEnd, std::string &macroName) {
	const std::string expectedName = upperAscii(firstMacroName);
	std::size_t i = 0;

	while (i < source.size()) {
		std::size_t pos = source.find('$', i);
		if (pos == std::string::npos) return false;
		i = pos + 1;

		if (!startsWithTokenInsensitive(source, pos, "$MACRO")) continue;
		std::size_t p = pos + 6;
		while (p < source.size() && std::isspace(static_cast<unsigned char>(source[p])) != 0)
			++p;
		if (p >= source.size()) return false;

		std::size_t nameStart = p;
		while (p < source.size()) {
			unsigned char ch = static_cast<unsigned char>(source[p]);
			if (std::isalnum(ch) == 0 && ch != '_') break;
			++p;
		}
		if (p == nameStart) continue;

		std::string candidateName = source.substr(nameStart, p - nameStart);
		if (!expectedName.empty() && upperAscii(candidateName) != expectedName) continue;

		std::size_t semicolonPos = source.find(';', p);
		if (semicolonPos == std::string::npos) return false;
		headerStart = pos;
		headerEnd = semicolonPos + 1;
		macroName = candidateName;
		return true;
	}
	return false;
}

std::string rebuildMacroHeader(const std::string &macroName, const std::string &oldHeader, const std::string &keySpec) {
	std::size_t namePos = 6;
	std::vector<std::string> tokens;
	std::ostringstream out;

	while (namePos < oldHeader.size() && std::isspace(static_cast<unsigned char>(oldHeader[namePos])) != 0)
		++namePos;
	while (namePos < oldHeader.size()) {
		unsigned char ch = static_cast<unsigned char>(oldHeader[namePos]);
		if (std::isalnum(ch) == 0 && ch != '_') break;
		++namePos;
	}
	std::size_t bodyStart = namePos;
	while (bodyStart < oldHeader.size() && std::isspace(static_cast<unsigned char>(oldHeader[bodyStart])) != 0)
		++bodyStart;
	std::size_t bodyEnd = oldHeader.rfind(';');
	if (bodyEnd == std::string::npos || bodyEnd < bodyStart) bodyEnd = oldHeader.size();

	std::istringstream in(oldHeader.substr(bodyStart, bodyEnd - bodyStart));
	std::string token;
	while (in >> token)
		tokens.push_back(token);

	for (std::size_t i = 0; i < tokens.size();) {
		if (upperAscii(tokens[i]) != "TO") {
			++i;
			continue;
		}
		tokens.erase(tokens.begin() + static_cast<long>(i));
		if (i < tokens.size()) tokens.erase(tokens.begin() + static_cast<long>(i));
	}

	std::size_t insertPos = tokens.size();
	for (std::size_t i = 0; i < tokens.size(); ++i)
		if (upperAscii(tokens[i]) == "FROM") {
			insertPos = i;
			break;
		}
	tokens.insert(tokens.begin() + static_cast<long>(insertPos), keySpec);
	tokens.insert(tokens.begin() + static_cast<long>(insertPos), "TO");

	out << "$MACRO " << macroName;
	if (!tokens.empty()) {
		out << " ";
		for (std::size_t i = 0; i < tokens.size(); ++i) {
			if (i != 0) out << " ";
			out << tokens[i];
		}
	}
	out << ";";
	return out.str();
}

bool captureBindingKeySpec(std::string &keySpec) {
	return mrCaptureBindingKeySpec("Bind Macro Key", "Press key to bind macro.\nEsc = cancel.", keySpec);
}

bool rebindMacroFileKey(const MacroFileEntry &entry, const std::string &keySpec, std::string &errorText) {
	std::string source;
	std::size_t headerStart = 0;
	std::size_t headerEnd = 0;
	std::string macroName;
	std::string updatedSource;
	std::string updatedHeader;
	size_t bytecodeSize = 0;
	unsigned char *bytecode = nullptr;

	errorText.clear();
	if (!readTextFile(entry.path, source)) {
		errorText = "Unable to read macro file.";
		return false;
	}
	if (!findFirstMacroHeader(source, entry.macroName, headerStart, headerEnd, macroName)) {
		errorText = "Unable to locate first $MACRO header.";
		return false;
	}

	updatedHeader = rebuildMacroHeader(macroName, source.substr(headerStart, headerEnd - headerStart), keySpec);
	updatedSource = source.substr(0, headerStart) + updatedHeader + source.substr(headerEnd);

	bytecode = compile_macro_code(updatedSource.c_str(), &bytecodeSize);
	if (bytecode == nullptr) {
		const char *err = get_last_compile_error();
		errorText = err != nullptr ? err : "Compilation failed after binding update.";
		return false;
	}
	std::free(bytecode);

	if (!writeTextFile(entry.path, updatedSource)) {
		errorText = "Unable to write updated macro file.";
		return false;
	}
	if (!mrvmLoadMacroFile(entry.path, &errorText)) return false;
	rememberLoadDialogPath(MRDialogHistoryScope::MacroFile, entry.path.c_str());
	return true;
}

bool openMacroSourceInEditor(const std::string &path) {
	MREditWindow *target = findReusableEmptyWindow(currentEditWindow());
	if (target == nullptr) target = createEditorWindow(baseNameOf(path).c_str());
	if (target == nullptr) return false;
	if (!target->loadFromFile(path.c_str())) {
		postMacroDialogError("Unable to load macro file: " + path);
		return false;
	}
	static_cast<void>(mrActivateEditWindow(target));
	return true;
}

class MacroManagerListView : public TListViewer {
  public:
	MacroManagerListView(const TRect &bounds, TScrollBar *aVScrollBar, const std::vector<std::string> &aItems, const std::vector<bool> &aErrorFlags, MacroManagerActivationSink &activationSink, bool autoexecList) noexcept : TListViewer(bounds, 1, nullptr, aVScrollBar), itemRows(aItems), compileErrorFlags(aErrorFlags), activationSink(activationSink), autoexecList(autoexecList) {
		setRange(static_cast<short>(itemRows.size()));
	}

	void setItems(const std::vector<std::string> &items, const std::vector<bool> &errorFlags, short selectableItems) {
		itemRows = items;
		compileErrorFlags = errorFlags;
		selectableItemCount = std::max<short>(0, selectableItems);
		setRange(static_cast<short>(itemRows.size()));
		if (itemRows.empty() || selectableItemCount <= 0) focusItemNum(0);
		else if (focused >= range)
			focusItemNum(range - 1);
	}

	void draw() override {
		TListViewer::draw();

		unsigned char errorBios = 0;
		short indent = hScrollBar != nullptr ? hScrollBar->value : 0;
		short colWidth = size.x / numCols + 1;
		TColorAttr errorColor;

		if (!configuredColorSlotOverride(kMrPaletteMessageError, errorBios)) return;
		errorColor = TColorAttr(errorBios);

		for (short i = 0; i < size.y; ++i) {
			for (short j = 0; j < numCols; ++j) {
				short item = static_cast<short>(j * size.y + i + topItem);
				short curCol = static_cast<short>(j * colWidth);
				char text[256];
				const int textPos = 0;
				int visPos = 0;
				TDrawBuffer mark;

				if (item < 0 || item >= range || !hasCompileError(item)) continue;
				getText(text, item, 255);
				if (text[0] != '!') continue;
				visPos = textPos - indent;
				if (visPos < 0 || visPos >= colWidth - 1) continue;
				mark.moveChar(0, '!', errorColor, 1);
				writeLine(static_cast<short>(curCol + 1 + visPos), i, 1, 1, mark);
			}
		}
	}

	void getText(char *dest, short item, short maxLen) override {
		std::size_t copyLen;
		if (dest == nullptr || maxLen <= 0) return;
		if (item < 0 || static_cast<std::size_t>(item) >= itemRows.size()) {
			dest[0] = EOS;
			return;
		}
		copyLen = static_cast<std::size_t>(maxLen - 1);
		std::strncpy(dest, itemRows[static_cast<std::size_t>(item)].c_str(), copyLen);
		dest[copyLen] = EOS;
	}

	void handleEvent(TEvent &event) override {
		const bool isDoubleClickActivation = event.what == evMouseDown && (event.mouse.buttons & mbLeftButton) != 0 && (event.mouse.eventFlags & meDoubleClick) != 0;

		TListViewer::handleEvent(event);

		if (isDoubleClickActivation && owner != nullptr && focused >= 0 && focused < selectableItemCount) {
			activationSink.activateFocusedEntry(autoexecList);
			clearEvent(event);
			return;
		}
		if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter && owner != nullptr && focused >= 0 && focused < selectableItemCount) {
			message(owner, evCommand, cmOK, nullptr);
			clearEvent(event);
		}
	}

	bool hasSelection() const noexcept {
		return focused >= 0 && focused < selectableItemCount;
	}

	void setState(ushort aState, Boolean enable) override {
		const bool hadFocus = (state & sfFocused) != 0;

		TListViewer::setState(aState, enable);
		if (aState == sfFocused && !hadFocus && (state & sfFocused) != 0) activationSink.noteListFocus(autoexecList);
	}

  private:
	bool hasCompileError(short item) const noexcept {
		return item >= 0 && static_cast<std::size_t>(item) < compileErrorFlags.size() && compileErrorFlags[static_cast<std::size_t>(item)];
	}

	std::vector<std::string> itemRows;
	std::vector<bool> compileErrorFlags;
	MacroManagerActivationSink &activationSink;
	short selectableItemCount = 0;
	bool autoexecList = false;
};

class MacroManagerDialog : public MRDialogFoundation, public MacroManagerActivationSink {
  public:
	MacroManagerDialog() : TWindowInit(initMrDialogFrame), MRDialogFoundation(centeredSetupDialogRect(kMacroManagerVirtualWidth, kMacroManagerVirtualHeight), "MACRO MANAGER", kMacroManagerVirtualWidth, kMacroManagerVirtualHeight, initMrDialogFrame), directoryPath(defaultMacroDirectoryPath()), macroListView(nullptr), macroScrollBar(nullptr), autoexecListView(nullptr), autoexecScrollBar(nullptr) {
		const int width = kMacroManagerVirtualWidth;
		const int height = kMacroManagerVirtualHeight;
		const int gap = 2;
		const std::array topButtons{mr::dialogs::DialogButtonSpec{"~C~reate", cmMRMacroManagerCreate, bfNormal}, mr::dialogs::DialogButtonSpec{"De~l~ete", cmMRMacroManagerDelete, bfNormal}, mr::dialogs::DialogButtonSpec{"C~o~py", cmMRMacroManagerCopy, bfNormal}, mr::dialogs::DialogButtonSpec{"~E~dit", cmMRMacroManagerEdit, bfNormal}, mr::dialogs::DialogButtonSpec{"~B~ind", cmMRMacroManagerBind, bfNormal}};
		const std::array addAutoexecButton{mr::dialogs::DialogButtonSpec{"~A~dd", cmMRMacroManagerAddAutoexec, bfNormal}};
		const std::array removeAutoexecButton{mr::dialogs::DialogButtonSpec{"~R~emove", cmMRMacroManagerRemoveAutoexec, bfNormal}};
		const std::array bottomButtons{mr::dialogs::DialogButtonSpec{"~P~layback", cmMRMacroManagerPlayback, bfDefault}, mr::dialogs::DialogButtonSpec{"~D~one", cmCancel, bfNormal}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
		const int uniformButtonWidth = std::max({mr::dialogs::measureUniformButtonRow(topButtons, gap).buttonWidth, mr::dialogs::measureUniformButtonRow(addAutoexecButton, 0).buttonWidth, mr::dialogs::measureUniformButtonRow(removeAutoexecButton, 0).buttonWidth, mr::dialogs::measureUniformButtonRow(bottomButtons, gap).buttonWidth});
		const mr::dialogs::DialogButtonRowMetrics topMetrics = mr::dialogs::measureUniformButtonRow(topButtons, gap, uniformButtonWidth);
		const mr::dialogs::DialogButtonRowMetrics addAutoexecMetrics = mr::dialogs::measureUniformButtonRow(addAutoexecButton, 0, uniformButtonWidth);
		const mr::dialogs::DialogButtonRowMetrics removeAutoexecMetrics = mr::dialogs::measureUniformButtonRow(removeAutoexecButton, 0, uniformButtonWidth);
		const mr::dialogs::DialogButtonRowMetrics bottomMetrics = mr::dialogs::measureUniformButtonRow(bottomButtons, gap, uniformButtonWidth);
		const int leftListLeft = 3;
		const int leftListRight = 52;
		const int centerLeft = leftListRight + 1;
		const int centerRight = centerLeft + 12;
		const int rightListLeft = centerRight + 1;
		const int rightListRight = width - 4;
		const int listTop = 6;
		const int listBottom = height - 4;
		const int topLeft = 3 + std::max(0, (width - 6 - topMetrics.rowWidth) / 2);
		const int addAutoexecLeft = centerLeft + std::max(0, (12 - addAutoexecMetrics.rowWidth) / 2);
		const int removeAutoexecLeft = centerLeft + std::max(0, (12 - removeAutoexecMetrics.rowWidth) / 2);
		const int bottomLeft = 3 + std::max(0, (width - 6 - bottomMetrics.rowWidth) / 2);

		mr::dialogs::insertUniformButtonRow(*this, topLeft, 2, gap, topButtons, uniformButtonWidth);

		insert(new TStaticText(TRect(leftListLeft, 5, leftListLeft + 8, 6), "Macros:"));
		insert(new TStaticText(TRect(rightListLeft, 5, rightListLeft + 10, 6), "Autoexec:"));

		macroScrollBar = new TScrollBar(TRect(leftListRight, listTop, leftListRight + 1, listBottom));
		insert(macroScrollBar);
		macroListView = new MacroManagerListView(TRect(leftListLeft, listTop, leftListRight, listBottom), macroScrollBar, std::vector<std::string>(), std::vector<bool>(), *this, false);
		insert(macroListView);

		autoexecScrollBar = new TScrollBar(TRect(rightListRight, listTop, rightListRight + 1, listBottom));
		insert(autoexecScrollBar);
		autoexecListView = new MacroManagerListView(TRect(rightListLeft, listTop, rightListRight, listBottom), autoexecScrollBar, std::vector<std::string>(), std::vector<bool>(), *this, true);
		insert(autoexecListView);

		mr::dialogs::insertUniformButtonRow(*this, addAutoexecLeft, 11, 0, addAutoexecButton, uniformButtonWidth);
		mr::dialogs::insertUniformButtonRow(*this, removeAutoexecLeft, 14, 0, removeAutoexecButton, uniformButtonWidth);
		mr::dialogs::insertUniformButtonRow(*this, bottomLeft, height - 3, gap, bottomButtons, uniformButtonWidth);

		loadAutoexecEntries();
		refreshEntries(-1);
		refreshAutoexecRows(-1);
		if (macroListView != nullptr) macroListView->select();
	}

	const std::string &openPath() const noexcept {
		return openPathValue;
	}

	const std::string &playbackPath() const noexcept {
		return playbackPathValue;
	}

	void activateFocusedEntry(bool fromAutoexecList) override {
		if (fromAutoexecList) return;
		if (selectedEntryHasCompileError()) {
			showSelectedEntryError();
			return;
		}
		handlePlayback();
	}

	void noteListFocus(bool autoexecListFocused) override {
		activeAutoexecList = autoexecListFocused;
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown) {
			switch (ctrlToArrow(event.keyDown.keyCode)) {
				case kbIns:
					handleCreate();
					clearEvent(event);
					return;
				case kbDel:
					if (activeAutoexecList) handleRemoveAutoexec();
					else
						handleDelete();
					clearEvent(event);
					return;
				case kbF4:
					handleCopy();
					clearEvent(event);
					return;
				case kbF3:
					handleEdit();
					clearEvent(event);
					return;
				case kbF2:
					handleBind();
					clearEvent(event);
					return;
				case kbEnter:
					activateFocusedEntry(activeAutoexecList);
					clearEvent(event);
					return;
				case kbUp:
					if ((event.keyDown.controlKeyState & kbCtrlShift) != 0) {
						handleMoveAutoexecBy(-1);
						clearEvent(event);
						return;
					}
					break;
				case kbDown:
					if ((event.keyDown.controlKeyState & kbCtrlShift) != 0) {
						handleMoveAutoexecBy(1);
						clearEvent(event);
						return;
					}
					break;
				case kbF1:
					static_cast<void>(mrShowProjectHelp());
					clearEvent(event);
					return;
			}
		}

		if (event.what != evCommand) goto base;

		switch (event.message.command) {
			case cmMRMacroManagerCreate:
				handleCreate();
				clearEvent(event);
				break;
			case cmMRMacroManagerDelete:
				handleDelete();
				clearEvent(event);
				break;
			case cmMRMacroManagerCopy:
				handleCopy();
				clearEvent(event);
				break;
			case cmMRMacroManagerEdit:
				handleEdit();
				clearEvent(event);
				break;
			case cmMRMacroManagerBind:
				handleBind();
				clearEvent(event);
				break;
			case cmMRMacroManagerAddAutoexec:
				handleAddAutoexec();
				clearEvent(event);
				break;
			case cmMRMacroManagerRemoveAutoexec:
				handleRemoveAutoexec();
				clearEvent(event);
				break;
			case cmMRMacroManagerPlayback:
				handlePlayback();
				clearEvent(event);
				break;
			case cmOK:
				if (activeAutoexecList) {
					clearEvent(event);
					break;
				}
				if (macroListView != nullptr && macroListView->hasSelection()) {
					activateFocusedEntry(false);
					clearEvent(event);
				}
				break;
			case cmHelp:
				static_cast<void>(mrShowProjectHelp());
				clearEvent(event);
				break;
		}

		if (event.what == evNothing) return;

	base:
		MRDialogFoundation::handleEvent(event);
	}

  private:
	int selectedIndex() const {
		if (macroListView == nullptr) return -1;
		short idx = macroListView->focused;
		if (idx < 0 || static_cast<std::size_t>(idx) >= entries.size()) return -1;
		return idx;
	}

	int selectedAutoexecIndex() const {
		if (autoexecListView == nullptr) return -1;
		short idx = autoexecListView->focused;
		if (idx < 0 || static_cast<std::size_t>(idx) >= autoexecEntries.size()) return -1;
		return idx;
	}

	const MacroFileEntry *selectedEntry() const {
		int idx = selectedIndex();
		if (idx < 0) return nullptr;
		return &entries[static_cast<std::size_t>(idx)];
	}

	bool selectedEntryHasCompileError() const {
		const MacroFileEntry *entry = selectedEntry();

		return entry != nullptr && !entry->compileError.empty();
	}

	void showSelectedEntryError() {
		const MacroFileEntry *entry = selectedEntry();

		if (entry == nullptr || entry->compileError.empty()) return;
		messageBox(mfError | mfOKButton, "Invalid macro file.\n%s\n\n%s", entry->fileName.c_str(), entry->compileError.c_str());
	}

	void refreshEntries(int keepIndex) {
		entries = scanMacroFilesInDirectory(directoryPath);
		rows.clear();
		rowHasCompileError.clear();
		if (entries.empty()) rows.push_back("(none available)");
		else
			for (auto &entrie : entries) {
				std::string autoexecDiagnostic;

				if (configuredAutoexecMacroDiagnosticForFile(entrie.fileName, autoexecDiagnostic) && !autoexecDiagnostic.empty()) {
					if (entrie.compileError.empty()) entrie.compileError = autoexecDiagnostic;
					else
						entrie.compileError += "\n\nAutoexec bootstrap:\n" + autoexecDiagnostic;
				}
				rows.push_back(rowTextFor(entrie));
				rowHasCompileError.push_back(!entrie.compileError.empty());
			}
		if (entries.empty()) rowHasCompileError.push_back(false);
		if (macroListView != nullptr) {
			macroListView->setItems(rows, rowHasCompileError, static_cast<short>(entries.size()));
			if (!rows.empty()) {
				int target = keepIndex;
				if (target < 0 || target >= static_cast<int>(rows.size())) target = 0;
				macroListView->focusItemNum(target);
			}
		}
	}

	void refreshAutoexecRows(int keepIndex) {
		autoexecRows.clear();
		for (const std::string &fileName : autoexecEntries)
			autoexecRows.push_back(autoexecRowTextFor(fileName));
		if (autoexecListView != nullptr) {
			autoexecListView->setItems(autoexecRows, std::vector<bool>(autoexecRows.size(), false), static_cast<short>(autoexecEntries.size()));
			if (!autoexecRows.empty()) {
				int target = keepIndex;
				if (target < 0 || target >= static_cast<int>(autoexecRows.size())) target = 0;
				autoexecListView->focusItemNum(target);
			}
			autoexecListView->drawView();
		}
		if (autoexecScrollBar != nullptr) {
			autoexecScrollBar->drawView();
		}
	}

	void loadAutoexecEntries() {
		configuredAutoexecMacroEntries(autoexecEntries);
	}

	bool persistAutoexecEntries(const std::vector<std::string> &newEntries) {
		std::string errorText;
		std::vector<std::string> originalEntries = autoexecEntries;

		if (!setConfiguredAutoexecMacroEntries(newEntries, &errorText) || !persistConfiguredSettingsSnapshot(&errorText)) {
			setConfiguredAutoexecMacroEntries(originalEntries, nullptr);
			postMacroDialogError(errorText.empty() ? "Unable to update AUTOEXEC_MACRO." : errorText);
			return false;
		}
		autoexecEntries = newEntries;
		return true;
	}

	void insertMacroIntoAutoexec(int macroIndex, int targetIndex) {
		if (macroIndex < 0 || static_cast<std::size_t>(macroIndex) >= entries.size()) return;
		std::vector<std::string> updatedEntries = autoexecEntries;
		const std::string &fileName = entries[static_cast<std::size_t>(macroIndex)].fileName;
		auto existing = std::find(updatedEntries.begin(), updatedEntries.end(), fileName);
		int existingIndex = existing == updatedEntries.end() ? -1 : static_cast<int>(std::distance(updatedEntries.begin(), existing));

		targetIndex = std::clamp(targetIndex, 0, static_cast<int>(updatedEntries.size()));
		if (existingIndex >= 0) {
			if (existingIndex < targetIndex) --targetIndex;
			if (existingIndex == targetIndex) {
				refreshAutoexecRows(existingIndex);
				return;
			}
			updatedEntries.erase(updatedEntries.begin() + existingIndex);
			updatedEntries.insert(updatedEntries.begin() + targetIndex, fileName);
			if (!persistAutoexecEntries(updatedEntries)) return;
			refreshAutoexecRows(targetIndex);
			return;
		}

		updatedEntries.insert(updatedEntries.begin() + targetIndex, fileName);
		if (!persistAutoexecEntries(updatedEntries)) return;
		refreshAutoexecRows(targetIndex);
	}

	void moveAutoexecEntry(int sourceIndex, int targetIndex) {
		std::vector<std::string> updatedEntries = autoexecEntries;
		std::string movedEntry;

		if (sourceIndex < 0 || static_cast<std::size_t>(sourceIndex) >= updatedEntries.size()) return;
		targetIndex = std::clamp(targetIndex, 0, static_cast<int>(updatedEntries.size()));
		if (sourceIndex < targetIndex) --targetIndex;
		if (sourceIndex == targetIndex) return;
		movedEntry = updatedEntries[static_cast<std::size_t>(sourceIndex)];
		updatedEntries.erase(updatedEntries.begin() + sourceIndex);
		updatedEntries.insert(updatedEntries.begin() + targetIndex, movedEntry);
		if (!persistAutoexecEntries(updatedEntries)) return;
		refreshAutoexecRows(targetIndex);
	}

	void removeAutoexecEntry(int index) {
		std::vector<std::string> updatedEntries = autoexecEntries;

		if (index < 0 || static_cast<std::size_t>(index) >= updatedEntries.size()) return;
		updatedEntries.erase(updatedEntries.begin() + index);
		if (!persistAutoexecEntries(updatedEntries)) return;
		refreshAutoexecRows(index);
	}

	void removeAutoexecFileName(const std::string &fileName) {
		auto it = std::find(autoexecEntries.begin(), autoexecEntries.end(), fileName);

		if (it == autoexecEntries.end()) return;
		removeAutoexecEntry(static_cast<int>(std::distance(autoexecEntries.begin(), it)));
	}

	void handleCreate() {
		enum {
			BufferSize = 512
		};
		char pathBuffer[BufferSize];
		std::string path;

		std::memset(pathBuffer, 0, sizeof(pathBuffer));
		std::strncpy(pathBuffer, "new_macro.mrmac", sizeof(pathBuffer) - 1);
		if (inputBox("MACRO MANAGER", "~F~ile name", pathBuffer, static_cast<uchar>(sizeof(pathBuffer) - 1)) == cmCancel) return;

		path = trimAscii(pathBuffer);
		if (path.empty()) return;
		path = ensureMrmacExtension(path);
		if (path.find('/') == std::string::npos) path = directoryPath + "/" + path;
		path = normalizeTvPathSeparators(path);

		if (fileExists(path)) {
			if (messageBox(mfConfirmation | mfYesButton | mfNoButton, "Macro file exists.\nOverwrite?\n%s", path.c_str()) != cmYes) return;
		}
		if (!writeTextFile(path, createMacroTemplateForPath(path))) {
			postMacroDialogError("Unable to create macro file: " + path);
			return;
		}
		rememberLoadDialogPath(MRDialogHistoryScope::MacroFile, path.c_str());
		openPathValue = path;
		endModal(cmMRMacroManagerOpenEditor);
	}

	void handleDelete() {
		const MacroFileEntry *entry = selectedEntry();
		if (entry == nullptr) return;
		if (messageBox(mfConfirmation | mfYesButton | mfNoButton, "Delete macro file?\n%s", entry->path.c_str()) != cmYes) return;
		if (::unlink(entry->path.c_str()) != 0) {
			postMacroDialogError("Unable to delete: " + entry->path);
			return;
		}
		static_cast<void>(mrvmUiRemoveRuntimeMenusOwnedByFile(entry->fileName));
		removeAutoexecFileName(entry->fileName);
		refreshEntries(selectedIndex());
	}

	void handleCopy() {
		enum {
			BufferSize = 512
		};
		const MacroFileEntry *entry = selectedEntry();
		char pathBuffer[BufferSize];
		std::string destPath;
		std::string suggested;
		if (entry == nullptr) return;

		suggested = entry->fileName;
		std::size_t dotPos = suggested.rfind('.');
		if (dotPos == std::string::npos) suggested += "_copy.mrmac";
		else
			suggested.insert(dotPos, "_copy");

		std::memset(pathBuffer, 0, sizeof(pathBuffer));
		std::strncpy(pathBuffer, suggested.c_str(), sizeof(pathBuffer) - 1);
		if (inputBox("MACRO MANAGER", "Copy to ~F~ile", pathBuffer, static_cast<uchar>(sizeof(pathBuffer) - 1)) == cmCancel) return;

		destPath = trimAscii(pathBuffer);
		if (destPath.empty()) return;
		destPath = ensureMrmacExtension(destPath);
		if (destPath.find('/') == std::string::npos) destPath = directoryPath + "/" + destPath;
		destPath = normalizeTvPathSeparators(destPath);

		if (fileExists(destPath)) {
			if (messageBox(mfConfirmation | mfYesButton | mfNoButton, "Target exists.\nOverwrite?\n%s", destPath.c_str()) != cmYes) return;
		}
		if (!copyFileBinary(entry->path, destPath)) {
			postMacroDialogError("Unable to copy macro file.");
			return;
		}
		refreshEntries(-1);
	}

	void handleEdit() {
		const MacroFileEntry *entry = selectedEntry();
		if (entry == nullptr) return;
		openPathValue = entry->path;
		endModal(cmMRMacroManagerOpenEditor);
	}

	void handleBind() {
		const MacroFileEntry *entry = selectedEntry();
		std::string keySpec;
		std::string err;

		if (entry == nullptr) return;
		if (!captureBindingKeySpec(keySpec)) return;
		if (keySpec.empty()) return;
		if (!rebindMacroFileKey(*entry, keySpec, err)) {
			postMacroDialogError("Unable to bind macro: " + err);
			return;
		}
		messageBox(mfInformation | mfOKButton, "Updated %s with TO %s", entry->fileName.c_str(), keySpec.c_str());
		refreshEntries(selectedIndex());
	}

	void handlePlayback() {
		const MacroFileEntry *entry = selectedEntry();
		if (entry == nullptr) return;
		playbackPathValue = entry->path;
		endModal(cmMRMacroManagerPlayback);
	}

	void handleAddAutoexec() {
		insertMacroIntoAutoexec(selectedIndex(), static_cast<int>(autoexecEntries.size()));
	}

	void handleRemoveAutoexec() {
		removeAutoexecEntry(selectedAutoexecIndex());
	}

	void handleMoveAutoexecBy(int delta) {
		int index = selectedAutoexecIndex();

		if (!activeAutoexecList || index < 0) return;
		moveAutoexecEntry(index, index + delta);
	}

	std::string directoryPath;
	std::vector<MacroFileEntry> entries;
	std::vector<std::string> rows;
	std::vector<bool> rowHasCompileError;
	std::vector<std::string> autoexecEntries;
	std::vector<std::string> autoexecRows;
	MacroManagerListView *macroListView;
	TScrollBar *macroScrollBar;
	MacroManagerListView *autoexecListView;
	TScrollBar *autoexecScrollBar;
	std::string openPathValue;
	std::string playbackPathValue;
	bool activeAutoexecList = false;
};
} // namespace

bool runMacroFileDialog() {
	enum {
		FileNameBufferSize = 1024
	};
	char fileName[FileNameBufferSize];
	ushort dialogResult;

	mr::dialogs::seedFileDialogPath(MRDialogHistoryScope::MacroFile, fileName, sizeof(fileName), "*.mrmac");
	dialogResult = mr::dialogs::execRememberingFileDialogWithData(MRDialogHistoryScope::MacroFile, "*.mrmac", "Load Macro File", "~N~ame", fdOpenButton, fileName);
	if (dialogResult == cmCancel) return false;
	if (!runMacroFileByPath(fileName)) {
		forgetLoadDialogPath(MRDialogHistoryScope::MacroFile, fileName);
		return false;
	}
	rememberLoadDialogPath(MRDialogHistoryScope::MacroFile, fileName);
	return true;
}

bool runMacroManagerDialog() {
	MacroManagerDialog *dialog = new MacroManagerDialog();
	ushort result = cmCancel;
	std::string openPath;
	std::string playbackPath;

	if (dialog == nullptr) return false;
	dialog->finalizeLayout();
	dialog->scrollToOrigin();
	result = TProgram::deskTop->execView(dialog);
	openPath = dialog->openPath();
	playbackPath = dialog->playbackPath();
	TObject::destroy(dialog);

	if (result == cmMRMacroManagerOpenEditor && !openPath.empty()) return openMacroSourceInEditor(openPath);
	if (result == cmMRMacroManagerPlayback && !playbackPath.empty()) return runMacroFileByPath(playbackPath.c_str());
	if (result == cmHelp) static_cast<void>(mrShowProjectHelp());
	return result != cmCancel;
}
