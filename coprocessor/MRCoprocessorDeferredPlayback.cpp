#define Uses_TProgram
#include <tvision/tv.h>

#include "MRCoprocessorDeferredPlayback.hpp"
#include "MRCoprocessorDispatch.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "MRWindowCommands.hpp"
#include "../mrmac/MRVM.hpp"
#include "../mrmac/mrmac.h"
#include "../mrmac/vm/MRVMRuntimeKv.hpp"
#include "../mrmac/vm/MRVMRuntimeState.hpp"
#include "../mrmac/vm/MRVMValue.hpp"
#include "../ui/MREditWindow.hpp"
#include "../ui/MRWindowSupport.hpp"

namespace {

constexpr std::size_t kMacroUiPlaybackBudgetCommands = 48;
const std::chrono::milliseconds kMacroUiPlaybackBudgetSlice(2);

constexpr std::array<const char *, mrducDelay + 1> kDeferredUiCommandNames{
    "UNKNOWN", "CREATE_WINDOW", "DELETE_WINDOW", "MODIFY_WINDOW", "LINK_WINDOW", "UNLINK_WINDOW", "ZOOM", "REDRAW", "NEW_SCREEN", "SWITCH_WINDOW", "SIZE_WINDOW", "MARQUEE", "MARQUEE_WARNING", "MARQUEE_ERROR", "MAKE_MESSAGE", "BRAIN", "PUT_BOX", "WRITE", "CLR_LINE", "GOTOXY", "PUT_LINE_NUM", "PUT_COL_NUM", "SCROLL_BOX_UP", "SCROLL_BOX_DN", "CLEAR_SCREEN", "KILL_BOX", "REGISTER_MENU_ITEM", "REMOVE_MENU_ITEM", "MESSAGEBOX", "DELAY",
};

std::optional<const char *> deferredUiCommandNameAt(int type) {
	if (type < 0) return std::nullopt;
	const std::size_t index = static_cast<std::size_t>(type);
	if (index >= kDeferredUiCommandNames.size()) return std::nullopt;
	return kDeferredUiCommandNames[index];
}

const char *deferredUiCommandName(int type) {
	return deferredUiCommandNameAt(type).value_or("UNKNOWN");
}

struct DeferredUiRenderGateway {
	static bool renderDeferredCommand(const MRMacroDeferredUiCommand &command) {
		static constexpr const char *kNoActiveApplicationMessage = "Deferred UI render failed: no active application.";
		if (command.type == mrducDelay) return true;
		if (TProgram::application == nullptr) {
			mrLogMessage(kNoActiveApplicationMessage);
			return false;
		}
		return mrvmUiRenderFacadeRenderDeferredCommand(command);
	}
};

int marqueeKindFromDeferredType(int type) noexcept {
	switch (type) {
		case mrducMarqueeWarning:
			return 1;
		case mrducMarqueeError:
			return 2;
		default:
			return 0;
	}
}

struct MacroScreenModel {
	struct Cell {
		char ch;
		unsigned char attr;
		bool known;

		Cell() noexcept : ch(' '), attr(0x07), known(false) {
		}

		Cell(char aCh, unsigned char anAttr, bool isKnown) noexcept : ch(aCh), attr(anAttr), known(isKnown) {
		}
	};

	bool seeded;
	bool cursorKnown;
	int cursorX;
	int cursorY;
	bool lineNumberKnown;
	int lineNumber;
	bool colNumberKnown;
	int colNumber;
	bool brainKnown;
	bool brainEnabled;
	bool marqueeKnown;
	int marqueeKind;
	std::string marqueeText;
	int screenWidth;
	int screenHeight;
	std::vector<Cell> cells;

	MacroScreenModel() noexcept : seeded(false), cursorKnown(false), cursorX(1), cursorY(1), lineNumberKnown(false), lineNumber(0), colNumberKnown(false), colNumber(0), brainKnown(false), brainEnabled(false), marqueeKnown(false), marqueeKind(0), marqueeText(), screenWidth(0), screenHeight(0), cells() {
	}

	void seedFromRuntime() {
		int x = 1;
		int y = 1;
		screenWidth = std::max(0, mrvmUiScreenWidth());
		screenHeight = std::max(0, mrvmUiScreenHeight());
		if (screenWidth > 0 && screenHeight > 0) cells.assign(static_cast<std::size_t>(screenWidth) * static_cast<std::size_t>(screenHeight), Cell());
		else
			cells.clear();
		if (mrvmUiCursorPosition(x, y)) {
			cursorKnown = true;
			cursorX = x;
			cursorY = y;
		}
		seeded = true;
	}

