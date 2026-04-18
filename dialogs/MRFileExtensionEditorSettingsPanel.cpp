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

#include "MRFileExtensionEditorSettingsPanelInternal.hpp"
#include "MRNumericSlider.hpp"
#include "MRSetupDialogCommon.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
using namespace MRFileExtensionProfilesDialogInternal;
using mr::dialogs::readRecordField;
using mr::dialogs::writeRecordField;

constexpr int kMinimumTabSize = 2;
constexpr int kMaximumTabSize = 32;
constexpr int kDefaultTabSize = 8;
constexpr int kMinimumRightMargin = 1;
constexpr int kMaximumRightMargin = 999;
constexpr int kDefaultRightMargin = 78;
constexpr int kMinimumBinaryRecordLength = 1;
constexpr int kMaximumBinaryRecordLength = 99999;
constexpr int kDefaultBinaryRecordLength = 100;
constexpr int kMinimumMiniMapWidth = 2;
constexpr int kMaximumMiniMapWidth = 20;
constexpr int kDefaultMiniMapWidth = 4;
constexpr ushort kUiManagedOptionsMask =
    kOptionTruncateSpaces | kOptionEofCtrlZ | kOptionEofCrLf | kOptionPersistentBlocks |
    kOptionCodeFolding | kOptionWordWrap | kOptionShowLineNumbers | kOptionLineNumZeroFill |
    kOptionShowEofMarker | kOptionShowEofMarkerEmoji;

