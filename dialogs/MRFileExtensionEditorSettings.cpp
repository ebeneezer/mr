#define Uses_TButton
#define Uses_TDialog
#define Uses_TCheckBoxes
#define Uses_TDrawBuffer
#define Uses_TInputLine
#define Uses_TRadioButtons
#define Uses_TRect
#define Uses_TStaticText
#define Uses_TSItem
#define Uses_TView
#include <tvision/tv.h>

#include "MRFileExtensionEditorSettingsInternal.hpp"
#include "MRNumericSlider.hpp"
#include "MRSetupCommon.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../ui/MRWindowSupport.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace MRFileExtensionProfilesInternal;
using mr::dialogs::readRecordField;
using mr::dialogs::writeRecordField;

constexpr int kMinimumTabSize = 2;
constexpr int kMaximumTabSize = 32;
constexpr int kDefaultTabSize = 8;
constexpr int kMinimumLeftMargin = 1;
constexpr int kMaximumLeftMargin = 999;
constexpr int kDefaultLeftMargin = 1;
constexpr int kMinimumRightMargin = 1;
constexpr int kMaximumRightMargin = 999;
constexpr int kDefaultRightMargin = 78;
constexpr int kMinimumBinaryRecordLength = 1;
constexpr int kMaximumBinaryRecordLength = 99999;
constexpr int kDefaultBinaryRecordLength = 100;
constexpr int kMinimumMiniMapWidth = 2;
constexpr int kMaximumMiniMapWidth = 20;
constexpr int kDefaultMiniMapWidth = 4;
constexpr ushort kUiManagedOptionsMask = kOptionTruncateSpaces | kOptionEofCtrlZ | kOptionEofCrLf | kOptionPersistentBlocks | kOptionCodeFolding | kOptionWordWrap | kOptionShowLineNumbers | kOptionLineNumZeroFill | kOptionShowEofMarker | kOptionShowEofMarkerEmoji | kOptionDisplayTabs | kOptionFormatRuler | kOptionCodeColoring | kOptionCodeFoldingFeature | kOptionSmartIndenting;
static const char *const kCodeLanguageChoices[] = {"None", "Auto", "C", "C++", "Python", "JavaScript", "TypeScript", "TSX", "Bash", "JSON", "Perl", "Swift"};

struct FileExtensionEditorSettingsPanelLayout {
	explicit FileExtensionEditorSettingsPanelLayout(const FileExtensionEditorSettingsPanelConfig &config)
	    : labelLeft(config.labelLeft), inputLeft(config.inputLeft), inputRight(config.inputRight > config.inputLeft ? config.inputRight : config.dialogWidth - 2), optionsHeadingX(config.clusterLeft >= 0 ? config.clusterLeft : labelLeft), optionsLeft(optionsHeadingX), optionsRight(optionsLeft + 26), lineNumbersLeft(optionsRight + 3), lineNumbersRight(lineNumbersLeft + 15), eofMarkerLeft(lineNumbersRight + 3), eofMarkerRight(eofMarkerLeft + 15), defaultModeLeft(eofMarkerRight + 3), defaultModeRight(defaultModeLeft + 15), tabExpandLeft(defaultModeRight + 3), tabExpandRight(tabExpandLeft + 15), columnBlockMoveLeft(lineNumbersLeft), columnBlockMoveRight(columnBlockMoveLeft + 18), indentStyleLeft(columnBlockMoveRight + 3), indentStyleRight(indentStyleLeft + 15), fileTypeLeft(indentStyleRight + 3), fileTypeRight(fileTypeLeft + 22), topY(config.topY), pageBreakY(topY), wordDelimitersY(pageBreakY + (config.compactTextRows ? 1 : 2)),
	      defaultExtensionsY(config.includeDefaultExtensions ? wordDelimitersY + (config.compactTextRows ? 1 : 2) : -1), tabSizeY(config.tabSizeY >= 0 ? config.tabSizeY : (config.includeDefaultExtensions ? defaultExtensionsY + 1 : wordDelimitersY + 1)), rightMarginY(tabSizeY + 1), postLoadMacroY(rightMarginY + 2), preSaveMacroY(postLoadMacroY + 1), defaultPathY(preSaveMacroY + 1), formatLineY(defaultPathY + 1), optionsHeadingY(config.clusterTopY >= 0 ? config.clusterTopY : formatLineY + 2), optionsBodyY(optionsHeadingY + 1), tabExpandHeadingY(optionsHeadingY), tabExpandBodyY(optionsBodyY), columnBlockMoveHeadingY(optionsBodyY + 4), columnBlockMoveBodyY(columnBlockMoveHeadingY + 1), indentStyleHeadingY(optionsBodyY + 4), indentStyleBodyY(indentStyleHeadingY + 1), fileTypeHeadingY(optionsBodyY + 4), fileTypeBodyY(fileTypeHeadingY + 1), miniMapHeadingY(std::max(optionsBodyY + 9, fileTypeBodyY + 4)), miniMapBodyY(miniMapHeadingY + 1), contentBottomY(miniMapBodyY + 7) {
	}

	int labelLeft;
	int inputLeft;
	int inputRight;
	int optionsHeadingX;
	int optionsLeft;
	int optionsRight;
	int lineNumbersLeft;
	int lineNumbersRight;
	int eofMarkerLeft;
	int eofMarkerRight;
	int defaultModeLeft;
	int defaultModeRight;
	int tabExpandLeft;
	int tabExpandRight;
	int columnBlockMoveLeft;
	int columnBlockMoveRight;
	int indentStyleLeft;
	int indentStyleRight;
	int fileTypeLeft;
	int fileTypeRight;
	int topY;
	int pageBreakY;
	int wordDelimitersY;
	int defaultExtensionsY;
	int tabSizeY;
	int rightMarginY;
	int postLoadMacroY;
	int preSaveMacroY;
	int defaultPathY;
	int formatLineY;
	int optionsHeadingY;
	int optionsBodyY;
	int tabExpandHeadingY;
	int tabExpandBodyY;
	int columnBlockMoveHeadingY;
	int columnBlockMoveBodyY;
	int indentStyleHeadingY;
	int indentStyleBodyY;
	int fileTypeHeadingY;
	int fileTypeBodyY;
	int miniMapHeadingY;
	int miniMapBodyY;
	int contentBottomY;
};

class TPanelGlyphButton : public TView {
  public:
	TPanelGlyphButton(const TRect &bounds, const char *glyph, ushort command) : TView(bounds), glyphId(glyph != nullptr ? glyph : ""), commandId(command) {
		options |= ofSelectable;
		options |= ofFirstClick;
		eventMask |= evMouseDown | evKeyDown;
	}

