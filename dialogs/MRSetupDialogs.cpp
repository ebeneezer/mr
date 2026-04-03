#define Uses_TApplication
#define Uses_TChDirDialog
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TFileDialog
#define Uses_MsgBox
#define Uses_TObject
#define Uses_TButton
#define Uses_TColorGroup
#define Uses_TColorGroupList
#define Uses_TColorItem
#define Uses_TColorItemList
#define Uses_TColorDisplay
#define Uses_TColorSelector
#define Uses_TMonoSelector
#define Uses_TLabel
#define Uses_TDrawBuffer
#define Uses_TGroup
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TStaticText
#define Uses_TView
#include <tvision/tv.h>

#include "MRSetupDialogs.hpp"

#include "../app/MRCommands.hpp"
#include "../app/TMREditorApp.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../ui/MRPalette.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "MRSetupDialogCommon.hpp"

#include <cctype>
#include <cstring>
#include <limits.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
enum : ushort {
	cmMrSetupPathsHelp = 3800,
	cmMrSetupPathsBrowseSettingsUri,
	cmMrSetupPathsBrowseMacroPath,
	cmMrSetupPathsBrowseHelpUri,
	cmMrSetupPathsBrowseTempPath,
	cmMrSetupPathsBrowseShellUri
};

enum {
	kPathFieldSize = 256
};

struct PathsDialogRecord {
	char settingsMacroPath[kPathFieldSize];
	char macroDirectoryPath[kPathFieldSize];
	char helpFilePath[kPathFieldSize];
	char tempDirectoryPath[kPathFieldSize];
	char shellExecutablePath[kPathFieldSize];
};

std::string trimAscii(const std::string &value) {
	std::size_t start = 0;
	std::size_t end = value.size();

	while (start < end && std::isspace(static_cast<unsigned char>(value[start])) != 0)
		++start;
	while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
		--end;
	return value.substr(start, end - start);
}

std::string ensureMrmacExtension(const std::string &path) {
	std::size_t dotPos = path.find_last_of('.');
	if (dotPos != std::string::npos) {
		std::string ext = path.substr(dotPos);
		for (std::size_t i = 0; i < ext.size(); ++i)
			ext[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(ext[i])));
		if (ext == ".mrmac")
			return path;
	}
	return path + ".mrmac";
}

std::string readRecordField(const char *value) {
	return trimAscii(value != nullptr ? value : "");
}

void writeRecordField(char *dest, std::size_t destSize, const std::string &value) {
	if (dest == nullptr || destSize == 0)
		return;
	std::memset(dest, 0, destSize);
	std::strncpy(dest, value.c_str(), destSize - 1);
	dest[destSize - 1] = '\0';
}

bool recordsEqual(const PathsDialogRecord &lhs, const PathsDialogRecord &rhs) {
	return readRecordField(lhs.settingsMacroPath) == readRecordField(rhs.settingsMacroPath) &&
	       readRecordField(lhs.macroDirectoryPath) == readRecordField(rhs.macroDirectoryPath) &&
	       readRecordField(lhs.helpFilePath) == readRecordField(rhs.helpFilePath) &&
	       readRecordField(lhs.tempDirectoryPath) == readRecordField(rhs.tempDirectoryPath) &&
	       readRecordField(lhs.shellExecutablePath) == readRecordField(rhs.shellExecutablePath);
}

MRSetupPaths pathsFromRecord(const PathsDialogRecord &record) {
	MRSetupPaths paths;
	paths.settingsMacroUri = normalizeConfiguredPathInput(readRecordField(record.settingsMacroPath));
	paths.macroPath = normalizeConfiguredPathInput(readRecordField(record.macroDirectoryPath));
	paths.helpUri = normalizeConfiguredPathInput(readRecordField(record.helpFilePath));
	paths.tempPath = normalizeConfiguredPathInput(readRecordField(record.tempDirectoryPath));
	paths.shellUri = normalizeConfiguredPathInput(readRecordField(record.shellExecutablePath));
	return paths;
}