struct FileExtensionEditorSettingsPanelLayout {
	explicit FileExtensionEditorSettingsPanelLayout(const FileExtensionEditorSettingsPanelConfig &config)
	    : labelLeft(config.labelLeft), inputLeft(config.inputLeft),
	      inputRight(config.inputRight > config.inputLeft ? config.inputRight : config.dialogWidth - 2),
	      optionsHeadingX(config.clusterLeft >= 0 ? config.clusterLeft : labelLeft),
	      optionsLeft(optionsHeadingX), optionsRight(optionsLeft + 24), lineNumbersLeft(optionsRight + 3),
	      lineNumbersRight(lineNumbersLeft + 15), eofMarkerLeft(lineNumbersRight + 3),
	      eofMarkerRight(eofMarkerLeft + 15), defaultModeLeft(eofMarkerRight + 3),
	      defaultModeRight(defaultModeLeft + 15), tabExpandLeft(defaultModeRight + 3),
	      tabExpandRight(tabExpandLeft + 15), columnBlockMoveLeft(lineNumbersLeft),
	      columnBlockMoveRight(columnBlockMoveLeft + 18), indentStyleLeft(columnBlockMoveRight + 3),
	      indentStyleRight(indentStyleLeft + 15), fileTypeLeft(indentStyleRight + 3),
	      fileTypeRight(fileTypeLeft + 22), topY(config.topY), pageBreakY(topY),
	      wordDelimitersY(pageBreakY + (config.compactTextRows ? 1 : 2)),
	      defaultExtensionsY(config.includeDefaultExtensions ? wordDelimitersY + (config.compactTextRows ? 1 : 2) : -1),
	      tabSizeY(config.tabSizeY >= 0 ? config.tabSizeY
	                                   : (config.includeDefaultExtensions ? defaultExtensionsY + 1 : wordDelimitersY + 1)),
	      rightMarginY(tabSizeY + 1), postLoadMacroY(rightMarginY + 1), preSaveMacroY(postLoadMacroY + 1),
	      defaultPathY(preSaveMacroY + 1), formatLineY(defaultPathY + 1),
	      optionsHeadingY(config.clusterTopY >= 0 ? config.clusterTopY : formatLineY + 2), optionsBodyY(optionsHeadingY + 1),
		      tabExpandHeadingY(optionsHeadingY), tabExpandBodyY(optionsBodyY), columnBlockMoveHeadingY(optionsBodyY + 4),
		      columnBlockMoveBodyY(columnBlockMoveHeadingY + 1), indentStyleHeadingY(optionsBodyY + 4),
		      indentStyleBodyY(indentStyleHeadingY + 1), fileTypeHeadingY(optionsBodyY + 4),
	      fileTypeBodyY(fileTypeHeadingY + 1), miniMapHeadingY(std::max(optionsBodyY + 8, fileTypeBodyY + 4)),
	      miniMapBodyY(miniMapHeadingY + 1), contentBottomY(miniMapBodyY + 7) {
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
	TPanelGlyphButton(const TRect &bounds, const char *glyph, ushort command)
	    : TView(bounds), glyph_(glyph != nullptr ? glyph : ""), command_(command) {
		options |= ofSelectable;
		options |= ofFirstClick;
		eventMask |= evMouseDown | evKeyDown;
	}

	void draw() override {
		TDrawBuffer buffer;
		ushort color = getColor((state & sfFocused) != 0 ? 2 : 1);
		int glyphWidth = strwidth(glyph_.c_str());
		int x = std::max(0, (size.x - glyphWidth) / 2);

		buffer.moveChar(0, ' ', color, size.x);
		buffer.moveStr(static_cast<ushort>(x), glyph_.c_str(), color, size.x - x);
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
		message(target != nullptr ? target : owner, evCommand, command_, this);
	}

	std::string glyph_;
	ushort command_;
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

MRNumericSlider *addPanelNumericSlider(MRScrollableDialog &dialog, const TRect &rect, int32_t minValue,
                                       int32_t maxValue, int32_t initialValue, int32_t step,
                                       int32_t pageStep, ushort changedCmd) {
	MRNumericSlider *view =
	    new MRNumericSlider(rect, minValue, maxValue, initialValue, step, pageStep,
	                        MRNumericSlider::fmtRaw, changedCmd);
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
	if (text.empty())
		return fallback;
	char *end = nullptr;
	long parsed = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str() || end == nullptr || *end != '\0')
		return fallback;
	return std::clamp(static_cast<int>(parsed), minimum, maximum);
}

[[nodiscard]] std::string defaultFormatLineForTabSize(int tabSize) {
	const int normalizedTabSize = std::max(1, std::min(tabSize, kMaximumTabSize));
	const int targetWidth = 80;
	std::string out("!");

	while (static_cast<int>(out.size()) + normalizedTabSize + 1 <= targetWidth) {
		out.append(static_cast<std::size_t>(normalizedTabSize), '-');
		out.push_back('!');
	}
	if (out.size() == 1) {
		out.append(static_cast<std::size_t>(normalizedTabSize), '-');
		out.push_back('!');
	}
	return out;
}

void writeSliderValue(MRNumericSlider *slider, char *dest, std::size_t destSize, int fallback) {
	int32_t value = fallback;
	if (slider != nullptr)
		slider->getData(&value);
	writeRecordField(dest, destSize, std::to_string(value));
}

void writeIntegerInputValue(TInputLine *inputLine, char *dest, std::size_t destSize, int fallback, int minimum,
                            int maximum) {
	std::vector<char> buffer(destSize > 0 ? destSize : 1, '\0');

	if (inputLine != nullptr)
		inputLine->getData(buffer.data());
	const int value = parseIntegerOrDefault(buffer.data(), fallback, minimum, maximum);
	writeRecordField(dest, destSize, std::to_string(value));
}

std::string readInputFieldValue(TInputLine *inputLine) {
	std::vector<char> buffer(512, '\0');

	if (inputLine == nullptr)
		return std::string();
	inputLine->getData(buffer.data());
	return readRecordField(buffer.data());
}

void writeInputFieldValue(TInputLine *inputLine, const std::string &value) {
	std::vector<char> buffer(512, '\0');

	if (inputLine == nullptr)
		return;
	writeRecordField(buffer.data(), buffer.size(), value);
	inputLine->setData(buffer.data());
}

} // namespace