	void draw() override {
		TDrawBuffer buffer;
		ushort color = getColor((state & sfFocused) != 0 ? 2 : 1);
		int glyphWidth = strwidth(glyphId.c_str());
		int x = std::max(0, (size.x - glyphWidth) / 2);

		buffer.moveChar(0, ' ', color, size.x);
		buffer.moveStr(static_cast<ushort>(x), glyphId.c_str(), color, size.x - x);
		writeLine(0, 0, size.x, size.y, buffer);
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evMouseDown) {
			dispatchCommand();
			clearEvent(event);
			return;
		}
		if (event.what == evKeyDown) {
			TKey key(event.keyDown);

			if (key == TKey(kbEnter) || key == TKey(' ')) {
				dispatchCommand();
				clearEvent(event);
				return;
			}
		}
		TView::handleEvent(event);
	}

  private:
	void dispatchCommand() {
		TView *target = owner;

		while (target != nullptr && dynamic_cast<TDialog *>(target) == nullptr)
			target = target->owner;
		message(target != nullptr ? target : owner, evCommand, commandId, this);
	}

	std::string glyphId;
	ushort commandId;
};

class TFormatRulerView : public TView {
  public:
	explicit TFormatRulerView(const TRect &bounds, FileExtensionEditorSettingsPanel &panel) : TView(bounds), panel(panel) {
		options |= ofSelectable;
		options |= ofFirstClick;
		eventMask |= evMouseDown | evKeyDown;
	}

	void draw() override {
		TDrawBuffer buffer;
		const ushort normal = getColor((state & sfFocused) != 0 ? 2 : 1);
		const ushort accent = getColor((state & sfFocused) != 0 ? 5 : 4);
		std::string normalized;
		int tabSize = panel.currentFormatLineTabSize();
		int leftMargin = panel.currentFormatLineLeftMargin();
		int rightMargin = panel.currentFormatLineRightMargin();

		buffer.moveChar(0, ' ', normal, size.x);
		if (normalizeEditFormatLine(panel.currentFormatLineValue(), tabSize, leftMargin, rightMargin, normalized, &leftMargin, &rightMargin, nullptr)) {
			for (int x = 0; x < size.x; ++x) {
				const char ch = x < static_cast<int>(normalized.size()) ? normalized[static_cast<std::size_t>(x)] : ' ';
				buffer.moveChar(static_cast<ushort>(x), ch, x == cursorColumn ? accent : normal, 1);
			}
		}
		writeLine(0, 0, size.x, size.y, buffer);
		if ((state & sfFocused) != 0 && size.x > 0) {
			setCursor(std::max(0, std::min(cursorColumn, size.x - 1)), 0);
			showCursor();
		} else
			hideCursor();
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evMouseDown) {
			select();
			cursorColumn = std::max(0, std::min(makeLocal(event.mouse.where).x, std::max(0, size.x - 1)));
			commitDragEdit(event, cursorColumn);
			clearEvent(event);
			return;
		}
		if (event.what == evKeyDown) {
			TKey key(event.keyDown);
			const char ch = static_cast<char>(event.keyDown.charScan.charCode);

			if (key == TKey(kbLeft)) {
				cursorColumn = std::max(0, cursorColumn - 1);
				drawView();
				clearEvent(event);
				return;
			}
			if (key == TKey(kbRight)) {
				cursorColumn = std::min(std::max(0, size.x - 1), cursorColumn + 1);
				drawView();
				clearEvent(event);
				return;
			}
			if (key == TKey(kbHome)) {
				cursorColumn = 0;
				drawView();
				clearEvent(event);
				return;
			}
			if (key == TKey(kbEnd)) {
				cursorColumn = std::max(0, size.x - 1);
				drawView();
				clearEvent(event);
				return;
			}
			if (key == TKey(kbBack) || key == TKey(kbDel)) {
				commitSymbolEdit('.');
				clearEvent(event);
				return;
			}
			if (ch == '.' || ch == ' ' || ch == '|' || ch == 'L' || ch == 'l' || ch == 'R' || ch == 'r') {
				commitSymbolEdit(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
				if (cursorColumn < std::max(0, size.x - 1)) ++cursorColumn;
				clearEvent(event);
				return;
			}
		}
		TView::handleEvent(event);
	}

	void setState(ushort aState, Boolean enable) override {
		TView::setState(aState, enable);
		if ((aState & sfFocused) != 0) {
			message(owner, evBroadcast, cmMrFileExtensionEditorSettingsPanelFocusChanged, this);
			drawView();
		}
	}

  private:
	[[nodiscard]] std::string normalizedFormatLine(int &tabSize, int &leftMargin, int &rightMargin) const {
		std::string normalized;
		tabSize = panel.currentFormatLineTabSize();
		leftMargin = panel.currentFormatLineLeftMargin();
		rightMargin = panel.currentFormatLineRightMargin();
		normalizeEditFormatLine(panel.currentFormatLineValue(), tabSize, leftMargin, rightMargin, normalized, &leftMargin, &rightMargin, nullptr);
		return normalized;
	}

	void applyUpdatedFormatLine(const std::string &value, int leftMargin, int rightMargin) {
		panel.applyFormatLineState(value, leftMargin, rightMargin);
		message(owner, evBroadcast, cmMrFileExtensionEditorSettingsPanelChanged, this);
		drawView();
	}

	void commitSymbolEdit(char symbol) {
		std::string updated;
		int tabSize = panel.currentFormatLineTabSize();
		int leftMargin = panel.currentFormatLineLeftMargin();
		int rightMargin = panel.currentFormatLineRightMargin();

		if (!editFormatLineAtColumn(panel.currentFormatLineValue(), tabSize, leftMargin, rightMargin, cursorColumn + 1, symbol, updated, &leftMargin, &rightMargin, nullptr)) return;
		applyUpdatedFormatLine(updated, leftMargin, rightMargin);
	}

	void commitMouseEdit(ushort modifiers) {
		int tabSize = panel.currentFormatLineTabSize();
		int leftMargin = panel.currentFormatLineLeftMargin();
		int rightMargin = panel.currentFormatLineRightMargin();
		const std::string normalized = normalizedFormatLine(tabSize, leftMargin, rightMargin);
		const int column = cursorColumn + 1;
		const char current = column <= static_cast<int>(normalized.size()) ? normalized[static_cast<std::size_t>(column - 1)] : '.';

		if ((modifiers & kbAltShift) != 0) {
			commitSymbolEdit('.');
			return;
		}
		if ((modifiers & kbShift) != 0) {
			commitSymbolEdit('L');
			return;
		}
		if ((modifiers & kbCtrlShift) != 0) {
			commitSymbolEdit('R');
			return;
		}
		if (column <= leftMargin) {
			commitSymbolEdit('L');
			return;
		}
		if (column >= rightMargin) {
			commitSymbolEdit('R');
			return;
		}
		if (current == 'L' || current == 'R') return;
		commitSymbolEdit(current == '|' ? '.' : '|');
	}

