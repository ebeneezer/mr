#define Uses_TButton
#define Uses_TChDirDialog
#define Uses_TCollection
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TDrawBuffer
#define Uses_TFileDialog
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TListBox
#define Uses_MsgBox
#define Uses_TObject
#define Uses_TProgram
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TStaticText
#define Uses_TView
#include <tvision/tv.h>

#include "MRCompilerProfiles.hpp"
#include "../app/utils/MRStringUtils.hpp"
#include "../config/settings/MRSettingsCompilerProfiles.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../config/settings/MRSettingsStorage.hpp"
#include "MRDirtyGating.hpp"
#include "setup/MRSetupCommon.hpp"
#include "../ui/MRFrame.hpp"
#include "../ui/widgets/MRDropList.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/MRWindowSupport.hpp"

#include <algorithm>
#include <cstring>
#include <limits.h>
#include <sstream>
#include <string>
#include <vector>

namespace {

enum : ushort {
	cmCompilerProfilesAdd = 3940,
	cmCompilerProfilesCopy,
	cmCompilerProfilesDelete,
	cmCompilerProfilesChooseExecutable,
	cmCompilerProfilesBrowseExecutable,
	cmCompilerProfilesAcceptExecutable,
	cmCompilerProfilesChooseSuccessAudio,
	cmCompilerProfilesBrowseSuccessAudio,
	cmCompilerProfilesAcceptSuccessAudio,
	cmCompilerProfilesChooseFailureAudio,
	cmCompilerProfilesBrowseFailureAudio,
	cmCompilerProfilesAcceptFailureAudio,
	cmCompilerProfilesAutomaticSetup,
	cmCompilerProfilesSelectionChanged
};

enum {
	kDialogWidth = 112,
	kDialogHeight = 29,
	kIdSize = 64,
	kNameSize = 128,
	kToolchainSize = 24,
	kExecutableSize = 256,
	kVersionSize = 160,
	kTargetSize = 128,
	kFlagsSize = 256,
	kBuildCommandSize = 256,
	kPathListSize = 256,
	kAudioUriSize = 256
};

TFrame *initMrDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}

char *dupCString(const std::string &value) {
	char *copy = new char[value.size() + 1];
	std::memcpy(copy, value.c_str(), value.size() + 1);
	return copy;
}

class TPlainStringCollection : public TCollection {
  public:
	TPlainStringCollection(short aLimit, short aDelta) noexcept : TCollection(aLimit, aDelta) {
	}

  protected:
	void freeItem(void *item) override {
		delete[] static_cast<char *>(item);
	}

  private:
	void *readItem(ipstream &) override {
		return nullptr;
	}

	void writeItem(void *, opstream &) override {
	}
};

class TCompilerProfileListBox : public TListBox {
  public:
	TCompilerProfileListBox(const TRect &bounds, TScrollBar *scrollBar) noexcept : TListBox(bounds, 1, scrollBar) {
	}

	void focusItemNum(short item) override {
		short oldFocused = focused;

		TListBox::focusItemNum(item);
		if (focused != oldFocused) dispatchSelectionChanged();
	}

  private:
	void dispatchSelectionChanged() {
		TView *target = owner;

		while (target != nullptr && dynamic_cast<TDialog *>(target) == nullptr)
			target = target->owner;
		message(target != nullptr ? target : owner, evBroadcast, cmCompilerProfilesSelectionChanged, this);
	}
};

class TInlineGlyphButton : public TView {
  public:
	TInlineGlyphButton(const TRect &bounds, const char *glyph, ushort command) : TView(bounds), mGlyph(glyph != nullptr ? glyph : ""), mCommand(command) {
		options |= ofSelectable | ofFirstClick;
		eventMask |= evMouseDown | evKeyDown;
	}

	void draw() override {
		TDrawBuffer buffer;
		ushort color = getColor((state & sfFocused) != 0 ? 2 : 1);
		int x = 0;

		buffer.moveChar(0, ' ', color, size.x);
		if (size.x > 1) x = (size.x - 1) / 2;
		buffer.moveStr(static_cast<ushort>(x), mGlyph.c_str(), color, size.x - x);
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
		message(target != nullptr ? target : owner, evCommand, mCommand, this);
	}