namespace MRFileExtensionProfilesDialogInternal {

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
	const int rightMarginFieldRight = g.inputLeft + 6;
	const int binaryLabelLeft = rightMarginFieldRight + 2;
	const int binaryFieldLeft = binaryLabelLeft + 23;
	const int binaryFieldRight = binaryFieldLeft + 5;
	const int printMarginFieldLeft = g.inputRight - (kPrintMarginFieldSize - 1);
	const int printMarginLabelLeft = printMarginFieldLeft - 14;

	addPanelLabel(dialog, TRect(g.labelLeft + 1, g.pageBreakY, g.inputLeft - 2, g.pageBreakY + 1),
	              "Page break:");
	pageBreakField = addPanelInput(dialog,
	                               TRect(g.inputLeft, g.pageBreakY, pageBreakRight, g.pageBreakY + 1),
	                               kPageBreakFieldSize - 1);

	addPanelLabel(dialog, TRect(printMarginLabelLeft, g.pageBreakY, printMarginFieldLeft - 1, g.pageBreakY + 1),
	              "Print Margin:");
	printMarginField = addPanelInput(dialog,
	                                 TRect(printMarginFieldLeft, g.pageBreakY, g.inputRight, g.pageBreakY + 1),
	                                 kPrintMarginFieldSize - 1);

	addPanelLabel(dialog, TRect(g.labelLeft + 1, g.wordDelimitersY, g.inputLeft - 2, g.wordDelimitersY + 1),
	              "Word delim:");
	wordDelimitersField = addPanelInput(dialog,
	                                    TRect(g.inputLeft, g.wordDelimitersY, wordDelimitersRight,
	                                          g.wordDelimitersY + 1),
	                                    kWordDelimsFieldSize - 1);

	if (config.includeDefaultExtensions) {
		addPanelLabel(dialog, TRect(g.labelLeft + 1, g.defaultExtensionsY, g.inputLeft - 2, g.defaultExtensionsY + 1),
		              "Auto ext.:");
		defaultExtensionsField = addPanelInput(
		    dialog, TRect(g.inputLeft, g.defaultExtensionsY, autoExtensionsRight, g.defaultExtensionsY + 1),
		    kDefaultExtsFieldSize - 1);
	}

	addPanelLabel(dialog, TRect(g.labelLeft + 1, g.tabSizeY, g.inputLeft - 2, g.tabSizeY + 1), "Tab size:");
	tabSizeSlider = addPanelNumericSlider(dialog, TRect(g.inputLeft, g.tabSizeY, g.inputRight, g.tabSizeY + 1),
	                                     kMinimumTabSize, kMaximumTabSize, kDefaultTabSize, 1, 4,
	                                     cmMrFileExtensionEditorSettingsPanelChanged);

	addPanelLabel(dialog, TRect(g.labelLeft + 1, g.rightMarginY, g.inputLeft - 2, g.rightMarginY + 1),
	              "Right margin:");
	rightMarginField = addPanelInput(dialog,
	                                 TRect(g.inputLeft, g.rightMarginY, rightMarginFieldRight, g.rightMarginY + 1),
	                                 kRightMarginFieldSize - 1);
	addPanelLabel(dialog, TRect(binaryLabelLeft, g.rightMarginY, binaryFieldLeft - 1, g.rightMarginY + 1),
	              "Binary record length:");
	binaryRecordLengthField = addPanelInput(dialog,
	                                       TRect(binaryFieldLeft, g.rightMarginY, binaryFieldRight, g.rightMarginY + 1),
	                                       kBinaryRecordLengthFieldSize - 1);

