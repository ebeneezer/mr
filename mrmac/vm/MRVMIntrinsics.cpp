#include "MRVMIntrinsics.hpp"

#include "MRVMHash.hpp"
#include "MRVMProcessRuntime.hpp"
#include "MRVMRuntimeInternal.hpp"
#include "MRVMValue.hpp"

#include "../ui/conventional/MRVMMacroDialogRuntime.hpp"
#include "../ui/modeless/MRVMMacroModelessProcedures.hpp"
#include "../ui/modeless/MRVMModelessUiRuntime.hpp"
#include "../../app/MRVersion.hpp"
#include "../../app/commands/MRWindowCommands.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/MRWindowSupport.hpp"

#define Uses_TProgram
#define Uses_TScreen
#include <tvision/tv.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace mrvm_runtime {

enum class MRVMIntrinsic {
	Unknown,
	Ascii,
	BarMenu,
	BlockText,
	Caps,
	Char,
	CheckKey,
	Copy,
	CopyFile,
	Exists,
	FileAttr,
	FileExists,
	FirstFile,
	GetEnvironment,
	GetExtension,
	GetPath,
	GetWord,
	GlobalHash,
	GlobalInt,
	GlobalStr,
	HasValue,
	InqMacro,
	IntR,
	Keys,
	Len,
	Length,
	NextFile,
	OsBack,
	OsColor,
	OsVersion,
	ParamStr,
	ParseInt,
	ParseStr,
	Pos,
	RealI,
	RemoveSpace,
	RenameFile,
	Rstr,
	ScreenLength,
	ScreenWidth,
	SearchBwd,
	SearchFwd,
	Str,
	StringIn,
	StrDel,
	StrIns,
	Subshell,
	SwitchFile,
	TruncateExtension,
	TruncatePath,
	UiExec,
	UiIndex,
	UiText,
	Utf8,
	Values,
	Version,
	VMenu,
	Wherex,
	Wherey,
	Xpos,
};

struct MRVMIntrinsicEntry {
	const char *name;
	MRVMIntrinsic intrinsic;
};

