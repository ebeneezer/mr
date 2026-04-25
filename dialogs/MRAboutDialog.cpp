#include "../app/utils/MRStringUtils.hpp"
#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TRect
#define Uses_TDialog
#define Uses_TButton
#define Uses_TStaticText
#define Uses_TView
#define Uses_TObject
#include <tvision/tv.h>

#include "MRAboutDialog.hpp"
#include "MRSetupDialogCommon.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "../app/MRVersion.hpp"
#include "../app/MRAboutQuotes.generated.hpp"

namespace {


constexpr uint kAnimationTickMs = 100;
constexpr uint kQuoteRotateMs = 10000;
constexpr uint kDoneLongPressMs = 700;
constexpr ushort cmAboutDone = 0x6A10;

TRect centeredRect(int width, int height) {
	return mr::dialogs::centeredDialogRect(width, height);
}

void insertCenteredStaticLine(TDialog *dialog, int width, int y, const std::string &text) {
	int x = std::max(2, (width - strwidth(text.c_str())) / 2);
	dialog->insert(new TStaticText(TRect(x, y, width - 2, y + 1), text.c_str()));
}

std::vector<std::string> wrapQuoteText(const std::string &text, int maxWidth) {
	std::vector<std::string> lines;
	std::istringstream in(text);
	std::string word;
	std::string current;

	while (in >> word) {
		std::string candidate = current.empty() ? word : current + " " + word;
		if (strwidth(candidate.c_str()) <= maxWidth)
			current = candidate;
		else {
			if (!current.empty())
				lines.push_back(current);
			current = word;
		}
	}
	if (!current.empty())
		lines.push_back(current);
	if (lines.empty())
		lines.push_back(std::string());
	return lines;
}

bool consumeQuoteToken(const std::string &text, std::size_t &pos) {
	static const char *const kQuoteTokens[] = {"\"", "“", "”", "„"};
	for (const char *token : kQuoteTokens) {
		const std::string marker(token);
		if (text.compare(pos, marker.size(), marker) == 0) {
			pos += marker.size();
			return true;
		}
	}
	return false;
}

std::string normalizeSpaces(const std::string &value) {
	std::ostringstream out;
	bool pendingSpace = false;
	bool hasOutput = false;

	for (char i : value) {
		unsigned char ch = static_cast<unsigned char>(i);
		if (std::isspace(ch) != 0) {
			pendingSpace = true;
			continue;
		}
		if (pendingSpace && hasOutput)
			out << ' ';
		out << i;
		hasOutput = true;
		pendingSpace = false;
	}
	return trimAscii(out.str());
}

struct ParsedQuoteParts {
	std::string openingQuote;
	std::string closingQuote;
	std::string quoteText;
	std::string authorText;
};

ParsedQuoteParts splitQuoteAndAuthor(const std::string &raw) {
	ParsedQuoteParts out;
	bool insideQuote = false;
	std::size_t i = 0;
	std::string lastQuoteToken;

	while (i < raw.size()) {
		std::size_t quotePos = i;
		if (consumeQuoteToken(raw, i)) {
			lastQuoteToken = raw.substr(quotePos, i - quotePos);
			if (!insideQuote && out.openingQuote.empty())
				out.openingQuote = lastQuoteToken;
			else if (insideQuote)
				out.closingQuote = lastQuoteToken;
			insideQuote = !insideQuote;
			continue;
		}
		if (insideQuote)
			out.quoteText.push_back(raw[i]);
		else
			out.authorText.push_back(raw[i]);
		++i;
	}

	out.quoteText = normalizeSpaces(out.quoteText);
	out.authorText = normalizeSpaces(out.authorText);

	if (out.quoteText.empty()) {
		out.quoteText = normalizeSpaces(raw);
		out.authorText.clear();
	}
	if (out.openingQuote.empty())
		out.openingQuote = "\"";
	if (out.closingQuote.empty())
		out.closingQuote = out.openingQuote;

	return out;
}

std::vector<std::string> buildQuoteDisplayLines(const std::string &raw, int maxWidth) {
	ParsedQuoteParts parts = splitQuoteAndAuthor(raw);
	std::string displayQuote = parts.quoteText;
	if (!displayQuote.empty())
		displayQuote = parts.openingQuote + displayQuote + parts.closingQuote;
	std::vector<std::string> lines = wrapQuoteText(displayQuote, maxWidth);

	if (!parts.authorText.empty()) {
		lines.push_back(parts.authorText);
	}
	return lines;
}

class MRAboutQuoteBox : public TView {
  public:
	MRAboutQuoteBox(const TRect &bounds) noexcept
	    : TView(bounds),  mAnimationFrame(0), mAnimationFramesTotal(22),
	      mAnimating(false), mScrambleSeed(0x00C0DA42u) {
		options |= ofBuffered;
	}

