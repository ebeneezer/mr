#ifndef MRSETTINGSEDITCONSTANTS_HPP
#define MRSETTINGSEDITCONSTANTS_HPP

constexpr const char * kDefaultPageBreakLiteral = "\\f";
constexpr const char * kDefaultWordDelimiters = ".()'\\\",#$012%^&*+-/[]?";
constexpr const char * kDefaultDefaultExtensions = "PAS;ASM;BAT;TXT;DO";
constexpr const char * kColumnBlockMoveDelete = "DELETE_SPACE";
constexpr const char * kColumnBlockMoveLeave = "LEAVE_SPACE";
constexpr const char * kDefaultModeInsert = "INSERT";
constexpr const char * kDefaultModeOverwrite = "OVERWRITE";
constexpr const char * kMiniMapPositionOff = "OFF";
constexpr const char * kMiniMapPositionLeading = "LEADING";
constexpr const char * kMiniMapPositionTrailing = "TRAILING";
constexpr const char * kLineNumbersPositionOff = "OFF";
constexpr const char * kLineNumbersPositionLeading = "LEADING";
constexpr const char * kLineNumbersPositionTrailing = "TRAILING";
constexpr const char * kCodeFoldingPositionOff = "OFF";
constexpr const char * kCodeFoldingPositionLeading = "LEADING";
constexpr const char * kDefaultGuttersOrder = "LCM";
constexpr const char * kIndentStyleOff = "OFF";
constexpr const char * kIndentStyleAutomatic = "AUTOMATIC";
constexpr const char * kIndentStyleSmart = "SMART";
constexpr const char * kFileTypeLegacyText = "LEGACY_TEXT";
constexpr const char * kFileTypeUnix = "UNIX";
constexpr const char * kFileTypeBinary = "BINARY";
constexpr const char *kBackupMethodOff = "OFF";
constexpr const char *kBackupMethodBakFile = "BAK_FILE";
constexpr const char *kBackupMethodDirectory = "DIRECTORY";
constexpr const char *kBackupFrequencyFirstSaveOnly = "FIRST_SAVE_ONLY";
constexpr const char *kBackupFrequencyEverySave = "EVERY_SAVE";
constexpr int kMinAutosaveInactivitySeconds = 5;
constexpr int kMaxAutosaveInactivitySeconds = 100;
constexpr int kMinAutosaveIntervalSeconds = 100;
constexpr int kMaxAutosaveIntervalSeconds = 300;
constexpr int kDefaultTabSize = 8;
constexpr int kMinTabSize = 2;
constexpr int kMaxTabSize = 32;
constexpr int kDefaultMiniMapWidth = 4;
constexpr int kMinMiniMapWidth = 2;
constexpr int kMaxMiniMapWidth = 20;
constexpr const char * kWindowColorThemeProfileKey = "WINDOW_COLORTHEME_URI";

#endif
