#include "MRMacroModelessUi.hpp"
#include "MRMacroModelessCanvas.hpp"
#include "MRMacroModelessControls.hpp"
#include "../../MRMacroRunner.hpp"
#include "../../MRVM.hpp"

#include "../../../app/MRCommands.hpp"
#include "../../../app/MRRuntimeScheduler.hpp"
#include "../../../app/commands/MRWindowCommands.hpp"
#include "../../../dialogs/MRWindowList.hpp"
#include "../../../dialogs/setup/MRSetupCommon.hpp"
#include "../../../ui/MRDesktopWindow.hpp"
#include "../../../ui/MRWindowLayout.hpp"
#include "MRVMModelessUiRuntime.hpp"

#define Uses_TButton
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TObject
#define Uses_TProgram
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TStaticText
#define Uses_TWindow
#include <tvision/tv.h>

#include "../../../ui/MRFrame.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <utility>

namespace {

constexpr ushort cmMacroModelessBase = 0x6F20;
constexpr ushort cmMacroModelessMax = 0x6FFF;

struct MRMacroModelessButtonCaption {
	std::string displayLabel;
	std::vector<ushort> hotKeys;
};

MRMacroModelessListResolver g_listResolver = nullptr;
MRMacroModelessCommandRunner g_commandRunner = nullptr;
std::map<std::string, class MRMacroModelessWindow *> g_windows;

static std::string trimCaption(const std::string &value) {
	std::size_t first = 0;
	std::size_t last = value.size();

	while (first < last && std::isspace(static_cast<unsigned char>(value[first])))
		++first;
	while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])))
		--last;
	return value.substr(first, last - first);
}

static std::string upperCaptionToken(const std::string &value) {
	std::string result(value);

	for (std::size_t index = 0; index < result.size(); ++index)
		result[index] = static_cast<char>(std::toupper(static_cast<unsigned char>(result[index])));
	return result;
}

static ushort namedHotKeyCode(const std::string &name) noexcept {
	struct NamedKey {
		const char *name;
		ushort code;
	};
	static const NamedKey namedKeys[] = {
	    {"ENTER", kbEnter}, {"ESC", kbEsc}, {"ESCAPE", kbEsc}, {"TAB", kbTab}, {"F1", kbF1}, {"F2", kbF2}, {"F3", kbF3}, {"F4", kbF4}, {"F5", kbF5}, {"F6", kbF6}, {"F7", kbF7}, {"F8", kbF8}, {"F9", kbF9}, {"F10", kbF10}, {"F11", kbF11}, {"F12", kbF12},
	};
	const std::size_t namedKeyCount = sizeof(namedKeys) / sizeof(namedKeys[0]);
	const std::string key = upperCaptionToken(name);

	for (std::size_t index = 0; index < namedKeyCount; ++index)
		if (key == namedKeys[index].name) return namedKeys[index].code;
	return 0;
}

static MRMacroModelessButtonCaption parseButtonCaption(const std::string &text) {
	MRMacroModelessButtonCaption entry;
	std::size_t index = 0;
	const std::string label = trimCaption(text);

	while (index < label.size()) {
		if (label[index] == '~') {
			const std::size_t close = label.find('~', index + 1);
			if (close != std::string::npos && close > index + 1) {
				entry.displayLabel += label.substr(index + 1, close - index - 1);
				entry.hotKeys.push_back(static_cast<ushort>(std::toupper(static_cast<unsigned char>(label[index + 1]))));
				index = close + 1;
				continue;
			}
		}
		if (label[index] == '<') {
			const std::size_t close = label.find('>', index + 1);
			if (close != std::string::npos) {
				const ushort keyCode = namedHotKeyCode(trimCaption(label.substr(index + 1, close - index - 1)));
				if (keyCode != 0) entry.hotKeys.push_back(keyCode);
				index = close + 1;
				continue;
			}
		}
		entry.displayLabel.push_back(label[index]);
		++index;
	}
	if (entry.displayLabel.size() == 1) entry.hotKeys.push_back(static_cast<ushort>(std::toupper(static_cast<unsigned char>(entry.displayLabel.front()))));
	if (entry.displayLabel.empty()) entry.displayLabel = " ";
	return entry;
}

static std::vector<std::string> resolveListItems(const std::string &itemSpec) {
	if (g_listResolver == nullptr) return std::vector<std::string>();
	return g_listResolver(itemSpec);
}

class MRMacroModelessDisplayLine final : public TView {
  public:
	MRMacroModelessDisplayLine(const TRect &bounds, std::string text) noexcept : TView(bounds), text(std::move(text)) {
		options &= ~(ofSelectable | ofFirstClick);
		eventMask &= static_cast<ushort>(~(evMouseDown | evMouseUp | evMouseMove | evKeyDown));
	}

	void draw() override {
		TDrawBuffer buffer;
		const TColorAttr color = getColor(1);
		const int width = size.x;
		const std::string value = text.empty() ? std::string(" ") : text;

		buffer.moveChar(0, ' ', color, static_cast<ushort>(width));
		if (width > 0) buffer.moveStr(0, value.c_str(), color, width);
		writeLine(0, 0, width, 1, buffer);
	}

	void setText(std::string value) {
		if (text == value) return;
		text = std::move(value);
		drawView();
	}

  private:
	std::string text;
};

static TRect modelessBounds(const MRMacroModelessWindowDefinition &definition) {
	TRect desk = TProgram::deskTop != nullptr ? TProgram::deskTop->getExtent() : TRect(0, 0, 80, 25);
	int windowWidth = std::min(std::max(24, definition.width), desk.b.x - desk.a.x);
	int windowHeight = std::min(std::max(8, definition.height), desk.b.y - desk.a.y);
	int left = desk.a.x + (desk.b.x - desk.a.x - windowWidth) / 2;
	int top = desk.a.y + (desk.b.y - desk.a.y - windowHeight) / 2;

	if (definition.x > 0) left = std::clamp(desk.a.x + definition.x - 1, desk.a.x, desk.b.x - windowWidth);
	if (definition.y > 0) top = std::clamp(desk.a.y + definition.y - 1, desk.a.y, desk.b.y - windowHeight);
	return TRect(left, top, left + windowWidth, top + windowHeight);
}

static TFrame *initMacroModelessFrame(TRect bounds);