	void invalidateAfterRenderFailure() {
		seeded = false;
		cursorKnown = false;
		lineNumberKnown = false;
		colNumberKnown = false;
		brainKnown = false;
		marqueeKnown = false;
		screenWidth = 0;
		screenHeight = 0;
		cells.clear();
	}

	[[nodiscard]] bool hasGrid() const noexcept {
		return screenWidth > 0 && screenHeight > 0 && cells.size() == static_cast<std::size_t>(screenWidth) * static_cast<std::size_t>(screenHeight);
	}

	[[nodiscard]] static unsigned char composeAttribute(int bgColor, int fgColor) noexcept {
		if ((bgColor & 0xFF) == 0) return static_cast<unsigned char>(fgColor & 0xFF);
		return static_cast<unsigned char>(((bgColor & 0x0F) << 4) | (fgColor & 0x0F));
	}

	[[nodiscard]] static int marqueeKindFor(int type) noexcept {
		return marqueeKindFromDeferredType(type);
	}

	[[nodiscard]] std::size_t indexFor(int x, int y) const noexcept {
		return static_cast<std::size_t>(y) * static_cast<std::size_t>(screenWidth) + static_cast<std::size_t>(x);
	}

	bool writeCell(int x, int y, char ch, unsigned char attr) {
		Cell &cell = cells[indexFor(x, y)];
		const bool changed = !cell.known || cell.ch != ch || cell.attr != attr;
		cell.ch = ch;
		cell.attr = attr;
		cell.known = true;
		return changed;
	}

	bool copyCell(int dstX, int dstY, int srcX, int srcY) {
		Cell &dst = cells[indexFor(dstX, dstY)];
		const Cell src = cells[indexFor(srcX, srcY)];
		const bool changed = dst.known != src.known || dst.ch != src.ch || dst.attr != src.attr;
		dst = src;
		return changed;
	}

	bool fillRect(int x1, int y1, int x2, int y2, char ch, unsigned char attr) {
		bool changed = false;
		for (int y = y1; y <= y2; ++y)
			for (int x = x1; x <= x2; ++x)
				changed = writeCell(x, y, ch, attr) || changed;
		return changed;
	}

	bool applyClearScreen(const MRMacroDeferredUiCommand &command) {
		const unsigned char attr = static_cast<unsigned char>(command.a1 & 0xFF);
		if (!hasGrid()) {
			cursorKnown = true;
			cursorX = 1;
			cursorY = 1;
			lineNumberKnown = false;
			lineNumber = 0;
			colNumberKnown = false;
			colNumber = 0;
			return true;
		}

		bool changed = false;
		for (std::size_t i = 0; i < cells.size(); ++i)
			changed = writeCell(static_cast<int>(i % static_cast<std::size_t>(screenWidth)), static_cast<int>(i / static_cast<std::size_t>(screenWidth)), ' ', attr) || changed;
		cursorKnown = true;
		cursorX = 1;
		cursorY = 1;
		lineNumberKnown = false;
		lineNumber = 0;
		colNumberKnown = false;
		colNumber = 0;
		return changed;
	}

	bool applyWrite(const MRMacroDeferredUiCommand &command) {
		if (!hasGrid()) return true;

		const int y = command.a2 - 1;
		if (y < 0 || y >= screenHeight) return false;

		const unsigned char attr = composeAttribute(command.a3, command.a4);
		bool changed = false;
		for (std::size_t i = 0; i < command.text.size(); ++i) {
			const int x = command.a1 - 1 + static_cast<int>(i);
			if (x < 0) continue;
			if (x >= screenWidth) break;
			changed = writeCell(x, y, command.text[i], attr) || changed;
		}
		return changed;
	}

