#include "MRVMScreen.hpp"
#include "MRVMScreenState.hpp"
#include "../../vm/MRVMRuntimeState.hpp"

#define Uses_MsgBox
#define Uses_TProgram
#define Uses_TApplication
#define Uses_TBackground
#define Uses_TDeskTop
#define Uses_TScreen
#define Uses_TDisplay
#define Uses_TDrawBuffer
#define Uses_TView
#include <tvision/tv.h>

#include "../../mrmac.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../../../app/commands/MRWindowCommands.hpp"
#include "../../../config/settings/MRSettingsRuntime.hpp"
#include "../../../ui/MREditWindow.hpp"
#include "../../../ui/MRMenuBar.hpp"
#include "../../../ui/MRMessageLineController.hpp"
#include "../../../ui/MRStatusLine.hpp"
#include "../../../ui/MRWindowSupport.hpp"

using mrvm_screen::MacroCellGrid;
using mrvm_screen::MacroDesktopCanvas;
using mrvm_screen::UiScreenStateFacade;
using mrvm_screen::noteMacroScreenFlush;

namespace {
using Value = VirtualMachine::Value;


static bool isStringLike(const Value &value) noexcept {
	return value.type == TYPE_STR || value.type == TYPE_CHAR;
}

static bool isNumeric(const Value &value) noexcept {
	return value.type == TYPE_INT || value.type == TYPE_REAL;
}

static int valueAsInt(const Value &value) {
	if (value.type == TYPE_INT) return value.i;
	throw std::runtime_error("integer value expected");
}

static std::string charToString(unsigned char c) {
	if (c == 0) return std::string();
	return std::string(1, static_cast<char>(c));
}

static std::string valueAsString(const Value &value) {
	if (value.type == TYPE_STR) return value.s;
	if (value.type == TYPE_CHAR) return charToString(value.c);
	throw std::runtime_error("string value expected");
}

static Value makeIntValue(int value) {
	Value v;
	v.type = TYPE_INT;
	v.i = value;
	return v;
}

static Value makeStringValue(const std::string &value) {
	Value v;
	v.type = TYPE_STR;
	v.s = value;
	return v;
}

// Render sink classification for the Strangler foundation:
// ordinary-view-draw: MacroCellView::draw() projects buffered cells only.
// base-redraw-trigger: forceMacroUiMessageRefresh(), projectAll() and redrawBaseAndOverlay().
// overlay-render: UiScreenStateFacade plus line/column overlay replay.
// unsafe-physical-write: the physical flush sink remains confined to facade-approved sinks.
static void forceMacroUiMessageRefresh(TApplication *app) {
	if (app == nullptr) return;
	if (app->menuBar != nullptr) app->menuBar->drawView();
	if (app->statusLine != nullptr) app->statusLine->drawView();
	noteMacroScreenFlush();
	TScreen::flushScreen();
}

static bool applyMakeMessageText(const std::string &text) {
	TApplication *app = dynamic_cast<TApplication *>(TProgram::application);
	mr::messageline::VisibleMessage existingMessage;

	if (app == nullptr || dynamic_cast<MRMenuBar *>(app->menuBar) == nullptr) throw std::runtime_error("MAKE_MESSAGE requires an active menu bar.");
	if (text.empty()) {
		if (!mr::messageline::currentOwnerMessage(mr::messageline::Owner::MacroMessage, existingMessage)) return true;
		mr::messageline::clearOwner(mr::messageline::Owner::MacroMessage);
	} else {
		if (mr::messageline::currentOwnerMessage(mr::messageline::Owner::MacroMessage, existingMessage) && existingMessage.kind == mr::messageline::Kind::Info && existingMessage.text == text) return true;
		mr::messageline::postAutoTimed(mr::messageline::Owner::MacroMessage, text, mr::messageline::Kind::Info, mr::messageline::kPriorityMedium);
	}
	forceMacroUiMessageRefresh(app);
	return returnWithDirectScreenMutation(true);
}

static bool renderMacroLineColOverlay() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MacroCellGrid grid;
	const bool result = grid.putLineColOverlay(
	    mrvmRuntimeStateInt("macroScreen", "line"),
	    mrvmRuntimeStateInt("macroScreen", "col"),
	    mrvmRuntimeStateInt("macroScreen", "haveLine") != 0,
	    mrvmRuntimeStateInt("macroScreen", "haveCol") != 0);
	grid.storeState();
	return result;
}

