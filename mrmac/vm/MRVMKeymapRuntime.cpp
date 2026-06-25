#include "MRVMKeymapRuntime.hpp"

#include "MRVMEditor.hpp"
#include "MRVMValue.hpp"

#include "../../app/utils/MRStringUtils.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/MRWindowSupport.hpp"
#include "../mrmac.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <sstream>
#include <string_view>
#include <unordered_map>

bool mrvmHasActiveBackgroundEditSession() noexcept;

namespace {
struct MacroKeymapActionCommand {
	const char *name;
	const char *actionId;
};

static thread_local int g_keyReplayDepth = 0;

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

std::string normalizeKeySpecToken(const std::string &spec) {
	std::string trimmed = trimAscii(spec);
	std::string normalized;

	if (trimmed.size() < 3 || trimmed.front() != '<' || trimmed.back() != '>') return std::string();
	trimmed = trimmed.substr(1, trimmed.size() - 2);
	for (char i : trimmed) {
		unsigned char ch = static_cast<unsigned char>(i);
		if (std::isspace(ch) != 0 || ch == '_' || ch == '+') continue;
		normalized.push_back(static_cast<char>(std::toupper(ch)));
	}
	return normalized;
}

bool isAsciiLetterKeyCode(ushort code) noexcept {
	return code >= 'A' && code <= 'Z';
}

bool dispatchSyntheticKeyToUi(const TKey &key, const char *text = nullptr, std::size_t textLength = 0) {
	MREditWindow *win = mrvmEditorActiveWindow();
	TEvent event;

	if (win == nullptr || win->getEditor() == nullptr) return false;

	std::memset(&event, 0, sizeof(event));
	event.what = evKeyDown;
	event.keyDown.keyCode = key.code;
	event.keyDown.controlKeyState = key.mods;
	if (text != nullptr && textLength > 0) {
		if (textLength > sizeof(event.keyDown.text)) textLength = sizeof(event.keyDown.text);
		std::memcpy(event.keyDown.text, text, textLength);
		event.keyDown.textLength = static_cast<uchar>(textLength);
	}
	win->handleEvent(event);
	return true;
}
} // namespace