	bool applyPutBox(const MRMacroDeferredUiCommand &command) {
		if (!hasGrid()) return true;

		int x1 = command.a1 - 1;
		int y1 = command.a2 - 1;
		int x2 = command.a3 - 1;
		int y2 = command.a4 - 1;
		const unsigned char attr = composeAttribute(command.a5, command.a6);
		const bool shadow = command.a7 != 0;
		bool changed = false;
		std::string title = command.text;

		if (x1 > x2) std::swap(x1, x2);
		if (y1 > y2) std::swap(y1, y2);
		x1 = std::max(0, std::min(x1, screenWidth - 1));
		x2 = std::max(0, std::min(x2, screenWidth - 1));
		y1 = std::max(0, std::min(y1, screenHeight - 1));
		y2 = std::max(0, std::min(y2, screenHeight - 1));
		if (x1 > x2 || y1 > y2) return false;

		changed = fillRect(x1, y1, x2, y2, ' ', attr) || changed;
		for (int x = x1 + 1; x < x2; ++x) {
			changed = writeCell(x, y1, '-', attr) || changed;
			changed = writeCell(x, y2, '-', attr) || changed;
		}
		for (int y = y1 + 1; y < y2; ++y) {
			changed = writeCell(x1, y, '|', attr) || changed;
			changed = writeCell(x2, y, '|', attr) || changed;
		}
		changed = writeCell(x1, y1, '+', attr) || changed;
		changed = writeCell(x2, y1, '+', attr) || changed;
		changed = writeCell(x1, y2, '+', attr) || changed;
		changed = writeCell(x2, y2, '+', attr) || changed;

		if (!title.empty() && x2 - x1 >= 2) {
			const int maxTitleLen = x2 - x1 - 1;
			if (maxTitleLen > 0) {
				if (static_cast<int>(title.size()) > maxTitleLen) title = title.substr(0, static_cast<std::size_t>(maxTitleLen));
				const int startX = x1 + 1 + std::max(0, (maxTitleLen - static_cast<int>(title.size())) / 2);
				for (std::size_t i = 0; i < title.size(); ++i) {
					const int x = startX + static_cast<int>(i);
					if (x >= x1 + 1 && x <= x2 - 1) changed = writeCell(x, y1, title[i], attr) || changed;
				}
			}
		}

		if (shadow) {
			if (x2 + 1 < screenWidth) changed = fillRect(x2 + 1, y1 + 1, x2 + 1, y2 + 1, ' ', 0x08) || changed;
			if (y2 + 1 < screenHeight) changed = fillRect(x1 + 1, y2 + 1, x2 + 1, y2 + 1, ' ', 0x08) || changed;
		}

		return changed;
	}

	bool applyClrLine() {
		if (!cursorKnown || !hasGrid()) return true;

		const int y = std::max(0, std::min(cursorY - 1, screenHeight - 1));
		unsigned char attr = 0x07;
		const Cell &rowHead = cells[indexFor(0, y)];
		if (rowHead.known) attr = rowHead.attr;
		return fillRect(0, y, screenWidth - 1, y, ' ', attr);
	}

	bool applyClrLine(const MRMacroDeferredUiCommand &command) {
		if (command.a3 <= 0) return applyClrLine();
		if (!hasGrid()) return true;

		int x = command.a1 - 1;
		const int y = command.a2 - 1;
		int count = command.a3;
		if (y < 0 || y >= screenHeight || count <= 0) return false;
		if (x < 0) {
			count += x;
			x = 0;
		}
		if (x >= screenWidth || count <= 0) return false;
		count = std::min(count, screenWidth - x);

		unsigned char attr = 0x07;
		const Cell &rowHead = cells[indexFor(0, y)];
		if (rowHead.known) attr = rowHead.attr;
		return fillRect(x, y, x + count - 1, y, ' ', attr);
	}

	bool applyGotoxy(const MRMacroDeferredUiCommand &command) {
		int x = command.a1;
		int y = command.a2;
		if (screenWidth > 0) x = std::max(1, std::min(x, screenWidth));
		if (screenHeight > 0) y = std::max(1, std::min(y, screenHeight));
		const bool changed = !cursorKnown || cursorX != x || cursorY != y;
		cursorKnown = true;
		cursorX = x;
		cursorY = y;
		return changed;
	}

	bool applyPutLineNum(const MRMacroDeferredUiCommand &command) {
		const bool changed = !lineNumberKnown || lineNumber != command.a1;
		lineNumberKnown = true;
		lineNumber = command.a1;
		return changed;
	}

	bool applyPutColNum(const MRMacroDeferredUiCommand &command) {
		const bool changed = !colNumberKnown || colNumber != command.a1;
		colNumberKnown = true;
		colNumber = command.a1;
		return changed;
	}

	bool applyScroll(const MRMacroDeferredUiCommand &command, bool down) {
		if (!hasGrid()) return true;

		int x1 = command.a1 - 1;
		int y1 = command.a2 - 1;
		int x2 = command.a3 - 1;
		int y2 = command.a4 - 1;
		const unsigned char attr = static_cast<unsigned char>(command.a5 & 0xFF);
		bool changed = false;

		if (x1 > x2) std::swap(x1, x2);
		if (y1 > y2) std::swap(y1, y2);
		x1 = std::max(0, std::min(x1, screenWidth - 1));
		x2 = std::max(0, std::min(x2, screenWidth - 1));
		y1 = std::max(0, std::min(y1, screenHeight - 1));
		y2 = std::max(0, std::min(y2, screenHeight - 1));
		if (x1 > x2 || y1 > y2) return false;

		if (y2 - y1 + 1 <= 1) return fillRect(x1, y1, x2, y2, ' ', attr);

		if (down) {
			for (int y = y2; y > y1; --y)
				for (int x = x1; x <= x2; ++x)
					changed = copyCell(x, y, x, y - 1) || changed;
			changed = fillRect(x1, y1, x2, y1, ' ', attr) || changed;
		} else {
			for (int y = y1; y < y2; ++y)
				for (int x = x1; x <= x2; ++x)
					changed = copyCell(x, y, x, y + 1) || changed;
			changed = fillRect(x1, y2, x2, y2, ' ', attr) || changed;
		}
		return changed;
	}