static void projectMacroDesktopBackground() {
	if (TProgram::deskTop == nullptr || TProgram::deskTop->background == nullptr) return;
	TProgram::deskTop->background->drawView();
	if (mrvmRuntimeStateInt("macroScreen", "projectionBatchDepth") > 0) {
		mrvmStoreRuntimeStateInt("macroScreen", "flushPending", 1);
		return;
	}
	noteMacroScreenFlush();
	TScreen::flushScreen();
}

} // namespace

bool returnWithMacroScreenMutation(bool ok) noexcept {
	if (ok) UiScreenStateFacade::noteMacroOverlayMutation();
	return ok;
}

bool returnWithDirectScreenMutation(bool ok) noexcept {
	if (ok) UiScreenStateFacade::noteBaseMutation();
	return ok;
}

std::uint64_t mrvmUiScreenMutationEpoch() noexcept {
	return mrvmRuntimeStateSize("macroScreen", "mutationEpoch", 1);
}

void mrvmUiInvalidateScreenBase() noexcept {
	UiScreenStateFacade::noteBaseMutation();
}

void mrvmUiTouchScreenMutationEpoch() noexcept {
	UiScreenStateFacade::noteBaseMutation();
}

void mrvmUiBeginMacroScreenBatch() noexcept {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MacroCellGrid grid;
	grid.beginProjectionBatch();
	grid.storeState();
}

void mrvmUiEndMacroScreenBatch() noexcept {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MacroCellGrid grid;
	grid.endProjectionBatch();
	grid.storeState();
}

std::uint64_t mrvmUiMacroScreenFlushCount() noexcept {
	return mrvmRuntimeStateSize("macroScreen", "flushCount");
}

void mrvmUiResetMacroScreenFlushCount() noexcept {
	mrvmStoreRuntimeStateSize("macroScreen", "flushCount", 0);
}

bool applyMarqueeProc(const std::string &name, const std::vector<Value> &args) {
	TApplication *app = dynamic_cast<TApplication *>(TProgram::application);
	mr::messageline::Kind kind = mr::messageline::Kind::Info;
	mr::messageline::VisibleMessage existingMessage;
	std::string text;

	if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error(name + " expects one string argument.");
	if (app == nullptr || dynamic_cast<MRMenuBar *>(app->menuBar) == nullptr) throw std::runtime_error(name + " requires an active menu bar.");
	if (name == "MARQUEE_WARNING") kind = mr::messageline::Kind::Warning;
	else if (name == "MARQUEE_ERROR")
		kind = mr::messageline::Kind::Error;

	text = valueAsString(args[0]);
	if (text.empty()) {
		if (!mr::messageline::currentOwnerMessage(mr::messageline::Owner::MacroMarquee, existingMessage)) return true;
		mr::messageline::clearOwner(mr::messageline::Owner::MacroMarquee);
	} else {
		if (mr::messageline::currentOwnerMessage(mr::messageline::Owner::MacroMarquee, existingMessage) && existingMessage.kind == kind && existingMessage.text == text) return true;
		mr::messageline::postAutoTimed(mr::messageline::Owner::MacroMarquee, text, kind, mr::messageline::kPriorityMedium);
	}
	forceMacroUiMessageRefresh(app);
	return returnWithDirectScreenMutation(true);
}

