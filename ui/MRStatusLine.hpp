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
#include <chrono>
#include <cstdint>
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

	MRStatusLine(const TRect &r, TStatusDef &aDef) : TStatusLine(r, aDef), mRecordingActive(false), mRecordingVisible(false), mShowFunctionKeyLabels(true), mContextFunctionKeysActive(false), mContextHintLabelsActive(false), mContextFunctionKeyLabels(), mContextFunctionLabelTransitions(), mFunctionKeyLabelRandomState(static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^ 0x4D52464Bu), mContextHintLabels(), mMacroFunctionLabels() {
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
		bool same = mContextFunctionKeyLabels.size() == labels.size();

		if (same)
			for (std::size_t i = 0; i < labels.size(); ++i)
				if (!(mContextFunctionKeyLabels[i].keyCode == labels[i].keyCode) || mContextFunctionKeyLabels[i].command != labels[i].command || mContextFunctionKeyLabels[i].text != labels[i].text) {
					same = false;
					break;
				}
		if (same) return;
		if (mContextFunctionKeyLabels.size() != labels.size() || mContextFunctionLabelTransitions.size() != labels.size()) {
			mContextFunctionKeyLabels = labels;
			mContextFunctionLabelTransitions.assign(labels.size(), FunctionKeyLabelTransition());
			drawView();
			return;
		}

		const auto now = std::chrono::steady_clock::now();
		for (std::size_t i = 0; i < labels.size(); ++i) {
			FunctionKeyLabelTransition &transition = mContextFunctionLabelTransitions[i];
			const std::string &oldText = mContextFunctionKeyLabels[i].text;
			const std::string &newText = labels[i].text;

			if (oldText == newText) continue;
			if (transition.phase == FunctionKeyLabelTransitionPhase::Outgoing) {
				if (transition.outgoingText == newText) {
					transition.phase = FunctionKeyLabelTransitionPhase::Stable;
					transition.outgoingText.clear();
					transition.startedAt = std::chrono::steady_clock::time_point::min();
				}
				continue;
			}
			transition.outgoingText = oldText;
			transition.phase = oldText.empty() ? FunctionKeyLabelTransitionPhase::Incoming : FunctionKeyLabelTransitionPhase::Outgoing;
			transition.startedAt = now + nextFunctionKeyLabelStartDelay();
		}
		mContextFunctionKeyLabels = labels;
		drawView();
	}

	void tickFunctionKeyLabelTransitions() {
		const auto now = std::chrono::steady_clock::now();

		for (std::size_t i = 0; i < mContextFunctionLabelTransitions.size(); ++i) {
			FunctionKeyLabelTransition &transition = mContextFunctionLabelTransitions[i];

			if (transition.phase == FunctionKeyLabelTransitionPhase::Stable) continue;
			if (transition.startedAt == std::chrono::steady_clock::time_point::min()) {
				transition.phase = FunctionKeyLabelTransitionPhase::Stable;
				transition.outgoingText.clear();
				continue;
			}

			auto elapsed = now - transition.startedAt;
			if (transition.phase == FunctionKeyLabelTransitionPhase::Outgoing && elapsed >= functionKeyLabelTransitionDuration()) {
				transition.outgoingText.clear();
				if (i >= mContextFunctionKeyLabels.size() || mContextFunctionKeyLabels[i].text.empty()) {
					transition.phase = FunctionKeyLabelTransitionPhase::Stable;
					transition.startedAt = std::chrono::steady_clock::time_point::min();
				} else {
					transition.phase = FunctionKeyLabelTransitionPhase::EntryPause;
					transition.startedAt += functionKeyLabelTransitionDuration();
					elapsed = now - transition.startedAt;
				}
			}
			if (transition.phase == FunctionKeyLabelTransitionPhase::EntryPause && elapsed >= functionKeyLabelEntryPauseDuration()) {
				transition.phase = FunctionKeyLabelTransitionPhase::Incoming;
				transition.startedAt += functionKeyLabelEntryPauseDuration();
				elapsed = now - transition.startedAt;
			}
			if (transition.phase == FunctionKeyLabelTransitionPhase::Incoming && elapsed >= functionKeyLabelTransitionDuration()) {
				transition.phase = FunctionKeyLabelTransitionPhase::Stable;
				transition.startedAt = std::chrono::steady_clock::time_point::min();
			}
		}
		if (!mShowFunctionKeyLabels || !mContextFunctionKeysActive || mContextHintLabelsActive || (state & sfVisible) == 0) return;

		std::vector<int> indexes = contextFunctionVisibleLabelIndexes();
		const int segmentCount = static_cast<int>(indexes.size());
		const int segmentWidth = segmentCount > 0 ? std::max(1, size.x / segmentCount) : size.x;

		for (int segment = 0; segment < segmentCount; ++segment) {
			const std::size_t labelIndex = static_cast<std::size_t>(indexes[static_cast<std::size_t>(segment)]);
			const FunctionKeyLabelTransition &transition = mContextFunctionLabelTransitions[labelIndex];
			const int width = segment == segmentCount - 1 ? size.x - segment * segmentWidth : segmentWidth;
			const int shift = functionKeyLabelTransitionShift(transition, width, now);

			if (transition.drawnPhase != transition.phase || transition.drawnShift != shift) {
				drawView();
				break;
			}
		}
	}

	void setContextFunctionKeysActive(bool active) {
		if (mContextFunctionKeysActive == active) return;
		mContextFunctionKeysActive = active;
		drawView();
	}

	void setContextHintLabels(const std::vector<std::string> &labels) {
		if (mContextHintLabels == labels) return;
		mContextHintLabels = labels;
		drawView();
	}

	void setContextHintLabelsActive(bool active) {
		if (mContextHintLabelsActive == active) return;
		mContextHintLabelsActive = active;
		drawView();
	}

	virtual void draw() override {
		if (!mShowFunctionKeyLabels) {
			TDrawBuffer b;
			TColorAttr color = getColor(1);
			b.moveChar(0, ' ', color, size.x);
			writeLine(0, 0, size.x, 1, b);
		} else if (mContextHintLabelsActive && !mContextHintLabels.empty()) {
			drawContextHintLabels();
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
	enum class FunctionKeyLabelTransitionPhase : unsigned char {
		Stable,
		Outgoing,
		EntryPause,
		Incoming
	};

	struct FunctionKeyLabelTransition {
		FunctionKeyLabelTransitionPhase phase = FunctionKeyLabelTransitionPhase::Stable;
		FunctionKeyLabelTransitionPhase drawnPhase = FunctionKeyLabelTransitionPhase::Stable;
		std::string outgoingText;
		std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::time_point::min();
		int drawnShift = 0;
	};

	static constexpr std::chrono::milliseconds functionKeyLabelTransitionDuration() {
		return std::chrono::milliseconds(262);
	}
	static constexpr std::chrono::milliseconds functionKeyLabelEntryPauseDuration() {
		return std::chrono::milliseconds(78);
	}

	std::chrono::milliseconds nextFunctionKeyLabelStartDelay() {
		mFunctionKeyLabelRandomState = mFunctionKeyLabelRandomState * 1664525u + 1013904223u;
		return std::chrono::milliseconds(mFunctionKeyLabelRandomState % 184u);
	}

	static int functionKeyLabelTransitionShift(const FunctionKeyLabelTransition &transition, int width, std::chrono::steady_clock::time_point now) {
		const long long durationMs = functionKeyLabelTransitionDuration().count();
		long long elapsedMs = 0;

		if (width <= 0 || transition.phase == FunctionKeyLabelTransitionPhase::Stable || transition.startedAt == std::chrono::steady_clock::time_point::min()) return 0;
		elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - transition.startedAt).count();
		if (elapsedMs < 0) elapsedMs = 0;
		if (elapsedMs > durationMs) elapsedMs = durationMs;
		if (transition.phase == FunctionKeyLabelTransitionPhase::Outgoing) return static_cast<int>((static_cast<long long>(width) * elapsedMs + durationMs - 1) / durationMs);
		if (transition.phase == FunctionKeyLabelTransitionPhase::EntryPause) return width;

		const long long remainingMs = durationMs - elapsedMs;
		return static_cast<int>((static_cast<long long>(width) * remainingMs + durationMs - 1) / durationMs);
	}

	std::vector<int> contextVisibleLabelIndexes(int count) const {
		std::vector<int> indexes;

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

	std::vector<int> contextFunctionVisibleLabelIndexes() const {
		return contextVisibleLabelIndexes(static_cast<int>(mContextFunctionKeyLabels.size()));
	}

	std::vector<int> contextHintVisibleLabelIndexes() const {
		return contextVisibleLabelIndexes(static_cast<int>(mContextHintLabels.size()));
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
		std::vector<int> indexes = contextFunctionVisibleLabelIndexes();
		const int segmentCount = static_cast<int>(indexes.size());
		const int segmentWidth = segmentCount > 0 ? std::max(1, size.x / segmentCount) : size.x;
		const auto now = std::chrono::steady_clock::now();

		buffer.moveChar(0, ' ', backgroundColor, size.x);
		for (int segment = 0; segment < segmentCount; ++segment) {
			const std::size_t labelIndex = static_cast<std::size_t>(indexes[static_cast<std::size_t>(segment)]);
			FunctionKeyLabelTransition &transition = mContextFunctionLabelTransitions[labelIndex];
			int x = segment * segmentWidth;
			int width = segment == segmentCount - 1 ? size.x - x : segmentWidth;
			const std::string *text = &mContextFunctionKeyLabels[labelIndex].text;
			int shift = functionKeyLabelTransitionShift(transition, width, now);

			if (width <= 0) continue;
			if (transition.phase == FunctionKeyLabelTransitionPhase::Outgoing) text = &transition.outgoingText;
			if (shift < width) buffer.moveCStr(static_cast<ushort>(x + shift), text->c_str(), labelColor, static_cast<ushort>(width - shift));
			transition.drawnPhase = transition.phase;
			transition.drawnShift = shift;
		}
		writeLine(0, 0, size.x, 1, buffer);
	}

	void drawContextHintLabels() {
		TDrawBuffer buffer;
		TColorAttr backgroundColor = getColor(1);
		TAttrPair labelColor = getColor(0x0403);
		std::vector<int> indexes = contextHintVisibleLabelIndexes();
		const int segmentCount = static_cast<int>(indexes.size());
		const int segmentWidth = segmentCount > 0 ? std::max(1, size.x / segmentCount) : size.x;

		buffer.moveChar(0, ' ', backgroundColor, size.x);
		for (int segment = 0; segment < segmentCount; ++segment) {
			int x = segment * segmentWidth;
			int width = segment == segmentCount - 1 ? size.x - x : segmentWidth;
			std::string text = mContextHintLabels[static_cast<std::size_t>(indexes[static_cast<std::size_t>(segment)])];

			if (width <= 0) continue;
			buffer.moveCStr(static_cast<ushort>(x), text.c_str(), labelColor, static_cast<ushort>(width));
		}
		writeLine(0, 0, size.x, 1, buffer);
	}

	ushort commandForContextFunctionLabelAt(short x, short y) const {
		std::vector<int> indexes = contextFunctionVisibleLabelIndexes();
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
	bool mContextHintLabelsActive;
	std::vector<FunctionKeyLabel> mContextFunctionKeyLabels;
	std::vector<FunctionKeyLabelTransition> mContextFunctionLabelTransitions;
	std::uint32_t mFunctionKeyLabelRandomState;
	std::vector<std::string> mContextHintLabels;
	std::vector<std::string> mMacroFunctionLabels;
};
#endif