	void setQuoteImmediate(const std::string &quote) {
		mCurrentLines = buildQuoteDisplayLines(quote, std::max(8, size.x - 4));
		mTargetLines.clear();
		mAnimationFrame = 0;
		mAnimating = false;
		drawView();
	}

	void beginQuoteTransition(const std::string &quote) {
		mTargetLines = buildQuoteDisplayLines(quote, std::max(8, size.x - 4));
		mAnimationFrame = 0;
		mAnimating = true;
		drawView();
	}

	bool tickTransition() {
		if (!mAnimating)
			return false;
		++mAnimationFrame;
		if (mAnimationFrame >= mAnimationFramesTotal) {
			mCurrentLines = mTargetLines;
			mTargetLines.clear();
			mAnimationFrame = 0;
			mAnimating = false;
		}
		drawView();
		return true;
	}

	bool animating() const noexcept {
		return mAnimating;
	}

	void draw() override {
		TDrawBuffer b;
		TColorAttr normal = getColor(1);
		TColorAttr border = getColor(2);
		char topLeft = '\xDA';
		char topRight = '\xBF';
		char bottomLeft = '\xC0';
		char bottomRight = '\xD9';
		char horiz = '\xC4';
		char vert = '\xB3';

		for (int y = 0; y < size.y; ++y) {
			b.moveChar(0, ' ', normal, size.x);
			writeBuf(0, y, size.x, 1, b);
		}

		if (size.x < 2 || size.y < 2)
			return;

		b.moveChar(0, horiz, border, size.x);
		b.putChar(0, topLeft);
		b.putChar(size.x - 1, topRight);
		writeBuf(0, 0, size.x, 1, b);

		for (int y = 1; y < size.y - 1; ++y) {
			b.moveChar(0, ' ', normal, size.x);
			b.putChar(0, vert);
			b.putChar(size.x - 1, vert);
			drawContentRow(b, y, normal);
			writeBuf(0, y, size.x, 1, b);
		}

		b.moveChar(0, horiz, border, size.x);
		b.putChar(0, bottomLeft);
		b.putChar(size.x - 1, bottomRight);
		writeBuf(0, size.y - 1, size.x, 1, b);
	}

	TPalette &getPalette() const override {
		static TPalette palette("\x06\x01", 2);
		return palette;
	}

  private:
	struct SweepState {
		int tail;
		int front;
		int revealUntil;

		SweepState() noexcept : tail(0), front(0), revealUntil(-1) {
		}
	};

	SweepState sweepStateForLength(std::size_t totalChars) const noexcept {
		SweepState s;
		int travel;
		s.tail = std::max<int>(6, static_cast<int>(totalChars) / 4);
		travel = static_cast<int>(totalChars) + s.tail;
		s.front = travel == 0 ? 0 : (travel * mAnimationFrame) / std::max(1, mAnimationFramesTotal);
		s.revealUntil = s.front - s.tail;
		return s;
	}