bool applyMakeMessageProc(const std::vector<Value> &args) {
	if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("MAKE_MESSAGE expects one string argument.");
	return applyMakeMessageText(valueAsString(args[0]));
}

bool applyBrainProc(const std::string &name, const std::vector<Value> &args) {
	bool enabled = false;
	bool activeChanged = false;
	bool visibleChanged = false;

	if (args.size() != 1 || !isNumeric(args[0])) throw std::runtime_error(name + " expects one integer argument.");

	enabled = valueAsInt(args[0]) != 0;
	activeChanged = mrIsMacroBrainMarkerActive() != enabled;
	visibleChanged = mrIsMacroBrainMarkerVisible() != enabled;
	if (!activeChanged && !visibleChanged) return true;
	mrSetMacroBrainMarkerActive(enabled);
	mrSetMacroBrainMarkerVisible(enabled);
	(void)mrvmUiRedrawCurrentWindow();
	return returnWithDirectScreenMutation(true);
}

bool applyDesktopCanvasCommand(const MRMacroDeferredUiCommand &command) {
	bool changed = false;
	bool projectionRequired = false;

	{
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		MacroDesktopCanvas canvas;
		switch (command.type) {
			case mrducDesktopClear:
				changed = canvas.clear();
				projectionRequired = changed;
				break;
			case mrducDesktopSetColor:
				changed = canvas.setAttribute(command.a1);
				break;
			case mrducDesktopPutChar:
				changed = canvas.putCharacter(command.text, command.a1, command.a2);
				projectionRequired = changed;
				break;
			case mrducDesktopPutString:
				changed = canvas.putString(command.text, command.a1, command.a2);
				projectionRequired = changed;
				break;
			case mrducDesktopBlit:
				changed = canvas.blit(command.a1, command.a2, command.a3, command.a4, command.text, command.text2);
				projectionRequired = changed;
				break;
			default:
				return false;
		}
		if (changed) canvas.storeState();
	}
	if (projectionRequired) {
		UiScreenStateFacade::noteBaseMutation();
		projectMacroDesktopBackground();
	}
	return true;
}

bool applyPutBoxProc(const std::string &name, const std::vector<Value> &args) {
	int x1 = 0;
	int y1 = 0;
	int x2 = 0;
	int y2 = 0;
	int bgColor = 0;
	int fgColor = 0;
	std::string title;
	bool shadow = false;

	if (args.size() != 8 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT || args[5].type != TYPE_INT || !isStringLike(args[6]) || args[7].type != TYPE_INT) throw std::runtime_error(name + " expects (int, int, int, int, int, int, string, int).");

	x1 = valueAsInt(args[0]);
	y1 = valueAsInt(args[1]);
	x2 = valueAsInt(args[2]);
	y2 = valueAsInt(args[3]);
	bgColor = valueAsInt(args[4]);
	fgColor = valueAsInt(args[5]);
	title = valueAsString(args[6]);
	shadow = valueAsInt(args[7]) != 0;

	{
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		MacroCellGrid grid;
		grid.putBox(x1, y1, x2, y2, bgColor, fgColor, title, shadow);
		grid.storeState();
	}
	return returnWithMacroScreenMutation(true);
}

bool applyWriteProc(const std::string &name, const std::vector<Value> &args) {
	std::string text;
	int x = 0;
	int y = 0;
	int bgColor = 0;
	int fgColor = 0;

	if (args.size() != 5 || !isStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT) throw std::runtime_error(name + " expects (string, int, int, int, int).");

	text = valueAsString(args[0]);
	x = valueAsInt(args[1]);
	y = valueAsInt(args[2]);
	bgColor = valueAsInt(args[3]);
	fgColor = valueAsInt(args[4]);

	{
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		MacroCellGrid grid;
		grid.writeText(text, x, y, bgColor, fgColor);
		grid.storeState();
	}
	return returnWithMacroScreenMutation(true);
}