	addPanelLabel(dialog, TRect(g.labelLeft + 1, g.postLoadMacroY, g.inputLeft - 2, g.postLoadMacroY + 1),
	              "Post-load macro:");
	postLoadMacroField = addPanelInput(dialog,
	                                   TRect(g.inputLeft, g.postLoadMacroY, browseFieldRight, g.postLoadMacroY + 1),
	                                   kMacroFieldSize - 1);
	postLoadMacroBrowseButton = addPanelGlyphButton(
	    dialog, TRect(browseLeft, g.postLoadMacroY, g.inputRight, g.postLoadMacroY + 1), "🔎",
	    cmMrFileExtensionEditorSettingsPanelBrowsePostLoadMacro);

	addPanelLabel(dialog, TRect(g.labelLeft + 1, g.preSaveMacroY, g.inputLeft - 2, g.preSaveMacroY + 1),
	              "Pre-save macro:");
	preSaveMacroField = addPanelInput(dialog,
	                                  TRect(g.inputLeft, g.preSaveMacroY, browseFieldRight, g.preSaveMacroY + 1),
	                                  kMacroFieldSize - 1);
	preSaveMacroBrowseButton = addPanelGlyphButton(
	    dialog, TRect(browseLeft, g.preSaveMacroY, g.inputRight, g.preSaveMacroY + 1), "🔎",
	    cmMrFileExtensionEditorSettingsPanelBrowsePreSaveMacro);

	addPanelLabel(dialog, TRect(g.labelLeft + 1, g.defaultPathY, g.inputLeft - 2, g.defaultPathY + 1),
	              "Default path:");
	defaultPathField = addPanelInput(dialog,
	                                TRect(g.inputLeft, g.defaultPathY, browseFieldRight, g.defaultPathY + 1),
	                                kDefaultPathFieldSize - 1);
	defaultPathBrowseButton = addPanelGlyphButton(
	    dialog, TRect(browseLeft, g.defaultPathY, g.inputRight, g.defaultPathY + 1), "🔎",
	    cmMrFileExtensionEditorSettingsPanelBrowseDefaultPath);

	addPanelLabel(dialog, TRect(g.labelLeft + 1, g.formatLineY, g.inputLeft - 2, g.formatLineY + 1),
	              "Format line:");
	formatLineField = addPanelInput(dialog,
	                               TRect(g.inputLeft, g.formatLineY, g.inputRight - 2, g.formatLineY + 1),
	                               kFormatLineFieldSize - 1);

	addPanelLabel(dialog, TRect(g.optionsHeadingX, g.optionsHeadingY, config.dialogWidth - 2, g.optionsHeadingY + 1),
	              "Options:");
	optionsLeftField = addPanelCheckGroup(
	    dialog, TRect(g.optionsLeft, g.optionsBodyY, g.optionsRight, g.optionsBodyY + 6),
	    new TSItem("~T~runcate spaces",
	               new TSItem("Control-~Z~ at EOF",
	                          new TSItem("~C~R/LF at EOF",
	                                     new TSItem("Persistent ~B~locks",
	                                                new TSItem("leading ~0~ fill",
	                                                         new TSItem("word wrap", nullptr)))))));

	addPanelLabel(dialog, TRect(g.lineNumbersLeft, g.optionsHeadingY, g.lineNumbersRight, g.optionsHeadingY + 1),
	              "Line numbers:");
	lineNumbersField = addPanelRadioGroup(
	    dialog, TRect(g.lineNumbersLeft, g.optionsBodyY, g.lineNumbersRight, g.optionsBodyY + 3),
	    new TSItem("~O~ff", new TSItem("~L~eading", new TSItem("~T~railing", nullptr))));

	addPanelLabel(dialog, TRect(g.eofMarkerLeft, g.optionsHeadingY, g.eofMarkerRight, g.optionsHeadingY + 1),
	              "EOF marker:");
	eofMarkerField = addPanelRadioGroup(
	    dialog, TRect(g.eofMarkerLeft, g.optionsBodyY, g.eofMarkerRight, g.optionsBodyY + 3),
	    new TSItem("~N~one", new TSItem("~P~lain", new TSItem("~E~moji", nullptr))));