static MRVMIntrinsic classifyIntrinsic(const std::string &name) {
	static constexpr MRVMIntrinsicEntry entries[] = {
	    {"ASCII", MRVMIntrinsic::Ascii},    {"BAR_MENU", MRVMIntrinsic::BarMenu}, {"BLOCK_TEXT", MRVMIntrinsic::BlockText}, {"CAPS", MRVMIntrinsic::Caps}, {"CHAR", MRVMIntrinsic::Char},     {"CHECK_KEY", MRVMIntrinsic::CheckKey}, {"COPY", MRVMIntrinsic::Copy},    {"COPY_FILE", MRVMIntrinsic::CopyFile}, {"EXISTS", MRVMIntrinsic::Exists}, {"FILE_ATTR", MRVMIntrinsic::FileAttr}, {"FILE_EXISTS", MRVMIntrinsic::FileExists}, {"FIRST_FILE", MRVMIntrinsic::FirstFile}, {"GET_ENVIRONMENT", MRVMIntrinsic::GetEnvironment}, {"GET_EXTENSION", MRVMIntrinsic::GetExtension}, {"GET_PATH", MRVMIntrinsic::GetPath}, {"GET_WORD", MRVMIntrinsic::GetWord}, {"GLOBAL_HASH", MRVMIntrinsic::GlobalHash}, {"GLOBAL_INT", MRVMIntrinsic::GlobalInt}, {"GLOBAL_STR", MRVMIntrinsic::GlobalStr}, {"HAS_VALUE", MRVMIntrinsic::HasValue}, {"INQ_MACRO", MRVMIntrinsic::InqMacro}, {"INT_R", MRVMIntrinsic::IntR}, {"KEYS", MRVMIntrinsic::Keys}, {"LEN", MRVMIntrinsic::Len}, {"LENGTH", MRVMIntrinsic::Length}, {"NEXT_FILE", MRVMIntrinsic::NextFile}, {"OS_BACK", MRVMIntrinsic::OsBack}, {"OS_COLOR", MRVMIntrinsic::OsColor}, {"OS_VERSION", MRVMIntrinsic::OsVersion}, {"PARAM_STR", MRVMIntrinsic::ParamStr}, {"PARSE_INT", MRVMIntrinsic::ParseInt}, {"PARSE_STR", MRVMIntrinsic::ParseStr}, {"POS", MRVMIntrinsic::Pos}, {"REAL_I", MRVMIntrinsic::RealI}, {"REMOVE_SPACE", MRVMIntrinsic::RemoveSpace}, {"RENAME_FILE", MRVMIntrinsic::RenameFile}, {"RSTR", MRVMIntrinsic::Rstr}, {"SCREEN_LENGTH", MRVMIntrinsic::ScreenLength}, {"SCREEN_WIDTH", MRVMIntrinsic::ScreenWidth}, {"SEARCH_BWD", MRVMIntrinsic::SearchBwd}, {"SEARCH_FWD", MRVMIntrinsic::SearchFwd}, {"STR", MRVMIntrinsic::Str}, {"STRING_IN", MRVMIntrinsic::StringIn}, {"STR_DEL", MRVMIntrinsic::StrDel}, {"STR_INS", MRVMIntrinsic::StrIns}, {"SUBSHELL", MRVMIntrinsic::Subshell}, {"SWITCH_FILE", MRVMIntrinsic::SwitchFile}, {"TRUNCATE_EXTENSION", MRVMIntrinsic::TruncateExtension}, {"TRUNCATE_PATH", MRVMIntrinsic::TruncatePath},
	    {"UI_EXEC", MRVMIntrinsic::UiExec}, {"UI_INDEX", MRVMIntrinsic::UiIndex}, {"UI_TEXT", MRVMIntrinsic::UiText},       {"UTF8", MRVMIntrinsic::Utf8}, {"VALUES", MRVMIntrinsic::Values}, {"VERSION", MRVMIntrinsic::Version},    {"V_MENU", MRVMIntrinsic::VMenu}, {"WHEREX", MRVMIntrinsic::Wherex},      {"WHEREY", MRVMIntrinsic::Wherey}, {"XPOS", MRVMIntrinsic::Xpos},
	};
	const MRVMIntrinsicEntry *first = entries;
	const MRVMIntrinsicEntry *last = entries + sizeof(entries) / sizeof(entries[0]);
	const MRVMIntrinsicEntry *found = std::lower_bound(first, last, name, [](const MRVMIntrinsicEntry &entry, const std::string &value) { return std::strcmp(entry.name, value.c_str()) < 0; });

	if (found == last || name != found->name) return MRVMIntrinsic::Unknown;
	return found->intrinsic;
}

} // namespace mrvm_runtime

using namespace mrvm_runtime;

MRVMIntrinsics::MRVMIntrinsics(VirtualMachine &machine) noexcept : vm(machine) {
}

bool MRVMIntrinsics::applyHash(const std::string &name, const std::vector<Value> &args, Value &out) {
	const MRVMIntrinsic intrinsic = classifyIntrinsic(name);

	switch (intrinsic) {
		case MRVMIntrinsic::GlobalHash: {
			GlobalEntry entry;
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("GLOBAL_HASH expects one string argument.");
			if (!readGlobalValue(mrvmValueAsString(args[0]), entry) || entry.type != TYPE_HASH || entry.value.type != TYPE_HASH) {
				Value value = mrvmMakeHash(mrvmRuntimeKv().globalStore().createHash(), true);
				setGlobalValue(mrvmValueAsString(args[0]), TYPE_HASH, value);
				out = value;
				return true;
			}
			out = entry.value;
			return true;
		}
		case MRVMIntrinsic::Exists: {
			if (args.size() != 2 || args[0].type != TYPE_HASH || !mrvmIsStringLike(args[1])) throw std::runtime_error("EXISTS expects (hash, string).");
			out = mrvmMakeInt(mrvmHashContainsValue(vm.localHashStore(), mrvmRuntimeKv().globalStore(), args[0], mrvmValueAsString(args[1])) ? 1 : 0);
			return true;
		}
		case MRVMIntrinsic::HasValue: {
			if (args.size() != 2 || args[0].type != TYPE_HASH || !mrvmIsStringLike(args[1])) throw std::runtime_error("HAS_VALUE expects (hash, string).");
			const std::string key = mrvmValueAsString(args[1]);
			if (!mrvmHashContainsValue(vm.localHashStore(), mrvmRuntimeKv().globalStore(), args[0], key)) {
				out = mrvmMakeInt(0);
				return true;
			}
			out = mrvmMakeInt(mrvmValueHasContent(mrvmHashReadValue(vm.localHashStore(), mrvmRuntimeKv().globalStore(), args[0], key)) ? 1 : 0);
			return true;
		}
		case MRVMIntrinsic::Keys: {
			Value result = mrvmMakeArrayValue(TYPE_STR);
			if (args.size() != 1 || args[0].type != TYPE_HASH) throw std::runtime_error("KEYS expects one hash argument.");
			for (const std::string &key : mrvmHashRuntimeStoreForValue(vm.localHashStore(), mrvmRuntimeKv().globalStore(), args[0]).keys(args[0].hashHandle))
				result.arrayValues.push_back(mrvmMakeString(key));
			out = result;
			return true;
		}
		case MRVMIntrinsic::Values: {
			Value result = mrvmMakeArrayValue(TYPE_STR);
			if (args.size() != 1 || args[0].type != TYPE_HASH) throw std::runtime_error("VALUES expects one hash argument.");
			for (const Value &value : mrvmHashRuntimeStoreForValue(vm.localHashStore(), mrvmRuntimeKv().globalStore(), args[0]).values(args[0].hashHandle))
				result.arrayValues.push_back(mrvmMakeString(mrvmValueAsString(value)));
			out = result;
			return true;
		}
		case MRVMIntrinsic::Unknown:
		default:
			return false;
	}
}