	static TColorAttr scrambleColor(std::uint32_t &seed, TColorAttr baseColor) noexcept {
		static const unsigned char kFgColors[] = {
		    0x0F, // bright white
		    0x0E, // yellow
		    0x0D, // bright magenta
		    0x0C, // bright red
		    0x0B, // bright cyan
		    0x0A, // bright green
		    0x09  // bright blue
		};
		unsigned char fg;
		unsigned char bg;
		seed = seed * 1664525u + 1013904223u;
		fg = kFgColors[seed % (sizeof(kFgColors) / sizeof(kFgColors[0]))];
		bg = static_cast<unsigned char>((baseColor >> 4u) & 0x07u);
		// Preserve DOS-style 4-bit fg + 3-bit bg mapping and keep background stable.
		return static_cast<TColorAttr>(((bg & 0x07u) << 4u) | (fg & 0x0Fu));
	}

	static char scrambleGlyph(std::uint32_t value) noexcept {
		static const char glyphs[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?@#$%&*+=<>/\\[]{}";
		return glyphs[value % (sizeof(glyphs) - 1)];
	}

	int centeredBaseRow(const std::vector<std::string> &lines) const noexcept {
		int innerHeight = std::max(1, size.y - 2);
		int freeRows = std::max(0, innerHeight - static_cast<int>(lines.size()));
		if (lines.size() == 1)
			return 1 + innerHeight / 2;
		return 1 + freeRows / 2;
	}

	std::string scrambledLine(const std::string &target, std::size_t lineIndex) const {
		if (!mAnimating)
			return target;

		std::string out = target;
		SweepState sweep = sweepStateForLength(target.size());
		std::uint32_t localSeed =
		    mScrambleSeed ^ static_cast<std::uint32_t>(lineIndex * 131u) ^
		    static_cast<std::uint32_t>(mAnimationFrame * 977u);
		for (std::size_t i = 0; i < out.size(); ++i) {
			unsigned char c = static_cast<unsigned char>(out[i]);
			if (c == ' ')
				continue;
			if (static_cast<int>(i) <= sweep.revealUntil)
				continue;
			localSeed = localSeed * 1664525u + 1013904223u;
			out[i] = scrambleGlyph(localSeed);
		}
		if (sweep.front >= 0 && sweep.front < static_cast<int>(out.size()) &&
		    out[static_cast<std::size_t>(sweep.front)] != ' ')
			out[static_cast<std::size_t>(sweep.front)] = '_';
		return out;
	}

	void drawContentRow(TDrawBuffer &b, int row, TColorAttr color) const {
		if (mAnimating) {
			int baseRow = centeredBaseRow(mTargetLines);
			int lineIndex = row - baseRow;
			if (0 <= lineIndex && lineIndex < static_cast<int>(mTargetLines.size())) {
				const std::string &target = mTargetLines[static_cast<std::size_t>(lineIndex)];
				std::string line = scrambledLine(target,
				                                 static_cast<std::size_t>(lineIndex));
				SweepState sweep = sweepStateForLength(target.size());
				std::uint32_t colorSeed = mScrambleSeed ^
				                          static_cast<std::uint32_t>(mAnimationFrame * 4099u) ^
				                          static_cast<std::uint32_t>(lineIndex * 97u);
				int innerWidth = std::max(0, size.x - 4);
				int textWidth = std::max(0, strwidth(line.c_str()));
				int startX = 2 + std::max(0, (innerWidth - textWidth) / 2);
				int limit = std::min<int>(static_cast<int>(line.size()), innerWidth);
				for (int i = 0; i < limit; ++i) {
					unsigned char ch = static_cast<unsigned char>(line[static_cast<std::size_t>(i)]);
					TColorAttr attr = color;
					if (ch != ' ') {
						if (i <= sweep.revealUntil)
							attr = color;
						else
							attr = scrambleColor(colorSeed, color);
					}
					b.putChar(static_cast<ushort>(startX + i), ch);
					b.putAttribute(static_cast<ushort>(startX + i), attr);
				}
			}
		} else {
			int baseRow = centeredBaseRow(mCurrentLines);
			int lineIndex = row - baseRow;
			if (0 <= lineIndex && lineIndex < static_cast<int>(mCurrentLines.size())) {
				const std::string &line = mCurrentLines[static_cast<std::size_t>(lineIndex)];
				int innerWidth = std::max(0, size.x - 4);
				int textWidth = std::max(0, strwidth(line.c_str()));
				int startX = 2 + std::max(0, (innerWidth - textWidth) / 2);
				b.moveStr(static_cast<ushort>(startX), line.c_str(), color, innerWidth);
			}
		}
	}

	std::vector<std::string> mCurrentLines;
	std::vector<std::string> mTargetLines;
	int mAnimationFrame;
	int mAnimationFramesTotal;
	bool mAnimating;
	mutable std::uint32_t mScrambleSeed;
};

class MRAboutDialog : public MRDialogFoundation {
  public:
	MRAboutDialog() noexcept
	    : TWindowInit(&TDialog::initFrame),
	      MRDialogFoundation(centeredRect(76, 16), "ABOUT", 76, 16), mQuoteBox(nullptr),
	      mDoneButton(nullptr), mQuoteIndex(0), mQuoteRandomState(0), mQuoteModeEnabled(false), mRotationTimer(nullptr),
	       mRearmRotationAfterAnimation(false), mDonePressTracking(false),
	      mDoneLongPressTriggered(false), mSuppressNextDoneCommand(false) {
		eventMask |= evBroadcast;
		insertCenteredStaticLine(this, size.x, 2,
		                         std::string("Multi-Edit Revisited ") + mrDisplayVersion());
		insertCenteredStaticLine(this, size.x, 3, "Dr. Michael \"iDoc\" Raus & Codex AI");

		mQuoteBox = new MRAboutQuoteBox(TRect(4, 5, size.x - 4, 13));
		insert(mQuoteBox);
		mQuoteBox->setQuoteImmediate(kAboutQuotes[0]);
		mQuoteSeen.assign(kAboutQuoteCount, 0);
		if (!mQuoteSeen.empty())
			mQuoteSeen[0] = 1;
		mQuoteRandomState = static_cast<std::uint32_t>(
		    std::chrono::steady_clock::now().time_since_epoch().count()) ^
		                   0xA39F1D5Bu;

		mDoneButton =
		    new TButton(TRect(size.x / 2 - 6, 13, size.x / 2 + 6, 15), "~P~ress", cmAboutDone, bfDefault);
		insert(mDoneButton);
	}

