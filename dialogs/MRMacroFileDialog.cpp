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

#include "MRMacroFileDialog.hpp"
#include "MRSetupDialogCommon.hpp"

#include "../mrmac/MRMacroRunner.hpp"
#include "../mrmac/mrmac.h"
#include "../mrmac/mrvm.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "../ui/TMREditWindow.hpp"

#include <algorithm>
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
using mr::dialogs::trimAscii;
enum : ushort {
	cmMRMacroManagerCreate = 220,
	cmMRMacroManagerDelete,
	cmMRMacroManagerCopy,
	cmMRMacroManagerEdit,
	cmMRMacroManagerBind,
	cmMRMacroManagerPlayback,
	cmMRMacroManagerOpenEditor
};

struct MacroFileEntry {
	std::string fileName;
	std::string path;
	std::string macroName;
	std::string keySpec;
	std::string compileError;
};

std::string baseNameOf(const std::string &path) {
	std::size_t pos = path.find_last_of('/');
	if (pos == std::string::npos)
		return path;
	return path.substr(pos + 1);
}

bool fileExists(const std::string &path) {
	return ::access(path.c_str(), F_OK) == 0;
}

bool readTextFile(const std::string &path, std::string &out) {
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	if (!in)
		return false;
	out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	return in.good() || in.eof();
}

bool writeTextFile(const std::string &path, const std::string &content) {
	std::ofstream out(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	out << content;
	return out.good();
}

bool copyFileBinary(const std::string &source, const std::string &dest) {
	std::ifstream in(source.c_str(), std::ios::in | std::ios::binary);
	std::ofstream out(dest.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!in || !out)
		return false;
	out << in.rdbuf();
	return in.good() && out.good();
}

std::string padRight(const std::string &value, std::size_t width) {
	if (value.size() >= width)
		return value.substr(0, width);
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
	if (!readTextFile(entry.path, source))
		return false;
	entry.macroName = firstMacroNameForSource(source, entry.keySpec, entry.compileError);
	return true;
}

bool fileNameLess(const MacroFileEntry &a, const MacroFileEntry &b) {
	std::string lhs = a.fileName;
	std::string rhs = b.fileName;
	for (char & lh : lhs)
		lh = static_cast<char>(std::tolower(static_cast<unsigned char>(lh)));
	for (char & rh : rhs)
		rh = static_cast<char>(std::tolower(static_cast<unsigned char>(rh)));
	return lhs < rhs;
}

std::vector<MacroFileEntry> scanMacroFilesInDirectory(const std::string &directoryPath) {
	std::vector<MacroFileEntry> entries;
	DIR *dir = ::opendir(directoryPath.c_str());

	if (dir == nullptr)
		return entries;
	for (;;) {
		dirent *de = ::readdir(dir);
		if (de == nullptr)
			break;
		if (std::strcmp(de->d_name, ".") == 0 || std::strcmp(de->d_name, "..") == 0)
			continue;

		std::string fileName = de->d_name;
		if (!hasMrmacExtension(fileName))
			continue;

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

std::string sanitizeMacroIdentifier(const std::string &name) {
	std::string out;
	for (char i : name) {
		unsigned char ch = static_cast<unsigned char>(i);
		if (std::isalnum(ch) != 0 || ch == '_')
			out.push_back(static_cast<char>(ch));
		else if (ch == '-' || ch == ' ' || ch == '.')
			out.push_back('_');
	}
	if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0])) != 0)
		out.insert(out.begin(), 'M');
	return out;
}

std::string createMacroTemplateForPath(const std::string &path) {
	std::string base = baseNameOf(path);
	std::size_t dotPos = base.rfind('.');
	if (dotPos != std::string::npos)
		base = base.substr(0, dotPos);
	std::string macroName = sanitizeMacroIdentifier(base);

	std::string source;
	source += "$MACRO ";
	source += macroName;
	source += " FROM EDIT;\n";
	source += "; MRMAC source\n";
	source += "END_MACRO;\n";
	return source;
}

