#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TRect
#define Uses_TDialog
#define Uses_TButton
#define Uses_TEvent
#define Uses_TGroup
#define Uses_TScrollBar
#define Uses_TStaticText
#define Uses_TView
#define Uses_TDrawBuffer
#include <tvision/tv.h>

#include "MRSetupDialogCommon.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

class TSetupDialogContentGroup : public TGroup {
  public:
	explicit TSetupDialogContentGroup(const TRect &bounds) : TGroup(bounds) {
	}

	void draw() override {
		TDrawBuffer buffer;
		TColorAttr color = owner != nullptr ? owner->mapColor(1) : mapColor(1);
		TView *child = first();

		buffer.moveChar(0, ' ', color, size.x);
		for (short y = 0; y < size.y; ++y)
			writeLine(0, y, size.x, 1, buffer);
		drawSubViews(child, nullptr);
	}
};

class TSetupScrollableDialog : public TDialog {
  public:
	struct ManagedItem {
		TView *view;
		TRect base;
	};

	TSetupScrollableDialog(const TRect &bounds, const char *title, int virtualWidth, int virtualHeight)
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

TGroup *createSetupDialogContentGroup(const TRect &bounds) {
	return new TSetupDialogContentGroup(bounds);
}

TRect centeredSetupDialogRect(int width, int height) {
	TRect r = TProgram::deskTop != nullptr ? TProgram::deskTop->getExtent() : TRect(0, 0, 80, 25);
	int availableWidth = std::max(1, r.b.x - r.a.x);
	int availableHeight = std::max(1, r.b.y - r.a.y);
	int safeWidth = std::max(10, std::min(width, availableWidth));
	int safeHeight = std::max(6, std::min(height, availableHeight));
	int left = r.a.x + std::max(0, (availableWidth - safeWidth) / 2);
	int top = r.a.y + std::max(0, (availableHeight - safeHeight) / 2);

	return TRect(left, top, left + safeWidth, top + safeHeight);
}

TRect centeredSetupDialogRectForProfile(int compactWidth, int compactHeight, int relaxedWidth,
                                        int relaxedHeight) {
	(void)compactWidth;
	(void)compactHeight;
	return centeredSetupDialogRect(relaxedWidth, relaxedHeight);
}

void insertSetupStaticLine(TDialog *dialog, int x, int y, const char *text) {
	dialog->insert(new TStaticText(TRect(x, y, x + std::strlen(text) + 1, y + 1), text));
}

TDialog *createSetupSimplePreviewDialog(const char *title, int width, int height,
                                        const std::vector<std::string> &lines,
                                        bool showOkCancelHelp) {
	TSetupScrollableDialog *dialog =
	    new TSetupScrollableDialog(centeredSetupDialogRect(width, height), title, width, height);
	int y = 2;

	if (dialog == nullptr)
		return nullptr;
	for (std::vector<std::string>::const_iterator it = lines.begin(); it != lines.end(); ++it, ++y) {
		TRect lineRect(2, y, 2 + std::strlen(it->c_str()) + 1, y + 1);
		dialog->addManaged(new TStaticText(lineRect, it->c_str()), lineRect);
	}

	if (showOkCancelHelp) {
		TRect okRect(width - 34, height - 3, width - 24, height - 1);
		TRect cancelRect(width - 23, height - 3, width - 10, height - 1);
		TRect helpRect(width - 9, height - 3, width - 2, height - 1);
		dialog->addManaged(new TButton(okRect, "OK", cmOK, bfDefault), okRect);
		dialog->addManaged(new TButton(cancelRect, "Cancel", cmCancel, bfNormal), cancelRect);
		dialog->addManaged(new TButton(helpRect, "Help", cmHelp, bfNormal), helpRect);
	} else {
		TRect doneRect(width / 2 - 4, height - 3, width / 2 + 4, height - 1);
		dialog->addManaged(new TButton(doneRect, "Done", cmOK, bfDefault), doneRect);
	}

	dialog->initScrollIfNeeded();
	return dialog;
}

TDialog *createSetupSimplePreviewDialogForProfile(const char *title, int compactWidth, int compactHeight,
                                                  int relaxedWidth, int relaxedHeight,
                                                  const std::vector<std::string> &lines,
                                                  bool showOkCancelHelp) {
	(void)compactWidth;
	(void)compactHeight;
	return createSetupSimplePreviewDialog(title, relaxedWidth, relaxedHeight, lines, showOkCancelHelp);
}
