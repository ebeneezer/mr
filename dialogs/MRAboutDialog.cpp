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
#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "../app/MRVersion.hpp"

namespace {

constexpr uint kAnimationTickMs = 100;
constexpr uint kQuoteRotateMs = 10000;

const char *const kAboutQuotes[] = {
    "\"I live again.\" Caleb (Blood)",
    "\"It is never enough.\" Frank Cotton (Hellraiser)",
    "\"C makes it easy to shoot yourself in the foot; C++ makes it harder, but when you do it blows your whole leg off.\" Bjarne Stroustrup",
    "\"Talk is cheap. Show me the code.\" Linus Torvalds",
    "\"My main conclusion after spending ten years of my life working on the TeX project is that software is hard. It's harder than anything else I've ever had to do.\" Donald Knuth",
    "\"Coding makes me horny.\" Michael 'iDoc' Raus"};

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

class MRAboutQuoteBox : public TView {
  public:
	MRAboutQuoteBox(const TRect &bounds) noexcept
	    : TView(bounds), currentLines_(), targetLines_(), animationFrame_(0), animationFramesTotal_(14),
	      animating_(false), scrambleSeed_(0x00C0DA42u) {
		options |= ofBuffered;
	}

	void setQuoteImmediate(const std::string &quote) {
		currentLines_ = wrapQuoteText(quote, std::max(8, size.x - 4));
		targetLines_.clear();
		animationFrame_ = 0;
		animating_ = false;
		drawView();
	}

	void beginQuoteTransition(const std::string &quote) {
		targetLines_ = wrapQuoteText(quote, std::max(8, size.x - 4));
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
		char topLeft = animating_ ? '\xC9' : '\xDA';
		char topRight = animating_ ? '\xBB' : '\xBF';
		char bottomLeft = animating_ ? '\xC8' : '\xC0';
		char bottomRight = animating_ ? '\xBC' : '\xD9';
		char horiz = animating_ ? '\xCD' : '\xC4';
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
	static char scrambleGlyph(std::uint32_t value) noexcept {
		static const char glyphs[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?@#$%&*+=<>/\\[]{}";
		return glyphs[value % (sizeof(glyphs) - 1)];
	}

	int centeredBaseRow(const std::vector<std::string> &lines) const noexcept {
		int innerHeight = std::max(1, size.y - 2);
		int freeRows = std::max(0, innerHeight - static_cast<int>(lines.size()));
		return 1 + freeRows / 2;
	}

	std::string scrambledLine(const std::string &target, std::size_t lineIndex) const {
		if (!animating_)
			return target;

		std::string out = target;
		std::size_t totalChars = target.size();
		std::size_t revealCount = totalChars == 0
		                              ? 0
		                              : (totalChars * static_cast<std::size_t>(animationFrame_)) /
		                                    static_cast<std::size_t>(animationFramesTotal_);
		std::uint32_t localSeed =
		    scrambleSeed_ ^ static_cast<std::uint32_t>(lineIndex * 131u) ^
		    static_cast<std::uint32_t>(animationFrame_ * 977u);
		for (std::size_t i = 0; i < out.size(); ++i) {
			unsigned char c = static_cast<unsigned char>(out[i]);
			if (c == ' ')
				continue;
			if (i < revealCount)
				continue;
			localSeed = localSeed * 1664525u + 1013904223u;
			out[i] = scrambleGlyph(localSeed);
		}
		if (revealCount < out.size())
			out[revealCount] = '_';
		return out;
	}

	void drawContentRow(TDrawBuffer &b, int row, TColorAttr color) const {
		if (animating_) {
			int baseRow = centeredBaseRow(targetLines_);
			int lineIndex = row - baseRow;
			if (0 <= lineIndex && lineIndex < static_cast<int>(targetLines_.size())) {
				std::string line = scrambledLine(targetLines_[static_cast<std::size_t>(lineIndex)],
				                                 static_cast<std::size_t>(lineIndex));
				b.moveStr(2, line.c_str(), color, size.x - 4);
			}
		} else {
			int baseRow = centeredBaseRow(currentLines_);
			int lineIndex = row - baseRow;
			if (0 <= lineIndex && lineIndex < static_cast<int>(currentLines_.size()))
				b.moveStr(2, currentLines_[static_cast<std::size_t>(lineIndex)].c_str(), color,
				          size.x - 4);
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
	      quoteIndex_(0), rotationTimer_(0), nextRotationAt_(), rearmRotationAfterAnimation_(false) {
		eventMask |= evBroadcast;
		insertCenteredStaticLine(this, size.x, 2,
		                         std::string("Multi-Edit Revisited ") + mrDisplayVersion());
		insertCenteredStaticLine(this, size.x, 3, "Dr. Michael \"iDoc\" Raus & Codex AI");

		quoteBox_ = new MRAboutQuoteBox(TRect(4, 5, size.x - 4, 10));
		insert(quoteBox_);
		quoteBox_->setQuoteImmediate(std::string());

		insert(new TButton(TRect(size.x / 2 - 5, 12, size.x / 2 + 5, 14), "Done", cmOK, bfDefault));
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
		TDialog::handleEvent(event);
		if (event.what == evBroadcast && event.message.command == cmTimerExpired &&
		    event.message.infoPtr == rotationTimer_) {
			tickQuoteRotation();
			clearEvent(event);
		}
	}

  private:
	void armRotationTimer() {
		if (rotationTimer_ == 0 && owner != nullptr) {
			rotationTimer_ = setTimer(kAnimationTickMs, kAnimationTickMs);
			quoteBox_->beginQuoteTransition(kAboutQuotes[quoteIndex_]);
			rearmRotationAfterAnimation_ = true;
		}
	}

	void tickQuoteRotation() {
		if (quoteBox_ == nullptr)
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
		quoteIndex_ = (quoteIndex_ + 1) % (sizeof(kAboutQuotes) / sizeof(kAboutQuotes[0]));
		quoteBox_->beginQuoteTransition(kAboutQuotes[quoteIndex_]);
		rearmRotationAfterAnimation_ = true;
	}

	MRAboutQuoteBox *quoteBox_;
	std::size_t quoteIndex_;
	TTimerId rotationTimer_;
	std::chrono::steady_clock::time_point nextRotationAt_;
	bool rearmRotationAfterAnimation_;
};

} // namespace

void showAboutDialog() {
	if (TProgram::deskTop == nullptr)
		return;
	TDialog *dialog = new MRAboutDialog();
	TProgram::deskTop->execView(dialog);
	TObject::destroy(dialog);
}