void initPathsDialogRecord(PathsDialogRecord &record) {
	std::memset(&record, 0, sizeof(record));
	writeRecordField(record.settingsMacroPath, sizeof(record.settingsMacroPath), configuredSettingsMacroFilePath());
	writeRecordField(record.macroDirectoryPath, sizeof(record.macroDirectoryPath), defaultMacroDirectoryPath());
	writeRecordField(record.helpFilePath, sizeof(record.helpFilePath), configuredHelpFilePath());
	writeRecordField(record.tempDirectoryPath, sizeof(record.tempDirectoryPath), configuredTempDirectoryPath());
	writeRecordField(record.shellExecutablePath, sizeof(record.shellExecutablePath), configuredShellExecutablePath());
}

bool validatePathsRecord(const PathsDialogRecord &record, std::string &errorText) {
	std::string settingsPath = normalizeConfiguredPathInput(readRecordField(record.settingsMacroPath));
	std::string macroDir = normalizeConfiguredPathInput(readRecordField(record.macroDirectoryPath));
	std::string helpPath = normalizeConfiguredPathInput(readRecordField(record.helpFilePath));
	std::string tempDir = normalizeConfiguredPathInput(readRecordField(record.tempDirectoryPath));
	std::string shellPath = normalizeConfiguredPathInput(readRecordField(record.shellExecutablePath));

	if (!validateSettingsMacroFilePath(settingsPath, &errorText))
		return false;
	if (!validateMacroDirectoryPath(macroDir, &errorText))
		return false;
	if (!validateHelpFilePath(helpPath, &errorText))
		return false;
	if (!validateTempDirectoryPath(tempDir, &errorText))
		return false;
	if (!validateShellExecutablePath(shellPath, &errorText))
		return false;
	errorText.clear();
	return true;
}

bool saveAndReloadPathsRecord(const PathsDialogRecord &record, std::string &errorText) {
	MRSetupPaths paths = pathsFromRecord(record);
	TMREditorApp *app;

	if (!validatePathsRecord(record, errorText))
		return false;
	if (!writeSettingsMacroFile(paths, &errorText))
		return false;

	app = dynamic_cast<TMREditorApp *>(TProgram::application);
	if (app == nullptr) {
		errorText = "Application error: TMREditorApp is unavailable.";
		return false;
	}
	if (!app->reloadSettingsMacroFromPath(paths.settingsMacroUri, &errorText))
		return false;

	errorText.clear();
	return true;
}

ushort execDialogRaw(TDialog *dialog) {
	ushort result = cmCancel;
	if (dialog != 0) {
		result = TProgram::deskTop->execView(dialog);
		TObject::destroy(dialog);
	}
	return result;
}

ushort execDialogRawWithData(TDialog *dialog, void *data) {
	ushort result = cmCancel;
	if (dialog != 0) {
		if (data != nullptr)
			dialog->setData(data);
		result = TProgram::deskTop->execView(dialog);
		if (result != cmCancel && data != nullptr)
			dialog->getData(data);
		TObject::destroy(dialog);
	}
	return result;
}

std::string currentWorkingDirectoryLocal() {
	char cwd[PATH_MAX];

	if (::getcwd(cwd, sizeof(cwd)) == nullptr)
		return std::string();
	return std::string(cwd);
}

bool browseUriWithFileDialog(const char *title, const std::string &currentValue, std::string &selectedUri) {
	char fileName[MAXPATH];
	std::string seed = trimAscii(currentValue);
	ushort result;

	if (seed.empty())
		seed = "*.*";
	std::memset(fileName, 0, sizeof(fileName));
	writeRecordField(fileName, sizeof(fileName), seed);
	result = execDialogRawWithData(new TFileDialog("*.*", title, "~N~ame", fdOKButton, 230), fileName);
	if (result == cmCancel)
		return false;
	selectedUri = normalizeConfiguredPathInput(fileName);
	return true;
}

bool browsePathWithDirectoryDialog(const std::string &currentValue, std::string &selectedPath) {
	std::string originalCwd = currentWorkingDirectoryLocal();
	std::string seed = normalizeConfiguredPathInput(trimAscii(currentValue));
	std::string picked;
	ushort result;

	if (!seed.empty())
		(void)::chdir(seed.c_str());
	result = execDialogRaw(new TChDirDialog(cdNormal, 231));
	picked = currentWorkingDirectoryLocal();
	if (!originalCwd.empty())
		(void)::chdir(originalCwd.c_str());
	if (result == cmCancel)
		return false;
	selectedPath = normalizeConfiguredPathInput(picked);
	return !selectedPath.empty();
}

