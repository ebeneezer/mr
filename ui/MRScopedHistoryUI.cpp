#define Uses_TChDirDialog
#define Uses_TDialog
#define Uses_TFileDialog
#define Uses_TFileInputLine
#define Uses_TFileList
#define Uses_THistory
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TListViewer
#define Uses_TObject
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TView
#include <tvision/tv.h>

#include "MRFrame.hpp"
#include "MRScopedHistoryUI.hpp"

#include "../config/MRDialogPaths.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {
TFrame *initScopedHistoryDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}

enum class ScopedDialogHistoryKind : unsigned char {
	File = 0,
	Path = 1
};

class ScopedHistoryListViewer final : public TListViewer {
  public:
	ScopedHistoryListViewer(const TRect &bounds, TScrollBar *aHScrollBar, TScrollBar *aVScrollBar, std::vector<std::string> aItems) noexcept : TListViewer(bounds, 1, aHScrollBar, aVScrollBar), items(std::move(aItems)) {
		setRange(static_cast<short>(items.size()));
		updateHorizontalRange();
	}

	void getText(char *dest, short item, short maxLen) override {
		std::size_t copyLen = 0;

		if (dest == nullptr || maxLen <= 0) return;
		if (item < 0 || static_cast<std::size_t>(item) >= items.size()) {
			dest[0] = EOS;
			return;
		}
		copyLen = static_cast<std::size_t>(maxLen - 1);
		std::strncpy(dest, items[static_cast<std::size_t>(item)].c_str(), copyLen);
		dest[copyLen] = EOS;
	}

	void handleEvent(TEvent &event) override {
		const bool doubleClick = event.what == evMouseDown && (event.mouse.buttons & mbLeftButton) != 0 && (event.mouse.eventFlags & meDoubleClick) != 0;

		TListViewer::handleEvent(event);
		if (doubleClick && focused >= 0 && focused < range) {
			endModal(cmOK);
			clearEvent(event);
			return;
		}
		if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter && focused >= 0 && focused < range) {
			endModal(cmOK);
			clearEvent(event);
		}
	}

	std::string selectedValue() const {
		if (focused < 0 || static_cast<std::size_t>(focused) >= items.size()) return std::string();
		return items[static_cast<std::size_t>(focused)];
	}

  private:
	void updateHorizontalRange() {
		int width = 0;

		for (const std::string &item : items)
			width = std::max(width, cstrlen(item.c_str()));
		if (hScrollBar != nullptr) hScrollBar->setRange(0, std::max(0, width - size.x + 1));
	}

	std::vector<std::string> items;
};

class ScopedHistoryPopupDialog final : public TDialog {
  public:
	ScopedHistoryPopupDialog(const TRect &bounds, std::vector<std::string> items) noexcept : TWindowInit(initScopedHistoryDialogFrame), TDialog(bounds, ""), scopedViewer(nullptr) {
		flags = wfClose;
		TRect viewerBounds = getExtent();
		viewerBounds.grow(-1, -1);
		scopedViewer = new ScopedHistoryListViewer(viewerBounds, standardScrollBar(sbHorizontal | sbHandleKeyboard), standardScrollBar(sbVertical | sbHandleKeyboard), std::move(items));
		insert(scopedViewer);
		selectNext(False);
	}

	TPalette &getPalette() const override {
		static const TPalette palette = []() -> TPalette {
			TColorAttr data[29];

			for (int i = 0; i < 29; ++i)
				data[i] = static_cast<TColorAttr>(i + 1);
			data[3] = 24;
			data[4] = 25;
			return TPalette(data, 29);
		}();
		return const_cast<TPalette &>(palette);
	}

	void handleEvent(TEvent &event) override {
		TDialog::handleEvent(event);
		if (event.what == evMouseDown && !mouseInView(event.mouse.where)) {
			endModal(cmCancel);
			clearEvent(event);
		}
	}

	std::string selectedValue() const {
		return scopedViewer != nullptr ? scopedViewer->selectedValue() : std::string();
	}

  private:
	ScopedHistoryListViewer *scopedViewer;
};

