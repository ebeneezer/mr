#define Uses_TButton
#define Uses_TDialog
#define Uses_TEvent
#define Uses_TGroup
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TView
#include <tvision/tv.h>

#include "MRSetupDialogs.hpp"
#include "MRSetupDialogCommon.hpp"

#include "../app/MRCommands.hpp"

#include <algorithm>
#include <vector>

namespace {
bool isColorSetupModalCommand(ushort command) {
	switch (command) {
		case cmMrColorWindowColors:
		case cmMrColorMenuDialogColors:
		case cmMrColorHelpColors:
		case cmMrColorOtherColors:
			return true;
		default:
			return false;
	}
}

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
	int width = 40;
	int height = 13;
	int left = 2;
	int right = width - 2;
	int row = 2;
	TColorSetupDialog *dialog =
	    new TColorSetupDialog(centeredSetupDialogRect(width, height), "COLOR SETUP", width, height);

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
	dialog->initScrollIfNeeded();
	return dialog;
}