	std::string mGlyph;
	ushort mCommand;
};

std::string readInput(TInputLine *field, std::size_t capacity) {
	std::vector<char> buffer(capacity, '\0');

	if (field == nullptr || capacity == 0) return std::string();
	field->getData(buffer.data());
	return std::string(buffer.data());
}

void writeInput(TInputLine *field, const std::string &value, std::size_t capacity) {
	std::vector<char> buffer(capacity, '\0');
	std::string clipped = value.substr(0, capacity > 0 ? capacity - 1 : 0);

	if (field == nullptr || capacity == 0) return;
	std::memcpy(buffer.data(), clipped.c_str(), clipped.size());
	field->setData(buffer.data());
}

std::string nextCompilerProfileId(const std::vector<MRCompilerProfile> &profiles, const std::string &seed) {
	std::string base = canonicalCompilerProfileId(seed.empty() ? "COMPILER_PROFILE" : seed);

	if (base.empty()) base = "COMPILER_PROFILE";
	for (int suffix = 0;; ++suffix) {
		std::string candidate = suffix == 0 ? base : base + "_" + std::to_string(suffix + 1);
		bool used = false;

		for (const MRCompilerProfile &profile : profiles)
			if (profile.id == candidate) {
				used = true;
				break;
			}
		if (!used) return candidate;
	}
}

std::string profileLabel(const MRCompilerProfile &profile) {
	std::string id = profile.id.empty() ? "<empty>" : profile.id;
	std::string name = profile.name.empty() ? "<unnamed>" : profile.name;

	if (id.size() < 14) id.append(14 - id.size(), ' ');
	return id + "  " + name;
}

std::vector<MRCompilerProfile> initialCompilerProfilesForDialog() {
	return configuredCompilerProfiles();
}

bool compilerProfileListsEqual(const std::vector<MRCompilerProfile> &lhs, const std::vector<MRCompilerProfile> &rhs) {
	return lhs == rhs;
}

std::vector<std::string> dirtyCompilerProfileIds(const std::vector<MRCompilerProfile> &initialProfiles, const std::vector<MRCompilerProfile> &profiles) {
	std::vector<std::string> out;
	const std::size_t count = std::max(initialProfiles.size(), profiles.size());

	for (std::size_t i = 0; i < count; ++i) {
		const MRCompilerProfile *initial = i < initialProfiles.size() ? &initialProfiles[i] : nullptr;
		const MRCompilerProfile *current = i < profiles.size() ? &profiles[i] : nullptr;
		if (initial != nullptr && current != nullptr && *initial == *current) continue;
		std::string id;
		if (current != nullptr) id = canonicalCompilerProfileId(current->id);
		if (id.empty() && initial != nullptr) id = canonicalCompilerProfileId(initial->id);
		if (id.empty()) id = "<empty>";
		out.push_back(id);
	}
	return out;
}

bool saveCompilerProfiles(const std::vector<MRCompilerProfile> &profiles, std::string &errorText) {
	MRSettingsWriteReport writeReport;

	if (!setConfiguredCompilerProfiles(profiles, &errorText)) return false;
	if (!persistConfiguredSettingsSnapshot(&errorText, &writeReport)) return false;
	mrLogSettingsWriteReport("compiler profiles", writeReport);
	errorText.clear();
	return true;
}