	addPanelLabel(dialog, TRect(g.defaultModeLeft, g.optionsHeadingY, g.defaultModeRight, g.optionsHeadingY + 1),
	              "Default mode:");
	defaultModeField = addPanelRadioGroup(
	    dialog, TRect(g.defaultModeLeft, g.optionsBodyY, g.defaultModeRight, g.optionsBodyY + 3),
	    new TSItem("~I~nsert", new TSItem("Ove~R~write", nullptr)));

	addPanelLabel(dialog, TRect(g.tabExpandLeft, g.tabExpandHeadingY, g.tabExpandRight, g.tabExpandHeadingY + 1),
	              "Tab expand:");
	tabExpandField = addPanelRadioGroup(
	    dialog, TRect(g.tabExpandLeft, g.tabExpandBodyY, g.tabExpandRight, g.tabExpandBodyY + 3),
	    new TSItem("~U~se tabs", new TSItem("Spa~X~es", nullptr)));

	addPanelLabel(dialog,
	              TRect(g.columnBlockMoveLeft, g.columnBlockMoveHeadingY, g.columnBlockMoveLeft + 18,
	                    g.columnBlockMoveHeadingY + 1),
	              "Column block move:");
	columnBlockMoveField = addPanelRadioGroup(
	    dialog,
	    TRect(g.columnBlockMoveLeft, g.columnBlockMoveBodyY, g.columnBlockMoveRight, g.columnBlockMoveBodyY + 3),
	    new TSItem("Re~M~ove space", new TSItem("~K~eep space", nullptr)));

	addPanelLabel(dialog, TRect(g.indentStyleLeft, g.indentStyleHeadingY, g.indentStyleRight, g.indentStyleHeadingY + 1),
	              "Indent style:");
	indentStyleField = addPanelRadioGroup(
	    dialog, TRect(g.indentStyleLeft, g.indentStyleBodyY, g.indentStyleRight, g.indentStyleBodyY + 3),
	    new TSItem("off", new TSItem("automatic", new TSItem("smart", nullptr))));

	addPanelLabel(dialog, TRect(g.fileTypeLeft, g.fileTypeHeadingY, g.fileTypeRight, g.fileTypeHeadingY + 1),
	              "File type:");
	fileTypeField = addPanelRadioGroup(
	    dialog, TRect(g.fileTypeLeft, g.fileTypeBodyY, g.fileTypeRight, g.fileTypeBodyY + 3),
	    new TSItem("legacy text (CR/LF)", new TSItem("UNIX (LF)", new TSItem("binary", nullptr))));

	addPanelLabel(dialog, TRect(g.optionsLeft, g.miniMapHeadingY, g.optionsLeft + 18, g.miniMapHeadingY + 1),
	              "Mini map:");
	miniMapPositionField = addPanelRadioGroup(
	    dialog, TRect(g.optionsLeft, g.miniMapBodyY, g.optionsLeft + 24, g.miniMapBodyY + 3),
	    new TSItem("~O~ff", new TSItem("~L~eading", new TSItem("~T~railing", nullptr))));
	addPanelLabel(dialog, TRect(g.optionsLeft + 27, g.miniMapHeadingY, g.optionsLeft + 44, g.miniMapHeadingY + 1),
	              "Code folding:");
	codeFoldingPositionField = addPanelRadioGroup(
	    dialog, TRect(g.optionsLeft + 27, g.miniMapBodyY, g.optionsLeft + 44, g.miniMapBodyY + 3),
	    new TSItem("~O~ff", new TSItem("~L~eading", new TSItem("~T~railing", nullptr))));
	addPanelLabel(dialog,
	              TRect(g.optionsLeft, g.miniMapBodyY + 3, g.optionsLeft + 24, g.miniMapBodyY + 4),
	              "Width:");
	miniMapWidthSlider = addPanelNumericSlider(
	    dialog, TRect(g.optionsLeft, g.miniMapBodyY + 4, g.optionsLeft + 24, g.miniMapBodyY + 5),
	    kMinimumMiniMapWidth, kMaximumMiniMapWidth, kDefaultMiniMapWidth, 1, 2,
	                                     cmMrFileExtensionEditorSettingsPanelChanged);
	addPanelLabel(dialog,
	              TRect(g.optionsLeft, g.miniMapBodyY + 5, g.optionsLeft + 19, g.miniMapBodyY + 6),
	              "Viewport cursor:");
	miniMapMarkerGlyphField = addPanelInput(
	    dialog, TRect(g.optionsLeft + 19, g.miniMapBodyY + 5, g.optionsLeft + 24, g.miniMapBodyY + 6),
	    kMiniMapMarkerGlyphFieldSize - 1);
	addPanelLabel(dialog,
	              TRect(g.optionsLeft, g.miniMapBodyY + 6, g.optionsLeft + 16, g.miniMapBodyY + 7),
	              "Gutters:");
	guttersField = addPanelInput(
	    dialog, TRect(g.optionsLeft + 16, g.miniMapBodyY + 6, g.optionsLeft + 24, g.miniMapBodyY + 7),
	    kGuttersFieldSize - 1);
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