bool chooseThemeFileForLoad(std::string &selectedUri) {
	char fileName[MAXPATH];
	std::string seed = configuredColorThemeFilePath();
	ushort result;

	if (seed.empty())
		seed = "*.mrmac";
	std::memset(fileName, 0, sizeof(fileName));
	writeRecordField(fileName, sizeof(fileName), seed);
	result = execDialogRawWithData(new TFileDialog("*.mrmac", "Load color theme", "~N~ame", fdOKButton, 232),
	                               fileName);
	if (result == cmCancel)
		return false;
	selectedUri = normalizeConfiguredPathInput(fileName);
	return true;
}

bool chooseThemeFileForSave(std::string &selectedUri) {
	char fileName[MAXPATH];
	std::string seed = configuredColorThemeFilePath();
	ushort result;

	if (seed.empty())
		seed = "*.mrmac";
	std::memset(fileName, 0, sizeof(fileName));
	writeRecordField(fileName, sizeof(fileName), seed);
	result = execDialogRawWithData(new TFileDialog("*.mrmac", "Save color theme as", "~N~ame", fdOKButton, 233),
	                               fileName);
	if (result == cmCancel)
		return false;
	selectedUri = normalizeConfiguredPathInput(ensureMrmacExtension(fileName));
	return true;
}

ushort execDialog(TDialog *dialog) {
	ushort result = execDialogRaw(dialog);
	if (result == cmHelp)
		mrShowProjectHelp();
	return result;
}

void applyDialogScrollbarSyncToPalette(TPalette &palette) {
	auto syncDialogScrollbarsToFrame = [&](int base) {
		palette[base + 3] = palette[base + 0];
		palette[base + 4] = palette[base + 0];
		palette[base + 23] = palette[base + 0];
		palette[base + 24] = palette[base + 0];
	};
	syncDialogScrollbarsToFrame(32);
	syncDialogScrollbarsToFrame(64);
	syncDialogScrollbarsToFrame(96);
}

TPalette buildColorSetupWorkingPalette() {
	static const TPalette basePalette = []() -> TPalette {
		static const int kBaseSlots = 135;
		static const int kTotalSlots = kMrPaletteChangedText;
		static const char cp[] = cpAppColor;
		TColorAttr data[kTotalSlots];
		int i = 0;

		for (i = 0; i < kBaseSlots; ++i)
			data[i] = static_cast<unsigned char>(cp[i]);
		data[kMrPaletteCurrentLine - 1] = data[10 - 1];
		data[kMrPaletteCurrentLineInBlock - 1] = data[12 - 1];
		data[kMrPaletteChangedText - 1] = data[14 - 1];
		return TPalette(data, static_cast<ushort>(kTotalSlots));
	}();
	TPalette palette = basePalette;
	unsigned char overrideValue = 0;

	for (int slot = 1; slot <= kMrPaletteChangedText; ++slot)
		if (configuredColorSlotOverride(static_cast<unsigned char>(slot), overrideValue))
			palette[slot] = overrideValue;
	applyDialogScrollbarSyncToPalette(palette);
	palette[1] = currentPalette.desktop;
	return palette;
}

bool applyWorkingColorPaletteToConfigured(const TPalette &palette, std::string &errorText) {
	static const MRColorSetupGroup groups[] = {MRColorSetupGroup::Window, MRColorSetupGroup::MenuDialog,
	                                           MRColorSetupGroup::Help, MRColorSetupGroup::Other};

	for (std::size_t g = 0; g < sizeof(groups) / sizeof(groups[0]); ++g) {
		std::size_t count = 0;
		const MRColorSetupItem *items = colorSetupGroupItems(groups[g], count);
		std::vector<unsigned char> values;

		if (items == nullptr || count == 0)
			continue;
		values.assign(count, 0);
		for (std::size_t i = 0; i < count; ++i)
			values[i] = static_cast<unsigned char>(palette[items[i].paletteIndex]);
		if (!setConfiguredColorSetupGroupValues(groups[g], values.data(), values.size(), &errorText))
			return false;
	}

	errorText.clear();
	return true;
}

