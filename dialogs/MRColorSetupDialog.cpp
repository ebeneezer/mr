#define Uses_TButton
#define Uses_TDialog
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TGroup
#define Uses_TProgram
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TView
#include <tvision/tv.h>

#include "MRSetupDialogs.hpp"
#include "MRSetupDialogCommon.hpp"

#include "../app/MRCommands.hpp"
#include "../services/MRDialogPaths.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace {
bool isColorSetupModalCommand(ushort command) {
	switch (command) {
		case cmMrColorWindowColors:
		case cmMrColorMenuDialogColors:
		case cmMrColorHelpColors:
		case cmMrColorOtherColors:
		case cmMrColorLoadTheme:
		case cmMrColorSaveTheme:
			return true;
		default:
			return false;
	}
}

class TThemeNameField : public TView {
  public:
	TThemeNameField(const TRect &bounds, const std::string &text) : TView(bounds), text_(text) {
	}

	void draw() override {
		TDrawBuffer buffer;
		TColorAttr color = (TProgram::application != nullptr) ? TProgram::application->mapColor(2)
		                                                      : TColorAttr(getColor(1));
		std::string shown = "active: " + text_;
		int start = 0;
		buffer.moveChar(0, ' ', color, size.x);
		if (size.x > 0) {
			if (shown.size() > static_cast<std::size_t>(size.x))
				shown = shown.substr(0, static_cast<std::size_t>(size.x));
			start = (size.x - static_cast<int>(shown.size())) / 2;
			if (start < 0)
				start = 0;
			buffer.moveStr(static_cast<ushort>(start), shown.c_str(), color, size.x - start);
		}
		writeLine(0, 0, size.x, 1, buffer);
	}

  private:
	std::string text_;
};

class TColorSetupDialog : public TDialog {
  public:
	struct ManagedItem {
		TView *view;
		TRect base;
	};

	TColorSetupDialog(const TRect &bounds, const char *title, int virtualWidth, int virtualHeight)
	    : TWindowInit(&TDialog::initFrame), TDialog(bounds, title), virtualWidth_(virtualWidth),
	      virtualHeight_(virtualHeight) {
		contentRect_ = TRect(1, 1, size.x - 1, size.y - 1);
		content_ = createSetupDialogContentGroup(contentRect_);
		if (content_ != nullptr)
			insert(content_);
	}

	void addManaged(TView *view, const TRect &base) {
		ManagedItem item;
		item.view = view;
		item.base = base;
		managedViews_.push_back(item);
		if (content_ != nullptr) {
			TRect local = base;
			local.move(-contentRect_.a.x, -contentRect_.a.y);
			view->locate(local);
			content_->insert(view);
		} else
			insert(view);
	}

	void initScrollIfNeeded() {
		int virtualContentWidth = std::max(1, virtualWidth_ - 2);
		int virtualContentHeight = std::max(1, virtualHeight_ - 2);
		bool needH = false;
		bool needV = false;

		for (;;) {
			bool prevH = needH;
			bool prevV = needV;
			int viewportWidth = std::max(1, size.x - 2);
			int viewportHeight = std::max(1, size.y - 2);
			needH = virtualContentWidth > viewportWidth;
			needV = virtualContentHeight > viewportHeight;
			if (needH == prevH && needV == prevV)
				break;
		}

		contentRect_ = TRect(1, 1, size.x - 1, size.y - 1);
		if (contentRect_.b.x <= contentRect_.a.x)
			contentRect_.b.x = contentRect_.a.x + 1;
		if (contentRect_.b.y <= contentRect_.a.y)
			contentRect_.b.y = contentRect_.a.y + 1;
		if (content_ != nullptr)
			content_->locate(contentRect_);

		if (needH) {
			TRect hRect(1, size.y - 1, size.x - 1, size.y);
			if (hScrollBar_ == nullptr) {
				hScrollBar_ = new TScrollBar(hRect);
				insert(hScrollBar_);
			} else
				hScrollBar_->locate(hRect);
		}
		if (needV) {
			TRect vRect(size.x - 1, 1, size.x, size.y - 1);
			if (vScrollBar_ == nullptr) {
				vScrollBar_ = new TScrollBar(vRect);
				insert(vScrollBar_);
			} else
				vScrollBar_->locate(vRect);
		}
		if (hScrollBar_ != nullptr) {
			int maxDx = std::max(0, virtualContentWidth - std::max(1, contentRect_.b.x - contentRect_.a.x));
			hScrollBar_->setParams(0, 0, maxDx, std::max(1, (contentRect_.b.x - contentRect_.a.x) / 2), 1);
		}
		if (vScrollBar_ != nullptr) {
			int maxDy = std::max(0, virtualContentHeight - std::max(1, contentRect_.b.y - contentRect_.a.y));
			vScrollBar_->setParams(0, 0, maxDy, std::max(1, (contentRect_.b.y - contentRect_.a.y) / 2), 1);
		}
		applyScroll();
	}

