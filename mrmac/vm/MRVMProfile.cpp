#include "MRVMProfile.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

#include "../mrmac.h"

namespace {
std::string upperProfileKey(const std::string &value) {
	std::string result(value);

	for (char &ch : result)
		ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
	return result;
}

void appendUniqueProfileString(std::vector<std::string> &values, const std::string &value) {
	if (value.empty()) return;
	if (std::find(values.begin(), values.end(), value) == values.end()) values.push_back(value);
}

void noteExecutionFlags(MRMacroExecutionProfile &profile, unsigned flags, const std::string &symbol = std::string()) {
	if (flags == 0) return;
	profile.flags |= flags;
	if (symbol.empty()) return;
	if ((flags & mrefStagedWrite) != 0) appendUniqueProfileString(profile.stagedWriteSymbols, symbol);
	if ((flags & mrefUiAffinity) != 0) appendUniqueProfileString(profile.uiAffinitySymbols, symbol);
	if ((flags & mrefExternalIo) != 0) appendUniqueProfileString(profile.externalIoSymbols, symbol);
}

bool skipBytecodeBytes(std::size_t length, std::size_t &ip, std::size_t count) {
	if (count > length || ip > length - count) return false;
	ip += count;
	return true;
}

bool readBytecodeCString(const unsigned char *bytecode, std::size_t length, std::size_t &ip, std::string &out) {
	std::size_t start = ip;

	if (bytecode == nullptr || ip >= length) return false;
	while (ip < length && bytecode[ip] != '\0')
		++ip;
	if (ip >= length) return false;
	out.assign(reinterpret_cast<const char *>(bytecode + start), ip - start);
	++ip;
	return true;
}

unsigned classifyPureOpcode(unsigned char opcode) {
	switch (opcode) {
		case OP_PUSH_I:
		case OP_PUSH_R:
		case OP_PUSH_S:
		case OP_STORE_VAR:
		case OP_LOAD_VAR:
		case OP_GOTO:
		case OP_DEF_VAR:
		case OP_HASH_LOAD:
		case OP_HASH_STORE:
		case OP_HASH_LOAD_VALUE:
		case OP_HASH_STORE_VALUE:
		case OP_ARRAY_LOAD:
		case OP_ARRAY_STORE:
		case OP_ARRAY_LOAD_VALUE:
		case OP_JZ:
		case OP_CALL:
		case OP_RET:
		case OP_VAL:
		case OP_RVAL:
		case OP_FIRST_GLOBAL:
		case OP_NEXT_GLOBAL:
		case OP_HALT:
		case OP_ADD:
		case OP_SUB:
		case OP_MUL:
		case OP_DIV:
		case OP_MOD:
		case OP_NEG:
		case OP_CMP_EQ:
		case OP_CMP_NE:
		case OP_CMP_LT:
		case OP_CMP_GT:
		case OP_CMP_LE:
		case OP_CMP_GE:
		case OP_AND:
		case OP_OR:
		case OP_NOT:
		case OP_SHL:
		case OP_SHR:
		case OP_BIT_AND:
		case OP_BIT_OR:
		case OP_BIT_XOR:
			return mrefBackgroundSafe;
		default:
			return 0;
	}
}

unsigned classifyIntrinsicName(const std::string &name) {
	if (name == "VERSION") return mrefBackgroundSafe;
	if (name == "FILE_EXISTS" || name == "FIRST_FILE" || name == "NEXT_FILE" || name == "GET_ENVIRONMENT") return mrefExternalIo;
	if (name == "FILE_ATTR" || name == "COPY_FILE" || name == "RENAME_FILE" || name == "SWITCH_FILE") return mrefUiAffinity | mrefExternalIo;
	if (name == "GLOBAL_STR" || name == "GLOBAL_INT" || name == "INQ_MACRO") return mrefUiAffinity;
	if (name == "BLOCK_TEXT") return mrefUiAffinity;
	if (name == "CHECK_KEY" || name == "BAR_MENU" || name == "V_MENU" || name == "STRING_IN" || name == "UI_EXEC" || name == "UI_TEXT" || name == "UI_INDEX") return mrefUiAffinity;
	if (name == "UTF8") return mrefBackgroundSafe;
	if (name == "OS_BACK" || name == "OS_COLOR") return mrefUiAffinity;
	if (name == "SCREEN_LENGTH" || name == "SCREEN_WIDTH" || name == "WHEREX" || name == "WHEREY") return mrefUiAffinity;
	if (name == "SEARCH_FWD" || name == "SEARCH_BWD" || name == "GET_WORD") return mrefUiAffinity;
	return mrefBackgroundSafe;
}

unsigned classifyProcVarName(const std::string &name) {
	if (name == "EXPAND_TABS" || name == "TABS_TO_SPACES") return mrefBackgroundSafe;
	return mrefUiAffinity;
}

unsigned classifyLoadVarName(const std::string &name) {
	if (name == "FIRST_MACRO" || name == "NEXT_MACRO") return mrefUiAffinity;
	if (name == "IGNORE_CASE" || name == "REG_EXP_STAT" || name == "TAB_EXPAND" || name == "DISPLAY_TABS") return mrefUiAffinity;
	if (name == "VIRTUAL_DESKTOPS" || name == "CYCLIC_VIRTUAL_DESKTOPS") return mrefUiAffinity;
	if (name == "DOC_MODE" || name == "PRINT_MARGIN") return mrefUiAffinity;
	if (name == "GLOBAL_HASH") return mrefUiAffinity;
	if (name == "INSERT_MODE" || name == "INDENT_LEVEL" || name == "GET_LINE" || name == "CUR_CHAR" || name == "C_COL" || name == "C_LINE" || name == "C_ROW" || name == "C_PAGE" || name == "PG_LINE" || name == "AT_EOF" || name == "AT_EOL" || name == "BLOCK_STAT" || name == "BLOCK_LINE1" || name == "BLOCK_LINE2" || name == "BLOCK_COL1" || name == "BLOCK_COL2" || name == "MARKING" || name == "FILE_CHANGED" || name == "FILE_NAME") return mrefUiAffinity;
	if (name == "CUR_WINDOW" || name == "LINK_STAT" || name == "WIN_X1" || name == "WIN_Y1" || name == "WIN_X2" || name == "WIN_Y2" || name == "WINDOW_COUNT" || name == "KEY1" || name == "KEY2" || name == "FIRST_SAVE" || name == "BUFFER_ID" || name == "TMP_FILE" || name == "TMP_FILE_NAME" || name == "LAST_FILE_ATTR" || name == "LAST_FILE_SIZE" || name == "LAST_FILE_TIME" || name == "CUR_FILE_ATTR" || name == "CUR_FILE_SIZE" || name == "READ_ONLY" || name == "FOUND_X" || name == "FOUND_Y" || name == "FOUND_STR" || name == "SEARCH_FILE") return mrefUiAffinity;
	return 0;
}

unsigned classifyStoreVarName(const std::string &name) {
	if (name == "IGNORE_CASE" || name == "REG_EXP_STAT" || name == "TAB_EXPAND" || name == "INSERT_MODE" || name == "INDENT_LEVEL" || name == "FILE_CHANGED" || name == "FILE_NAME" || name == "VIRTUAL_DESKTOPS" || name == "CYCLIC_VIRTUAL_DESKTOPS") return mrefUiAffinity | mrefStagedWrite;
	if (name == "DOC_MODE" || name == "PRINT_MARGIN") return mrefUiAffinity;
	return 0;
}

bool isKeymapActionMacroCommand(const std::string &name) {
	static constexpr const char *commands[] = {"APPEND_BLOCK",
	                                           "BACK_HOME",
	                                           "BACK_WORD",
	                                           "BLOCK_MATH",
	                                           "BOTTOM_OF_WINDOW",
	                                           "CENTER_LINE",
	                                           "CENTER_LINE_ON_SCREEN",
	                                           "COPY_BLOCK_TO_CLIPBOARD",
	                                           "CUT_APPEND_BLOCK",
	                                           "CUT_BLOCK",
	                                           "DEL_CHAR_OR_BLOCK",
	                                           "DEL_EOL",
	                                           "DEL_WORD",
	                                           "END_OF_BLOCK",
	                                           "FORCE_SAVE",
	                                           "INDENT_BLOCK",
	                                           "JUSTIFY_PARAGRAPH",
	                                           "LIST_MATCHED_FILES",
	                                           "LOAD_BLOCK",
	                                           "MARK_WORD_RIGHT",
	                                           "MULTI_FILE_SEARCH",
	                                           "NEXT_SEARCH_RESULT",
	                                           "PASTE_BLOCK",
	                                           "PASTE_FROM_CLIPBOARD",
	                                           "REDO",
	                                           "REFORMAT_DOCUMENT",
	                                           "REFORMAT_PARAGRAPH",
	                                           "REVERT_FILE",
	                                           "REPEAT_SEARCH",
	                                           "SAVE_ALL",
	                                           "SCROLL_DOWN",
	                                           "SCROLL_UP",
	                                           "SEARCH",
	                                           "SEARCH_REPLACE",
	                                           "SET_LEFT_MARGIN",
	                                           "SET_RIGHT_MARGIN",
	                                           "SORT_COLUMN_BLOCK_TOGGLE",
	                                           "START_OF_BLOCK",
	                                           "TOGGLE_FORMAT_RULER",
	                                           "TOGGLE_WORD_WRAP",
	                                           "TOP_OF_WINDOW",
	                                           "UNDO",
	                                           "UNDENT_BLOCK",
	                                           "WINDOW_COPY_BLOCK",
	                                           "WINDOW_MOVE_BLOCK",
	                                           "EXIT_SAVE_ALL"};

	for (const char *command : commands)
		if (name == command) return true;
	return false;
}

unsigned classifyProcName(const std::string &name) {
	if (name == "MRSETUP" || name == "MRCOMPILERPROFILE") return mrefUiAffinity;
	if (name == "MAKE_MESSAGE") return mrefUiAffinity;
	if (name == "REGISTER_MENU_ITEM" || name == "REMOVE_MENU_ITEM") return mrefUiAffinity;
	if (name == "CREATE_GLOBAL_STR" || name == "SET_GLOBAL_STR" || name == "SET_GLOBAL_INT" || name == "SET_GLOBAL_HASH" || name == "UNLOAD_MACRO") return name == "UNLOAD_MACRO" ? mrefUiAffinity : (mrefUiAffinity | mrefStagedWrite);
	if (name == "LOAD_MACRO_FILE" || name == "CHANGE_DIR" || name == "DEL_FILE" || name == "SET_FILE_ATTR") return mrefExternalIo;
	if (name == "SHELL_TO_OS") return mrefUiAffinity | mrefExternalIo;
	if (name == "LOAD_FILE" || name == "SAVE_FILE" || name == "SAVE_BLOCK") return mrefUiAffinity | mrefExternalIo;
	if (name == "UI_DIALOG" || name == "UI_LABEL" || name == "UI_BUTTON" || name == "UI_DISPLAY" || name == "UI_INPUT" || name == "UI_LISTBOX") return mrefUiAffinity;
	if (name == "SAVE_SETTINGS") return mrefUiAffinity | mrefExternalIo;
	if (name == "BEEP") return mrefUiAffinity;
	if (name == "WRITE_SOD") return mrefUiAffinity;
	if (name == "REPLACE" || name == "TEXT" || name == "PUT_LINE" || name == "CR" || name == "KEY_IN" || name == "DEL_CHAR" || name == "DEL_CHARS" || name == "DEL_LINE" || name == "INDENT" || name == "UNDENT" || name == "COPY_BLOCK" || name == "MOVE_BLOCK" || name == "DELETE_BLOCK" || name == "ERASE_WINDOW" || name == "WINDOW_COPY" || name == "WINDOW_MOVE") return mrefUiAffinity | mrefStagedWrite;
	if (name == "SNIPPET_START" || name == "SNIPPETS_UNLOAD" || name == "SNIPPET_NEXT_PLACEHOLDER" || name == "SNIPPET_PREV_PLACEHOLDER") return mrefUiAffinity;
	if (name == "RUN_MACRO") return mrefUiAffinity | mrefStagedWrite;
	if (name == "DELAY") return mrefBackgroundSafe;
	if (isKeymapActionMacroCommand(name)) return mrefUiAffinity;
	if (name == "SET_RANDOM_MARK" || name == "GET_RANDOM_MARK" || name == "EXTEND_BLOCK_BY_MOTION") return mrefUiAffinity;
	if (name == "SET_INDENT_LEVEL" || name == "LEFT" || name == "RIGHT" || name == "UP" || name == "DOWN" || name == "HOME" || name == "EOL" || name == "TOF" || name == "EOF" || name == "WORD_LEFT" || name == "WORD_RIGHT" || name == "FIRST_WORD" || name == "MARK_POS" || name == "GOTO_MARK" || name == "POP_MARK" || name == "PAGE_UP" || name == "PAGE_DOWN" || name == "NEXT_PAGE_BREAK" || name == "LAST_PAGE_BREAK" || name == "TAB_RIGHT" || name == "TAB_LEFT" || name == "BLOCK_BEGIN" || name == "BLOCK_LINE" || name == "COL_BLOCK_BEGIN" || name == "BLOCK_COL" || name == "STR_BLOCK_BEGIN" || name == "BLOCK_END" || name == "BLOCK_OFF" || name == "CREATE_WINDOW" || name == "DELETE_WINDOW" || name == "MODIFY_WINDOW" || name == "LINK_WINDOW" || name == "UNLINK_WINDOW" || name == "ZOOM" || name == "REDRAW" || name == "NEW_SCREEN" || name == "READ_KEY" || name == "PUSH_KEY" || name == "PASS_KEY" || name == "PUSH_LABELS" || name == "POP_LABELS" || name == "FLABEL" || name == "MACRO_TO_KEY" ||
	    name == "CMD_TO_KEY" || name == "UNASSIGN_KEY" || name == "UNASSIGN_ALL_KEYS" || name == "KEY_RECORD" || name == "PLAY_KEY_MACRO" || name == "SAVE_OS_SCREEN" || name == "REST_OS_SCREEN" || name == "QUIT" || name == "GOTO_LINE" || name == "GOTO_COL" || name == "SWITCH_WINDOW" || name == "SIZE_WINDOW" || name == "MOVE_WIN_TO_NEXT_DESKTOP" || name == "MOVE_WIN_TO_PREV_DESKTOP" || name == "MOVE_VIEWPORT_RIGHT" || name == "MOVE_VIEWPORT_LEFT" || name == "SAVE_WORKSPACE" || name == "LOAD_WORKSPACE" || name == "SAVE_SETTINGS")
		return mrefUiAffinity;
	return mrefUiAffinity;
}

unsigned classifyTvCallName(const std::string &name) {
	if (name == "MESSAGEBOX") return mrefUiAffinity;
	return mrefUiAffinity;
}

bool isSupportedStagedSymbol(const std::string &value) noexcept {
	static const char *const kAllowed[] = {"TEXT",
	                                       "PUT_LINE",
	                                       "CR",
	                                       "DEL_CHAR",
	                                       "DEL_CHARS",
	                                       "DEL_LINE",
	                                       "REPLACE",
	                                       "GET_LINE",
	                                       "CUR_CHAR",
	                                       "GET_WORD",
	                                       "C_COL",
	                                       "C_LINE",
	                                       "C_ROW",
	                                       "C_PAGE",
	                                       "PG_LINE",
	                                       "AT_EOF",
	                                       "AT_EOL",
	                                       "INSERT_MODE",
	                                       "INDENT_LEVEL",
	                                       "SET_INDENT_LEVEL",
	                                       "LEFT",
	                                       "RIGHT",
	                                       "UP",
	                                       "DOWN",
	                                       "HOME",
	                                       "EOL",
	                                       "TOF",
	                                       "EOF",
	                                       "WORD_LEFT",
	                                       "WORD_RIGHT",
	                                       "FIRST_WORD",
	                                       "GOTO_LINE",
	                                       "GOTO_COL",
	                                       "TAB_RIGHT",
	                                       "TAB_LEFT",
	                                       "INDENT",
	                                       "UNDENT",
	                                       "MARK_POS",
	                                       "GOTO_MARK",
	                                       "POP_MARK",
	                                       "PAGE_UP",
	                                       "PAGE_DOWN",
	                                       "NEXT_PAGE_BREAK",
	                                       "LAST_PAGE_BREAK",
	                                       "SEARCH_FWD",
	                                       "SEARCH_BWD",
	                                       "RUN_MACRO",
	                                       "BLOCK_BEGIN",
	                                       "BLOCK_LINE",
	                                       "COL_BLOCK_BEGIN",
	                                       "BLOCK_COL",
	                                       "STR_BLOCK_BEGIN",
	                                       "BLOCK_END",
	                                       "BLOCK_OFF",
	                                       "COPY_BLOCK",
	                                       "MOVE_BLOCK",
	                                       "DELETE_BLOCK",
	                                       "ERASE_WINDOW",
	                                       "BLOCK_STAT",
	                                       "BLOCK_LINE1",
	                                       "BLOCK_LINE2",
	                                       "BLOCK_COL1",
	                                       "BLOCK_COL2",
	                                       "MARKING",
	                                       "FIRST_SAVE",
	                                       "BUFFER_ID",
	                                       "TMP_FILE",
	                                       "TMP_FILE_NAME",
	                                       "CUR_WINDOW",
	                                       "LINK_STAT",
	                                       "WINDOW_COUNT",
	                                       "VIRTUAL_DESKTOPS",
	                                       "CYCLIC_VIRTUAL_DESKTOPS",
	                                       "KEY1",
	                                       "KEY2",
	                                       "WIN_X1",
	                                       "WIN_Y1",
	                                       "WIN_X2",
	                                       "WIN_Y2",
	                                       "GLOBAL_STR",
	                                       "GLOBAL_INT",
	                                       "FIRST_GLOBAL",
	                                       "NEXT_GLOBAL",
	                                       "CREATE_GLOBAL_STR",
	                                       "SET_GLOBAL_STR",
	                                       "SET_GLOBAL_INT",
	                                       "INQ_MACRO",
	                                       "FIRST_MACRO",
	                                       "NEXT_MACRO",
	                                       "CREATE_WINDOW",
	                                       "DELETE_WINDOW",
	                                       "MODIFY_WINDOW",
	                                       "LINK_WINDOW",
	                                       "UNLINK_WINDOW",
	                                       "ZOOM",
	                                       "REDRAW",
	                                       "NEW_SCREEN",
	                                       "SWITCH_WINDOW",
	                                       "SIZE_WINDOW",
	                                       "MOVE_WIN_TO_NEXT_DESKTOP",
	                                       "MOVE_WIN_TO_PREV_DESKTOP",
	                                       "MOVE_VIEWPORT_RIGHT",
	                                       "MOVE_VIEWPORT_LEFT",
	                                       "SAVE_WORKSPACE",
	                                       "LOAD_WORKSPACE",
	                                       "SAVE_SETTINGS",
	                                       "FILE_CHANGED",
	                                       "FILE_NAME",
	                                       "IGNORE_CASE",
	                                       "TAB_EXPAND",
	                                       "DISPLAY_TABS",
	                                       "PUSH_LABELS",
	                                       "POP_LABELS",
	                                       "FLABEL",
	                                       "MARQUEE",
	                                       "MARQUEE_WARNING",
	                                       "MARQUEE_ERROR",
	                                       "WORKING",
	                                       "BRAIN",
	                                       "SCREEN_LENGTH",
	                                       "SCREEN_WIDTH",
	                                       "WHEREX",
	                                       "WHEREY",
	                                       "PUT_BOX",
	                                       "WRITE",
	                                       "CLR_LINE",
	                                       "GOTOXY",
	                                       "PUT_LINE_NUM",
	                                       "PUT_COL_NUM",
	                                       "SCROLL_BOX_UP",
	                                       "SCROLL_BOX_DN",
	                                       "CLEAR_SCREEN",
	                                       "KILL_BOX",
	                                       "MESSAGEBOX"};

	for (const char *symbol : kAllowed)
		if (value == symbol) return true;
	return false;
}

bool containsOnlySupportedStagedSymbols(const std::vector<std::string> &values) noexcept {
	for (const auto &value : values)
		if (!isSupportedStagedSymbol(value)) return false;
	return true;
}
} // namespace

