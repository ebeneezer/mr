#ifndef MRSETUPDIALOGCOMMON_HPP
#define MRSETUPDIALOGCOMMON_HPP

#define Uses_TDialog
#define Uses_TEvent
#define Uses_TGroup
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TView
#include <tvision/tv.h>

#include <string>
#include <vector>

class TDialog;
class TGroup;
class TRect;

TRect centeredSetupDialogRect(int width, int height);
TGroup *createSetupDialogContentGroup(const TRect &bounds);
void insertSetupStaticLine(TDialog *dialog, int x, int y, const char *text);
TDialog *createSetupSimplePreviewDialog(const char *title, int width, int height,
                                        const std::vector<std::string> &lines,
                                        bool showOkCancelHelp);

class MRScrollableDialog : public TDialog {
  public:
	struct ManagedItem {
		TView *view;
		TRect base;
	};

	MRScrollableDialog(const TRect &bounds, const char *title, int virtualWidth,
	                   int virtualHeight);
	void handleEvent(TEvent &event) override;

	void addManaged(TView *view, const TRect &base);
	void initScrollIfNeeded();
	void selectContent();
	TGroup *managedContent() const noexcept { return content_; }

  private:
	void applyScroll();

	int virtualWidth_ = 0;
	int virtualHeight_ = 0;
	TRect contentRect_;
	TGroup *content_ = nullptr;
	std::vector<ManagedItem> managedViews_;
	TScrollBar *hScrollBar_ = nullptr;
	TScrollBar *vScrollBar_ = nullptr;
};

#endif