	void handleEvent(TEvent &event) override {
		TDialog::handleEvent(event);
		if (event.what == evCommand && isColorSetupModalCommand(event.message.command)) {
			endModal(event.message.command);
			clearEvent(event);
			return;
		}
		if (event.what == evBroadcast && event.message.command == cmScrollBarChanged &&
		    (event.message.infoPtr == hScrollBar_ || event.message.infoPtr == vScrollBar_)) {
			applyScroll();
			clearEvent(event);
		}
	}

  private:
	void applyScroll() {
		int dx = hScrollBar_ != nullptr ? hScrollBar_->value : 0;
		int dy = vScrollBar_ != nullptr ? vScrollBar_->value : 0;

		for (std::size_t i = 0; i < managedViews_.size(); ++i) {
			TRect moved = managedViews_[i].base;
			moved.move(-dx, -dy);
			moved.move(-contentRect_.a.x, -contentRect_.a.y);
			managedViews_[i].view->locate(moved);
		}
		if (content_ != nullptr)
			content_->drawView();
	}

	int virtualWidth_ = 0;
	int virtualHeight_ = 0;
	TRect contentRect_;
	TGroup *content_ = nullptr;
	std::vector<ManagedItem> managedViews_;
	TScrollBar *hScrollBar_ = nullptr;
	TScrollBar *vScrollBar_ = nullptr;
};
} // namespace

TDialog *createColorSetupDialog() {
	int width = 56;
	int height = 15;
	int left = 4;
	int right = width - 4;
	int row = 2;
	TColorSetupDialog *dialog =
	    new TColorSetupDialog(centeredSetupDialogRect(width, height), "COLOR SETUP", width, height);
	std::string activeThemeName = configuredColorThemeDisplayName();
	int fieldRow = 10;
	int fieldLeft = 4;
	int fieldRight = width - 4;
	int buttonWidth = 13;
	int buttonGap = 2;
	int buttonsTotal = buttonWidth * 2 + buttonGap;
	int buttonStart = (width - buttonsTotal) / 2;
	int buttonRow = 11;

	if (dialog == nullptr)
		return nullptr;
	dialog->addManaged(new TButton(TRect(left, row, right, row + 2), "Window colors", cmMrColorWindowColors, bfNormal),
	                   TRect(left, row, right, row + 2));
	row += 2;
	dialog->addManaged(
	    new TButton(TRect(left, row, right, row + 2), "Menu/Dialog colors", cmMrColorMenuDialogColors, bfNormal),
	    TRect(left, row, right, row + 2));
	row += 2;
	dialog->addManaged(new TButton(TRect(left, row, right, row + 2), "Help colors", cmMrColorHelpColors, bfNormal),
	                   TRect(left, row, right, row + 2));
	row += 2;
	dialog->addManaged(new TButton(TRect(left, row, right, row + 2), "Other colors", cmMrColorOtherColors, bfNormal),
	                   TRect(left, row, right, row + 2));

	dialog->addManaged(new TThemeNameField(TRect(fieldLeft, fieldRow, fieldRight, fieldRow + 1), activeThemeName),
	                   TRect(fieldLeft, fieldRow, fieldRight, fieldRow + 1));

	dialog->addManaged(new TButton(TRect(buttonStart, buttonRow, buttonStart + buttonWidth, buttonRow + 2),
	                               "Load Theme", cmMrColorLoadTheme, bfNormal),
	                   TRect(buttonStart, buttonRow, buttonStart + buttonWidth, buttonRow + 2));
	dialog->addManaged(
	    new TButton(TRect(buttonStart + buttonWidth + buttonGap, buttonRow,
	                      buttonStart + buttonWidth + buttonGap + buttonWidth, buttonRow + 2),
	                "Save Theme", cmMrColorSaveTheme, bfNormal),
	    TRect(buttonStart + buttonWidth + buttonGap, buttonRow,
	          buttonStart + buttonWidth + buttonGap + buttonWidth, buttonRow + 2));

	dialog->initScrollIfNeeded();
	return dialog;
}