class MRMacroModelessWindow final : public TWindow, public MRDesktopWindow {
	public:
	explicit MRMacroModelessWindow(const MRMacroModelessWindowDefinition &windowDefinition) : TWindowInit(initMacroModelessFrame), TWindow(modelessBounds(windowDefinition), windowDefinition.title.empty() ? "MRMac" : windowDefinition.title.c_str(), wnNoNumber), definition(windowDefinition) {
		TRect contentBounds = getExtent();

		contentBounds.grow(-1, -1);
		contentGroup = createSetupDialogContentGroup(contentBounds);
		if (contentGroup != nullptr) {
			contentGroup->options |= ofSelectable;
			contentGroup->growMode = gfGrowHiX | gfGrowHiY;
			insert(contentGroup);
		}
		buildControls();
	}

	TPalette &getPalette() const override {
		static TPalette palette(cpGrayDialog, sizeof(cpGrayDialog) - 1);
		return palette;
	}

	TWindow *desktopNativeWindow() override {
		return this;
	}

	const TWindow *desktopNativeWindow() const override {
		return this;
	}

	int desktopIndex() const override {
		MRMacroModelessWindowDesktopState desktopState;

		return mrvmReadModelessWindowDesktopState(definition.windowId, desktopState) ? desktopState.virtualDesktop : 1;
	}

	void setDesktopIndex(int index) override {
		MRMacroModelessWindowDesktopState desktopState;

		if (!mrvmReadModelessWindowDesktopState(definition.windowId, desktopState)) return;
		desktopState.virtualDesktop = std::max(1, index);
		desktopState.assigned = true;
		mrvmStoreModelessWindowDesktopState(definition.windowId, desktopState);
	}

	bool desktopManuallyHidden() const override {
		MRMacroModelessWindowDesktopState desktopState;

		return mrvmReadModelessWindowDesktopState(definition.windowId, desktopState) && desktopState.manuallyHidden;
	}

	void setDesktopManuallyHidden(bool hidden) override {
		MRMacroModelessWindowDesktopState desktopState;

		if (!mrvmReadModelessWindowDesktopState(definition.windowId, desktopState)) return;
		desktopState.manuallyHidden = hidden;
		desktopState.assigned = true;
		mrvmStoreModelessWindowDesktopState(definition.windowId, desktopState);
	}

	bool desktopMinimized() const override {
		MRMacroModelessWindowDesktopState desktopState;

		return mrvmReadModelessWindowDesktopState(definition.windowId, desktopState) && desktopState.minimized;
	}

	void readDesktopMinimizedState(MRDesktopMinimizedState &state) const override {
		MRMacroModelessWindowDesktopState desktopState;

		state = MRDesktopMinimizedState();
		if (!mrvmReadModelessWindowDesktopState(definition.windowId, desktopState)) return;
		state.minimized = desktopState.minimized;
		state.bufferedBeforeMinimize = desktopState.bufferedBeforeMinimize;
		state.restoreBounds = TRect(desktopState.restoreX, desktopState.restoreY, desktopState.restoreX + desktopState.restoreWidth, desktopState.restoreY + desktopState.restoreHeight);
		state.lastMinimizedBounds = TRect(desktopState.lastMinimizedX, desktopState.lastMinimizedY, desktopState.lastMinimizedX + desktopState.lastMinimizedWidth, desktopState.lastMinimizedY + desktopState.lastMinimizedHeight);
	}

	void storeDesktopMinimizedState(const MRDesktopMinimizedState &state) override {
		MRMacroModelessWindowDesktopState desktopState;

		if (!mrvmReadModelessWindowDesktopState(definition.windowId, desktopState)) return;
		desktopState.minimized = state.minimized;
		desktopState.bufferedBeforeMinimize = state.bufferedBeforeMinimize;
		desktopState.restoreX = state.restoreBounds.a.x;
		desktopState.restoreY = state.restoreBounds.a.y;
		desktopState.restoreWidth = state.restoreBounds.b.x - state.restoreBounds.a.x;
		desktopState.restoreHeight = state.restoreBounds.b.y - state.restoreBounds.a.y;
		desktopState.lastMinimizedX = state.lastMinimizedBounds.a.x;
		desktopState.lastMinimizedY = state.lastMinimizedBounds.a.y;
		desktopState.lastMinimizedWidth = state.lastMinimizedBounds.b.x - state.lastMinimizedBounds.a.x;
		desktopState.lastMinimizedHeight = state.lastMinimizedBounds.b.y - state.lastMinimizedBounds.a.y;
		desktopState.assigned = true;
		mrvmStoreModelessWindowDesktopState(definition.windowId, desktopState);
	}

	const char *desktopMinimizedTitle() const override {
		return const_cast<MRMacroModelessWindow *>(this)->getTitle(0);
	}

	void layoutDesktopContents() override {
	}

	void synchronizeDesktopContents() override {
	}

	void restoreDesktopWindow() override {
		MRWindowLayout::restoreWindow(this);
	}

	void applyDesktopBounds(const TRect &bounds) override {
		freeBuffer();
		setBounds(bounds);
		clip = getExtent();
		if (frame != nullptr) frame->setBounds(getExtent());
		if (contentGroup != nullptr) {
			TRect contentBounds = getExtent();

			contentBounds.grow(-1, -1);
			contentGroup->locate(contentBounds);
		}
		storeLiveGeometry();
	}

	bool desktopShowsFrameGrowHandle() const override {
		return false;
	}

	void dragView(TEvent &event, uchar mode, TRect &limits, TPoint minSize, TPoint maxSize) override {
		MRWindowLayout::handleDragView(this, event, mode, limits, minSize, maxSize);
	}

	void close() override {
		MRMacroExecutionOwner executionOwner;
		std::map<std::string, MRMacroModelessWindow *>::iterator it = g_windows.find(definition.windowId);

		executionOwner.modelessWindowId = definition.windowId;
		removeRuntimeScheduledConsumersForOwner(executionOwner);
		requestMacroExecutionCancellationForOwner(executionOwner);
		if (it != g_windows.end() && it->second == this) g_windows.erase(it);
		mrvmRemoveModelessWindowDefinition(definition.windowId);
		mrNotifyWindowTopologyChanged();
		TWindow::close();
	}

	void changeBounds(const TRect &bounds) override {
		const TRect previousBounds = getBounds();

		TWindow::changeBounds(bounds);
		if (previousBounds != getBounds()) storeLiveGeometry();
	}