bool applyClrLineProc(const std::string &name, const std::vector<Value> &args) {
	int col = 0;
	int row = 0;
	int count = 0;

	if (!(args.empty() || (args.size() == 3 && args[0].type == TYPE_INT && args[1].type == TYPE_INT && args[2].type == TYPE_INT))) throw std::runtime_error(name + " expects no arguments or (int, int, int).");

	if (!args.empty()) {
		col = valueAsInt(args[0]);
		row = valueAsInt(args[1]);
		count = valueAsInt(args[2]);
	}
	{
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		MacroCellGrid grid;
		grid.clearLine(col, row, count);
		grid.storeState();
	}
	return returnWithMacroScreenMutation(true);
}

bool applyGotoxyProc(const std::string &name, const std::vector<Value> &args) {
	TApplication *app = dynamic_cast<TApplication *>(TProgram::application);
	int width = static_cast<int>(TDisplay::getCols());
	int height = static_cast<int>(TDisplay::getRows());
	int x = 1;
	int y = 1;

	if (args.size() != 2 || args[0].type != TYPE_INT || args[1].type != TYPE_INT) throw std::runtime_error(name + " expects (int, int).");
	if (app == nullptr || width <= 0 || height <= 0) return true;

	x = std::max(1, std::min(valueAsInt(args[0]), width));
	y = std::max(1, std::min(valueAsInt(args[1]), height));
	app->setCursor(x - 1, y - 1);
	app->showCursor();
	app->drawCursor();
	return returnWithDirectScreenMutation(true);
}

bool applyPutLineColNumberProc(const std::string &name, const std::vector<Value> &args) {
	if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error(name + " expects one integer argument.");

	if (name == "PUT_LINE_NUM") {
		mrvmStoreRuntimeStateInt("macroScreen", "line", valueAsInt(args[0]));
		mrvmStoreRuntimeStateInt("macroScreen", "haveLine", 1);
	} else {
		mrvmStoreRuntimeStateInt("macroScreen", "col", valueAsInt(args[0]));
		mrvmStoreRuntimeStateInt("macroScreen", "haveCol", 1);
	}
	renderMacroLineColOverlay();
	return returnWithMacroScreenMutation(true);
}

bool applyScrollBoxProc(const std::string &name, const std::vector<Value> &args, bool down) {
	int x1 = 0;
	int y1 = 0;
	int x2 = 0;
	int y2 = 0;
	int attr = 0x07;

	if (args.size() != 5 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT) throw std::runtime_error(name + " expects (int, int, int, int, int).");

	x1 = valueAsInt(args[0]);
	y1 = valueAsInt(args[1]);
	x2 = valueAsInt(args[2]);
	y2 = valueAsInt(args[3]);
	attr = valueAsInt(args[4]);
	{
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		MacroCellGrid grid;
		grid.scrollBox(x1, y1, x2, y2, attr, down);
		grid.storeState();
	}
	return returnWithMacroScreenMutation(true);
}

bool applyClearScreenProc(const std::string &name, const std::vector<Value> &args) {
	int attr = 0x07;

	if (!(args.empty() || (args.size() == 1 && args[0].type == TYPE_INT))) throw std::runtime_error(name + " expects no arguments or one integer argument.");
	if (!args.empty()) attr = valueAsInt(args[0]);
	{
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		MacroCellGrid grid;
		grid.clearScreen(attr);
		grid.storeState();
	}
	return returnWithMacroScreenMutation(true);
}

bool applyKillBoxProc(const std::string &name, const std::vector<Value> &args) {
	if (!args.empty()) throw std::runtime_error(name + " expects no arguments.");
	{
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		MacroCellGrid grid;
		grid.killBox();
		grid.storeState();
	}
	return returnWithMacroScreenMutation(true);
}

