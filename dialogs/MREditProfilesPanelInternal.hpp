#ifndef MREDITPROFILESPANELINTERNAL_HPP
#define MREDITPROFILESPANELINTERNAL_HPP

#include <tvision/tv.h>

#include <cstddef>

class MRScrollableDialog;
class MRNumericSlider;
class TCheckBoxes;
class TInputLine;
class TRadioButtons;
class TView;

namespace MREditProfilesDialogInternal {

enum : ushort {
	cmMrEditSettingsPanelChanged = 3860,
	cmMrEditSettingsPanelFocusChanged,
	cmMrEditSettingsPanelBrowsePostLoadMacro,
	cmMrEditSettingsPanelBrowsePreSaveMacro,
	cmMrEditSettingsPanelBrowseDefaultPath
};

enum {
	kPageBreakFieldSize = 64,
	kWordDelimsFieldSize = 256,
	kDefaultExtsFieldSize = 256,
	kTabSizeFieldSize = 8,
	kRightMarginFieldSize = 8,
	kBinaryRecordLengthFieldSize = 8,
	kMacroFieldSize = 256,
	kDefaultPathFieldSize = 256,
	kFormatLineFieldSize = 256,
	kCursorStatusColorFieldSize = 8
};

enum : ushort {
	kOptionTruncateSpaces = 0x0001,
	kOptionEofCtrlZ = 0x0002,
	kOptionEofCrLf = 0x0004,
	kOptionShowLineNumbers = 0x0008,
	kOptionLineNumZeroFill = 0x0010,
	kOptionPersistentBlocks = 0x0020,
	kOptionBackupFiles = 0x0040,
	kOptionCodeFolding = 0x0080,
	kOptionShowEofMarker = 0x0100,
	kOptionShowEofMarkerEmoji = 0x0200,
	kOptionWordWrap = 0x0400
};

enum : ushort {
	kLeftOptionTruncateSpaces = 0x0001,
	kLeftOptionEofCtrlZ = 0x0002,
	kLeftOptionEofCrLf = 0x0004,
	kLeftOptionBackupFiles = 0x0008,
	kLeftOptionPersistentBlocks = 0x0010,
	kLeftOptionCodeFolding = 0x0020,
	kLeftOptionWordWrap = 0x0040
};

enum : ushort {
	kLineNumbersOff = 0,
	kLineNumbersOn = 1,
	kLineNumbersLeadingZero = 2
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

struct EditSettingsDialogRecord {
	char pageBreak[kPageBreakFieldSize];
	char wordDelimiters[kWordDelimsFieldSize];
	char defaultExtensions[kDefaultExtsFieldSize];
	char tabSize[kTabSizeFieldSize];
	char rightMargin[kRightMarginFieldSize];
	char binaryRecordLength[kBinaryRecordLengthFieldSize];
	char postLoadMacro[kMacroFieldSize];
	char preSaveMacro[kMacroFieldSize];
	char defaultPath[kDefaultPathFieldSize];
	char formatLine[kFormatLineFieldSize];
	char cursorStatusColor[kCursorStatusColorFieldSize];
	ushort optionsMask;
	ushort tabExpandChoice;
	ushort indentStyleChoice;
	ushort fileTypeChoice;
	ushort columnBlockMoveChoice;
	ushort defaultModeChoice;
};

struct EditSettingsPanelConfig {
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

class EditSettingsPanel {
  public:
	explicit EditSettingsPanel(const EditSettingsPanelConfig &config = EditSettingsPanelConfig());
	void buildViews(MRScrollableDialog &dialog);
	void loadFieldsFromRecord(const EditSettingsDialogRecord &record);
	void saveFieldsToRecord(EditSettingsDialogRecord &record) const;
	[[nodiscard]] std::string postLoadMacroValue() const;
	[[nodiscard]] std::string preSaveMacroValue() const;
	[[nodiscard]] std::string defaultPathValue() const;
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

	EditSettingsPanelConfig config_;
	TInputLine *pageBreakField_ = nullptr;
	TInputLine *wordDelimitersField_ = nullptr;
	TInputLine *defaultExtensionsField_ = nullptr;
	MRNumericSlider *tabSizeSlider_ = nullptr;
	TInputLine *rightMarginField_ = nullptr;
	TInputLine *binaryRecordLengthField_ = nullptr;
	ushort preservedOptionsMask_ = 0;
		TInputLine *postLoadMacroField_ = nullptr;
		TInputLine *preSaveMacroField_ = nullptr;
		TInputLine *defaultPathField_ = nullptr;
		TInputLine *formatLineField_ = nullptr;
		TView *postLoadMacroBrowseButton_ = nullptr;
		TView *preSaveMacroBrowseButton_ = nullptr;
		TView *defaultPathBrowseButton_ = nullptr;
	TCheckBoxes *optionsLeftField_ = nullptr;
	TRadioButtons *lineNumbersField_ = nullptr;
	TRadioButtons *eofMarkerField_ = nullptr;
	TRadioButtons *tabExpandField_ = nullptr;
	TRadioButtons *indentStyleField_ = nullptr;
	TRadioButtons *fileTypeField_ = nullptr;
	TRadioButtons *columnBlockMoveField_ = nullptr;
	TRadioButtons *defaultModeField_ = nullptr;
};

} // namespace MREditProfilesDialogInternal

#endif