	if (optionsLeftField != nullptr)
		optionsLeftField->getData((void *)&leftMask);
	if (lineNumbersField != nullptr)
		lineNumbersField->getData((void *)&lineNumbersChoice);
	if (codeFoldingPositionField != nullptr)
		codeFoldingPositionField->getData((void *)&codeFoldingChoice);
	if (eofMarkerField != nullptr)
		eofMarkerField->getData((void *)&eofMarkerChoice);

	if ((leftMask & kLeftOptionTruncateSpaces) != 0)
		options |= kOptionTruncateSpaces;
	if ((leftMask & kLeftOptionEofCtrlZ) != 0)
		options |= kOptionEofCtrlZ;
	if ((leftMask & kLeftOptionEofCrLf) != 0)
		options |= kOptionEofCrLf;
	if ((leftMask & kLeftOptionPersistentBlocks) != 0)
		options |= kOptionPersistentBlocks;
	if ((leftMask & kLeftOptionLineNumZeroFill) != 0)
		options |= kOptionLineNumZeroFill;
	if ((leftMask & kLeftOptionWordWrap) != 0)
		options |= kOptionWordWrap;

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
	if (codeFoldingChoice != kCodeFoldingOff)
		options |= kOptionCodeFolding;

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

	return options | preservedOptionsMask;
}

void FileExtensionEditorSettingsPanel::setOptionsMask(ushort options) {
	ushort leftMask = 0;
	ushort lineNumbersChoice = kLineNumbersOff;
	ushort codeFoldingChoice = kCodeFoldingOff;
	ushort eofMarkerChoice = kEofMarkerOff;

	if ((options & kOptionTruncateSpaces) != 0)
		leftMask |= kLeftOptionTruncateSpaces;
	if ((options & kOptionEofCtrlZ) != 0)
		leftMask |= kLeftOptionEofCtrlZ;
	if ((options & kOptionEofCrLf) != 0)
		leftMask |= kLeftOptionEofCrLf;
	if ((options & kOptionPersistentBlocks) != 0)
		leftMask |= kLeftOptionPersistentBlocks;
	if ((options & kOptionLineNumZeroFill) != 0)
		leftMask |= kLeftOptionLineNumZeroFill;
	if ((options & kOptionWordWrap) != 0)
		leftMask |= kLeftOptionWordWrap;

	if ((options & kOptionShowLineNumbers) != 0)
		lineNumbersChoice = kLineNumbersLeading;
	if ((options & kOptionCodeFolding) != 0)
		codeFoldingChoice = kCodeFoldingLeading;
	if ((options & kOptionShowEofMarker) != 0)
		eofMarkerChoice = ((options & kOptionShowEofMarkerEmoji) != 0) ? kEofMarkerEmoji : kEofMarkerPlain;

	preservedOptionsMask = options & ~kUiManagedOptionsMask;

	if (optionsLeftField != nullptr)
		optionsLeftField->setData((void *)&leftMask);
	if (lineNumbersField != nullptr)
		lineNumbersField->setData((void *)&lineNumbersChoice);
	if (codeFoldingPositionField != nullptr)
		codeFoldingPositionField->setData((void *)&codeFoldingChoice);
	if (eofMarkerField != nullptr)
		eofMarkerField->setData((void *)&eofMarkerChoice);
}

