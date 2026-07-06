#include "../../app/MRVersion.hpp"
#include "../../app/utils/MRFileIOUtils.hpp"
#include "../../app/utils/MRStringUtils.hpp"
#include "../../keymap/MRKeymapResolver.hpp"
#include "../../mrmac/MRMacroRunner.hpp"
#include "../../ui/MRWindowSupport.hpp"
#include "MRSettingsHistory.hpp"
#include "MRSettingsRuntime.hpp"
#include "MRSettingsRuntimeState.hpp"
#include "MRSettingsThemesProfiles.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <map>
#include <regex>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <vector>

namespace {

bool setError(std::string *errorMessage, const std::string &message) {
	if (errorMessage != nullptr) *errorMessage = message;
	return false;
}

std::string toUpperHexByte(unsigned char value);

std::string summarizeConfiguredKeymapsForLog(const std::vector<MRKeymapProfile> &profiles, std::string_view activeProfileName) {
	std::string text = "Keymap configured state: active='" + std::string(activeProfileName) + "' profiles=" + std::to_string(profiles.size());

	for (const MRKeymapProfile &profile : profiles)
		text += " [" + profile.name + ":" + std::to_string(profile.bindings.size()) + "]";
	return text;
}

std::string fileNamePartOf(std::string_view path) {
	std::size_t sep = path.find_last_of("/\\");
	if (sep == std::string_view::npos) return std::string(path);
	return std::string(path.substr(sep + 1));
}

std::string appendFileName(std::string_view directory, const char *fileName) {
	std::string result(directory);

	if (!result.empty() && result.back() != '/' && result.back() != '\\') result.push_back('/');
	result += fileName;
	return result;
}

std::string builtInTempDirectoryPath() {
	std::string cwd;

	if (isWritableDirectory("/tmp")) return "/tmp";
	cwd = currentWorkingDirectory();
	if (!cwd.empty() && isWritableDirectory(cwd)) return cwd;
	return "/tmp";
}

std::string toUpperHexByte(unsigned char value) {
	static constexpr char digits[] = "0123456789ABCDEF";
	std::string text(2, '0');

	text[0] = digits[(value >> 4) & 0x0F];
	text[1] = digits[value & 0x0F];
	return text;
}

std::string escapeMrmacSingleQuotedLiteral(const std::string &value) {
	std::string out;

	out.reserve(value.size());
	for (char ch : value) {
		if (ch == '\'') out.push_back('\'');
		out.push_back(ch);
	}
	return out;
}

std::string unescapeMrmacSingleQuotedLiteral(const std::string &value) {
	std::string out;

	out.reserve(value.size());
	for (std::size_t i = 0; i < value.size(); ++i) {
		if (value[i] == '\'' && i + 1 < value.size() && value[i + 1] == '\'') {
			out.push_back('\'');
			++i;
		} else
			out.push_back(value[i]);
	}
	return out;
}

bool ensureDirectoryTree(const std::string &directoryPath, std::string *errorMessage) {
	struct stat st;
	std::string parentPath;

	if (directoryPath.empty() || directoryPath == "." || directoryPath == "/") {
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (::stat(directoryPath.c_str(), &st) == 0) {
		if (S_ISDIR(st.st_mode)) {
			if (errorMessage != nullptr) errorMessage->clear();
			return true;
		}
		if (errorMessage != nullptr) *errorMessage = "Path exists and is not a path container: " + directoryPath;
		return false;
	}
	parentPath = directoryPartOf(directoryPath);
	if (!parentPath.empty() && parentPath != directoryPath)
		if (!ensureDirectoryTree(parentPath, errorMessage)) return false;
	if (::mkdir(directoryPath.c_str(), 0755) != 0 && errno != EEXIST) {
		if (errorMessage != nullptr) *errorMessage = "Unable to create path container: " + directoryPath;
		return false;
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

static const unsigned char kPaletteMenuDescription = 2;
static const unsigned char kPaletteMenuGhostedDescription = 3;
static const unsigned char kPaletteMenuHotkey = 4;
static const unsigned char kPaletteMenuSelector = 5;
static const unsigned char kPaletteMenuGhostedSelector = 6;
static const unsigned char kPaletteMenuSelectedHotkey = 7;

static const unsigned char kPaletteBlueWindowFrame = 8;
static const unsigned char kPaletteBlueWindowBold = 9;
static const unsigned char kPaletteBlueWindowText = 13;
static const unsigned char kPaletteBlueWindowHighlight = 14;

static const unsigned char kPaletteGrayDialogBackground = 32;
static const unsigned char kPaletteGrayDialogFrame = 33;
static const unsigned char kPaletteGrayDialogFrameAccent = 34;
static const unsigned char kPaletteGrayDialogText = 37;
static const unsigned char kPaletteDialogButtonDescription = 41;
static const unsigned char kPaletteDialogButtonDefault = 42;
static const unsigned char kPaletteDialogButtonSelected = 43;
static const unsigned char kPaletteDialogButtonDisabled = 44;
static const unsigned char kPaletteDialogButtonHotkey = 45;
static const unsigned char kPaletteDialogButtonShadow = 46;
static const unsigned char kPaletteDialogClusterHotkey = 49;
static const unsigned char kPaletteDialogInputLineNormal = 50;
static const unsigned char kPaletteDialogInputLineSelected = 51;
static const unsigned char kPaletteDialogInputLineArrows = 52;
static const unsigned char kPaletteDialogHistoryArrow = 53;
static const unsigned char kPaletteDialogHistorySides = 54;
static const unsigned char kPaletteDialogListFrameLegacyPrimary = 24;
static const unsigned char kPaletteDialogListFrameLegacySecondary = 25;
static const unsigned char kPaletteDialogListNormalLegacy = 26;
static const unsigned char kPaletteDialogListFocusedLegacy = 27;
static const unsigned char kPaletteDialogListSelectedLegacy = 28;
static const unsigned char kPaletteDialogListTextLegacy = 29;
static const unsigned char kPaletteDialogListFrameExtendedPrimary = 55;
static const unsigned char kPaletteDialogListFrameExtendedSecondary = 56;
static const unsigned char kPaletteDialogListNormal = 57;
static const unsigned char kPaletteDialogListFocused = 58;
static const unsigned char kPaletteDialogListSelectedInactive = 59;
static const unsigned char kPaletteDialogListText = 60;
static const unsigned char kPaletteBlueDialogBackground = 64;
static const unsigned char kPaletteBlueDialogFrame = 65;
static const unsigned char kPaletteBlueDialogFrameAccent = 66;
static const unsigned char kPaletteBlueDialogText = 69;
static const unsigned char kPaletteBlueDialogInputLineNormal = 82;
static const unsigned char kPaletteBlueDialogInputLineSelected = 83;
static const unsigned char kPaletteBlueDialogInputLineArrows = 84;
static const unsigned char kPaletteBlueDialogHistoryArrow = 85;
static const unsigned char kPaletteBlueDialogHistorySides = 86;
static const unsigned char kPaletteCyanDialogBackground = 96;
static const unsigned char kPaletteCyanDialogFrame = 97;
static const unsigned char kPaletteCyanDialogFrameAccent = 98;
static const unsigned char kPaletteCyanDialogText = 101;
static const unsigned char kPaletteCyanDialogInputLineNormal = 114;
static const unsigned char kPaletteCyanDialogInputLineSelected = 115;
static const unsigned char kPaletteCyanDialogInputLineArrows = 116;
static const unsigned char kPaletteCyanDialogHistoryArrow = 117;
static const unsigned char kPaletteCyanDialogHistorySides = 118;
static const unsigned char kPaletteDialogInactiveClusterGray = 62;
static const unsigned char kPaletteDialogInactiveClusterBlue = 94;
static const unsigned char kPaletteDialogInactiveClusterCyan = 126;
static const unsigned char kPaletteHelpFrame = 128;
static const unsigned char kPaletteHelpText = 133;
static const unsigned char kPaletteHelpHighlight = 134;
static const unsigned char kPaletteHelpChapter = 135;

enum : std::size_t {
	kMenuDialogIndexButtonDescription = 5,
	kMenuDialogIndexListboxSelector = 11,
	kMenuDialogIndexInactiveCluster = 12,
	kMenuDialogIndexInactiveElements = 13,
	kMenuDialogIndexDialogFrame = 14,
	kMenuDialogIndexDialogText = 15,
	kMenuDialogIndexDialogBackground = 16,
	kMenuDialogIndexDropListDescription = 17,
	kMenuDialogIndexDropListSelectedInactive = 18,
	kMenuDialogIndexButtonDefault = 19,
	kMenuDialogIndexButtonSelected = 20,
	kMenuDialogIndexButtonDisabled = 21,
	kMenuDialogIndexInputLineNormal = 22,
	kMenuDialogIndexInputLineSelected = 23,
	kMenuDialogIndexInputLineArrows = 24,
	kMenuDialogIndexHistoryArrow = 25,
	kMenuDialogIndexHistorySides = 26,
	kMenuDialogIndexMenuBarHotkey = 27,
	kMenuDialogIndexSpinnerHandles = 28,
	kMenuDialogIndexSpinnerDisplay = 29,
	kMenuDialogIndexFocusedSpinnerHandles = 30,
	kMenuDialogIndexFocusedSpinnerDisplay = 31
};

struct ColorGroupDefinition {
	MRColorSetupGroup group;
	const char *title;
	const char *key;
	const MRColorSetupItem *items;
	std::size_t count;
};

static const MRColorSetupItem kWindowColorItems[] = {
    {"text", kPaletteBlueWindowText}, {"changed text", kMrPaletteChangedText}, {"highlighted text", kPaletteBlueWindowHighlight}, {"EOF marker", kMrPaletteEofMarker}, {"window border", kPaletteBlueWindowFrame}, {"window bold", kPaletteBlueWindowBold}, {"current line", kMrPaletteCurrentLine}, {"current line in block", kMrPaletteCurrentLineInBlock}, {"line numbers", kMrPaletteLineNumbers}, {"code folding", kMrPaletteCodeFolding}, {"code folding marker", kMrPaletteCodeFoldingMarker}, {"format ruler", kMrPaletteFormatRuler}, {"focused pane border", kMrPaletteFocusedPaneBorder}, {"diagnostic information", kMrPaletteDiagnosticInformation},
};

static const MRColorSetupItem kMenuDialogColorItems[] = {
    {"description of selectable menu element", kPaletteMenuDescription}, {"description of ghosted menu element", kPaletteMenuGhostedDescription}, {"hotkey of menu element", kPaletteMenuHotkey}, {"menu selector on selectable menu element", kPaletteMenuSelector}, {"menu selector on ghosted menu element", kPaletteMenuGhostedSelector}, {"description of buttons", kPaletteDialogButtonDescription}, {"hotkey on buttons", kPaletteDialogButtonHotkey}, {"button shadow", kPaletteDialogButtonShadow}, {"selected element in unfocussed listbox", kPaletteDialogListSelectedInactive}, {"element description in listbox", kPaletteDialogListNormal}, {"hotkeys on radio buttons & check boxes", kPaletteDialogClusterHotkey}, {"dialog selector", kPaletteDialogListFocused}, {"inactive radio buttons and checkboxes", kPaletteDialogInactiveClusterGray}, {"inactive dialog elements", kMrPaletteDialogInactiveElements}, {"dialog frame", kPaletteGrayDialogFrame}, {"dialog text", kPaletteGrayDialogText}, {"dialog background", kPaletteGrayDialogBackground}, {"element description in droplists", kMrPaletteDropListDescription}, {"selected element in unfocussed droplist", kMrPaletteDropListSelectedInactive}, {"default button", kPaletteDialogButtonDefault}, {"selected button", kPaletteDialogButtonSelected}, {"disabled button", kPaletteDialogButtonDisabled}, {"input line text", kPaletteDialogInputLineNormal}, {"selected text in input line", kPaletteDialogInputLineSelected}, {"input line arrows", kPaletteDialogInputLineArrows}, {"history arrow", kPaletteDialogHistoryArrow}, {"history sides", kPaletteDialogHistorySides}, {"Hotkeys on menu bar", kMrPaletteMenuBarHotkey}, {"spinner handles", kMrPaletteSpinnerHandles}, {"spinner display", kMrPaletteSpinnerDisplay}, {"focused spinner handles", kMrPaletteFocusedSpinnerHandles}, {"focused spinner display", kMrPaletteFocusedSpinnerDisplay},
};

static const MRColorSetupItem kHelpColorItems[] = {
    {"Help-Text", kPaletteHelpText}, {"help-Highlight", kPaletteHelpHighlight}, {"help-Chapter", kPaletteHelpChapter}, {"help-Border", kPaletteHelpFrame}, {"help-Link", kPaletteHelpHighlight}, {"help-F-keys", kPaletteHelpChapter}, {"help-attr-1", kPaletteHelpText}, {"help-attr-2", kPaletteHelpHighlight}, {"help-attr-3", kPaletteHelpChapter},
};

static const MRColorSetupItem kOtherColorItems[] = {
    {"statusline", kMrPaletteStatusLine}, {"statusline bold", kMrPaletteStatusLineBold}, {"function descriptions on statusline", kMrPaletteStatusLineFunctionDescription}, {"function keys on statusline", kMrPaletteStatusLineFunctionKey}, {"error message", kMrPaletteMessageError}, {"message", kMrPaletteMessage}, {"warning message", kMrPaletteMessageWarning}, {"hero events", kMrPaletteMessageHero}, {"cursor position marker", kMrPaletteCursorPositionMarker}, {"desktop background", kMrPaletteDesktop}, {"virtual desktop marker", kMrPaletteVirtualDesktopMarker},
};

static const MRColorSetupItem kMiniMapColorItems[] = {
    {"normal", kMrPaletteMiniMapNormal}, {"viewport cursor", kMrPaletteMiniMapViewport}, {"changed", kMrPaletteMiniMapChanged}, {"find marker", kMrPaletteMiniMapFindMarker}, {"error marker", kMrPaletteMiniMapErrorMarker}, {"diagnostics", kMrPaletteMiniMapDiagnostics},
};

static const MRColorSetupItem kFileCompareMiniMapColorItems[] = {
    {"normal", kMrPaletteFileCompareMiniMapNormal}, {"viewport cursor", kMrPaletteFileCompareMiniMapViewport}, {"changed", kMrPaletteFileCompareMiniMapChanged}, {"find marker", kMrPaletteFileCompareMiniMapFindMarker}, {"error marker", kMrPaletteFileCompareMiniMapErrorMarker},
    {"diff equal", kMrPaletteFileCompareMiniMapEqual}, {"diff missing", kMrPaletteFileCompareMiniMapMissing}, {"diff insert", kMrPaletteFileCompareMiniMapInsert}, {"diff offset", kMrPaletteFileCompareMiniMapOffset},
};

static const MRColorSetupItem kCodeColorItems[] = {
    {"comments", kMrPaletteCodeComments}, {"strings", kMrPaletteCodeStrings}, {"characters", kMrPaletteCodeCharacters}, {"numbers", kMrPaletteCodeNumbers}, {"keywords", kMrPaletteCodeKeywords}, {"types", kMrPaletteCodeTypes}, {"directives", kMrPaletteCodeDirectives}, {"functions", kMrPaletteCodeFunctions}, {"builtins", kMrPaletteCodeBuiltins}, {"constants", kMrPaletteCodeConstants}, {"operators", kMrPaletteCodeOperators}, {"brackets", kMrPaletteCodeBrackets}, {"delimiters", kMrPaletteCodeDelimiters}, {"sidekick editor text", kMrPaletteSidekickEditorText}, {"sidekick editor highlight", kMrPaletteSidekickEditorHighlight}, {"context menu", kMrPaletteContextMenu}, {"context menu selector", kMrPaletteContextMenuSelector}, {"snippet sidekick frame", kMrPaletteSnippetSidekickFrame}, {"snippet sidekick text", kMrPaletteSnippetSidekickText}, {"snippet placeholder", kMrPaletteSnippetPlaceholder}, {"snippet active placeholder", kMrPaletteSnippetActivePlaceholder}, {"snippet default text", kMrPaletteSnippetDefaultText}, {"outline file header", kMrPaletteOutlineFileHeader}, {"outline level 1", kMrPaletteOutlineLevel0}, {"outline level 2", kMrPaletteOutlineLevel1}, {"outline level 3", kMrPaletteOutlineLevel2}, {"outline level 4", kMrPaletteOutlineLevel3}, {"outline level 5", kMrPaletteOutlineLevel4}, {"outline level 6", kMrPaletteOutlineLevel5}, {"outline level 7", kMrPaletteOutlineLevel6}, {"outline level 8", kMrPaletteOutlineLevel7}, {"outline level 9", kMrPaletteOutlineLevel8}, {"outline level 10", kMrPaletteOutlineLevel9},
};

static const MRColorSetupItem kFileCompareColorItems[] = {
    {"text equal", kMrPaletteFileCompareTextEqual}, {"text missing", kMrPaletteFileCompareTextMissing}, {"text insert", kMrPaletteFileCompareTextInsert}, {"text offset", kMrPaletteFileCompareTextOffset},
    {"gutter equal", kMrPaletteFileCompareGutterEqual}, {"gutter missing", kMrPaletteFileCompareGutterMissing}, {"gutter insert", kMrPaletteFileCompareGutterInsert}, {"gutter offset", kMrPaletteFileCompareGutterOffset},
    {"bento border", kMrPaletteFileCompareBentoBorder}, {"pane border", kMrPaletteFileComparePaneBorder}, {"bento border bold", kMrPaletteFileCompareBentoBorderBold}, {"format ruler", kMrPaletteFileCompareFormatRuler},
    {"line numbers", kMrPaletteFileCompareLineNumbers}, {"focused pane border", kMrPaletteFileCompareFocusedPaneBorder},
};

static const ColorGroupDefinition kColorGroups[] = {
    {MRColorSetupGroup::Window, "WINDOW COLORS", "WINDOWCOLORS", kWindowColorItems, std::size(kWindowColorItems)},
    {MRColorSetupGroup::MenuDialog, "MENU / DIALOG COLORS", "MENUDIALOGCOLORS", kMenuDialogColorItems, std::size(kMenuDialogColorItems)},
    {MRColorSetupGroup::Help, "HELP COLORS", "HELPCOLORS", kHelpColorItems, std::size(kHelpColorItems)},
    {MRColorSetupGroup::Other, "OTHER COLORS", "OTHERCOLORS", kOtherColorItems, std::size(kOtherColorItems)},
    {MRColorSetupGroup::MiniMap, "MINIMAP COLORS", "MINIMAPCOLORS", kMiniMapColorItems, std::size(kMiniMapColorItems)},
    {MRColorSetupGroup::FileCompareMiniMap, "FILE COMPARE MINIMAP COLORS", "FILECOMPAREMINIMAPCOLORS", kFileCompareMiniMapColorItems, std::size(kFileCompareMiniMapColorItems)},
    {MRColorSetupGroup::Code, "CODE COLORS", "CODECOLORS", kCodeColorItems, std::size(kCodeColorItems)},
    {MRColorSetupGroup::FileCompare, "FILE COMPARE COLORS", "FILECOMPARECOLORS", kFileCompareColorItems, std::size(kFileCompareColorItems)},
};

const ColorGroupDefinition *findColorGroupDefinition(MRColorSetupGroup group) {
	for (const auto &kColorGroup : kColorGroups)
		if (kColorGroup.group == group) return &kColorGroup;
	return nullptr;
}

const ColorGroupDefinition *findColorGroupDefinitionByKey(const std::string &key) {
	std::string upper = upperAscii(trimAscii(key));
	for (const auto &kColorGroup : kColorGroups)
		if (upper == kColorGroup.key) return &kColorGroup;
	return nullptr;
}

bool codeColorCountAccepted(std::size_t count, std::size_t currentCount) noexcept {
	if (count == currentCount) return true;
	if (count == currentCount + 1) return true;
	if (currentCount > 0 && count == currentCount - 1) return true;
	if (currentCount > 5 && count == currentCount - 6) return true;
	if (currentCount > 7 && count == currentCount - 8) return true;
	if (currentCount > 9 && count == currentCount - 10) return true;
	if (currentCount > 10 && count == currentCount - 11) return true;
	return false;
}

unsigned char defaultColorForSlot(unsigned char paletteIndex) {
	static constexpr std::array<unsigned char, 146> defaults = {
	    0x00, 0x71, 0x70, 0x78, 0x74, 0x20, 0x28, 0x24, 0x17, 0x1F, 0x1A, 0x31, 0x31, 0x1E, 0x71, 0x1F, 0x37, 0x3F, 0x3A, 0x13, 0x13, 0x3E, 0x21, 0x3F, 0x70, 0x7F, 0x7A, 0x13, 0x13, 0x70, 0x7F, 0x7E, 0x70, 0x7F, 0x7A, 0x13, 0x13, 0x70, 0x70, 0x7F, 0x7E, 0x20, 0x2B, 0x2F, 0x78, 0x2E, 0x70, 0x30, 0x3F, 0x3E, 0x1F, 0x2F, 0x1A, 0x20, 0x72, 0x31, 0x31, 0x30, 0x2F, 0x3E, 0x31, 0x13, 0x38, 0x00, 0x17, 0x1F, 0x1A, 0x71, 0x71, 0x1E, 0x17, 0x1F, 0x1E, 0x20, 0x2B, 0x2F, 0x78, 0x2E, 0x10, 0x30, 0x3F, 0x3E, 0x70, 0x2F, 0x7A, 0x20, 0x12, 0x31, 0x31, 0x30, 0x2F, 0x3E, 0x31, 0x13, 0x38, 0x00, 0x37, 0x3F, 0x3A, 0x13, 0x13, 0x3E, 0x30, 0x3F, 0x3E, 0x20, 0x2B, 0x2F, 0x78, 0x2E, 0x30, 0x70, 0x7F, 0x7E, 0x1F, 0x2F, 0x1A, 0x20, 0x32, 0x31, 0x71, 0x70, 0x2F, 0x7E, 0x71, 0x13, 0x78, 0x00, 0x37, 0x3F, 0x3A, 0x13, 0x13, 0x30, 0x3E, 0x1E,
	};

	if (paletteIndex == kMrPaletteCurrentLine) return defaults[10];
	if (paletteIndex == kMrPaletteCurrentLineInBlock) return defaults[12];
	if (paletteIndex == kMrPaletteChangedText) return defaults[14];
	if (paletteIndex == kMrPaletteMessageError) return defaults[42];
	if (paletteIndex == kMrPaletteMessage) return defaults[43];
	if (paletteIndex == kMrPaletteMessageWarning) return defaults[44];
	if (paletteIndex == kMrPaletteMessageHero) return defaults[43];
	if (paletteIndex == kMrPaletteCursorPositionMarker) return defaults[3];
	if (paletteIndex == kMrPaletteLineNumbers) return defaults[9];
	if (paletteIndex == kMrPaletteCodeFolding) return defaults[9];
	if (paletteIndex == kMrPaletteCodeFoldingMarker) return defaults[9];
	if (paletteIndex == kMrPaletteFormatRuler) return defaults[13];
	if (paletteIndex == kMrPaletteEofMarker) return defaults[14];
	if (paletteIndex == kMrPaletteMiniMapNormal) return defaults[13];
	if (paletteIndex == kMrPaletteMiniMapViewport) return defaults[11];
	if (paletteIndex == kMrPaletteMiniMapChanged) return defaults[14];
	if (paletteIndex == kMrPaletteMiniMapFindMarker) return defaults[5];
	if (paletteIndex == kMrPaletteMiniMapErrorMarker) return defaults[42];
	if (paletteIndex == kMrPaletteCodeComments) return defaults[12];
	if (paletteIndex == kMrPaletteCodeStrings) return defaults[14];
	if (paletteIndex == kMrPaletteCodeCharacters) return defaults[14];
	if (paletteIndex == kMrPaletteCodeNumbers) return defaults[13];
	if (paletteIndex == kMrPaletteCodeKeywords) return defaults[11];
	if (paletteIndex == kMrPaletteCodeTypes) return defaults[9];
	if (paletteIndex == kMrPaletteCodeDirectives) return defaults[42];
	if (paletteIndex == kMrPaletteCodeFunctions) return defaults[10];
	if (paletteIndex == kMrPaletteCodeBuiltins) return defaults[43];
	if (paletteIndex == kMrPaletteCodeConstants) return defaults[3];
	if (paletteIndex == kMrPaletteCodeOperators) return defaults[37];
	if (paletteIndex == kMrPaletteCodeBrackets) return defaults[9];
	if (paletteIndex == kMrPaletteCodeDelimiters) return defaults[13];
	if (paletteIndex == kMrPaletteSidekickEditorText) return 0x30;
	if (paletteIndex == kMrPaletteSidekickEditorHighlight) return 0xE0;
	if (paletteIndex == kMrPaletteContextMenu) return defaultColorForSlot(kMrPaletteDropListDescription);
	if (paletteIndex == kMrPaletteContextMenuSelector) return defaultColorForSlot(kMrPaletteDropListSelectedInactive);
	if (paletteIndex == kMrPaletteSnippetSidekickFrame) return 0x3F;
	if (paletteIndex == kMrPaletteSnippetSidekickText) return 0x30;
	if (paletteIndex == kMrPaletteSnippetPlaceholder) return 0x38;
	if (paletteIndex == kMrPaletteSnippetActivePlaceholder) return 0xE0;
	if (paletteIndex == kMrPaletteSnippetDefaultText) return 0x38;
	if (paletteIndex == kMrPaletteOutlineFileHeader) return 0x1F;
	if (paletteIndex == kMrPaletteOutlineLevel0) return 0x1F;
	if (paletteIndex == kMrPaletteOutlineLevel1) return 0x1E;
	if (paletteIndex == kMrPaletteOutlineLevel2) return 0x1B;
	if (paletteIndex == kMrPaletteOutlineLevel3) return 0x1A;
	if (paletteIndex == kMrPaletteOutlineLevel4) return 0x1D;
	if (paletteIndex == kMrPaletteOutlineLevel5) return 0x19;
	if (paletteIndex == kMrPaletteOutlineLevel6) return 0x1C;
	if (paletteIndex == kMrPaletteOutlineLevel7) return 0x13;
	if (paletteIndex == kMrPaletteOutlineLevel8) return 0x1F;
	if (paletteIndex == kMrPaletteOutlineLevel9) return 0x1E;
	if (paletteIndex == kMrPaletteFileCompareTextEqual) return 0x1A;
	if (paletteIndex == kMrPaletteFileCompareTextMissing) return 0x1C;
	if (paletteIndex == kMrPaletteFileCompareTextInsert) return 0x1E;
	if (paletteIndex == kMrPaletteFileCompareTextOffset) return 0x1F;
	if (paletteIndex == kMrPaletteFileCompareGutterEqual) return 0x1A;
	if (paletteIndex == kMrPaletteFileCompareGutterMissing) return 0x1C;
	if (paletteIndex == kMrPaletteFileCompareGutterInsert) return 0x1E;
	if (paletteIndex == kMrPaletteFileCompareGutterOffset) return 0x1F;
	if (paletteIndex == kMrPaletteFileCompareMiniMapEqual) return 0x1A;
	if (paletteIndex == kMrPaletteFileCompareMiniMapMissing) return 0x1C;
	if (paletteIndex == kMrPaletteFileCompareMiniMapInsert) return 0x1E;
	if (paletteIndex == kMrPaletteFileCompareMiniMapOffset) return 0x1F;
	if (paletteIndex == kMrPaletteFileCompareBentoBorder) return defaultColorForSlot(kPaletteBlueWindowFrame);
	if (paletteIndex == kMrPaletteFileComparePaneBorder) return defaultColorForSlot(kMrPaletteFocusedPaneBorder);
	if (paletteIndex == kMrPaletteFileCompareBentoBorderBold) return defaultColorForSlot(kPaletteBlueWindowBold);
	if (paletteIndex == kMrPaletteFileCompareFormatRuler) return defaultColorForSlot(kMrPaletteFormatRuler);
	if (paletteIndex == kMrPaletteFileCompareLineNumbers) return defaultColorForSlot(kMrPaletteLineNumbers);
	if (paletteIndex == kMrPaletteFileCompareFocusedPaneBorder) return defaultColorForSlot(kMrPaletteFocusedPaneBorder);
	if (paletteIndex == kMrPaletteFileCompareMiniMapNormal) return defaultColorForSlot(kMrPaletteMiniMapNormal);
	if (paletteIndex == kMrPaletteFileCompareMiniMapViewport) return defaultColorForSlot(kMrPaletteMiniMapViewport);
	if (paletteIndex == kMrPaletteFileCompareMiniMapChanged) return defaultColorForSlot(kMrPaletteMiniMapChanged);
	if (paletteIndex == kMrPaletteFileCompareMiniMapFindMarker) return defaultColorForSlot(kMrPaletteMiniMapFindMarker);
	if (paletteIndex == kMrPaletteFileCompareMiniMapErrorMarker) return defaultColorForSlot(kMrPaletteMiniMapErrorMarker);
	if (paletteIndex == kMrPaletteFocusedPaneBorder) return defaults[kPaletteBlueWindowBold];
	if (paletteIndex == kMrPaletteStatusLine) return defaults[kPaletteMenuDescription];
	if (paletteIndex == kMrPaletteStatusLineBold) return defaults[kPaletteMenuGhostedDescription];
	if (paletteIndex == kMrPaletteStatusLineFunctionDescription) return defaults[kPaletteMenuHotkey];
	if (paletteIndex == kMrPaletteStatusLineFunctionKey) return defaults[kPaletteMenuSelector];
	if (paletteIndex == kMrPaletteMenuBarHotkey) return defaults[kPaletteMenuHotkey];
	if (paletteIndex == kPaletteDialogButtonDefault) return defaults[kPaletteDialogButtonDescription];
	if (paletteIndex == kPaletteDialogButtonSelected) return defaults[kPaletteDialogButtonDescription];
	if (paletteIndex == kPaletteDialogButtonDisabled) return defaults[kPaletteDialogInactiveClusterGray];
	if (paletteIndex == kPaletteDialogInputLineNormal) return defaults[kPaletteDialogInputLineNormal];
	if (paletteIndex == kPaletteDialogInputLineSelected) return defaults[kPaletteDialogInputLineSelected];
	if (paletteIndex == kPaletteDialogInputLineArrows) return defaults[kPaletteDialogInputLineArrows];
	if (paletteIndex == kPaletteDialogHistoryArrow) return defaults[kPaletteDialogHistoryArrow];
	if (paletteIndex == kPaletteDialogHistorySides) return defaults[kPaletteDialogHistorySides];
	if (paletteIndex == kMrPaletteSpinnerHandles) return defaultColorForSlot(kPaletteDialogInputLineArrows);
	if (paletteIndex == kMrPaletteSpinnerDisplay) return defaultColorForSlot(kPaletteDialogInputLineNormal);
	if (paletteIndex == kMrPaletteFocusedSpinnerHandles) return defaultColorForSlot(kPaletteDialogListFocused);
	if (paletteIndex == kMrPaletteFocusedSpinnerDisplay) return defaultColorForSlot(kPaletteDialogInputLineSelected);
	if (paletteIndex == kMrPaletteDropListDescription) return defaults[57];
	if (paletteIndex == kMrPaletteDropListSelectedInactive) return defaults[59];
	if (paletteIndex == kMrPaletteDialogInactiveElements) return defaults[kPaletteDialogInactiveClusterGray];
	if (paletteIndex == kMrPaletteDesktop) return 0x90;
	if (paletteIndex == kMrPaletteVirtualDesktopMarker) return 0x9F;
	if (paletteIndex == 0 || paletteIndex >= std::size(defaults)) return 0x70;
	return defaults[paletteIndex];
}

MRColorSetupSettings defaultsFromColorGroups() {
	MRColorSetupSettings settings;

	for (std::size_t i = 0; i < settings.windowColors.size(); ++i)
		settings.windowColors[i] = defaultColorForSlot(kWindowColorItems[i].paletteIndex);
	for (std::size_t i = 0; i < settings.menuDialogColors.size(); ++i)
		settings.menuDialogColors[i] = defaultColorForSlot(kMenuDialogColorItems[i].paletteIndex);
	for (std::size_t i = 0; i < settings.helpColors.size(); ++i)
		settings.helpColors[i] = defaultColorForSlot(kHelpColorItems[i].paletteIndex);
	for (std::size_t i = 0; i < settings.otherColors.size(); ++i)
		settings.otherColors[i] = defaultColorForSlot(kOtherColorItems[i].paletteIndex);
	for (std::size_t i = 0; i < settings.miniMapColors.size(); ++i)
		settings.miniMapColors[i] = defaultColorForSlot(kMiniMapColorItems[i].paletteIndex);
	for (std::size_t i = 0; i < settings.fileCompareMiniMapColors.size(); ++i)
		settings.fileCompareMiniMapColors[i] = defaultColorForSlot(kFileCompareMiniMapColorItems[i].paletteIndex);
	for (std::size_t i = 0; i < settings.codeColors.size(); ++i)
		settings.codeColors[i] = defaultColorForSlot(kCodeColorItems[i].paletteIndex);
	for (std::size_t i = 0; i < settings.fileCompareColors.size(); ++i)
		settings.fileCompareColors[i] = defaultColorForSlot(kFileCompareColorItems[i].paletteIndex);
	return settings;
}

bool parseHexColorToken(const std::string &token, unsigned char &outValue) {
	std::string value = trimAscii(token);
	unsigned int parsed = 0;

	if (value.empty() || value.size() > 2) return false;
	for (char ch : value)
		if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
	parsed = static_cast<unsigned int>(std::strtoul(value.c_str(), nullptr, 16));
	if (parsed > 0xFFu) return false;
	outValue = static_cast<unsigned char>(parsed);
	return true;
}

template <std::size_t N> std::string formatColorListLiteral(const std::array<unsigned char, N> &values) {
	std::string out = "v1:";

	for (std::size_t i = 0; i < values.size(); ++i) {
		if (i != 0) out.push_back(',');
		out += toUpperHexByte(values[i]);
	}
	return out;
}

std::string formatWindowColorListLiteral(const std::array<unsigned char, MRColorSetupSettings::kWindowCount> &values) {
	std::string out = formatColorListLiteral(values);
	out[1] = '7';
	return out;
}

std::string formatFileCompareColorListLiteral(const std::array<unsigned char, MRColorSetupSettings::kFileCompareCount> &values) {
	std::string out = formatColorListLiteral(values);
	out[1] = '2';
	return out;
}

template <std::size_t N> bool parseColorListLiteral(const std::string &literal, std::array<unsigned char, N> &outValues, std::string *errorMessage) {
	std::string text = trimAscii(literal);

	if (text.empty()) return setError(errorMessage, "Empty color list.");
	if (text.size() < 3 || text[0] != 'v' || text[2] != ':') return setError(errorMessage, "Expected color list version prefix (e.g. v1:...).");
	std::fill(outValues.begin(), outValues.end(), 0);
	std::size_t cursor = 3;
	std::size_t itemIndex = 0;
	while (cursor <= text.size()) {
		std::size_t comma = text.find(',', cursor);
		std::string token = text.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor);
		unsigned char value = 0;

		if (!parseHexColorToken(token, value)) return setError(errorMessage, "Expected hex color list (e.g. v1:70,7F,...).");
		if (itemIndex >= outValues.size()) return setError(errorMessage, "Too many color values in list.");
		outValues[itemIndex++] = value;
		if (comma == std::string::npos) break;
		cursor = comma + 1;
	}
	if (itemIndex != N) return setError(errorMessage, "Unexpected color list size.");
	if (text.find(',', cursor) != std::string::npos) return setError(errorMessage, "Too many color values in list.");
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool parseCodeColorListLiteral(const std::string &literal, std::array<unsigned char, MRColorSetupSettings::kCodeCount> &outValues, std::string *errorMessage) {
	std::string text = trimAscii(literal);
	std::size_t cursor = 0;
	std::vector<unsigned char> parsed;
	unsigned char value = 0;

	if (text.rfind("v1:", 0) == 0 || text.rfind("V1:", 0) == 0) text = text.substr(3);
	if (text.empty()) return setError(errorMessage, "Empty color list.");
	while (cursor <= text.size()) {
		std::size_t comma = text.find(',', cursor);
		std::string token = text.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor);

		if (!parseHexColorToken(token, value)) return setError(errorMessage, "Expected hex color list (e.g. v1:70,7F,...).");
		parsed.push_back(value);
		if (comma == std::string::npos) break;
		cursor = comma + 1;
	}
	if (!codeColorCountAccepted(parsed.size(), outValues.size())) return setError(errorMessage, "Unexpected CODECOLORS list size.");
	for (std::size_t i = 0; i < parsed.size() && i < outValues.size(); ++i)
		outValues[i] = parsed[i];
	for (std::size_t i = std::min<std::size_t>(parsed.size(), outValues.size()); i < outValues.size(); ++i)
		outValues[i] = defaultColorForSlot(kCodeColorItems[i].paletteIndex);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool parseFileCompareMiniMapColorListLiteral(const std::string &literal, std::array<unsigned char, MRColorSetupSettings::kFileCompareMiniMapCount> &outValues, std::string *errorMessage) {
	std::string text = trimAscii(literal);
	std::size_t cursor = 0;
	std::vector<unsigned char> parsed;
	unsigned char value = 0;

	if (text.rfind("v1:", 0) == 0 || text.rfind("V1:", 0) == 0) text = text.substr(3);
	if (text.empty()) return setError(errorMessage, "Empty color list.");
	while (cursor <= text.size()) {
		std::size_t comma = text.find(',', cursor);
		std::string token = text.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor);

		if (!parseHexColorToken(token, value)) return setError(errorMessage, "Expected hex color list (e.g. v1:70,7F,...).");
		parsed.push_back(value);
		if (comma == std::string::npos) break;
		cursor = comma + 1;
	}
	if (parsed.size() != outValues.size() && parsed.size() != 5) return setError(errorMessage, "Unexpected FILECOMPAREMINIMAPCOLORS list size.");
	for (std::size_t i = 0; i < outValues.size(); ++i)
		outValues[i] = defaultColorForSlot(kFileCompareMiniMapColorItems[i].paletteIndex);
	for (std::size_t i = 0; i < parsed.size(); ++i)
		outValues[i] = parsed[i];
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool parseMiniMapColorListLiteral(const std::string &literal, std::array<unsigned char, MRColorSetupSettings::kMiniMapCount> &outValues, std::string *errorMessage) {
	std::string text = trimAscii(literal);
	std::size_t cursor = 0;
	std::vector<unsigned char> parsed;
	unsigned char value = 0;

	if (text.rfind("v1:", 0) == 0 || text.rfind("V1:", 0) == 0) text = text.substr(3);
	if (text.empty()) return setError(errorMessage, "Empty color list.");
	while (cursor <= text.size()) {
		std::size_t comma = text.find(',', cursor);
		std::string token = text.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor);

		if (!parseHexColorToken(token, value)) return setError(errorMessage, "Expected hex color list (e.g. v1:70,7F,...).");
		parsed.push_back(value);
		if (comma == std::string::npos) break;
		cursor = comma + 1;
	}
	if (parsed.size() != outValues.size() && parsed.size() != outValues.size() - 1) return setError(errorMessage, "Unexpected MINIMAPCOLORS list size.");
	for (std::size_t i = 0; i < outValues.size(); ++i)
		outValues[i] = defaultColorForSlot(kMiniMapColorItems[i].paletteIndex);
	for (std::size_t i = 0; i < parsed.size(); ++i)
		outValues[i] = parsed[i];
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool parseFileCompareColorListLiteral(const std::string &literal, std::array<unsigned char, MRColorSetupSettings::kFileCompareCount> &outValues, std::string *errorMessage) {
	std::string text = trimAscii(literal);
	std::size_t cursor = 0;
	std::vector<unsigned char> parsed;
	unsigned char value = 0;
	bool v2Format = false;
	static constexpr std::array<std::size_t, 5> acceptedSizes = {12, MRColorSetupSettings::kFileCompareCount, 15, 16, 18};

	if (text.rfind("v2:", 0) == 0 || text.rfind("V2:", 0) == 0) {
		text = text.substr(3);
		v2Format = true;
	} else if (text.rfind("v1:", 0) == 0 || text.rfind("V1:", 0) == 0)
		text = text.substr(3);
	if (text.empty()) return setError(errorMessage, "Empty color list.");
	while (cursor <= text.size()) {
		std::size_t comma = text.find(',', cursor);
		std::string token = text.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor);

		if (!parseHexColorToken(token, value)) return setError(errorMessage, "Expected hex color list (e.g. v1:70,7F,...).");
		parsed.push_back(value);
		if (comma == std::string::npos) break;
		cursor = comma + 1;
	}
	if (v2Format) {
		if (parsed.size() != outValues.size()) return setError(errorMessage, "Unexpected FILECOMPARECOLORS v2 list size.");
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	bool accepted = false;
	for (std::size_t acceptedSize : acceptedSizes)
		if (parsed.size() == acceptedSize) accepted = true;
	if (!accepted) return setError(errorMessage, "Unexpected FILECOMPARECOLORS list size.");
	for (std::size_t i = 0; i < outValues.size(); ++i)
		outValues[i] = defaultColorForSlot(kFileCompareColorItems[i].paletteIndex);
	for (std::size_t i = 0; i < std::min<std::size_t>(8, parsed.size()); ++i)
		outValues[i] = parsed[i];
	if (parsed.size() >= 14) {
		outValues[8] = parsed[12];
		outValues[9] = parsed[13];
	}
	if (parsed.size() >= 15) outValues[10] = parsed[14];
	if (parsed.size() >= 16) outValues[11] = parsed[15];
	if (parsed.size() >= 18) {
		outValues[12] = parsed[16];
		outValues[13] = parsed[17];
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool parseWindowColorListLiteral(const std::string &literal, std::array<unsigned char, MRColorSetupSettings::kWindowCount> &outValues, std::string *errorMessage) {
	std::string text = trimAscii(literal);
	std::size_t cursor = 0;
	std::vector<unsigned char> parsed;
	const unsigned char defaultEofMarker = defaultColorForSlot(kMrPaletteEofMarker);
	const unsigned char defaultLineNumbers = defaultColorForSlot(kMrPaletteLineNumbers);
	const unsigned char defaultCodeFolding = defaultColorForSlot(kMrPaletteCodeFolding);
	const unsigned char defaultCodeFoldingMarker = defaultColorForSlot(kMrPaletteCodeFoldingMarker);
	const unsigned char defaultFormatRuler = defaultColorForSlot(kMrPaletteFormatRuler);
	const unsigned char defaultFocusedPaneBorder = defaultColorForSlot(kMrPaletteFocusedPaneBorder);
	unsigned char value = 0;
	bool v7Format = false;
	bool v6Format = false;
	bool v5Format = false;
	bool v4Format = false;
	bool v3Format = false;
	bool v2Format = false;
	auto resetWindowColorDefaults = [&]() {
		for (std::size_t i = 0; i < outValues.size(); ++i)
			outValues[i] = defaultColorForSlot(kWindowColorItems[i].paletteIndex);
	};

	if (text.rfind("v7:", 0) == 0 || text.rfind("V7:", 0) == 0) {
		text = text.substr(3);
		v7Format = true;
	} else if (text.rfind("v6:", 0) == 0 || text.rfind("V6:", 0) == 0) {
		text = text.substr(3);
		v6Format = true;
	} else if (text.rfind("v5:", 0) == 0 || text.rfind("V5:", 0) == 0) {
		text = text.substr(3);
		v5Format = true;
	} else if (text.rfind("v4:", 0) == 0 || text.rfind("V4:", 0) == 0) {
		text = text.substr(3);
		v4Format = true;
	} else if (text.rfind("v3:", 0) == 0 || text.rfind("V3:", 0) == 0) {
		text = text.substr(3);
		v3Format = true;
	} else if (text.rfind("v2:", 0) == 0 || text.rfind("V2:", 0) == 0) {
		text = text.substr(3);
		v2Format = true;
	} else if (text.rfind("v1:", 0) == 0 || text.rfind("V1:", 0) == 0)
		text = text.substr(3);
	if (text.empty()) return setError(errorMessage, "Empty color list.");

	while (cursor <= text.size()) {
		std::size_t comma = text.find(',', cursor);
		std::string token = text.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor);

		if (!parseHexColorToken(token, value)) return setError(errorMessage, "Expected hex color list (e.g. v1:70,7F,...).");
		parsed.push_back(value);
		if (comma == std::string::npos) break;
		cursor = comma + 1;
	}

	if (v7Format) {
		if (parsed.size() != MRColorSetupSettings::kWindowCount) return setError(errorMessage, "Unexpected WINDOWCOLORS list size for v7.");
		resetWindowColorDefaults();
		for (std::size_t i = 0; i < outValues.size(); ++i)
			outValues[i] = parsed[i];
	} else if (v6Format) {
		if (parsed.size() != MRColorSetupSettings::kWindowCount - 1) return setError(errorMessage, "Unexpected WINDOWCOLORS list size for v6.");
		resetWindowColorDefaults();
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
	} else if (v5Format) {
		if (parsed.size() != MRColorSetupSettings::kWindowCount - 2) return setError(errorMessage, "Unexpected WINDOWCOLORS list size for v5.");
		resetWindowColorDefaults();
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[12] = defaultFocusedPaneBorder;
	} else if (v4Format) {
		if (parsed.size() != MRColorSetupSettings::kWindowCount - 3) return setError(errorMessage, "Unexpected WINDOWCOLORS list size for v4.");
		resetWindowColorDefaults();
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[11] = defaultFormatRuler;
		outValues[12] = defaultFocusedPaneBorder;
	} else if (v3Format) {
		if (parsed.size() != MRColorSetupSettings::kWindowCount - 4) return setError(errorMessage, "Unexpected WINDOWCOLORS list size for v3.");
		resetWindowColorDefaults();
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[10] = defaultCodeFoldingMarker;
		outValues[11] = defaultFormatRuler;
		outValues[12] = defaultFocusedPaneBorder;
	} else if (v2Format) {
		if (parsed.size() != 8) return setError(errorMessage, "Unexpected WINDOWCOLORS list size for v2.");
		resetWindowColorDefaults();
		outValues[0] = parsed[0];
		outValues[1] = parsed[1];
		outValues[2] = parsed[2];
		outValues[3] = defaultEofMarker;
		outValues[4] = parsed[3];
		outValues[5] = parsed[4];
		outValues[6] = parsed[5];
		outValues[7] = parsed[6];
		outValues[8] = defaultLineNumbers;
		outValues[9] = defaultCodeFolding;
		outValues[10] = defaultCodeFoldingMarker;
		outValues[11] = defaultFormatRuler;
		outValues[12] = defaultFocusedPaneBorder;
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount) {
		resetWindowColorDefaults();
		for (std::size_t i = 0; i < outValues.size(); ++i)
			outValues[i] = parsed[i];
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount - 1) {
		resetWindowColorDefaults();
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount - 2) {
		resetWindowColorDefaults();
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[12] = defaultFocusedPaneBorder;
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount - 3) {
		resetWindowColorDefaults();
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[11] = defaultFormatRuler;
		outValues[12] = defaultFocusedPaneBorder;
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount - 4) {
		resetWindowColorDefaults();
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[10] = defaultCodeFoldingMarker;
		outValues[11] = defaultFormatRuler;
		outValues[12] = defaultFocusedPaneBorder;
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount - 5) {
		resetWindowColorDefaults();
		outValues[0] = parsed[0];
		outValues[1] = parsed[1];
		outValues[2] = parsed[2];
		outValues[3] = parsed[3];
		outValues[4] = parsed[4];
		outValues[5] = parsed[5];
		outValues[6] = parsed[6];
		outValues[7] = parsed[7];
		outValues[8] = defaultLineNumbers;
		outValues[9] = defaultCodeFolding;
		outValues[10] = defaultCodeFoldingMarker;
		outValues[11] = defaultFormatRuler;
		outValues[12] = defaultFocusedPaneBorder;
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount - 6) {
		resetWindowColorDefaults();
		outValues[0] = parsed[0];
		outValues[1] = parsed[1];
		outValues[2] = parsed[2];
		outValues[3] = parsed[3];
		outValues[4] = parsed[4];
		outValues[5] = parsed[5];
		outValues[6] = parsed[6];
		outValues[7] = parsed[7];
		outValues[8] = defaultLineNumbers;
		outValues[9] = defaultCodeFolding;
		outValues[10] = defaultCodeFoldingMarker;
		outValues[11] = defaultFormatRuler;
		outValues[12] = defaultFocusedPaneBorder;
	} else if (parsed.size() == MRColorSetupSettings::kWindowCount - 7) {
		resetWindowColorDefaults();
		outValues[0] = parsed[0];
		outValues[1] = parsed[1];
		outValues[2] = parsed[2];
		outValues[3] = defaultEofMarker;
		outValues[4] = parsed[3];
		outValues[5] = parsed[4];
		outValues[6] = parsed[5];
		outValues[7] = parsed[6];
		outValues[8] = defaultLineNumbers;
		outValues[9] = defaultCodeFolding;
		outValues[10] = defaultCodeFoldingMarker;
		outValues[11] = defaultFormatRuler;
		outValues[12] = defaultFocusedPaneBorder;
	} else {
		return setError(errorMessage, "Unexpected WINDOWCOLORS list size.");
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool parseMenuDialogColorListLiteral(const std::string &literal, std::array<unsigned char, MRColorSetupSettings::kMenuDialogCount> &outValues, std::string *errorMessage) {
	std::string text = trimAscii(literal);
	std::size_t cursor = 0;
	std::vector<unsigned char> parsed;
	unsigned char value = 0;

	if (text.size() >= 3 && (text[0] == 'v' || text[0] == 'V') && std::isdigit(static_cast<unsigned char>(text[1])) && text[2] == ':') text = text.substr(3);
	if (text.empty()) return setError(errorMessage, "Empty color list.");
	while (cursor <= text.size()) {
		std::size_t comma = text.find(',', cursor);
		std::string token = text.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor);
		if (!parseHexColorToken(token, value)) return setError(errorMessage, "Expected hex color list (e.g. v1:70,7F,...).");
		parsed.push_back(value);
		if (comma == std::string::npos) break;
		cursor = comma + 1;
	}

	outValues = defaultsFromColorGroups().menuDialogColors;
	if (parsed.size() >= 17 && parsed.size() <= MRColorSetupSettings::kMenuDialogCount) {
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
	} else if (parsed.size() == 16) {
		for (std::size_t i = 0; i <= kMenuDialogIndexInactiveCluster; ++i)
			outValues[i] = parsed[i];
		outValues[kMenuDialogIndexDialogFrame] = parsed[13];
		outValues[kMenuDialogIndexDialogText] = parsed[14];
		outValues[kMenuDialogIndexDialogBackground] = parsed[15];
	} else if (parsed.size() == 15) {
		for (std::size_t i = 0; i <= kMenuDialogIndexListboxSelector; ++i)
			outValues[i] = parsed[i];
		outValues[kMenuDialogIndexDialogFrame] = parsed[12];
		outValues[kMenuDialogIndexDialogText] = parsed[13];
		outValues[kMenuDialogIndexDialogBackground] = parsed[12];
	} else if (parsed.size() == 14) {
		for (std::size_t i = 0; i <= kMenuDialogIndexListboxSelector; ++i)
			outValues[i] = parsed[i];
		outValues[kMenuDialogIndexDialogFrame] = parsed[12];
		outValues[kMenuDialogIndexDialogText] = parsed[13];
		outValues[kMenuDialogIndexDialogBackground] = parsed[12];
	} else if (parsed.size() == 13) {
		for (std::size_t i = 0; i <= kMenuDialogIndexListboxSelector; ++i)
			outValues[i] = parsed[i];
	} else if (parsed.size() == 12) {
		for (std::size_t i = 0; i < 11; ++i)
			outValues[i] = parsed[i];
	} else if (parsed.size() == 11) {
		for (std::size_t i = 0; i < 11; ++i)
			outValues[i] = parsed[i];
	} else {
		return setError(errorMessage, "Unexpected MENUDIALOGCOLORS list size.");
	}
	if (parsed.size() <= kMenuDialogIndexButtonDefault) outValues[kMenuDialogIndexButtonDefault] = outValues[kMenuDialogIndexButtonDescription];
	if (parsed.size() <= kMenuDialogIndexButtonSelected) outValues[kMenuDialogIndexButtonSelected] = outValues[kMenuDialogIndexButtonDescription];
	if (parsed.size() <= kMenuDialogIndexButtonDisabled) outValues[kMenuDialogIndexButtonDisabled] = outValues[kMenuDialogIndexInactiveElements];
	if (parsed.size() <= kMenuDialogIndexInputLineNormal) outValues[kMenuDialogIndexInputLineNormal] = defaultColorForSlot(kPaletteDialogInputLineNormal);
	if (parsed.size() <= kMenuDialogIndexInputLineSelected) outValues[kMenuDialogIndexInputLineSelected] = outValues[kMenuDialogIndexListboxSelector];
	if (parsed.size() <= kMenuDialogIndexInputLineArrows) outValues[kMenuDialogIndexInputLineArrows] = outValues[kMenuDialogIndexDialogFrame];
	if (parsed.size() <= kMenuDialogIndexHistoryArrow) outValues[kMenuDialogIndexHistoryArrow] = outValues[kMenuDialogIndexDialogFrame];
	if (parsed.size() <= kMenuDialogIndexHistorySides) outValues[kMenuDialogIndexHistorySides] = outValues[kMenuDialogIndexDialogFrame];
	if (parsed.size() <= kMenuDialogIndexMenuBarHotkey) outValues[kMenuDialogIndexMenuBarHotkey] = defaultColorForSlot(kMrPaletteMenuBarHotkey);
	if (parsed.size() <= kMenuDialogIndexSpinnerHandles) outValues[kMenuDialogIndexSpinnerHandles] = defaultColorForSlot(kMrPaletteSpinnerHandles);
	if (parsed.size() <= kMenuDialogIndexSpinnerDisplay) outValues[kMenuDialogIndexSpinnerDisplay] = defaultColorForSlot(kMrPaletteSpinnerDisplay);
	if (parsed.size() <= kMenuDialogIndexFocusedSpinnerHandles) outValues[kMenuDialogIndexFocusedSpinnerHandles] = defaultColorForSlot(kMrPaletteFocusedSpinnerHandles);
	if (parsed.size() <= kMenuDialogIndexFocusedSpinnerDisplay) outValues[kMenuDialogIndexFocusedSpinnerDisplay] = defaultColorForSlot(kMrPaletteFocusedSpinnerDisplay);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool parseOtherColorListLiteral(const std::string &literal, std::array<unsigned char, MRColorSetupSettings::kOtherCount> &outValues, std::string *errorMessage) {
	std::string text = trimAscii(literal);

	if (text.empty()) return setError(errorMessage, "Empty color list.");
	if (text.size() < 3 || text[0] != 'v' || text[2] != ':') return setError(errorMessage, "Expected color list version prefix (e.g. v1:...).");

	std::vector<unsigned char> parsed;
	std::size_t cursor = 3;
	while (cursor <= text.size()) {
		std::size_t comma = text.find(',', cursor);
		std::string token = text.substr(cursor, comma == std::string::npos ? std::string::npos : comma - cursor);
		unsigned char value = 0;

		if (!parseHexColorToken(token, value)) return setError(errorMessage, "Expected hex color list (e.g. v1:70,7F,...).");
		parsed.push_back(value);
		if (comma == std::string::npos) break;
		cursor = comma + 1;
	}

	if (parsed.size() == MRColorSetupSettings::kOtherCount)
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
	else if (parsed.size() == 9) {
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[9] = defaultColorForSlot(kMrPaletteDesktop);
		outValues[10] = defaultColorForSlot(kMrPaletteVirtualDesktopMarker);
	} else if (parsed.size() == 8) {
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[8] = defaultColorForSlot(kMrPaletteCursorPositionMarker);
		outValues[9] = defaultColorForSlot(kMrPaletteDesktop);
		outValues[10] = defaultColorForSlot(kMrPaletteVirtualDesktopMarker);
	} else if (parsed.size() == 7) {
		for (std::size_t i = 0; i < parsed.size(); ++i)
			outValues[i] = parsed[i];
		outValues[7] = defaultColorForSlot(kMrPaletteMessageHero);
		outValues[8] = defaultColorForSlot(kMrPaletteCursorPositionMarker);
		outValues[9] = defaultColorForSlot(kMrPaletteDesktop);
		outValues[10] = defaultColorForSlot(kMrPaletteVirtualDesktopMarker);
	} else if (parsed.size() == 10) {
		for (std::size_t i = 0; i < 7; ++i)
			outValues[i] = parsed[i];
		outValues[7] = defaultColorForSlot(kMrPaletteMessageHero);
		outValues[8] = defaultColorForSlot(kMrPaletteCursorPositionMarker);
		outValues[9] = defaultColorForSlot(kMrPaletteDesktop);
		outValues[10] = defaultColorForSlot(kMrPaletteVirtualDesktopMarker);
	}
	else
		return setError(errorMessage, "Unexpected OTHERCOLORS list size.");
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool applyColorSetupValueToGroup(MRColorSetupSettings &configured, const std::string &key, const std::string &value, std::string *errorMessage) {
	const ColorGroupDefinition *definition = findColorGroupDefinitionByKey(key);

	if (definition == nullptr) return setError(errorMessage, "Unknown color setup key.");
	switch (definition->group) {
		case MRColorSetupGroup::Window:
			if (!parseWindowColorListLiteral(value, configured.windowColors, errorMessage)) return false;
			break;
		case MRColorSetupGroup::MenuDialog:
			if (!parseMenuDialogColorListLiteral(value, configured.menuDialogColors, errorMessage)) return false;
			break;
		case MRColorSetupGroup::Help:
			if (!parseColorListLiteral(value, configured.helpColors, errorMessage)) return false;
			break;
		case MRColorSetupGroup::Other:
			if (!parseOtherColorListLiteral(value, configured.otherColors, errorMessage)) return false;
			break;
		case MRColorSetupGroup::MiniMap:
			if (!parseMiniMapColorListLiteral(value, configured.miniMapColors, errorMessage)) return false;
			break;
		case MRColorSetupGroup::FileCompareMiniMap:
			if (!parseFileCompareMiniMapColorListLiteral(value, configured.fileCompareMiniMapColors, errorMessage)) return false;
			break;
		case MRColorSetupGroup::Code:
			if (!parseCodeColorListLiteral(value, configured.codeColors, errorMessage)) return false;
			break;
		case MRColorSetupGroup::FileCompare:
			if (!parseFileCompareColorListLiteral(value, configured.fileCompareColors, errorMessage)) return false;
			break;
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

} // namespace

bool applyColorSetupValueInternal(MRColorSetupSettings &configured, const std::string &key, const std::string &value, std::string *errorMessage) {
	return applyColorSetupValueToGroup(configured, key, value, errorMessage);
}

std::string defaultColorThemePathForSettings(std::string_view settingsPath) {
	std::string dir = directoryPartOf(makeAbsolutePath(std::string(settingsPath)));
	if (dir.empty()) dir = builtInTempDirectoryPath();
	return appendFileName(dir, "default-theme.mrmac");
}

bool parseThemeSetupAssignments(const std::string &source, std::map<std::string, std::string> &assignments, bool *upgradeRequired, std::string *errorMessage) {
	static const std::regex pattern("(?:MRSETUP\\s*\\(\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*\\)|([A-Z_][A-Z0-9_]*)\\s*\\(\\s*'((?:''|[^'])*)'\\s*\\))", std::regex::icase);
	bool localUpgradeRequired = false;
	assignments.clear();

	auto begin = std::sregex_iterator(source.begin(), source.end(), pattern);
	auto end = std::sregex_iterator();

	for (auto it = begin; it != end; ++it) {
		const bool mrsetupRecord = (*it)[1].matched;
		std::string key = trimAscii(unescapeMrmacSingleQuotedLiteral(mrsetupRecord ? (*it)[1].str() : (*it)[3].str()));
		std::string value = unescapeMrmacSingleQuotedLiteral(mrsetupRecord ? (*it)[2].str() : (*it)[4].str());
		if (key.empty()) continue;
		assignments[upperAscii(key)] = value;
	}
	{
		const auto versionIt = assignments.find(std::string(mrThemeVersionSetupKey()));
		const std::uint64_t currentVersion = mrCurrentPersistenceVersion();

		if (versionIt == assignments.end()) localUpgradeRequired = true;
		else {
			const std::string versionLiteral = trimAscii(versionIt->second);
			std::uint64_t parsedVersion = 0;

			if (!mrParsePersistenceVersion(versionLiteral, parsedVersion)) return setError(errorMessage, mrInvalidPersistenceVersionMessage("theme file"));
			if (parsedVersion > currentVersion) return setError(errorMessage, mrFuturePersistenceVersionMessage("Theme file", versionLiteral));
			if (parsedVersion < currentVersion) localUpgradeRequired = true;
		}
	}

	MRColorSetupSettings defaults = resolveColorSetupDefaults();
	if (!assignments.contains("WINDOWCOLORS")) assignments["WINDOWCOLORS"] = formatWindowColorListLiteral(defaults.windowColors);
	if (!assignments.contains("MENUDIALOGCOLORS")) assignments["MENUDIALOGCOLORS"] = formatColorListLiteral(defaults.menuDialogColors);
	if (!assignments.contains("HELPCOLORS")) assignments["HELPCOLORS"] = formatColorListLiteral(defaults.helpColors);
	if (!assignments.contains("OTHERCOLORS")) assignments["OTHERCOLORS"] = formatColorListLiteral(defaults.otherColors);
	if (!assignments.contains("MINIMAPCOLORS")) assignments["MINIMAPCOLORS"] = formatColorListLiteral(defaults.miniMapColors);
	if (!assignments.contains("FILECOMPAREMINIMAPCOLORS")) assignments["FILECOMPAREMINIMAPCOLORS"] = formatColorListLiteral(defaults.fileCompareMiniMapColors);
	if (!assignments.contains("CODECOLORS")) assignments["CODECOLORS"] = formatColorListLiteral(defaults.codeColors);
	if (!assignments.contains("FILECOMPARECOLORS")) assignments["FILECOMPARECOLORS"] = formatFileCompareColorListLiteral(defaults.fileCompareColors);
	if (upgradeRequired != nullptr) *upgradeRequired = localUpgradeRequired;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

void ensureConfiguredColorSettingsInitialized() {
	if (configuredColorSettingsInitialized()) return;
	configuredColorSettings() = resolveColorSetupDefaults();
	configuredColorSettingsInitialized() = true;
}

MRColorSetupSettings resolveColorSetupDefaults() {
	return defaultsFromColorGroups();
}

MRColorSetupSettings configuredColorSetupSettings() {
	recordSettingsRuntimeRead();
	ensureConfiguredColorSettingsInitialized();
	return configuredColorSettings();
}

const char *colorSetupGroupTitle(MRColorSetupGroup group) {
	const ColorGroupDefinition *definition = findColorGroupDefinition(group);
	return definition != nullptr ? definition->title : "";
}

const char *colorSetupGroupKey(MRColorSetupGroup group) {
	const ColorGroupDefinition *definition = findColorGroupDefinition(group);
	return definition != nullptr ? definition->key : "";
}

const MRColorSetupItem *colorSetupGroupItems(MRColorSetupGroup group, std::size_t &count) {
	const ColorGroupDefinition *definition = findColorGroupDefinition(group);
	if (definition == nullptr) {
		count = 0;
		return nullptr;
	}
	count = definition->count;
	return definition->items;
}

bool setConfiguredColorSetupGroupValues(MRColorSetupGroup group, const unsigned char *values, std::size_t count, std::string *errorMessage) {
	const ColorGroupDefinition *definition = findColorGroupDefinition(group);
	MRColorSetupSettings &configured = configuredColorSettings();

	ensureConfiguredColorSettingsInitialized();
	if (definition == nullptr) return setError(errorMessage, "Unknown color setup group.");
	if (values == nullptr) return setError(errorMessage, "Unexpected color setup group value count.");
	if (group == MRColorSetupGroup::Code) {
		if (!codeColorCountAccepted(count, definition->count)) return setError(errorMessage, "Unexpected color setup group value count.");
	} else if (count != definition->count)
		return setError(errorMessage, "Unexpected color setup group value count.");

	switch (group) {
		case MRColorSetupGroup::Window:
			for (std::size_t i = 0; i < configured.windowColors.size(); ++i) configured.windowColors[i] = values[i];
			break;
		case MRColorSetupGroup::MenuDialog:
			for (std::size_t i = 0; i < configured.menuDialogColors.size(); ++i) configured.menuDialogColors[i] = values[i];
			break;
		case MRColorSetupGroup::Help:
			for (std::size_t i = 0; i < configured.helpColors.size(); ++i) configured.helpColors[i] = values[i];
			break;
		case MRColorSetupGroup::Other:
			for (std::size_t i = 0; i < configured.otherColors.size(); ++i) configured.otherColors[i] = values[i];
			break;
		case MRColorSetupGroup::MiniMap:
			for (std::size_t i = 0; i < configured.miniMapColors.size(); ++i) configured.miniMapColors[i] = values[i];
			break;
		case MRColorSetupGroup::FileCompareMiniMap:
			for (std::size_t i = 0; i < configured.fileCompareMiniMapColors.size(); ++i) configured.fileCompareMiniMapColors[i] = values[i];
			break;
		case MRColorSetupGroup::Code:
			for (std::size_t i = 0; i < configured.codeColors.size(); ++i)
				configured.codeColors[i] = i < count ? values[i] : defaultColorForSlot(kCodeColorItems[i].paletteIndex);
			break;
		case MRColorSetupGroup::FileCompare:
			for (std::size_t i = 0; i < configured.fileCompareColors.size(); ++i) configured.fileCompareColors[i] = values[i];
			break;
	}
	configuredColorThemeDisplayNameValue().clear();
	recordSettingsRuntimeWrite();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

void configuredColorSetupGroupValues(MRColorSetupGroup group, unsigned char *values, std::size_t count) {
	const ColorGroupDefinition *definition = findColorGroupDefinition(group);
	MRColorSetupSettings configured = configuredColorSetupSettings();

	if (values == nullptr || definition == nullptr || count != definition->count) return;
	switch (group) {
		case MRColorSetupGroup::Window:
			for (std::size_t i = 0; i < configured.windowColors.size(); ++i) values[i] = configured.windowColors[i];
			break;
		case MRColorSetupGroup::MenuDialog:
			for (std::size_t i = 0; i < configured.menuDialogColors.size(); ++i) values[i] = configured.menuDialogColors[i];
			break;
		case MRColorSetupGroup::Help:
			for (std::size_t i = 0; i < configured.helpColors.size(); ++i) values[i] = configured.helpColors[i];
			break;
		case MRColorSetupGroup::Other:
			for (std::size_t i = 0; i < configured.otherColors.size(); ++i) values[i] = configured.otherColors[i];
			break;
		case MRColorSetupGroup::MiniMap:
			for (std::size_t i = 0; i < configured.miniMapColors.size(); ++i) values[i] = configured.miniMapColors[i];
			break;
		case MRColorSetupGroup::FileCompareMiniMap:
			for (std::size_t i = 0; i < configured.fileCompareMiniMapColors.size(); ++i) values[i] = configured.fileCompareMiniMapColors[i];
			break;
		case MRColorSetupGroup::Code:
			for (std::size_t i = 0; i < configured.codeColors.size(); ++i) values[i] = configured.codeColors[i];
			break;
		case MRColorSetupGroup::FileCompare:
			for (std::size_t i = 0; i < configured.fileCompareColors.size(); ++i) values[i] = configured.fileCompareColors[i];
			break;
	}
}

bool applyConfiguredColorSetupValue(const std::string &key, const std::string &value, std::string *errorMessage, bool clearThemeDisplayName) {
	MRColorSetupSettings configured = configuredColorSetupSettings();

	if (!applyColorSetupValueInternal(configured, key, value, errorMessage)) return false;
	configuredColorSettings() = configured;
	configuredColorSettingsInitialized() = true;
	if (clearThemeDisplayName) configuredColorThemeDisplayNameValue().clear();
	recordSettingsRuntimeWrite();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool configuredColorSlotOverride(unsigned char paletteIndex, unsigned char &value) {
	ensureConfiguredColorSettingsInitialized();
	const MRColorSetupSettings &configured = configuredColorSettings();
	unsigned char dialogFrame = 0;
	unsigned char dialogText = 0;
	unsigned char dialogBackground = 0;
	unsigned char dialogInactiveCluster = 0;
	unsigned char dialogInactiveElements = 0;
	unsigned char dialogListNormal = 0;
	unsigned char dialogListFocused = 0;
	unsigned char dialogListSelected = 0;
	unsigned char dropListNormal = 0;
	unsigned char dropListSelected = 0;
	unsigned char inputLineNormal = 0;
	unsigned char inputLineSelected = 0;
	unsigned char inputLineArrows = 0;
	unsigned char historyArrow = 0;
	unsigned char historySides = 0;

	if (paletteIndex == kPaletteMenuSelectedHotkey) {
		for (std::size_t i = 0; i < std::size(kMenuDialogColorItems); ++i)
			if (kMenuDialogColorItems[i].paletteIndex == kPaletteMenuHotkey) {
				value = configured.menuDialogColors[i];
				return true;
			}
	}

	for (std::size_t i = 0; i < std::size(kMenuDialogColorItems); ++i) {
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteGrayDialogFrame) dialogFrame = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteGrayDialogText) dialogText = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteGrayDialogBackground) dialogBackground = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteDialogListNormal) dialogListNormal = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteDialogListFocused) dialogListFocused = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteDialogListSelectedInactive) dialogListSelected = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kMrPaletteDropListDescription) dropListNormal = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kMrPaletteDropListSelectedInactive) dropListSelected = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteDialogInactiveClusterGray) dialogInactiveCluster = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kMrPaletteDialogInactiveElements) dialogInactiveElements = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteDialogInputLineNormal) inputLineNormal = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteDialogInputLineSelected) inputLineSelected = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteDialogInputLineArrows) inputLineArrows = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteDialogHistoryArrow) historyArrow = configured.menuDialogColors[i];
		if (kMenuDialogColorItems[i].paletteIndex == kPaletteDialogHistorySides) historySides = configured.menuDialogColors[i];
	}

	switch (paletteIndex) {
		case kPaletteGrayDialogFrame:
		case kPaletteGrayDialogFrameAccent:
		case kPaletteBlueDialogFrame:
		case kPaletteBlueDialogFrameAccent:
		case kPaletteCyanDialogFrame:
		case kPaletteCyanDialogFrameAccent:
			value = dialogFrame;
			return true;
		case kPaletteGrayDialogText:
		case kPaletteBlueDialogText:
		case kPaletteCyanDialogText:
			value = dialogText;
			return true;
		case kPaletteGrayDialogBackground:
		case kPaletteBlueDialogBackground:
		case kPaletteCyanDialogBackground:
			value = dialogBackground;
			return true;
		case kPaletteDialogListFrameLegacyPrimary:
		case kPaletteDialogListFrameLegacySecondary:
		case kPaletteDialogListFrameExtendedPrimary:
		case kPaletteDialogListFrameExtendedSecondary:
			value = dialogFrame;
			return true;
		case kPaletteDialogListNormalLegacy:
		case kPaletteDialogListNormal:
			value = dialogListNormal;
			return true;
		case kPaletteDialogListFocusedLegacy:
		case kPaletteDialogListFocused:
			value = dialogListFocused;
			return true;
		case kPaletteDialogListSelectedLegacy:
		case kPaletteDialogListSelectedInactive:
			value = dialogListSelected;
			return true;
		case kPaletteDialogListTextLegacy:
		case kPaletteDialogListText:
			value = dialogText;
			return true;
		case kMrPaletteDropListDescription:
			value = dropListNormal;
			return true;
		case kMrPaletteDropListSelectedInactive:
			value = dropListSelected;
			return true;
		case kPaletteDialogInactiveClusterGray:
		case kPaletteDialogInactiveClusterBlue:
		case kPaletteDialogInactiveClusterCyan:
			value = dialogInactiveCluster;
			return true;
		case kMrPaletteDialogInactiveElements:
			value = dialogInactiveElements;
			return true;
		case kPaletteDialogInputLineNormal:
		case kPaletteBlueDialogInputLineNormal:
		case kPaletteCyanDialogInputLineNormal:
			value = inputLineNormal;
			return true;
		case kPaletteDialogInputLineSelected:
		case kPaletteBlueDialogInputLineSelected:
		case kPaletteCyanDialogInputLineSelected:
			value = inputLineSelected;
			return true;
		case kPaletteDialogInputLineArrows:
		case kPaletteBlueDialogInputLineArrows:
		case kPaletteCyanDialogInputLineArrows:
			value = inputLineArrows;
			return true;
		case kPaletteDialogHistoryArrow:
		case kPaletteBlueDialogHistoryArrow:
		case kPaletteCyanDialogHistoryArrow:
			value = historyArrow;
			return true;
		case kPaletteDialogHistorySides:
		case kPaletteBlueDialogHistorySides:
		case kPaletteCyanDialogHistorySides:
			value = historySides;
			return true;
		case kMrPaletteDesktop:
			value = configured.otherColors[MRColorSetupSettings::kOtherCount - 2];
			return true;
		case kMrPaletteVirtualDesktopMarker:
			value = configured.otherColors[MRColorSetupSettings::kOtherCount - 1];
			return true;
		default:
			break;
	}

	for (std::size_t i = 0; i < std::size(kWindowColorItems); ++i)
		if (kWindowColorItems[i].paletteIndex == paletteIndex) {
			value = configured.windowColors[i];
			return true;
		}
	for (std::size_t i = 0; i < std::size(kMenuDialogColorItems); ++i)
		if (kMenuDialogColorItems[i].paletteIndex == paletteIndex) {
			value = configured.menuDialogColors[i];
			return true;
		}
	for (std::size_t i = 0; i < std::size(kHelpColorItems); ++i)
		if (kHelpColorItems[i].paletteIndex == paletteIndex) {
			value = configured.helpColors[i];
			return true;
		}
	for (std::size_t i = 0; i < std::size(kOtherColorItems); ++i)
		if (kOtherColorItems[i].paletteIndex == paletteIndex) {
			value = configured.otherColors[i];
			return true;
		}
	for (std::size_t i = 0; i < std::size(kMiniMapColorItems); ++i)
		if (kMiniMapColorItems[i].paletteIndex == paletteIndex) {
			value = configured.miniMapColors[i];
			return true;
		}
	for (std::size_t i = 0; i < std::size(kFileCompareMiniMapColorItems); ++i)
		if (kFileCompareMiniMapColorItems[i].paletteIndex == paletteIndex) {
			value = configured.fileCompareMiniMapColors[i];
			return true;
		}
	for (std::size_t i = 0; i < std::size(kCodeColorItems); ++i)
		if (kCodeColorItems[i].paletteIndex == paletteIndex) {
			value = configured.codeColors[i];
			return true;
		}
	for (std::size_t i = 0; i < std::size(kFileCompareColorItems); ++i)
		if (kFileCompareColorItems[i].paletteIndex == paletteIndex) {
			value = configured.fileCompareColors[i];
			return true;
		}
	return false;
}