class CompilerProfilesDialog : public MRDialogFoundation {
  public:
	explicit CompilerProfilesDialog(const std::vector<MRCompilerProfile> &initialProfiles)
	    : TWindowInit(initMrDialogFrame), MRDialogFoundation(centeredSetupDialogRect(kDialogWidth, kDialogHeight), "COMPILER PROFILES", kDialogWidth, kDialogHeight, initMrDialogFrame), profiles(initialProfiles) {
		const int left = 3;
		const int listRight = 37;
		const int scrollRight = 38;
		const int labelLeft = 39;
		const int fieldLeft = 63;
		const int right = 109;
		const int browseLeft = right - 2;
		const int historyLeft = browseLeft - 2;
		const int fieldWithButtonsRight = historyLeft;
		const int wideFieldRight = browseLeft;
		const int buttonTop = 24;
		const int bottomTop = 26;

		options |= ofCentered;

		insert(new TStaticText(TRect(left, 2, left + 18, 3), "Profiles:"));
		scrollBar = new TScrollBar(TRect(listRight, 3, scrollRight, 23));
		insert(scrollBar);
		list = new TCompilerProfileListBox(TRect(left, 3, listRight, 23), scrollBar);
		insert(list);

		insert(new TStaticText(TRect(labelLeft, 2, fieldLeft - 1, 3), "Profile ID:"));
		idField = addField(TRect(fieldLeft, 2, wideFieldRight, 3), kIdSize - 1);
		insert(new TStaticText(TRect(labelLeft, 3, fieldLeft - 1, 4), "Name:"));
		nameField = addField(TRect(fieldLeft, 3, wideFieldRight, 4), kNameSize - 1);
		insert(new TStaticText(TRect(labelLeft, 4, fieldLeft - 1, 5), "Toolchain:"));
		toolchainField = addField(TRect(fieldLeft, 4, wideFieldRight, 5), kToolchainSize - 1);
		insert(new TStaticText(TRect(labelLeft, 5, fieldLeft - 1, 6), "Executable:"));
		executableField = addField(TRect(fieldLeft, 5, fieldWithButtonsRight, 6), kExecutableSize - 1);
		executableHistoryButton = executableDropList.createButton(*this, TRect(historyLeft, 5, browseLeft, 6), executableField, this, cmCompilerProfilesChooseExecutable, true);
		executableBrowseButton = new TInlineGlyphButton(TRect(browseLeft, 5, right, 6), "🔎", cmCompilerProfilesBrowseExecutable);
		insert(executableBrowseButton);
		executableListAnchor = TRect(fieldLeft, 6, right, 7);
		insert(new TStaticText(TRect(labelLeft, 6, fieldLeft - 1, 7), "Version:"));
		versionField = addField(TRect(fieldLeft, 6, wideFieldRight, 7), kVersionSize - 1);
		insert(new TStaticText(TRect(labelLeft, 7, fieldLeft - 1, 8), "Target:"));
		targetField = addField(TRect(fieldLeft, 7, wideFieldRight, 8), kTargetSize - 1);
		insert(new TStaticText(TRect(labelLeft, 8, fieldLeft - 1, 9), "Build flags:"));
		flagsField = addField(TRect(fieldLeft, 8, wideFieldRight, 9), kFlagsSize - 1);
		insert(new TStaticText(TRect(labelLeft, 9, fieldLeft - 1, 10), "Pre build cmd:"));
		preBuildCommandField = addField(TRect(fieldLeft, 9, wideFieldRight, 10), kBuildCommandSize - 1);
		insert(new TStaticText(TRect(labelLeft, 10, fieldLeft - 1, 11), "Build succeeded cmd:"));
		buildSucceededCommandField = addField(TRect(fieldLeft, 10, wideFieldRight, 11), kBuildCommandSize - 1);
		insert(new TStaticText(TRect(labelLeft, 11, fieldLeft - 1, 12), "Build succeeded audio:"));
		successAudioField = addField(TRect(fieldLeft, 11, fieldWithButtonsRight, 12), kAudioUriSize - 1);
		successAudioHistoryButton = successAudioDropList.createButton(*this, TRect(historyLeft, 11, browseLeft, 12), successAudioField, this, cmCompilerProfilesChooseSuccessAudio, true);
		successAudioBrowseButton = new TInlineGlyphButton(TRect(browseLeft, 11, right, 12), "🔎", cmCompilerProfilesBrowseSuccessAudio);
		insert(successAudioBrowseButton);
		successAudioListAnchor = TRect(fieldLeft, 12, right, 13);
		insert(new TStaticText(TRect(labelLeft, 12, fieldLeft - 1, 13), "Build failed cmd:"));
		buildFailedCommandField = addField(TRect(fieldLeft, 12, wideFieldRight, 13), kBuildCommandSize - 1);
		insert(new TStaticText(TRect(labelLeft, 13, fieldLeft - 1, 14), "Build failed audio:"));
		failureAudioField = addField(TRect(fieldLeft, 13, fieldWithButtonsRight, 14), kAudioUriSize - 1);
		failureAudioHistoryButton = failureAudioDropList.createButton(*this, TRect(historyLeft, 13, browseLeft, 14), failureAudioField, this, cmCompilerProfilesChooseFailureAudio, true);
		failureAudioBrowseButton = new TInlineGlyphButton(TRect(browseLeft, 13, right, 14), "🔎", cmCompilerProfilesBrowseFailureAudio);
		insert(failureAudioBrowseButton);
		failureAudioListAnchor = TRect(fieldLeft, 14, right, 15);
		insert(new TStaticText(TRect(labelLeft, 14, fieldLeft - 1, 15), "Includes:"));
		includesField = addField(TRect(fieldLeft, 14, wideFieldRight, 15), kPathListSize - 1);
		insert(new TStaticText(TRect(labelLeft, 15, fieldLeft - 1, 16), "Libraries:"));
		librariesField = addField(TRect(fieldLeft, 15, wideFieldRight, 16), kPathListSize - 1);
		insert(new TStaticText(TRect(labelLeft, 16, fieldLeft - 1, 17), "Runtime:"));
		runtimeField = addField(TRect(fieldLeft, 16, wideFieldRight, 17), kPathListSize - 1);
		insert(new TButton(TRect(left, buttonTop, left + 10, buttonTop + 2), "~A~dd", cmCompilerProfilesAdd, bfNormal));
		insert(new TButton(TRect(left + 12, buttonTop, left + 22, buttonTop + 2), "~C~opy", cmCompilerProfilesCopy, bfNormal));
		insert(new TButton(TRect(left + 24, buttonTop, scrollRight, buttonTop + 2), "De~l~ete", cmCompilerProfilesDelete, bfNormal));
		insert(new TButton(TRect(46, bottomTop, 65, bottomTop + 2), "Automatic Setup", cmCompilerProfilesAutomaticSetup, bfNormal));

		currentIndex = profiles.empty() ? -1 : 0;
		refreshList();
		loadCurrentProfile();
		finalizeLayout();
		if (list != nullptr) list->select();
	}