void FileExtensionEditorSettingsPanel::loadFieldsFromRecord(const FileExtensionEditorSettingsDialogRecord &record) {
	setInputLineValue(pageBreakField, record.pageBreak, sizeof(record.pageBreak));
	setInputLineValue(printMarginField, record.printMargin, sizeof(record.printMargin));
	setInputLineValue(wordDelimitersField, record.wordDelimiters, sizeof(record.wordDelimiters));
	if (defaultExtensionsField != nullptr)
		setInputLineValue(defaultExtensionsField, record.defaultExtensions, sizeof(record.defaultExtensions));
	if (tabSizeSlider != nullptr) {
		int32_t value = parseIntegerOrDefault(record.tabSize, kDefaultTabSize, kMinimumTabSize, kMaximumTabSize);
		tabSizeSlider->setData(&value);
		lastKnownTabSizeForFormatLine = static_cast<int>(value);
	}
	setInputLineValue(rightMarginField, record.rightMargin, sizeof(record.rightMargin));
	setInputLineValue(binaryRecordLengthField, record.binaryRecordLength, sizeof(record.binaryRecordLength));
	setInputLineValue(postLoadMacroField, record.postLoadMacro, sizeof(record.postLoadMacro));
	setInputLineValue(preSaveMacroField, record.preSaveMacro, sizeof(record.preSaveMacro));
	setInputLineValue(defaultPathField, record.defaultPath, sizeof(record.defaultPath));
	setInputLineValue(formatLineField, record.formatLine, sizeof(record.formatLine));
	setOptionsMask(record.optionsMask);
	if (lineNumbersField != nullptr)
		lineNumbersField->setData((void *)&record.lineNumbersPositionChoice);
	if (codeFoldingPositionField != nullptr)
		codeFoldingPositionField->setData((void *)&record.codeFoldingPositionChoice);
	if (tabExpandField != nullptr)
		tabExpandField->setData((void *)&record.tabExpandChoice);
	if (indentStyleField != nullptr)
		indentStyleField->setData((void *)&record.indentStyleChoice);
	if (fileTypeField != nullptr)
		fileTypeField->setData((void *)&record.fileTypeChoice);
	if (columnBlockMoveField != nullptr)
		columnBlockMoveField->setData((void *)&record.columnBlockMoveChoice);
	if (defaultModeField != nullptr)
		defaultModeField->setData((void *)&record.defaultModeChoice);
	if (miniMapPositionField != nullptr)
		miniMapPositionField->setData((void *)&record.miniMapPositionChoice);
	if (miniMapWidthSlider != nullptr) {
		int32_t value =
		    parseIntegerOrDefault(record.miniMapWidth, kDefaultMiniMapWidth, kMinimumMiniMapWidth, kMaximumMiniMapWidth);
		miniMapWidthSlider->setData(&value);
	}
	setInputLineValue(miniMapMarkerGlyphField, record.miniMapMarkerGlyph, sizeof(record.miniMapMarkerGlyph));
	setInputLineValue(guttersField, record.gutters, sizeof(record.gutters));
	syncDynamicStates();
}