const char *mrvmKeymapActionIdForMacroCommand(const std::string &name) noexcept {
	static constexpr std::array commands{
	    MacroKeymapActionCommand{"APPEND_BLOCK", "MRMAC_BLOCK_APPEND_TO_BUFFER"},
	    MacroKeymapActionCommand{"BACK_HOME", "MRMAC_DELETE_BACKWARD_TO_HOME"},
	    MacroKeymapActionCommand{"BACK_WORD", "MRMAC_DELETE_BACKWARD_WORD"},
	    MacroKeymapActionCommand{"BLOCK_MARK_LINES", "MR_BLOCK_MARK_LINES"},
	    MacroKeymapActionCommand{"BLOCK_TOGGLE_MARKING", "MR_BLOCK_TOGGLE_MARKING"},
	    MacroKeymapActionCommand{"BLOCK_TOGGLE_VISIBILITY", "MRMAC_BLOCK_TOGGLE_VISIBILITY"},
	    MacroKeymapActionCommand{"BOTTOM_OF_WINDOW", "MRMAC_CURSOR_BOTTOM_OF_WINDOW"},
	    MacroKeymapActionCommand{"CENTER_LINE", "MR_TEXT_CENTER_LINE"},
	    MacroKeymapActionCommand{"CENTER_LINE_ON_SCREEN", "MRMAC_VIEW_CENTER_LINE"},
	    MacroKeymapActionCommand{"COPY_BLOCK_TO_CLIPBOARD", "MRMAC_BLOCK_COPY_TO_CLIPBOARD"},
	    MacroKeymapActionCommand{"CUT_APPEND_BLOCK", "MRMAC_BLOCK_CUT_APPEND_TO_BUFFER"},
	    MacroKeymapActionCommand{"CUT_BLOCK", "MRMAC_BLOCK_MOVE_TO_BUFFER"},
	    MacroKeymapActionCommand{"DEL_CHAR_OR_BLOCK", "MRMAC_DELETE_FORWARD_CHAR_OR_BLOCK"},
	    MacroKeymapActionCommand{"DEL_EOL", "MRMAC_DELETE_TO_EOL"},
	    MacroKeymapActionCommand{"DEL_WORD", "MRMAC_DELETE_FORWARD_WORD"},
	    MacroKeymapActionCommand{"DESKTOP_MOVE_WINDOW_LEFT", "MR_DESKTOP_MOVE_WINDOW_LEFT"},
	    MacroKeymapActionCommand{"DESKTOP_MOVE_WINDOW_RIGHT", "MR_DESKTOP_MOVE_WINDOW_RIGHT"},
	    MacroKeymapActionCommand{"DESKTOP_VIEWPORT_LEFT", "MR_DESKTOP_VIEWPORT_LEFT"},
	    MacroKeymapActionCommand{"DESKTOP_VIEWPORT_RIGHT", "MR_DESKTOP_VIEWPORT_RIGHT"},
	    MacroKeymapActionCommand{"END_OF_BLOCK", "MRMAC_CURSOR_END_OF_BLOCK"},
	    MacroKeymapActionCommand{"FORCE_SAVE", "MR_FILE_FORCE_SAVE"},
	    MacroKeymapActionCommand{"INDENT_BLOCK", "MRMAC_BLOCK_INDENT"},
	    MacroKeymapActionCommand{"JUSTIFY_PARAGRAPH", "MR_JUSTIFY_PARAGRAPH"},
	    MacroKeymapActionCommand{"LIST_MATCHED_FILES", "MRMAC_SEARCH_LIST_MATCHED_FILES"},
	    MacroKeymapActionCommand{"LSP_CODE_ACTIONS", "MR_LSP_CODE_ACTIONS"},
	    MacroKeymapActionCommand{"LSP_COMPLETE", "MR_LSP_COMPLETE"},
	    MacroKeymapActionCommand{"LSP_DEFINITION", "MR_LSP_GOTO_DEFINITION"},
	    MacroKeymapActionCommand{"LSP_HIGHLIGHT", "MR_LSP_DOCUMENT_HIGHLIGHT"},
	    MacroKeymapActionCommand{"LSP_HOVER", "MR_LSP_SHOW_HOVER"},
	    MacroKeymapActionCommand{"LSP_REFERENCES", "MR_LSP_FIND_REFERENCES"},
	    MacroKeymapActionCommand{"LSP_RENAME", "MR_LSP_RENAME"},
	    MacroKeymapActionCommand{"LSP_SIGNATURE", "MR_LSP_SIGNATURE_HELP"},
	    MacroKeymapActionCommand{"LSP_SYMBOLS", "MR_LSP_DOCUMENT_SYMBOLS"},
	    MacroKeymapActionCommand{"LSP_WORKSPACE_SYMBOLS", "MR_LSP_WORKSPACE_SYMBOLS"},
	    MacroKeymapActionCommand{"LOAD_BLOCK", "MR_LOAD_BLOCK_FROM_FILE"},
	    MacroKeymapActionCommand{"MARK_WORD_RIGHT", "MRMAC_BLOCK_MARK_WORD_RIGHT"},
	    MacroKeymapActionCommand{"MULTI_FILE_SEARCH", "MRMAC_SEARCH_MULTI_FILE"},
	    MacroKeymapActionCommand{"MULTI_FILE_SEARCH_REPLACE", "MR_SEARCH_MULTI_FILE_REPLACE"},
	    MacroKeymapActionCommand{"NEXT_SEARCH_RESULT", "MR_SEARCH_RESULTS_NEXT"},
	    MacroKeymapActionCommand{"PASTE_BLOCK", "MRMAC_BLOCK_COPY_FROM_BUFFER"},
	    MacroKeymapActionCommand{"PASTE_FROM_CLIPBOARD", "MRMAC_BLOCK_PASTE_FROM_CLIPBOARD"},
	    MacroKeymapActionCommand{"REDO", "MRMAC_REDO_LAST_UNDO"},
	    MacroKeymapActionCommand{"REFORMAT_DOCUMENT", "MR_TEXT_REFORMAT_DOCUMENT"},
	    MacroKeymapActionCommand{"REFORMAT_PARAGRAPH", "MR_TEXT_REFORMAT_PARAGRAPH"},
	    MacroKeymapActionCommand{"REVERT_FILE", "MR_FILE_REVERT"},
	    MacroKeymapActionCommand{"REPEAT_SEARCH", "MRMAC_SEARCH_REPEAT_LAST"},
	    MacroKeymapActionCommand{"SAVE_ALL", "MR_FILE_SAVE_ALL"},
	    MacroKeymapActionCommand{"SCROLL_DOWN", "MRMAC_VIEW_SCROLL_DOWN"},
	    MacroKeymapActionCommand{"SCROLL_UP", "MRMAC_VIEW_SCROLL_UP"},
	    MacroKeymapActionCommand{"SEARCH", "MRMAC_SEARCH_FORWARD"},
	    MacroKeymapActionCommand{"SEARCH_REPLACE", "MRMAC_SEARCH_REPLACE"},
	    MacroKeymapActionCommand{"SET_LEFT_MARGIN", "MR_SET_LEFT_MARGIN"},
	    MacroKeymapActionCommand{"SET_RIGHT_MARGIN", "MR_SET_RIGHT_MARGIN"},
	    MacroKeymapActionCommand{"SORT_COLUMN_BLOCK_TOGGLE", "MR_SORT_COLUMN_BLOCK_TOGGLE"},
	    MacroKeymapActionCommand{"START_OF_BLOCK", "MRMAC_CURSOR_START_OF_BLOCK"},
	    MacroKeymapActionCommand{"TOGGLE_FORMAT_RULER", "MR_TOGGLE_FORMAT_RULER"},
	    MacroKeymapActionCommand{"TOGGLE_WORD_WRAP", "MR_TOGGLE_WORD_WRAP"},
	    MacroKeymapActionCommand{"TOP_OF_WINDOW", "MRMAC_CURSOR_TOP_OF_WINDOW"},
	    MacroKeymapActionCommand{"UNDO", "MRMAC_UNDO"},
	    MacroKeymapActionCommand{"UNDENT_BLOCK", "MRMAC_BLOCK_UNDENT"},
	    MacroKeymapActionCommand{"WINDOW_CASCADE", "MR_WINDOW_CASCADE"},
	    MacroKeymapActionCommand{"WINDOW_CLOSE", "MR_WINDOW_CLOSE"},
	    MacroKeymapActionCommand{"WINDOW_COPY_BLOCK", "MRMAC_BLOCK_COPY_INTERWINDOW"},
	    MacroKeymapActionCommand{"WINDOW_LIST", "MR_WINDOW_LIST"},
	    MacroKeymapActionCommand{"WINDOW_MOVE_BLOCK", "MRMAC_BLOCK_MOVE_INTERWINDOW"},
	    MacroKeymapActionCommand{"WINDOW_OPEN", "MR_WINDOW_OPEN"},
	    MacroKeymapActionCommand{"WINDOW_NEXT", "MR_WINDOW_NEXT"},
	    MacroKeymapActionCommand{"WINDOW_PREVIOUS", "MR_WINDOW_PREVIOUS"},
	    MacroKeymapActionCommand{"WINDOW_SPLIT_HORIZONTAL", "MR_WINDOW_SPLIT_HORIZONTAL"},
	    MacroKeymapActionCommand{"WINDOW_SPLIT_VERTICAL", "MR_WINDOW_SPLIT_VERTICAL"},
	    MacroKeymapActionCommand{"WINDOW_TILE", "MR_WINDOW_TILE"},
	    MacroKeymapActionCommand{"WINDOW_ZOOM", "MR_WINDOW_ZOOM"},
	    MacroKeymapActionCommand{"MATCH_PARENTHESIS", "MR_MATCH_PARENTHESIS"},
	    MacroKeymapActionCommand{"MACRO_TOGGLE_RECORDING", "MR_MACRO_TOGGLE_RECORDING"},
	    MacroKeymapActionCommand{"SETUP_EDIT_SETTINGS", "MR_SETUP_EDIT_SETTINGS"},
	    MacroKeymapActionCommand{"SETUP_COLOR", "MR_SETUP_COLOR"},
	    MacroKeymapActionCommand{"SETUP_KEYMAP", "MR_SETUP_KEYMAP"},
	    MacroKeymapActionCommand{"SETUP_LSP_SUPPORT", "MR_SETUP_LSP_SUPPORT"},
	    MacroKeymapActionCommand{"SETUP_FILENAME_EXTENSIONS", "MR_SETUP_FILENAME_EXTENSIONS"},
	    MacroKeymapActionCommand{"SETUP_COMPILER_PROFILES", "MR_SETUP_COMPILER_PROFILES"},
	    MacroKeymapActionCommand{"SETUP_PATHS", "MR_SETUP_PATHS"},
	    MacroKeymapActionCommand{"SETUP_BACKUPS_AUTOSAVE", "MR_SETUP_BACKUPS_AUTOSAVE"},
	    MacroKeymapActionCommand{"SETUP_SEARCH_REPLACE_DEFAULTS", "MR_SETUP_SEARCH_REPLACE_DEFAULTS"},
	    MacroKeymapActionCommand{"SETUP_USER_INTERFACE", "MR_SETUP_USER_INTERFACE"},
	    MacroKeymapActionCommand{"SETUP_LIVE_LOGS", "MR_SETUP_LIVE_LOGS"},
	    MacroKeymapActionCommand{"EXIT_SAVE_ALL", "MR_EXIT_DIRTY_SAVE_ALL"}};

	for (const MacroKeymapActionCommand &command : commands)
		if (name == command.name) return command.actionId;
	return nullptr;
}

