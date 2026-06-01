#ifndef MRSTATUSLINE_HPP
#define MRSTATUSLINE_HPP
#define Uses_TStatusLine
#define Uses_TDrawBuffer
#define Uses_TPalette
#include "MRPalette.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include <tvision/tv.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

void mrvmUiInvalidateScreenBase() noexcept;

class MRStatusLine : public TStatusLine {
  public:
	struct FunctionKeyLabel {
		TKey keyCode;
		ushort command;
		std::string text;
	};

	MRStatusLine(const TRect &r, TStatusDef &aDef) : TStatusLine(r, aDef), mRecordingActive(false), mRecordingVisible(false), mShowFunctionKeyLabels(true), mContextFunctionKeysActive(false), mContextFunctionKeyLabels(), mMacroFunctionLabels() {
	}

	virtual TPalette &getPalette() const override {
		static const TColorAttr data[] = {kMrPaletteStatusLine, kMrPaletteStatusLineBold, kMrPaletteStatusLineFunctionDescription, kMrPaletteStatusLineFunctionKey, kMrPaletteStatusLineBold, kMrPaletteStatusLineFunctionKey};
		static TPalette palette(data);
		return palette;
	}

	void setRecordingState(bool active, bool visible) {
		if (mRecordingActive == active && mRecordingVisible == visible) return;
		mRecordingActive = active;
		mRecordingVisible = visible;
		drawView();
	}

	void setShowFunctionKeyLabels(bool enabled) {
		if (mShowFunctionKeyLabels == enabled) return;
		mShowFunctionKeyLabels = enabled;
		drawView();
	}

	void setMacroFunctionLabels(const std::vector<std::string> &labels) {
		if (mMacroFunctionLabels == labels) return;
		mMacroFunctionLabels = labels;
		drawView();
	}

	void setContextFunctionKeyLabels(const std::vector<FunctionKeyLabel> &labels) {
		if (mContextFunctionKeyLabels.size() == labels.size()) {
			bool same = true;
			for (std::size_t i = 0; i < labels.size(); ++i) {
				if (!(mContextFunctionKeyLabels[i].keyCode == labels[i].keyCode) || mContextFunctionKeyLabels[i].command != labels[i].command || mContextFunctionKeyLabels[i].text != labels[i].text) {
					same = false;
					break;
				}
			}
			if (same) return;
		}
		mContextFunctionKeyLabels = labels;
		drawView();
	}

	void setContextFunctionKeysActive(bool active) {
		if (mContextFunctionKeysActive == active) return;
		mContextFunctionKeysActive = active;
		drawView();
	}

	virtual void draw() override {
		if (!mShowFunctionKeyLabels) {
			TDrawBuffer b;
			TColorAttr color = getColor(1);
			b.moveChar(0, ' ', color, size.x);
			writeLine(0, 0, size.x, 1, b);
		} else if (mContextFunctionKeysActive && !mContextFunctionKeyLabels.empty()) {
			drawContextFunctionLabels();
		} else if (!mMacroFunctionLabels.empty()) {
			drawMacroFunctionLabels();
		} else
			TStatusLine::draw();
		if (!mRecordingActive || !mRecordingVisible) {
			mrvmUiInvalidateScreenBase();
			return;
		}

		static const char *kRecText = " REC ";
		const int recLen = strwidth(kRecText);
		const int recX = size.x - recLen - 1;
		TDrawBuffer b;
		TColorAttr color = getColor(1);

		if (recX < 0) return;
		b.moveChar(0, ' ', color, recLen);
		b.moveStr(1, "REC", color);
		writeBuf(recX, 0, recLen, 1, b);
		mrvmUiInvalidateScreenBase();
	}

	virtual void handleEvent(TEvent &event) override {
		if (mContextFunctionKeysActive && !mContextFunctionKeyLabels.empty()) {
			if (event.what == evKeyDown && event.keyDown.keyCode != kbNoKey) {
				const TKey pressed(event.keyDown);
				for (const FunctionKeyLabel &label : mContextFunctionKeyLabels) {
					if (pressed == label.keyCode && commandEnabled(label.command)) {
						event.what = evCommand;
						event.message.command = label.command;
						event.message.infoPtr = nullptr;
						return;
					}
				}
				if (pressed.mods == 0 && isFunctionKey(pressed.code)) return;
			}
			if (event.what == evMouseDown) {
				TPoint mouse = makeLocal(event.mouse.where);
				ushort command = commandForContextFunctionLabelAt(mouse.x, mouse.y);
				if (command != 0 && commandEnabled(command)) {
					event.what = evCommand;
					event.message.command = command;
					event.message.infoPtr = nullptr;
					putEvent(event);
					clearEvent(event);
					drawView();
					return;
				}
			}
		}
		TStatusLine::handleEvent(event);
	}