class TPathsSetupDialog : public TDialog {
  public:
	struct ManagedItem {
		TView *view;
		TRect base;
	};

	class TInlineGlyphButton : public TView {
	  public:
		TInlineGlyphButton(const TRect &bounds, const char *glyph, ushort command)
		    : TView(bounds), glyph_(glyph != nullptr ? glyph : ""), command_(command) {
			options |= ofSelectable;
			options |= ofFirstClick;
			eventMask |= evMouseDown | evKeyDown;
		}

		void draw() override {
			TDrawBuffer b;
			ushort color = getColor((state & sfFocused) != 0 ? 2 : 1);
			int x = 0;

			b.moveChar(0, ' ', color, size.x);
			if (size.x > 1)
				x = (size.x - 1) / 2;
			b.moveStr(static_cast<ushort>(x), glyph_.c_str(), color, size.x - x);
			writeLine(0, 0, size.x, size.y, b);
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

	explicit TPathsSetupDialog(const PathsDialogRecord &initialRecord)
	    : TWindowInit(&TDialog::initFrame),
	      TDialog(centeredSetupDialogRect(kVirtualDialogWidth, kVirtualDialogHeight),
	              "PATHS"),
	      initialRecord_(initialRecord), currentRecord_(initialRecord) {
		contentRect_ = TRect(1, 1, size.x - 1, size.y - 1);
		content_ = createSetupDialogContentGroup(contentRect_);
		if (content_ != nullptr)
			insert(content_);

		int dialogWidth = kVirtualDialogWidth;
		int inputLeft = 2;
		int glyphWidth = 3;
		int glyphRight = dialogWidth - 2;
		int glyphLeft = glyphRight - glyphWidth;
		int inputRight = glyphLeft - 1;
		int doneLeft = dialogWidth / 2 - 17;
		int cancelLeft = doneLeft + 12;
		int helpLeft = cancelLeft + 14;
		int buttonTop = size.y - 3;

		addManaged(new TStaticText(TRect(2, 2, dialogWidth - 2, 3), "Settings macro URI:"),
		           TRect(2, 2, dialogWidth - 2, 3));
		settingsMacroPathField_ = new TInputLine(TRect(inputLeft, 3, inputRight, 4), kPathFieldSize - 1);
		addManaged(settingsMacroPathField_, TRect(inputLeft, 3, inputRight, 4));
		addManaged(new TInlineGlyphButton(TRect(glyphLeft, 3, glyphRight, 4), "🔎",
		                                  cmMrSetupPathsBrowseSettingsUri),
		           TRect(glyphLeft, 3, glyphRight, 4));

		addManaged(new TStaticText(TRect(2, 5, dialogWidth - 2, 6), "Macro path (*.mrmac):"),
		           TRect(2, 5, dialogWidth - 2, 6));
		macroDirectoryPathField_ = new TInputLine(TRect(inputLeft, 6, inputRight, 7), kPathFieldSize - 1);
		addManaged(macroDirectoryPathField_, TRect(inputLeft, 6, inputRight, 7));
		addManaged(new TInlineGlyphButton(TRect(glyphLeft, 6, glyphRight, 7), "🔎",
		                                  cmMrSetupPathsBrowseMacroPath),
		           TRect(glyphLeft, 6, glyphRight, 7));

		addManaged(new TStaticText(TRect(2, 8, dialogWidth - 2, 9), "Help file URI:"),
		           TRect(2, 8, dialogWidth - 2, 9));
		helpFilePathField_ = new TInputLine(TRect(inputLeft, 9, inputRight, 10), kPathFieldSize - 1);
		addManaged(helpFilePathField_, TRect(inputLeft, 9, inputRight, 10));
		addManaged(new TInlineGlyphButton(TRect(glyphLeft, 9, glyphRight, 10), "🔎",
		                                  cmMrSetupPathsBrowseHelpUri),
		           TRect(glyphLeft, 9, glyphRight, 10));

		addManaged(new TStaticText(TRect(2, 11, dialogWidth - 2, 12), "Temporary path:"),
		           TRect(2, 11, dialogWidth - 2, 12));
		tempDirectoryPathField_ = new TInputLine(TRect(inputLeft, 12, inputRight, 13), kPathFieldSize - 1);
		addManaged(tempDirectoryPathField_, TRect(inputLeft, 12, inputRight, 13));
		addManaged(new TInlineGlyphButton(TRect(glyphLeft, 12, glyphRight, 13), "🔎",
		                                  cmMrSetupPathsBrowseTempPath),
		           TRect(glyphLeft, 12, glyphRight, 13));

		addManaged(new TStaticText(TRect(2, 14, dialogWidth - 2, 15), "Shell executable URI:"),
		           TRect(2, 14, dialogWidth - 2, 15));
		shellExecutablePathField_ = new TInputLine(TRect(inputLeft, 15, inputRight, 16), kPathFieldSize - 1);
		addManaged(shellExecutablePathField_, TRect(inputLeft, 15, inputRight, 16));
		addManaged(new TInlineGlyphButton(TRect(glyphLeft, 15, glyphRight, 16), "🔎",
		                                  cmMrSetupPathsBrowseShellUri),
		           TRect(glyphLeft, 15, glyphRight, 16));

		addManaged(new TButton(TRect(doneLeft, buttonTop, doneLeft + 10, buttonTop + 2), "Done", cmOK,
		                       bfDefault),
		           TRect(doneLeft, buttonTop, doneLeft + 10, buttonTop + 2));
		addManaged(new TButton(TRect(cancelLeft, buttonTop, cancelLeft + 12, buttonTop + 2), "Cancel",
		                       cmCancel, bfNormal),
		           TRect(cancelLeft, buttonTop, cancelLeft + 12, buttonTop + 2));
		addManaged(new TButton(TRect(helpLeft, buttonTop, helpLeft + 8, buttonTop + 2), "Help",
		                       cmMrSetupPathsHelp, bfNormal),
		           TRect(helpLeft, buttonTop, helpLeft + 8, buttonTop + 2));

		loadFieldsFromRecord(currentRecord_);
		initScrollIfNeeded();
	}

	ushort run(PathsDialogRecord &outRecord, bool &changed) {
		ushort result = TProgram::deskTop->execView(this);
		saveFieldsToRecord(currentRecord_);
		outRecord = currentRecord_;
		changed = !recordsEqual(initialRecord_, currentRecord_);
		return result;
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evCommand) {
			switch (event.message.command) {
				case cmMrSetupPathsHelp:
					endModal(event.message.command);
					clearEvent(event);
					return;

				case cmMrSetupPathsBrowseSettingsUri:
					browseSettingsMacroUri();
					clearEvent(event);
					return;

				case cmMrSetupPathsBrowseMacroPath:
					browseMacroPath();
					clearEvent(event);
					return;

				case cmMrSetupPathsBrowseHelpUri:
					browseHelpUri();
					clearEvent(event);
					return;

				case cmMrSetupPathsBrowseTempPath:
					browseTempPath();
					clearEvent(event);
					return;

				case cmMrSetupPathsBrowseShellUri:
					browseShellUri();
					clearEvent(event);
					return;

				default:
					break;
			}
		}

		TDialog::handleEvent(event);
		if (event.what == evBroadcast && event.message.command == cmScrollBarChanged &&
		    (event.message.infoPtr == hScrollBar_ || event.message.infoPtr == vScrollBar_)) {
			applyScroll();
			clearEvent(event);
			return;
		}
	}