Value MRVMIntrinsics::apply(const std::string &name, const std::vector<Value> &args) {
	const MRVMIntrinsic intrinsic = classifyIntrinsic(name);

	switch (intrinsic) {
		case MRVMIntrinsic::Str: {
			if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("STR expects one integer argument.");
			return mrvmMakeString(mrvmValueAsString(args[0]));
		}
		case MRVMIntrinsic::Char: {
			if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("CHAR expects one integer argument.");
			return mrvmMakeChar(static_cast<unsigned char>(args[0].i & 0xFF));
		}
		case MRVMIntrinsic::Utf8: {
			if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("UTF8 expects one integer argument.");
			return mrvmMakeString(mrvmUtf8FromCodepoint(static_cast<std::uint32_t>(args[0].i)));
		}
		case MRVMIntrinsic::Ascii: {
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("ASCII expects one string argument.");
			std::string s = mrvmValueAsString(args[0]);
			return mrvmMakeInt(s.empty() ? 0 : static_cast<unsigned char>(s[0]));
		}
		case MRVMIntrinsic::Caps: {
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("CAPS expects one string argument.");
			return mrvmMakeString(mrvmUpperKey(mrvmValueAsString(args[0])));
		}
		case MRVMIntrinsic::Copy: {
			std::string s;
			int pos;
			int count;
			std::size_t start;
			if (args.size() != 3 || !mrvmIsStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT) throw std::runtime_error("COPY expects (string, int, int).");
			s = mrvmValueAsString(args[0]);
			pos = mrvmCheckedStringIndex(args[1].i);
			count = args[2].i;
			if (count < 0) throw std::runtime_error("Invalid string index on string copy operation.");
			if (static_cast<std::size_t>(pos) > s.size()) return mrvmMakeString("");
			start = static_cast<std::size_t>(pos - 1);
			return mrvmMakeString(s.substr(start, static_cast<std::size_t>(count)));
		}
		case MRVMIntrinsic::Length: {
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("LENGTH expects one string argument.");
			return mrvmMakeInt(static_cast<int>(mrvmValueAsString(args[0]).size()));
		}
		case MRVMIntrinsic::Len: {
			if (args.size() != 1 || (!mrvmIsStringLike(args[0]) && !mrvmValueIsArrayType(args[0].type))) throw std::runtime_error("LEN expects one string or array argument.");
			if (mrvmValueIsArrayType(args[0].type)) return mrvmMakeInt(static_cast<int>(args[0].arrayValues.size()));
			return mrvmMakeInt(static_cast<int>(mrvmValueAsString(args[0]).size()));
		}
		case MRVMIntrinsic::Pos: {
			std::string needle;
			std::string haystack;
			std::size_t pos;
			if (args.size() != 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error("POS expects (substring, string).");
			needle = mrvmValueAsString(args[0]);
			haystack = mrvmValueAsString(args[1]);
			if (needle.empty()) return mrvmMakeInt(1);
			pos = haystack.find(needle);
			return mrvmMakeInt(pos == std::string::npos ? 0 : static_cast<int>(pos + 1));
		}
		case MRVMIntrinsic::Xpos: {
			std::string needle;
			std::string haystack;
			int startPos;
			std::size_t pos;
			if (args.size() != 3 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || args[2].type != TYPE_INT) throw std::runtime_error("XPOS expects (substring, string, int).");
			needle = mrvmValueAsString(args[0]);
			haystack = mrvmValueAsString(args[1]);
			startPos = mrvmCheckedStringIndex(args[2].i);
			if (needle.empty()) return mrvmMakeInt(startPos <= static_cast<int>(haystack.size()) + 1 ? startPos : 0);
			if (static_cast<std::size_t>(startPos) > haystack.size()) return mrvmMakeInt(0);
			pos = haystack.find(needle, static_cast<std::size_t>(startPos - 1));
			return mrvmMakeInt(pos == std::string::npos ? 0 : static_cast<int>(pos + 1));
		}
		case MRVMIntrinsic::StrDel: {
			std::string s;
			int pos;
			int count;
			std::size_t start;
			if (args.size() != 3 || !mrvmIsStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT) throw std::runtime_error("STR_DEL expects (string, int, int).");
			s = mrvmValueAsString(args[0]);
			pos = mrvmCheckedStringIndex(args[1].i);
			count = args[2].i;
			if (count < 0) throw std::runtime_error("Invalid string index on string copy operation.");
			if (static_cast<std::size_t>(pos) > s.size()) return mrvmMakeString(s);
			start = static_cast<std::size_t>(pos - 1);
			s.erase(start, static_cast<std::size_t>(count));
			return mrvmMakeString(s);
		}
		case MRVMIntrinsic::StrIns: {
			std::string target;
			std::string dest;
			int location;
			std::size_t insertPos;
			if (args.size() != 3 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || args[2].type != TYPE_INT) throw std::runtime_error("STR_INS expects (string, string, int).");
			target = mrvmValueAsString(args[0]);
			dest = mrvmValueAsString(args[1]);
			location = mrvmCheckedInsertIndex(args[2].i);
			insertPos = static_cast<std::size_t>(location);
			if (insertPos > dest.size()) insertPos = dest.size();
			dest.insert(insertPos, target);
			mrvmEnforceStringLength(dest);
			return mrvmMakeString(dest);
		}
		case MRVMIntrinsic::RealI: {
			if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("REAL_I expects one integer argument.");
			return mrvmMakeReal(static_cast<double>(args[0].i));
		}
		case MRVMIntrinsic::IntR: {
			if (args.size() != 1 || args[0].type != TYPE_REAL) throw std::runtime_error("INT_R expects one real argument.");
			if (args[0].r < static_cast<double>(std::numeric_limits<int>::min()) || args[0].r > static_cast<double>(std::numeric_limits<int>::max())) throw std::runtime_error("Real to Integer conversion out of range.");
			return mrvmMakeInt(static_cast<int>(args[0].r));
		}
		case MRVMIntrinsic::Rstr: {
			char fmt[32];
			char buf[256];
			int width;
			int precision;

			if (args.size() != 3 || args[0].type != TYPE_REAL || args[1].type != TYPE_INT || args[2].type != TYPE_INT) throw std::runtime_error("RSTR expects (real, int, int).");

			width = args[1].i;
			precision = args[2].i;
			if (width < 0) width = 0;
			if (precision < 0) precision = 0;
			if (precision > 20) precision = 20;

			std::snprintf(fmt, sizeof(fmt), "%%%d.%df", width, precision);
			std::snprintf(buf, sizeof(buf), fmt, args[0].r);
			mrvmEnforceStringLength(buf);
			return mrvmMakeString(buf);
		}
		case MRVMIntrinsic::RemoveSpace: {
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("REMOVE_SPACE expects one string argument.");
			return mrvmMakeString(mrvmRemoveSpaceAscii(mrvmValueAsString(args[0])));
		}
		case MRVMIntrinsic::GetExtension: {
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("GET_EXTENSION expects one string argument.");
			return mrvmMakeString(mrvmGetExtensionPart(mrvmValueAsString(args[0])));
		}
		case MRVMIntrinsic::GetPath: {
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("GET_PATH expects one string argument.");
			return mrvmMakeString(mrvmGetPathPart(mrvmValueAsString(args[0])));
		}
		case MRVMIntrinsic::TruncateExtension: {
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("TRUNCATE_EXTENSION expects one string argument.");
			return mrvmMakeString(mrvmTruncateExtensionPart(mrvmValueAsString(args[0])));
		}
		case MRVMIntrinsic::TruncatePath: {
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("TRUNCATE_PATH expects one string argument.");
			return mrvmMakeString(mrvmTruncatePathPart(mrvmValueAsString(args[0])));
		}
		case MRVMIntrinsic::FileExists: {
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("FILE_EXISTS expects one string argument.");
			return mrvmMakeInt(mrvmFileExistsPath(mrvmValueAsString(args[0])) ? 1 : 0);
		}
		case MRVMIntrinsic::FileAttr: {
			int attr = 0;
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("FILE_ATTR expects one string argument.");
			if (!mrvmReadFileMetadata(mrvmValueAsString(args[0]), &attr, nullptr, nullptr)) {
				setRuntimeErrorLevel(errno != 0 ? errno : 1);
				return mrvmMakeInt(0);
			}
			setRuntimeErrorLevel(0);
			return mrvmMakeInt(attr);
		}
		case MRVMIntrinsic::FirstFile: {
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("FIRST_FILE expects one string argument.");
			return mrvmMakeInt(findFirstFileMatch(mrvmValueAsString(args[0])));
		}
		case MRVMIntrinsic::NextFile: {
			if (!args.empty()) throw std::runtime_error("NEXT_FILE expects no arguments.");
			return mrvmMakeInt(findNextFileMatch());
		}
		case MRVMIntrinsic::Subshell: {
			MRVMSubshellResult subshell;
			if (args.size() != 2 || !mrvmIsStringLike(args[0]) || args[1].type != TYPE_INT) throw std::runtime_error("SUBSHELL expects (string, int).");
			subshell = mrvmRunSubshellCapture(mrvmValueAsString(args[0]), mrvmValueAsInt(args[1]), configuredShellExecutablePath());
			setRuntimeErrorLevel(subshell.errorLevel);
			return mrvmMakeString(subshell.output);
		}
		case MRVMIntrinsic::SearchFwd: {
			MRFileEditor *editor;
			std::size_t matchStart = 0;
			std::size_t matchEnd = 0;
			MREditWindow *win;
			BackgroundEditSession *session;
			if (args.size() != 2 || !mrvmIsStringLike(args[0]) || args[1].type != TYPE_INT) throw std::runtime_error("SEARCH_FWD expects (string, int).");
			if (mrvmValueAsString(args[0]).empty()) {
				setRuntimeErrorLevel(1010);
				return mrvmMakeInt(0);
			}
			editor = currentEditor();
			session = currentBackgroundEditSession();
			win = currentEditorCommandWindow();
			if (editor == nullptr && session == nullptr) return mrvmMakeInt(0);
			if (!searchEditorForward(editor, mrvmValueAsString(args[0]), mrvmValueAsInt(args[1]), currentRuntimeIgnoreCase(), matchStart, matchEnd)) {
				if (session != nullptr) session->clearLastSearch();
				else
					mrvmUiReplaceWindowLastSearch(win, win != nullptr ? std::string(win->currentFileName()) : std::string(), false, 0, 0, 0);
				setRuntimeErrorLevel(0);
				return mrvmMakeInt(0);
			}
			if (editor != nullptr) {
				editor->setCursorOffset(static_cast<uint>(matchStart), 0);
				editor->setSelectionOffsets(static_cast<uint>(matchStart), static_cast<uint>(matchEnd), False);
				editor->revealCursor(True);
			} else {
				session->cursorOffset = matchStart;
				session->selectionStart = matchStart;
				session->selectionEnd = matchEnd;
			}
			win = currentEditorCommandWindow();
			if (session != nullptr) {
				session->lastSearchValid = true;
				session->lastSearchStart = matchStart;
				session->lastSearchEnd = matchEnd;
				session->lastSearchCursor = matchStart;
			} else {
				mrvmUiReplaceWindowLastSearch(win, win != nullptr ? std::string(win->currentFileName()) : std::string(), true, matchStart, matchEnd, matchStart);
			}
			setRuntimeErrorLevel(0);
			return mrvmMakeInt(1);
		}
		case MRVMIntrinsic::SearchBwd: {
			MRFileEditor *editor;
			std::size_t matchStart = 0;
			std::size_t matchEnd = 0;
			MREditWindow *win;
			BackgroundEditSession *session;
			if (args.size() != 2 || !mrvmIsStringLike(args[0]) || args[1].type != TYPE_INT) throw std::runtime_error("SEARCH_BWD expects (string, int).");
			if (mrvmValueAsString(args[0]).empty()) {
				setRuntimeErrorLevel(1010);
				return mrvmMakeInt(0);
			}
			editor = currentEditor();
			session = currentBackgroundEditSession();
			win = currentEditorCommandWindow();
			if (editor == nullptr && session == nullptr) return mrvmMakeInt(0);
			if (!searchEditorBackward(editor, mrvmValueAsString(args[0]), mrvmValueAsInt(args[1]), currentRuntimeIgnoreCase(), matchStart, matchEnd)) {
				if (session != nullptr) session->clearLastSearch();
				else
					mrvmUiReplaceWindowLastSearch(win, win != nullptr ? std::string(win->currentFileName()) : std::string(), false, 0, 0, 0);
				setRuntimeErrorLevel(0);
				return mrvmMakeInt(0);
			}
			if (editor != nullptr) {
				editor->setCursorOffset(static_cast<uint>(matchStart), 0);
				editor->setSelectionOffsets(static_cast<uint>(matchStart), static_cast<uint>(matchEnd), False);
				editor->revealCursor(True);
			} else {
				session->cursorOffset = matchStart;
				session->selectionStart = matchStart;
				session->selectionEnd = matchEnd;
			}
			win = currentEditorCommandWindow();
			if (session != nullptr) {
				session->lastSearchValid = true;
				session->lastSearchStart = matchStart;
				session->lastSearchEnd = matchEnd;
				session->lastSearchCursor = matchStart;
			} else {
				mrvmUiReplaceWindowLastSearch(win, win != nullptr ? std::string(win->currentFileName()) : std::string(), true, matchStart, matchEnd, matchStart);
			}
			setRuntimeErrorLevel(0);
			return mrvmMakeInt(1);
		}
		case MRVMIntrinsic::GetEnvironment: {
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("GET_ENVIRONMENT expects one string argument.");
			return mrvmMakeString(getEnvironmentValue(mrvmValueAsString(args[0])));
		}
		case MRVMIntrinsic::GetWord: {
			MRFileEditor *editor;
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("GET_WORD expects one string argument.");
			editor = currentEditor();
			if (editor == nullptr && currentBackgroundEditSession() == nullptr) return mrvmMakeString("");
			return mrvmMakeString(currentEditorWord(editor, mrvmValueAsString(args[0])));
		}
		case MRVMIntrinsic::ParamStr: {
			int index;
			const std::vector<std::string> processArgs = mrvmRuntimeStateStringList("process", "arguments");
			if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error("PARAM_STR expects one integer argument.");
			index = mrvmValueAsInt(args[0]);
			if (index == 0) return mrvmMakeString(mrvmRuntimeStateString("process", "startupCommand"));
			if (index < 0 || static_cast<std::size_t>(index) > processArgs.size()) return mrvmMakeString("");
			return mrvmMakeString(processArgs[static_cast<std::size_t>(index - 1)]);
		}
		case MRVMIntrinsic::GlobalStr: {
			GlobalEntry entry;
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("GLOBAL_STR expects one string argument.");
			if (!readGlobalValue(mrvmValueAsString(args[0]), entry) || entry.type != TYPE_STR) return mrvmMakeString("");
			return entry.value;
		}
		case MRVMIntrinsic::GlobalInt: {
			GlobalEntry entry;
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("GLOBAL_INT expects one string argument.");
			if (!readGlobalValue(mrvmValueAsString(args[0]), entry) || entry.type != TYPE_INT) return mrvmMakeInt(0);
			return entry.value;
		}
		case MRVMIntrinsic::GlobalHash:
		case MRVMIntrinsic::Exists:
		case MRVMIntrinsic::HasValue:
		case MRVMIntrinsic::Keys:
		case MRVMIntrinsic::Values: {
			Value result;
			if (applyHash(name, args, result)) return result;
			break;
		}
		case MRVMIntrinsic::CheckKey: {
			int key1 = 0;
			int key2 = 0;
			if (!args.empty()) throw std::runtime_error("CHECK_KEY expects no arguments.");
			if (readMacroKeyPair(false, key1, key2)) return mrvmMakeInt(1);
			return mrvmMakeInt(0);
		}
		case MRVMIntrinsic::Version: {
			if (!args.empty()) throw std::runtime_error("VERSION expects no arguments.");
			return mrvmMakeString(mrDisplayVersion());
		}
		case MRVMIntrinsic::OsBack: {
			if (!args.empty()) throw std::runtime_error("OS_BACK expects no arguments.");
			return mrvmMakeInt(0);
		}
		case MRVMIntrinsic::OsColor: {
			if (!args.empty()) throw std::runtime_error("OS_COLOR expects no arguments.");
			return mrvmMakeInt(7);
		}
		case MRVMIntrinsic::OsVersion: {
			if (!args.empty()) throw std::runtime_error("OS_VERSION expects no arguments.");
			return mrvmMakeString(mrvmDetectShellVersion(configuredShellExecutablePath()));
		}
		case MRVMIntrinsic::ParseStr: {
			if (args.size() != 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error("PARSE_STR expects (string, string).");
			return mrvmMakeString(parseNamedValue(mrvmValueAsString(args[0]), mrvmValueAsString(args[1])));
		}
		case MRVMIntrinsic::ParseInt: {
			std::string parsed;
			int errorPos;
			if (args.size() != 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error("PARSE_INT expects (string, string).");
			parsed = parseNamedValue(mrvmValueAsString(args[0]), mrvmValueAsString(args[1]));
			if (parsed.empty()) return mrvmMakeInt(0);
			errorPos = mrvmFindValErrorPosition(parsed);
			if (errorPos != 0) return mrvmMakeInt(0);
			return mrvmMakeInt(static_cast<int>(std::strtol(parsed.c_str(), nullptr, 10)));
		}
		case MRVMIntrinsic::InqMacro: {
			BackgroundEditSession *session;
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("INQ_MACRO expects one string argument.");
			session = currentBackgroundEditSession();
			if (session != nullptr) return mrvmMakeInt(session->loadedMacroDisplayNames.find(mrvmUpperKey(mrvmValueAsString(args[0]))) != session->loadedMacroDisplayNames.end() ? 1 : 0);
			return mrvmMakeInt(loadedMacroExists(mrvmUpperKey(mrvmValueAsString(args[0]))) ? 1 : 0);
		}
		case MRVMIntrinsic::CopyFile: {
			std::string source;
			std::string target;
			const bool append = args.size() == 3 && mrvmValueAsInt(args[2]) != 0;
			std::ifstream in;
			std::ofstream out;

			if ((args.size() != 2 && args.size() != 3) || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1]) || (args.size() == 3 && args[2].type != TYPE_INT)) throw std::runtime_error("COPY_FILE expects (string, string[, int]).");
			source = mrvmProcessExpandUserPath(mrvmValueAsString(args[0]));
			target = mrvmProcessExpandUserPath(mrvmValueAsString(args[1]));
			in.open(source.c_str(), std::ios::in | std::ios::binary);
			out.open(target.c_str(), (append ? (std::ios::out | std::ios::binary | std::ios::app) : (std::ios::out | std::ios::binary | std::ios::trunc)));
			if (!in || !out) {
				setRuntimeErrorLevel(errno != 0 ? errno : 1);
				return mrvmMakeInt(runtimeErrorLevel());
			}
			out << in.rdbuf();
			setRuntimeErrorLevel((in.good() || in.eof()) && out.good() ? 0 : (errno != 0 ? errno : 1));
			return mrvmMakeInt(runtimeErrorLevel());
		}
		case MRVMIntrinsic::RenameFile: {
			std::string source;
			std::string target;
			if (args.size() != 2 || !mrvmIsStringLike(args[0]) || !mrvmIsStringLike(args[1])) throw std::runtime_error("RENAME_FILE expects (string, string).");
			source = mrvmProcessExpandUserPath(mrvmValueAsString(args[0]));
			target = mrvmProcessExpandUserPath(mrvmValueAsString(args[1]));
			setRuntimeErrorLevel(::rename(source.c_str(), target.c_str()) == 0 ? 0 : (errno != 0 ? errno : 1));
			return mrvmMakeInt(runtimeErrorLevel());
		}
		case MRVMIntrinsic::SwitchFile: {
			const std::string target = mrvmProcessExpandUserPath(mrvmValueAsString(args[0]));
			if (args.size() != 1 || !mrvmIsStringLike(args[0])) throw std::runtime_error("SWITCH_FILE expects one string argument.");
			if (currentBackgroundEditSession() != nullptr) return mrvmMakeInt(0);
			for (MREditWindow *window : allEditWindowsInZOrder()) {
				if (window == nullptr) continue;
				if (target != mrvmProcessExpandUserPath(window->currentFileName())) continue;
				setRuntimeErrorLevel(mrActivateEditWindow(window) ? 0 : 1001);
				return mrvmMakeInt(runtimeErrorLevel() == 0 ? 1 : 0);
			}
			setRuntimeErrorLevel(0);
			return mrvmMakeInt(0);
		}
		case MRVMIntrinsic::ScreenLength:
			return mrvmMakeInt(currentBackgroundEditSession() != nullptr ? currentBackgroundEditSession()->screenHeight : static_cast<int>(TDisplay::getRows()));
		case MRVMIntrinsic::ScreenWidth:
			return mrvmMakeInt(currentBackgroundEditSession() != nullptr ? currentBackgroundEditSession()->screenWidth : static_cast<int>(TDisplay::getCols()));
		case MRVMIntrinsic::Wherex: {
			BackgroundEditSession *session = currentBackgroundEditSession();
			int x = 0;
			int y = 0;
			MRFileEditor *editor = currentEditor();
			if (editor != nullptr && currentUiCursorPosition(x, y)) return mrvmMakeInt(x);
			if (session != nullptr) return mrvmMakeInt(session->screenCursorX);
			return mrvmMakeInt(currentUiCursorPosition(x, y) ? x : 0);
		}
		case MRVMIntrinsic::Wherey: {
			BackgroundEditSession *session = currentBackgroundEditSession();
			int x = 0;
			int y = 0;
			if (currentEditor() != nullptr && currentUiCursorPosition(x, y)) return mrvmMakeInt(y);
			if (session != nullptr) return mrvmMakeInt(session->screenCursorY);
			return mrvmMakeInt(currentUiCursorPosition(x, y) ? y : 0);
		}
		case MRVMIntrinsic::BlockText: {
			return mrvmMakeString(std::string());
		}
		case MRVMIntrinsic::BarMenu:
		case MRVMIntrinsic::VMenu: {
			if (currentBackgroundEditSession() != nullptr) throw std::runtime_error(name + " is not available in background mode.");
			return mrvmMakeInt(mrvmRunMacroMenuIntrinsic(name, args));
		}
		case MRVMIntrinsic::UiExec:
			return mrvmMakeInt(mrvmRunMacroUiDialogDefinition(mrvmRuntimeKv()));
		case MRVMIntrinsic::UiText:
			return mrvmMakeString(mrvmModelessUiReadTextValue(mrvmRuntimeKv(), mrvmValueAsInt(args[0])));
		case MRVMIntrinsic::UiIndex:
			return mrvmMakeInt(mrvmModelessUiReadIndexValue(mrvmRuntimeKv(), mrvmValueAsInt(args[0])));
		case MRVMIntrinsic::StringIn:
		case MRVMIntrinsic::Unknown:
		default:
			break;
	}

	{
		Value modelessResult;
		if (mrvmDispatchMacroModelessIntrinsic(mrvmRuntimeKv(), name, args, modelessResult)) return modelessResult;
	}
	if (intrinsic == MRVMIntrinsic::StringIn) {
		if (currentBackgroundEditSession() != nullptr) throw std::runtime_error("STRING_IN is not available in background mode.");
		return mrvmMakeString(mrvmRunMacroStringInputIntrinsic(args));
	}

	throw std::runtime_error("Unknown intrinsic: " + name);
}
