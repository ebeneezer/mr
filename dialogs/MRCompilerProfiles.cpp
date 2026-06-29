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
#include "../app/services/MRLspServerProfile.hpp"
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
#include <unistd.h>
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
	cmCompilerProfilesChooseLspExecutable,
	cmCompilerProfilesBrowseLspExecutable,
	cmCompilerProfilesAcceptLspExecutable,
	cmCompilerProfilesChooseLspWorkingDirectory,
	cmCompilerProfilesBrowseLspWorkingDirectory,
	cmCompilerProfilesAcceptLspWorkingDirectory,
	cmCompilerProfilesChooseLspMiddleware,
	cmCompilerProfilesBrowseLspMiddleware,
	cmCompilerProfilesAcceptLspMiddleware,
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
	kPathListSize = 256,
	kAudioUriSize = 256,
	kLspExecutableSize = 256,
	kLspArgumentsSize = 256,
	kLspWorkingDirectorySize = 256,
	kLspMiddlewareSize = 256
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

std::string lspCandidateArgumentText(const mr::services::MRLspServerCandidate &candidate) {
	std::ostringstream text;

	for (std::size_t i = 0; i < candidate.arguments.size(); ++i) {
		if (i != 0) text << " ";
		text << candidate.arguments[i];
	}
	return text.str();
}

std::string lspCandidateLabel(const mr::services::MRLspServerCandidate &candidate) {
	const std::string arguments = lspCandidateArgumentText(candidate);
	std::string label = candidate.executableName;

	if (!arguments.empty()) label += " " + arguments;
	if (!candidate.profileName.empty()) label += "  [" + candidate.profileName + "]";
	return label;
}

std::string readCurrentWorkingDirectory() {
	char cwd[PATH_MAX];

	if (::getcwd(cwd, sizeof(cwd)) == nullptr) return std::string();
	return std::string(cwd);
}

MRSyntaxLanguage inferLspLanguageFromCompilerProfile(const MRCompilerProfile &profile) {
	struct LanguageToken {
		const char *token;
		MRSyntaxLanguage language;
	};
	static const LanguageToken tokens[] = {
		{ "LATEXMK", MRSyntaxLanguage::Latex },
		{ "LATEX", MRSyntaxLanguage::Latex },
		{ "TEX", MRSyntaxLanguage::Latex },
		{ "SWIFT", MRSyntaxLanguage::Swift },
		{ "CLANG", MRSyntaxLanguage::Cpp },
		{ "GCC", MRSyntaxLanguage::Cpp },
		{ "G++", MRSyntaxLanguage::Cpp },
		{ "CPP", MRSyntaxLanguage::Cpp },
		{ "C++", MRSyntaxLanguage::Cpp },
		{ "PYTHON", MRSyntaxLanguage::Python },
		{ "JAVASCRIPT", MRSyntaxLanguage::JavaScript },
		{ "TYPESCRIPT", MRSyntaxLanguage::JavaScript },
		{ "JSON", MRSyntaxLanguage::Json },
		{ "YAML", MRSyntaxLanguage::Yaml },
		{ "XML", MRSyntaxLanguage::Xml },
		{ "BASH", MRSyntaxLanguage::Bash },
		{ "ZSH", MRSyntaxLanguage::Zsh },
		{ "FISH", MRSyntaxLanguage::Fish },
		{ "PERL", MRSyntaxLanguage::Perl },
		{ "RUST", MRSyntaxLanguage::Rust },
		{ "GO", MRSyntaxLanguage::Go },
		{ "KOTLIN", MRSyntaxLanguage::Kotlin },
		{ "CSHARP", MRSyntaxLanguage::CSharp },
		{ "C#", MRSyntaxLanguage::CSharp },
		{ "PASCAL", MRSyntaxLanguage::Pascal },
		{ "SYSTEMD", MRSyntaxLanguage::Systemd },
		{ "MAKE", MRSyntaxLanguage::Make },
		{ "MARKDOWN", MRSyntaxLanguage::Markdown }
	};
	const std::string text = upperAscii(profile.toolchain + " " + profile.id + " " + profile.name);

	for (const LanguageToken &token : tokens)
		if (text.find(token.token) != std::string::npos) return token.language;
	return MRSyntaxLanguage::PlainText;
}