  private:
	void addManaged(TView *view, const TRect &base) {
		ManagedItem item;

		item.view = view;
		item.base = base;
		managedViews_.push_back(item);
		if (content_ != nullptr) {
			TRect local = base;
			local.move(-contentRect_.a.x, -contentRect_.a.y);
			view->locate(local);
			content_->insert(view);
		} else
			insert(view);
	}

	void applyScroll() {
		int dx = hScrollBar_ != nullptr ? hScrollBar_->value : 0;
		int dy = vScrollBar_ != nullptr ? vScrollBar_->value : 0;

		for (std::size_t i = 0; i < managedViews_.size(); ++i) {
			TRect moved = managedViews_[i].base;
			moved.move(-dx, -dy);
			moved.move(-contentRect_.a.x, -contentRect_.a.y);
			managedViews_[i].view->locate(moved);
		}
		if (content_ != nullptr)
			content_->drawView();
	}

	void initScrollIfNeeded() {
		int virtualContentWidth = std::max(1, kVirtualDialogWidth - 2);
		int virtualContentHeight = std::max(1, kVirtualDialogHeight - 2);
		bool needH = false;
		bool needV = false;

		for (;;) {
			bool prevH = needH;
			bool prevV = needV;
			int viewportWidth = std::max(1, size.x - 2);
			int viewportHeight = std::max(1, size.y - 2);
			needH = virtualContentWidth > viewportWidth;
			needV = virtualContentHeight > viewportHeight;
			if (needH == prevH && needV == prevV)
				break;
		}

		contentRect_ = TRect(1, 1, size.x - 1, size.y - 1);
		if (contentRect_.b.x <= contentRect_.a.x)
			contentRect_.b.x = contentRect_.a.x + 1;
		if (contentRect_.b.y <= contentRect_.a.y)
			contentRect_.b.y = contentRect_.a.y + 1;
		if (content_ != nullptr)
			content_->locate(contentRect_);

		if (needH) {
			TRect hRect(1, size.y - 1, size.x - 1, size.y);
			if (hScrollBar_ == nullptr) {
				hScrollBar_ = new TScrollBar(hRect);
				insert(hScrollBar_);
			} else
				hScrollBar_->locate(hRect);
		}
		if (needV) {
			TRect vRect(size.x - 1, 1, size.x, size.y - 1);
			if (vScrollBar_ == nullptr) {
				vScrollBar_ = new TScrollBar(vRect);
				insert(vScrollBar_);
			} else
				vScrollBar_->locate(vRect);
		}

		if (hScrollBar_ != nullptr) {
			int maxDx = std::max(0, virtualContentWidth - std::max(1, contentRect_.b.x - contentRect_.a.x));
			hScrollBar_->setParams(0, 0, maxDx, std::max(1, (contentRect_.b.x - contentRect_.a.x) / 2), 1);
		}
		if (vScrollBar_ != nullptr) {
			int maxDy = std::max(0, virtualContentHeight - std::max(1, contentRect_.b.y - contentRect_.a.y));
			vScrollBar_->setParams(0, 0, maxDy, std::max(1, (contentRect_.b.y - contentRect_.a.y) / 2), 1);
		}
		applyScroll();
	}