bool mrvmUiMarquee(int kind, const std::string &text) {
	try {
		std::vector<Value> args;
		std::string name = "MARQUEE";

		args.push_back(makeStringValue(text));
		if (kind > 0) name = (kind == 1) ? "MARQUEE_WARNING" : "MARQUEE_ERROR";
		return applyMarqueeProc(name, args);
	} catch (...) {
		return false;
	}
}

bool mrvmUiBrain(bool enabled) {
	try {
		std::vector<Value> args;
		args.push_back(makeIntValue(enabled ? 1 : 0));
		return applyBrainProc("BRAIN", args);
	} catch (...) {
		return false;
	}
}

bool mrvmUiPutBox(int x1, int y1, int x2, int y2, int bgColor, int fgColor, const std::string &title, int shadow) {
	try {
		std::vector<Value> args;
		args.push_back(makeIntValue(x1));
		args.push_back(makeIntValue(y1));
		args.push_back(makeIntValue(x2));
		args.push_back(makeIntValue(y2));
		args.push_back(makeIntValue(bgColor));
		args.push_back(makeIntValue(fgColor));
		args.push_back(makeStringValue(title));
		args.push_back(makeIntValue(shadow));
		return applyPutBoxProc("PUT_BOX", args);
	} catch (...) {
		return false;
	}
}

bool mrvmUiWrite(const std::string &text, int x, int y, int bgColor, int fgColor) {
	try {
		std::vector<Value> args;
		args.push_back(makeStringValue(text));
		args.push_back(makeIntValue(x));
		args.push_back(makeIntValue(y));
		args.push_back(makeIntValue(bgColor));
		args.push_back(makeIntValue(fgColor));
		return applyWriteProc("WRITE", args);
	} catch (...) {
		return false;
	}
}

bool mrvmUiClrLine(int col, int row, int count) {
	try {
		std::vector<Value> args;
		if (col != 0 || row != 0 || count != 0) {
			args.push_back(makeIntValue(col));
			args.push_back(makeIntValue(row));
			args.push_back(makeIntValue(count));
		}
		return applyClrLineProc("CLR_LINE", args);
	} catch (...) {
		return false;
	}
}

bool mrvmUiGotoxy(int x, int y) {
	try {
		std::vector<Value> args;
		args.push_back(makeIntValue(x));
		args.push_back(makeIntValue(y));
		return applyGotoxyProc("GOTOXY", args);
	} catch (...) {
		return false;
	}
}

bool mrvmUiPutLineNum(int line) {
	try {
		std::vector<Value> args;
		args.push_back(makeIntValue(line));
		return applyPutLineColNumberProc("PUT_LINE_NUM", args);
	} catch (...) {
		return false;
	}
}

bool mrvmUiPutColNum(int col) {
	try {
		std::vector<Value> args;
		args.push_back(makeIntValue(col));
		return applyPutLineColNumberProc("PUT_COL_NUM", args);
	} catch (...) {
		return false;
	}
}

bool mrvmUiScrollBoxUp(int x1, int y1, int x2, int y2, int attr) {
	try {
		std::vector<Value> args;
		args.push_back(makeIntValue(x1));
		args.push_back(makeIntValue(y1));
		args.push_back(makeIntValue(x2));
		args.push_back(makeIntValue(y2));
		args.push_back(makeIntValue(attr));
		return applyScrollBoxProc("SCROLL_BOX_UP", args, false);
	} catch (...) {
		return false;
	}
}

bool mrvmUiScrollBoxDn(int x1, int y1, int x2, int y2, int attr) {
	try {
		std::vector<Value> args;
		args.push_back(makeIntValue(x1));
		args.push_back(makeIntValue(y1));
		args.push_back(makeIntValue(x2));
		args.push_back(makeIntValue(y2));
		args.push_back(makeIntValue(attr));
		return applyScrollBoxProc("SCROLL_BOX_DN", args, true);
	} catch (...) {
		return false;
	}
}

