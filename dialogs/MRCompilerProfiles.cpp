#define Uses_TButton
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
#include "../ui/MRDropList.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/MRWindowSupport.hpp"

#include <algorithm>
#include <cstring>
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
	cmCompilerProfilesAutomaticSetup,
	cmCompilerProfilesSelectionChanged
};

enum {
	kDialogWidth = 112,
	kDialogHeight = 26,
	kIdSize = 64,
	kNameSize = 128,
	kToolchainSize = 24,
	kExecutableSize = 256,
	kVersionSize = 160,
	kTargetSize = 128,
	kFlagsSize = 256,
	kPathListSize = 256
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

void appendMissingDefaultProfiles(std::vector<MRCompilerProfile> &profiles) {
	for (const MRCompilerProfile &defaultProfile : defaultCompilerProfiles()) {
		const std::string defaultId = canonicalCompilerProfileId(defaultProfile.id);
		bool exists = false;

		if (defaultId.empty()) continue;
		for (const MRCompilerProfile &profile : profiles)
			if (canonicalCompilerProfileId(profile.id) == defaultId) {
				exists = true;
				break;
			}
		if (!exists) profiles.push_back(defaultProfile);
	}
}

std::vector<MRCompilerProfile> initialCompilerProfilesForDialog() {
	std::vector<MRCompilerProfile> profiles = configuredCompilerProfiles();

	appendMissingDefaultProfiles(profiles);
	return profiles;
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
	    : TWindowInit(initMrDialogFrame), MRDialogFoundation(centeredSetupDialogRect(kDialogWidth, kDialogHeight), "Compiler profiles", kDialogWidth, kDialogHeight, initMrDialogFrame), profiles(initialProfiles) {
		const int left = 3;
		const int listRight = 37;
		const int scrollRight = 38;
		const int labelLeft = 41;
		const int fieldLeft = 55;
		const int right = 109;
		const int browseLeft = right - 2;
		const int historyLeft = browseLeft - 2;
		const int fieldWithButtonsRight = historyLeft;
		const int buttonTop = 21;
		const int bottomTop = 23;

		options |= ofCentered;

		insert(new TStaticText(TRect(left, 2, left + 18, 3), "Profiles:"));
		scrollBar = new TScrollBar(TRect(listRight, 3, scrollRight, 20));
		insert(scrollBar);
		list = new TCompilerProfileListBox(TRect(left, 3, listRight, 20), scrollBar);
		insert(list);

		insert(new TStaticText(TRect(labelLeft, 2, fieldLeft - 1, 3), "Profile ID:"));
		idField = addField(TRect(fieldLeft, 2, right, 3), kIdSize - 1);
		insert(new TStaticText(TRect(labelLeft, 4, fieldLeft - 1, 5), "Name:"));
		nameField = addField(TRect(fieldLeft, 4, right, 5), kNameSize - 1);
		insert(new TStaticText(TRect(labelLeft, 6, fieldLeft - 1, 7), "Toolchain:"));
		toolchainField = addField(TRect(fieldLeft, 6, 75, 7), kToolchainSize - 1);
		insert(new TStaticText(TRect(labelLeft, 8, fieldLeft - 1, 9), "Executable:"));
		executableField = addField(TRect(fieldLeft, 8, fieldWithButtonsRight, 9), kExecutableSize - 1);
		executableHistoryButton = executableDropList.createButton(*this, TRect(historyLeft, 8, browseLeft, 9), executableField, this, cmCompilerProfilesChooseExecutable, true);
		executableBrowseButton = new TInlineGlyphButton(TRect(browseLeft, 8, right, 9), "🔎", cmCompilerProfilesBrowseExecutable);
		insert(executableBrowseButton);
		executableListAnchor = TRect(fieldLeft, 9, right, 10);
		insert(new TStaticText(TRect(labelLeft, 10, fieldLeft - 1, 11), "Version:"));
		versionField = addField(TRect(fieldLeft, 10, right, 11), kVersionSize - 1);
		insert(new TStaticText(TRect(labelLeft, 12, fieldLeft - 1, 13), "Target:"));
		targetField = addField(TRect(fieldLeft, 12, right, 13), kTargetSize - 1);
		insert(new TStaticText(TRect(labelLeft, 14, fieldLeft - 1, 15), "Build flags:"));
		flagsField = addField(TRect(fieldLeft, 14, right, 15), kFlagsSize - 1);
		insert(new TStaticText(TRect(labelLeft, 16, fieldLeft - 1, 17), "Includes:"));
		includesField = addField(TRect(fieldLeft, 16, right, 17), kPathListSize - 1);
		insert(new TStaticText(TRect(labelLeft, 18, fieldLeft - 1, 19), "Libraries:"));
		librariesField = addField(TRect(fieldLeft, 18, right, 19), kPathListSize - 1);
		insert(new TStaticText(TRect(labelLeft, 20, fieldLeft - 1, 21), "Runtime:"));
		runtimeField = addField(TRect(fieldLeft, 20, right, 21), kPathListSize - 1);

		insert(new TButton(TRect(left, buttonTop, left + 10, buttonTop + 2), "~A~dd", cmCompilerProfilesAdd, bfNormal));
		insert(new TButton(TRect(left + 12, buttonTop, left + 22, buttonTop + 2), "~C~opy", cmCompilerProfilesCopy, bfNormal));
		insert(new TButton(TRect(left + 24, buttonTop, scrollRight, buttonTop + 2), "De~l~ete", cmCompilerProfilesDelete, bfNormal));
		insert(new TButton(TRect(51, bottomTop, 61, bottomTop + 2), "~D~one", cmOK, bfDefault));
		insert(new TButton(TRect(90, bottomTop, right, bottomTop + 2), "Automatic Setup", cmCompilerProfilesAutomaticSetup, bfNormal));

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

		if (executableDropList.handleLinkedInputEvent(event, *this, executableListAnchor, executableChoices(), executableField, this, cmCompilerProfilesAcceptExecutable, 8)) return;
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
		for (const std::string &pathValue : defaultCompilerExecutablePaths()) {
			const std::string path = normalizeConfiguredPathInput(pathValue);
			if (!path.empty() && std::find(choices.begin(), choices.end(), path) == choices.end()) choices.push_back(path);
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

	void browseExecutable() {
		char fileName[MAXPATH] = {0};
		const std::string currentPath = normalizeConfiguredPathInput(readInput(executableField, kExecutableSize));
		ushort result = cmCancel;

		mr::dialogs::seedFileDialogPath(MRDialogHistoryScope::General, fileName, sizeof(fileName), "*.*");
		if (!currentPath.empty()) mr::dialogs::suggestFileDialogName(fileName, sizeof(fileName), currentPath);
		result = mr::dialogs::execRememberingFileDialogWithData(MRDialogHistoryScope::General, "*.*", "Select compiler executable", "~N~ame", fdOpenButton, fileName);
		if (result == cmCancel) return;
		writeInput(executableField, normalizeConfiguredPathInput(fileName), kExecutableSize);
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
		writeInput(includesField, normalizeCompilerProfilePathList(profile.includePaths), kPathListSize);
		writeInput(librariesField, normalizeCompilerProfilePathList(profile.libraryPaths), kPathListSize);
		writeInput(runtimeField, normalizeCompilerProfilePathList(profile.runtimePaths), kPathListSize);
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
		profile.includePaths = splitCompilerProfilePathList(readInput(includesField, kPathListSize));
		profile.libraryPaths = splitCompilerProfilePathList(readInput(librariesField, kPathListSize));
		profile.runtimePaths = splitCompilerProfilePathList(readInput(runtimeField, kPathListSize));
	}

	void changeSelection(int index) {
		if (index == currentIndex || index < 0 || index >= static_cast<int>(profiles.size())) return;
		executableDropList.hide();
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
		profile.id = nextCompilerProfileId(profiles, profile.id + "copy");
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

		if (currentIndex < 0 || currentIndex >= static_cast<int>(profiles.size())) return;
		saveCurrentProfile();
		if (!autoConfigureCompilerProfileFromExecutable(profiles[currentIndex], &errorText)) {
			mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
			loadCurrentProfile();
			return;
		}
		loadCurrentProfile();
		refreshList();
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
	TRect executableListAnchor;
	TInputLine *idField = nullptr;
	TInputLine *nameField = nullptr;
	TInputLine *toolchainField = nullptr;
	TInputLine *executableField = nullptr;
	TView *executableHistoryButton = nullptr;
	TInlineGlyphButton *executableBrowseButton = nullptr;
	TInputLine *versionField = nullptr;
	TInputLine *targetField = nullptr;
	TInputLine *flagsField = nullptr;
	TInputLine *includesField = nullptr;
	TInputLine *librariesField = nullptr;
	TInputLine *runtimeField = nullptr;
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
