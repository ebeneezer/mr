#ifndef MREDITSETTINGSDIALOGINTERNAL_HPP
#define MREDITSETTINGSDIALOGINTERNAL_HPP

#include <tvision/tv.h>

#include <cstddef>
#include <string>

struct MREditSetupSettings;

namespace MREditSettingsDialogInternal {

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
	kOptionShowEofMarker = 0x0080,
	kOptionShowEofMarkerEmoji = 0x0100
};

enum : ushort {
	kLeftOptionTruncateSpaces = 0x0001,
	kLeftOptionEofCtrlZ = 0x0002,
	kLeftOptionEofCrLf = 0x0004,
	kLeftOptionBackupFiles = 0x0008,
	kLeftOptionPersistentBlocks = 0x0010
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

std::string trimAscii(const std::string &value);
std::string upperAscii(std::string value);
std::string readRecordField(const char *value);
void writeRecordField(char *dest, std::size_t destSize, const std::string &value);
bool recordsEqual(const EditSettingsDialogRecord &lhs, const EditSettingsDialogRecord &rhs);
void initEditSettingsDialogRecord(EditSettingsDialogRecord &record);
bool recordToSettings(const EditSettingsDialogRecord &record, MREditSetupSettings &settings,
                      std::string &errorText);
bool saveAndReloadEditSettings(const EditSettingsDialogRecord &record, std::string &errorText);

ushort buildOptionsMask(ushort leftMask, ushort lineNumbersChoice, ushort eofMarkerChoice) noexcept;
void splitOptionsMask(ushort options, ushort &leftMask, ushort &lineNumbersChoice,
                      ushort &eofMarkerChoice) noexcept;

} // namespace MREditSettingsDialogInternal

#endif