	void handleEvent(TEvent &event) override {
		const TRect previousBounds = getBounds();
		if (event.what == evMouseDown) {
			if (owner != nullptr) makeFirst();
			if (TProgram::deskTop != nullptr) TProgram::deskTop->setCurrent(this, TView::normalSelect);
			else
				select();
			if (frame != nullptr) frame->drawView();
			const TPoint localMouse = makeLocal(event.mouse.where);
			if ((event.mouse.buttons & mbLeftButton) != 0 && (contentGroup == nullptr || contentGroup->getBounds().contains(localMouse)) && runCanvasHotspot(localMouse)) {
				clearEvent(event);
				return;
			}
		}
		if (desktopMinimized()) {
			if (event.what == evCommand && (event.message.command == cmMrWindowMinimize || event.message.command == cmMrWindowRestore || event.message.command == cmZoom)) {
				MRWindowLayout::restoreWindow(this);
				clearEvent(event);
				return;
			}
			if (event.what == evCommand && event.message.command == cmResize) {
				MRWindowLayout::reinsertMinimizedWindow(this);
				clearEvent(event);
				return;
			}
		}

		if (event.what == evKeyDown) {
			const unsigned char typedChar = static_cast<unsigned char>(event.keyDown.charScan.charCode);
			const ushort hotKey = typedChar >= 32 ? static_cast<ushort>(std::toupper(typedChar)) : event.keyDown.keyCode;

			for (std::size_t index = 0; index < hotKeys.size(); ++index)
				if (hotKeys[index].first == hotKey) {
					runButton(hotKeys[index].second);
					clearEvent(event);
					return;
				}
		}

		TWindow::handleEvent(event);
		if (previousBounds != getBounds()) storeLiveGeometry();
		if (event.what == evCommand) {
			std::map<ushort, int>::const_iterator buttonIt = commandToButton.find(event.message.command);
			if (buttonIt != commandToButton.end()) {
				runButton(buttonIt->second);
				clearEvent(event);
				return;
			}
			std::map<ushort, int>::const_iterator listIt = commandToList.find(event.message.command);
			if (listIt != commandToList.end()) {
				selection = readSelection(listIt->second);
				clearEvent(event);
				return;
			}
			std::map<ushort, int>::const_iterator gridIt = commandToGrid.find(event.message.command);
			if (gridIt != commandToGrid.end()) {
				std::map<int, MRMacroModelessGridSpec>::const_iterator specIt = gridSpecs.find(gridIt->second);

				selection = readSelection(gridIt->second);
				if (specIt != gridSpecs.end()) runModelessMacro(definition.windowId, specIt->second.macroSpec);
				clearEvent(event);
				return;
			}
			std::map<ushort, int>::const_iterator treeIt = commandToTree.find(event.message.command);
			if (treeIt != commandToTree.end()) {
				std::map<int, MRMacroModelessTreeSpec>::const_iterator specIt = treeSpecs.find(treeIt->second);

				selection = readSelection(treeIt->second);
				if (specIt != treeSpecs.end()) runModelessMacro(definition.windowId, specIt->second.macroSpec);
				clearEvent(event);
				return;
			}
			std::map<ushort, int>::const_iterator tableIt = commandToTable.find(event.message.command);
			if (tableIt != commandToTable.end()) {
				std::map<int, MRMacroModelessTableSpec>::const_iterator specIt = tableSpecs.find(tableIt->second);

				selection = readSelection(tableIt->second);
				if (specIt != tableSpecs.end()) runModelessMacro(definition.windowId, specIt->second.macroSpec);
				clearEvent(event);
				return;
			}
		}
	}

	void refreshSelectionControls() {
		refreshDisplays();
		for (std::map<int, TView *>::iterator entry = listViews.begin(); entry != listViews.end(); ++entry) {
			std::map<int, MRMacroModelessListBoxSpec>::const_iterator specIt = listSpecs.find(entry->first);
			TView *listView = entry->second;
			const int oldIndex = macroUiListSelectedIndex(listView);

			if (listView == nullptr || specIt == listSpecs.end()) continue;
			setMacroUiListItems(listView, resolveListItems(specIt->second.itemSpec), std::max(1, oldIndex));
		}
		for (std::map<int, TView *>::iterator entry = gridViews.begin(); entry != gridViews.end(); ++entry) {
			std::map<int, MRMacroModelessGridSpec>::const_iterator specIt = gridSpecs.find(entry->first);
			TView *gridView = entry->second;

			if (gridView == nullptr || specIt == gridSpecs.end()) continue;
			refreshMacroUiGridItems(gridView, resolveListItems(specIt->second.itemSpec));
		}
		for (std::map<int, TView *>::iterator entry = treeViews.begin(); entry != treeViews.end(); ++entry) {
			std::map<int, MRMacroModelessTreeSpec>::const_iterator specIt = treeSpecs.find(entry->first);

			if (entry->second == nullptr || specIt == treeSpecs.end()) continue;
			refreshMacroUiTreeItems(entry->second, resolveListItems(specIt->second.itemSpec));
		}
		for (std::map<int, TView *>::iterator entry = tableViews.begin(); entry != tableViews.end(); ++entry) {
			std::map<int, MRMacroModelessTableSpec>::const_iterator specIt = tableSpecs.find(entry->first);

			if (entry->second == nullptr || specIt == tableSpecs.end()) continue;
			refreshMacroUiTableItems(entry->second, resolveListItems(specIt->second.itemSpec));
		}
		if (selection.controlId != 0) selection = readSelection(selection.controlId);
	}

	void updateDefinition(const MRMacroModelessWindowDefinition &nextDefinition) {
		definition = nextDefinition;
		refreshSelectionControls();
		refreshTextFields();
		refreshBoolFields();
		refreshIntFields();
		refreshProgressFields();
		refreshLogFields();
		refreshSelectFields();
		storeLiveGeometry();
	}

	bool updateDisplay(int displayIndex, const std::string &text) {
		const std::size_t index = static_cast<std::size_t>(displayIndex - 1);

		if (displayIndex <= 0 || index >= displayViews.size() || index >= definition.displays.size()) return false;
		definition.displays[index].text = text;
		if (displayViews[index] != nullptr) displayViews[index]->setText(text);
		return true;
	}