class ScopedHistoryButton final : public THistory {
  public:
	ScopedHistoryButton(const TRect &bounds, TInputLine *aLink, MRDialogHistoryScope aScope, ScopedDialogHistoryKind aKind) noexcept : THistory(bounds, aLink, aKind == ScopedDialogHistoryKind::File ? kFileDialogHistoryId : kPathDialogHistoryId), scope(aScope), kind(aKind) {
	}

	void handleEvent(TEvent &event) override {
		ScopedHistoryPopupDialog *historyDialog = nullptr;
		TRect r, p;
		ushort command = 0;

		TView::handleEvent(event);
		if (event.what == evMouseDown || (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbDown && (link->state & sfFocused) != 0)) {
			if (!link->focus()) {
				clearEvent(event);
				return;
			}
			recordHistory(link->data);
			r = link->getBounds();
			r.a.x--;
			r.b.x++;
			r.b.y += 7;
			r.a.y--;
			p = owner->getExtent();
			r.intersect(p);
			r.b.y--;
			historyDialog = initHistoryDialog(r);
			if (historyDialog != nullptr) {
				command = owner->execView(historyDialog);
				if (command == cmOK) {
					const std::string value = historyDialog->selectedValue();
					strnzcpy(link->data, value.c_str(), link->maxLen + 1);
					link->selectAll(True);
					link->drawView();
				}
				destroy(historyDialog);
			}
			clearEvent(event);
		} else if (event.what == evBroadcast)
			if ((event.message.command == cmReleasedFocus && event.message.infoPtr == link) || event.message.command == cmRecordHistory) recordHistory(link->data);
	}

	void recordHistory(const char *) override {
	}

  private:
	ScopedHistoryPopupDialog *initHistoryDialog(const TRect &bounds) {
		std::vector<std::string> entries;

		if (kind == ScopedDialogHistoryKind::File) configuredScopedDialogFileHistoryEntries(scope, entries);
		else
			configuredScopedDialogPathHistoryEntries(scope, entries);
		if (entries.empty()) return nullptr;
		return new ScopedHistoryPopupDialog(bounds, std::move(entries));
	}

	MRDialogHistoryScope scope;
	ScopedDialogHistoryKind kind;
};

class TWheelFileDialog final : public TFileDialog {
  public:
	TWheelFileDialog(MRDialogHistoryScope aScope, const char *wildCard, const char *title, const char *inputName, ushort options) noexcept : TWindowInit(TFileDialog::initFrame), TFileDialog(wildCard, title, inputName, options, 0), scope(aScope), dialogOptions(options) {
		replaceHistoryView(static_cast<TInputLine *>(fileName), ScopedDialogHistoryKind::File);
	}

	void handleEvent(TEvent &event) override {
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
	void replaceHistoryView(TInputLine *link, ScopedDialogHistoryKind kind) {
		if (link == nullptr) return;
		for (TView *child = first(); child != nullptr;) {
			TView *next = child->nextView();
			if (dynamic_cast<THistory *>(child) != nullptr) {
				const TRect childBounds = child->getBounds();
				const ushort childGrowMode = child->growMode;
				remove(child);
				TObject::destroy(child);
				ScopedHistoryButton *history = new ScopedHistoryButton(childBounds, link, scope, kind);
				history->growMode = childGrowMode;
				insert(history);
				return;
			}
			child = next;
		}
	}

	MRDialogHistoryScope scope;
	ushort dialogOptions = 0;
};

class TWheelChDirDialog final : public TChDirDialog {
  public:
	TWheelChDirDialog(MRDialogHistoryScope aScope, ushort options) noexcept : TWindowInit(TChDirDialog::initFrame), TChDirDialog(options, 0), scope(aScope) {
		replaceHistoryView(findInputLine(TRect(3, 3, 42, 4)), ScopedDialogHistoryKind::Path);
	}

  private:
	TInputLine *findInputLine(const TRect &bounds) {
		for (TView *child = first(); child != nullptr; child = child->nextView())
			if (child->getBounds() == bounds) return dynamic_cast<TInputLine *>(child);
		return nullptr;
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
				ScopedHistoryButton *history = new ScopedHistoryButton(childBounds, link, scope, kind);
				history->growMode = childGrowMode;
				insert(history);
				return;
			}
			child = next;
		}
	}

	MRDialogHistoryScope scope;
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