	bool applyMarquee(const MRMacroDeferredUiCommand &command) {
		const int nextKind = marqueeKindFor(command.type);
		const bool changed = !marqueeKnown || marqueeKind != nextKind || marqueeText != command.text;
		marqueeKnown = true;
		marqueeKind = nextKind;
		marqueeText = command.text;
		return changed;
	}

	bool applyBrain(const MRMacroDeferredUiCommand &command) {
		const bool enabled = command.a1 != 0;
		const bool changed = !brainKnown || brainEnabled != enabled;
		brainKnown = true;
		brainEnabled = enabled;
		return changed;
	}

	bool shouldRenderAndProject(const MRMacroDeferredUiCommand &command) {
		switch (command.type) {
			case mrducMarqueeInfo:
			case mrducMarqueeWarning:
			case mrducMarqueeError:
			case mrducMakeMessage:
				return applyMarquee(command);
			case mrducBrain:
				return applyBrain(command);
			case mrducGotoxy:
				return applyGotoxy(command);
			case mrducPutLineNum:
				return applyPutLineNum(command);
			case mrducPutColNum:
				return applyPutColNum(command);
			case mrducWrite:
				return applyWrite(command);
			case mrducPutBox:
				return applyPutBox(command);
			case mrducClrLine:
				return applyClrLine(command);
			case mrducScrollBoxUp:
				return applyScroll(command, false);
			case mrducScrollBoxDn:
				return applyScroll(command, true);
			case mrducClearScreen:
				return applyClearScreen(command);
			case mrducKillBox:
			case mrducMessageBox:
				return true;
			default:
				return true;
		}
	}
};

struct MacroScreenView {
	static bool render(const MRMacroDeferredUiCommand &command) {
		return DeferredUiRenderGateway::renderDeferredCommand(command);
	}
};

struct DeferredMacroUiPlayback {
	std::size_t documentId;
	std::string displayName;
	std::vector<MRMacroDeferredUiCommand> commands;
	std::size_t nextIndex;
	std::size_t appliedCount;
	std::size_t skippedCount;
	std::size_t failedCount;
	std::uint64_t observedScreenEpoch;
	bool waitingForDelay;
	std::chrono::steady_clock::time_point resumeAfter;
	MacroScreenModel screenModel;

	DeferredMacroUiPlayback(std::size_t aDocumentId, std::string aDisplayName, std::vector<MRMacroDeferredUiCommand> aCommands) : documentId(aDocumentId), displayName(std::move(aDisplayName)), commands(std::move(aCommands)), nextIndex(0), appliedCount(0), skippedCount(0), failedCount(0), observedScreenEpoch(0), waitingForDelay(false), resumeAfter(std::chrono::steady_clock::time_point::min()), screenModel() {
	}
};

using RuntimeValue = VirtualMachine::Value;

RuntimeValue deferredPlaybackRoot(MRVMRuntimeKv &runtimeKv) {
	return runtimeKv.ensureRoot("DEFERREDUI");
}

RuntimeValue deferredPlaybackQueue(MRVMRuntimeKv &runtimeKv) {
	return runtimeKv.ensureChild(deferredPlaybackRoot(runtimeKv), "playbackQueue");
}

bool deferredReadValue(MRVMRuntimeKv &runtimeKv, const RuntimeValue &parent, const char *key, RuntimeValue &value) {
	MRVMHashStore &store = runtimeKv.globalStore();

	if (!mrvmHashContainsValue(store, store, parent, key)) return false;
	value = mrvmHashReadValue(store, store, parent, key);
	return true;
}

int deferredReadInt(MRVMRuntimeKv &runtimeKv, const RuntimeValue &parent, const char *key, int fallback = 0) {
	RuntimeValue value;

	if (!deferredReadValue(runtimeKv, parent, key, value) || value.type != TYPE_INT) return fallback;
	return value.i;
}

std::uint64_t deferredParseUnsigned(const std::string &text) {
	char *end = nullptr;
	const unsigned long long value = std::strtoull(text.c_str(), &end, 10);

	return end != text.c_str() && *end == '\0' ? static_cast<std::uint64_t>(value) : 0;
}

std::uint64_t deferredReadUnsigned(MRVMRuntimeKv &runtimeKv, const RuntimeValue &parent, const char *key) {
	RuntimeValue value;

	if (!deferredReadValue(runtimeKv, parent, key, value)) return 0;
	if (value.type == TYPE_STR) return deferredParseUnsigned(value.s);
	if (value.type == TYPE_INT) return value.i > 0 ? static_cast<std::uint64_t>(value.i) : 0;
	return 0;
}

std::string deferredReadString(MRVMRuntimeKv &runtimeKv, const RuntimeValue &parent, const char *key) {
	RuntimeValue value;

	if (!deferredReadValue(runtimeKv, parent, key, value) || value.type != TYPE_STR) return std::string();
	return value.s;
}

void deferredWriteInt(MRVMRuntimeKv &runtimeKv, const RuntimeValue &parent, const char *key, int value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeInt(value));
}

