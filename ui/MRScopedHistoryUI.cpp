#define Uses_TChDirDialog
#define Uses_TDialog
#define Uses_TFileDialog
#define Uses_TFileInputLine
#define Uses_TFileList
#define Uses_THistory
#define Uses_TInputLine
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TObject
#define Uses_TRect
#define Uses_TView
#include <tvision/tv.h>

#include "MRDropList.hpp"
#include "MRScopedHistoryUI.hpp"

#include "../config/MRDialogPaths.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {
enum : ushort {
	cmMrScopedHistoryChoose = 3868,
	cmMrScopedHistoryAccept
};

class TWheelFileDialog final : public TFileDialog {
 public:
	TWheelFileDialog(MRDialogHistoryScope aScope, const char *wildCard, const char *title, const char *inputName, ushort options) noexcept : TWindowInit(TFileDialog::initFrame), TFileDialog(wildCard, title, inputName, options, 0), scope(aScope), dialogOptions(options) {
		replaceHistoryView(static_cast<TInputLine *>(fileName));
	}

	void handleEvent(TEvent &event) override {
		if (historyDropList.visible()) {
			if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEsc) {
				hideHistoryList();
				clearEvent(event);
				return;
			}
			if (event.what == evMouseDown && !historyDropList.containsPoint(event.mouse.where) && !historyDropList.buttonContainsPoint(event.mouse.where)) {
				hideHistoryList();
				clearEvent(event);
				return;
			}
		}
		if (event.what == evCommand && event.message.command == cmMrScopedHistoryChoose) {
			toggleHistoryList();
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmMrScopedHistoryAccept) {
			acceptHistorySelection();
			clearEvent(event);
			return;
		}
		if (event.what == evBroadcast && event.message.command == cmFileDoubleClicked && (dialogOptions & fdOpenButton) != 0) {
			event.what = evCommand;
			event.message.command = cmFileOpen;
			TFileDialog::handleEvent(event);
			return;
		}
		if (event.what == evMouseWheel && fileList != nullptr && fileList->containsMouse(event) && fileList->range > 0) {
			const int delta = event.mouse.wheel == mwUp || event.mouse.wheel == mwLeft ? -1 : 1;
			const short next = static_cast<short>(std::clamp<int>(fileList->focused + delta, 0, fileList->range - 1));

			fileList->focusItemNum(next);
			clearEvent(event);
			return;
		}
		TFileDialog::handleEvent(event);
	}

  private:
	void toggleHistoryList() {
		std::vector<std::string> entries;
		TRect bounds;
		short visibleRows = 7;

		if (historyLink == nullptr) return;
		configuredScopedDialogFileHistoryEntries(scope, entries);
		if (entries.empty()) return;
		bounds = historyLink->getBounds();
		bounds.b.x++;
		if (visibleRows > size.y - bounds.a.y - 1) visibleRows = static_cast<short>(size.y - bounds.a.y - 1);
		if (visibleRows < 1) visibleRows = 1;
		historyDropList.toggle(*this, bounds, entries, std::string(historyLink->data), this, cmMrScopedHistoryAccept, visibleRows);
	}

	void hideHistoryList() {
		historyDropList.hide();
		if (historyLink != nullptr) historyLink->selectAll(True);
	}

	void acceptHistorySelection() {
		std::string value;

		if (!historyDropList.acceptSelection(value) || historyLink == nullptr) return;
		strnzcpy(historyLink->data, value.c_str(), historyLink->maxLen + 1);
		historyLink->selectAll(True);
		historyLink->drawView();
	}

	void replaceHistoryView(TInputLine *link) {
		if (link == nullptr) return;
		for (TView *child = first(); child != nullptr;) {
			TView *next = child->nextView();
			if (dynamic_cast<THistory *>(child) != nullptr) {
				const TRect childBounds = child->getBounds();
				const ushort childGrowMode = child->growMode;
				remove(child);
				TObject::destroy(child);
				TView *history = historyDropList.createButton(*this, childBounds, link, this, cmMrScopedHistoryChoose, true);
				history->growMode = childGrowMode;
				historyLink = link;
				return;
			}
			child = next;
		}
	}

	MRDialogHistoryScope scope;
	ushort dialogOptions = 0;
	TInputLine *historyLink = nullptr;
	MRDropList historyDropList;
};