	~MRAboutDialog() override {
		killTimer(mRotationTimer);
	}

	void awaken() override {
		TDialog::awaken();
		armRotationTimer();
	}

	void draw() override {
		armRotationTimer();
		TDialog::draw();
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evMouseDown || event.what == evMouseUp)
			trackDonePress(event);
		if (event.what == evCommand && event.message.command == cmAboutDone) {
			if (mSuppressNextDoneCommand) {
				mSuppressNextDoneCommand = false;
				clearEvent(event);
				return;
			}
			endModal(cmOK);
			clearEvent(event);
			return;
		}
		MRDialogFoundation::handleEvent(event);
		if (event.what == evBroadcast && event.message.command == cmTimerExpired &&
		    event.message.infoPtr == mRotationTimer) {
			tickQuoteRotation();
			clearEvent(event);
		}
	}

  private:
	void armRotationTimer() {
		if (mRotationTimer == nullptr && owner != nullptr)
			mRotationTimer = setTimer(kAnimationTickMs, kAnimationTickMs);
	}

	void trackDonePress(const TEvent &event) {
		if (mDoneButton == nullptr)
			return;
		TPoint local = makeLocal(event.mouse.where);
		bool insideDone = mDoneButton->getBounds().contains(local);
		if (event.what == evMouseDown) {
			if (insideDone) {
				mDonePressTracking = true;
				mDoneLongPressTriggered = false;
				mDonePressStartedAt = std::chrono::steady_clock::now();
			}
			return;
		}
		if (mDonePressTracking) {
			if (mDoneLongPressTriggered)
				mSuppressNextDoneCommand = true;
			mDonePressTracking = false;
		}
	}

	void enableQuoteModeFromLongPress() {
		if (mQuoteModeEnabled || mQuoteBox == nullptr || kAboutQuoteCount <= 1)
			return;
		mQuoteModeEnabled = true;
		mQuoteIndex = pickNextQuoteIndex();
		mQuoteBox->beginQuoteTransition(kAboutQuotes[mQuoteIndex]);
		mRearmRotationAfterAnimation = true;
	}