	bool updateTextField(const std::string &fieldId, const std::string &text) {
		std::map<std::string, TView *>::iterator viewIt = textFieldViews.find(fieldId);

		if (viewIt == textFieldViews.end() || viewIt->second == nullptr) return false;
		for (std::size_t index = 0; index < definition.textFields.size(); ++index)
			if (definition.textFields[index].fieldId == fieldId) {
				definition.textFields[index].text = text;
				return setMacroModelessTextInputValue(viewIt->second, text);
			}
		return false;
	}

	bool updateBoolField(const std::string &fieldId, bool value) {
		std::map<std::string, TView *>::iterator viewIt = boolFieldViews.find(fieldId);

		if (viewIt == boolFieldViews.end() || viewIt->second == nullptr) return false;
		for (std::size_t index = 0; index < definition.boolFields.size(); ++index)
			if (definition.boolFields[index].fieldId == fieldId) {
				definition.boolFields[index].value = value;
				return setMacroModelessBoolInputValue(viewIt->second, value);
			}
		return false;
	}

	bool updateIntField(const std::string &fieldId, int value) {
		std::map<std::string, TView *>::iterator viewIt = intFieldViews.find(fieldId);

		if (viewIt == intFieldViews.end() || viewIt->second == nullptr) return false;
		for (std::size_t index = 0; index < definition.intFields.size(); ++index)
			if (definition.intFields[index].fieldId == fieldId) {
				definition.intFields[index].value = value;
				return setMacroModelessIntInputValue(viewIt->second, value);
			}
		return false;
	}

	bool updateProgressField(const std::string &fieldId) {
		std::map<std::string, TView *>::iterator viewIt = progressFieldViews.find(fieldId);

		if (viewIt == progressFieldViews.end() || viewIt->second == nullptr) return false;
		redrawMacroModelessProgressView(viewIt->second);
		return true;
	}

	bool updateLogField(const std::string &logId) {
		std::map<std::string, TView *>::iterator viewIt = logFieldViews.find(logId);

		if (viewIt == logFieldViews.end() || viewIt->second == nullptr) return false;
		redrawMacroModelessLogView(viewIt->second);
		return true;
	}

	bool updateSelectField(const std::string &fieldId, const std::string &value) {
		std::map<std::string, TView *>::iterator viewIt = selectFieldViews.find(fieldId);

		if (viewIt == selectFieldViews.end() || viewIt->second == nullptr || !setMacroModelessSelectInputValue(viewIt->second, value)) return false;
		for (std::size_t index = 0; index < definition.selectFields.size(); ++index)
			if (definition.selectFields[index].fieldId == fieldId) {
				definition.selectFields[index].value = value;
				return true;
			}
		return false;
	}

	bool commitCanvas(const std::string &canvasId) {
		std::map<std::string, TView *>::iterator it = canvasViews.find(canvasId);

		if (it == canvasViews.end() || it->second == nullptr) return false;
		redrawMacroModelessCanvasView(it->second);
		return true;
	}

	void storeLiveGeometry() {
		const TRect bounds = getBounds();

		mrvmStoreModelessWindowLiveGeometry(definition.windowId, bounds.a.x, bounds.a.y, bounds.b.x - bounds.a.x, bounds.b.y - bounds.a.y);
	}

  private:
	void insertContentControl(TView *view) {
		if (view == nullptr) return;
		if (contentGroup == nullptr) {
			insert(view);
			return;
		}
		TRect bounds = view->getBounds();

		bounds.move(-contentGroup->origin.x, -contentGroup->origin.y);
		view->locate(bounds);
		contentGroup->insert(view);
	}