void deferredWriteUnsigned(MRVMRuntimeKv &runtimeKv, const RuntimeValue &parent, const char *key, std::uint64_t value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeString(std::to_string(value)));
}

void deferredWriteString(MRVMRuntimeKv &runtimeKv, const RuntimeValue &parent, const char *key, const std::string &value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeString(value));
}

void writeDeferredCommand(MRVMRuntimeKv &runtimeKv, const RuntimeValue &parent, const MRMacroDeferredUiCommand &command) {
	deferredWriteInt(runtimeKv, parent, "type", command.type);
	deferredWriteInt(runtimeKv, parent, "a1", command.a1);
	deferredWriteInt(runtimeKv, parent, "a2", command.a2);
	deferredWriteInt(runtimeKv, parent, "a3", command.a3);
	deferredWriteInt(runtimeKv, parent, "a4", command.a4);
	deferredWriteInt(runtimeKv, parent, "a5", command.a5);
	deferredWriteInt(runtimeKv, parent, "a6", command.a6);
	deferredWriteInt(runtimeKv, parent, "a7", command.a7);
	deferredWriteInt(runtimeKv, parent, "a8", command.a8);
	deferredWriteString(runtimeKv, parent, "text", command.text);
	deferredWriteString(runtimeKv, parent, "text2", command.text2);
	deferredWriteString(runtimeKv, parent, "text3", command.text3);
	deferredWriteString(runtimeKv, parent, "text4", command.text4);
}

MRMacroDeferredUiCommand readDeferredCommand(MRVMRuntimeKv &runtimeKv, const RuntimeValue &parent) {
	MRMacroDeferredUiCommand command;

	command.type = deferredReadInt(runtimeKv, parent, "type");
	command.a1 = deferredReadInt(runtimeKv, parent, "a1");
	command.a2 = deferredReadInt(runtimeKv, parent, "a2");
	command.a3 = deferredReadInt(runtimeKv, parent, "a3");
	command.a4 = deferredReadInt(runtimeKv, parent, "a4");
	command.a5 = deferredReadInt(runtimeKv, parent, "a5");
	command.a6 = deferredReadInt(runtimeKv, parent, "a6");
	command.a7 = deferredReadInt(runtimeKv, parent, "a7");
	command.a8 = deferredReadInt(runtimeKv, parent, "a8");
	command.text = deferredReadString(runtimeKv, parent, "text");
	command.text2 = deferredReadString(runtimeKv, parent, "text2");
	command.text3 = deferredReadString(runtimeKv, parent, "text3");
	command.text4 = deferredReadString(runtimeKv, parent, "text4");
	return command;
}

std::string encodeMacroScreenModelCells(const std::vector<MacroScreenModel::Cell> &cells) {
	std::string encoded;

	encoded.reserve(cells.size() * 3);
	for (const MacroScreenModel::Cell &cell : cells) {
		encoded.push_back(cell.ch);
		encoded.push_back(static_cast<char>(cell.attr));
		encoded.push_back(cell.known ? '\1' : '\0');
	}
	return encoded;
}

std::vector<MacroScreenModel::Cell> decodeMacroScreenModelCells(const std::string &encoded) {
	std::vector<MacroScreenModel::Cell> cells;

	cells.reserve(encoded.size() / 3);
	for (std::size_t i = 0; i + 2 < encoded.size(); i += 3)
		cells.emplace_back(encoded[i], static_cast<unsigned char>(encoded[i + 1]), encoded[i + 2] != '\0');
	return cells;
}