std::string upperAscii(const std::string &value) {
	std::string out = value;
	for (char & i : out)
		i = static_cast<char>(std::toupper(static_cast<unsigned char>(i)));
	return out;
}

bool startsWithTokenInsensitive(const std::string &text, std::size_t pos, const char *token) {
	std::size_t i = 0;
	if (token == nullptr)
		return false;
	while (token[i] != '\0') {
		if (pos + i >= text.size())
			return false;
		if (std::toupper(static_cast<unsigned char>(text[pos + i])) !=
		    std::toupper(static_cast<unsigned char>(token[i])))
			return false;
		++i;
	}
	if (pos + i < text.size()) {
		unsigned char ch = static_cast<unsigned char>(text[pos + i]);
		if (std::isalnum(ch) != 0 || ch == '_')
			return false;
	}
	return true;
}

bool findFirstMacroHeader(const std::string &source, const std::string &firstMacroName,
                          std::size_t &headerStart, std::size_t &headerEnd, std::string &macroName) {
	const std::string expectedName = upperAscii(firstMacroName);
	std::size_t i = 0;

	while (i < source.size()) {
		std::size_t pos = source.find('$', i);
		if (pos == std::string::npos)
			return false;
		i = pos + 1;

		if (!startsWithTokenInsensitive(source, pos, "$MACRO"))
			continue;
		std::size_t p = pos + 6;
		while (p < source.size() && std::isspace(static_cast<unsigned char>(source[p])) != 0)
			++p;
		if (p >= source.size())
			return false;

		std::size_t nameStart = p;
		while (p < source.size()) {
			unsigned char ch = static_cast<unsigned char>(source[p]);
			if (std::isalnum(ch) == 0 && ch != '_')
				break;
			++p;
		}
		if (p == nameStart)
			continue;

		std::string candidateName = source.substr(nameStart, p - nameStart);
		if (!expectedName.empty() && upperAscii(candidateName) != expectedName)
			continue;

		std::size_t semicolonPos = source.find(';', p);
		if (semicolonPos == std::string::npos)
			return false;
		headerStart = pos;
		headerEnd = semicolonPos + 1;
		macroName = candidateName;
		return true;
	}
	return false;
}

std::string rebuildMacroHeader(const std::string &macroName, const std::string &oldHeader,
                               const std::string &keySpec) {
	std::size_t namePos = 6;
	std::vector<std::string> tokens;
	std::ostringstream out;

	while (namePos < oldHeader.size() && std::isspace(static_cast<unsigned char>(oldHeader[namePos])) != 0)
		++namePos;
	while (namePos < oldHeader.size()) {
		unsigned char ch = static_cast<unsigned char>(oldHeader[namePos]);
		if (std::isalnum(ch) == 0 && ch != '_')
			break;
		++namePos;
	}
	std::size_t bodyStart = namePos;
	while (bodyStart < oldHeader.size() && std::isspace(static_cast<unsigned char>(oldHeader[bodyStart])) != 0)
		++bodyStart;
	std::size_t bodyEnd = oldHeader.rfind(';');
	if (bodyEnd == std::string::npos || bodyEnd < bodyStart)
		bodyEnd = oldHeader.size();

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
		if (i < tokens.size())
			tokens.erase(tokens.begin() + static_cast<long>(i));
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
			if (i != 0)
				out << " ";
			out << tokens[i];
		}
	}
	out << ";";
	return out.str();
}