	void buildControls() {
		ushort nextCommand = cmMacroModelessBase;

		options |= ofTileable;
		for (std::size_t labelIndex = 0; labelIndex < definition.labels.size(); ++labelIndex) {
			const MRMacroModelessLabelSpec &label = definition.labels[labelIndex];

			insertContentControl(new TStaticText(TRect(label.x, label.y, label.x + strwidth(label.text.c_str()), label.y + 1), label.text.c_str()));
		}

		for (std::size_t displayIndex = 0; displayIndex < definition.displays.size(); ++displayIndex) {
			const MRMacroModelessDisplaySpec &display = definition.displays[displayIndex];
			MRMacroModelessDisplayLine *displayLine = new MRMacroModelessDisplayLine(TRect(display.x, display.y, display.x + display.width, display.y + 1), display.text);

			insertContentControl(displayLine);
			displayViews.push_back(displayLine);
		}

		for (std::size_t textFieldIndex = 0; textFieldIndex < definition.textFields.size(); ++textFieldIndex) {
			const MRMacroModelessTextFieldSpec &textField = definition.textFields[textFieldIndex];
			const std::string labelText = textField.label + ":";
			TView *input;

			if (textField.fieldId.empty()) continue;
			insertContentControl(new TStaticText(TRect(textField.x, textField.y, textField.x + strwidth(labelText.c_str()), textField.y + 1), labelText.c_str()));
			input = createMacroModelessTextInput(TRect(textField.x + strwidth(labelText.c_str()) + 1, textField.y, textField.x + strwidth(labelText.c_str()) + 1 + textField.width, textField.y + 1), textField.width, definition.windowId, textField.fieldId, textField.text);
			if (input == nullptr) continue;
			insertContentControl(input);
			textFieldViews[textField.fieldId] = input;
		}

		for (std::size_t boolFieldIndex = 0; boolFieldIndex < definition.boolFields.size(); ++boolFieldIndex) {
			const MRMacroModelessBoolFieldSpec &boolField = definition.boolFields[boolFieldIndex];
			TView *input;

			if (boolField.fieldId.empty() || boolField.caption.empty()) continue;
			input = createMacroModelessBoolInput(TRect(boolField.x, boolField.y, boolField.x + strwidth(boolField.caption.c_str()) + 5, boolField.y + 1), definition.windowId, boolField.fieldId, boolField.caption, boolField.value);
			if (input == nullptr) continue;
			insertContentControl(input);
			boolFieldViews[boolField.fieldId] = input;
		}

		for (std::size_t intFieldIndex = 0; intFieldIndex < definition.intFields.size(); ++intFieldIndex) {
			const MRMacroModelessIntFieldSpec &intField = definition.intFields[intFieldIndex];
			const std::string labelText = intField.label + ":";
			TView *input;

			if (intField.fieldId.empty() || intField.label.empty()) continue;
			insertContentControl(new TStaticText(TRect(intField.x, intField.y, intField.x + strwidth(labelText.c_str()), intField.y + 1), labelText.c_str()));
			input = createMacroModelessIntInput(TRect(intField.x + strwidth(labelText.c_str()) + 1, intField.y, intField.x + strwidth(labelText.c_str()) + 1 + intField.width, intField.y + 1), intField.width, definition.windowId, intField.fieldId, intField.value);
			if (input == nullptr) continue;
			insertContentControl(input);
			intFieldViews[intField.fieldId] = input;
		}

		for (std::size_t progressFieldIndex = 0; progressFieldIndex < definition.progressFields.size(); ++progressFieldIndex) {
			const MRMacroModelessProgressFieldSpec &progressField = definition.progressFields[progressFieldIndex];
			const std::string labelText = progressField.label + ":";
			TView *view;

			if (progressField.fieldId.empty() || progressField.label.empty()) continue;
			insertContentControl(new TStaticText(TRect(progressField.x, progressField.y, progressField.x + strwidth(labelText.c_str()), progressField.y + 1), labelText.c_str()));
			view = createMacroModelessProgressView(TRect(progressField.x + strwidth(labelText.c_str()) + 1, progressField.y, progressField.x + strwidth(labelText.c_str()) + 1 + progressField.width, progressField.y + 1), definition.windowId, progressField.fieldId);
			if (view == nullptr) continue;
			insertContentControl(view);
			progressFieldViews[progressField.fieldId] = view;
		}

		for (std::size_t logFieldIndex = 0; logFieldIndex < definition.logFields.size(); ++logFieldIndex) {
			const MRMacroModelessLogFieldSpec &logField = definition.logFields[logFieldIndex];
			TView *view;

			if (logField.logId.empty() || logField.label.empty()) continue;
			insertContentControl(new TStaticText(TRect(logField.x, logField.y, logField.x + strwidth(logField.label.c_str()), logField.y + 1), logField.label.c_str()));
			view = createMacroModelessLogView(TRect(logField.x, logField.y + 1, logField.x + logField.width, logField.y + 1 + logField.height), definition.windowId, logField.logId);
			if (view == nullptr) continue;
			insertContentControl(view);
			logFieldViews[logField.logId] = view;
		}

		for (std::size_t selectFieldIndex = 0; selectFieldIndex < definition.selectFields.size(); ++selectFieldIndex) {
			const MRMacroModelessSelectFieldSpec &selectField = definition.selectFields[selectFieldIndex];
			TScrollBar *scrollBar;
			TView *input;

			if (selectField.fieldId.empty() || selectField.label.empty()) continue;
			insertContentControl(new TStaticText(TRect(selectField.x, selectField.y, selectField.x + strwidth(selectField.label.c_str()), selectField.y + 1), selectField.label.c_str()));
			scrollBar = new TScrollBar(TRect(selectField.x + selectField.width - 1, selectField.y + 1, selectField.x + selectField.width, selectField.y + 1 + selectField.height));
			insertContentControl(scrollBar);
			input = createMacroModelessSelectInput(TRect(selectField.x, selectField.y + 1, selectField.x + selectField.width - 1, selectField.y + 1 + selectField.height), scrollBar, selectField.options, definition.windowId, selectField.fieldId, selectField.value);
			if (input == nullptr) continue;
			insertContentControl(input);
			selectFieldViews[selectField.fieldId] = input;
		}

		for (std::size_t canvasIndex = 0; canvasIndex < definition.canvases.size(); ++canvasIndex) {
			const MRMacroModelessCanvasSpec &canvas = definition.canvases[canvasIndex];
			TView *canvasView;

			if (canvas.canvasId.empty()) continue;
			canvasView = createMacroModelessCanvasView(TRect(canvas.x, canvas.y, canvas.x + canvas.width, canvas.y + canvas.height), definition.windowId, canvas.canvasId);
			if (canvasView == nullptr) continue;
			insertContentControl(canvasView);
			canvasViews[canvas.canvasId] = canvasView;
		}

		for (std::size_t listBoxIndex = 0; listBoxIndex < definition.listBoxes.size(); ++listBoxIndex) {
			const MRMacroModelessListBoxSpec &listBox = definition.listBoxes[listBoxIndex];
			const std::vector<std::string> items = resolveListItems(listBox.itemSpec);
			const int listTop = listBox.label.empty() ? listBox.y : listBox.y + 1;
			TScrollBar *scrollBar = new TScrollBar(TRect(listBox.x + listBox.width - 1, listTop, listBox.x + listBox.width, listTop + listBox.height));
			TView *listView = nullptr;

			if (nextCommand >= cmMacroModelessMax) break;
			if (!listBox.label.empty()) insertContentControl(new TStaticText(TRect(listBox.x, listBox.y, listBox.x + strwidth(listBox.label.c_str()), listBox.y + 1), listBox.label.c_str()));
			insertContentControl(scrollBar);
			listView = createMacroUiListView(TRect(listBox.x, listTop, listBox.x + listBox.width - 1, listTop + listBox.height), scrollBar, items, nextCommand);
			insertContentControl(listView);
			setMacroUiListItems(listView, items, listBox.start);
			commandToList[nextCommand] = listBox.id;
			listSpecs[listBox.id] = listBox;
			listViews[listBox.id] = listView;
			if (selection.controlId == 0) selection = readSelection(listBox.id);
			++nextCommand;
		}

		for (std::size_t gridIndex = 0; gridIndex < definition.grids.size(); ++gridIndex) {
			const MRMacroModelessGridSpec &grid = definition.grids[gridIndex];
			const std::vector<std::string> items = resolveListItems(grid.itemSpec);
			const int gridTop = grid.label.empty() ? grid.y : grid.y + 1;
			TScrollBar *scrollBar = new TScrollBar(TRect(grid.x + grid.width - 1, gridTop, grid.x + grid.width, gridTop + grid.height));
			TView *gridView = nullptr;

			if (nextCommand >= cmMacroModelessMax) break;
			if (!grid.label.empty()) insertContentControl(new TStaticText(TRect(grid.x, grid.y, grid.x + strwidth(grid.label.c_str()), grid.y + 1), grid.label.c_str()));
			insertContentControl(scrollBar);
			gridView = createMacroUiGridView(TRect(grid.x, gridTop, grid.x + grid.width - 1, gridTop + grid.height), scrollBar, items, nextCommand);
			insertContentControl(gridView);
			setMacroUiGridItems(gridView, items, grid.start);
			commandToGrid[nextCommand] = grid.id;
			gridSpecs[grid.id] = grid;
			gridViews[grid.id] = gridView;
			if (selection.controlId == 0) selection = readSelection(grid.id);
			++nextCommand;
		}

		for (std::size_t treeIndex = 0; treeIndex < definition.trees.size(); ++treeIndex) {
			const MRMacroModelessTreeSpec &tree = definition.trees[treeIndex];
			const std::vector<std::string> items = resolveListItems(tree.itemSpec);
			const int treeTop = tree.label.empty() ? tree.y : tree.y + 1;
			TScrollBar *scrollBar = new TScrollBar(TRect(tree.x + tree.width - 1, treeTop, tree.x + tree.width, treeTop + tree.height));
			TView *treeView = nullptr;

			if (nextCommand >= cmMacroModelessMax) break;
			if (!tree.label.empty()) insertContentControl(new TStaticText(TRect(tree.x, tree.y, tree.x + strwidth(tree.label.c_str()), tree.y + 1), tree.label.c_str()));
			insertContentControl(scrollBar);
			treeView = createMacroUiTreeView(TRect(tree.x, treeTop, tree.x + tree.width - 1, treeTop + tree.height), scrollBar, items, nextCommand, definition.windowId, tree.id);
			insertContentControl(treeView);
			setMacroUiTreeItems(treeView, items, tree.start);
			commandToTree[nextCommand] = tree.id;
			treeSpecs[tree.id] = tree;
			treeViews[tree.id] = treeView;
			if (selection.controlId == 0) selection = readSelection(tree.id);
			++nextCommand;
		}

		for (std::size_t tableIndex = 0; tableIndex < definition.tables.size(); ++tableIndex) {
			const MRMacroModelessTableSpec &table = definition.tables[tableIndex];
			const std::vector<std::string> items = resolveListItems(table.itemSpec);
			const int tableTop = table.label.empty() ? table.y : table.y + 1;
			TScrollBar *scrollBar = new TScrollBar(TRect(table.x + table.width - 1, tableTop, table.x + table.width, tableTop + table.height));
			TView *tableView = nullptr;

			if (nextCommand >= cmMacroModelessMax) break;
			if (!table.label.empty()) insertContentControl(new TStaticText(TRect(table.x, table.y, table.x + strwidth(table.label.c_str()), table.y + 1), table.label.c_str()));
			insertContentControl(scrollBar);
			tableView = createMacroUiTableView(TRect(table.x, tableTop, table.x + table.width - 1, tableTop + table.height), scrollBar, items, nextCommand, definition.windowId, table.id);
			insertContentControl(tableView);
			setMacroUiTableItems(tableView, items, table.start);
			commandToTable[nextCommand] = table.id;
			tableSpecs[table.id] = table;
			tableViews[table.id] = tableView;
			if (selection.controlId == 0) selection = readSelection(table.id);
			++nextCommand;
		}

		for (std::size_t buttonIndex = 0; buttonIndex < definition.buttons.size(); ++buttonIndex) {
			const MRMacroModelessButtonSpec &button = definition.buttons[buttonIndex];
			const MRMacroModelessButtonCaption caption = parseButtonCaption(button.text);

			if (nextCommand >= cmMacroModelessMax) break;
			commandToButton[nextCommand] = button.id;
			buttons[button.id] = button;
			for (std::size_t hotKeyIndex = 0; hotKeyIndex < caption.hotKeys.size(); ++hotKeyIndex)
				hotKeys.emplace_back(caption.hotKeys[hotKeyIndex], button.id);
			insertContentControl(new TButton(TRect(button.x, button.y, button.x + button.width, button.y + 2), caption.displayLabel.c_str(), nextCommand, bfNormal));
			++nextCommand;
		}
	}