class TWheelChDirDialog final : public TChDirDialog {
 public:
	TWheelChDirDialog(MRDialogHistoryScope aScope, ushort options) noexcept : TWindowInit(TChDirDialog::initFrame), TChDirDialog(options, 0), scope(aScope) {
		replaceHistoryView(findInputLine(TRect(3, 3, 42, 4)));
	}

	void handleEvent(TEvent &event) override {
		if (historyDropList.visible()) {
			if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEsc) {
				hideHistoryList();
				clearEvent(event);
				return;
			}
			if (event.what == evMouseDown && !historyDropList.containsPoint(event.mouse.where) && !historyDropList.buttonContainsPoint(event.mouse.where)) {
				hideHistoryList();
				clearEvent(event);
				return;
			}
		}
		if (event.what == evCommand && event.message.command == cmMrScopedHistoryChoose) {
			toggleHistoryList();
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmMrScopedHistoryAccept) {
			acceptHistorySelection();
			clearEvent(event);
			return;
		}
		TChDirDialog::handleEvent(event);
	}

  private:
	TInputLine *findInputLine(const TRect &bounds) {
		for (TView *child = first(); child != nullptr; child = child->nextView())
			if (child->getBounds() == bounds) return dynamic_cast<TInputLine *>(child);
		return nullptr;
	}

	void toggleHistoryList() {
		std::vector<std::string> entries;
		TRect bounds;
		short visibleRows = 7;

		if (historyLink == nullptr) return;
		configuredScopedDialogPathHistoryEntries(scope, entries);
		if (entries.empty()) return;
		bounds = historyLink->getBounds();
		bounds.b.x++;
		if (visibleRows > size.y - bounds.a.y - 1) visibleRows = static_cast<short>(size.y - bounds.a.y - 1);
		if (visibleRows < 1) visibleRows = 1;
		historyDropList.toggle(*this, bounds, entries, std::string(historyLink->data), this, cmMrScopedHistoryAccept, visibleRows);
	}

	void hideHistoryList() {
		historyDropList.hide();
		if (historyLink != nullptr) historyLink->selectAll(True);
	}

	void acceptHistorySelection() {
		std::string value;

		if (!historyDropList.acceptSelection(value) || historyLink == nullptr) return;
		strnzcpy(historyLink->data, value.c_str(), historyLink->maxLen + 1);
		historyLink->selectAll(True);
		historyLink->drawView();
	}

	void replaceHistoryView(TInputLine *link) {
		if (link == nullptr) return;
		for (TView *child = first(); child != nullptr;) {
			TView *next = child->nextView();
			if (dynamic_cast<THistory *>(child) != nullptr) {
				const TRect childBounds = child->getBounds();
				const ushort childGrowMode = child->growMode;
				remove(child);
				TObject::destroy(child);
				TView *history = historyDropList.createButton(*this, childBounds, link, this, cmMrScopedHistoryChoose, true);
				history->growMode = childGrowMode;
				historyLink = link;
				return;
			}
			child = next;
		}
	}

	MRDialogHistoryScope scope;
	TInputLine *historyLink = nullptr;
	MRDropList historyDropList;
};

} // namespace

namespace mr::ui {

TFileDialog *createScopedFileDialog(MRDialogHistoryScope scope, const char *wildCard, const char *title, const char *inputName, unsigned short options) {
	return new TWheelFileDialog(scope, wildCard, title, inputName, options);
}

TDialog *createScopedDirectoryDialog(MRDialogHistoryScope scope, unsigned short options) {
	return new TWheelChDirDialog(scope, options);
}

} // namespace mr::ui