class LspCandidateSelectionDialog : public MRDialogFoundation {
  public:
	explicit LspCandidateSelectionDialog(const std::vector<mr::services::MRLspServerCandidate> &availableCandidates)
	    : TWindowInit(initMrDialogFrame), MRDialogFoundation(centeredSetupDialogRect(84, 13), "SELECT LSP SERVER", 84, 13, initMrDialogFrame), candidates(availableCandidates) {
		TPlainStringCollection *items = new TPlainStringCollection(std::max<short>(1, static_cast<short>(candidates.size())), 5);
		TListBoxRec data;

		insert(new TStaticText(TRect(3, 2, 79, 3), "Available language servers:"));
		scrollBar = new TScrollBar(TRect(79, 3, 80, 9));
		insert(scrollBar);
		list = new TListBox(TRect(3, 3, 79, 9), 1, scrollBar);
		insert(list);
		insert(new TButton(TRect(36, 10, 48, 12), "~S~elect", cmOK, bfDefault));
		if (items != nullptr && list != nullptr) {
			for (const mr::services::MRLspServerCandidate &candidate : candidates)
				items->insert(dupCString(lspCandidateLabel(candidate)));
			data.items = items;
			data.selection = 0;
			list->setData(&data);
		}
		finalizeLayout();
		if (list != nullptr) list->select();
	}

	int run() {
		TListBoxRec data;
		const ushort result = TProgram::deskTop != nullptr ? TProgram::deskTop->execView(this) : cmCancel;

		if (result != cmOK || list == nullptr || candidates.empty()) return -1;
		list->getData(&data);
		if (data.selection >= candidates.size()) return -1;
		return static_cast<int>(data.selection);
	}

  private:
	std::vector<mr::services::MRLspServerCandidate> candidates;
	TListBox *list = nullptr;
	TScrollBar *scrollBar = nullptr;
};

