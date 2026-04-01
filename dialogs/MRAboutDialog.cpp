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
	TRect r = TProgram::deskTop->getExtent();
	int left = r.a.x + (r.b.x - r.a.x - width) / 2;
	int top = r.a.y + (r.b.y - r.a.y - height) / 2;
	return TRect(left, top, left + width, top + height);
}

void insertCenteredStaticLine(TDialog *dialog, int width, int y, const std::string &text) {
	int x = std::max(2, (width - static_cast<int>(strwidth(text.c_str()))) / 2);
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

std::string trimAscii(const std::string &value) {
	std::size_t start = 0;
	std::size_t end = value.size();
	while (start < end && std::isspace(static_cast<unsigned char>(value[start])) != 0)
		++start;
	while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
		--end;
	return value.substr(start, end - start);
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

	for (std::size_t i = 0; i < value.size(); ++i) {
		unsigned char ch = static_cast<unsigned char>(value[i]);
		if (std::isspace(ch) != 0) {
			pendingSpace = true;
			continue;
		}
		if (pendingSpace && hasOutput)
			out << ' ';
		out << value[i];
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
	    : TView(bounds), currentLines_(), targetLines_(), animationFrame_(0), animationFramesTotal_(22),
	      animating_(false), scrambleSeed_(0x00C0DA42u) {
		options |= ofBuffered;
	}

	void setQuoteImmediate(const std::string &quote) {
		currentLines_ = buildQuoteDisplayLines(quote, std::max(8, size.x - 4));
		targetLines_.clear();
		animationFrame_ = 0;
		animating_ = false;
		drawView();
	}

	void beginQuoteTransition(const std::string &quote) {
		targetLines_ = buildQuoteDisplayLines(quote, std::max(8, size.x - 4));
		animationFrame_ = 0;
		animating_ = true;
		drawView();
	}

	bool tickTransition() {
		if (!animating_)
			return false;
		++animationFrame_;
		if (animationFrame_ >= animationFramesTotal_) {
			currentLines_ = targetLines_;
			targetLines_.clear();
			animationFrame_ = 0;
			animating_ = false;
		}
		drawView();
		return true;
	}

	bool animating() const noexcept {
		return animating_;
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
		s.front = travel == 0 ? 0 : (travel * animationFrame_) / std::max(1, animationFramesTotal_);
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
		if (!animating_)
			return target;

		std::string out = target;
		SweepState sweep = sweepStateForLength(target.size());
		std::uint32_t localSeed =
		    scrambleSeed_ ^ static_cast<std::uint32_t>(lineIndex * 131u) ^
		    static_cast<std::uint32_t>(animationFrame_ * 977u);
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
		if (animating_) {
			int baseRow = centeredBaseRow(targetLines_);
			int lineIndex = row - baseRow;
			if (0 <= lineIndex && lineIndex < static_cast<int>(targetLines_.size())) {
				const std::string &target = targetLines_[static_cast<std::size_t>(lineIndex)];
				std::string line = scrambledLine(target,
				                                 static_cast<std::size_t>(lineIndex));
				SweepState sweep = sweepStateForLength(target.size());
				std::uint32_t colorSeed = scrambleSeed_ ^
				                          static_cast<std::uint32_t>(animationFrame_ * 4099u) ^
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
			int baseRow = centeredBaseRow(currentLines_);
			int lineIndex = row - baseRow;
			if (0 <= lineIndex && lineIndex < static_cast<int>(currentLines_.size())) {
				const std::string &line = currentLines_[static_cast<std::size_t>(lineIndex)];
				int innerWidth = std::max(0, size.x - 4);
				int textWidth = std::max(0, strwidth(line.c_str()));
				int startX = 2 + std::max(0, (innerWidth - textWidth) / 2);
				b.moveStr(static_cast<ushort>(startX), line.c_str(), color, innerWidth);
			}
		}
	}

	std::vector<std::string> currentLines_;
	std::vector<std::string> targetLines_;
	int animationFrame_;
	int animationFramesTotal_;
	bool animating_;
	mutable std::uint32_t scrambleSeed_;
};

class MRAboutDialog : public TDialog {
  public:
	MRAboutDialog() noexcept
	    : TWindowInit(&TDialog::initFrame), TDialog(centeredRect(76, 16), "ABOUT"), quoteBox_(nullptr),
	      doneButton_(nullptr), quoteIndex_(0), quoteRandomState_(0), quoteModeEnabled_(false), rotationTimer_(0),
	      nextRotationAt_(), rearmRotationAfterAnimation_(false), donePressTracking_(false),
	      doneLongPressTriggered_(false), suppressNextDoneCommand_(false), donePressStartedAt_() {
		eventMask |= evBroadcast;
		insertCenteredStaticLine(this, size.x, 2,
		                         std::string("Multi-Edit Revisited ") + mrDisplayVersion());
		insertCenteredStaticLine(this, size.x, 3, "Dr. Michael \"iDoc\" Raus & Codex AI");

		quoteBox_ = new MRAboutQuoteBox(TRect(4, 5, size.x - 4, 13));
		insert(quoteBox_);
		quoteBox_->setQuoteImmediate(kAboutQuotes[0]);
		quoteSeen_.assign(kAboutQuoteCount, 0);
		if (!quoteSeen_.empty())
			quoteSeen_[0] = 1;
		quoteRandomState_ = static_cast<std::uint32_t>(
		    std::chrono::steady_clock::now().time_since_epoch().count()) ^
		                   0xA39F1D5Bu;

		doneButton_ =
		    new TButton(TRect(size.x / 2 - 5, 13, size.x / 2 + 5, 15), "Done", cmAboutDone, bfDefault);
		insert(doneButton_);
	}

	~MRAboutDialog() override {
		killTimer(rotationTimer_);
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
			if (suppressNextDoneCommand_) {
				suppressNextDoneCommand_ = false;
				clearEvent(event);
				return;
			}
			endModal(cmOK);
			clearEvent(event);
			return;
		}
		TDialog::handleEvent(event);
		if (event.what == evBroadcast && event.message.command == cmTimerExpired &&
		    event.message.infoPtr == rotationTimer_) {
			tickQuoteRotation();
			clearEvent(event);
		}
	}

  private:
	void armRotationTimer() {
		if (rotationTimer_ == 0 && owner != nullptr)
			rotationTimer_ = setTimer(kAnimationTickMs, kAnimationTickMs);
	}

	void trackDonePress(const TEvent &event) {
		if (doneButton_ == nullptr)
			return;
		TPoint local = makeLocal(event.mouse.where);
		bool insideDone = doneButton_->getBounds().contains(local);
		if (event.what == evMouseDown) {
			if (insideDone) {
				donePressTracking_ = true;
				doneLongPressTriggered_ = false;
				donePressStartedAt_ = std::chrono::steady_clock::now();
			}
			return;
		}
		if (donePressTracking_) {
			if (doneLongPressTriggered_)
				suppressNextDoneCommand_ = true;
			donePressTracking_ = false;
		}
	}

	void enableQuoteModeFromLongPress() {
		if (quoteModeEnabled_ || quoteBox_ == nullptr || kAboutQuoteCount <= 1)
			return;
		quoteModeEnabled_ = true;
		quoteIndex_ = pickNextQuoteIndex();
		quoteBox_->beginQuoteTransition(kAboutQuotes[quoteIndex_]);
		rearmRotationAfterAnimation_ = true;
	}

	std::size_t pickNextQuoteIndex() {
		if (kAboutQuoteCount <= 1)
			return 0;
		if (quoteBagCursor_ >= quoteBag_.size())
			rebuildQuoteBag();
		if (quoteBagCursor_ >= quoteBag_.size())
			return (quoteIndex_ + 1) % kAboutQuoteCount;
		std::size_t picked = quoteBag_[quoteBagCursor_++];
		if (picked < quoteSeen_.size())
			quoteSeen_[picked] = 1;
		return picked;
	}

	void rebuildQuoteBag() {
		bool hasUnseen = false;

		quoteBag_.clear();
		quoteBag_.reserve(kAboutQuoteCount > 0 ? kAboutQuoteCount - 1 : 0);

		for (std::size_t i = 0; i < quoteSeen_.size(); ++i) {
			if (quoteSeen_[i] == 0) {
				hasUnseen = true;
				break;
			}
		}

		if (!hasUnseen) {
			std::fill(quoteSeen_.begin(), quoteSeen_.end(), static_cast<unsigned char>(0));
			if (quoteIndex_ < quoteSeen_.size())
				quoteSeen_[quoteIndex_] = 1;
		}

		for (std::size_t i = 0; i < kAboutQuoteCount; ++i)
			if (i < quoteSeen_.size() && quoteSeen_[i] == 0)
				quoteBag_.push_back(i);

		if (quoteBag_.size() > 1) {
			for (std::size_t i = quoteBag_.size() - 1; i > 0; --i) {
				quoteRandomState_ = quoteRandomState_ * 1664525u + 1013904223u;
				std::size_t j =
				    static_cast<std::size_t>(quoteRandomState_ % static_cast<std::uint32_t>(i + 1));
				std::size_t tmp = quoteBag_[i];
				quoteBag_[i] = quoteBag_[j];
				quoteBag_[j] = tmp;
			}
		}
		quoteBagCursor_ = 0;
	}

	void tickQuoteRotation() {
		if (donePressTracking_ && !doneLongPressTriggered_) {
			if (std::chrono::steady_clock::now() - donePressStartedAt_ >=
			    std::chrono::milliseconds(kDoneLongPressMs)) {
				doneLongPressTriggered_ = true;
				suppressNextDoneCommand_ = true;
				enableQuoteModeFromLongPress();
			}
		}
		if (quoteBox_ == nullptr || !quoteModeEnabled_)
			return;
		if (quoteBox_->animating()) {
			quoteBox_->tickTransition();
			if (!quoteBox_->animating() && rearmRotationAfterAnimation_) {
				nextRotationAt_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(kQuoteRotateMs);
				rearmRotationAfterAnimation_ = false;
			}
			return;
		}
		if (std::chrono::steady_clock::now() < nextRotationAt_)
			return;
		quoteIndex_ = pickNextQuoteIndex();
		quoteBox_->beginQuoteTransition(kAboutQuotes[quoteIndex_]);
		rearmRotationAfterAnimation_ = true;
	}

	MRAboutQuoteBox *quoteBox_;
	TButton *doneButton_;
	std::size_t quoteIndex_;
	std::uint32_t quoteRandomState_;
	std::vector<std::size_t> quoteBag_;
	std::vector<unsigned char> quoteSeen_;
	std::size_t quoteBagCursor_ = 0;
	bool quoteModeEnabled_;
	TTimerId rotationTimer_;
	std::chrono::steady_clock::time_point nextRotationAt_;
	bool rearmRotationAfterAnimation_;
	bool donePressTracking_;
	bool doneLongPressTriggered_;
	bool suppressNextDoneCommand_;
	std::chrono::steady_clock::time_point donePressStartedAt_;
};

} // namespace

void showAboutDialog() {
	if (TProgram::deskTop == nullptr)
		return;
	TDialog *dialog = new MRAboutDialog();
	TProgram::deskTop->execView(dialog);
	TObject::destroy(dialog);
}