bool mrvmUiClearScreen(int attr) {
	try {
		std::vector<Value> args;
		args.push_back(makeIntValue(attr));
		return applyClearScreenProc("CLEAR_SCREEN", args);
	} catch (...) {
		return false;
	}
}

bool mrvmUiKillBox() {
	try {
		std::vector<Value> args;
		return applyKillBoxProc("KILL_BOX", args);
	} catch (...) {
		return false;
	}
}

bool mrvmUiRegisterMenuItem(const std::string &menuTitle, const std::string &itemTitle, const std::string &macroSpec, const std::string &ownerSpec, std::string *errorMessage) {
	auto *app = dynamic_cast<TApplication *>(TProgram::application);
	auto *menuBar = app != nullptr ? dynamic_cast<MRMenuBar *>(app->menuBar) : nullptr;

	if (errorMessage != nullptr) errorMessage->clear();
	if (menuBar == nullptr) {
		if (errorMessage != nullptr) *errorMessage = "REGISTER_MENU_ITEM requires an active MRMenuBar.";
		return false;
	}
	return returnWithDirectScreenMutation(menuBar->registerRuntimeMenuItem(menuTitle, itemTitle, macroSpec, ownerSpec, errorMessage));
}

bool mrvmUiRemoveMenuItem(const std::string &menuTitle, const std::string &itemTitle, const std::string &ownerSpec, std::string *errorMessage) {
	auto *app = dynamic_cast<TApplication *>(TProgram::application);
	auto *menuBar = app != nullptr ? dynamic_cast<MRMenuBar *>(app->menuBar) : nullptr;

	if (errorMessage != nullptr) errorMessage->clear();
	if (menuBar == nullptr) {
		if (errorMessage != nullptr) *errorMessage = "REMOVE_MENU_ITEM requires an active MRMenuBar.";
		return false;
	}
	return returnWithDirectScreenMutation(menuBar->removeRuntimeMenuItem(menuTitle, itemTitle, ownerSpec, errorMessage));
}

bool mrvmUiRemoveRuntimeMenusOwnedByMacroSpec(const std::string &ownerSpec, std::string *errorMessage) {
	auto *app = dynamic_cast<TApplication *>(TProgram::application);
	auto *menuBar = app != nullptr ? dynamic_cast<MRMenuBar *>(app->menuBar) : nullptr;

	if (errorMessage != nullptr) errorMessage->clear();
	if (menuBar == nullptr) return true;
	return returnWithDirectScreenMutation(menuBar->removeRuntimeNodesOwnedByMacroSpec(ownerSpec, errorMessage));
}

bool mrvmUiRemoveRuntimeMenusOwnedByFile(const std::string &fileSpec, std::string *errorMessage) {
	auto *app = dynamic_cast<TApplication *>(TProgram::application);
	auto *menuBar = app != nullptr ? dynamic_cast<MRMenuBar *>(app->menuBar) : nullptr;

	if (errorMessage != nullptr) errorMessage->clear();
	if (menuBar == nullptr) return true;
	return returnWithDirectScreenMutation(menuBar->removeRuntimeNodesOwnedByFile(fileSpec, errorMessage));
}

bool mrvmUiProjectRuntimeMenuKeyLabels(const std::vector<std::pair<std::string, std::string>> &labels, std::string *errorMessage) {
	auto *app = dynamic_cast<TApplication *>(TProgram::application);
	auto *menuBar = app != nullptr ? dynamic_cast<MRMenuBar *>(app->menuBar) : nullptr;

	if (errorMessage != nullptr) errorMessage->clear();
	if (menuBar == nullptr) return true;
	return returnWithDirectScreenMutation(menuBar->projectRuntimeMenuKeyLabels(labels));
}