	ushort run(std::vector<MRCompilerProfile> &outProfiles) {
		ushort result = TProgram::deskTop != nullptr ? TProgram::deskTop->execView(this) : cmCancel;

		saveCurrentProfile();
		outProfiles = profiles;
		return result;
	}

	void handleEvent(TEvent &event) override {
		ushort originalWhat = event.what;
		ushort originalCommand = event.what == evCommand ? event.message.command : 0;
		ushort originalBroadcast = event.what == evBroadcast ? event.message.command : 0;

		if (executableDropList.handleOpenListEvent(event)) return;
		if (successAudioDropList.handleOpenListEvent(event)) return;
		if (failureAudioDropList.handleOpenListEvent(event)) return;
		if (originalWhat == evCommand && originalCommand == cmOK) {
			saveCurrentProfile();
			if (!saveProfiles()) {
				clearEvent(event);
				return;
			}
		}
		MRDialogFoundation::handleEvent(event);
		if (originalWhat == evBroadcast && originalBroadcast == cmCompilerProfilesSelectionChanged) {
			changeSelection(selectedIndex());
			clearEvent(event);
			return;
		}
		if (originalWhat == evCommand) {
			switch (originalCommand) {
				case cmCompilerProfilesAdd:
					addProfile();
					clearEvent(event);
					return;
				case cmCompilerProfilesCopy:
					copyProfile();
					clearEvent(event);
					return;
				case cmCompilerProfilesDelete:
					deleteProfile();
					clearEvent(event);
					return;
				case cmCompilerProfilesChooseExecutable:
					executableDropList.toggle(*this, executableListAnchor, executableChoices(), readInput(executableField, kExecutableSize), this, cmCompilerProfilesAcceptExecutable, 8);
					clearEvent(event);
					return;
				case cmCompilerProfilesBrowseExecutable:
					browseExecutable();
					clearEvent(event);
					return;
				case cmCompilerProfilesAcceptExecutable:
					acceptExecutableSelection();
					clearEvent(event);
					return;
				case cmCompilerProfilesChooseSuccessAudio:
					successAudioDropList.toggle(*this, successAudioListAnchor, audioUriChoices(), readInput(successAudioField, kAudioUriSize), this, cmCompilerProfilesAcceptSuccessAudio, 8);
					clearEvent(event);
					return;
				case cmCompilerProfilesBrowseSuccessAudio:
					browseAudioUri(successAudioField);
					clearEvent(event);
					return;
				case cmCompilerProfilesAcceptSuccessAudio:
					acceptAudioUriSelection(successAudioDropList, successAudioField);
					clearEvent(event);
					return;
				case cmCompilerProfilesChooseFailureAudio:
					failureAudioDropList.toggle(*this, failureAudioListAnchor, audioUriChoices(), readInput(failureAudioField, kAudioUriSize), this, cmCompilerProfilesAcceptFailureAudio, 8);
					clearEvent(event);
					return;
				case cmCompilerProfilesBrowseFailureAudio:
					browseAudioUri(failureAudioField);
					clearEvent(event);
					return;
				case cmCompilerProfilesAcceptFailureAudio:
					acceptAudioUriSelection(failureAudioDropList, failureAudioField);
					clearEvent(event);
					return;
				case cmCompilerProfilesAutomaticSetup:
					automaticSetup();
					clearEvent(event);
					return;
				default:
					break;
			}
		}
	}