class CompilerProfilesDialog : public MRDialogFoundation {
  public:
	explicit CompilerProfilesDialog(const std::vector<MRCompilerProfile> &initialProfiles)
	    : TWindowInit(initMrDialogFrame), MRDialogFoundation(centeredSetupDialogRect(kDialogWidth, kDialogHeight), "COMPILER PROFILES", kDialogWidth, kDialogHeight, initMrDialogFrame), profiles(initialProfiles) {
		const int left = 3;
		const int listRight = 37;
		const int scrollRight = 38;
		const int labelLeft = 41;
		const int fieldLeft = 57;
		const int right = 109;
		const int browseLeft = right - 2;
		const int historyLeft = browseLeft - 2;
		const int fieldWithButtonsRight = historyLeft;
		const int wideFieldRight = browseLeft;
		const int buttonTop = 21;
		const int bottomTop = 23;

		options |= ofCentered;

		insert(new TStaticText(TRect(left, 2, left + 18, 3), "Profiles:"));
		scrollBar = new TScrollBar(TRect(listRight, 3, scrollRight, 20));
		insert(scrollBar);
		list = new TCompilerProfileListBox(TRect(left, 3, listRight, 20), scrollBar);
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
		insert(new TStaticText(TRect(labelLeft, 9, fieldLeft - 1, 10), "Includes:"));
		includesField = addField(TRect(fieldLeft, 9, wideFieldRight, 10), kPathListSize - 1);
		insert(new TStaticText(TRect(labelLeft, 10, fieldLeft - 1, 11), "Libraries:"));
		librariesField = addField(TRect(fieldLeft, 10, wideFieldRight, 11), kPathListSize - 1);
		insert(new TStaticText(TRect(labelLeft, 11, fieldLeft - 1, 12), "Runtime:"));
		runtimeField = addField(TRect(fieldLeft, 11, wideFieldRight, 12), kPathListSize - 1);
		insert(new TStaticText(TRect(labelLeft, 12, fieldLeft - 1, 13), "LSP exec:"));
		lspExecutableField = addField(TRect(fieldLeft, 12, fieldWithButtonsRight, 13), kLspExecutableSize - 1);
		lspExecutableHistoryButton = lspExecutableDropList.createButton(*this, TRect(historyLeft, 12, browseLeft, 13), lspExecutableField, this, cmCompilerProfilesChooseLspExecutable, true);
		lspExecutableBrowseButton = new TInlineGlyphButton(TRect(browseLeft, 12, right, 13), "🔎", cmCompilerProfilesBrowseLspExecutable);
		insert(lspExecutableBrowseButton);
		lspExecutableListAnchor = TRect(fieldLeft, 13, right, 14);
		insert(new TStaticText(TRect(labelLeft, 13, fieldLeft - 1, 14), "LSP args:"));
		lspArgumentsField = addField(TRect(fieldLeft, 13, wideFieldRight, 14), kLspArgumentsSize - 1);
		insert(new TStaticText(TRect(labelLeft, 14, fieldLeft - 1, 15), "LSP cwd:"));
		lspWorkingDirectoryField = addField(TRect(fieldLeft, 14, fieldWithButtonsRight, 15), kLspWorkingDirectorySize - 1);
		lspWorkingDirectoryHistoryButton = lspWorkingDirectoryDropList.createButton(*this, TRect(historyLeft, 14, browseLeft, 15), lspWorkingDirectoryField, this, cmCompilerProfilesChooseLspWorkingDirectory, true);
		lspWorkingDirectoryBrowseButton = new TInlineGlyphButton(TRect(browseLeft, 14, right, 15), "🔎", cmCompilerProfilesBrowseLspWorkingDirectory);
		insert(lspWorkingDirectoryBrowseButton);
		lspWorkingDirectoryListAnchor = TRect(fieldLeft, 15, right, 16);
		insert(new TStaticText(TRect(labelLeft, 15, fieldLeft - 1, 16), "LSP middleware:"));
		lspMiddlewareField = addField(TRect(fieldLeft, 15, fieldWithButtonsRight, 16), kLspMiddlewareSize - 1);
		lspMiddlewareHistoryButton = lspMiddlewareDropList.createButton(*this, TRect(historyLeft, 15, browseLeft, 16), lspMiddlewareField, this, cmCompilerProfilesChooseLspMiddleware, true);
		lspMiddlewareBrowseButton = new TInlineGlyphButton(TRect(browseLeft, 15, right, 16), "🔎", cmCompilerProfilesBrowseLspMiddleware);
		insert(lspMiddlewareBrowseButton);
		lspMiddlewareListAnchor = TRect(fieldLeft, 16, right, 17);
		insert(new TStaticText(TRect(labelLeft, 16, fieldLeft - 1, 17), "Success audio:"));
		successAudioField = addField(TRect(fieldLeft, 16, fieldWithButtonsRight, 17), kAudioUriSize - 1);
		successAudioHistoryButton = successAudioDropList.createButton(*this, TRect(historyLeft, 16, browseLeft, 17), successAudioField, this, cmCompilerProfilesChooseSuccessAudio, true);
		successAudioBrowseButton = new TInlineGlyphButton(TRect(browseLeft, 16, right, 17), "🔎", cmCompilerProfilesBrowseSuccessAudio);
		insert(successAudioBrowseButton);
		successAudioListAnchor = TRect(fieldLeft, 17, right, 18);
		insert(new TStaticText(TRect(labelLeft, 17, fieldLeft - 1, 18), "Failure audio:"));
		failureAudioField = addField(TRect(fieldLeft, 17, fieldWithButtonsRight, 18), kAudioUriSize - 1);
		failureAudioHistoryButton = failureAudioDropList.createButton(*this, TRect(historyLeft, 17, browseLeft, 18), failureAudioField, this, cmCompilerProfilesChooseFailureAudio, true);
		failureAudioBrowseButton = new TInlineGlyphButton(TRect(browseLeft, 17, right, 18), "🔎", cmCompilerProfilesBrowseFailureAudio);
		insert(failureAudioBrowseButton);
		failureAudioListAnchor = TRect(fieldLeft, 18, right, 19);

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
		if (lspExecutableDropList.handleOpenListEvent(event)) return;
		if (lspWorkingDirectoryDropList.handleOpenListEvent(event)) return;
		if (lspMiddlewareDropList.handleOpenListEvent(event)) return;
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
				case cmCompilerProfilesChooseLspExecutable:
					lspExecutableDropList.toggle(*this, lspExecutableListAnchor, lspExecutableChoices(), readInput(lspExecutableField, kLspExecutableSize), this, cmCompilerProfilesAcceptLspExecutable, 8);
					clearEvent(event);
					return;
				case cmCompilerProfilesBrowseLspExecutable:
					browseLspExecutable();
					clearEvent(event);
					return;
				case cmCompilerProfilesAcceptLspExecutable:
					acceptLspExecutableSelection();
					clearEvent(event);
					return;
				case cmCompilerProfilesChooseLspWorkingDirectory:
					lspWorkingDirectoryDropList.toggle(*this, lspWorkingDirectoryListAnchor, lspWorkingDirectoryChoices(), readInput(lspWorkingDirectoryField, kLspWorkingDirectorySize), this, cmCompilerProfilesAcceptLspWorkingDirectory, 8);
					clearEvent(event);
					return;
				case cmCompilerProfilesBrowseLspWorkingDirectory:
					browseLspWorkingDirectory();
					clearEvent(event);
					return;
				case cmCompilerProfilesAcceptLspWorkingDirectory:
					acceptLspWorkingDirectorySelection();
					clearEvent(event);
					return;
				case cmCompilerProfilesChooseLspMiddleware:
					lspMiddlewareDropList.toggle(*this, lspMiddlewareListAnchor, lspMiddlewareChoices(), readInput(lspMiddlewareField, kLspMiddlewareSize), this, cmCompilerProfilesAcceptLspMiddleware, 8);
					clearEvent(event);
					return;
				case cmCompilerProfilesBrowseLspMiddleware:
					browseLspMiddleware();
					clearEvent(event);
					return;
				case cmCompilerProfilesAcceptLspMiddleware:
					acceptLspMiddlewareSelection();
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
		for (const std::string &pathValue : defaultCompilerExecutablePaths()) {
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

	std::vector<std::string> lspExecutableChoices() const {
		std::vector<std::string> choices;
		MRSyntaxLanguage language = MRSyntaxLanguage::PlainText;

		for (const MRCompilerProfile &profile : profiles) {
			const std::string path = normalizeConfiguredPathInput(profile.lspExecutablePath);

			if (!path.empty() && std::find(choices.begin(), choices.end(), path) == choices.end()) choices.push_back(path);
		}
		if (lspExecutableField != nullptr) {
			const std::string path = normalizeConfiguredPathInput(readInput(lspExecutableField, kLspExecutableSize));

			if (!path.empty() && std::find(choices.begin(), choices.end(), path) == choices.end()) choices.push_back(path);
		}
		if (currentIndex >= 0 && currentIndex < static_cast<int>(profiles.size())) language = inferLspLanguageFromCompilerProfile(profiles[currentIndex]);
		if (language != MRSyntaxLanguage::PlainText) {
			for (const mr::services::MRLspServerCandidate &candidate : mr::services::availableLspServerCandidatesForLanguage(language)) {
				mr::services::MRLspServerProfile profile;

				if (!mr::services::resolveLspServerCandidate(candidate, profile)) continue;
				if (!profile.executablePath.empty() && std::find(choices.begin(), choices.end(), profile.executablePath) == choices.end()) choices.push_back(profile.executablePath);
			}
		}
		std::sort(choices.begin(), choices.end());
		return choices;
	}

	std::vector<std::string> lspWorkingDirectoryChoices() const {
		std::vector<std::string> choices;
		std::vector<std::string> pathHistory;

		for (const MRCompilerProfile &profile : profiles) {
			const std::string path = normalizeConfiguredPathInput(profile.lspWorkingDirectory);

			if (!path.empty() && std::find(choices.begin(), choices.end(), path) == choices.end()) choices.push_back(path);
		}
		if (lspWorkingDirectoryField != nullptr) {
			const std::string path = normalizeConfiguredPathInput(readInput(lspWorkingDirectoryField, kLspWorkingDirectorySize));

			if (!path.empty() && std::find(choices.begin(), choices.end(), path) == choices.end()) choices.push_back(path);
		}
		configuredScopedDialogPathHistoryEntries(MRDialogHistoryScope::General, pathHistory);
		for (const std::string &pathValue : pathHistory) {
			const std::string path = normalizeConfiguredPathInput(pathValue);

			if (!path.empty() && std::find(choices.begin(), choices.end(), path) == choices.end()) choices.push_back(path);
		}
		std::sort(choices.begin(), choices.end());
		return choices;
	}

	std::vector<std::string> lspMiddlewareChoices() const {
		std::vector<std::string> choices;
		std::vector<std::string> fileHistory;

		for (const MRCompilerProfile &profile : profiles) {
			const std::string path = normalizeConfiguredPathInput(profile.lspMiddlewarePath);

			if (!path.empty() && std::find(choices.begin(), choices.end(), path) == choices.end()) choices.push_back(path);
		}
		if (lspMiddlewareField != nullptr) {
			const std::string path = normalizeConfiguredPathInput(readInput(lspMiddlewareField, kLspMiddlewareSize));

			if (!path.empty() && std::find(choices.begin(), choices.end(), path) == choices.end()) choices.push_back(path);
		}
		configuredScopedDialogFileHistoryEntries(MRDialogHistoryScope::General, fileHistory);
		for (const std::string &pathValue : fileHistory) {
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

	void acceptLspExecutableSelection() {
		std::string selectedValue;

		if (!lspExecutableDropList.acceptSelection(selectedValue)) return;
		writeInput(lspExecutableField, selectedValue, kLspExecutableSize);
		saveCurrentProfile();
	}

	void acceptLspWorkingDirectorySelection() {
		std::string selectedValue;

		if (!lspWorkingDirectoryDropList.acceptSelection(selectedValue)) return;
		writeInput(lspWorkingDirectoryField, selectedValue, kLspWorkingDirectorySize);
		saveCurrentProfile();
	}

	void acceptLspMiddlewareSelection() {
		std::string selectedValue;

		if (!lspMiddlewareDropList.acceptSelection(selectedValue)) return;
		writeInput(lspMiddlewareField, selectedValue, kLspMiddlewareSize);
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

	void browseLspExecutable() {
		char fileName[MAXPATH] = {0};
		const std::string currentPath = normalizeConfiguredPathInput(readInput(lspExecutableField, kLspExecutableSize));
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
		result = mr::dialogs::execRememberingFileDialogWithData(MRDialogHistoryScope::General, "*.*", "SELECT LSP EXECUTABLE", "~N~ame", fdOpenButton, fileName);
		if (result == cmCancel) return;
		writeInput(lspExecutableField, normalizeConfiguredPathInput(fileName), kLspExecutableSize);
		saveCurrentProfile();
	}

	void browseLspWorkingDirectory() {
		const std::string originalCwd = readCurrentWorkingDirectory();
		const std::string currentPath = normalizeConfiguredPathInput(readInput(lspWorkingDirectoryField, kLspWorkingDirectorySize));
		std::string picked;
		ushort result = cmCancel;

		if (!currentPath.empty()) (void)::chdir(currentPath.c_str());
		else {
			const std::string seed = configuredLastFileDialogPath(MRDialogHistoryScope::General);

			if (!seed.empty()) (void)::chdir(seed.c_str());
		}
		result = mr::dialogs::execDialog(mr::dialogs::createDirectoryDialog(MRDialogHistoryScope::General, cdNormal));
		picked = readCurrentWorkingDirectory();
		if (!originalCwd.empty()) (void)::chdir(originalCwd.c_str());
		if (result == cmCancel || picked.empty()) return;
		picked = normalizeConfiguredPathInput(picked);
		writeInput(lspWorkingDirectoryField, picked, kLspWorkingDirectorySize);
		rememberLoadDialogPath(MRDialogHistoryScope::General, picked.c_str());
		saveCurrentProfile();
	}

	void browseLspMiddleware() {
		char fileName[MAXPATH] = {0};
		const std::string currentPath = normalizeConfiguredPathInput(readInput(lspMiddlewareField, kLspMiddlewareSize));
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
		result = mr::dialogs::execRememberingFileDialogWithData(MRDialogHistoryScope::General, "*.*", "SELECT LSP MIDDLEWARE", "~N~ame", fdOpenButton, fileName);
		if (result == cmCancel) return;
		writeInput(lspMiddlewareField, normalizeConfiguredPathInput(fileName), kLspMiddlewareSize);
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
		writeInput(includesField, normalizeCompilerProfilePathList(profile.includePaths), kPathListSize);
		writeInput(librariesField, normalizeCompilerProfilePathList(profile.libraryPaths), kPathListSize);
		writeInput(runtimeField, normalizeCompilerProfilePathList(profile.runtimePaths), kPathListSize);
		writeInput(lspExecutableField, profile.lspExecutablePath, kLspExecutableSize);
		writeInput(lspArgumentsField, profile.lspArguments, kLspArgumentsSize);
		writeInput(lspWorkingDirectoryField, profile.lspWorkingDirectory, kLspWorkingDirectorySize);
		writeInput(lspMiddlewareField, profile.lspMiddlewarePath, kLspMiddlewareSize);
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
		profile.includePaths = splitCompilerProfilePathList(readInput(includesField, kPathListSize));
		profile.libraryPaths = splitCompilerProfilePathList(readInput(librariesField, kPathListSize));
		profile.runtimePaths = splitCompilerProfilePathList(readInput(runtimeField, kPathListSize));
		profile.lspExecutablePath = readInput(lspExecutableField, kLspExecutableSize);
		profile.lspArguments = readInput(lspArgumentsField, kLspArgumentsSize);
		profile.lspWorkingDirectory = readInput(lspWorkingDirectoryField, kLspWorkingDirectorySize);
		profile.lspMiddlewarePath = readInput(lspMiddlewareField, kLspMiddlewareSize);
		profile.buildSuccessAudioUri = readInput(successAudioField, kAudioUriSize);
		profile.buildFailureAudioUri = readInput(failureAudioField, kAudioUriSize);
	}

	void changeSelection(int index) {
		if (index == currentIndex || index < 0 || index >= static_cast<int>(profiles.size())) return;
		executableDropList.hide();
		lspExecutableDropList.hide();
		lspWorkingDirectoryDropList.hide();
		lspMiddlewareDropList.hide();
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
		MRSyntaxLanguage language = MRSyntaxLanguage::PlainText;
		std::vector<mr::services::MRLspServerCandidate> availableCandidates;
		int selectedCandidate = -1;

		if (currentIndex < 0 || currentIndex >= static_cast<int>(profiles.size())) return;
		saveCurrentProfile();
		if (!autoConfigureCompilerProfileFromExecutable(profiles[currentIndex], &errorText)) {
			mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
			loadCurrentProfile();
			return;
		}
		language = inferLspLanguageFromCompilerProfile(profiles[currentIndex]);
		if (language != MRSyntaxLanguage::PlainText) {
			availableCandidates = mr::services::availableLspServerCandidatesForLanguage(language);
			if (availableCandidates.size() == 1) {
				mr::services::MRLspServerProfile lspProfile;

				if (mr::services::resolveLspServerCandidate(availableCandidates[0], lspProfile)) {
					profiles[currentIndex].lspExecutablePath = lspProfile.executablePath;
					profiles[currentIndex].lspArguments = mr::services::lspServerProfileArgumentText(lspProfile);
					profiles[currentIndex].lspWorkingDirectory = lspProfile.workingDirectory;
					profiles[currentIndex].lspMiddlewarePath = lspProfile.lspMiddlewarePath;
				}
			} else if (availableCandidates.size() > 1) {
				LspCandidateSelectionDialog *dialog = new LspCandidateSelectionDialog(availableCandidates);

				if (dialog != nullptr) {
					selectedCandidate = dialog->run();
					TObject::destroy(dialog);
				}
				if (selectedCandidate >= 0 && selectedCandidate < static_cast<int>(availableCandidates.size())) {
					mr::services::MRLspServerProfile lspProfile;

					if (mr::services::resolveLspServerCandidate(availableCandidates[static_cast<std::size_t>(selectedCandidate)], lspProfile)) {
						profiles[currentIndex].lspExecutablePath = lspProfile.executablePath;
						profiles[currentIndex].lspArguments = mr::services::lspServerProfileArgumentText(lspProfile);
						profiles[currentIndex].lspWorkingDirectory = lspProfile.workingDirectory;
						profiles[currentIndex].lspMiddlewarePath = lspProfile.lspMiddlewarePath;
					}
				}
			} else {
				mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, std::string("No LSP server found in PATH for ") + tmrSyntaxLanguageName(language) + ".", mr::messageline::Kind::Warning, mr::messageline::kPriorityMedium);
			}
		} else {
			mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Unable to infer LSP language for this compiler profile.", mr::messageline::Kind::Warning, mr::messageline::kPriorityMedium);
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
	MRDropList lspExecutableDropList;
	MRDropList lspWorkingDirectoryDropList;
	MRDropList lspMiddlewareDropList;
	MRDropList successAudioDropList;
	MRDropList failureAudioDropList;
	TRect executableListAnchor;
	TRect lspExecutableListAnchor;
	TRect lspWorkingDirectoryListAnchor;
	TRect lspMiddlewareListAnchor;
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
	TInputLine *includesField = nullptr;
	TInputLine *librariesField = nullptr;
	TInputLine *runtimeField = nullptr;
	TInputLine *lspExecutableField = nullptr;
	TView *lspExecutableHistoryButton = nullptr;
	TInlineGlyphButton *lspExecutableBrowseButton = nullptr;
	TInputLine *lspArgumentsField = nullptr;
	TInputLine *lspWorkingDirectoryField = nullptr;
	TView *lspWorkingDirectoryHistoryButton = nullptr;
	TInlineGlyphButton *lspWorkingDirectoryBrowseButton = nullptr;
	TInputLine *lspMiddlewareField = nullptr;
	TView *lspMiddlewareHistoryButton = nullptr;
	TInlineGlyphButton *lspMiddlewareBrowseButton = nullptr;
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
