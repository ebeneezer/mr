#include "MRVMDeferredUi.hpp"

#include "../mrmac.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Value = VirtualMachine::Value;
}

bool queueDeferredUiProcedure(const std::string &name, const std::vector<Value> &args, int &errorCode);
bool currentExecutingMacroSpec(std::string &macroSpec);

namespace {
constexpr const char *kDeferredWorkingMessageText = "working...";

bool isStringLike(const Value &value) {
	return value.type == TYPE_STR || value.type == TYPE_CHAR;
}

bool isNumeric(const Value &value) {
	return value.type == TYPE_INT || value.type == TYPE_REAL || value.type == TYPE_CHAR;
}

std::string valueAsString(const Value &value) {
	switch (value.type) {
		case TYPE_STR:
			return value.s;
		case TYPE_CHAR:
			return std::string(1, static_cast<char>(value.i));
		case TYPE_INT:
			return std::to_string(value.i);
		case TYPE_REAL: {
			char buffer[64];
			std::snprintf(buffer, sizeof(buffer), "%.15g", value.r);
			return std::string(buffer);
		}
		default:
			return std::string();
	}
}

int valueAsInt(const Value &value) {
	switch (value.type) {
		case TYPE_INT:
		case TYPE_CHAR:
			return value.i;
		case TYPE_REAL:
			return static_cast<int>(value.r);
		default:
			return 0;
	}
}

enum class DeferredVisualUiProc {
	Unknown,
	MakeMessage,
	MarqueeInfo,
	MarqueeWarning,
	MarqueeError,
	MessageBox,
	Working,
	Brain,
	PutBox,
	Write,
	ClrLine,
	Gotoxy,
	PutLineNum,
	PutColNum,
	ScrollBoxUp,
	ScrollBoxDn,
	ClearScreen,
	KillBox
};

DeferredVisualUiProc classifyDeferredVisualUiProc(const std::string &name) noexcept {
	if (name == "MAKE_MESSAGE") return DeferredVisualUiProc::MakeMessage;
	if (name == "MARQUEE") return DeferredVisualUiProc::MarqueeInfo;
	if (name == "MARQUEE_WARNING") return DeferredVisualUiProc::MarqueeWarning;
	if (name == "MARQUEE_ERROR") return DeferredVisualUiProc::MarqueeError;
	if (name == "UI_MESSAGEBOX") return DeferredVisualUiProc::MessageBox;
	if (name == "WORKING") return DeferredVisualUiProc::Working;
	if (name == "BRAIN") return DeferredVisualUiProc::Brain;
	if (name == "PUT_BOX") return DeferredVisualUiProc::PutBox;
	if (name == "WRITE") return DeferredVisualUiProc::Write;
	if (name == "CLR_LINE") return DeferredVisualUiProc::ClrLine;
	if (name == "GOTOXY") return DeferredVisualUiProc::Gotoxy;
	if (name == "PUT_LINE_NUM") return DeferredVisualUiProc::PutLineNum;
	if (name == "PUT_COL_NUM") return DeferredVisualUiProc::PutColNum;
	if (name == "SCROLL_BOX_UP") return DeferredVisualUiProc::ScrollBoxUp;
	if (name == "SCROLL_BOX_DN") return DeferredVisualUiProc::ScrollBoxDn;
	if (name == "CLEAR_SCREEN") return DeferredVisualUiProc::ClearScreen;
	if (name == "KILL_BOX") return DeferredVisualUiProc::KillBox;
	return DeferredVisualUiProc::Unknown;
}

bool buildDeferredVisualUiProcedureCommand(const std::string &name, const std::vector<Value> &args, MRMacroDeferredUiCommand &command) {
	switch (classifyDeferredVisualUiProc(name)) {
		case DeferredVisualUiProc::MakeMessage:
			if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("MAKE_MESSAGE expects one string argument.");
			command = MRMacroDeferredUiCommand(mrducMakeMessage, 0, 0, 0, 0, 0, 0, 0, 0, valueAsString(args[0]));
			return true;
		case DeferredVisualUiProc::MarqueeInfo:
		case DeferredVisualUiProc::MarqueeWarning:
		case DeferredVisualUiProc::MarqueeError:
			if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error(name + " expects one string argument.");
			command = MRMacroDeferredUiCommand(name == "MARQUEE" ? mrducMarqueeInfo : (name == "MARQUEE_WARNING" ? mrducMarqueeWarning : mrducMarqueeError), 0, 0, 0, 0, 0, 0, 0, 0, valueAsString(args[0]));
			return true;
		case DeferredVisualUiProc::MessageBox:
			if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("UI_MESSAGEBOX expects one string argument.");
			command = MRMacroDeferredUiCommand(mrducMessageBox, 0, 0, 0, 0, 0, 0, 0, 0, valueAsString(args[0]));
			return true;
		case DeferredVisualUiProc::Working:
			if (!args.empty()) throw std::runtime_error("WORKING expects no arguments.");
			command = MRMacroDeferredUiCommand(mrducMarqueeWarning, 0, 0, 0, 0, 0, 0, 0, 0, kDeferredWorkingMessageText);
			return true;
		case DeferredVisualUiProc::Brain:
			if (args.size() != 1 || !isNumeric(args[0])) throw std::runtime_error("BRAIN expects one integer argument.");
			command = MRMacroDeferredUiCommand(mrducBrain, valueAsInt(args[0]) != 0 ? 1 : 0);
			return true;
		case DeferredVisualUiProc::PutBox:
			if (args.size() != 8 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT || args[5].type != TYPE_INT || !isStringLike(args[6]) || args[7].type != TYPE_INT) throw std::runtime_error(name + " expects (int, int, int, int, int, int, string, int).");
			command = MRMacroDeferredUiCommand(mrducPutBox, valueAsInt(args[0]), valueAsInt(args[1]), valueAsInt(args[2]), valueAsInt(args[3]), valueAsInt(args[4]), valueAsInt(args[5]), valueAsInt(args[7]), 0, valueAsString(args[6]));
			return true;
		case DeferredVisualUiProc::Write:
			if (args.size() != 5 || !isStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT) throw std::runtime_error(name + " expects (string, int, int, int, int).");
			command = MRMacroDeferredUiCommand(mrducWrite, valueAsInt(args[1]), valueAsInt(args[2]), valueAsInt(args[3]), valueAsInt(args[4]), 0, 0, 0, 0, valueAsString(args[0]));
			return true;
		case DeferredVisualUiProc::ClrLine:
			if (!(args.empty() || (args.size() == 3 && args[0].type == TYPE_INT && args[1].type == TYPE_INT && args[2].type == TYPE_INT))) throw std::runtime_error(name + " expects no arguments or (int, int, int).");
			command = args.empty() ? MRMacroDeferredUiCommand(mrducClrLine) : MRMacroDeferredUiCommand(mrducClrLine, valueAsInt(args[0]), valueAsInt(args[1]), valueAsInt(args[2]));
			return true;
		case DeferredVisualUiProc::Gotoxy:
			if (args.size() != 2 || args[0].type != TYPE_INT || args[1].type != TYPE_INT) throw std::runtime_error(name + " expects (int, int).");
			command = MRMacroDeferredUiCommand(mrducGotoxy, valueAsInt(args[0]), valueAsInt(args[1]));
			return true;
		case DeferredVisualUiProc::PutLineNum:
		case DeferredVisualUiProc::PutColNum:
			if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error(name + " expects one integer argument.");
			command = MRMacroDeferredUiCommand(classifyDeferredVisualUiProc(name) == DeferredVisualUiProc::PutLineNum ? mrducPutLineNum : mrducPutColNum, valueAsInt(args[0]));
			return true;
		case DeferredVisualUiProc::ScrollBoxUp:
		case DeferredVisualUiProc::ScrollBoxDn:
			if (args.size() != 5 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT) throw std::runtime_error(name + " expects (int, int, int, int, int).");
			command = MRMacroDeferredUiCommand(classifyDeferredVisualUiProc(name) == DeferredVisualUiProc::ScrollBoxUp ? mrducScrollBoxUp : mrducScrollBoxDn, valueAsInt(args[0]), valueAsInt(args[1]), valueAsInt(args[2]), valueAsInt(args[3]), valueAsInt(args[4]), 0, 0, 0);
			return true;
		case DeferredVisualUiProc::ClearScreen:
			if (!(args.empty() || (args.size() == 1 && args[0].type == TYPE_INT))) throw std::runtime_error(name + " expects no arguments or one integer argument.");
			command = MRMacroDeferredUiCommand(mrducClearScreen, args.empty() ? 0x07 : valueAsInt(args[0]));
			return true;
		case DeferredVisualUiProc::KillBox:
			if (!args.empty()) throw std::runtime_error(name + " expects no arguments.");
			command = MRMacroDeferredUiCommand(mrducKillBox);
			return true;
		case DeferredVisualUiProc::Unknown:
			return false;
	}
	return false;
}

enum class DeferredMenuUiProc {
	Unknown,
	RegisterMenuItem,
	RemoveMenuItem
};

DeferredMenuUiProc classifyDeferredMenuUiProc(const std::string &name) noexcept {
	if (name == "REGISTER_MENU_ITEM") return DeferredMenuUiProc::RegisterMenuItem;
	if (name == "REMOVE_MENU_ITEM") return DeferredMenuUiProc::RemoveMenuItem;
	return DeferredMenuUiProc::Unknown;
}

bool buildDeferredMenuUiProcedureCommand(const std::string &name, const std::vector<Value> &args, MRMacroDeferredUiCommand &command) {
	std::string macroSpec;

	if (!currentExecutingMacroSpec(macroSpec)) throw std::runtime_error(name + " requires an active macro context.");

	switch (classifyDeferredMenuUiProc(name)) {
		case DeferredMenuUiProc::RegisterMenuItem:
			if ((args.size() != 2 && args.size() != 3) || !isStringLike(args[0]) || !isStringLike(args[1]) || (args.size() == 3 && !isStringLike(args[2]))) throw std::runtime_error("REGISTER_MENU_ITEM expects (string, string[, string]).");
			command.type = mrducRegisterMenuItem;
			command.text = valueAsString(args[0]);
			command.text2 = valueAsString(args[1]);
			command.text3 = args.size() == 3 ? valueAsString(args[2]) : macroSpec;
			command.text4 = macroSpec;
			return true;
		case DeferredMenuUiProc::RemoveMenuItem:
			if (args.size() != 2 || !isStringLike(args[0]) || !isStringLike(args[1])) throw std::runtime_error("REMOVE_MENU_ITEM expects (string, string).");
			command.type = mrducRemoveMenuItem;
			command.text = valueAsString(args[0]);
			command.text2 = valueAsString(args[1]);
			command.text3 = macroSpec;
			return true;
		case DeferredMenuUiProc::Unknown:
			return false;
	}
	return false;
}

bool applyDeferredMenuUiProcedureCommand(const MRMacroDeferredUiCommand &command) {
	std::string errorText;

	switch (command.type) {
		case mrducRegisterMenuItem:
			if (!mrvmUiRegisterMenuItem(command.text, command.text2, command.text3, command.text4, &errorText)) throw std::runtime_error("REGISTER_MENU_ITEM failed: " + (errorText.empty() ? std::string("unable to register menu item.") : errorText));
			return true;
		case mrducRemoveMenuItem:
			if (!mrvmUiRemoveMenuItem(command.text, command.text2, command.text3, &errorText)) throw std::runtime_error("REMOVE_MENU_ITEM failed: " + (errorText.empty() ? std::string("unable to remove menu item.") : errorText));
			return true;
		default:
			return false;
	}
}

} // namespace

bool dispatchDeferredVisualUiProcedure(const std::string &name, const std::vector<Value> &args, int &errorCode) {
	MRMacroDeferredUiCommand command;

	errorCode = 0;
	if (queueDeferredUiProcedure(name, args, errorCode)) return true;
	if (!buildDeferredVisualUiProcedureCommand(name, args, command)) return false;
	mrvmUiRenderFacadeRenderDeferredCommand(command);
	return true;
}

bool dispatchDeferredMenuUiProcedure(const std::string &name, const std::vector<Value> &args, int &errorCode) {
	MRMacroDeferredUiCommand command;

	errorCode = 0;
	if (queueDeferredUiProcedure(name, args, errorCode)) return true;
	if (!buildDeferredMenuUiProcedureCommand(name, args, command)) return false;
	return applyDeferredMenuUiProcedureCommand(command);
}