bool keySpecFromEvent(ushort keyCode, ushort controlKeyState, std::string &outToken) {
	struct ComboSpec {
		const char *prefix;
		ushort mods;
	};
	struct NamedKeySpec {
		const char *token;
		ushort code;
	};
	static const ComboSpec combos[] = {{"", 0},
	                                   {"Shft", kbShift},
	                                   {"Ctrl", kbCtrlShift},
	                                   {"Alt", kbAltShift},
	                                   {"CtrlShft", static_cast<ushort>(kbCtrlShift | kbShift)},
	                                   {"AltShft", static_cast<ushort>(kbAltShift | kbShift)},
	                                   {"CtrlAlt", static_cast<ushort>(kbCtrlShift | kbAltShift)},
	                                   {"CtrlAltShft",
	                                    static_cast<ushort>(kbCtrlShift | kbAltShift | kbShift)}};
	static const NamedKeySpec named[] = {{"Enter", kbEnter},
	                                     {"Tab", kbTab},
	                                     {"Esc", kbEsc},
	                                     {"Backspace", kbBack},
	                                     {"Up", kbUp},
	                                     {"Down", kbDown},
	                                     {"Left", kbLeft},
	                                     {"Right", kbRight},
	                                     {"PgUp", kbPgUp},
	                                     {"PgDn", kbPgDn},
	                                     {"Home", kbHome},
	                                     {"End", kbEnd},
	                                     {"Ins", kbIns},
	                                     {"Del", kbDel},
	                                     {"F1", kbF1},
	                                     {"F2", kbF2},
	                                     {"F3", kbF3},
	                                     {"F4", kbF4},
	                                     {"F5", kbF5},
	                                     {"F6", kbF6},
	                                     {"F7", kbF7},
	                                     {"F8", kbF8},
	                                     {"F9", kbF9},
	                                     {"F10", kbF10},
	                                     {"F11", kbF11},
	                                     {"F12", kbF12}};
	TKey pressed(keyCode, controlKeyState);

	for (const ComboSpec &combo : combos)
		for (const NamedKeySpec &entry : named)
			if (pressed == TKey(entry.code, combo.mods)) {
				outToken = "<";
				outToken += combo.prefix;
				outToken += entry.token;
				outToken += ">";
				return true;
			}

	for (const ComboSpec &combo : combos) {
		for (char c = 'A'; c <= 'Z'; ++c)
			if (pressed == TKey(static_cast<ushort>(c), combo.mods)) {
				outToken = "<";
				outToken += combo.prefix;
				outToken.push_back(c);
				outToken += ">";
				return true;
			}
		for (char c = '0'; c <= '9'; ++c)
			if (pressed == TKey(static_cast<ushort>(c), combo.mods)) {
				outToken = "<";
				outToken += combo.prefix;
				outToken.push_back(c);
				outToken += ">";
				return true;
			}
	}

	if (keyCode != kbNoKey && keyCode < 256 && std::isprint(static_cast<unsigned char>(keyCode)) != 0) {
		outToken = "<";
		outToken.push_back(static_cast<char>(keyCode));
		outToken += ">";
		return true;
	}
	return false;
}

class TBindKeyCaptureDialog : public TDialog {
  public:
	TBindKeyCaptureDialog()
	    : TWindowInit(&TDialog::initFrame), TDialog(TRect(0, 0, 50, 8), "Bind Macro Key"),
	      captured_(false), keyCode_(kbNoKey), controlState_(0) {
		options |= ofCentered;
		insert(new TStaticText(TRect(2, 2, 48, 6), "Press key to bind macro.\nEsc = cancel."));
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown) {
			TKey key(event.keyDown);
			if (key == TKey(kbEsc)) {
				endModal(cmCancel);
				clearEvent(event);
				return;
			}
			captured_ = true;
			keyCode_ = event.keyDown.keyCode;
			controlState_ = event.keyDown.controlKeyState;
			endModal(cmOK);
			clearEvent(event);
			return;
		}
		TDialog::handleEvent(event);
	}

	bool captured() const noexcept {
		return captured_;
	}
	ushort keyCode() const noexcept {
		return keyCode_;
	}
	ushort controlState() const noexcept {
		return controlState_;
	}

  private:
	bool captured_;
	ushort keyCode_;
	ushort controlState_;
};