	void refreshDisplays() {
		while (displayViews.size() > definition.displays.size()) {
			MRMacroModelessDisplayLine *displayLine = displayViews.back();

			displayViews.pop_back();
			if (displayLine == nullptr) continue;
			if (contentGroup != nullptr)
				contentGroup->remove(displayLine);
			else
				remove(displayLine);
			TObject::destroy(displayLine);
		}

		for (std::size_t index = 0; index < definition.displays.size(); ++index) {
			const MRMacroModelessDisplaySpec &display = definition.displays[index];
			TRect bounds(display.x, display.y, display.x + display.width, display.y + 1);

			if (index >= displayViews.size()) {
				MRMacroModelessDisplayLine *displayLine = new MRMacroModelessDisplayLine(bounds, display.text);

				insertContentControl(displayLine);
				displayViews.push_back(displayLine);
				continue;
			}
			if (displayViews[index] == nullptr) continue;
			if (contentGroup != nullptr) bounds.move(-contentGroup->origin.x, -contentGroup->origin.y);
			if (displayViews[index]->getBounds() != bounds) displayViews[index]->locate(bounds);
			displayViews[index]->setText(display.text);
		}
	}

	void refreshTextFields() {
		for (std::size_t index = 0; index < definition.textFields.size(); ++index) {
			const MRMacroModelessTextFieldSpec &textField = definition.textFields[index];
			std::map<std::string, TView *>::iterator viewIt = textFieldViews.find(textField.fieldId);

			if (viewIt == textFieldViews.end() || viewIt->second == nullptr) continue;
			static_cast<void>(setMacroModelessTextInputValue(viewIt->second, textField.text));
		}
	}

	void refreshBoolFields() {
		for (std::size_t index = 0; index < definition.boolFields.size(); ++index) {
			const MRMacroModelessBoolFieldSpec &boolField = definition.boolFields[index];
			std::map<std::string, TView *>::iterator viewIt = boolFieldViews.find(boolField.fieldId);

			if (viewIt == boolFieldViews.end() || viewIt->second == nullptr) continue;
			static_cast<void>(setMacroModelessBoolInputValue(viewIt->second, boolField.value));
		}
	}

	void refreshIntFields() {
		for (std::size_t index = 0; index < definition.intFields.size(); ++index) {
			const MRMacroModelessIntFieldSpec &intField = definition.intFields[index];
			std::map<std::string, TView *>::iterator viewIt = intFieldViews.find(intField.fieldId);

			if (viewIt == intFieldViews.end() || viewIt->second == nullptr) continue;
			static_cast<void>(setMacroModelessIntInputValue(viewIt->second, intField.value));
		}
	}