bool mrvmUiRefreshRuntimeMenus(std::string *errorMessage) {
	auto *app = dynamic_cast<TApplication *>(TProgram::application);
	auto *menuBar = app != nullptr ? dynamic_cast<MRMenuBar *>(app->menuBar) : nullptr;

	if (errorMessage != nullptr) errorMessage->clear();
	if (menuBar == nullptr) return true;
	return returnWithDirectScreenMutation(menuBar->refreshRuntimeMenus(errorMessage));
}

bool mrvmUiMessageBox(const std::string &text) {
	try {
		messageBox(mfInformation | mfOKButton, "%s", text.c_str());
		return returnWithDirectScreenMutation(true);
	} catch (...) {
		return false;
	}
}

struct ScreenRenderFacade {
	static bool renderDeferredCommand(const MRMacroDeferredUiCommand &command) {
		switch (command.type) {
			case mrducCreateWindow:
				return mrvmUiCreateWindow();
			case mrducDeleteWindow:
				return mrvmUiDeleteCurrentWindow();
			case mrducModifyWindow:
				return mrvmUiModifyCurrentWindow();
			case mrducLinkWindow:
				return mrvmUiLinkCurrentWindow();
			case mrducUnlinkWindow:
				return mrvmUiUnlinkCurrentWindow();
			case mrducZoom:
				return mrvmUiZoomCurrentWindow();
			case mrducRedraw:
				return mrvmUiRedrawCurrentWindow();
			case mrducNewScreen:
				return mrvmUiNewScreen();
			case mrducSwitchWindow:
				return mrvmUiSwitchWindow(command.a1);
			case mrducSizeWindow:
				return mrvmUiSizeCurrentWindow(command.a1, command.a2, command.a3, command.a4);
			case mrducMarqueeInfo:
				return mrvmUiMarquee(0, command.text);
			case mrducMarqueeWarning:
				return mrvmUiMarquee(1, command.text);
			case mrducMarqueeError:
				return mrvmUiMarquee(2, command.text);
			case mrducMakeMessage:
				return applyMakeMessageProc(std::vector<Value>{makeStringValue(command.text)});
			case mrducBrain:
				return mrvmUiBrain(command.a1 != 0);
			case mrducDesktopSetColor:
			case mrducDesktopPutChar:
			case mrducDesktopPutString:
			case mrducDesktopBlit:
			case mrducDesktopClear:
				return applyDesktopCanvasCommand(command);
			case mrducPutBox:
				return mrvmUiPutBox(command.a1, command.a2, command.a3, command.a4, command.a5, command.a6, command.text, command.a7);
			case mrducWrite:
				return mrvmUiWrite(command.text, command.a1, command.a2, command.a3, command.a4);
			case mrducClrLine:
				return mrvmUiClrLine(command.a1, command.a2, command.a3);
			case mrducGotoxy:
				return mrvmUiGotoxy(command.a1, command.a2);
			case mrducPutLineNum:
				return mrvmUiPutLineNum(command.a1);
			case mrducPutColNum:
				return mrvmUiPutColNum(command.a1);
			case mrducScrollBoxUp:
				return mrvmUiScrollBoxUp(command.a1, command.a2, command.a3, command.a4, command.a5);
			case mrducScrollBoxDn:
				return mrvmUiScrollBoxDn(command.a1, command.a2, command.a3, command.a4, command.a5);
			case mrducClearScreen:
				return mrvmUiClearScreen(command.a1);
			case mrducKillBox:
				return mrvmUiKillBox();
			case mrducRegisterMenuItem:
				return mrvmUiRegisterMenuItem(command.text, command.text2, command.text3, command.text4);
			case mrducRemoveMenuItem:
				return mrvmUiRemoveMenuItem(command.text, command.text2, command.text3);
			case mrducMessageBox:
				return mrvmUiMessageBox(command.text);
			case mrducDelay:
				return true;
			default:
				return false;
		}
	}
};

bool mrvmUiScreenRenderDeferredCommand(const MRMacroDeferredUiCommand &command) {
	return ScreenRenderFacade::renderDeferredCommand(command);
}