	static void setInputLineValue(TInputLine *inputLine, const char *value) {
		char buffer[kPathFieldSize];

		std::memset(buffer, 0, sizeof(buffer));
		writeRecordField(buffer, sizeof(buffer), readRecordField(value));
		inputLine->setData(buffer);
	}

	static void readInputLineValue(TInputLine *inputLine, char *dest, std::size_t destSize) {
		char buffer[kPathFieldSize];

		std::memset(buffer, 0, sizeof(buffer));
		inputLine->getData(buffer);
		writeRecordField(dest, destSize, readRecordField(buffer));
	}

	void loadFieldsFromRecord(const PathsDialogRecord &record) {
		setInputLineValue(settingsMacroPathField_, record.settingsMacroPath);
		setInputLineValue(macroDirectoryPathField_, record.macroDirectoryPath);
		setInputLineValue(helpFilePathField_, record.helpFilePath);
		setInputLineValue(tempDirectoryPathField_, record.tempDirectoryPath);
		setInputLineValue(shellExecutablePathField_, record.shellExecutablePath);
	}

	void saveFieldsToRecord(PathsDialogRecord &record) {
		readInputLineValue(settingsMacroPathField_, record.settingsMacroPath, sizeof(record.settingsMacroPath));
		readInputLineValue(macroDirectoryPathField_, record.macroDirectoryPath,
		                   sizeof(record.macroDirectoryPath));
		readInputLineValue(helpFilePathField_, record.helpFilePath, sizeof(record.helpFilePath));
		readInputLineValue(tempDirectoryPathField_, record.tempDirectoryPath, sizeof(record.tempDirectoryPath));
		readInputLineValue(shellExecutablePathField_, record.shellExecutablePath,
		                   sizeof(record.shellExecutablePath));
	}