	void refreshProgressFields() {
		for (std::map<std::string, TView *>::iterator entry = progressFieldViews.begin(); entry != progressFieldViews.end(); ++entry)
			redrawMacroModelessProgressView(entry->second);
	}

	void refreshLogFields() {
		for (std::map<std::string, TView *>::iterator entry = logFieldViews.begin(); entry != logFieldViews.end(); ++entry)
			redrawMacroModelessLogView(entry->second);
	}

	void refreshSelectFields() {
		for (std::size_t index = 0; index < definition.selectFields.size(); ++index) {
			const MRMacroModelessSelectFieldSpec &selectField = definition.selectFields[index];
			std::map<std::string, TView *>::iterator viewIt = selectFieldViews.find(selectField.fieldId);

			if (viewIt == selectFieldViews.end() || viewIt->second == nullptr) continue;
			static_cast<void>(setMacroModelessSelectInputOptions(viewIt->second, selectField.options, selectField.value));
		}
	}

	MRMacroModelessSelection readSelection(int controlId) const {
		MRMacroModelessSelection result;
		std::map<int, TView *>::const_iterator it = listViews.find(controlId);
		std::map<int, TView *>::const_iterator gridIt = gridViews.find(controlId);
		std::map<int, TView *>::const_iterator treeIt = treeViews.find(controlId);
		std::map<int, TView *>::const_iterator tableIt = tableViews.find(controlId);

		result.controlId = controlId;
		if (it != listViews.end() && it->second != nullptr) {
			result.index = macroUiListSelectedIndex(it->second);
			result.text = macroUiListSelectedText(it->second);
			return result;
		}
		if (gridIt != gridViews.end() && gridIt->second != nullptr) {
			result.index = macroUiGridSelectedIndex(gridIt->second);
			result.text = macroUiGridSelectedText(gridIt->second);
			return result;
		}
		if (treeIt != treeViews.end() && treeIt->second != nullptr) {
			result.index = macroUiTreeSelectedIndex(treeIt->second);
			result.text = macroUiTreeSelectedText(treeIt->second);
			return result;
		}
		if (tableIt != tableViews.end() && tableIt->second != nullptr) {
			result.index = macroUiTableSelectedIndex(tableIt->second);
			result.text = macroUiTableSelectedText(tableIt->second);
		}
		return result;
	}

	bool runCanvasHotspot(const TPoint &mouse) {
		for (std::size_t hotspotIndex = 0; hotspotIndex < definition.canvasHotspots.size(); ++hotspotIndex) {
			const MRMacroModelessCanvasHotspotSpec &hotspot = definition.canvasHotspots[hotspotIndex];

			for (std::size_t canvasIndex = 0; canvasIndex < definition.canvases.size(); ++canvasIndex) {
				const MRMacroModelessCanvasSpec &canvas = definition.canvases[canvasIndex];
				const TRect hotspotBounds(canvas.x + hotspot.x, canvas.y + hotspot.y, canvas.x + hotspot.x + hotspot.width, canvas.y + hotspot.y + hotspot.height);

				if (canvas.canvasId != hotspot.canvasId || !hotspotBounds.contains(mouse)) continue;
				selection.controlId = hotspot.id;
				selection.index = 1;
				selection.text = hotspot.canvasId;
				runModelessMacro(definition.windowId, hotspot.macroSpec);
				return true;
			}
		}
		return false;
	}

	void runButton(int buttonId) {
		std::map<int, MRMacroModelessButtonSpec>::const_iterator it = buttons.find(buttonId);
		const std::string windowId = definition.windowId;

		if (it == buttons.end()) return;
		if (selection.controlId != 0) selection = readSelection(selection.controlId);
		else if (!listViews.empty())
			selection = readSelection(listViews.begin()->first);
		else if (!gridViews.empty())
			selection = readSelection(gridViews.begin()->first);
		else if (!treeViews.empty())
			selection = readSelection(treeViews.begin()->first);
		else if (!tableViews.empty())
			selection = readSelection(tableViews.begin()->first);
		runModelessMacro(windowId, it->second.macroSpec);
	}

	void runModelessMacro(const std::string &windowId, const std::string &macroSpec) {
		std::map<std::string, MRMacroModelessWindow *>::const_iterator windowIt;

		if (g_commandRunner != nullptr) g_commandRunner(windowId, selection.controlId, selection, macroSpec);
		windowIt = g_windows.find(windowId);
		if (windowIt == g_windows.end() || windowIt->second != this) return;
		refreshSelectionControls();
	}

	MRMacroModelessWindowDefinition definition;
	TGroup *contentGroup = nullptr;
	std::map<ushort, int> commandToButton;
	std::map<ushort, int> commandToList;
	std::map<ushort, int> commandToGrid;
	std::map<ushort, int> commandToTree;
	std::map<ushort, int> commandToTable;
	std::map<int, MRMacroModelessButtonSpec> buttons;
	std::map<int, MRMacroModelessListBoxSpec> listSpecs;
	std::map<int, MRMacroModelessGridSpec> gridSpecs;
	std::map<int, MRMacroModelessTreeSpec> treeSpecs;
	std::map<int, MRMacroModelessTableSpec> tableSpecs;
	std::map<int, TView *> listViews;
	std::map<int, TView *> gridViews;
	std::map<int, TView *> treeViews;
	std::map<int, TView *> tableViews;
	std::vector<MRMacroModelessDisplayLine *> displayViews;
	std::map<std::string, TView *> textFieldViews;
	std::map<std::string, TView *> boolFieldViews;
	std::map<std::string, TView *> intFieldViews;
	std::map<std::string, TView *> progressFieldViews;
	std::map<std::string, TView *> logFieldViews;
	std::map<std::string, TView *> selectFieldViews;
	std::map<std::string, TView *> canvasViews;
	std::vector<std::pair<ushort, int>> hotKeys;
	MRMacroModelessSelection selection;
};

static TFrame *initMacroModelessFrame(TRect bounds) {
	return new MRFrame(bounds);
}

} // namespace

void setMacroModelessListResolver(MRMacroModelessListResolver resolver) {
	g_listResolver = resolver;
}

void setMacroModelessCommandRunner(MRMacroModelessCommandRunner runner) {
	g_commandRunner = runner;
}