std::string defaultColorThemeFilePath() {
	return defaultColorThemePathForSettings(configuredSettingsMacroFilePath());
}

bool validateColorThemeFilePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);
	struct stat st;

	if (normalized.empty()) return setError(errorMessage, "Empty color theme URI.");
	if (::stat(normalized.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return setError(errorMessage, "Color theme URI must include a filename.");
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setConfiguredColorThemeFilePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);

	if (!validateColorThemeFilePath(path, errorMessage)) return false;
	configuredColorThemeFile() = makeAbsolutePath(normalized);
	static_cast<void>(setScopedDialogLastPath(MRDialogHistoryScope::SetupThemeLoad, configuredColorThemeFile(), nullptr));
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredColorThemeFilePath() {
	const std::string &configured = configuredColorThemeFile();

	recordSettingsRuntimeRead();
	if (!configured.empty()) return makeAbsolutePath(configured);
	return defaultColorThemeFilePath();
}

std::string configuredColorThemeDisplayName() {
	std::string name = trimAscii(configuredColorThemeDisplayNameValue());

	recordSettingsRuntimeRead();
	if (name.empty()) return std::string("default");
	return name;
}

bool setConfiguredColorThemeDisplayName(const std::string &name, std::string *errorMessage) {
	configuredColorThemeDisplayNameValue() = trimAscii(name);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string buildColorThemeMacroSource(const MRColorSetupSettings &colors) {
	std::string source;
	const std::string themeName = trimAscii(configuredColorThemeDisplayNameValue());

	source += "$MACRO MR_COLOR_THEME FROM EDIT;\n";
	source += "THEME_RESET();\n";
	source += "THEME_VERSION('" + escapeMrmacSingleQuotedLiteral(mrCurrentPersistenceVersionString()) + "');\n";
	source += "WINDOWCOLORS('" + escapeMrmacSingleQuotedLiteral(formatWindowColorListLiteral(colors.windowColors)) + "');\n";
	source += "MENUDIALOGCOLORS('" + escapeMrmacSingleQuotedLiteral(formatColorListLiteral(colors.menuDialogColors)) + "');\n";
	source += "HELPCOLORS('" + escapeMrmacSingleQuotedLiteral(formatColorListLiteral(colors.helpColors)) + "');\n";
	source += "OTHERCOLORS('" + escapeMrmacSingleQuotedLiteral(formatColorListLiteral(colors.otherColors)) + "');\n";
	source += "MINIMAPCOLORS('" + escapeMrmacSingleQuotedLiteral(formatColorListLiteral(colors.miniMapColors)) + "');\n";
	source += "FILECOMPAREMINIMAPCOLORS('" + escapeMrmacSingleQuotedLiteral(formatColorListLiteral(colors.fileCompareMiniMapColors)) + "');\n";
	source += "CODECOLORS('" + escapeMrmacSingleQuotedLiteral(formatColorListLiteral(colors.codeColors)) + "');\n";
	source += "FILECOMPARECOLORS('" + escapeMrmacSingleQuotedLiteral(formatFileCompareColorListLiteral(colors.fileCompareColors)) + "');\n";
	if (!themeName.empty()) source += "THEME_NAME('" + escapeMrmacSingleQuotedLiteral(themeName) + "');\n";
	source += "END_MACRO;\n";
	return source;
}

bool writeColorThemeFile(const std::string &themeUri, std::string *errorMessage) {
	std::string themePath = normalizeConfiguredPathInput(themeUri);
	std::string themeDir = directoryPartOf(themePath);
	std::string source;

	if (!validateColorThemeFilePath(themePath, errorMessage)) return false;
	if (!ensureDirectoryTree(themeDir, errorMessage)) return false;
	source = buildColorThemeMacroSource(configuredColorSetupSettings());
	if (!writeTextFile(themePath, source)) return setError(errorMessage, "Unable to write color theme file: " + themePath);
	if (!setConfiguredColorThemeFilePath(themePath, errorMessage)) return false;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool ensureColorThemeFileExists(const std::string &themeUri, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(themeUri);
	std::string themeDir = directoryPartOf(normalized);
	struct stat st;

	if (!validateColorThemeFilePath(normalized, errorMessage)) return false;
	if (::stat(normalized.c_str(), &st) == 0) {
		if (S_ISDIR(st.st_mode)) return setError(errorMessage, "Color theme URI must include a filename.");
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (!ensureDirectoryTree(themeDir, errorMessage)) return false;
	if (!writeTextFile(normalized, buildColorThemeMacroSource(resolveColorSetupDefaults()))) return setError(errorMessage, "Unable to write color theme file: " + normalized);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool loadColorThemeFile(const std::string &themeUri, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(themeUri);
	std::string source;
	std::map<std::string, std::string> assignments;
	MRColorSetupSettings colors = resolveColorSetupDefaults();
	bool upgradeRequired = false;
	std::string themeName;

	if (!validateColorThemeFilePath(normalized, errorMessage)) return false;
	if (!ensureColorThemeFileExists(normalized, errorMessage)) return false;
	if (!readTextFile(normalized, source)) return setError(errorMessage, "Unable to read color theme file: " + normalized);
	if (!parseThemeSetupAssignments(source, assignments, &upgradeRequired, errorMessage)) return false;
	(void)upgradeRequired;
	for (const ColorGroupDefinition &group : kColorGroups)
		if (!applyColorSetupValueInternal(colors, group.key, assignments[group.key], errorMessage)) return false;
	configuredColorSettings() = colors;
	configuredColorSettingsInitialized() = true;
	themeName = trimAscii(assignments["THEME_NAME"]);
	if (themeName.empty()) themeName = fileNamePartOf(normalized);
	if (!setConfiguredColorThemeDisplayName(themeName, errorMessage)) return false;
	if (!setConfiguredColorThemeFilePath(normalized, errorMessage)) return false;
	recordSettingsRuntimeWrite();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool loadWindowColorThemeGroupValues(const std::string &themeUri, std::array<unsigned char, MRColorSetupSettings::kWindowCount> &outValues, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(themeUri);
	std::string source;
	std::map<std::string, std::string> assignments;
	bool upgradeRequired = false;

	if (!validateColorThemeFilePath(normalized, errorMessage)) return false;
	if (!ensureColorThemeFileExists(normalized, errorMessage)) return false;
	if (!readTextFile(normalized, source)) return setError(errorMessage, "Unable to read color theme file: " + normalized);
	if (!parseThemeSetupAssignments(source, assignments, &upgradeRequired, errorMessage)) return false;
	(void)upgradeRequired;
	if (!parseWindowColorListLiteral(assignments["WINDOWCOLORS"], outValues, errorMessage)) return false;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredDefaultProfileDescription() {
	recordSettingsRuntimeRead();
	return configuredDefaultProfileDescriptionValue();
}

bool setConfiguredDefaultProfileDescription(const std::string &value, std::string *errorMessage) {
	const std::string normalized = trimAscii(value);

	if (configuredDefaultProfileDescriptionValue() != normalized) {
		configuredDefaultProfileDescriptionValue() = normalized;
		markConfiguredSettingsDirty();
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

const std::vector<MRKeymapProfile> &configuredKeymapProfiles() {
	recordSettingsRuntimeRead();
	return configuredKeymapProfilesValue();
}

bool setConfiguredKeymapProfiles(const std::vector<MRKeymapProfile> &profiles, std::string *errorMessage) {
	std::vector<MRKeymapProfile> normalized = profiles;
	std::string normalizedActiveProfile = configuredActiveKeymapProfileValue();
	std::string runtimeError;
	const std::vector<MRKeymapProfile> previousProfiles = configuredKeymapProfilesValue();
	const std::string previousActiveProfile = configuredActiveKeymapProfileValue();

	for (MRKeymapProfile &profile : normalized) {
		profile.name = trimAscii(profile.name);
		profile.description = trimAscii(profile.description);
		for (MRKeymapBindingRecord &binding : profile.bindings) {
			binding.profileName = trimAscii(binding.profileName);
			binding.target.target = trimAscii(binding.target.target);
			binding.description = trimAscii(binding.description);
		}
	}
	bool activeProfileExists = false;
	for (const MRKeymapProfile &profile : normalized) {
		if (profile.name == normalizedActiveProfile) {
			activeProfileExists = true;
			break;
		}
	}
	if (normalizedActiveProfile.empty() || !activeProfileExists) normalizedActiveProfile.clear();

	const auto diagnostics = validateKeymapProfiles(normalized);
	for (const MRKeymapDiagnostic &diagnostic : diagnostics)
		if (diagnostic.severity == MRKeymapDiagnosticSeverity::Error) return setError(errorMessage, diagnostic.message);
	if (!runtimeKeymapResolver().rebuild(normalized, normalizedActiveProfile, &runtimeError)) return setError(errorMessage, runtimeError);

	configuredKeymapProfilesValue() = normalized;
	configuredActiveKeymapProfileValue() = normalizedActiveProfile;
	if (previousProfiles != configuredKeymapProfilesValue() || previousActiveProfile != configuredActiveKeymapProfileValue()) markConfiguredSettingsDirty();
	mrLogMessage(summarizeConfiguredKeymapsForLog(configuredKeymapProfilesValue(), configuredActiveKeymapProfileValue()));
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredKeymapFilePath() {
	const std::string &configured = configuredKeymapFileValue();

	recordSettingsRuntimeRead();
	return configured.empty() ? std::string() : makeAbsolutePath(configured);
}

bool setConfiguredKeymapFilePath(const std::string &path, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);
	struct stat st;

	if (normalized.empty()) {
		if (!configuredKeymapFileValue().empty()) configuredKeymapFileValue().clear();
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (::stat(normalized.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return setError(errorMessage, "Keymap URI must include a filename.");
	configuredKeymapFileValue() = makeAbsolutePath(normalized);
	static_cast<void>(setScopedDialogLastPath(MRDialogHistoryScope::KeymapProfileLoad, configuredKeymapFileValue(), nullptr));
	static_cast<void>(setScopedDialogLastPath(MRDialogHistoryScope::KeymapProfileSave, configuredKeymapFileValue(), nullptr));
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string configuredActiveKeymapProfile() {
	recordSettingsRuntimeRead();
	return configuredActiveKeymapProfileValue();
}

bool setConfiguredActiveKeymapProfile(const std::string &value, std::string *errorMessage) {
	std::string normalized = trimAscii(value);
	std::string runtimeError;
	const std::string previousActiveProfile = configuredActiveKeymapProfileValue();

	bool activeProfileExists = false;
	for (const MRKeymapProfile &profile : configuredKeymapProfilesValue()) {
		if (profile.name == normalized) {
			activeProfileExists = true;
			break;
		}
	}
	if (!normalized.empty() && !activeProfileExists) normalized.clear();
	if (!runtimeKeymapResolver().rebuild(configuredKeymapProfilesValue(), normalized, &runtimeError)) return setError(errorMessage, runtimeError);
	configuredActiveKeymapProfileValue() = normalized;
	if (previousActiveProfile != configuredActiveKeymapProfileValue()) markConfiguredSettingsDirty();
	if (normalized.empty()) mrLogMessage("Keymap active profile cleared; built-in key handling remains active.");
	else
		mrLogMessage("Keymap active profile set to '" + normalized + "'.");
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}