void writeMacroScreenModel(MRVMRuntimeKv &runtimeKv, const RuntimeValue &parent, const MacroScreenModel &model) {
	deferredWriteInt(runtimeKv, parent, "seeded", model.seeded ? 1 : 0);
	deferredWriteInt(runtimeKv, parent, "cursorKnown", model.cursorKnown ? 1 : 0);
	deferredWriteInt(runtimeKv, parent, "cursorX", model.cursorX);
	deferredWriteInt(runtimeKv, parent, "cursorY", model.cursorY);
	deferredWriteInt(runtimeKv, parent, "lineNumberKnown", model.lineNumberKnown ? 1 : 0);
	deferredWriteInt(runtimeKv, parent, "lineNumber", model.lineNumber);
	deferredWriteInt(runtimeKv, parent, "colNumberKnown", model.colNumberKnown ? 1 : 0);
	deferredWriteInt(runtimeKv, parent, "colNumber", model.colNumber);
	deferredWriteInt(runtimeKv, parent, "brainKnown", model.brainKnown ? 1 : 0);
	deferredWriteInt(runtimeKv, parent, "brainEnabled", model.brainEnabled ? 1 : 0);
	deferredWriteInt(runtimeKv, parent, "marqueeKnown", model.marqueeKnown ? 1 : 0);
	deferredWriteInt(runtimeKv, parent, "marqueeKind", model.marqueeKind);
	deferredWriteString(runtimeKv, parent, "marqueeText", model.marqueeText);
	deferredWriteInt(runtimeKv, parent, "screenWidth", model.screenWidth);
	deferredWriteInt(runtimeKv, parent, "screenHeight", model.screenHeight);
	deferredWriteString(runtimeKv, parent, "cells", encodeMacroScreenModelCells(model.cells));
}

void readMacroScreenModel(MRVMRuntimeKv &runtimeKv, const RuntimeValue &parent, MacroScreenModel &model) {
	model.seeded = deferredReadInt(runtimeKv, parent, "seeded") != 0;
	model.cursorKnown = deferredReadInt(runtimeKv, parent, "cursorKnown") != 0;
	model.cursorX = deferredReadInt(runtimeKv, parent, "cursorX", 1);
	model.cursorY = deferredReadInt(runtimeKv, parent, "cursorY", 1);
	model.lineNumberKnown = deferredReadInt(runtimeKv, parent, "lineNumberKnown") != 0;
	model.lineNumber = deferredReadInt(runtimeKv, parent, "lineNumber");
	model.colNumberKnown = deferredReadInt(runtimeKv, parent, "colNumberKnown") != 0;
	model.colNumber = deferredReadInt(runtimeKv, parent, "colNumber");
	model.brainKnown = deferredReadInt(runtimeKv, parent, "brainKnown") != 0;
	model.brainEnabled = deferredReadInt(runtimeKv, parent, "brainEnabled") != 0;
	model.marqueeKnown = deferredReadInt(runtimeKv, parent, "marqueeKnown") != 0;
	model.marqueeKind = deferredReadInt(runtimeKv, parent, "marqueeKind");
	model.marqueeText = deferredReadString(runtimeKv, parent, "marqueeText");
	model.screenWidth = deferredReadInt(runtimeKv, parent, "screenWidth");
	model.screenHeight = deferredReadInt(runtimeKv, parent, "screenHeight");
	model.cells = decodeMacroScreenModelCells(deferredReadString(runtimeKv, parent, "cells"));
}

std::uint64_t steadyClockMillis(const std::chrono::steady_clock::time_point &value) {
	if (value == std::chrono::steady_clock::time_point::min()) return 0;
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(value.time_since_epoch()).count());
}

void writeDeferredPlayback(MRVMRuntimeKv &runtimeKv, const RuntimeValue &parent, const DeferredMacroUiPlayback &playback, bool writeCommands) {
	deferredWriteUnsigned(runtimeKv, parent, "documentId", playback.documentId);
	deferredWriteString(runtimeKv, parent, "displayName", playback.displayName);
	deferredWriteUnsigned(runtimeKv, parent, "nextIndex", playback.nextIndex);
	deferredWriteUnsigned(runtimeKv, parent, "appliedCount", playback.appliedCount);
	deferredWriteUnsigned(runtimeKv, parent, "skippedCount", playback.skippedCount);
	deferredWriteUnsigned(runtimeKv, parent, "failedCount", playback.failedCount);
	deferredWriteUnsigned(runtimeKv, parent, "observedScreenEpoch", playback.observedScreenEpoch);
	deferredWriteInt(runtimeKv, parent, "waitingForDelay", playback.waitingForDelay ? 1 : 0);
	deferredWriteUnsigned(runtimeKv, parent, "resumeAfterMs", steadyClockMillis(playback.resumeAfter));
	writeMacroScreenModel(runtimeKv, runtimeKv.replaceChild(parent, "screenModel"), playback.screenModel);
	if (!writeCommands) return;
	RuntimeValue commands = runtimeKv.replaceChild(parent, "commands");
	deferredWriteUnsigned(runtimeKv, commands, "count", playback.commands.size());
	for (std::size_t i = 0; i < playback.commands.size(); ++i)
		writeDeferredCommand(runtimeKv, runtimeKv.ensureChild(commands, std::to_string(i)), playback.commands[i]);
}