  private:
	TInputLine *addField(const TRect &bounds, int maxLen) {
		TInputLine *field = new TInputLine(bounds, maxLen);
		insert(field);
		return field;
	}

	std::vector<std::string> executableChoices() const {
		std::vector<std::string> choices;

		for (const MRCompilerProfile &profile : profiles) {
			const std::string path = normalizeConfiguredPathInput(profile.executablePath);
			if (!path.empty() && std::find(choices.begin(), choices.end(), path) == choices.end()) choices.push_back(path);
		}
		for (const std::string &pathValue : detectedCompilerExecutablePaths()) {
			const std::string path = normalizeConfiguredPathInput(pathValue);
			if (!path.empty() && std::find(choices.begin(), choices.end(), path) == choices.end()) choices.push_back(path);
		}
		std::sort(choices.begin(), choices.end());
		return choices;
	}

	std::vector<std::string> audioUriChoices() const {
		std::vector<std::string> choices;

		for (const MRCompilerProfile &profile : profiles) {
			const std::string successPath = normalizeConfiguredPathInput(profile.buildSuccessAudioUri);
			const std::string failurePath = normalizeConfiguredPathInput(profile.buildFailureAudioUri);

			if (!successPath.empty() && std::find(choices.begin(), choices.end(), successPath) == choices.end()) choices.push_back(successPath);
			if (!failurePath.empty() && std::find(choices.begin(), choices.end(), failurePath) == choices.end()) choices.push_back(failurePath);
		}
		std::sort(choices.begin(), choices.end());
		return choices;
	}
	void acceptExecutableSelection() {
		std::string selectedValue;

		if (!executableDropList.acceptSelection(selectedValue)) return;
		writeInput(executableField, selectedValue, kExecutableSize);
		saveCurrentProfile();
	}
	void acceptAudioUriSelection(MRDropList &dropList, TInputLine *field) {
		std::string selectedValue;

		if (!dropList.acceptSelection(selectedValue)) return;
		writeInput(field, selectedValue, kAudioUriSize);
		saveCurrentProfile();
	}

