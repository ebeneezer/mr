#ifndef MRMENUBAR_HPP
#define MRMENUBAR_HPP
#define Uses_TMenuBar
#define Uses_TDrawBuffer
#include "MRPalette.hpp"
#include <tvision/tv.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

class MRMenuBar : public TMenuBar {
  public:
	enum class MarqueeKind : unsigned char {
		Info,
		Success,
		Warning,
		Error,
		Hero
	};

	MRMenuBar(const TRect &r, TSubMenu &aMenu);
	~MRMenuBar() override;

	void handleEvent(TEvent &event) override;
	virtual void draw() override;
	void tickMarquee();
	void setPersistentBlocksMenuState(bool enabled);
	bool registerRuntimeMenuItem(const std::string &menuTitle, const std::string &itemTitle,
	                             const std::string &macroSpec, const std::string &ownerSpec,
	                             std::string *errorMessage = nullptr);
	bool refreshRuntimeMenus(std::string *errorMessage = nullptr);
	bool removeRuntimeMenuItem(const std::string &menuTitle, const std::string &itemTitle,
	                           const std::string &ownerSpec,
	                           std::string *errorMessage = nullptr);
	bool removeRuntimeNodesOwnedByMacroSpec(const std::string &ownerSpec,
	                                        std::string *errorMessage = nullptr);
	bool removeRuntimeNodesOwnedByFile(const std::string &fileSpec, std::string *errorMessage = nullptr);
	bool handleRuntimeCommand(ushort command);

	void setRightStatus(const std::string &status) {
		if (mRightStatus != status) {
			mRightStatus = status;
			drawView();
		}
	}

	const std::string &rightStatus() const noexcept {
		return mRightStatus;
	}

	void setAutoMarqueeStatus(const std::string &status, MarqueeKind kind = MarqueeKind::Info) {
		if (mAutoMarqueeStatus != status || mAutoMarqueeKind != kind) {
			mAutoMarqueeStatus = status;
			mAutoMarqueeKind = kind;
			drawView();
		}
	}

	void setManualMarqueeStatus(const std::string &status) {
		setManualMarqueeStatus(status, MarqueeKind::Info);
	}

	void setManualMarqueeStatus(const std::string &status, MarqueeKind kind) {
		if (mManualMarqueeStatus != status || mManualMarqueeKind != kind) {
			mManualMarqueeStatus = status;
		mManualMarqueeKind = kind;
			drawView();
		}
	}

	const std::string &autoMarqueeStatus() const noexcept {
		return mAutoMarqueeStatus;
	}
	const std::string &manualMarqueeStatus() const noexcept {
		return mManualMarqueeStatus;
	}

 private:
	enum class RuntimeMenuNodeKind : unsigned char {
		Item,
		Separator
	};

	struct RuntimeMenuNode {
		RuntimeMenuNodeKind kind = RuntimeMenuNodeKind::Item;
		std::string menuKey;
		std::string menuTitle;
		std::string itemKey;
		std::string itemTitle;
		std::string ownerSpec;
		std::string macroSpec;
		ushort command = 0;
		std::uint32_t order = 0;
	};

	static int marqueeVisibleSpanFor(const std::string &text, int laneWidth) noexcept {
		const int textLen = static_cast<int>(text.size());
		if (textLen <= 0 || laneWidth <= 0)
			return 0;
		return std::min(textLen, laneWidth);
	}
	static constexpr std::chrono::milliseconds marqueeScrollStepInterval() {
		return std::chrono::milliseconds(180);
	}
	static constexpr std::chrono::milliseconds marqueeIntroDuration() {
		return std::chrono::milliseconds(1000);
	}
	static constexpr std::chrono::milliseconds marqueeScrollStartDelay() {
		return std::chrono::milliseconds(3000);
	}
	void resetMarqueeState() {
		mMarqueeOffset = 0;
		mMarqueeDirection = -1;
		mMarqueeLaneWidth = 0;
		mMarqueeActiveText.clear();
		mMarqueeActiveKind = MarqueeKind::Info;
		mMarqueeHasPending = false;
		mMarqueePendingText.clear();
		mMarqueePendingKind = MarqueeKind::Info;
		mMarqueeScrollNextAt = std::chrono::steady_clock::time_point::min();
		mMarqueeIntroActive = false;
		mMarqueeIntroShift = 0;
		mMarqueeIntroStartShift = 0;
		mMarqueeIntroStartedAt = std::chrono::steady_clock::time_point::min();
		mMarqueeOutroActive = false;
		mMarqueeOutroShift = 0;
		mMarqueeOutroStartShift = 0;
		mMarqueeOutroStartedAt = std::chrono::steady_clock::time_point::min();
	}
	static std::string canonicalMenuToken(const std::string &value);
	static std::string trimAscii(std::string value);
	static bool ownerSpecMatchesFile(const std::string &ownerSpec, const std::string &fileSpec) noexcept;
	bool allocateRuntimeCommand(ushort &command, std::string *errorMessage);
	bool rebuildRuntimeMenu();
	int findRuntimeNodeIndex(const std::string &menuKey, const std::string &itemKey,
	                         const std::string &ownerSpec) const noexcept;

	TMenu *mBaseMenu = nullptr;
	std::vector<RuntimeMenuNode> mRuntimeNodes;
	std::uint32_t mNextRuntimeOrder = 0;
	ushort mNextRuntimeCommand = 0x7400;
	std::string mRightStatus;
	std::string mAutoMarqueeStatus;
	std::string mManualMarqueeStatus;
	MarqueeKind mManualMarqueeKind = MarqueeKind::Info;
	MarqueeKind mAutoMarqueeKind;
	int mMarqueeOffset = 0;
	int mMarqueeDirection = -1;
	int mMarqueeLaneWidth = 0;
	std::string mMarqueeActiveText;
	MarqueeKind mMarqueeActiveKind = MarqueeKind::Info;
	bool mMarqueeHasPending = false;
	std::string mMarqueePendingText;
	MarqueeKind mMarqueePendingKind = MarqueeKind::Info;
	std::chrono::steady_clock::time_point mMarqueeScrollNextAt = std::chrono::steady_clock::time_point::min();
	bool mMarqueeIntroActive = false;
	int mMarqueeIntroShift = 0;
	int mMarqueeIntroStartShift = 0;
	std::chrono::steady_clock::time_point mMarqueeIntroStartedAt = std::chrono::steady_clock::time_point::min();
	bool mMarqueeOutroActive = false;
	int mMarqueeOutroShift = 0;
	int mMarqueeOutroStartShift = 0;
	std::chrono::steady_clock::time_point mMarqueeOutroStartedAt = std::chrono::steady_clock::time_point::min();
};

#endif