bool readDeferredPlayback(MRVMRuntimeKv &runtimeKv, const RuntimeValue &parent, DeferredMacroUiPlayback &playback) {
	RuntimeValue commands;
	std::vector<MRMacroDeferredUiCommand> storedCommands;

	if (!runtimeKv.findChild(parent, "commands", commands)) return false;
	const std::size_t count = static_cast<std::size_t>(deferredReadUnsigned(runtimeKv, commands, "count"));
	storedCommands.reserve(count);
	for (std::size_t i = 0; i < count; ++i) {
		RuntimeValue command;
		if (!runtimeKv.findChild(commands, std::to_string(i), command)) return false;
		storedCommands.push_back(readDeferredCommand(runtimeKv, command));
	}
	playback = DeferredMacroUiPlayback(
	    static_cast<std::size_t>(deferredReadUnsigned(runtimeKv, parent, "documentId")),
	    deferredReadString(runtimeKv, parent, "displayName"),
	    std::move(storedCommands));
	playback.nextIndex = static_cast<std::size_t>(deferredReadUnsigned(runtimeKv, parent, "nextIndex"));
	playback.appliedCount = static_cast<std::size_t>(deferredReadUnsigned(runtimeKv, parent, "appliedCount"));
	playback.skippedCount = static_cast<std::size_t>(deferredReadUnsigned(runtimeKv, parent, "skippedCount"));
	playback.failedCount = static_cast<std::size_t>(deferredReadUnsigned(runtimeKv, parent, "failedCount"));
	playback.observedScreenEpoch = deferredReadUnsigned(runtimeKv, parent, "observedScreenEpoch");
	playback.waitingForDelay = deferredReadInt(runtimeKv, parent, "waitingForDelay") != 0;
	const std::uint64_t resumeAfterMs = deferredReadUnsigned(runtimeKv, parent, "resumeAfterMs");
	playback.resumeAfter = resumeAfterMs != 0 ? std::chrono::steady_clock::time_point(std::chrono::milliseconds(resumeAfterMs)) : std::chrono::steady_clock::time_point::min();
	RuntimeValue model;
	if (runtimeKv.findChild(parent, "screenModel", model)) readMacroScreenModel(runtimeKv, model, playback.screenModel);
	return true;
}

std::vector<std::uint64_t> deferredPlaybackIds(MRVMRuntimeKv &runtimeKv, const RuntimeValue &queue) {
	const std::vector<std::string> keys = runtimeKv.globalStore().keys(queue.hashHandle);
	std::vector<std::uint64_t> ids;

	ids.reserve(keys.size());
	for (const std::string &key : keys) {
		const std::uint64_t id = deferredParseUnsigned(key);
		if (id != 0) ids.push_back(id);
	}
	std::sort(ids.begin(), ids.end());
	return ids;
}

bool loadFirstDeferredPlayback(std::uint64_t &playbackId, DeferredMacroUiPlayback &playback) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	RuntimeValue root;
	RuntimeValue queue;

	playbackId = 0;
	if (!runtimeKv.findRoot("DEFERREDUI", root) || !runtimeKv.findChild(root, "playbackQueue", queue)) return false;
	const std::vector<std::uint64_t> ids = deferredPlaybackIds(runtimeKv, queue);
	for (std::uint64_t id : ids) {
		RuntimeValue stored;
		if (!runtimeKv.findChild(queue, std::to_string(id), stored)) continue;
		if (!readDeferredPlayback(runtimeKv, stored, playback)) {
			static_cast<void>(runtimeKv.eraseChild(queue, std::to_string(id)));
			continue;
		}
		playbackId = id;
		return true;
	}
	return false;
}

void storeDeferredPlaybackProgress(std::uint64_t playbackId, const DeferredMacroUiPlayback &playback) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	RuntimeValue root;
	RuntimeValue queue;
	RuntimeValue stored;

	if (playbackId == 0 || !runtimeKv.findRoot("DEFERREDUI", root) || !runtimeKv.findChild(root, "playbackQueue", queue) || !runtimeKv.findChild(queue, std::to_string(playbackId), stored)) return;
	writeDeferredPlayback(runtimeKv, stored, playback, false);
}

void eraseDeferredPlayback(std::uint64_t playbackId) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	RuntimeValue root;
	RuntimeValue queue;

	if (playbackId == 0 || !runtimeKv.findRoot("DEFERREDUI", root) || !runtimeKv.findChild(root, "playbackQueue", queue)) return;
	static_cast<void>(runtimeKv.eraseChild(queue, std::to_string(playbackId)));
}