bool captureBindingKeySpec(std::string &keySpec) {
	TBindKeyCaptureDialog *dialog = new TBindKeyCaptureDialog();
	ushort result;

	keySpec.clear();
	if (dialog == nullptr)
		return false;
	result = TProgram::deskTop->execView(dialog);
	bool captured = dialog->captured();
	ushort keyCode = dialog->keyCode();
	ushort controlState = dialog->controlState();
	TObject::destroy(dialog);

	if (result == cmCancel || !captured)
		return true;
	if (!keySpecFromEvent(keyCode, controlState, keySpec)) {
		messageBox(mfError | mfOKButton, "Unsupported key combination.");
		return false;
	}
	return true;
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
	if (!mrvmLoadMacroFile(entry.path, &errorText))
		return false;
	rememberLoadDialogPath(entry.path.c_str());
	return true;
}

bool openMacroSourceInEditor(const std::string &path) {
	TMREditWindow *target = findReusableEmptyWindow(currentEditWindow());
	if (target == nullptr)
		target = createEditorWindow(baseNameOf(path).c_str());
	if (target == nullptr)
		return false;
	if (!target->loadFromFile(path.c_str())) {
		messageBox(mfError | mfOKButton, "Unable to load macro file:\n%s", path.c_str());
		return false;
	}
	static_cast<void>(mrActivateEditWindow(target));
	return true;
}

class MacroManagerListView : public TListViewer {
  public:
	MacroManagerListView(const TRect &bounds, TScrollBar *aVScrollBar,
	                     const std::vector<std::string> &aItems,
	                     const std::vector<bool> &aErrorFlags) noexcept
	    : TListViewer(bounds, 1, nullptr, aVScrollBar), items_(aItems), errorFlags_(aErrorFlags) {
		setRange(static_cast<short>(items_.size()));
	}

	void setItems(const std::vector<std::string> &items, const std::vector<bool> &errorFlags) {
		items_ = items;
		errorFlags_ = errorFlags;
		setRange(static_cast<short>(items_.size()));
		if (items_.empty())
			focusItemNum(0);
		else if (focused >= range)
			focusItemNum(range - 1);
	}

	void draw() override {
		TListViewer::draw();

		unsigned char errorBios = 0;
		short indent = hScrollBar != nullptr ? hScrollBar->value : 0;
		short colWidth = size.x / numCols + 1;
		TColorAttr errorColor;

		if (!configuredColorSlotOverride(kMrPaletteMessageError, errorBios))
			return;
		errorColor = TColorAttr(errorBios);

		for (short i = 0; i < size.y; ++i) {
			for (short j = 0; j < numCols; ++j) {
				short item = static_cast<short>(j * size.y + i + topItem);
				short curCol = static_cast<short>(j * colWidth);
				char text[256];
				const int textPos = 0;
				int visPos = 0;
				TDrawBuffer mark;

				if (item < 0 || item >= range || !hasCompileError(item))
					continue;
				getText(text, item, 255);
				if (text[0] != '!')
					continue;
				visPos = textPos - indent;
				if (visPos < 0 || visPos >= colWidth - 1)
					continue;
				mark.moveChar(0, '!', errorColor, 1);
				writeLine(static_cast<short>(curCol + 1 + visPos), i, 1, 1, mark);
			}
		}
	}

	void getText(char *dest, short item, short maxLen) override {
		std::size_t copyLen;
		if (dest == nullptr || maxLen <= 0)
			return;
		if (item < 0 || static_cast<std::size_t>(item) >= items_.size()) {
			dest[0] = EOS;
			return;
		}
		copyLen = static_cast<std::size_t>(maxLen - 1);
		std::strncpy(dest, items_[static_cast<std::size_t>(item)].c_str(), copyLen);
		dest[copyLen] = EOS;
	}

	void handleEvent(TEvent &event) override {
		const bool isDoubleClickPlayback = event.what == evMouseDown &&
		                                   (event.mouse.buttons & mbLeftButton) != 0 &&
		                                   (event.mouse.eventFlags & meDoubleClick) != 0;

		TListViewer::handleEvent(event);

		if (!isDoubleClickPlayback || owner == nullptr)
			return;
		message(owner, evCommand, cmMRMacroManagerPlayback, this);
		clearEvent(event);
	}