  private:
	std::vector<int> contextVisibleLabelIndexes() const {
		std::vector<int> indexes;
		const int count = static_cast<int>(mContextFunctionKeyLabels.size());

		if (size.x >= 120 || count <= 8) {
			for (int i = 0; i < count; ++i)
				indexes.push_back(i);
			return indexes;
		}
		if (size.x >= 100) {
			static constexpr std::array<int, 10> preferred = {0, 1, 2, 4, 5, 6, 7, 9, 10, 11};
			for (int index : preferred)
				if (index < count) indexes.push_back(index);
			return indexes;
		}
		static constexpr std::array<int, 8> preferred = {0, 1, 2, 4, 5, 6, 9, 11};
		for (int index : preferred)
			if (index < count) indexes.push_back(index);
		return indexes;
	}

	static bool isFunctionKey(ushort keyCode) noexcept {
		switch (keyCode) {
			case kbF1:
			case kbF2:
			case kbF3:
			case kbF4:
			case kbF5:
			case kbF6:
			case kbF7:
			case kbF8:
			case kbF9:
			case kbF10:
			case kbF11:
			case kbF12:
				return true;
			default:
				return false;
		}
	}

	void drawContextFunctionLabels() {
		TDrawBuffer buffer;
		TColorAttr backgroundColor = getColor(1);
		TAttrPair labelColor = getColor(0x0403);
		std::vector<int> indexes = contextVisibleLabelIndexes();
		const int segmentCount = static_cast<int>(indexes.size());
		const int segmentWidth = segmentCount > 0 ? std::max(1, size.x / segmentCount) : size.x;

		buffer.moveChar(0, ' ', backgroundColor, size.x);
		for (int segment = 0; segment < segmentCount; ++segment) {
			int x = segment * segmentWidth;
			int width = segment == segmentCount - 1 ? size.x - x : segmentWidth;
			std::string text = mContextFunctionKeyLabels[static_cast<std::size_t>(indexes[static_cast<std::size_t>(segment)])].text;

			if (width <= 0) continue;
			buffer.moveCStr(static_cast<ushort>(x), text.c_str(), labelColor, static_cast<ushort>(width));
		}
		writeLine(0, 0, size.x, 1, buffer);
	}

	ushort commandForContextFunctionLabelAt(short x, short y) const {
		std::vector<int> indexes = contextVisibleLabelIndexes();
		const int segmentCount = static_cast<int>(indexes.size());
		const int segmentWidth = segmentCount > 0 ? std::max(1, size.x / segmentCount) : size.x;
		int segment = 0;

		if (y != 0 || x < 0 || segmentCount <= 0) return 0;
		segment = std::min(segmentCount - 1, static_cast<int>(x) / segmentWidth);
		if (segment < 0 || segment >= segmentCount) return 0;
		return mContextFunctionKeyLabels[static_cast<std::size_t>(indexes[static_cast<std::size_t>(segment)])].command;
	}

	void drawMacroFunctionLabels() {
		static constexpr std::array<int, 12> visibleKeyNumbers = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 41, 42};
		TDrawBuffer buffer;
		TColorAttr backgroundColor = getColor(1);
		TAttrPair labelColor = getColor(0x0403);
		const int segmentCount = size.x >= 120 ? 12 : (size.x >= 100 ? 10 : 8);
		const int segmentWidth = std::max(1, size.x / std::max(1, segmentCount));

		buffer.moveChar(0, ' ', backgroundColor, size.x);
		for (int segment = 0; segment < segmentCount; ++segment) {
			const int keyNumber = visibleKeyNumbers[static_cast<std::size_t>(segment)];
			std::string text;
			int x = segment * segmentWidth;
			int width = segment == segmentCount - 1 ? size.x - x : segmentWidth;

			if (keyNumber > 0 && keyNumber < static_cast<int>(mMacroFunctionLabels.size()) && !mMacroFunctionLabels[static_cast<std::size_t>(keyNumber)].empty()) {
				text = "~F";
				text += std::to_string(keyNumber <= 10 ? keyNumber : keyNumber - 30);
				text += "~ ";
				text += mMacroFunctionLabels[static_cast<std::size_t>(keyNumber)];
			}
			if (width <= 0) continue;
			buffer.moveCStr(static_cast<ushort>(x), text.c_str(), labelColor, static_cast<ushort>(width));
		}
		writeLine(0, 0, size.x, 1, buffer);
	}

	bool mRecordingActive;
	bool mRecordingVisible;
	bool mShowFunctionKeyLabels;
	bool mContextFunctionKeysActive;
	std::vector<FunctionKeyLabel> mContextFunctionKeyLabels;
	std::vector<std::string> mMacroFunctionLabels;
};
#endif