	void browseExecutable() {
		char fileName[MAXPATH] = {0};
		const std::string currentPath = normalizeConfiguredPathInput(readInput(executableField, kExecutableSize));
		ushort result = cmCancel;

		if (!currentPath.empty()) {
			const std::size_t slashPos = currentPath.find_last_of('/');

			if (slashPos != std::string::npos) {
				std::string seed = currentPath.substr(0, slashPos + 1);
				seed += "*.*";
				mr::dialogs::writeRecordField(fileName, sizeof(fileName), seed);
			} else
				mr::dialogs::writeRecordField(fileName, sizeof(fileName), currentPath);
		} else
			mr::dialogs::seedFileDialogPath(MRDialogHistoryScope::General, fileName, sizeof(fileName), "*.*");
		result = mr::dialogs::execRememberingFileDialogWithData(MRDialogHistoryScope::General, "*.*", "SELECT COMPILER EXECUTABLE", "~N~ame", fdOpenButton, fileName);
		if (result == cmCancel) return;
		writeInput(executableField, normalizeConfiguredPathInput(fileName), kExecutableSize);
		saveCurrentProfile();
	}
	void browseAudioUri(TInputLine *field) {
		char fileName[MAXPATH] = {0};
		const std::string currentPath = normalizeConfiguredPathInput(readInput(field, kAudioUriSize));
		ushort result = cmCancel;

		if (field == nullptr) return;
		if (!currentPath.empty()) {
			const std::size_t slashPos = currentPath.find_last_of('/');

			if (slashPos != std::string::npos) {
				std::string seed = currentPath.substr(0, slashPos + 1);
				seed += "*.*";
				mr::dialogs::writeRecordField(fileName, sizeof(fileName), seed);
			} else
				mr::dialogs::writeRecordField(fileName, sizeof(fileName), currentPath);
		} else
			mr::dialogs::seedFileDialogPath(MRDialogHistoryScope::General, fileName, sizeof(fileName), "*.*");
		result = mr::dialogs::execRememberingFileDialogWithData(MRDialogHistoryScope::General, "*.*", "SELECT AUDIO FILE", "~N~ame", fdOpenButton, fileName);
		if (result == cmCancel) return;
		writeInput(field, normalizeConfiguredPathInput(fileName), kAudioUriSize);
		saveCurrentProfile();
	}

	void refreshList() {
		TPlainStringCollection *items = new TPlainStringCollection(std::max<short>(1, static_cast<short>(profiles.size())), 5);
		TListBoxRec data;

		if (items == nullptr || list == nullptr) return;
		for (const MRCompilerProfile &profile : profiles)
			items->insert(dupCString(profileLabel(profile)));
		data.items = items;
		data.selection = static_cast<ushort>(std::max(0, currentIndex));
		list->setData(&data);
	}

	int selectedIndex() const {
		TListBoxRec data;

		if (list == nullptr || profiles.empty()) return -1;
		list->getData(&data);
		if (data.selection >= profiles.size()) return static_cast<int>(profiles.size()) - 1;
		return static_cast<int>(data.selection);
	}

	void loadCurrentProfile() {
		if (currentIndex < 0 || currentIndex >= static_cast<int>(profiles.size())) return;
		const MRCompilerProfile &profile = profiles[currentIndex];

		writeInput(idField, profile.id, kIdSize);
		writeInput(nameField, profile.name, kNameSize);
		writeInput(toolchainField, profile.toolchain, kToolchainSize);
		writeInput(executableField, profile.executablePath, kExecutableSize);
		writeInput(versionField, profile.versionText, kVersionSize);
		writeInput(targetField, profile.targetTriple, kTargetSize);
		writeInput(flagsField, profile.buildFlags, kFlagsSize);
		writeInput(preBuildCommandField, profile.preBuildCommand, kBuildCommandSize);
		writeInput(buildSucceededCommandField, profile.buildSucceededCommand, kBuildCommandSize);
		writeInput(buildFailedCommandField, profile.buildFailedCommand, kBuildCommandSize);
		writeInput(includesField, normalizeCompilerProfilePathList(profile.includePaths), kPathListSize);
		writeInput(librariesField, normalizeCompilerProfilePathList(profile.libraryPaths), kPathListSize);
		writeInput(runtimeField, normalizeCompilerProfilePathList(profile.runtimePaths), kPathListSize);
		writeInput(successAudioField, profile.buildSuccessAudioUri, kAudioUriSize);
		writeInput(failureAudioField, profile.buildFailureAudioUri, kAudioUriSize);
	}