	std::size_t pickNextQuoteIndex() {
		if (kAboutQuoteCount <= 1)
			return 0;
		if (mQuoteBagCursor >= mQuoteBag.size())
			rebuildQuoteBag();
		if (mQuoteBagCursor >= mQuoteBag.size())
			return (mQuoteIndex + 1) % kAboutQuoteCount;
		std::size_t picked = mQuoteBag[mQuoteBagCursor++];
		if (picked < mQuoteSeen.size())
			mQuoteSeen[picked] = 1;
		return picked;
	}

	void rebuildQuoteBag() {
		bool hasUnseen = false;

		mQuoteBag.clear();
		mQuoteBag.reserve(kAboutQuoteCount > 0 ? kAboutQuoteCount - 1 : 0);

		for (unsigned char i : mQuoteSeen) {
			if (i == 0) {
				hasUnseen = true;
				break;
			}
		}

		if (!hasUnseen) {
			std::fill(mQuoteSeen.begin(), mQuoteSeen.end(), static_cast<unsigned char>(0));
			if (mQuoteIndex < mQuoteSeen.size())
				mQuoteSeen[mQuoteIndex] = 1;
		}

		for (std::size_t i = 0; i < kAboutQuoteCount; ++i)
			if (i < mQuoteSeen.size() && mQuoteSeen[i] == 0)
				mQuoteBag.push_back(i);

		if (mQuoteBag.size() > 1) {
			for (std::size_t i = mQuoteBag.size() - 1; i > 0; --i) {
				mQuoteRandomState = mQuoteRandomState * 1664525u + 1013904223u;
				std::size_t j =
				    static_cast<std::size_t>(mQuoteRandomState % static_cast<std::uint32_t>(i + 1));
				std::size_t tmp = mQuoteBag[i];
				mQuoteBag[i] = mQuoteBag[j];
				mQuoteBag[j] = tmp;
			}
		}
		mQuoteBagCursor = 0;
	}

	void tickQuoteRotation() {
		if (mDonePressTracking && !mDoneLongPressTriggered) {
			if (std::chrono::steady_clock::now() - mDonePressStartedAt >=
			    std::chrono::milliseconds(kDoneLongPressMs)) {
				mDoneLongPressTriggered = true;
				mSuppressNextDoneCommand = true;
				enableQuoteModeFromLongPress();
			}
		}
		if (mQuoteBox == nullptr || !mQuoteModeEnabled)
			return;
		if (mQuoteBox->animating()) {
			mQuoteBox->tickTransition();
			if (!mQuoteBox->animating() && mRearmRotationAfterAnimation) {
				mNextRotationAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(kQuoteRotateMs);
				mRearmRotationAfterAnimation = false;
			}
			return;
		}
		if (std::chrono::steady_clock::now() < mNextRotationAt)
			return;
		mQuoteIndex = pickNextQuoteIndex();
		mQuoteBox->beginQuoteTransition(kAboutQuotes[mQuoteIndex]);
		mRearmRotationAfterAnimation = true;
	}

	MRAboutQuoteBox *mQuoteBox;
	TButton *mDoneButton;
	std::size_t mQuoteIndex;
	std::uint32_t mQuoteRandomState;
	std::vector<std::size_t> mQuoteBag;
	std::vector<unsigned char> mQuoteSeen;
	std::size_t mQuoteBagCursor = 0;
	bool mQuoteModeEnabled;
	TTimerId mRotationTimer;
	std::chrono::steady_clock::time_point mNextRotationAt;
	bool mRearmRotationAfterAnimation;
	bool mDonePressTracking;
	bool mDoneLongPressTriggered;
	bool mSuppressNextDoneCommand;
	std::chrono::steady_clock::time_point mDonePressStartedAt;
};

} // namespace

void showAboutDialog() {
	if (TProgram::deskTop == nullptr)
		return;
	TDialog *dialog = new MRAboutDialog();
	(void)mr::dialogs::execDialog(dialog);
}
