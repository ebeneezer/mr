#ifndef MRFILEEXTENSIONEDITORSETTINGSPANELINTERNAL_HPP
#define MRFILEEXTENSIONEDITORSETTINGSPANELINTERNAL_HPP

#include <tvision/tv.h>

#include <cstddef>

class MRScrollableDialog;
class MRNumericSlider;
class TCheckBoxes;
class TInputLine;
class TRadioButtons;
class TView;

namespace MRFileExtensionProfilesInternal {

enum : ushort {
	cmMrFileExtensionEditorSettingsPanelChanged = 3860,
	cmMrFileExtensionEditorSettingsPanelFocusChanged,
	cmMrFileExtensionEditorSettingsPanelBrowsePostLoadMacro,
	cmMrFileExtensionEditorSettingsPanelBrowsePreSaveMacro,
	cmMrFileExtensionEditorSettingsPanelBrowseDefaultPath
};

enum {
	kPageBreakFieldSize = 64,
	kWordDelimsFieldSize = 256,
	kDefaultExtsFieldSize = 256,
	kTabSizeFieldSize = 8,
	kLeftMarginFieldSize = 8,
	kRightMarginFieldSize = 8,
	kBinaryRecordLengthFieldSize = 8,
	kMacroFieldSize = 256,
	kDefaultPathFieldSize = 256,
	kFormatLineFieldSize = 256,
	kCursorStatusColorFieldSize = 8,
	kMiniMapWidthFieldSize = 8,
	kMiniMapMarkerGlyphFieldSize = 5,
	kGuttersFieldSize = 8
};

enum : ushort {
	kOptionTruncateSpaces = 0x0001,
	kOptionEofCtrlZ = 0x0002,
	kOptionEofCrLf = 0x0004,
	kOptionShowLineNumbers = 0x0008,
	kOptionLineNumZeroFill = 0x0010,
	kOptionPersistentBlocks = 0x0020,
	kOptionCodeFolding = 0x0040,
	kOptionShowEofMarker = 0x0080,
	kOptionShowEofMarkerEmoji = 0x0100,
	kOptionWordWrap = 0x0200,
	kOptionDisplayTabs = 0x0400,
	kOptionFormatRuler = 0x0800
};

enum : ushort {
	kLeftOptionTruncateSpaces = 0x0001,
	kLeftOptionEofCtrlZ = 0x0002,
	kLeftOptionEofCrLf = 0x0004,
	kLeftOptionPersistentBlocks = 0x0008,
	kLeftOptionLineNumZeroFill = 0x0010,
	kLeftOptionWordWrap = 0x0020,
	kLeftOptionDisplayTabs = 0x0040,
	kLeftOptionFormatRuler = 0x0080
};

enum : ushort {
	kLineNumbersOff = 0,
	kLineNumbersLeading = 1,
	kLineNumbersTrailing = 2
};

enum : ushort {
	kEofMarkerOff = 0,
	kEofMarkerPlain = 1,
	kEofMarkerEmoji = 2
};

enum : ushort {
	kTabExpandTabs = 0,
	kTabExpandSpaces = 1
};

enum : ushort {
	kIndentStyleOff = 0,
	kIndentStyleAutomatic = 1,
	kIndentStyleSmart = 2
};

enum : ushort {
	kFileTypeLegacyText = 0,
	kFileTypeUnix = 1,
	kFileTypeBinary = 2
};

enum : ushort {
	kColumnMoveDeleteSpace = 0,
	kColumnMoveLeaveSpace = 1
};

enum : ushort {
	kDefaultModeInsert = 0,
	kDefaultModeOverwrite = 1
};

enum : ushort {
	kMiniMapOff = 0,
	kMiniMapLeading = 1,
	kMiniMapTrailing = 2
};

enum : ushort {
	kCodeFoldingOff = 0,
	kCodeFoldingLeading = 1,
	kCodeFoldingTrailing = 2
};

struct FileExtensionEditorSettingsDialogRecord {
	char pageBreak[kPageBreakFieldSize];
	char wordDelimiters[kWordDelimsFieldSize];
	char defaultExtensions[kDefaultExtsFieldSize];
	char tabSize[kTabSizeFieldSize];
	char leftMargin[kLeftMarginFieldSize];
	char rightMargin[kRightMarginFieldSize];
	char binaryRecordLength[kBinaryRecordLengthFieldSize];
	char postLoadMacro[kMacroFieldSize];
	char preSaveMacro[kMacroFieldSize];
	char defaultPath[kDefaultPathFieldSize];
	char formatLine[kFormatLineFieldSize];
	char cursorStatusColor[kCursorStatusColorFieldSize];
	char miniMapWidth[kMiniMapWidthFieldSize];
	char miniMapMarkerGlyph[kMiniMapMarkerGlyphFieldSize];
	char gutters[kGuttersFieldSize];
	ushort optionsMask;
	ushort tabExpandChoice;
	ushort indentStyleChoice;
	ushort fileTypeChoice;
	ushort columnBlockMoveChoice;
	ushort defaultModeChoice;
	ushort lineNumbersPositionChoice;
	ushort codeFoldingPositionChoice;
	ushort miniMapPositionChoice;
};

struct FileExtensionEditorSettingsPanelConfig {
	int topY = 2;
	int dialogWidth = 88;
	int labelLeft = 2;
	int inputLeft = 32;
	int inputRight = -1;
	int clusterLeft = -1;
	int clusterTopY = -1;
	int tabSizeY = -1;
	bool includeDefaultExtensions = true;
	bool compactTextRows = false;
	bool tabExpandBesideDefaultMode = false;
};

class FileExtensionEditorSettingsPanel {
  public:
	explicit FileExtensionEditorSettingsPanel(const FileExtensionEditorSettingsPanelConfig &config = FileExtensionEditorSettingsPanelConfig());
	void buildViews(MRScrollableDialog &dialog);
	void loadFieldsFromRecord(const FileExtensionEditorSettingsDialogRecord &record);
	void saveFieldsToRecord(FileExtensionEditorSettingsDialogRecord &record) const;
	[[nodiscard]] std::string postLoadMacroValue() const;
	[[nodiscard]] std::string preSaveMacroValue() const;
	[[nodiscard]] std::string defaultPathValue() const;
	[[nodiscard]] bool formatRulerEnabled() const noexcept;
	[[nodiscard]] int currentFormatLineTabSize() const noexcept;
	[[nodiscard]] int currentFormatLineLeftMargin() const noexcept;
	[[nodiscard]] int currentFormatLineRightMargin() const noexcept;
	[[nodiscard]] std::string currentFormatLineValue() const;
	void applyFormatLineState(const std::string &value, int leftMargin, int rightMargin);
	void setPostLoadMacroValue(const std::string &value);
	void setPreSaveMacroValue(const std::string &value);
	void setDefaultPathValue(const std::string &value);
	void syncDynamicStates();
	TView *primaryView() const noexcept;