bool showMacroModelessWindow(const MRMacroModelessWindowDefinition &definition) {
	if (TProgram::deskTop == nullptr || definition.windowId.empty()) return false;
	std::map<std::string, MRMacroModelessWindow *>::iterator it = g_windows.find(definition.windowId);
	MRMacroModelessWindowDesktopState desktopState;

	mrvmStoreModelessWindowDefinition(definition);
	if (!mrvmReadModelessWindowDesktopState(definition.windowId, desktopState) || !desktopState.assigned) {
		desktopState.virtualDesktop = currentVirtualDesktop();
		desktopState.manuallyHidden = false;
		desktopState.assigned = true;
		mrvmStoreModelessWindowDesktopState(definition.windowId, desktopState);
	}

	if (it != g_windows.end() && it->second != nullptr) {
		it->second->updateDefinition(definition);
		if (desktopState.virtualDesktop == currentVirtualDesktop() && !desktopState.manuallyHidden) {
			if ((it->second->state & sfVisible) == 0) it->second->show();
			it->second->select();
		}
		return true;
	}

	MRMacroModelessWindow *window = new MRMacroModelessWindow(definition);
	if (window == nullptr) return false;
	g_windows[definition.windowId] = window;
	TProgram::deskTop->insert(window);
	window->refreshSelectionControls();
	window->storeLiveGeometry();
	if (desktopState.virtualDesktop != currentVirtualDesktop() || desktopState.manuallyHidden) window->hide();
	mrNotifyWindowTopologyChanged();
	return true;
}

bool updateMacroModelessWindow(const MRMacroModelessWindowDefinition &definition) {
	if (TProgram::deskTop == nullptr || definition.windowId.empty()) return false;
	std::map<std::string, MRMacroModelessWindow *>::iterator it = g_windows.find(definition.windowId);

	if (it == g_windows.end() || it->second == nullptr) return false;
	mrvmStoreModelessWindowDefinition(definition);
	it->second->updateDefinition(definition);
	return true;
}

bool updateMacroModelessDisplay(const std::string &windowId, int displayIndex, const std::string &text) {
	if (TProgram::deskTop == nullptr || windowId.empty() || displayIndex <= 0) return false;
	std::map<std::string, MRMacroModelessWindow *>::iterator it = g_windows.find(windowId);

	if (it == g_windows.end() || it->second == nullptr) return false;
	if (!mrvmStoreModelessWindowDisplay(windowId, displayIndex, text)) return false;
	return it->second->updateDisplay(displayIndex, text);
}

bool updateMacroModelessTextField(const std::string &windowId, const std::string &fieldId, const std::string &text) {
	if (TProgram::deskTop == nullptr || windowId.empty() || fieldId.empty()) return false;
	std::map<std::string, MRMacroModelessWindow *>::iterator it = g_windows.find(windowId);

	if (it == g_windows.end() || it->second == nullptr) return false;
	if (!mrvmStoreModelessWindowTextFieldValue(windowId, fieldId, text)) return false;
	return it->second->updateTextField(fieldId, text);
}

bool updateMacroModelessBoolField(const std::string &windowId, const std::string &fieldId, bool value) {
	if (TProgram::deskTop == nullptr || windowId.empty() || fieldId.empty()) return false;
	std::map<std::string, MRMacroModelessWindow *>::iterator it = g_windows.find(windowId);

	if (it == g_windows.end() || it->second == nullptr) return false;
	if (!mrvmStoreModelessWindowBoolFieldValue(windowId, fieldId, value)) return false;
	return it->second->updateBoolField(fieldId, value);
}

bool updateMacroModelessIntField(const std::string &windowId, const std::string &fieldId, int value) {
	if (TProgram::deskTop == nullptr || windowId.empty() || fieldId.empty()) return false;
	std::map<std::string, MRMacroModelessWindow *>::iterator it = g_windows.find(windowId);

	if (it == g_windows.end() || it->second == nullptr) return false;
	return it->second->updateIntField(fieldId, value);
}

bool updateMacroModelessProgressField(const std::string &windowId, const std::string &fieldId) {
	if (TProgram::deskTop == nullptr || windowId.empty() || fieldId.empty()) return false;
	std::map<std::string, MRMacroModelessWindow *>::iterator it = g_windows.find(windowId);

	if (it == g_windows.end() || it->second == nullptr) return false;
	return it->second->updateProgressField(fieldId);
}

bool updateMacroModelessLogField(const std::string &windowId, const std::string &logId) {
	if (TProgram::deskTop == nullptr || windowId.empty() || logId.empty()) return false;
	std::map<std::string, MRMacroModelessWindow *>::iterator it = g_windows.find(windowId);

	if (it == g_windows.end() || it->second == nullptr) return false;
	return it->second->updateLogField(logId);
}

bool updateMacroModelessSelectField(const std::string &windowId, const std::string &fieldId, const std::string &value) {
	if (TProgram::deskTop == nullptr || windowId.empty() || fieldId.empty()) return false;
	std::map<std::string, MRMacroModelessWindow *>::iterator it = g_windows.find(windowId);

	if (it == g_windows.end() || it->second == nullptr) return false;
	return it->second->updateSelectField(fieldId, value);
}

bool commitMacroModelessCanvas(const std::string &windowId, const std::string &canvasId) {
	std::map<std::string, MRMacroModelessWindow *>::iterator windowIt = g_windows.find(windowId);

	if (windowIt == g_windows.end() || windowIt->second == nullptr || canvasId.empty()) return false;
	return windowIt->second->commitCanvas(canvasId);
}

bool closeMacroModelessWindow(const std::string &windowId) {
	std::map<std::string, MRMacroModelessWindow *>::iterator it = g_windows.find(windowId);
	TEvent closeEvent{};

	if (it == g_windows.end() || it->second == nullptr) return false;
	closeEvent.what = evCommand;
	closeEvent.message.command = cmClose;
	closeEvent.message.infoPtr = it->second;
	it->second->putEvent(closeEvent);
	return true;
}

void refreshMacroModelessWindows() {
	for (std::map<std::string, MRMacroModelessWindow *>::iterator entry = g_windows.begin(); entry != g_windows.end(); ++entry)
		if (entry->second != nullptr) {
			entry->second->storeLiveGeometry();
			entry->second->refreshSelectionControls();
		}
}
