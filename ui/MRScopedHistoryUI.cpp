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
enum class ScopedDialogHistoryKind : unsigned char {
	File = 0,
	Path = 1
};

enum : ushort {
	cmMrScopedHistoryChoose = 3868,
	cmMrScopedHistoryAccept
};

class ScopedHistoryButton final : public THistory {
  public:
	ScopedHistoryButton(const TRect &bounds, TInputLine *aLink, ScopedDialogHistoryKind aKind) noexcept : THistory(bounds, aLink, aKind == ScopedDialogHistoryKind::File ? kFileDialogHistoryId : kPathDialogHistoryId), kind(aKind) {
	}

	void handleEvent(TEvent &event) override {
		TView *target = owner;

		TView::handleEvent(event);
		if (event.what == evMouseDown || (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbDown && (link->state & sfFocused) != 0)) {
			if (!link->focus()) {
				clearEvent(event);
				return;
			}
			recordHistory(link->data);
			while (target != nullptr && dynamic_cast<TDialog *>(target) == nullptr)
				target = target->owner;
			message(target != nullptr ? target : owner, evCommand, cmMrScopedHistoryChoose, this);
			clearEvent(event);
		} else if (event.what == evBroadcast)
			if ((event.message.command == cmReleasedFocus && event.message.infoPtr == link) || event.message.command == cmRecordHistory) recordHistory(link->data);
	}

	void recordHistory(const char *) override {
	}

	[[nodiscard]] TInputLine *linkedInput() const noexcept {
		return link;
	}

	[[nodiscard]] ScopedDialogHistoryKind historyKind() const noexcept {
		return kind;
	}

  private:
	ScopedDialogHistoryKind kind;
};

class TWheelFileDialog final : public TFileDialog {
  public:
	TWheelFileDialog(MRDialogHistoryScope aScope, const char *wildCard, const char *title, const char *inputName, ushort options) noexcept : TWindowInit(TFileDialog::initFrame), TFileDialog(wildCard, title, inputName, options, 0), scope(aScope), dialogOptions(options) {
		replaceHistoryView(static_cast<TInputLine *>(fileName), ScopedDialogHistoryKind::File);
	}

	void handleEvent(TEvent &event) override {
		if (historyDropList.visible()) {
			if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEsc) {
				hideHistoryList();
				clearEvent(event);
				return;
			}
			if (event.what == evMouseDown && !historyDropList.containsPoint(event.mouse.where) && (historyButton == nullptr || !historyButton->mouseInView(event.mouse.where))) {
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

	void replaceHistoryView(TInputLine *link, ScopedDialogHistoryKind kind) {
		if (link == nullptr) return;
		for (TView *child = first(); child != nullptr;) {
			TView *next = child->nextView();
			if (dynamic_cast<THistory *>(child) != nullptr) {
				const TRect childBounds = child->getBounds();
				const ushort childGrowMode = child->growMode;
				remove(child);
				TObject::destroy(child);
				ScopedHistoryButton *history = new ScopedHistoryButton(childBounds, link, kind);
				history->growMode = childGrowMode;
				historyButton = history;
				historyLink = link;
				insert(history);
				return;
			}
			child = next;
		}
	}

	MRDialogHistoryScope scope;
	ushort dialogOptions = 0;
	ScopedHistoryButton *historyButton = nullptr;
	TInputLine *historyLink = nullptr;
	MRDropList historyDropList;
};

class TWheelChDirDialog final : public TChDirDialog {
  public:
	TWheelChDirDialog(MRDialogHistoryScope aScope, ushort options) noexcept : TWindowInit(TChDirDialog::initFrame), TChDirDialog(options, 0), scope(aScope) {
		replaceHistoryView(findInputLine(TRect(3, 3, 42, 4)), ScopedDialogHistoryKind::Path);
	}

	void handleEvent(TEvent &event) override {
		if (historyDropList.visible()) {
			if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEsc) {
				hideHistoryList();
				clearEvent(event);
				return;
			}
			if (event.what == evMouseDown && !historyDropList.containsPoint(event.mouse.where) && (historyButton == nullptr || !historyButton->mouseInView(event.mouse.where))) {
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

	void replaceHistoryView(TInputLine *link, ScopedDialogHistoryKind kind) {
		if (link == nullptr) return;
		for (TView *child = first(); child != nullptr;) {
			TView *next = child->nextView();
			if (dynamic_cast<THistory *>(child) != nullptr) {
				const TRect childBounds = child->getBounds();
				const ushort childGrowMode = child->growMode;
				remove(child);
				TObject::destroy(child);
				ScopedHistoryButton *history = new ScopedHistoryButton(childBounds, link, kind);
				history->growMode = childGrowMode;
				historyButton = history;
				historyLink = link;
				insert(history);
				return;
			}
			child = next;
		}
	}

	MRDialogHistoryScope scope;
	ScopedHistoryButton *historyButton = nullptr;
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