  private:
	[[nodiscard]] ushort currentOptionsMask() const noexcept;
	void setOptionsMask(ushort options);
	static void setInputLineValue(TInputLine *inputLine, const char *value, std::size_t capacity);
	static void readInputLineValue(TInputLine *inputLine, char *dest, std::size_t destSize);

	FileExtensionEditorSettingsPanelConfig config;
	TInputLine *pageBreakField = nullptr;
	TInputLine *wordDelimitersField = nullptr;
	TInputLine *defaultExtensionsField = nullptr;
	MRNumericSlider *tabSizeSlider = nullptr;
	TInputLine *leftMarginField = nullptr;
	TInputLine *rightMarginField = nullptr;
	TInputLine *binaryRecordLengthField = nullptr;
	ushort preservedOptionsMask = 0;
	TInputLine *postLoadMacroField = nullptr;
	TInputLine *preSaveMacroField = nullptr;
	TInputLine *defaultPathField = nullptr;
	TInputLine *formatLineField = nullptr;
	TView *formatRulerView = nullptr;
	TView *postLoadMacroBrowseButton = nullptr;
	TView *preSaveMacroBrowseButton = nullptr;
	TView *defaultPathBrowseButton = nullptr;
	TCheckBoxes *optionsLeftField = nullptr;
	TRadioButtons *lineNumbersField = nullptr;
	TRadioButtons *codeFoldingPositionField = nullptr;
	TRadioButtons *miniMapPositionField = nullptr;
	TRadioButtons *eofMarkerField = nullptr;
	TRadioButtons *tabExpandField = nullptr;
	TRadioButtons *indentStyleField = nullptr;
	TRadioButtons *fileTypeField = nullptr;
	TRadioButtons *columnBlockMoveField = nullptr;
	TRadioButtons *defaultModeField = nullptr;
	MRNumericSlider *miniMapWidthSlider = nullptr;
	TInputLine *miniMapMarkerGlyphField = nullptr;
	TInputLine *guttersField = nullptr;
	int lastKnownLeftMarginForFormatLine = 1;
	int lastKnownTabSizeForFormatLine = 8;
	int lastKnownRightMarginForFormatLine = 78;
};

} // namespace MRFileExtensionProfilesInternal

#endif