	void commitDragEdit(TEvent &event, int startColumn) {
		const std::string initialValue = panel.currentFormatLineValue();
		const int initialTabSize = panel.currentFormatLineTabSize();
		const int initialLeftMargin = panel.currentFormatLineLeftMargin();
		const int initialRightMargin = panel.currentFormatLineRightMargin();
		bool dragged = false;

		while (mouseEvent(event, evMouseMove | evMouseAuto | evMouseUp)) {
			std::string translated;
			int leftMargin = initialLeftMargin;
			int rightMargin = initialRightMargin;
			const int currentColumn = std::max(0, std::min(makeLocal(event.mouse.where).x, std::max(0, size.x - 1)));
			const int delta = currentColumn - startColumn;

			if (event.what == evMouseUp) break;
			if (delta == 0) continue;
			dragged = true;
			cursorColumn = currentColumn;
			if (!translateEditFormatLine(initialValue, initialTabSize, initialLeftMargin, initialRightMargin, delta, translated, &leftMargin, &rightMargin, nullptr)) continue;
			applyUpdatedFormatLine(translated, leftMargin, rightMargin);
		}
		if (!dragged) commitMouseEdit(event.mouse.controlKeyState);
	}

	FileExtensionEditorSettingsPanel &panel;
	int cursorColumn = 0;
};

TStaticText *addPanelLabel(MRScrollableDialog &dialog, const TRect &rect, const char *text) {
	TStaticText *view = new TStaticText(rect, text);
	dialog.addManaged(view, rect);
	return view;
}

TInputLine *addPanelInput(MRScrollableDialog &dialog, const TRect &rect, int maxLen) {
	TInputLine *view = new TInputLine(rect, maxLen);
	dialog.addManaged(view, rect);
	return view;
}

MRNumericSlider *addPanelNumericSlider(MRScrollableDialog &dialog, const TRect &rect, int32_t minValue, int32_t maxValue, int32_t initialValue, int32_t step, int32_t pageStep, ushort changedCmd) {
	MRNumericSlider *view = new MRNumericSlider(rect, minValue, maxValue, initialValue, step, pageStep, MRNumericSlider::fmtRaw, changedCmd);
	dialog.addManaged(view, rect);
	return view;
}

TCheckBoxes *addPanelCheckGroup(MRScrollableDialog &dialog, const TRect &rect, TSItem *items) {
	TCheckBoxes *view = new TCheckBoxes(rect, items);
	dialog.addManaged(view, rect);
	return view;
}

TRadioButtons *addPanelRadioGroup(MRScrollableDialog &dialog, const TRect &rect, TSItem *items) {
	TRadioButtons *view = new TRadioButtons(rect, items);
	dialog.addManaged(view, rect);
	return view;
}

TView *addPanelGlyphButton(MRScrollableDialog &dialog, const TRect &rect, const char *glyph, ushort command) {
	TView *view = new TPanelGlyphButton(rect, glyph, command);
	dialog.addManaged(view, rect);
	return view;
}

[[nodiscard]] int parseIntegerOrDefault(const char *value, int fallback, int minimum, int maximum) {
	std::string text = readRecordField(value);
	if (text.empty()) return fallback;
	char *end = nullptr;
	long parsed = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str() || end == nullptr || *end != '\0') return fallback;
	return std::clamp(static_cast<int>(parsed), minimum, maximum);
}

[[nodiscard]] int parseIntegerTextOrDefault(const std::string &value, int fallback, int minimum, int maximum) {
	std::string text = trimAscii(value);
	char *end = nullptr;
	long parsed = 0;

	if (text.empty()) return fallback;
	parsed = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str() || end == nullptr || *end != '\0') return fallback;
	return std::clamp(static_cast<int>(parsed), minimum, maximum);
}

void writeSliderValue(MRNumericSlider *slider, char *dest, std::size_t destSize, int fallback) {
	int32_t value = fallback;
	if (slider != nullptr) slider->getData(&value);
	writeRecordField(dest, destSize, std::to_string(value));
}

void writeIntegerInputValue(TInputLine *inputLine, char *dest, std::size_t destSize, int fallback, int minimum, int maximum) {
	std::vector<char> buffer(destSize > 0 ? destSize : 1, '\0');

	if (inputLine != nullptr) inputLine->getData(buffer.data());
	const int value = parseIntegerOrDefault(buffer.data(), fallback, minimum, maximum);
	writeRecordField(dest, destSize, std::to_string(value));
}

std::string readInputFieldValue(TInputLine *inputLine) {
	std::vector<char> buffer(512, '\0');

	if (inputLine == nullptr) return std::string();
	inputLine->getData(buffer.data());
	return readRecordField(buffer.data());
}

void writeInputFieldValue(TInputLine *inputLine, const std::string &value) {
	std::vector<char> buffer(512, '\0');

	if (inputLine == nullptr) return;
	writeRecordField(buffer.data(), buffer.size(), value);
	inputLine->setData(buffer.data());
}

} // namespace