	void saveCurrentProfile() {
		if (currentIndex < 0 || currentIndex >= static_cast<int>(profiles.size())) return;
		MRCompilerProfile &profile = profiles[currentIndex];

		profile.id = readInput(idField, kIdSize);
		profile.name = readInput(nameField, kNameSize);
		profile.toolchain = readInput(toolchainField, kToolchainSize);
		profile.executablePath = readInput(executableField, kExecutableSize);
		profile.versionText = readInput(versionField, kVersionSize);
		profile.targetTriple = readInput(targetField, kTargetSize);
		profile.buildFlags = readInput(flagsField, kFlagsSize);
		profile.preBuildCommand = readInput(preBuildCommandField, kBuildCommandSize);
		profile.buildSucceededCommand = readInput(buildSucceededCommandField, kBuildCommandSize);
		profile.buildFailedCommand = readInput(buildFailedCommandField, kBuildCommandSize);
		profile.includePaths = splitCompilerProfilePathList(readInput(includesField, kPathListSize));
		profile.libraryPaths = splitCompilerProfilePathList(readInput(librariesField, kPathListSize));
		profile.runtimePaths = splitCompilerProfilePathList(readInput(runtimeField, kPathListSize));
		profile.buildSuccessAudioUri = readInput(successAudioField, kAudioUriSize);
		profile.buildFailureAudioUri = readInput(failureAudioField, kAudioUriSize);
	}

	void changeSelection(int index) {
		if (index == currentIndex || index < 0 || index >= static_cast<int>(profiles.size())) return;
		executableDropList.hide();
		successAudioDropList.hide();
		failureAudioDropList.hide();
		saveCurrentProfile();
		currentIndex = index;
		loadCurrentProfile();
		refreshList();
	}

	void addProfile() {
		MRCompilerProfile profile;

		saveCurrentProfile();
		profile.id = nextCompilerProfileId(profiles, "compiler_profile");
		profile.name = "New compiler profile";
		profile.toolchain = "CUSTOM";
		profiles.push_back(profile);
		currentIndex = static_cast<int>(profiles.size()) - 1;
		refreshList();
		loadCurrentProfile();
		if (idField != nullptr) idField->select();
	}

	void copyProfile() {
		if (currentIndex < 0 || currentIndex >= static_cast<int>(profiles.size())) return;
		saveCurrentProfile();
		MRCompilerProfile profile = profiles[currentIndex];
		profile.id = nextCompilerProfileId(profiles, profile.id + "_copy");
		profile.name += " copy";
		profiles.push_back(profile);
		currentIndex = static_cast<int>(profiles.size()) - 1;
		refreshList();
		loadCurrentProfile();
		if (idField != nullptr) idField->select();
	}

	void deleteProfile() {
		if (currentIndex < 0 || currentIndex >= static_cast<int>(profiles.size())) return;
		if (messageBox(mfConfirmation | mfYesButton | mfNoButton, "Delete compiler profile:\n%s", profiles[currentIndex].name.c_str()) != cmYes) return;
		profiles.erase(profiles.begin() + currentIndex);
		if (currentIndex >= static_cast<int>(profiles.size())) currentIndex = static_cast<int>(profiles.size()) - 1;
		refreshList();
		loadCurrentProfile();
	}

	void automaticSetup() {
		std::string errorText;

		if (currentIndex < 0 || currentIndex >= static_cast<int>(profiles.size())) {
			insertDetectedCompilerProfiles();
			return;
		}
		saveCurrentProfile();
		if (trimAscii(profiles[currentIndex].executablePath).empty()) {
			insertDetectedCompilerProfiles();
			return;
		}
		if (!autoConfigureCompilerProfileFromExecutable(profiles[currentIndex], &errorText)) {
			mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
			loadCurrentProfile();
			return;
		}
		loadCurrentProfile();
		refreshList();
	}

	void insertDetectedCompilerProfiles() {
		const std::vector<MRCompilerProfile> detectedProfiles = detectedCompilerProfiles();
		int firstDetectedIndex = -1;
		int firstAddedIndex = -1;

		for (const MRCompilerProfile &detectedProfile : detectedProfiles) {
			const std::string detectedId = canonicalCompilerProfileId(detectedProfile.id);
			int existingIndex = -1;

			for (std::size_t i = 0; i < profiles.size(); ++i) {
				if (canonicalCompilerProfileId(profiles[i].id) == detectedId) {
					existingIndex = static_cast<int>(i);
					break;
				}
			}
			if (existingIndex >= 0) {
				if (firstDetectedIndex < 0) firstDetectedIndex = existingIndex;
				continue;
			}
			profiles.push_back(detectedProfile);
			if (firstAddedIndex < 0) firstAddedIndex = static_cast<int>(profiles.size()) - 1;
		}
		if (firstAddedIndex >= 0) {
			currentIndex = firstAddedIndex;
			refreshList();
			loadCurrentProfile();
			mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Detected compiler profiles inserted.", mr::messageline::Kind::Success, mr::messageline::kPriorityMedium);
			return;
		}
		if (firstDetectedIndex >= 0) {
			currentIndex = firstDetectedIndex;
			refreshList();
			loadCurrentProfile();
			mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Detected compiler profile selected.", mr::messageline::Kind::Info, mr::messageline::kPriorityMedium);
			return;
		}
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "No compiler executables detected.", mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
	}