  private:
	bool hasCompileError(short item) const noexcept {
		return item >= 0 && static_cast<std::size_t>(item) < errorFlags_.size() &&
		       errorFlags_[static_cast<std::size_t>(item)];
	}

	std::vector<std::string> items_;
	std::vector<bool> errorFlags_;
};

class MacroManagerDialog : public TDialog {
  public:
	MacroManagerDialog()
	    : TWindowInit(&TDialog::initFrame), TDialog(centeredBounds(), "MACRO MANAGER"),
	      directory_(defaultMacroDirectoryPath()),  listView_(nullptr),
	      scrollBar_(nullptr) {
		int width = size.x;
		int height = size.y;
		int listLeft = 3;
		int listRight = width - 4;
		int listWidth = std::max(1, listRight - listLeft);
		int gap = 2;
		int row1Width = 14 + gap + 14 + gap + 12;
		int row1Left = listLeft + std::max(0, (listWidth - row1Width) / 2);
		int row2Width = 12 + gap + 12;
		int row2Left = listLeft + std::max(0, (listWidth - row2Width) / 2);
		int bottomWidth = 16 + gap + 13 + gap + 12;
		int bottomLeft = listLeft + std::max(0, (listWidth - bottomWidth) / 2);

		insert(new TButton(TRect(row1Left, 2, row1Left + 14, 4), "~C~reate<Ins>", cmMRMacroManagerCreate,
		                   bfNormal));
		insert(new TButton(TRect(row1Left + 14 + gap, 2, row1Left + 14 + gap + 14, 4), "~D~elete<Del>",
		                   cmMRMacroManagerDelete, bfNormal));
		insert(new TButton(TRect(row1Left + 14 + gap + 14 + gap, 2, row1Left + row1Width, 4), "C~o~py<F4>",
		                   cmMRMacroManagerCopy, bfNormal));
		insert(new TButton(TRect(row2Left, 4, row2Left + 12, 6), "~E~dit<F3>", cmMRMacroManagerEdit, bfNormal));
		insert(new TButton(TRect(row2Left + 12 + gap, 4, row2Left + row2Width, 6), "~B~ind<F2>",
		                   cmMRMacroManagerBind, bfNormal));

		scrollBar_ = new TScrollBar(TRect(width - 4, 7, width - 3, height - 4));
		insert(scrollBar_);
		listView_ = new MacroManagerListView(TRect(3, 7, width - 4, height - 4), scrollBar_,
		                                     std::vector<std::string>(), std::vector<bool>());
		insert(listView_);

		insert(new TButton(TRect(bottomLeft, height - 3, bottomLeft + 16, height - 1), "Playback<ENTER>",
		                   cmMRMacroManagerPlayback, bfDefault));
		insert(new TButton(TRect(bottomLeft + 16 + gap, height - 3, bottomLeft + 16 + gap + 13, height - 1),
		                   "Done<ESC>", cmCancel, bfNormal));
		insert(new TButton(TRect(bottomLeft + 16 + gap + 13 + gap, height - 3, bottomLeft + bottomWidth,
		                         height - 1),
		                   "Help<F1>", cmHelp, bfNormal));

		refreshEntries(-1);
		if (listView_ != nullptr)
			listView_->select();
	}

	const std::string &openPath() const noexcept {
		return openPath_;
	}