bool mrvmParseAssignedKeySpec(const std::string &spec, TKey &outKey) {
	std::string token = normalizeKeySpecToken(spec);
	bool wantShift = false;
	bool wantCtrl = false;
	bool wantAlt = false;
	bool wantSuper = false;
	bool changed = true;
	ushort baseCode = 0;
	ushort modifiers = 0;

	if (token.empty()) return false;

	while (changed) {
		changed = false;
		if (token.rfind("SHIFT", 0) == 0) {
			wantShift = true;
			token.erase(0, 5);
			changed = true;
			continue;
		}
		if (token.rfind("SHFT", 0) == 0) {
			wantShift = true;
			token.erase(0, 4);
			changed = true;
			continue;
		}
		if (token.rfind("CTRL", 0) == 0) {
			wantCtrl = true;
			token.erase(0, 4);
			changed = true;
			continue;
		}
		if (token.rfind("ALT", 0) == 0) {
			wantAlt = true;
			token.erase(0, 3);
			changed = true;
			continue;
		}
		if (token.rfind("SUPER", 0) == 0) {
			wantSuper = true;
			token.erase(0, 5);
			changed = true;
			continue;
		}
	}

	if (token.empty()) return false;

	if (token.size() >= 2 && token[0] == 'F' && std::all_of(token.begin() + 1, token.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
		int number = std::atoi(token.c_str() + 1);
		switch (number) {
			case 1:
				baseCode = kbF1;
				break;
			case 2:
				baseCode = kbF2;
				break;
			case 3:
				baseCode = kbF3;
				break;
			case 4:
				baseCode = kbF4;
				break;
			case 5:
				baseCode = kbF5;
				break;
			case 6:
				baseCode = kbF6;
				break;
			case 7:
				baseCode = kbF7;
				break;
			case 8:
				baseCode = kbF8;
				break;
			case 9:
				baseCode = kbF9;
				break;
			case 10:
				baseCode = kbF10;
				break;
			case 11:
				baseCode = kbF11;
				break;
			case 12:
				baseCode = kbF12;
				break;
			default:
				return false;
		}
	} else if (token == "ENTER" || token == "RETURN")
		baseCode = kbEnter;
	else {
		static const std::unordered_map<std::string_view, ushort> tokenToBaseCode = {{"TAB", kbTab}, {"ESC", kbEsc}, {"BS", kbBack}, {"BACK", kbBack}, {"BACKSPACE", kbBack}, {"UP", kbUp}, {"DN", kbDown}, {"DOWN", kbDown}, {"LF", kbLeft}, {"LEFT", kbLeft}, {"RT", kbRight}, {"RIGHT", kbRight}, {"PGUP", kbPgUp}, {"PGDN", kbPgDn}, {"HOME", kbHome}, {"END", kbEnd}, {"INS", kbIns}, {"DEL", kbDel}, {"GREY-", kbGrayMinus}, {"GREY+", kbGrayPlus}, {"GREY*", static_cast<ushort>('*')}, {"SPACE", static_cast<ushort>(' ')}, {"MINUS", static_cast<ushort>('-')}, {"EQUAL", static_cast<ushort>('=')}};
		auto it = tokenToBaseCode.find(token);
		if (it != tokenToBaseCode.end()) {
			baseCode = it->second;
		} else if (token.size() == 1) {
			baseCode = static_cast<ushort>(token[0]);
		} else {
			return false;
		}
	}

	if (wantShift) modifiers |= kbShift;
	if (wantCtrl) modifiers |= kbCtrlShift;
	if (wantAlt) modifiers |= kbAltShift;
	if (wantSuper) modifiers |= kbSuperShift;

	outKey = TKey(baseCode, modifiers);
	return true;
}

bool mrvmParseIndexedBindingHeaders(const std::string &source, std::vector<TKey> &keys) {
	std::size_t i = 0;
	bool foundAny = false;

	keys.clear();
	while (i < source.size()) {
		std::size_t macroPos = source.find('$', i);
		if (macroPos == std::string::npos) break;
		i = macroPos + 1;
		if (!startsWithTokenInsensitive(source, macroPos, "$MACRO")) continue;

		std::size_t p = macroPos + 6;
		while (p < source.size() && std::isspace(static_cast<unsigned char>(source[p])) != 0)
			++p;
		if (p >= source.size()) break;
		std::size_t nameStart = p;
		while (p < source.size()) {
			unsigned char ch = static_cast<unsigned char>(source[p]);
			if (std::isalnum(ch) == 0 && ch != '_') break;
			++p;
		}
		if (p == nameStart) continue;

		std::size_t semicolon = source.find(';', p);
		if (semicolon == std::string::npos) break;

		std::istringstream header(source.substr(p, semicolon - p));
		std::vector<std::string> tokens;
		std::string token;
		while (header >> token)
			tokens.push_back(token);
		for (std::size_t t = 0; t + 1 < tokens.size(); ++t) {
			TKey parsed;
			if (mrvmUpperKey(tokens[t]) != "TO") continue;
			if (!mrvmParseAssignedKeySpec(tokens[t + 1], parsed)) continue;
			bool duplicate = false;
			for (const auto &key : keys)
				if (key == parsed) {
					duplicate = true;
					break;
				}
			if (!duplicate) {
				keys.push_back(parsed);
				foundAny = true;
			}
		}
		i = semicolon + 1;
	}
	return foundAny;
}

bool mrvmBindingKeysEqual(const TKey &lhs, const TKey &rhs) noexcept {
	const ushort lhsModsSansShift = lhs.mods & static_cast<ushort>(~kbShift);
	const ushort rhsModsSansShift = rhs.mods & static_cast<ushort>(~kbShift);

	if (lhs == rhs) return true;
	if (lhs.code != rhs.code || !isAsciiLetterKeyCode(lhs.code)) return false;
	if ((lhs.mods & kbAltShift) == 0 || (rhs.mods & kbAltShift) == 0) return false;
	return lhsModsSansShift == rhsModsSansShift;
}

bool mrvmParseBindingKeyValue(const VirtualMachine::Value &value, TKey &key) {
	if (value.type == TYPE_INT) {
		const int encoded = mrvmValueAsInt(value);
		const ushort code = static_cast<ushort>(encoded & 0xFFFF);
		const ushort mods = static_cast<ushort>((static_cast<unsigned int>(encoded) >> 16) & 0xFFFF);
		key = TKey(code, mods);
		return code != kbNoKey;
	}
	if (!mrvmIsStringLike(value)) return false;
	return mrvmParseAssignedKeySpec(mrvmValueAsString(value), key);
}

std::string mrvmNormalizeMenuKeySpec(std::string keySpec) {
	keySpec = trimAscii(keySpec);
	if (keySpec.size() >= 2 && keySpec.front() == '<' && keySpec.back() == '>') keySpec = keySpec.substr(1, keySpec.size() - 2);
	return keySpec;
}

std::string mrvmMenuLabelFromBindingKey(const TKey &key) {
	struct ComboSpec {
		const char *prefix;
		ushort mods;
	};
	struct NamedKeySpec {
		const char *token;
		ushort code;
	};
	static const ComboSpec combos[] = {{"", 0}, {"Shft", kbShift}, {"Ctrl", kbCtrlShift}, {"Alt", kbAltShift}, {"CtrlShft", static_cast<ushort>(kbCtrlShift | kbShift)}, {"AltShft", static_cast<ushort>(kbAltShift | kbShift)}, {"CtrlAlt", static_cast<ushort>(kbCtrlShift | kbAltShift)}, {"CtrlAltShft", static_cast<ushort>(kbCtrlShift | kbAltShift | kbShift)}, {"Super", kbSuperShift}, {"SuperShft", static_cast<ushort>(kbSuperShift | kbShift)}, {"SuperCtrl", static_cast<ushort>(kbSuperShift | kbCtrlShift)}, {"SuperAlt", static_cast<ushort>(kbSuperShift | kbAltShift)}, {"SuperCtrlShft", static_cast<ushort>(kbSuperShift | kbCtrlShift | kbShift)}, {"SuperAltShft", static_cast<ushort>(kbSuperShift | kbAltShift | kbShift)}, {"SuperCtrlAlt", static_cast<ushort>(kbSuperShift | kbCtrlShift | kbAltShift)}, {"SuperCtrlAltShft", static_cast<ushort>(kbSuperShift | kbCtrlShift | kbAltShift | kbShift)}};
	static const NamedKeySpec named[] = {{"Enter", kbEnter}, {"Tab", kbTab}, {"Esc", kbEsc}, {"Backspace", kbBack}, {"Up", kbUp}, {"Down", kbDown}, {"Left", kbLeft}, {"Right", kbRight}, {"PgUp", kbPgUp}, {"PgDn", kbPgDn}, {"Home", kbHome}, {"End", kbEnd}, {"Ins", kbIns}, {"Del", kbDel}, {"Grey-", kbGrayMinus}, {"Grey+", kbGrayPlus}, {"Grey*", static_cast<ushort>('*')}, {"Space", static_cast<ushort>(' ')}, {"Minus", static_cast<ushort>('-')}, {"Equal", static_cast<ushort>('=')}, {"F1", kbF1}, {"F2", kbF2}, {"F3", kbF3}, {"F4", kbF4}, {"F5", kbF5}, {"F6", kbF6}, {"F7", kbF7}, {"F8", kbF8}, {"F9", kbF9}, {"F10", kbF10}, {"F11", kbF11}, {"F12", kbF12}};
	for (const ComboSpec &combo : combos)
		for (const NamedKeySpec &entry : named)
			if (key == TKey(entry.code, combo.mods)) return std::string(combo.prefix) + entry.token;
	for (const ComboSpec &combo : combos) {
		for (char c = 'A'; c <= 'Z'; ++c)
			if (key == TKey(static_cast<ushort>(c), combo.mods)) return std::string(combo.prefix) + c;
		for (char c = '0'; c <= '9'; ++c)
			if (key == TKey(static_cast<ushort>(c), combo.mods)) return std::string(combo.prefix) + c;
	}
	if (key.code != kbNoKey && key.code < 256 && std::isprint(static_cast<unsigned char>(key.code)) != 0) return std::string(1, static_cast<char>(key.code));
	return std::string();
}

bool mrvmParseBindingModeValue(int rawMode, int &mode) noexcept {
	if (rawMode == MACRO_MODE_EDIT || rawMode == MACRO_MODE_DOS_SHELL || rawMode == MACRO_MODE_ALL) {
		mode = rawMode;
		return true;
	}
	return false;
}

bool mrvmBindingModeMatches(int bindingMode, int currentMode) noexcept {
	return bindingMode == MACRO_MODE_ALL || bindingMode == currentMode;
}

void mrvmRemoveExplicitBindingsForKey(std::vector<MRVMExplicitKeyBinding> &bindings, const TKey &key, int mode) {
	bindings.erase(std::remove_if(bindings.begin(), bindings.end(), [&](const MRVMExplicitKeyBinding &binding) { return binding.mode == mode && mrvmBindingKeysEqual(binding.key, key); }), bindings.end());
}

static bool isCalculatorHotkey(const TKey &key) noexcept {
	return (key.mods & kbAltShift) != 0 && key.code == 'C';
}

void mrvmLogCalculatorHotkeyState(const char *stage, const TKey &key, std::string_view detail) {
	if (!isCalculatorHotkey(key)) return;
	char line[320];
	if (detail.empty()) std::snprintf(line, sizeof(line), "KEYDBG calc stage=%s code=0x%04X mods=0x%04X", stage, static_cast<unsigned>(key.code), static_cast<unsigned>(key.mods));
	else
		std::snprintf(line, sizeof(line), "KEYDBG calc stage=%s code=0x%04X mods=0x%04X %.*s", stage, static_cast<unsigned>(key.code), static_cast<unsigned>(key.mods), static_cast<int>(detail.size()), detail.data());
	mrLogMessage(line);
}

bool mrvmReplayKeyInputSequence(const std::string &sequence) {
	struct KeyReplayGuard {
		KeyReplayGuard() noexcept {
			++g_keyReplayDepth;
		}
		~KeyReplayGuard() {
			--g_keyReplayDepth;
		}
	} replayGuard;

	std::size_t i = 0;

	if (mrvmHasActiveBackgroundEditSession()) return false;
	if (mrvmEditorActiveWindow() == nullptr || mrvmEditorCurrentEditor() == nullptr) return false;

	while (i < sequence.size()) {
		unsigned char ch = static_cast<unsigned char>(sequence[i]);
		if (ch == '<') {
			std::size_t closePos = sequence.find('>', i + 1);
			if (closePos != std::string::npos) {
				TKey key;
				std::string token = sequence.substr(i, closePos - i + 1);
				if (mrvmParseAssignedKeySpec(token, key)) {
					if (!dispatchSyntheticKeyToUi(key)) return false;
					i = closePos + 1;
					continue;
				}
			}
		}

		if (ch == '\r') {
			if (i + 1 < sequence.size() && sequence[i + 1] == '\n') ++i;
			if (!dispatchSyntheticKeyToUi(TKey(kbEnter))) return false;
			++i;
			continue;
		}
		if (ch == '\n') {
			if (!dispatchSyntheticKeyToUi(TKey(kbEnter))) return false;
			++i;
			continue;
		}
		if (ch == '\t') {
			if (!dispatchSyntheticKeyToUi(TKey(kbTab))) return false;
			++i;
			continue;
		}
		if (ch == '\b') {
			if (!dispatchSyntheticKeyToUi(TKey(kbBack))) return false;
			++i;
			continue;
		}
		if (ch == 27) {
			if (!dispatchSyntheticKeyToUi(TKey(kbEsc))) return false;
			++i;
			continue;
		}
		if (ch == 127) {
			if (!dispatchSyntheticKeyToUi(TKey(kbDel))) return false;
			++i;
			continue;
		}

		char textByte = static_cast<char>(ch);
		if (!dispatchSyntheticKeyToUi(TKey(static_cast<ushort>(ch)), &textByte, 1)) return false;
		++i;
	}
	return true;
}

bool mrvmKeyReplayActive() noexcept {
	return g_keyReplayDepth > 0;
}

bool mrvmKeyPairToTKey(int key1, int key2, TKey &key, const char *&text, std::size_t &textLength, char &textByte) {
	static const std::unordered_map<int, ushort> scanCodeToKey = {{59, kbF1}, {60, kbF2}, {61, kbF3}, {62, kbF4}, {63, kbF5}, {64, kbF6}, {65, kbF7}, {66, kbF8}, {67, kbF9}, {68, kbF10}, {133, kbF11}, {134, kbF12}, {72, kbUp}, {80, kbDown}, {75, kbLeft}, {77, kbRight}, {73, kbPgUp}, {81, kbPgDn}, {71, kbHome}, {79, kbEnd}, {82, kbIns}, {83, kbDel}, {15, kbShiftTab}};

	text = nullptr;
	textLength = 0;
	if (key1 != 0) {
		switch (key1) {
			case 13:
				key = TKey(kbEnter);
				return true;
			case 9:
				key = TKey(kbTab);
				return true;
			case 8:
				key = TKey(kbBack);
				return true;
			case 27:
				key = TKey(kbEsc);
				return true;
			case 127:
				key = TKey(kbDel);
				return true;
			default:
				key = TKey(static_cast<ushort>(static_cast<unsigned char>(key1)));
				if (key1 >= 32 && key1 <= 255) {
					textByte = static_cast<char>(static_cast<unsigned char>(key1));
					text = &textByte;
					textLength = 1;
				}
				return true;
		}
	}
	auto it = scanCodeToKey.find(key2);
	if (it == scanCodeToKey.end()) return false;
	key = TKey(it->second);
	return true;
}

bool mrvmPassMacroKeyPairToUi(int key1, int key2) {
	TKey key;
	const char *text = nullptr;
	std::size_t textLength = 0;
	char textByte = '\0';

	if (mrvmHasActiveBackgroundEditSession()) return false;
	if (!mrvmKeyPairToTKey(key1, key2, key, text, textLength, textByte)) return false;
	return dispatchSyntheticKeyToUi(key, text, textLength);
}