void FileExtensionEditorSettingsPanel::saveFieldsToRecord(FileExtensionEditorSettingsDialogRecord &record) const {
	readInputLineValue(pageBreakField, record.pageBreak, sizeof(record.pageBreak));
	readInputLineValue(wordDelimitersField, record.wordDelimiters, sizeof(record.wordDelimiters));
	if (defaultExtensionsField != nullptr)
		readInputLineValue(defaultExtensionsField, record.defaultExtensions, sizeof(record.defaultExtensions));
	writeSliderValue(tabSizeSlider, record.tabSize, sizeof(record.tabSize), kDefaultTabSize);
	writeIntegerInputValue(rightMarginField, record.rightMargin, sizeof(record.rightMargin), kDefaultRightMargin,
	                      kMinimumRightMargin, kMaximumRightMargin);
	writeIntegerInputValue(printMarginField, record.printMargin, sizeof(record.printMargin), kDefaultRightMargin,
	                      kMinimumRightMargin, kMaximumRightMargin);
	writeIntegerInputValue(binaryRecordLengthField, record.binaryRecordLength, sizeof(record.binaryRecordLength),
	                      kDefaultBinaryRecordLength, kMinimumBinaryRecordLength, kMaximumBinaryRecordLength);
	readInputLineValue(postLoadMacroField, record.postLoadMacro, sizeof(record.postLoadMacro));
	readInputLineValue(preSaveMacroField, record.preSaveMacro, sizeof(record.preSaveMacro));
	readInputLineValue(defaultPathField, record.defaultPath, sizeof(record.defaultPath));
	readInputLineValue(formatLineField, record.formatLine, sizeof(record.formatLine));
	record.optionsMask = currentOptionsMask();
	if (lineNumbersField != nullptr)
		lineNumbersField->getData((void *)&record.lineNumbersPositionChoice);
	if (codeFoldingPositionField != nullptr)
		codeFoldingPositionField->getData((void *)&record.codeFoldingPositionChoice);
	if (tabExpandField != nullptr)
		tabExpandField->getData((void *)&record.tabExpandChoice);
	if (indentStyleField != nullptr)
		indentStyleField->getData((void *)&record.indentStyleChoice);
	if (fileTypeField != nullptr)
		fileTypeField->getData((void *)&record.fileTypeChoice);
	if (columnBlockMoveField != nullptr)
		columnBlockMoveField->getData((void *)&record.columnBlockMoveChoice);
	if (defaultModeField != nullptr)
		defaultModeField->getData((void *)&record.defaultModeChoice);
	if (miniMapPositionField != nullptr)
		miniMapPositionField->getData((void *)&record.miniMapPositionChoice);
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
	std::string previousAutoFormat = defaultFormatLineForTabSize(lastKnownTabSizeForFormatLine);
	std::string currentFormatLine = readInputFieldValue(formatLineField);

	if (tabSizeSlider != nullptr)
		tabSizeSlider->getData(&currentTabSize);
	const bool formatLineBlank =
	    currentFormatLine.find_first_not_of(" \t\r\n") == std::string::npos;
	if (formatLineBlank || currentFormatLine == previousAutoFormat)
		writeInputFieldValue(formatLineField, defaultFormatLineForTabSize(static_cast<int>(currentTabSize)));
	lastKnownTabSizeForFormatLine = static_cast<int>(currentTabSize);

	if (binaryRecordLengthField != nullptr)
		binaryRecordLengthField->setState(sfDisabled, binaryEnabled ? False : True);
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

void FileExtensionEditorSettingsPanel::setPostLoadMacroValue(const std::string &value) {
	if (postLoadMacroField != nullptr)
		writeInputFieldValue(postLoadMacroField, value);
}

void FileExtensionEditorSettingsPanel::setPreSaveMacroValue(const std::string &value) {
	if (preSaveMacroField != nullptr)
		writeInputFieldValue(preSaveMacroField, value);
}

void FileExtensionEditorSettingsPanel::setDefaultPathValue(const std::string &value) {
	if (defaultPathField != nullptr)
		writeInputFieldValue(defaultPathField, value);
}

} // namespace MRFileExtensionProfilesDialogInternal