	std::string currentInputValue(TInputLine *inputLine) {
		char buffer[kPathFieldSize];

		std::memset(buffer, 0, sizeof(buffer));
		inputLine->getData(buffer);
		return readRecordField(buffer);
	}

	void setInputValue(TInputLine *inputLine, const std::string &value) {
		char buffer[kPathFieldSize];

		std::memset(buffer, 0, sizeof(buffer));
		writeRecordField(buffer, sizeof(buffer), value);
		inputLine->setData(buffer);
	}

	void browseSettingsMacroUri() {
		std::string selected;
		if (browseUriWithFileDialog("Select settings macro URI", currentInputValue(settingsMacroPathField_),
		                            selected))
			setInputValue(settingsMacroPathField_, selected);
	}

	void browseMacroPath() {
		std::string selected;
		if (browsePathWithDirectoryDialog(currentInputValue(macroDirectoryPathField_), selected))
			setInputValue(macroDirectoryPathField_, selected);
	}

	void browseHelpUri() {
		std::string selected;
		if (browseUriWithFileDialog("Select help file URI", currentInputValue(helpFilePathField_), selected))
			setInputValue(helpFilePathField_, selected);
	}

	void browseTempPath() {
		std::string selected;
		if (browsePathWithDirectoryDialog(currentInputValue(tempDirectoryPathField_), selected))
			setInputValue(tempDirectoryPathField_, selected);
	}

	void browseShellUri() {
		std::string selected;
		if (browseUriWithFileDialog("Select shell executable URI", currentInputValue(shellExecutablePathField_),
		                            selected))
			setInputValue(shellExecutablePathField_, selected);
	}

	PathsDialogRecord initialRecord_;
	PathsDialogRecord currentRecord_;
	TRect contentRect_;
	TGroup *content_ = nullptr;
	std::vector<ManagedItem> managedViews_;
	TScrollBar *hScrollBar_ = nullptr;
	TScrollBar *vScrollBar_ = nullptr;
	static const int kVirtualDialogWidth = 84;
	static const int kVirtualDialogHeight = 23;
	TInputLine *settingsMacroPathField_ = nullptr;
	TInputLine *macroDirectoryPathField_ = nullptr;
	TInputLine *helpFilePathField_ = nullptr;
	TInputLine *tempDirectoryPathField_ = nullptr;
	TInputLine *shellExecutablePathField_ = nullptr;
};

void showPathsHelpDummyDialog() {
	std::vector<std::string> lines;
	lines.push_back("PATHS HELP");
	lines.push_back("");
	lines.push_back("Dummy help screen.");
	lines.push_back("Configure settings URI, macro path, help URI, temp path and shell URI.");
	lines.push_back("Done saves settings.mrmac and reloads silently.");
	lines.push_back("Cancel asks for confirmation when fields were modified.");
	execDialog(createSetupSimplePreviewDialog("PATHS HELP", 74, 16, lines, false));
}

void runPathsSetupDialogFlowLocal() {
	bool running = true;
	std::string errorText;
	PathsDialogRecord workingRecord;

	initPathsDialogRecord(workingRecord);
	while (running) {
		ushort result;
		bool changed = false;
		PathsDialogRecord editedRecord = workingRecord;
		TPathsSetupDialog *dialog = new TPathsSetupDialog(workingRecord);

		if (dialog == nullptr)
			return;
		result = dialog->run(editedRecord, changed);
		TObject::destroy(dialog);
		workingRecord = editedRecord;

		switch (result) {
			case cmMrSetupPathsHelp:
				showPathsHelpDummyDialog();
				break;

			case cmOK:
				if (!saveAndReloadPathsRecord(workingRecord, errorText)) {
					messageBox(mfError | mfOKButton, "Installation / Paths\n\n%s", errorText.c_str());
					break;
				}
				running = false;
				break;

			case cmCancel:
				if (changed) {
					if (messageBox(mfConfirmation | mfYesButton | mfNoButton,
					               "Discard changed path settings?") != cmYes)
						break;
				}
				running = false;
				break;

			default:
				running = false;
				break;
		}
	}
}
} // namespace