void queueDeferredMacroUiPlaybackInternal(std::size_t documentId, const std::string &displayName, const std::vector<MRMacroDeferredUiCommand> &commands) {
	if (commands.empty()) return;
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	const RuntimeValue root = deferredPlaybackRoot(runtimeKv);
	const RuntimeValue queue = deferredPlaybackQueue(runtimeKv);
	const std::uint64_t playbackId = deferredReadUnsigned(runtimeKv, root, "nextPlaybackId") + 1;
	const DeferredMacroUiPlayback playback(documentId, displayName, commands);

	deferredWriteUnsigned(runtimeKv, root, "nextPlaybackId", playbackId);
	writeDeferredPlayback(runtimeKv, runtimeKv.replaceChild(queue, std::to_string(playbackId)), playback, true);
}

void logDeferredMacroUiPlaybackSummary(const DeferredMacroUiPlayback &playback) {
	std::ostringstream summary;
	summary << "Deferred UI playback '" << playback.displayName << "' finished: applied=" << playback.appliedCount << ", skipped=" << playback.skippedCount << ", failed=" << playback.failedCount << ".";
	mrLogMessage(summary.str().c_str());
}

void pumpDeferredMacroUiPlaybackQueue() {
	const auto deadline = std::chrono::steady_clock::now() + kMacroUiPlaybackBudgetSlice;
	std::size_t remainingCommands = kMacroUiPlaybackBudgetCommands;
	std::uint64_t playbackId = 0;
	DeferredMacroUiPlayback playback(0, std::string(), std::vector<MRMacroDeferredUiCommand>());

	while (loadFirstDeferredPlayback(playbackId, playback) && remainingCommands > 0 && std::chrono::steady_clock::now() < deadline) {
		MREditWindow *targetWindow = findEditWindowByBufferId(static_cast<int>(playback.documentId));

		if (playback.waitingForDelay) {
			if (std::chrono::steady_clock::now() < playback.resumeAfter) {
				storeDeferredPlaybackProgress(playbackId, playback);
				break;
			}
			playback.waitingForDelay = false;
			playback.resumeAfter = std::chrono::steady_clock::time_point::min();
		}
		{
			const std::uint64_t liveEpoch = mrvmUiScreenMutationEpoch();
			if (playback.observedScreenEpoch == 0) playback.observedScreenEpoch = liveEpoch;
			else if (liveEpoch != playback.observedScreenEpoch) {
				playback.screenModel.invalidateAfterRenderFailure();
				playback.observedScreenEpoch = liveEpoch;
			}
		}
		if (!playback.screenModel.seeded) playback.screenModel.seedFromRuntime();
		if (targetWindow != nullptr) mrvmUiSetCurrentWindow(targetWindow);
		mrvmUiBeginMacroScreenBatch();

		while (playback.nextIndex < playback.commands.size() && remainingCommands > 0 && std::chrono::steady_clock::now() < deadline) {
			const MRMacroDeferredUiCommand &command = playback.commands[playback.nextIndex];
			++playback.nextIndex;
			--remainingCommands;

			if (command.type == mrducDelay) {
				const int millis = std::max(0, command.a1);
				if (millis > 0) {
					playback.waitingForDelay = true;
					playback.resumeAfter = std::chrono::steady_clock::now() + std::chrono::milliseconds(millis);
				}
				break;
			}
			if (!playback.screenModel.shouldRenderAndProject(command)) {
				++playback.skippedCount;
				continue;
			}
			if (MacroScreenView::render(command)) {
				++playback.appliedCount;
				playback.observedScreenEpoch = mrvmUiScreenMutationEpoch();
				continue;
			}
			++playback.failedCount;
			playback.screenModel.invalidateAfterRenderFailure();
			playback.observedScreenEpoch = mrvmUiScreenMutationEpoch();
			mrLogMessage((std::string("Deferred UI command failed: ") + deferredUiCommandName(command.type)).c_str());
		}
		mrvmUiEndMacroScreenBatch();

		if (playback.nextIndex >= playback.commands.size() && !playback.waitingForDelay) {
			logDeferredMacroUiPlaybackSummary(playback);
			eraseDeferredPlayback(playbackId);
			continue;
		}
		storeDeferredPlaybackProgress(playbackId, playback);
		break;
	}
}
} // namespace

void queueDeferredMacroUiPlayback(std::size_t documentId, const std::string &displayName, const std::vector<MRMacroDeferredUiCommand> &commands) {
	queueDeferredMacroUiPlaybackInternal(documentId, displayName, commands);
}

void pumpDeferredMacroUiPlayback() {
	pumpDeferredMacroUiPlaybackQueue();
}