	void handleEvent(TEvent &event) override {
		TDialog::handleEvent(event);

		if (event.what == evKeyDown) {
			switch (ctrlToArrow(event.keyDown.keyCode)) {
				case kbIns:
					handleCreate();
					clearEvent(event);
					return;
				case kbDel:
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
					handlePlayback();
					clearEvent(event);
					return;
				case kbF1:
					static_cast<void>(mrShowProjectHelp());
					clearEvent(event);
					return;
			}
		}

		if (event.what != evCommand)
			return;

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
			case cmMRMacroManagerPlayback:
				handlePlayback();
				clearEvent(event);
				break;
			case cmHelp:
				static_cast<void>(mrShowProjectHelp());
				clearEvent(event);
				break;
		}
	}

  private:
	static TRect centeredBounds() {
		TRect desk = TProgram::deskTop->getExtent();
		int width = std::min(58, std::max(52, desk.b.x - desk.a.x - 4));
		int height = std::min(16, std::max(14, desk.b.y - desk.a.y - 2));
		int left = desk.a.x + (desk.b.x - desk.a.x - width) / 2;
		int top = desk.a.y + (desk.b.y - desk.a.y - height) / 2;
		return TRect(left, top, left + width, top + height);
	}

	int selectedIndex() const {
		if (listView_ == nullptr)
			return -1;
		short idx = listView_->focused;
		if (idx < 0 || static_cast<std::size_t>(idx) >= entries_.size())
			return -1;
		return idx;
	}

	const MacroFileEntry *selectedEntry() const {
		int idx = selectedIndex();
		if (idx < 0)
			return nullptr;
		return &entries_[static_cast<std::size_t>(idx)];
	}

	void refreshEntries(int keepIndex) {
		entries_ = scanMacroFilesInDirectory(directory_);
		rows_.clear();
		rowHasCompileError_.clear();
		if (entries_.empty())
			rows_.push_back("(none available)");
		else
			for (auto & entrie : entries_) {
				rows_.push_back(rowTextFor(entrie));
				rowHasCompileError_.push_back(!entrie.compileError.empty());
			}
		if (entries_.empty())
			rowHasCompileError_.push_back(false);
		if (listView_ != nullptr) {
			listView_->setItems(rows_, rowHasCompileError_);
			if (!rows_.empty()) {
				int target = keepIndex;
				if (target < 0 || target >= static_cast<int>(rows_.size()))
					target = 0;
				listView_->focusItemNum(target);
			}
		}
	}

	void handleCreate() {
		enum { BufferSize = 512 };
		char pathBuffer[BufferSize];
		std::string path;

		std::memset(pathBuffer, 0, sizeof(pathBuffer));
		std::strncpy(pathBuffer, "new_macro.mrmac", sizeof(pathBuffer) - 1);
		if (inputBox("MACRO MANAGER", "~F~ile name", pathBuffer,
		             static_cast<uchar>(sizeof(pathBuffer) - 1)) == cmCancel)
			return;

		path = trimAscii(pathBuffer);
		if (path.empty())
			return;
		path = ensureMrmacExtension(path);
		if (path.find('/') == std::string::npos)
			path = directory_ + "/" + path;
		path = normalizeTvPathSeparators(path);

		if (fileExists(path)) {
			if (messageBox(mfConfirmation | mfYesButton | mfNoButton,
			               "Macro file exists.\nOverwrite?\n%s", path.c_str()) != cmYes)
				return;
		}
		if (!writeTextFile(path, createMacroTemplateForPath(path))) {
			messageBox(mfError | mfOKButton, "Unable to create macro file:\n%s", path.c_str());
			return;
		}
		rememberLoadDialogPath(path.c_str());
		openPath_ = path;
		endModal(cmMRMacroManagerOpenEditor);
	}

	void handleDelete() {
		const MacroFileEntry *entry = selectedEntry();
		if (entry == nullptr)
			return;
		if (messageBox(mfConfirmation | mfYesButton | mfNoButton,
		               "Delete macro file?\n%s", entry->path.c_str()) != cmYes)
			return;
		if (::unlink(entry->path.c_str()) != 0) {
			messageBox(mfError | mfOKButton, "Unable to delete:\n%s", entry->path.c_str());
			return;
		}
		refreshEntries(selectedIndex());
	}

	void handleCopy() {
		enum { BufferSize = 512 };
		const MacroFileEntry *entry = selectedEntry();
		char pathBuffer[BufferSize];
		std::string destPath;
		std::string suggested;
		if (entry == nullptr)
			return;

		suggested = entry->fileName;
		std::size_t dotPos = suggested.rfind('.');
		if (dotPos == std::string::npos)
			suggested += "_copy.mrmac";
		else
			suggested.insert(dotPos, "_copy");

		std::memset(pathBuffer, 0, sizeof(pathBuffer));
		std::strncpy(pathBuffer, suggested.c_str(), sizeof(pathBuffer) - 1);
		if (inputBox("MACRO MANAGER", "Copy to ~F~ile", pathBuffer,
		             static_cast<uchar>(sizeof(pathBuffer) - 1)) == cmCancel)
			return;

		destPath = trimAscii(pathBuffer);
		if (destPath.empty())
			return;
		destPath = ensureMrmacExtension(destPath);
		if (destPath.find('/') == std::string::npos)
			destPath = directory_ + "/" + destPath;
		destPath = normalizeTvPathSeparators(destPath);

		if (fileExists(destPath)) {
			if (messageBox(mfConfirmation | mfYesButton | mfNoButton,
			               "Target exists.\nOverwrite?\n%s", destPath.c_str()) != cmYes)
				return;
		}
		if (!copyFileBinary(entry->path, destPath)) {
			messageBox(mfError | mfOKButton, "Unable to copy macro file.");
			return;
		}
		refreshEntries(-1);
	}

	void handleEdit() {
		const MacroFileEntry *entry = selectedEntry();
		if (entry == nullptr)
			return;
		openPath_ = entry->path;
		endModal(cmMRMacroManagerOpenEditor);
	}

	void handleBind() {
		const MacroFileEntry *entry = selectedEntry();
		std::string keySpec;
		std::string err;

		if (entry == nullptr)
			return;
		if (!captureBindingKeySpec(keySpec))
			return;
		if (keySpec.empty())
			return;
		if (!rebindMacroFileKey(*entry, keySpec, err)) {
			messageBox(mfError | mfOKButton, "Unable to bind macro:\n\n%s", err.c_str());
			return;
		}
		messageBox(mfInformation | mfOKButton, "Updated %s with TO %s", entry->fileName.c_str(),
		           keySpec.c_str());
		refreshEntries(selectedIndex());
	}

	void handlePlayback() {
		const MacroFileEntry *entry = selectedEntry();
		if (entry == nullptr)
			return;
		runMacroFileByPath(entry->path.c_str());
	}

	std::string directory_;
	std::vector<MacroFileEntry> entries_;
	std::vector<std::string> rows_;
	std::vector<bool> rowHasCompileError_;
	MacroManagerListView *listView_;
	TScrollBar *scrollBar_;
	std::string openPath_;
};
} // namespace

bool runMacroFileDialog() {
	enum { FileNameBufferSize = 1024 };
	char fileName[FileNameBufferSize];
	ushort dialogResult;

	initRememberedLoadDialogPath(fileName, sizeof(fileName), "*.mrmac");
	dialogResult = mr::dialogs::execDialogRawWithData(
	    new TFileDialog("*.mrmac", "Load Macro File", "~N~ame", fdOpenButton, kFileDialogHistoryId), fileName);
	if (dialogResult == cmCancel)
		return false;
	rememberLoadDialogPath(fileName);
	return runMacroFileByPath(fileName);
}

bool runMacroManagerDialog() {
	MacroManagerDialog *dialog = new MacroManagerDialog();
	ushort result = cmCancel;
	std::string openPath;

	if (dialog == nullptr)
		return false;
	result = TProgram::deskTop->execView(dialog);
	openPath = dialog->openPath();
	TObject::destroy(dialog);

	if (result == cmMRMacroManagerOpenEditor && !openPath.empty())
		return openMacroSourceInEditor(openPath);
	if (result == cmHelp)
		static_cast<void>(mrShowProjectHelp());
	return result != cmCancel;
}