MRMacroExecutionProfile mrvmAnalyzeBytecode(const unsigned char *bytecode, std::size_t length) {
	MRMacroExecutionProfile profile;
	std::size_t ip = 0;

	if (bytecode == nullptr || length == 0) return profile;

	while (ip < length) {
		unsigned char opcode = bytecode[ip++];
		++profile.opcodeCount;
		noteExecutionFlags(profile, classifyPureOpcode(opcode));

		switch (opcode) {
			case OP_PUSH_I:
				if (!skipBytecodeBytes(length, ip, sizeof(int))) return profile;
				break;
			case OP_PUSH_R:
				if (!skipBytecodeBytes(length, ip, sizeof(double))) return profile;
				break;
			case OP_PUSH_S:
			case OP_VAL:
			case OP_RVAL: {
				std::string ignored;
				if (!readBytecodeCString(bytecode, length, ip, ignored)) return profile;
				break;
			}
			case OP_DEF_VAR: {
				std::string ignored;
				if (!skipBytecodeBytes(length, ip, sizeof(unsigned char)) || !readBytecodeCString(bytecode, length, ip, ignored)) return profile;
				break;
			}
			case OP_LOAD_VAR: {
				std::string name;
				if (!readBytecodeCString(bytecode, length, ip, name)) return profile;
				name = upperProfileKey(name);
				noteExecutionFlags(profile, classifyLoadVarName(name), name);
				break;
			}
			case OP_HASH_LOAD:
			case OP_HASH_STORE: {
				std::string ignored;
				if (!readBytecodeCString(bytecode, length, ip, ignored)) return profile;
				break;
			}
			case OP_HASH_LOAD_VALUE:
			case OP_HASH_STORE_VALUE:
			case OP_ARRAY_LOAD_VALUE:
				break;
			case OP_ARRAY_LOAD:
			case OP_ARRAY_STORE: {
				std::string ignored;
				if (!readBytecodeCString(bytecode, length, ip, ignored)) return profile;
				break;
			}
			case OP_STORE_VAR: {
				std::string name;
				if (!skipBytecodeBytes(length, ip, sizeof(unsigned char)) || !readBytecodeCString(bytecode, length, ip, name)) return profile;
				name = upperProfileKey(name);
				noteExecutionFlags(profile, classifyStoreVarName(name), name);
				break;
			}
			case OP_GOTO:
			case OP_CALL:
			case OP_JZ:
				if (!skipBytecodeBytes(length, ip, sizeof(int))) return profile;
				break;
			case OP_FIRST_GLOBAL: {
				std::string ignored;
				if (!readBytecodeCString(bytecode, length, ip, ignored)) return profile;
				noteExecutionFlags(profile, mrefUiAffinity, "FIRST_GLOBAL");
				break;
			}
			case OP_NEXT_GLOBAL: {
				std::string ignored;
				if (!readBytecodeCString(bytecode, length, ip, ignored)) return profile;
				noteExecutionFlags(profile, mrefUiAffinity, "NEXT_GLOBAL");
				break;
			}
			case OP_INTRINSIC: {
				std::string name;
				if (!readBytecodeCString(bytecode, length, ip, name) || !skipBytecodeBytes(length, ip, sizeof(unsigned char))) return profile;
				++profile.intrinsicCount;
				name = upperProfileKey(name);
				noteExecutionFlags(profile, classifyIntrinsicName(name), name);
				break;
			}
			case OP_PROC_VAR: {
				std::string name;
				std::string variableName;
				unsigned char argc = 0;
				if (!readBytecodeCString(bytecode, length, ip, name) || !skipBytecodeBytes(length, ip, sizeof(unsigned char))) return profile;
				argc = bytecode[ip - 1];
				if (argc == 0 || argc > 2) return profile;
				if (!readBytecodeCString(bytecode, length, ip, variableName)) return profile;
				if (argc > 1) {
					std::string ignored;
					if (!readBytecodeCString(bytecode, length, ip, ignored)) return profile;
				}
				++profile.procVarCount;
				name = upperProfileKey(name);
				noteExecutionFlags(profile, classifyProcVarName(name), name);
				break;
			}
			case OP_PROC: {
				std::string name;
				if (!readBytecodeCString(bytecode, length, ip, name) || !skipBytecodeBytes(length, ip, sizeof(unsigned char))) return profile;
				++profile.procCount;
				name = upperProfileKey(name);
				noteExecutionFlags(profile, classifyProcName(name), name);
				break;
			}
			case OP_TVCALL: {
				std::string name;
				if (!readBytecodeCString(bytecode, length, ip, name) || !skipBytecodeBytes(length, ip, sizeof(unsigned char))) return profile;
				++profile.tvCallCount;
				name = upperProfileKey(name);
				noteExecutionFlags(profile, classifyTvCallName(name), name);
				break;
			}
			case OP_RET:
			case OP_HALT:
			case OP_ADD:
			case OP_SUB:
			case OP_MUL:
			case OP_DIV:
			case OP_MOD:
			case OP_NEG:
			case OP_CMP_EQ:
			case OP_CMP_NE:
			case OP_CMP_LT:
			case OP_CMP_GT:
			case OP_CMP_LE:
			case OP_CMP_GE:
			case OP_AND:
			case OP_OR:
			case OP_NOT:
			case OP_SHL:
			case OP_SHR:
			case OP_BIT_AND:
			case OP_BIT_OR:
			case OP_BIT_XOR:
				break;
			default: {
				char unknownOp[32];
				std::snprintf(unknownOp, sizeof(unknownOp), "UNKNOWN_OPCODE%02X", opcode);
				noteExecutionFlags(profile, mrefUiAffinity, unknownOp);
				return profile;
			}
		}
	}

	return profile;
}