	bool saveProfiles() {
		std::string errorText;

		if (!saveCompilerProfiles(profiles, errorText)) {
			mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
			return false;
		}
		return true;
	}

	std::vector<MRCompilerProfile> profiles;
	int currentIndex = -1;
	TCompilerProfileListBox *list = nullptr;
	TScrollBar *scrollBar = nullptr;
	MRDropList executableDropList;
	MRDropList successAudioDropList;
	MRDropList failureAudioDropList;
	TRect executableListAnchor;
	TRect successAudioListAnchor;
	TRect failureAudioListAnchor;
	TInputLine *idField = nullptr;
	TInputLine *nameField = nullptr;
	TInputLine *toolchainField = nullptr;
	TInputLine *executableField = nullptr;
	TView *executableHistoryButton = nullptr;
	TInlineGlyphButton *executableBrowseButton = nullptr;
	TInputLine *versionField = nullptr;
	TInputLine *targetField = nullptr;
	TInputLine *flagsField = nullptr;
	TInputLine *preBuildCommandField = nullptr;
	TInputLine *buildSucceededCommandField = nullptr;
	TInputLine *buildFailedCommandField = nullptr;
	TInputLine *includesField = nullptr;
	TInputLine *librariesField = nullptr;
	TInputLine *runtimeField = nullptr;
	TInputLine *successAudioField = nullptr;
	TInputLine *failureAudioField = nullptr;
	TView *successAudioHistoryButton = nullptr;
	TInlineGlyphButton *successAudioBrowseButton = nullptr;
	TView *failureAudioHistoryButton = nullptr;
	TInlineGlyphButton *failureAudioBrowseButton = nullptr;
};

} // namespace

void runCompilerProfilesDialogFlow() {
	std::vector<MRCompilerProfile> baselineProfiles = initialCompilerProfilesForDialog();
	std::vector<MRCompilerProfile> workingProfiles = baselineProfiles;
	bool running = true;

	while (running) {
		CompilerProfilesDialog *dialog = new CompilerProfilesDialog(workingProfiles);
		std::vector<MRCompilerProfile> editedProfiles = workingProfiles;
		std::string errorText;
		ushort result = cmCancel;

		if (dialog == nullptr) return;
		result = dialog->run(editedProfiles);
		TObject::destroy(dialog);

		const bool changed = mr::dialogs::isDialogDraftDirty(baselineProfiles, editedProfiles, compilerProfileListsEqual);
		switch (result) {
			case cmOK:
				baselineProfiles = editedProfiles;
				workingProfiles = editedProfiles;
				running = false;
				break;

			case cmClose:
			case cmCancel:
				if (changed) {
					std::vector<std::string> dirtyIds = dirtyCompilerProfileIds(baselineProfiles, editedProfiles);
					mr::dialogs::UnsavedChangesChoice choice = mr::dialogs::runDialogDirtyListGating("UNSAVED COMPILER PROFILES", "Discard changed compiler profiles?", "Dirty profile IDs:", dirtyIds, "~S~ave All");

					if (choice == mr::dialogs::UnsavedChangesChoice::Save) {
						if (!saveCompilerProfiles(editedProfiles, errorText)) {
							mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
							workingProfiles = editedProfiles;
							break;
						}
						baselineProfiles = editedProfiles;
						workingProfiles = editedProfiles;
						running = false;
						break;
					}
					if (choice == mr::dialogs::UnsavedChangesChoice::Cancel) {
						workingProfiles = editedProfiles;
						break;
					}
				}
				running = false;
				break;

			default:
				running = false;
				break;
		}
	}
}