void runColorSetupDialogFlowLocal() {
	auto persistSettingsFileOnly = [](std::string &errorText) -> bool {
		MRSetupPaths paths = resolveSetupPathDefaults();
		paths.settingsMacroUri = configuredSettingsMacroFilePath();
		paths.macroPath = defaultMacroDirectoryPath();
		paths.helpUri = configuredHelpFilePath();
		paths.tempPath = configuredTempDirectoryPath();
		paths.shellUri = configuredShellExecutablePath();
		return writeSettingsMacroFile(paths, &errorText);
	};

	bool running = true;
	std::string errorText;

	while (running) {
		TPalette workingPalette = buildColorSetupWorkingPalette();
		ushort result = execDialogRawWithData(createColorSetupDialog(), &workingPalette);
		switch (result) {
			case cmOK:
				if (!applyWorkingColorPaletteToConfigured(workingPalette, errorText)) {
					messageBox(mfError | mfOKButton, "Installation / Color setup\n\n%s", errorText.c_str());
					break;
				}
				TProgram::application->redraw();
				running = false;
				break;

			case cmMrColorLoadTheme: {
				std::string themeUri;

				if (!chooseThemeFileForLoad(themeUri))
					break;
				if (!loadColorThemeFile(themeUri, &errorText)) {
					messageBox(mfError | mfOKButton, "Color Setup / Load Theme\n\n%s", errorText.c_str());
					break;
				}
				if (!persistSettingsFileOnly(errorText)) {
					messageBox(mfError | mfOKButton, "Color Setup / Save settings\n\n%s", errorText.c_str());
					break;
				}
				TProgram::application->redraw();
				break;
			}

			case cmMrColorSaveTheme: {
				std::string themeUri;

				if (!chooseThemeFileForSave(themeUri))
					break;
				if (!writeColorThemeFile(themeUri, &errorText)) {
					messageBox(mfError | mfOKButton, "Color Setup / Save Theme\n\n%s", errorText.c_str());
					break;
				}
				if (!persistSettingsFileOnly(errorText)) {
					messageBox(mfError | mfOKButton, "Color Setup / Save settings\n\n%s", errorText.c_str());
					break;
				}
				TProgram::application->redraw();
				break;
			}

			case cmCancel:
			default:
				running = false;
				break;
		}
	}
}

bool saveCurrentSetupConfiguration(std::string &errorText) {
	TMREditorApp *app = dynamic_cast<TMREditorApp *>(TProgram::application);
	MRSetupPaths paths = resolveSetupPathDefaults();

	if (app == nullptr) {
		errorText = "Application error: TMREditorApp is unavailable.";
		return false;
	}

	paths.settingsMacroUri = configuredSettingsMacroFilePath();
	paths.macroPath = defaultMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();

	if (!writeSettingsMacroFile(paths, &errorText))
		return false;
	if (!app->reloadSettingsMacroFromPath(paths.settingsMacroUri, &errorText))
		return false;

	errorText.clear();
	return true;
}

void runInstallationAndSetupDialogFlow() {
	bool running = true;
	std::string errorText;

	while (running) {
		ushort result = execDialog(createInstallationAndSetupDialog());
		switch (result) {
			case cmMrSetupEditSettings:
				runEditSettingsDialogFlow();
				break;

			case cmMrSetupColorSetup:
				runColorSetupDialogFlowLocal();
				break;

			case cmMrSetupSwappingEmsXms:
				runPathsSetupDialogFlowLocal();
				break;

			case cmMrSetupSearchAndReplaceDefaults:
				messageBox(mfInformation | mfOKButton,
				           "Installation / Search and Replace defaults\n\nDummy implementation for now.");
				break;

			case cmMrSetupSaveConfigurationAndExit:
				if (!saveCurrentSetupConfiguration(errorText)) {
					messageBox(mfError | mfOKButton, "Installation / Save configuration\n\n%s",
					           errorText.c_str());
					break;
				}
				running = false;
				break;

			case cmCancel:
			default:
				running = false;
				break;
		}
	}
}