std::string mrvmDescribeExecutionProfile(const MRMacroExecutionProfile &profile) {
	std::vector<std::string> parts;
	std::ostringstream out;

	if (profile.has(mrefBackgroundSafe)) parts.emplace_back("background-safe");
	if (profile.has(mrefStagedWrite)) parts.emplace_back("staged-write");
	if (profile.has(mrefUiAffinity)) parts.emplace_back("ui-affin");
	if (profile.has(mrefExternalIo)) parts.emplace_back("external-io");
	if (parts.empty()) parts.emplace_back("unclassified");

	for (std::size_t i = 0; i < parts.size(); ++i) {
		if (i != 0) out << ", ";
		out << parts[i];
	}

	out << " [ops=" << profile.opcodeCount << ", intr=" << profile.intrinsicCount << ", proc=" << profile.procCount << ", procvar=" << profile.procVarCount << ", tv=" << profile.tvCallCount << "]";
	return out.str();
}

bool mrvmCanRunInBackground(const MRMacroExecutionProfile &profile) noexcept {
	return profile.has(mrefBackgroundSafe) && !profile.has(mrefStagedWrite | mrefUiAffinity | mrefExternalIo);
}

bool mrvmCanRunStagedInBackground(const MRMacroExecutionProfile &profile) noexcept {
	if (profile.has(mrefExternalIo)) return false;
	if (!profile.has(mrefUiAffinity) && !profile.has(mrefStagedWrite)) return false;
	if (!containsOnlySupportedStagedSymbols(profile.stagedWriteSymbols)) return false;
	if (!containsOnlySupportedStagedSymbols(profile.uiAffinitySymbols)) return false;
	return true;
}

std::vector<std::string> mrvmUnsupportedStagedSymbols(const MRMacroExecutionProfile &profile) {
	std::vector<std::string> unsupported;

	for (const auto &stagedWriteSymbol : profile.stagedWriteSymbols)
		if (!isSupportedStagedSymbol(stagedWriteSymbol)) appendUniqueProfileString(unsupported, stagedWriteSymbol);
	for (const auto &uiAffinitySymbol : profile.uiAffinitySymbols)
		if (!isSupportedStagedSymbol(uiAffinitySymbol)) appendUniqueProfileString(unsupported, uiAffinitySymbol);
	return unsupported;
}