namespace MRFileExtensionProfilesInternal {

FileExtensionEditorSettingsPanel::FileExtensionEditorSettingsPanel(const FileExtensionEditorSettingsPanelConfig &config) : config(config) {
}

void FileExtensionEditorSettingsPanel::buildViews(MRScrollableDialog &dialog) {
	const FileExtensionEditorSettingsPanelLayout g(config);
	const int browseWidth = 2;
	const int browseLeft = g.inputRight - browseWidth;
	const int browseFieldRight = browseLeft;
	const int pageBreakRight = std::min(g.inputRight, g.inputLeft + 7);
	const int wordDelimitersRight = std::min(g.inputRight, g.inputLeft + 16);
	const int autoExtensionsRight = std::min(g.inputRight, g.inputLeft + 12);
	const int codeLanguageLabelLeft = autoExtensionsRight + 2;
	const int codeLanguageFieldLeft = codeLanguageLabelLeft + 15;
	const int codeLanguageFieldRight = browseFieldRight;
	const int codeLanguageListLeft = codeLanguageFieldLeft;
	const int codeLanguageListRight = codeLanguageFieldRight;
	const int tabSizeSliderRight = std::max(g.inputLeft + 1, g.inputRight - 2);
	const int marginFieldWidth = std::max(kLeftMarginFieldSize, kRightMarginFieldSize) - 1;
	const int leftMarginFieldRight = g.inputLeft + marginFieldWidth;
	const int rightMarginLabelLeft = leftMarginFieldRight + 2;
	const int rightMarginFieldLeft = rightMarginLabelLeft + 14;
	const int rightMarginFieldRight = rightMarginFieldLeft + marginFieldWidth;
	const int binaryLabelLeft = g.labelLeft + 1;
	const int binaryFieldLeft = g.inputLeft;
	const int binaryFieldRight = binaryFieldLeft + 6;
	const int tabExpandClusterRight = g.tabExpandRight - 1;
	const int lineNumbersClusterRight = g.lineNumbersRight - 1;
	const int eofMarkerClusterRight = g.eofMarkerRight - 4;
	const int fileTypeClusterLeft = g.fileTypeLeft - 1;
	const int fileTypeClusterRight = g.fileTypeRight + 2;
	const int codeFoldingClusterRight = g.columnBlockMoveLeft + 41;

	addPanelLabel(dialog, TRect(g.labelLeft + 1, g.pageBreakY, g.inputLeft - 2, g.pageBreakY + 1), "Page break:");
	pageBreakField = addPanelInput(dialog, TRect(g.inputLeft, g.pageBreakY, pageBreakRight, g.pageBreakY + 1), kPageBreakFieldSize - 1);

	addPanelLabel(dialog, TRect(g.labelLeft + 1, g.wordDelimitersY, g.inputLeft - 2, g.wordDelimitersY + 1), "Word delim:");
	wordDelimitersField = addPanelInput(dialog, TRect(g.inputLeft, g.wordDelimitersY, wordDelimitersRight, g.wordDelimitersY + 1), kWordDelimsFieldSize - 1);

	if (config.includeDefaultExtensions) {
		addPanelLabel(dialog, TRect(g.labelLeft + 1, g.defaultExtensionsY, g.inputLeft - 2, g.defaultExtensionsY + 1), "Auto ext.:");
		defaultExtensionsField = addPanelInput(dialog, TRect(g.inputLeft, g.defaultExtensionsY, autoExtensionsRight, g.defaultExtensionsY + 1), kDefaultExtsFieldSize - 1);
			addPanelLabel(dialog, TRect(codeLanguageLabelLeft, g.defaultExtensionsY, codeLanguageFieldLeft - 1, g.defaultExtensionsY + 1), "Code language:");
			codeLanguageField = addPanelInput(dialog, TRect(codeLanguageFieldLeft, g.defaultExtensionsY, codeLanguageFieldRight, g.defaultExtensionsY + 1), kCodeLanguageFieldSize - 1);
			codeLanguageListAnchor = TRect(codeLanguageListLeft, g.defaultExtensionsY, codeLanguageListRight, g.defaultExtensionsY + 1);
			codeLanguageBrowseButton = addPanelGlyphButton(dialog, TRect(browseLeft, g.defaultExtensionsY, g.inputRight, g.defaultExtensionsY + 1), "▾", cmMrFileExtensionEditorSettingsPanelChooseCodeLanguage);
		}

	addPanelLabel(dialog, TRect(g.labelLeft + 1, g.tabSizeY, g.inputLeft - 2, g.tabSizeY + 1), "Tab size:");
	tabSizeSlider = addPanelNumericSlider(dialog, TRect(g.inputLeft, g.tabSizeY, tabSizeSliderRight, g.tabSizeY + 1), kMinimumTabSize, kMaximumTabSize, kDefaultTabSize, 1, 4, cmMrFileExtensionEditorSettingsPanelChanged);

	addPanelLabel(dialog, TRect(g.labelLeft + 1, g.rightMarginY, g.inputLeft - 2, g.rightMarginY + 1), "Left margin:");
	leftMarginField = addPanelInput(dialog, TRect(g.inputLeft, g.rightMarginY, leftMarginFieldRight, g.rightMarginY + 1), kLeftMarginFieldSize - 1);
	addPanelLabel(dialog, TRect(rightMarginLabelLeft, g.rightMarginY, rightMarginFieldLeft - 1, g.rightMarginY + 1), "Right margin:");
	rightMarginField = addPanelInput(dialog, TRect(rightMarginFieldLeft, g.rightMarginY, rightMarginFieldRight, g.rightMarginY + 1), kRightMarginFieldSize - 1);
	addPanelLabel(dialog, TRect(binaryLabelLeft, g.rightMarginY + 1, binaryFieldLeft - 1, g.rightMarginY + 2), "Binary record:");
	binaryRecordLengthField = addPanelInput(dialog, TRect(binaryFieldLeft, g.rightMarginY + 1, binaryFieldRight, g.rightMarginY + 2), kBinaryRecordLengthFieldSize - 1);

	addPanelLabel(dialog, TRect(g.labelLeft + 1, g.postLoadMacroY, g.inputLeft - 2, g.postLoadMacroY + 1), "Post-load macro:");
	postLoadMacroField = addPanelInput(dialog, TRect(g.inputLeft, g.postLoadMacroY, browseFieldRight, g.postLoadMacroY + 1), kMacroFieldSize - 1);
	postLoadMacroBrowseButton = addPanelGlyphButton(dialog, TRect(browseLeft, g.postLoadMacroY, g.inputRight, g.postLoadMacroY + 1), "🔎", cmMrFileExtensionEditorSettingsPanelBrowsePostLoadMacro);

	addPanelLabel(dialog, TRect(g.labelLeft + 1, g.preSaveMacroY, g.inputLeft - 2, g.preSaveMacroY + 1), "Pre-save macro:");
	preSaveMacroField = addPanelInput(dialog, TRect(g.inputLeft, g.preSaveMacroY, browseFieldRight, g.preSaveMacroY + 1), kMacroFieldSize - 1);
	preSaveMacroBrowseButton = addPanelGlyphButton(dialog, TRect(browseLeft, g.preSaveMacroY, g.inputRight, g.preSaveMacroY + 1), "🔎", cmMrFileExtensionEditorSettingsPanelBrowsePreSaveMacro);

	addPanelLabel(dialog, TRect(g.labelLeft + 1, g.defaultPathY, g.inputLeft - 2, g.defaultPathY + 1), "Default path:");
	defaultPathField = addPanelInput(dialog, TRect(g.inputLeft, g.defaultPathY, browseFieldRight, g.defaultPathY + 1), kDefaultPathFieldSize - 1);
	defaultPathBrowseButton = addPanelGlyphButton(dialog, TRect(browseLeft, g.defaultPathY, g.inputRight, g.defaultPathY + 1), "🔎", cmMrFileExtensionEditorSettingsPanelBrowseDefaultPath);

	addPanelLabel(dialog, TRect(g.labelLeft + 1, g.formatLineY, g.inputLeft - 2, g.formatLineY + 1), "Format line:");
	formatLineField = addPanelInput(dialog, TRect(g.inputLeft, g.formatLineY, g.inputRight - 2, g.formatLineY + 1), kFormatLineFieldSize - 1);
	if (formatLineField != nullptr) formatLineField->hide();
	formatRulerView = new TFormatRulerView(TRect(g.inputLeft, g.formatLineY, g.inputRight - 2, g.formatLineY + 1), *this);
	dialog.addManaged(formatRulerView, formatRulerView->getBounds());

	addPanelLabel(dialog, TRect(g.optionsHeadingX, g.optionsHeadingY, config.dialogWidth - 2, g.optionsHeadingY + 1), "Options:");
	optionsLeftField = addPanelCheckGroup(dialog, TRect(g.optionsLeft, g.optionsBodyY, g.optionsRight, g.optionsBodyY + 11), new TSItem("~T~runcate whitespace", new TSItem("Control-~Z~ at EOF", new TSItem("~C~R/LF at EOF", new TSItem("Persistent ~B~locks", new TSItem("leading ~0~ fill", new TSItem("word wrap", new TSItem("~D~isplay tabs", new TSItem("~F~ormat ruler", new TSItem("Code c~O~loring", new TSItem("Code fo~L~ding", new TSItem("Smart inde~N~ting", nullptr))))))))))));

	addPanelLabel(dialog, TRect(g.lineNumbersLeft, g.optionsHeadingY, lineNumbersClusterRight, g.optionsHeadingY + 1), "Line numbers:");
	lineNumbersField = addPanelRadioGroup(dialog, TRect(g.lineNumbersLeft, g.optionsBodyY, lineNumbersClusterRight, g.optionsBodyY + 3), new TSItem("~O~ff", new TSItem("~L~eading", new TSItem("~T~railing", nullptr))));

	addPanelLabel(dialog, TRect(g.eofMarkerLeft, g.optionsHeadingY, eofMarkerClusterRight, g.optionsHeadingY + 1), "EOF marker:");
	eofMarkerField = addPanelRadioGroup(dialog, TRect(g.eofMarkerLeft, g.optionsBodyY, eofMarkerClusterRight, g.optionsBodyY + 3), new TSItem("~N~one", new TSItem("~P~lain", new TSItem("~E~moji", nullptr))));

	addPanelLabel(dialog, TRect(g.defaultModeLeft, g.optionsHeadingY, g.defaultModeRight, g.optionsHeadingY + 1), "Default mode:");
	defaultModeField = addPanelRadioGroup(dialog, TRect(g.defaultModeLeft, g.optionsBodyY, g.defaultModeRight, g.optionsBodyY + 3), new TSItem("~I~nsert", new TSItem("Ove~R~write", nullptr)));

	addPanelLabel(dialog, TRect(g.tabExpandLeft, g.tabExpandHeadingY, tabExpandClusterRight, g.tabExpandHeadingY + 1), "Tab expand:");
	tabExpandField = addPanelRadioGroup(dialog, TRect(g.tabExpandLeft, g.tabExpandBodyY, tabExpandClusterRight, g.tabExpandBodyY + 3), new TSItem("~U~se tabs", new TSItem("Spa~X~es", nullptr)));

	addPanelLabel(dialog, TRect(g.columnBlockMoveLeft, g.columnBlockMoveHeadingY, g.columnBlockMoveLeft + 18, g.columnBlockMoveHeadingY + 1), "Column block move:");
	columnBlockMoveField = addPanelRadioGroup(dialog, TRect(g.columnBlockMoveLeft, g.columnBlockMoveBodyY, g.columnBlockMoveRight, g.columnBlockMoveBodyY + 3), new TSItem("Re~M~ove space", new TSItem("~K~eep space", nullptr)));

	addPanelLabel(dialog, TRect(g.indentStyleLeft, g.indentStyleHeadingY, g.indentStyleRight, g.indentStyleHeadingY + 1), "Indent style:");
	indentStyleField = addPanelRadioGroup(dialog, TRect(g.indentStyleLeft, g.indentStyleBodyY, g.indentStyleRight, g.indentStyleBodyY + 3), new TSItem("off", new TSItem("automatic", new TSItem("smart", nullptr))));

	addPanelLabel(dialog, TRect(fileTypeClusterLeft, g.fileTypeHeadingY, fileTypeClusterRight, g.fileTypeHeadingY + 1), "File type:");
	fileTypeField = addPanelRadioGroup(dialog, TRect(fileTypeClusterLeft, g.fileTypeBodyY, fileTypeClusterRight, g.fileTypeBodyY + 3), new TSItem("legacy text (CR/LF)", new TSItem("UNIX (LF)", new TSItem("binary", nullptr))));

	addPanelLabel(dialog, TRect(g.columnBlockMoveLeft, g.miniMapHeadingY, g.columnBlockMoveLeft + 18, g.miniMapHeadingY + 1), "Mini map:");
	miniMapPositionField = addPanelRadioGroup(dialog, TRect(g.columnBlockMoveLeft, g.miniMapBodyY, g.columnBlockMoveLeft + 24, g.miniMapBodyY + 3), new TSItem("~O~ff", new TSItem("~L~eading", new TSItem("~T~railing", nullptr))));
	addPanelLabel(dialog, TRect(g.columnBlockMoveLeft + 27, g.miniMapHeadingY, codeFoldingClusterRight, g.miniMapHeadingY + 1), "Code folding:");
	codeFoldingPositionField = addPanelRadioGroup(dialog, TRect(g.columnBlockMoveLeft + 27, g.miniMapBodyY, codeFoldingClusterRight, g.miniMapBodyY + 3), new TSItem("~O~ff", new TSItem("~L~eading", new TSItem("~T~railing", nullptr))));
	addPanelLabel(dialog, TRect(g.columnBlockMoveLeft, g.miniMapBodyY + 3, g.columnBlockMoveLeft + 24, g.miniMapBodyY + 4), "Width:");
	miniMapWidthSlider = addPanelNumericSlider(dialog, TRect(g.columnBlockMoveLeft, g.miniMapBodyY + 4, g.columnBlockMoveLeft + 24, g.miniMapBodyY + 5), kMinimumMiniMapWidth, kMaximumMiniMapWidth, kDefaultMiniMapWidth, 1, 2, cmMrFileExtensionEditorSettingsPanelChanged);
	addPanelLabel(dialog, TRect(g.columnBlockMoveLeft, g.miniMapBodyY + 5, g.columnBlockMoveLeft + 19, g.miniMapBodyY + 6), "Viewport cursor:");
	miniMapMarkerGlyphField = addPanelInput(dialog, TRect(g.columnBlockMoveLeft + 19, g.miniMapBodyY + 5, g.columnBlockMoveLeft + 24, g.miniMapBodyY + 6), kMiniMapMarkerGlyphFieldSize - 1);
	addPanelLabel(dialog, TRect(g.columnBlockMoveLeft, g.miniMapBodyY + 6, g.columnBlockMoveLeft + 16, g.miniMapBodyY + 7), "Gutters:");
	guttersField = addPanelInput(dialog, TRect(g.columnBlockMoveLeft + 16, g.miniMapBodyY + 6, g.columnBlockMoveLeft + 24, g.miniMapBodyY + 7), kGuttersFieldSize - 1);
}

TView *FileExtensionEditorSettingsPanel::primaryView() const noexcept {
	return pageBreakField;
}

void FileExtensionEditorSettingsPanel::setInputLineValue(TInputLine *inputLine, const char *value, std::size_t capacity) {
	std::vector<char> buffer(capacity, '\0');
	writeRecordField(buffer.data(), buffer.size(), readRecordField(value));
	inputLine->setData(buffer.data());
}

void FileExtensionEditorSettingsPanel::readInputLineValue(TInputLine *inputLine, char *dest, std::size_t destSize) {
	std::vector<char> buffer(destSize, '\0');
	inputLine->getData(buffer.data());
	writeRecordField(dest, destSize, readRecordField(buffer.data()));
}

ushort FileExtensionEditorSettingsPanel::currentOptionsMask() const noexcept {
	ushort leftMask = 0;
	ushort lineNumbersChoice = kLineNumbersOff;
	ushort codeFoldingChoice = kCodeFoldingOff;
	ushort eofMarkerChoice = kEofMarkerOff;
	ushort options = 0;

	if (optionsLeftField != nullptr) optionsLeftField->getData((void *)&leftMask);
	if (lineNumbersField != nullptr) lineNumbersField->getData((void *)&lineNumbersChoice);
	if (codeFoldingPositionField != nullptr) codeFoldingPositionField->getData((void *)&codeFoldingChoice);
	if (eofMarkerField != nullptr) eofMarkerField->getData((void *)&eofMarkerChoice);

	if ((leftMask & kLeftOptionTruncateSpaces) != 0) options |= kOptionTruncateSpaces;
	if ((leftMask & kLeftOptionEofCtrlZ) != 0) options |= kOptionEofCtrlZ;
	if ((leftMask & kLeftOptionEofCrLf) != 0) options |= kOptionEofCrLf;
	if ((leftMask & kLeftOptionPersistentBlocks) != 0) options |= kOptionPersistentBlocks;
	if ((leftMask & kLeftOptionLineNumZeroFill) != 0) options |= kOptionLineNumZeroFill;
	if ((leftMask & kLeftOptionWordWrap) != 0) options |= kOptionWordWrap;
	if ((leftMask & kLeftOptionDisplayTabs) != 0) options |= kOptionDisplayTabs;
	if ((leftMask & kLeftOptionFormatRuler) != 0) options |= kOptionFormatRuler;
	if ((leftMask & kLeftOptionCodeColoring) != 0) options |= kOptionCodeColoring;
	if ((leftMask & kLeftOptionCodeFoldingFeature) != 0) options |= kOptionCodeFoldingFeature;
	if ((leftMask & kLeftOptionSmartIndenting) != 0) options |= kOptionSmartIndenting;

	switch (lineNumbersChoice) {
		case kLineNumbersLeading:
			options |= kOptionShowLineNumbers;
			break;
		case kLineNumbersTrailing:
			options |= kOptionShowLineNumbers;
			break;
		default:
			break;
	}
	if (codeFoldingChoice != kCodeFoldingOff) options |= kOptionCodeFolding;

	switch (eofMarkerChoice) {
		case kEofMarkerPlain:
			options |= kOptionShowEofMarker;
			break;
		case kEofMarkerEmoji:
			options |= kOptionShowEofMarker;
			options |= kOptionShowEofMarkerEmoji;
			break;
		default:
			break;
	}

	options |= preservedOptionsMask;
	return options;
}

void FileExtensionEditorSettingsPanel::setOptionsMask(ushort options) {
	ushort leftMask = 0;
	ushort lineNumbersChoice = kLineNumbersOff;
	ushort codeFoldingChoice = kCodeFoldingOff;
	ushort eofMarkerChoice = kEofMarkerOff;
	if ((options & kOptionTruncateSpaces) != 0) leftMask |= kLeftOptionTruncateSpaces;
	if ((options & kOptionEofCtrlZ) != 0) leftMask |= kLeftOptionEofCtrlZ;
	if ((options & kOptionEofCrLf) != 0) leftMask |= kLeftOptionEofCrLf;
	if ((options & kOptionPersistentBlocks) != 0) leftMask |= kLeftOptionPersistentBlocks;
	if ((options & kOptionLineNumZeroFill) != 0) leftMask |= kLeftOptionLineNumZeroFill;
	if ((options & kOptionWordWrap) != 0) leftMask |= kLeftOptionWordWrap;
	if ((options & kOptionDisplayTabs) != 0) leftMask |= kLeftOptionDisplayTabs;
	if ((options & kOptionFormatRuler) != 0) leftMask |= kLeftOptionFormatRuler;
	if ((options & kOptionCodeColoring) != 0) leftMask |= kLeftOptionCodeColoring;
	if ((options & kOptionCodeFoldingFeature) != 0) leftMask |= kLeftOptionCodeFoldingFeature;
	if ((options & kOptionSmartIndenting) != 0) leftMask |= kLeftOptionSmartIndenting;

	if ((options & kOptionShowLineNumbers) != 0) lineNumbersChoice = kLineNumbersLeading;
	if ((options & kOptionCodeFolding) != 0) codeFoldingChoice = kCodeFoldingLeading;
	if ((options & kOptionShowEofMarker) != 0) eofMarkerChoice = ((options & kOptionShowEofMarkerEmoji) != 0) ? kEofMarkerEmoji : kEofMarkerPlain;

	preservedOptionsMask = options & ~kUiManagedOptionsMask;
	if (optionsLeftField != nullptr) optionsLeftField->setData((void *)&leftMask);
	if (lineNumbersField != nullptr) lineNumbersField->setData((void *)&lineNumbersChoice);
	if (codeFoldingPositionField != nullptr) codeFoldingPositionField->setData((void *)&codeFoldingChoice);
	if (eofMarkerField != nullptr) eofMarkerField->setData((void *)&eofMarkerChoice);
}

void FileExtensionEditorSettingsPanel::loadFieldsFromRecord(const FileExtensionEditorSettingsDialogRecord &record) {
	setInputLineValue(pageBreakField, record.pageBreak, sizeof(record.pageBreak));
	setInputLineValue(wordDelimitersField, record.wordDelimiters, sizeof(record.wordDelimiters));
	if (defaultExtensionsField != nullptr) setInputLineValue(defaultExtensionsField, record.defaultExtensions, sizeof(record.defaultExtensions));
	codeLanguageDropList.hide();
	if (codeLanguageField != nullptr) setInputLineValue(codeLanguageField, record.codeLanguage, sizeof(record.codeLanguage));
	if (tabSizeSlider != nullptr) {
		int32_t value = parseIntegerOrDefault(record.tabSize, kDefaultTabSize, kMinimumTabSize, kMaximumTabSize);
		tabSizeSlider->setData(&value);
		lastKnownTabSizeForFormatLine = static_cast<int>(value);
	}
	{
		std::string normalizedFormatLine;
		const int fallbackLeftMargin = configuredEditSetupSettings().leftMargin > 0 ? configuredEditSetupSettings().leftMargin : kDefaultLeftMargin;
		int normalizedLeftMargin = fallbackLeftMargin;
		int normalizedRightMargin = parseIntegerOrDefault(record.rightMargin, kDefaultRightMargin, kMinimumRightMargin, kMaximumRightMargin);
		if (!normalizeEditFormatLine(readRecordField(record.formatLine), lastKnownTabSizeForFormatLine, fallbackLeftMargin, normalizedRightMargin, normalizedFormatLine, &normalizedLeftMargin, &normalizedRightMargin, nullptr)) normalizedLeftMargin = fallbackLeftMargin;
		lastKnownLeftMarginForFormatLine = normalizedLeftMargin;
		lastKnownRightMarginForFormatLine = normalizedRightMargin;
	}
	setInputLineValue(leftMarginField, record.leftMargin, sizeof(record.leftMargin));
	setInputLineValue(rightMarginField, record.rightMargin, sizeof(record.rightMargin));
	setInputLineValue(binaryRecordLengthField, record.binaryRecordLength, sizeof(record.binaryRecordLength));
	setInputLineValue(postLoadMacroField, record.postLoadMacro, sizeof(record.postLoadMacro));
	setInputLineValue(preSaveMacroField, record.preSaveMacro, sizeof(record.preSaveMacro));
	setInputLineValue(defaultPathField, record.defaultPath, sizeof(record.defaultPath));
	setInputLineValue(formatLineField, record.formatLine, sizeof(record.formatLine));
	setOptionsMask(record.optionsMask);
	if (lineNumbersField != nullptr) lineNumbersField->setData((void *)&record.lineNumbersPositionChoice);
	if (codeFoldingPositionField != nullptr) codeFoldingPositionField->setData((void *)&record.codeFoldingPositionChoice);
	if (tabExpandField != nullptr) tabExpandField->setData((void *)&record.tabExpandChoice);
	if (indentStyleField != nullptr) indentStyleField->setData((void *)&record.indentStyleChoice);
	if (fileTypeField != nullptr) fileTypeField->setData((void *)&record.fileTypeChoice);
	if (columnBlockMoveField != nullptr) columnBlockMoveField->setData((void *)&record.columnBlockMoveChoice);
	if (defaultModeField != nullptr) defaultModeField->setData((void *)&record.defaultModeChoice);
	if (miniMapPositionField != nullptr) miniMapPositionField->setData((void *)&record.miniMapPositionChoice);
	if (miniMapWidthSlider != nullptr) {
		int32_t value = parseIntegerOrDefault(record.miniMapWidth, kDefaultMiniMapWidth, kMinimumMiniMapWidth, kMaximumMiniMapWidth);
		miniMapWidthSlider->setData(&value);
	}
	setInputLineValue(miniMapMarkerGlyphField, record.miniMapMarkerGlyph, sizeof(record.miniMapMarkerGlyph));
	setInputLineValue(guttersField, record.gutters, sizeof(record.gutters));
	syncDynamicStates();
}

void FileExtensionEditorSettingsPanel::saveFieldsToRecord(FileExtensionEditorSettingsDialogRecord &record) const {
	readInputLineValue(pageBreakField, record.pageBreak, sizeof(record.pageBreak));
	readInputLineValue(wordDelimitersField, record.wordDelimiters, sizeof(record.wordDelimiters));
	if (defaultExtensionsField != nullptr) readInputLineValue(defaultExtensionsField, record.defaultExtensions, sizeof(record.defaultExtensions));
	if (codeLanguageField != nullptr) readInputLineValue(codeLanguageField, record.codeLanguage, sizeof(record.codeLanguage));
	writeSliderValue(tabSizeSlider, record.tabSize, sizeof(record.tabSize), kDefaultTabSize);
	writeIntegerInputValue(leftMarginField, record.leftMargin, sizeof(record.leftMargin), kDefaultLeftMargin, kMinimumLeftMargin, kMaximumLeftMargin);
	writeIntegerInputValue(rightMarginField, record.rightMargin, sizeof(record.rightMargin), kDefaultRightMargin, kMinimumRightMargin, kMaximumRightMargin);
	writeIntegerInputValue(binaryRecordLengthField, record.binaryRecordLength, sizeof(record.binaryRecordLength), kDefaultBinaryRecordLength, kMinimumBinaryRecordLength, kMaximumBinaryRecordLength);
	readInputLineValue(postLoadMacroField, record.postLoadMacro, sizeof(record.postLoadMacro));
	readInputLineValue(preSaveMacroField, record.preSaveMacro, sizeof(record.preSaveMacro));
	readInputLineValue(defaultPathField, record.defaultPath, sizeof(record.defaultPath));
	readInputLineValue(formatLineField, record.formatLine, sizeof(record.formatLine));
	record.optionsMask = currentOptionsMask();
	if (lineNumbersField != nullptr) lineNumbersField->getData((void *)&record.lineNumbersPositionChoice);
	if (codeFoldingPositionField != nullptr) codeFoldingPositionField->getData((void *)&record.codeFoldingPositionChoice);
	if (tabExpandField != nullptr) tabExpandField->getData((void *)&record.tabExpandChoice);
	if (indentStyleField != nullptr) indentStyleField->getData((void *)&record.indentStyleChoice);
	if (fileTypeField != nullptr) fileTypeField->getData((void *)&record.fileTypeChoice);
	if (columnBlockMoveField != nullptr) columnBlockMoveField->getData((void *)&record.columnBlockMoveChoice);
	if (defaultModeField != nullptr) defaultModeField->getData((void *)&record.defaultModeChoice);
	if (miniMapPositionField != nullptr) miniMapPositionField->getData((void *)&record.miniMapPositionChoice);
	writeSliderValue(miniMapWidthSlider, record.miniMapWidth, sizeof(record.miniMapWidth), kDefaultMiniMapWidth);
	readInputLineValue(miniMapMarkerGlyphField, record.miniMapMarkerGlyph, sizeof(record.miniMapMarkerGlyph));
	readInputLineValue(guttersField, record.gutters, sizeof(record.gutters));
}

void FileExtensionEditorSettingsPanel::syncDynamicStates() {
	ushort fileTypeChoice = kFileTypeUnix;
	const bool binaryEnabled = fileTypeField != nullptr && ([&]() {
		                           fileTypeField->getData((void *)&fileTypeChoice);
		                           return fileTypeChoice == kFileTypeBinary;
	                           })();
	int32_t currentTabSize = lastKnownTabSizeForFormatLine;
	int currentLeftMargin = lastKnownLeftMarginForFormatLine;
	int currentRightMargin = lastKnownRightMarginForFormatLine;
	int typedLeftMargin = lastKnownLeftMarginForFormatLine;
	std::string currentFormatLine = readInputFieldValue(formatLineField);
	std::string normalizedFormatLine;
	if (tabSizeSlider != nullptr) tabSizeSlider->getData(&currentTabSize);
	typedLeftMargin = parseIntegerTextOrDefault(readInputFieldValue(leftMarginField), lastKnownLeftMarginForFormatLine, kMinimumLeftMargin, kMaximumLeftMargin);
	currentRightMargin = parseIntegerTextOrDefault(readInputFieldValue(rightMarginField), lastKnownRightMarginForFormatLine, kMinimumRightMargin, kMaximumRightMargin);
	if (typedLeftMargin >= currentRightMargin) typedLeftMargin = std::max(kMinimumLeftMargin, currentRightMargin - 1);
	if (!normalizeEditFormatLine(currentFormatLine, static_cast<int>(currentTabSize), typedLeftMargin, currentRightMargin, normalizedFormatLine, &currentLeftMargin, &currentRightMargin, nullptr)) {
		currentLeftMargin = typedLeftMargin;
		currentRightMargin = lastKnownRightMarginForFormatLine;
	}
	if (currentTabSize != lastKnownTabSizeForFormatLine) {
		writeInputFieldValue(formatLineField, defaultEditFormatLineForTabSize(static_cast<int>(currentTabSize), typedLeftMargin, currentRightMargin));
		currentLeftMargin = typedLeftMargin;
	} else if (typedLeftMargin != lastKnownLeftMarginForFormatLine || currentRightMargin != lastKnownRightMarginForFormatLine) {
		writeInputFieldValue(formatLineField, synchronizeEditFormatLineMargins(currentFormatLine, typedLeftMargin, currentRightMargin, static_cast<int>(currentTabSize)));
		currentLeftMargin = typedLeftMargin;
	} else if (isBlankString(currentFormatLine)) {
		writeInputFieldValue(formatLineField, defaultEditFormatLineForTabSize(static_cast<int>(currentTabSize), currentLeftMargin, currentRightMargin));
	} else {
		writeInputFieldValue(formatLineField, normalizedFormatLine);
		writeInputFieldValue(leftMarginField, std::to_string(currentLeftMargin));
		writeInputFieldValue(rightMarginField, std::to_string(currentRightMargin));
	}
	lastKnownLeftMarginForFormatLine = currentLeftMargin;
	lastKnownTabSizeForFormatLine = static_cast<int>(currentTabSize);
	lastKnownRightMarginForFormatLine = currentRightMargin;
	if (formatRulerView != nullptr) {
		formatRulerView->show();
		formatRulerView->drawView();
	}

	if (binaryRecordLengthField != nullptr) binaryRecordLengthField->setState(sfDisabled, binaryEnabled ? False : True);
}

bool FileExtensionEditorSettingsPanel::formatRulerEnabled() const noexcept {
	ushort leftMask = 0;
	if (optionsLeftField != nullptr) optionsLeftField->getData((void *)&leftMask);
	return (leftMask & kLeftOptionFormatRuler) != 0;
}

int FileExtensionEditorSettingsPanel::currentFormatLineTabSize() const noexcept {
	int32_t value = lastKnownTabSizeForFormatLine;
	if (tabSizeSlider != nullptr) tabSizeSlider->getData(&value);
	return static_cast<int>(value);
}

int FileExtensionEditorSettingsPanel::currentFormatLineLeftMargin() const noexcept {
	return parseIntegerTextOrDefault(readInputFieldValue(leftMarginField), lastKnownLeftMarginForFormatLine, kMinimumLeftMargin, kMaximumLeftMargin);
}

int FileExtensionEditorSettingsPanel::currentFormatLineRightMargin() const noexcept {
	return parseIntegerTextOrDefault(readInputFieldValue(rightMarginField), lastKnownRightMarginForFormatLine, kMinimumRightMargin, kMaximumRightMargin);
}

std::string FileExtensionEditorSettingsPanel::currentFormatLineValue() const {
	return readInputFieldValue(formatLineField);
}

void FileExtensionEditorSettingsPanel::applyFormatLineState(const std::string &value, int leftMargin, int rightMargin) {
	writeInputFieldValue(formatLineField, value);
	writeInputFieldValue(leftMarginField, std::to_string(leftMargin));
	writeInputFieldValue(rightMarginField, std::to_string(rightMargin));
	lastKnownLeftMarginForFormatLine = leftMargin;
	lastKnownRightMarginForFormatLine = rightMargin;
	syncDynamicStates();
}

std::string FileExtensionEditorSettingsPanel::postLoadMacroValue() const {
	return readInputFieldValue(postLoadMacroField);
}

std::string FileExtensionEditorSettingsPanel::preSaveMacroValue() const {
	return readInputFieldValue(preSaveMacroField);
}

std::string FileExtensionEditorSettingsPanel::defaultPathValue() const {
	return readInputFieldValue(defaultPathField);
}

std::string FileExtensionEditorSettingsPanel::codeLanguageValue() const {
	return readInputFieldValue(codeLanguageField);
}

void FileExtensionEditorSettingsPanel::setPostLoadMacroValue(const std::string &value) {
	if (postLoadMacroField != nullptr) writeInputFieldValue(postLoadMacroField, value);
}

void FileExtensionEditorSettingsPanel::setPreSaveMacroValue(const std::string &value) {
	if (preSaveMacroField != nullptr) writeInputFieldValue(preSaveMacroField, value);
}

void FileExtensionEditorSettingsPanel::setDefaultPathValue(const std::string &value) {
	if (defaultPathField != nullptr) writeInputFieldValue(defaultPathField, value);
}

void FileExtensionEditorSettingsPanel::setCodeLanguageValue(const std::string &value) {
	if (codeLanguageField != nullptr) writeInputFieldValue(codeLanguageField, value);
}

void FileExtensionEditorSettingsPanel::toggleCodeLanguageList(MRScrollableDialog &dialog) {
	std::vector<std::string> values;

	if (codeLanguageField == nullptr) return;
	values.reserve(sizeof(kCodeLanguageChoices) / sizeof(kCodeLanguageChoices[0]));
	for (const char *choice : kCodeLanguageChoices)
		values.push_back(choice);
	codeLanguageDropList.toggle(dialog, codeLanguageListAnchor, values, trimAscii(codeLanguageValue()), &dialog, cmMrFileExtensionEditorSettingsPanelAcceptCodeLanguage);
}

void FileExtensionEditorSettingsPanel::hideCodeLanguageList() {
	codeLanguageDropList.hide();
	if (codeLanguageField != nullptr) codeLanguageField->select();
}

bool FileExtensionEditorSettingsPanel::codeLanguageListVisible() const noexcept {
	return codeLanguageDropList.visible();
}

bool FileExtensionEditorSettingsPanel::codeLanguageListContainsPoint(TPoint where) const noexcept {
	return codeLanguageDropList.containsPoint(where);
}

bool FileExtensionEditorSettingsPanel::acceptCodeLanguageListSelection() {
	std::string selectedValue;

	if (!codeLanguageDropList.acceptSelection(selectedValue)) return false;
	setCodeLanguageValue(selectedValue);
	hideCodeLanguageList();
	return true;
}

} // namespace MRFileExtensionProfilesInternal
