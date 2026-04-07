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
	cmMrEditSettingsPanelFocusChanged
};

enum {
	kPageBreakFieldSize = 64,
	kWordDelimsFieldSize = 256,
	kDefaultExtsFieldSize = 256,
	kTabSizeFieldSize = 8
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
	kOptionShowEofMarkerEmoji = 0x0200
};

enum : ushort {
	kLeftOptionTruncateSpaces = 0x0001,
	kLeftOptionEofCtrlZ = 0x0002,
	kLeftOptionEofCrLf = 0x0004,
	kLeftOptionBackupFiles = 0x0008,
	kLeftOptionPersistentBlocks = 0x0010,
	kLeftOptionCodeFolding = 0x0020
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
	ushort optionsMask;
	ushort tabExpandChoice;
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
	TCheckBoxes *optionsLeftField_ = nullptr;
	TRadioButtons *lineNumbersField_ = nullptr;
	TRadioButtons *eofMarkerField_ = nullptr;
	TRadioButtons *tabExpandField_ = nullptr;
	TRadioButtons *columnBlockMoveField_ = nullptr;
	TRadioButtons *defaultModeField_ = nullptr;
};

} // namespace MREditProfilesDialogInternal

#endif
