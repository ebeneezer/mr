#define Uses_TButton
#define Uses_TDialog
#define Uses_TDrawBuffer
#define Uses_TFileDialog
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_MsgBox
#define Uses_TObject
#define Uses_TProgram
#define Uses_TRect
#define Uses_TRadioButtons
#define Uses_TScrollBar
#define Uses_TStaticText
#define Uses_TSItem
#define Uses_TView
#define Uses_TWindow
#define Uses_TWindowInit
#include <tvision/tv.h>

#include "MRKeymapManager.hpp"
#include "../app/MRHelpTopics.generated.hpp"

#include "MRDirtyGating.hpp"
#include "setup/MRSetup.hpp"
#include "setup/MRSetupCommon.hpp"
#include "../app/MRVersion.hpp"
#include "../app/utils/MRFileIOUtils.hpp"
#include "../app/utils/MRStringUtils.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../config/settings/MRSettingsRuntimeState.hpp"
#include "../config/settings/MRSettingsStorage.hpp"
#include "../keymap/MRKeymapActionCatalog.hpp"
#include "../keymap/MRKeymapProfile.hpp"
#include "../mrmac/MRMacroRunner.hpp"
#include "../mrmac/mrmac.h"
#include "../ui/widgets/MRColumnListView.hpp"
#include "../ui/MRFrame.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/MRWindowSupport.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <map>
#include <regex>
#include <set>
#include <span>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

enum : ushort {
	cmMrSetupKeymapLoad = 3921,
	cmMrSetupKeymapSave,
	cmMrSetupKeymapSaveAs,
	cmMrSetupKeymapProfileSelectionChanged,
	cmMrSetupKeymapProfileUse,
	cmMrSetupKeymapProfileAdd,
	cmMrSetupKeymapProfileEdit,
	cmMrSetupKeymapProfileDelete,
	cmMrSetupKeymapBindingAdd,
	cmMrSetupKeymapBindingEdit,
	cmMrSetupKeymapBindingDelete,
	cmMrSetupKeymapBindingCapture,
	cmMrSetupKeymapBindingTargetFilterChanged,
	cmMrSetupKeymapBindingFilterChanged
};

constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);
constexpr int kDialogWidth = 108;
constexpr int kVisibleHeight = 24;
constexpr int kVirtualHeight = 24;
constexpr int kFileDialogPathSize = 512;
constexpr int kProfileNameFieldSize = 64;
constexpr int kProfileDescriptionFieldSize = 128;
constexpr int kBindingDescriptionFieldSize = 128;
constexpr int kBindingFilterFieldSize = 64;
constexpr int kTargetFilterFieldSize = 64;
constexpr char kBindingRecordingGlyph[] = "📼";

TFrame *initMrDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}

void postDialogError(const std::string &text) {
	if (text.empty()) return;
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, text, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}

void postDialogWarning(const std::string &text) {
	if (text.empty()) return;
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, text, mr::messageline::Kind::Warning, mr::messageline::kPriorityHigh);
}

std::string keymapDiagnosticIdentity(const MRKeymapDiagnostic &diagnostic) {
	return std::to_string(static_cast<unsigned>(diagnostic.kind)) + "|" + std::to_string(static_cast<unsigned>(diagnostic.severity)) + "|" + std::to_string(diagnostic.profileIndex) + "|" + std::to_string(diagnostic.bindingIndex) + "|" + diagnostic.message;
}

std::string describeKeymapDiagnostic(std::span<const MRKeymapProfile> profiles, const MRKeymapDiagnostic &diagnostic) {
	std::string text = diagnostic.message;

	if (diagnostic.profileIndex != kNoIndex && diagnostic.profileIndex < profiles.size()) {
		const MRKeymapProfile &profile = profiles[diagnostic.profileIndex];
		text += " profile='" + profile.name + "'";
		if (diagnostic.bindingIndex != kNoIndex && diagnostic.bindingIndex < profile.bindings.size()) {
			const MRKeymapBindingRecord &binding = profile.bindings[diagnostic.bindingIndex];
			text += " binding=" + std::to_string(diagnostic.bindingIndex + 1);
			text += " target='" + binding.target.target + "'";
			text += " sequence='" + binding.sequence.toString() + "'";
		}
	}
	return text;
}

std::string summarizeKeymapDiagnosticsForMessageLine(std::span<const MRKeymapDiagnostic> diagnostics, std::string_view operation) {
	std::set<std::string> seen;
	std::size_t errorCount = 0;
	std::size_t warningCount = 0;

	for (const MRKeymapDiagnostic &diagnostic : diagnostics) {
		if (!seen.insert(keymapDiagnosticIdentity(diagnostic)).second) continue;
		if (diagnostic.severity == MRKeymapDiagnosticSeverity::Error) ++errorCount;
		else
			++warningCount;
	}
	if (errorCount == 0 && warningCount == 0) return std::string();
	if (errorCount == 0) return std::string(operation) + ": " + std::to_string(warningCount) + " warning(s); see log.";
	return std::string(operation) + ": removed " + std::to_string(errorCount) + " invalid key binding(s); see log.";
}

void logKeymapDiagnostics(std::string_view origin, std::span<const MRKeymapProfile> profiles, std::span<const MRKeymapDiagnostic> diagnostics) {
	std::set<std::string> seen;

	for (const MRKeymapDiagnostic &diagnostic : diagnostics) {
		if (!seen.insert(keymapDiagnosticIdentity(diagnostic)).second) continue;
		const char *severity = diagnostic.severity == MRKeymapDiagnosticSeverity::Error ? "error" : "warning";
		mrLogMessage(std::string(origin) + " diagnostic [" + severity + "]: " + describeKeymapDiagnostic(profiles, diagnostic));
	}
}

bool pathIsRegularFile(const std::string &path) {
	struct stat st;

	return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool confirmOverwriteForPath(const char *primaryLabel, const char *headline, const std::string &targetPath) {
	if (!pathIsRegularFile(targetPath)) return true;
	return mr::dialogs::showUnsavedChangesDialog(primaryLabel, headline, targetPath.c_str()) == mr::dialogs::UnsavedChangesChoice::Save;
}

struct KeymapManagerDraft {
	std::vector<MRKeymapProfile> profiles;
	std::string activeProfileName;

	bool operator==(const KeymapManagerDraft &other) const noexcept {
		return profiles == other.profiles && activeProfileName == other.activeProfileName;
	}
};

std::string summarizeDraftForLog(const KeymapManagerDraft &draft) {
	std::string text = "Keymap dialog state: active='" + draft.activeProfileName + "' profiles=" + std::to_string(draft.profiles.size());

	for (const MRKeymapProfile &profile : draft.profiles)
		text += " [" + profile.name + ":" + std::to_string(profile.bindings.size()) + "]";
	return text;
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

bool validateKeymapFileVersion(const std::string &source, bool &upgradeRequired, std::string &errorText) {
	static const std::regex assignmentPattern("(?:MRSETUP\\s*\\(\\s*'((?:''|[^'])*)'\\s*,\\s*'((?:''|[^'])*)'\\s*\\)|([A-Z_][A-Z0-9_]*)\\s*\\(\\s*'((?:''|[^'])*)'\\s*\\))", std::regex::icase);
	const std::uint64_t currentVersion = mrCurrentPersistenceVersion();
	std::smatch match;
	std::string remaining = source;
	bool versionSeen = false;

	upgradeRequired = false;
	errorText.clear();
	while (std::regex_search(remaining, match, assignmentPattern)) {
		const bool mrsetupRecord = match[1].matched;
		const std::string key = upperAscii(trimAscii(unescapeMrmacSingleQuotedLiteral(mrsetupRecord ? match[1].str() : match[3].str())));
		const std::string value = trimAscii(unescapeMrmacSingleQuotedLiteral(mrsetupRecord ? match[2].str() : match[4].str()));

		if (key == mrKeymapVersionSetupKey()) {
			std::uint64_t parsedVersion = 0;

			versionSeen = true;
			if (!mrParsePersistenceVersion(value, parsedVersion)) {
				errorText = mrInvalidPersistenceVersionMessage("keymap file");
				return false;
			}
			if (parsedVersion > currentVersion) {
				errorText = mrFuturePersistenceVersionMessage("Keymap file", value);
				return false;
			}
			if (parsedVersion < currentVersion) upgradeRequired = true;
		}
		remaining = match.suffix().str();
	}
	if (!versionSeen) upgradeRequired = true;
	return true;
}

std::string buildSerializedKeymapFileSource(const KeymapManagerDraft &draft) {
	return buildExecutableKeymapMacroSource(draft.profiles, draft.activeProfileName);
}

bool syncConfiguredKeymapAutoexecEntry(const std::string &fileUri, const std::string &previousFileUri, std::string &errorText);

std::string stripTargetPrefix(std::string_view target) {
	std::string stripped(target);

	if (stripped.rfind("MRMAC_", 0) == 0) return stripped.substr(6);
	if (stripped.rfind("MR_", 0) == 0) return stripped.substr(3);
	return stripped;
}

std::string compactActionDescription(std::string_view text) {
	std::string result(text);

	const std::pair<std::string_view, std::string_view> replacements[] = {
	    {"end of line", "EOL"}, {"top of the file", "BOF"}, {"bottom of the file", "EOF"}, {"page break", "page brk"}, {"random access", "rand acc"}, {"Search and replace", "Search/replace"}, {"Display ", ""}, {"Move cursor to ", ""}, {"Cursor to ", ""}, {"Cursor ", ""}, {"marked block", "block"},
	};

	for (const auto &[needle, replacement] : replacements) {
		const std::size_t pos = result.find(needle);
		if (pos != std::string::npos) result.replace(pos, needle.size(), replacement);
	}
	return result;
}

class TInlineGlyphButton : public TView {
  public:
	TInlineGlyphButton(const TRect &bounds, const char *glyph, ushort command) : TView(bounds), mGlyph(glyph != nullptr ? glyph : ""), mCommand(command) {
		options |= ofSelectable;
		options |= ofFirstClick;
		eventMask |= evMouseDown | evKeyDown;
	}

	void draw() override {
		TDrawBuffer buffer;
		const TAttrPair color = getColor((state & sfFocused) != 0 ? 2 : 1);
		const int x = size.x > 1 ? (size.x - 1) / 2 : 0;

		buffer.moveChar(0, ' ', color, size.x);
		buffer.moveStr(static_cast<ushort>(x), mGlyph.c_str(), color, std::max(0, size.x - x));
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

class TActiveProfileField : public TView {
  public:
	TActiveProfileField(const TRect &bounds, const std::string &text) : TView(bounds), mText(text) {
	}

	void setText(const std::string &text) {
		mText = text;
		drawView();
	}

	void draw() override {
		TDrawBuffer buffer;
		TColorAttr color = getColor(1);
		std::string shown = "active: " + mText;
		int start = 0;

		buffer.moveChar(0, ' ', color, size.x);
		if (shown.size() > static_cast<std::size_t>(size.x)) shown = shown.substr(0, static_cast<std::size_t>(size.x));
		start = (size.x - static_cast<int>(shown.size())) / 2;
		if (start < 0) start = 0;
		buffer.moveStr(static_cast<ushort>(start), shown.c_str(), color, std::max(0, size.x - start));
		writeLine(0, 0, size.x, 1, buffer);
	}

  private:
	std::string mText;
};

class TSequenceDisplay : public TView {
  public:
	TSequenceDisplay(const TRect &bounds) : TView(bounds) {
	}

	void setText(const std::string &text) {
		if (mText == text) return;
		mText = text;
		drawView();
	}

	[[nodiscard]] const std::string &text() const noexcept {
		return mText;
	}

	void draw() override {
		TDrawBuffer buffer;
		TColorAttr configuredAttr;
		TColorAttr color = getColor(1);
		std::string shown = mText;

		if (configuredColorSlotOverride(9, configuredAttr)) color = configuredAttr;
		buffer.moveChar(0, ' ', color, size.x);
		if (shown.size() > static_cast<std::size_t>(size.x)) shown = shown.substr(0, static_cast<std::size_t>(size.x));
		if (!shown.empty()) buffer.moveStr(0, shown.c_str(), color, size.x);
		writeLine(0, 0, size.x, size.y, buffer);
	}

  private:
	std::string mText;
};

class TNotifyingInputLine : public TInputLine {
  public:
	TNotifyingInputLine(const TRect &bounds, int maxLen, ushort changeCommand) noexcept : TInputLine(bounds, maxLen), mCapacity(maxLen + 1), mChangeCommand(changeCommand) {
	}

	void handleEvent(TEvent &event) override {
		std::string beforeText = currentText();
		TView *target = owner;

		TInputLine::handleEvent(event);
		while (target != nullptr && dynamic_cast<TDialog *>(target) == nullptr)
			target = target->owner;
		if (currentText() != beforeText) message(target != nullptr ? target : owner, evBroadcast, mChangeCommand, this);
	}

  private:
	std::string currentText() const {
		std::vector<char> buffer(mCapacity, '\0');

		const_cast<TNotifyingInputLine *>(this)->getData(buffer.data());
		return std::string(buffer.data());
	}

	int mCapacity = 0;
	ushort mChangeCommand = 0;
};

std::size_t activeProfileIndex(const KeymapManagerDraft &draft) {
	for (std::size_t i = 0; i < draft.profiles.size(); ++i)
		if (draft.profiles[i].name == draft.activeProfileName) return i;
	return kNoIndex;
}

KeymapManagerDraft draftFromCanonicalizedResult(const MRKeymapCanonicalizationResult &result) {
	KeymapManagerDraft draft;

	draft.profiles = result.profiles;
	draft.activeProfileName = result.activeProfileName;
	return draft;
}

struct BindingTargetChoice {
	std::string target;
	std::string label;
};

class KeymapBindingListView : public MRColumnListView {
  public:
	KeymapBindingListView(const TRect &bounds, TScrollBar *verticalScrollBar, TScrollBar *horizontalScrollBar, TView *relay = nullptr, ushort selectionCommand = 0, ushort activationCommand = 0) noexcept : MRColumnListView(bounds, verticalScrollBar, horizontalScrollBar, relay, selectionCommand, activationCommand) {
	}

	void setRows(const std::vector<Row> &rows, const std::vector<bool> &errorFlags, short selection = 0) {
		compileErrorFlags = errorFlags;
		MRColumnListView::setRows(rows, selection);
	}

	void draw() override {
		TListBox::draw();

		TColorAttr errorColor;
		short indent = hScrollBar != nullptr ? hScrollBar->value : 0;
		short colWidth = size.x / numCols + 1;

		if (!configuredColorSlotOverride(kMrPaletteMessageError, errorColor)) return;

		for (short i = 0; i < size.y; ++i)
			for (short j = 0; j < numCols; ++j) {
				short item = static_cast<short>(j * size.y + i + topItem);
				short curCol = static_cast<short>(j * colWidth);
				char text[256];
				int visPos = -indent;
				TDrawBuffer mark;

				if (item < 0 || item >= range || !hasCompileError(item)) continue;
				getText(text, item, 255);
				if (text[0] != '!') continue;
				if (visPos < 0 || visPos >= colWidth - 1) continue;
				mark.moveChar(0, '!', errorColor, 1);
				writeLine(static_cast<short>(curCol + 1 + visPos), i, 1, 1, mark);
			}
	}

  private:
	bool hasCompileError(short item) const noexcept {
		return item >= 0 && static_cast<std::size_t>(item) < compileErrorFlags.size() && compileErrorFlags[static_cast<std::size_t>(item)];
	}

	std::vector<bool> compileErrorFlags;
};

std::string macroSpecFilePart(std::string_view target) {
	const std::size_t caretPos = target.find('^');

	return caretPos == std::string_view::npos ? std::string() : std::string(trimAscii(target.substr(0, caretPos)));
}

std::string macroSpecMacroPart(std::string_view target) {
	const std::size_t caretPos = target.find('^');

	if (caretPos == std::string_view::npos) return trimAscii(std::string(target));
	return trimAscii(std::string(target.substr(caretPos + 1)));
}

std::string composeMacroBindingTarget(std::string_view fileName, std::string_view macroName) {
	const std::string normalizedFile = trimAscii(std::string(fileName));
	const std::string normalizedMacro = trimAscii(std::string(macroName));

	if (normalizedFile.empty()) return normalizedMacro;
	if (normalizedMacro.empty()) return normalizedFile;
	return normalizedFile + "^" + normalizedMacro;
}

bool macroTargetMatchesChoice(std::string_view desiredTarget, const BindingTargetChoice &choice) {
	if (trimAscii(std::string(desiredTarget)).empty()) return false;
	if (choice.target == desiredTarget) return true;
	if (choice.target.find('^') == std::string::npos) return false;
	return upperAscii(macroSpecMacroPart(choice.target)) == upperAscii(std::string(trimAscii(desiredTarget)));
}

std::string bindingTargetDisplayName(const MRKeymapBindingRecord &binding) {
	if (binding.target.type == MRKeymapBindingType::Macro) return macroSpecMacroPart(binding.target.target);
	return stripTargetPrefix(binding.target.target);
}

std::string bindingDisplayDescription(const MRKeymapBindingRecord &binding, std::string description) {
	if (!description.empty()) return compactActionDescription(description);
	if (binding.target.type == MRKeymapBindingType::Macro) {
		const std::string filePart = macroSpecFilePart(binding.target.target);
		return filePart.empty() ? std::string("macro") : filePart;
	}
	return compactActionDescription(description);
}

std::vector<BindingTargetChoice> buildActionTargetChoices() {
	std::vector<BindingTargetChoice> choices;

	for (const MRKeymapActionDefinition &definition : MRKeymapActionCatalog::definitions())
		choices.push_back({std::string(definition.id), compactActionDescription(definition.displayName)});
	return choices;
}

void collectMacroRelativePathsRecursively(const std::string &macroDirectory, std::vector<std::string> &relativePaths) {
	namespace fs = std::filesystem;

	std::error_code ec;
	const fs::path root = fs::path(macroDirectory);

	if (macroDirectory.empty() || !fs::exists(root, ec) || !fs::is_directory(root, ec)) return;
	for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
		const fs::path fullPath = it->path();
		fs::path relativePath;

		if (ec) {
			ec.clear();
			continue;
		}
		if (!it->is_regular_file(ec)) {
			ec.clear();
			continue;
		}
		if (ec) {
			ec.clear();
			continue;
		}
		if (!mr::dialogs::hasMrmacExtension(fullPath.filename().string())) continue;
		relativePath = fs::relative(fullPath, root, ec);
		if (ec) {
			ec.clear();
			continue;
		}
		relativePaths.push_back(mr::dialogs::normalizeTvPathSeparators(relativePath.generic_string()));
	}
}

std::vector<BindingTargetChoice> buildMacroTargetChoices() {
	namespace fs = std::filesystem;

	std::vector<BindingTargetChoice> choices;
	std::set<std::string> seen;
	const std::string macroDirectory = configuredMacroDirectoryPath();
	std::vector<std::string> relativePaths;

	collectMacroRelativePathsRecursively(macroDirectory, relativePaths);
	for (const std::string &relativePath : relativePaths) {
		std::string source;
		const std::string fullPath = mr::dialogs::normalizeTvPathSeparators((fs::path(macroDirectory) / fs::path(relativePath)).generic_string());
		const std::string fileName = fs::path(relativePath).filename().string();

		if (!readTextFile(fullPath, source)) continue;
		std::size_t bytecodeSize = 0;
		unsigned char *bytecode = compile_macro_code(source.c_str(), &bytecodeSize);

		if (bytecode == nullptr) continue;
		for (int i = 0; i < get_compiled_macro_count(); ++i) {
			const char *compiledName = get_compiled_macro_name(i);
			const std::string macroName = trimAscii(compiledName != nullptr ? compiledName : "");
			const std::string bindingTarget = composeMacroBindingTarget(relativePath, macroName);
			if (macroName.empty() || bindingTarget.empty()) continue;
			const std::string seenKey = upperAscii(bindingTarget);
			if (!seen.insert(seenKey).second) continue;
			choices.push_back({bindingTarget, fileName});
		}
		std::free(bytecode);
	}
	std::sort(choices.begin(), choices.end(), [](const BindingTargetChoice &lhs, const BindingTargetChoice &rhs) { return upperAscii(macroSpecMacroPart(lhs.target)) < upperAscii(macroSpecMacroPart(rhs.target)); });
	return choices;
}

std::string makeUniqueProfileName(const KeymapManagerDraft &draft) {
	for (int index = 1;; ++index) {
		const std::string candidate = "PROFILE" + std::to_string(index);
		bool used = false;

		for (const MRKeymapProfile &profile : draft.profiles)
			if (upperAscii(profile.name) == candidate) {
				used = true;
				break;
			}
		if (!used) return candidate;
	}
}

bool chooseKeymapFileForLoad(std::string &selectedUri) {
	char fileName[kFileDialogPathSize] = {0};
	ushort result = cmCancel;

	mr::dialogs::seedFileDialogPath(MRDialogHistoryScope::KeymapProfileLoad, fileName, sizeof(fileName), "*.mrmac");
	result = mr::dialogs::execRememberingFileDialogWithData(MRDialogHistoryScope::KeymapProfileLoad, "*.mrmac", "LOAD KEYMAP PROFILE", "~N~ame", fdOpenButton, fileName);
	if (result == cmCancel) return false;
	selectedUri = normalizeConfiguredPathInput(fileName);
	return !selectedUri.empty();
}

bool chooseKeymapFileForSave(std::string &selectedUri) {
	char fileName[kFileDialogPathSize] = {0};
	ushort result = cmCancel;

	mr::dialogs::seedFileDialogPath(MRDialogHistoryScope::KeymapProfileSave, fileName, sizeof(fileName), "*.mrmac");
	result = mr::dialogs::execRememberingFileDialogWithData(MRDialogHistoryScope::KeymapProfileSave, "*.mrmac", "SAVE KEYMAP PROFILE AS", "~N~ame", fdOKButton, fileName);
	if (result == cmCancel) return false;
	selectedUri = mr::dialogs::ensureMrmacExtension(normalizeConfiguredPathInput(fileName));
	return !selectedUri.empty();
}

bool loadKeymapDraftFromFile(const std::string &path, KeymapManagerDraft &draft, std::string &errorText, std::vector<MRKeymapDiagnostic> *diagnosticsOut = nullptr) {
	std::string source;
	std::string readError;
	bool upgradeRequired = false;

	if (!readTextFile(path, source, readError)) {
		errorText = readError;
		return false;
	}
	if (!validateKeymapFileVersion(source, upgradeRequired, errorText)) return false;
	if (upgradeRequired) mrLogMessage("Keymap file version upgrade required: " + path);
	MRKeymapLoadResult result = loadKeymapProfilesFromSettingsSource(source);

	draft.profiles = result.profiles;
	draft.activeProfileName = result.activeProfileName;
	if (diagnosticsOut != nullptr) *diagnosticsOut = result.diagnostics;
	mrLogMessage("Keymap file loaded from '" + path + "'.");
	mrLogMessage(summarizeDraftForLog(draft));
	errorText.clear();
	return true;
}

MRKeymapCanonicalizationResult canonicalizeDraftForCommit(const KeymapManagerDraft &draft) {
	return canonicalizeKeymapProfiles(draft.profiles, draft.activeProfileName, MRKeymapCanonicalizationMode::TrustedCommit);
}

KeymapManagerDraft canonicalizedDraftForComparison(const KeymapManagerDraft &draft) {
	return draftFromCanonicalizedResult(canonicalizeDraftForCommit(draft));
}

void applyCommitCanonicalization(KeymapManagerDraft &draft, std::string_view operation) {
	const MRKeymapCanonicalizationResult canonicalized = canonicalizeDraftForCommit(draft);
	const std::string summary = summarizeKeymapDiagnosticsForMessageLine(canonicalized.diagnostics, operation);

	logKeymapDiagnostics(operation, canonicalized.profiles, canonicalized.diagnostics);
	draft = draftFromCanonicalizedResult(canonicalized);
	if (!summary.empty()) postDialogError(summary);
}

bool saveKeymapDraftToFile(const KeymapManagerDraft &draft, const std::string &path, std::string &errorText) {
	if (path.empty()) {
		errorText = "Keymap file path is empty.";
		return false;
	}
	if (!writeTextFile(path, buildSerializedKeymapFileSource(draft))) {
		errorText = "Could not write keymap file: " + path;
		return false;
	}
	mrLogMessage("Keymap file written to '" + path + "'.");
	mrLogMessage(summarizeDraftForLog(draft));
	errorText.clear();
	return true;
}

bool saveKeymapDraftToConfiguredState(const KeymapManagerDraft &draft, const std::string &fileUri, std::string &errorText) {
	const std::string previousFileUri = configuredKeymapFilePath();
	MRSettingsWriteReport writeReport;

	if (!runMacroFileByPath(fileUri.c_str(), &errorText, false)) return false;
	if (!setConfiguredKeymapFilePath(fileUri, &errorText)) return false;
	if (!syncConfiguredKeymapAutoexecEntry(fileUri, previousFileUri, errorText)) return false;
	if (!persistConfiguredSettingsSnapshot(&errorText, &writeReport)) return false;
	mrLogSettingsWriteReport("persist keymap settings", writeReport);
	mrLogMessage("Keymap dialog applied via VM macro execution.");
	mrLogMessage(summarizeDraftForLog(draft));
	errorText.clear();
	return true;
}

bool applyKeymapDraftToConfiguredState(const KeymapManagerDraft &draft, std::string &errorText) {
	const std::string source = buildSerializedKeymapFileSource(draft);

	if (!runMacroSourceText("Keymap dialog", source.c_str(), &errorText, false)) return false;
	mrLogMessage("Keymap dialog applied current draft via VM macro execution.");
	mrLogMessage(summarizeDraftForLog(draft));
	errorText.clear();
	return true;
}

bool doesKeymapDraftMatchFile(const KeymapManagerDraft &draft, const std::string &fileUri) {
	KeymapManagerDraft loadedDraft;
	std::string errorText;

	if (fileUri.empty()) return false;
	if (!loadKeymapDraftFromFile(fileUri, loadedDraft, errorText)) return false;
	return canonicalizedDraftForComparison(draft) == canonicalizedDraftForComparison(loadedDraft);
}

std::string initialKeymapManagerFileUri(const KeymapManagerDraft &draft) {
	const std::string configuredFileUri = configuredKeymapFilePath();

	if (!doesKeymapDraftMatchFile(draft, configuredFileUri)) return std::string();
	return configuredFileUri;
}

bool pathStartsWithParentTraversal(const std::filesystem::path &path) {
	for (const std::filesystem::path &part : path)
		if (part == "..") return true;
	return false;
}

bool tryRelativeAutoexecMacroEntryForPath(const std::string &fileUri, std::string &relativeEntry) {
	namespace fs = std::filesystem;

	const std::string macroDirectory = defaultMacroDirectoryPath();
	const std::string normalizedFileUri = normalizeConfiguredPathInput(fileUri);
	std::error_code ec;
	fs::path macroRoot;
	fs::path filePath;
	fs::path relativePath;

	relativeEntry.clear();
	if (macroDirectory.empty() || normalizedFileUri.empty()) return false;
	macroRoot = fs::weakly_canonical(fs::path(macroDirectory), ec);
	if (ec) return false;
	ec.clear();
	filePath = fs::weakly_canonical(fs::path(normalizedFileUri), ec);
	if (ec) return false;
	relativePath = filePath.lexically_relative(macroRoot);
	if (relativePath.empty() || relativePath.is_absolute() || pathStartsWithParentTraversal(relativePath)) return false;
	relativeEntry = mr::dialogs::normalizeTvPathSeparators(relativePath.generic_string());
	return !relativeEntry.empty();
}

bool resolvePersistentKeymapAutoexecEntry(const std::string &fileUri, std::string &relativeEntry, std::string &errorText) {
	if (!tryRelativeAutoexecMacroEntryForPath(fileUri, relativeEntry)) {
		errorText = "Persistent keymap files must live under MACROPATH.";
		return false;
	}
	if (!validateAutoexecMacroEntry(relativeEntry, &errorText)) return false;
	errorText.clear();
	return true;
}

bool syncConfiguredKeymapAutoexecEntry(const std::string &fileUri, const std::string &previousFileUri, std::string &errorText) {
	std::vector<std::string> autoexecEntries;
	std::string currentEntry;
	std::string previousEntry;

	if (!resolvePersistentKeymapAutoexecEntry(fileUri, currentEntry, errorText)) return false;
	configuredAutoexecMacroEntries(autoexecEntries);
	if (tryRelativeAutoexecMacroEntryForPath(previousFileUri, previousEntry) && upperAscii(previousEntry) != upperAscii(currentEntry))
		autoexecEntries.erase(std::remove_if(autoexecEntries.begin(), autoexecEntries.end(), [&](const std::string &entry) { return upperAscii(entry) == upperAscii(previousEntry); }), autoexecEntries.end());
	autoexecEntries.erase(std::remove_if(autoexecEntries.begin(), autoexecEntries.end(), [&](const std::string &entry) { return upperAscii(entry) == upperAscii(currentEntry); }), autoexecEntries.end());
	autoexecEntries.push_back(currentEntry);
	if (!setConfiguredAutoexecMacroEntries(autoexecEntries, &errorText)) return false;
	errorText.clear();
	return true;
}

std::vector<MRColumnListView::Row> buildProfileRows(const KeymapManagerDraft &draft) {
	std::vector<MRColumnListView::Row> rows;

	rows.reserve(draft.profiles.size());
	for (const MRKeymapProfile &profile : draft.profiles)
		rows.push_back({profile.name, profile.description});
	return rows;
}

bool bindingMatchesFilter(const MRColumnListView::Row &row, std::string_view filterText) {
	const std::string filter = upperAscii(trimAscii(filterText));

	if (filter.empty()) return true;
	for (const std::string &column : row)
		if (upperAscii(column).find(filter) != std::string::npos) return true;
	return false;
}

bool targetChoiceMatchesFilter(const BindingTargetChoice &choice, MRKeymapBindingType type, std::string_view filterText) {
	const std::string filter = upperAscii(trimAscii(filterText));
	std::array<std::string, 3> haystacks;

	if (filter.empty()) return true;
	haystacks[0] = upperAscii(choice.target);
	haystacks[1] = upperAscii(choice.label);
	haystacks[2] = type == MRKeymapBindingType::Macro ? upperAscii(macroSpecMacroPart(choice.target)) : upperAscii(stripTargetPrefix(choice.target));
	for (const std::string &haystack : haystacks)
		if (haystack.find(filter) != std::string::npos) return true;
	return false;
}

std::vector<MRColumnListView::Row> buildBindingRows(const MRKeymapProfile *profile, std::string_view filterText, std::vector<std::size_t> *visibleIndexes = nullptr, std::vector<bool> *errorFlags = nullptr) {
	std::vector<MRColumnListView::Row> rows;
	std::map<std::size_t, std::string> bindingErrors;

	if (profile == nullptr) return rows;
	if (visibleIndexes != nullptr) visibleIndexes->clear();
	if (errorFlags != nullptr) errorFlags->clear();
	{
		const MRKeymapCanonicalizationResult canonicalized = canonicalizeKeymapProfiles(std::span<const MRKeymapProfile>(profile, 1), profile->name, MRKeymapCanonicalizationMode::TrustedCommit);
		for (const MRKeymapDiagnostic &diagnostic : canonicalized.diagnostics)
			if (diagnostic.severity == MRKeymapDiagnosticSeverity::Error && diagnostic.profileIndex == 0 && diagnostic.bindingIndex != kNoIndex && bindingErrors.find(diagnostic.bindingIndex) == bindingErrors.end()) bindingErrors.emplace(diagnostic.bindingIndex, diagnostic.message);
	}
	rows.reserve(profile->bindings.size());
	for (std::size_t i = 0; i < profile->bindings.size(); ++i) {
		const MRKeymapBindingRecord &binding = profile->bindings[i];
		std::string description = binding.description;
		MRColumnListView::Row row;
		const auto errorIt = bindingErrors.find(i);
		const bool hasError = errorIt != bindingErrors.end();

		if (description.empty() && binding.target.type == MRKeymapBindingType::Action)
			if (const MRKeymapActionDefinition *action = MRKeymapActionCatalog::findById(binding.target.target)) description = std::string(action->description);
		if (hasError) description = errorIt->second;
		row = {std::string(hasError ? "! " : "  ") + bindingTargetDisplayName(binding), bindingDisplayDescription(binding, description), binding.sequence.toString()};
		if (!bindingMatchesFilter(row, filterText)) continue;
		rows.push_back(std::move(row));
		if (visibleIndexes != nullptr) visibleIndexes->push_back(i);
		if (errorFlags != nullptr) errorFlags->push_back(hasError);
	}
	return rows;
}

class TBindingEditorDialog : public MRDialogFoundation {
  public:
	TBindingEditorDialog(const MRKeymapBindingRecord &binding) : TWindowInit(initMrDialogFrame), MRDialogFoundation(centeredSetupDialogRect(96, 18), "EDIT BINDING", 96, 18, initMrDialogFrame), draft(binding) {
		helpCtx = hcDialogKeymapBinding;
		buildViews();
		if (frame != nullptr)
			if (MRFrame *mrFrame = dynamic_cast<MRFrame *>(frame))
				mrFrame->setMarkerStateProvider([this]() {
					MRFrame::MarkerState state;
					state.recording = recordingSequence;
					state.recordingVisible = recordingVisible;
					return state;
				});
		loadDraftToFields();
		setDialogValidationHook([this]() { return validateValues(); });
		finalizeLayout();
	}

	~TBindingEditorDialog() override {
		mr::messageline::clearOwner(mr::messageline::Owner::DialogInteraction);
	}

	ushort run(MRKeymapBindingRecord &outBinding) {
		const ushort result = TProgram::deskTop != nullptr ? TProgram::deskTop->execView(this) : cmCancel;
		saveFieldsToDraft();
		outBinding = draft;
		return result;
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evCommand && event.message.command == cmMrSetupKeymapBindingCapture) {
			toggleSequenceRecording();
			clearEvent(event);
			return;
		}
		if (event.what == evNothing && updateRecordingBlink()) return;
		if (handleSequenceRecordingEvent(event)) return;
		const ushort originalWhat = event.what;
		const ushort originalCommand = (event.what == evCommand || event.what == evBroadcast) ? event.message.command : 0;
		void *originalInfo = event.what == evBroadcast ? event.message.infoPtr : nullptr;

		MRDialogFoundation::handleEvent(event);
		if (originalWhat == evBroadcast && originalCommand == cmMrSetupKeymapBindingTargetFilterChanged && originalInfo == mTargetFilterField) {
			rebuildTargetChoices(true);
			runDialogValidation();
			clearEvent(event);
			return;
		}
		syncTargetChoicesToType(true);
		if (originalWhat != evCommand) return;
		switch (originalCommand) {
			case cmMrSetupKeymapBindingCapture:
				toggleSequenceRecording();
				clearEvent(event);
				return;
			default:
				break;
		}
	}

  private:
	static constexpr ushort kTypeActionIndex = 0;
	static constexpr ushort kTypeMacroIndex = 1;
	static constexpr ushort kContextMenuIndex = 0;
	static constexpr ushort kContextDialogIndex = 1;
	static constexpr ushort kContextDialogListIndex = 2;
	static constexpr ushort kContextListIndex = 3;
	static constexpr ushort kContextReadonlyIndex = 4;
	static constexpr ushort kContextEditIndex = 5;

	TStaticText *addLabel(const TRect &rect, const char *text) {
		TStaticText *view = new TStaticText(rect, text);
		insert(view);
		return view;
	}

	TInputLine *addInput(const TRect &rect, int maxLen) {
		TInputLine *view = new TInputLine(rect, maxLen);
		insert(view);
		return view;
	}

	TRadioButtons *addRadioGroup(const TRect &rect, TSItem *items) {
		TRadioButtons *view = new TRadioButtons(rect, items);
		insert(view);
		return view;
	}

	void buildViews() {
		const int left = 2;
		const int typeRight = 14;
		const int contextLeft = 16;
		const int contextRight = 30;
		const int targetLeft = 32;
		const int targetRight = 92;
		const int targetScrollLeft = 93;
		const int targetFilterLabelWidth = 8;
		const int targetFilterFieldWidth = 28;
		const int targetFilterFieldLeft = targetScrollLeft - targetFilterFieldWidth;
		const int targetFilterLabelLeft = targetFilterFieldLeft - targetFilterLabelWidth;
		const int sequenceFieldLeft = 18;
		const int sequenceGlyphLeft = 92;
		const int buttonTop = 15;
		const int buttonGap = 2;
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, buttonGap);
		const int buttonLeft = (96 - metrics.rowWidth) / 2;

		addLabel(TRect(left, 2, typeRight, 3), "Type:");
		mTypeGroup = addRadioGroup(TRect(left, 3, typeRight, 5), new TSItem("~A~ction", new TSItem("~M~acro", nullptr)));
		addLabel(TRect(contextLeft, 2, contextRight, 3), "Context:");
		mContextGroup = addRadioGroup(TRect(contextLeft, 3, contextRight, 9), new TSItem("~M~enu", new TSItem("~D~ialog", new TSItem("Li~s~tbox", new TSItem("~L~ist", new TSItem("~R~eadonly", new TSItem("~E~dit", nullptr)))))));
		addLabel(TRect(targetLeft, 2, targetLeft + 10, 3), "Target:");
		addLabel(TRect(targetFilterLabelLeft, 2, targetFilterFieldLeft, 3), "Filter:");
		mTargetFilterField = new TNotifyingInputLine(TRect(targetFilterFieldLeft, 2, targetScrollLeft, 3), kTargetFilterFieldSize - 1, cmMrSetupKeymapBindingTargetFilterChanged);
		insert(mTargetFilterField);
		mTargetScrollBar = new TScrollBar(TRect(targetScrollLeft, 3, targetScrollLeft + 1, 10));
		insert(mTargetScrollBar);
		mTargetList = new MRColumnListView(TRect(targetLeft, 3, targetScrollLeft, 10), mTargetScrollBar, nullptr, 0);
		insert(mTargetList);
		addLabel(TRect(left, 11, sequenceFieldLeft, 12), "Sequence:");
		mSequenceField = new TSequenceDisplay(TRect(sequenceFieldLeft, 11, sequenceGlyphLeft, 12));
		insert(mSequenceField);
		insert(new TInlineGlyphButton(TRect(sequenceGlyphLeft, 11, sequenceGlyphLeft + 2, 12), kBindingRecordingGlyph, cmMrSetupKeymapBindingCapture));
		addLabel(TRect(left, 12, sequenceFieldLeft, 13), "Description:");
		mDescriptionField = addInput(TRect(sequenceFieldLeft, 12, targetRight, 13), kBindingDescriptionFieldSize - 1);

		mr::dialogs::insertUniformButtonRow(*this, buttonLeft, buttonTop, buttonGap, buttons);
	}

	void setFieldText(TInputLine *field, std::string_view value, std::size_t capacity) {
		std::vector<char> buffer(capacity, '\0');
		mr::dialogs::writeRecordField(buffer.data(), buffer.size(), value);
		field->setData(buffer.data());
	}

	std::string fieldText(TInputLine *field, std::size_t capacity) const {
		std::vector<char> buffer(capacity, '\0');
		const_cast<TInputLine *>(field)->getData(buffer.data());
		return trimAscii(buffer.data());
	}

	void setRadioValue(TRadioButtons *field, ushort value) {
		field->setData(&value);
	}

	ushort radioValue(TRadioButtons *field) const {
		ushort value = 0;
		const_cast<TRadioButtons *>(field)->getData(&value);
		return value;
	}

	std::optional<MRKeymapBindingType> selectedType() const {
		switch (radioValue(mTypeGroup)) {
			case kTypeActionIndex:
				return MRKeymapBindingType::Action;
			case kTypeMacroIndex:
				return MRKeymapBindingType::Macro;
			default:
				return std::nullopt;
		}
	}

	std::optional<MRKeymapContext> selectedContext() const {
		switch (radioValue(mContextGroup)) {
			case kContextMenuIndex:
				return MRKeymapContext::Menu;
			case kContextDialogIndex:
				return MRKeymapContext::Dialog;
			case kContextDialogListIndex:
				return MRKeymapContext::DialogList;
			case kContextListIndex:
				return MRKeymapContext::List;
			case kContextReadonlyIndex:
				return MRKeymapContext::ReadOnly;
			case kContextEditIndex:
				return MRKeymapContext::Edit;
			default:
				return std::nullopt;
		}
	}

	ushort typeIndexFor(MRKeymapBindingType type) const noexcept {
		return type == MRKeymapBindingType::Macro ? kTypeMacroIndex : kTypeActionIndex;
	}

	ushort contextIndexFor(MRKeymapContext context) const noexcept {
		switch (context) {
			case MRKeymapContext::Menu:
				return kContextMenuIndex;
			case MRKeymapContext::Dialog:
				return kContextDialogIndex;
			case MRKeymapContext::DialogList:
				return kContextDialogListIndex;
			case MRKeymapContext::List:
				return kContextListIndex;
			case MRKeymapContext::ReadOnly:
				return kContextReadonlyIndex;
			case MRKeymapContext::Edit:
			default:
				return kContextEditIndex;
		}
	}

	void rebuildTargetChoices(bool preserveCurrentSelection) {
		std::string desiredTarget = preserveCurrentSelection ? selectedTarget() : draft.target.target;
		const auto type = selectedType().value_or(MRKeymapBindingType::Action);
		std::vector<MRColumnListView::Row> rows;
		short selected = 0;
		const std::string filter = targetFilterText();

		allTargetChoices = type == MRKeymapBindingType::Macro ? buildMacroTargetChoices() : buildActionTargetChoices();
		currentTargetChoices.clear();
		rows.reserve(allTargetChoices.size());
		for (const BindingTargetChoice &choice : allTargetChoices) {
			if (!targetChoiceMatchesFilter(choice, type, filter)) continue;
			currentTargetChoices.push_back(choice);
		}
		for (std::size_t i = 0; i < currentTargetChoices.size(); ++i) {
			const BindingTargetChoice &choice = currentTargetChoices[i];
			rows.push_back({type == MRKeymapBindingType::Macro ? macroSpecMacroPart(choice.target) : stripTargetPrefix(choice.target), choice.label});
			if (macroTargetMatchesChoice(desiredTarget, choice)) selected = static_cast<short>(i);
		}
		if (mTargetList != nullptr) mTargetList->setRows(rows, selected);
	}

	void syncTargetChoicesToType(bool preserveCurrentSelection) {
		const auto type = selectedType().value_or(MRKeymapBindingType::Action);
		if (lastSyncedType && *lastSyncedType == type) return;
		rebuildTargetChoices(preserveCurrentSelection);
		lastSyncedType = type;
	}

	std::string selectedTarget() const {
		const short index = mTargetList != nullptr ? mTargetList->selectedIndex() : -1;
		return index >= 0 && static_cast<std::size_t>(index) < currentTargetChoices.size() ? currentTargetChoices[static_cast<std::size_t>(index)].target : std::string();
	}

	void loadDraftToFields() {
		setRadioValue(mTypeGroup, typeIndexFor(draft.target.type));
		setRadioValue(mContextGroup, contextIndexFor(draft.context));
		setFieldText(mTargetFilterField, "", kTargetFilterFieldSize);
		rebuildTargetChoices(false);
		lastSyncedType = draft.target.type;
		mSequenceField->setText(draft.sequence.toString());
		setFieldText(mDescriptionField, draft.description, kBindingDescriptionFieldSize);
	}

	void saveFieldsToDraft() {
		const std::string sequenceText = mSequenceField->text();

		if (const auto type = selectedType()) draft.target.type = *type;
		if (const auto context = selectedContext()) draft.context = *context;
		draft.target.target = selectedTarget();
		if (const auto sequence = MRKeymapSequence::parse(sequenceText)) draft.sequence = *sequence;
		draft.description = fieldText(mDescriptionField, kBindingDescriptionFieldSize);
		if (draft.description.empty() && draft.target.type == MRKeymapBindingType::Action)
			if (const MRKeymapActionDefinition *action = MRKeymapActionCatalog::findById(draft.target.target)) draft.description = std::string(action->description);
	}

	DialogValidationResult validateValues() {
		DialogValidationResult result;
		const std::string sequenceText = mSequenceField->text();

		if (!selectedType()) {
			result.valid = false;
			result.warningText = "Select a binding type.";
			return result;
		}
		if (!selectedContext()) {
			result.valid = false;
			result.warningText = "Select a binding context.";
			return result;
		}
		if (allTargetChoices.empty()) {
			result.valid = false;
			result.warningText = "No target is available for the selected type.";
			return result;
		}
		if (currentTargetChoices.empty()) {
			result.valid = false;
			result.warningText = "No target matches the current filter.";
			return result;
		}
		if (selectedTarget().empty()) {
			result.valid = false;
			result.warningText = "Select a target.";
			return result;
		}
		if (recordingSequence) return result;
		if (!MRKeymapSequence::parse(sequenceText)) {
			result.valid = false;
			result.warningText = "Sequence is invalid.";
			return result;
		}
		return result;
	}

	bool handleSequenceRecordingEvent(TEvent &event) {
		if (!recordingSequence || event.what != evKeyDown) return false;
		const TKey pressed(event.keyDown);
		std::string token;

		if (pressed == TKey(kbEsc)) {
			recordingSequence = false;
			mr::messageline::clearOwner(mr::messageline::Owner::DialogInteraction);
			mSequenceField->setText(sequenceBeforeRecording);
			updateRecordingMarker();
			setDoneButtonDisabled(false);
			runDialogValidation();
			clearEvent(event);
			return true;
		}
		if (pressed == TKey(kbAltF10)) {
			finishSequenceRecording();
			clearEvent(event);
			return true;
		}
		if (!mrKeyTokenFromEvent(event.keyDown.keyCode, event.keyDown.controlKeyState, token)) {
			postDialogWarning("Unsupported key in sequence recording.");
			clearEvent(event);
			return true;
		}
		recordedSequenceTokens.push_back(token);
		mSequenceField->setText(joinRecordedSequence());
		runDialogValidation();
		clearEvent(event);
		return true;
	}

	void finishSequenceRecording() {
		recordingSequence = false;
		mr::messageline::clearOwner(mr::messageline::Owner::DialogInteraction);
		updateRecordingMarker();
		setDoneButtonDisabled(false);
		runDialogValidation();
	}

	std::string joinRecordedSequence() const {
		std::string text;

		for (const std::string &token : recordedSequenceTokens)
			text += token;
		return text;
	}

	void startSequenceRecording() {
		recordingSequence = true;
		sequenceBeforeRecording = mSequenceField->text();
		recordedSequenceTokens.clear();
		recordingVisible = true;
		recordingBlinkToggleAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
		updateRecordingMarker();
		mSequenceField->setText("");
		setDoneButtonDisabled(true);
		mr::messageline::postSticky(mr::messageline::Owner::DialogInteraction, "now recording key sequence, ALT-F10 ends, ESC aborts", mr::messageline::Kind::Warning, mr::messageline::kPriorityHigh);
	}

	void toggleSequenceRecording() {
		if (recordingSequence) {
			finishSequenceRecording();
			return;
		}
		startSequenceRecording();
	}

	bool updateRecordingBlink() {
		if (!recordingSequence) return false;
		const auto now = std::chrono::steady_clock::now();
		if (now < recordingBlinkToggleAt) return false;
		recordingVisible = !recordingVisible;
		recordingBlinkToggleAt = now + std::chrono::milliseconds(400);
		updateRecordingMarker();
		return false;
	}

	void updateRecordingMarker() {
		if (frame != nullptr) frame->drawView();
	}

	std::string targetFilterText() const {
		std::vector<char> buffer(kTargetFilterFieldSize + 1, '\0');

		if (mTargetFilterField == nullptr) return std::string();
		const_cast<TInputLine *>(mTargetFilterField)->getData(buffer.data());
		return trimAscii(buffer.data());
	}

	MRKeymapBindingRecord draft;
	TRadioButtons *mTypeGroup = nullptr;
	TRadioButtons *mContextGroup = nullptr;
	MRColumnListView *mTargetList = nullptr;
	TScrollBar *mTargetScrollBar = nullptr;
	TInputLine *mTargetFilterField = nullptr;
	TSequenceDisplay *mSequenceField = nullptr;
	TInputLine *mDescriptionField = nullptr;
	std::vector<BindingTargetChoice> allTargetChoices;
	std::vector<BindingTargetChoice> currentTargetChoices;
	std::optional<MRKeymapBindingType> lastSyncedType;
	bool recordingSequence = false;
	bool recordingVisible = false;
	std::string sequenceBeforeRecording;
	std::vector<std::string> recordedSequenceTokens;
	std::chrono::steady_clock::time_point recordingBlinkToggleAt;
};

class TProfileEditorDialog : public MRDialogFoundation {
  public:
	TProfileEditorDialog(const MRKeymapProfile &profile, std::vector<std::string> peerNames) : TWindowInit(initMrDialogFrame), MRDialogFoundation(centeredSetupDialogRect(74, 12), "EDIT PROFILE", 74, 12, initMrDialogFrame), draft(profile), peerProfileNames(std::move(peerNames)) {
		helpCtx = hcDialogKeymapProfile;
		buildViews();
		loadDraftToFields();
		setDialogValidationHook([this]() { return validateValues(); });
		finalizeLayout();
	}

	ushort run(MRKeymapProfile &outProfile) {
		const ushort result = TProgram::deskTop != nullptr ? TProgram::deskTop->execView(this) : cmCancel;
		saveFieldsToDraft();
		outProfile = draft;
		return result;
	}

  private:
	TStaticText *addLabel(const TRect &rect, const char *text) {
		TStaticText *view = new TStaticText(rect, text);
		insert(view);
		return view;
	}

	TInputLine *addInput(const TRect &rect, int maxLen) {
		TInputLine *view = new TInputLine(rect, maxLen);
		insert(view);
		return view;
	}

	void buildViews() {
		const int left = 2;
		const int fieldLeft = 18;
		const int fieldRight = 69;
		const int buttonTop = 7;
		const int buttonGap = 2;
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, buttonGap);
		const int buttonLeft = (72 - metrics.rowWidth) / 2;

		addLabel(TRect(left, 2, fieldLeft, 3), "Name:");
		mNameField = addInput(TRect(fieldLeft, 2, fieldRight, 3), kProfileNameFieldSize - 1);
		addLabel(TRect(left, 3, fieldLeft, 4), "Description:");
		mDescriptionField = addInput(TRect(fieldLeft, 3, fieldRight, 4), kProfileDescriptionFieldSize - 1);
		mr::dialogs::insertUniformButtonRow(*this, buttonLeft, buttonTop, buttonGap, buttons);
	}

	void setFieldText(TInputLine *field, std::string_view value, std::size_t capacity) {
		std::vector<char> buffer(capacity, '\0');
		mr::dialogs::writeRecordField(buffer.data(), buffer.size(), value);
		field->setData(buffer.data());
	}

	std::string fieldText(TInputLine *field, std::size_t capacity) const {
		std::vector<char> buffer(capacity, '\0');
		const_cast<TInputLine *>(field)->getData(buffer.data());
		return trimAscii(buffer.data());
	}

	void loadDraftToFields() {
		setFieldText(mNameField, draft.name, kProfileNameFieldSize);
		setFieldText(mDescriptionField, draft.description, kProfileDescriptionFieldSize);
	}

	void saveFieldsToDraft() {
		draft.name = fieldText(mNameField, kProfileNameFieldSize);
		draft.description = fieldText(mDescriptionField, kProfileDescriptionFieldSize);
	}

	DialogValidationResult validateValues() {
		DialogValidationResult result;
		const std::string name = fieldText(mNameField, kProfileNameFieldSize);

		if (name.empty()) {
			result.valid = false;
			result.warningText = "Profile name must not be empty.";
			return result;
		}
		for (const std::string &peerName : peerProfileNames)
			if (upperAscii(peerName) == upperAscii(name)) {
				result.valid = false;
				result.warningText = "Profile name already exists.";
				return result;
			}
		return result;
	}

	MRKeymapProfile draft;
	std::vector<std::string> peerProfileNames;
	TInputLine *mNameField = nullptr;
	TInputLine *mDescriptionField = nullptr;
};

class TKeymapManagerDialog : public MRScrollableDialog {
 public:
	TKeymapManagerDialog(const KeymapManagerDraft &baseline, const std::string &initialFileUri) : TWindowInit(initMrDialogFrame), MRScrollableDialog(centeredSetupDialogRect(kDialogWidth, kVisibleHeight), "KEY MANAGER", kDialogWidth, kVirtualHeight, initMrDialogFrame), persistedBaselineDraft(baseline), workingDraft(baseline), fileUri(initialFileUri), persistedFileUri(initialFileUri) {
		helpCtx = hcDialogKeymapManager;
		buildViews();
		setDialogValidationHook([this]() { return validateDialogValues(); });
		viewedProfileName = preferredViewedProfileName();
		refreshAllViews();
		initScrollIfNeeded();
		if (mProfileList != nullptr) mProfileList->select();
		scrollToOrigin();
	}

	~TKeymapManagerDialog() override {
		mr::messageline::clearOwner(mr::messageline::Owner::DialogInteraction);
	}

	ushort run(KeymapManagerDraft &outDraft, std::string &outFileUri, KeymapManagerDraft &outBaselineDraft, std::string &outBaselineFileUri) {
		const ushort result = TProgram::deskTop->execView(this);
		outDraft = workingDraft;
		outFileUri = fileUri;
		outBaselineDraft = persistedBaselineDraft;
		outBaselineFileUri = persistedFileUri;
		return result;
	}

	void handleEvent(TEvent &event) override {
		const ushort originalWhat = event.what;
		const ushort originalCommand = (event.what == evCommand || event.what == evBroadcast) ? event.message.command : 0;
		void *originalInfo = event.what == evBroadcast ? event.message.infoPtr : nullptr;

		if (event.what == evMouseWheel) {
			if (mProfileList != nullptr && mProfileList->handleWheel(event)) return;
			if (mBindingList != nullptr && mBindingList->handleWheel(event)) return;
		}

		MRScrollableDialog::handleEvent(event);
		if (originalWhat == evBroadcast && originalCommand == cmMrSetupKeymapProfileSelectionChanged && originalInfo == mProfileList) {
			selectViewedProfile();
			clearEvent(event);
			return;
		}
		if (originalWhat == evBroadcast && originalCommand == cmMrSetupKeymapBindingFilterChanged && originalInfo == mBindingFilterField) {
			refreshBindingList();
			clearEvent(event);
			return;
		}
		if (originalWhat != evCommand) return;
		switch (originalCommand) {
			case cmMrSetupKeymapProfileUse:
				useSelectedProfile();
				clearEvent(event);
				return;
			case cmMrSetupKeymapProfileAdd:
				addProfile();
				clearEvent(event);
				return;
			case cmMrSetupKeymapProfileEdit:
				editSelectedProfile();
				clearEvent(event);
				return;
			case cmMrSetupKeymapProfileDelete:
				deleteSelectedProfile();
				clearEvent(event);
				return;
			case cmMrSetupKeymapLoad:
				loadFromSelectedFile();
				clearEvent(event);
				return;
			case cmMrSetupKeymapSave:
				saveToSelectedFile();
				clearEvent(event);
				return;
			case cmMrSetupKeymapSaveAs:
				saveAs();
				clearEvent(event);
				return;
			case cmMrSetupKeymapBindingAdd:
				addBinding();
				clearEvent(event);
				return;
			case cmMrSetupKeymapBindingEdit:
				editSelectedBinding();
				clearEvent(event);
				return;
			case cmMrSetupKeymapBindingDelete:
				deleteSelectedBinding();
				clearEvent(event);
				return;
			default:
				break;
		}
	}

  private:
	TStaticText *addLabel(const TRect &rect, const char *text) {
		TStaticText *view = new TStaticText(rect, text);
		addManaged(view, rect);
		return view;
	}

	TInputLine *addInput(const TRect &rect, int maxLen) {
		TInputLine *view = new TInputLine(rect, maxLen);
		addManaged(view, rect);
		return view;
	}

	TScrollBar *addScrollBar(const TRect &rect) {
		TScrollBar *view = new TScrollBar(rect);
		addManaged(view, rect);
		return view;
	}

	void buildViews() {
		const int left = 2;
		const int right = kDialogWidth - 2;
		const int profilesWidth = 31;
		const int filterRow = 2;
		const int profileListTop = 3;
		const int bindingListTop = 3;
		const int listBottom = 15;
		const int bindingListBottom = listBottom - 1;
		const int topButtonRow = 16;
		const int profileScrollLeft = left + profilesWidth;
		const int bindingLeft = profileScrollLeft + 3;
		const int bindingScrollLeft = right - 1;
		const int buttonTop = 19;
		const int activeTop = 21;
		const int gap = 2;
		const int profileButtonGap = 1;
		const std::array profileButtons{mr::dialogs::DialogButtonSpec{"~U~se", cmMrSetupKeymapProfileUse, bfNormal}, mr::dialogs::DialogButtonSpec{"~A~dd", cmMrSetupKeymapProfileAdd, bfNormal}, mr::dialogs::DialogButtonSpec{"~R~en", cmMrSetupKeymapProfileEdit, bfNormal}, mr::dialogs::DialogButtonSpec{"De~l~", cmMrSetupKeymapProfileDelete, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics profileMetrics = mr::dialogs::measureUniformButtonRow(profileButtons, profileButtonGap);
		const int profileButtonLeft = left + std::max(0, (profilesWidth - profileMetrics.rowWidth) / 2);
		const int bindingButtonGap = 1;
		const std::array bindingButtons{mr::dialogs::DialogButtonSpec{"~N~ew", cmMrSetupKeymapBindingAdd, bfNormal}, mr::dialogs::DialogButtonSpec{"~E~dit", cmMrSetupKeymapBindingEdit, bfNormal}, mr::dialogs::DialogButtonSpec{"De~l~ete", cmMrSetupKeymapBindingDelete, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics bindingMetrics = mr::dialogs::measureUniformButtonRow(bindingButtons, bindingButtonGap);
		const int bindingButtonLeft = bindingLeft + std::max(0, ((bindingScrollLeft - bindingLeft) - bindingMetrics.rowWidth) / 2);
		const std::array bottomButtons{mr::dialogs::DialogButtonSpec{"~L~oad", cmMrSetupKeymapLoad, bfNormal}, mr::dialogs::DialogButtonSpec{"~S~ave", cmMrSetupKeymapSave, bfNormal}, mr::dialogs::DialogButtonSpec{"Save ~A~s", cmMrSetupKeymapSaveAs, bfNormal}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics bottomMetrics = mr::dialogs::measureUniformButtonRow(bottomButtons, gap);
		const int filterLabelWidth = 8;
		const int filterFieldWidth = 28;
		const int filterFieldLeft = std::max(bindingLeft + filterLabelWidth, bindingScrollLeft - filterFieldWidth);
		const int filterLabelLeft = filterFieldLeft - filterLabelWidth;
		int buttonLeft = 0;

		addLabel(TRect(left, 2, left + 10, 3), "Profiles:");
		mProfileScrollBar = addScrollBar(TRect(profileScrollLeft, profileListTop, profileScrollLeft + 1, listBottom));
		mProfileList = new MRColumnListView(TRect(left, profileListTop, profileScrollLeft, listBottom), mProfileScrollBar, this, cmMrSetupKeymapProfileSelectionChanged);
		addManaged(mProfileList, TRect(left, profileListTop, profileScrollLeft, listBottom));
		mr::dialogs::addManagedUniformButtonRow(*this, profileButtonLeft, topButtonRow, profileButtonGap, profileButtons);

		addLabel(TRect(bindingLeft, 2, bindingLeft + 10, 3), "Bindings:");
		addLabel(TRect(filterLabelLeft, filterRow, filterFieldLeft, filterRow + 1), "Filter:");
		mBindingFilterField = new TNotifyingInputLine(TRect(filterFieldLeft, filterRow, bindingScrollLeft, filterRow + 1), kBindingFilterFieldSize - 1, cmMrSetupKeymapBindingFilterChanged);
		addManaged(mBindingFilterField, TRect(filterFieldLeft, filterRow, bindingScrollLeft, filterRow + 1));
		mBindingScrollBar = addScrollBar(TRect(bindingScrollLeft, bindingListTop, bindingScrollLeft + 1, bindingListBottom));
		mBindingHorizontalScrollBar = addScrollBar(TRect(bindingLeft, bindingListBottom, bindingScrollLeft, bindingListBottom + 1));
		mBindingList = new KeymapBindingListView(TRect(bindingLeft, bindingListTop, bindingScrollLeft, bindingListBottom), mBindingScrollBar, mBindingHorizontalScrollBar, this, 0, cmMrSetupKeymapBindingEdit);
		addManaged(mBindingList, TRect(bindingLeft, bindingListTop, bindingScrollLeft, bindingListBottom));
		mr::dialogs::addManagedUniformButtonRow(*this, bindingButtonLeft, topButtonRow, bindingButtonGap, bindingButtons);

		buttonLeft = (kDialogWidth - bottomMetrics.rowWidth) / 2;
		mr::dialogs::addManagedUniformButtonRow(*this, buttonLeft, buttonTop, gap, bottomButtons);

		mActiveProfileField = new TActiveProfileField(TRect(5, activeTop, right, activeTop + 1), workingDraft.activeProfileName);
		addManaged(mActiveProfileField, TRect(5, activeTop, right, activeTop + 1));
	}

	void refreshAllViews() {
		refreshProfileList();
		refreshBindingList();
		refreshActiveProfile();
		runDialogValidation();
	}

	void refreshProfileList() {
		if (mProfileList == nullptr) return;
		normalizeViewedProfileName();
		const std::size_t index = viewedProfileIndex();
		const short selection = index == kNoIndex ? 0 : static_cast<short>(index);
		mProfileList->setRows(buildProfileRows(workingDraft), selection);
	}

	void refreshBindingList() {
		const std::size_t selectedIndexBeforeRefresh = selectedBindingIndex();
		std::vector<bool> rowErrorFlags;
		short selected = 0;

		if (mBindingList == nullptr) return;
		const std::size_t index = selectedProfileIndex();
		const MRKeymapProfile *profile = (index == kNoIndex || index >= workingDraft.profiles.size()) ? nullptr : &workingDraft.profiles[index];
		visibleBindingIndexes.clear();
		const std::vector<MRColumnListView::Row> rows = buildBindingRows(profile, bindingFilterText(), &visibleBindingIndexes, &rowErrorFlags);

		if (selectedIndexBeforeRefresh != kNoIndex) {
			const auto it = std::find(visibleBindingIndexes.begin(), visibleBindingIndexes.end(), selectedIndexBeforeRefresh);
			if (it != visibleBindingIndexes.end()) selected = static_cast<short>(std::distance(visibleBindingIndexes.begin(), it));
		}
		mBindingList->setRows(rows, rowErrorFlags, selected);
	}

	void refreshActiveProfile() {
		if (mActiveProfileField == nullptr) return;
		std::string activeText = workingDraft.activeProfileName;
		const std::size_t index = activeProfileIndex(workingDraft);
		if (index != kNoIndex && index < workingDraft.profiles.size()) {
			const std::string description = trimAscii(workingDraft.profiles[index].description);
			if (!description.empty()) activeText += " (" + description + ")";
		}
		mActiveProfileField->setText(activeText);
	}

	void selectViewedProfile() {
		const short index = mProfileList != nullptr ? mProfileList->selectedIndex() : -1;

		if (index < 0 || index >= static_cast<short>(workingDraft.profiles.size())) return;
		viewedProfileName = workingDraft.profiles[index].name;
		refreshBindingList();
		runDialogValidation();
	}

	void useSelectedProfile() {
		const std::size_t index = selectedProfileIndex();

		if (index == kNoIndex || index >= workingDraft.profiles.size()) return;
		workingDraft.activeProfileName = workingDraft.profiles[index].name;
		refreshActiveProfile();
		runDialogValidation();
	}

	DialogValidationResult validateDialogValues() const {
		DialogValidationResult result;
		if (workingDraft.profiles.empty()) {
			result.valid = false;
			result.warningText = "At least one keymap profile is required.";
			return result;
		}
		return result;
	}

	std::size_t selectedProfileIndex() const {
		const short index = mProfileList != nullptr ? mProfileList->selectedIndex() : -1;
		return index < 0 ? kNoIndex : static_cast<std::size_t>(index);
	}

	std::size_t profileIndexByName(const std::string &name) const {
		for (std::size_t i = 0; i < workingDraft.profiles.size(); ++i)
			if (workingDraft.profiles[i].name == name) return i;
		return kNoIndex;
	}

	std::string preferredViewedProfileName() const {
		if (profileIndexByName(workingDraft.activeProfileName) != kNoIndex) return workingDraft.activeProfileName;
		if (!workingDraft.profiles.empty()) return workingDraft.profiles.front().name;
		return std::string();
	}

	void normalizeViewedProfileName() {
		if (profileIndexByName(viewedProfileName) != kNoIndex) return;
		viewedProfileName = preferredViewedProfileName();
	}

	std::size_t viewedProfileIndex() const {
		return profileIndexByName(viewedProfileName);
	}

	void suspendVisualFocus() {
		TView *dialogFrame = frame;

		if (visualFocusSuspended) return;
		visualFocusWasActive = (state & sfActive) != 0;
		visualFocusWasFocused = (state & sfFocused) != 0;
		visualFrameWasActive = dialogFrame != nullptr && (dialogFrame->state & sfActive) != 0;
		visualFrameWasFocused = dialogFrame != nullptr && (dialogFrame->state & sfFocused) != 0;
		visualFocusedView = current;
		visualFocusedViewWasActive = visualFocusedView != nullptr && (visualFocusedView->state & sfActive) != 0;
		visualFocusedViewWasFocused = visualFocusedView != nullptr && (visualFocusedView->state & sfFocused) != 0;
		visualFocusSuspended = true;
		if (visualFocusWasActive) setState(sfActive, False);
		if (visualFocusWasFocused) setState(sfFocused, False);
		if (visualFocusedView != nullptr) {
			if (visualFocusedViewWasFocused) visualFocusedView->setState(sfFocused, False);
			if (visualFocusedViewWasActive) visualFocusedView->setState(sfActive, False);
		}
		if (dialogFrame != nullptr) {
			if (visualFrameWasFocused) dialogFrame->setState(sfFocused, False);
			if (visualFrameWasActive) dialogFrame->setState(sfActive, False);
		}
		if (dialogFrame != nullptr) dialogFrame->drawView();
		drawView();
		if (TProgram::deskTop != nullptr) TProgram::deskTop->drawView();
	}

	void resumeVisualFocus() {
		TView *dialogFrame = frame;

		if (!visualFocusSuspended) return;
		if (visualFocusWasActive && (state & sfActive) == 0) setState(sfActive, True);
		if (visualFocusWasFocused && (state & sfFocused) == 0) setState(sfFocused, True);
		if (visualFocusedView != nullptr) {
			if (visualFocusedViewWasActive && (visualFocusedView->state & sfActive) == 0) visualFocusedView->setState(sfActive, True);
			if (visualFocusedViewWasFocused && (visualFocusedView->state & sfFocused) == 0) visualFocusedView->setState(sfFocused, True);
		}
		if (dialogFrame != nullptr) {
			if (visualFrameWasActive && (dialogFrame->state & sfActive) == 0) dialogFrame->setState(sfActive, True);
			if (visualFrameWasFocused && (dialogFrame->state & sfFocused) == 0) dialogFrame->setState(sfFocused, True);
		}
		visualFocusSuspended = false;
		visualFocusedView = nullptr;
		if (dialogFrame != nullptr) dialogFrame->drawView();
		drawView();
		if (TProgram::deskTop != nullptr) TProgram::deskTop->drawView();
	}

	bool editProfileWithDialog(MRKeymapProfile &profile, std::size_t editedIndex) {
		std::vector<std::string> peerNames;
		TProfileEditorDialog *dialog;
		MRKeymapProfile edited;
		ushort result;

		peerNames.reserve(workingDraft.profiles.size());
		for (std::size_t i = 0; i < workingDraft.profiles.size(); ++i)
			if (i != editedIndex) peerNames.push_back(workingDraft.profiles[i].name);
		dialog = new TProfileEditorDialog(profile, peerNames);
		edited = profile;
		suspendVisualFocus();
		result = dialog != nullptr ? dialog->run(edited) : cmCancel;
		resumeVisualFocus();
		if (dialog != nullptr) TObject::destroy(dialog);
		if (result == cmOK) {
			const std::string previousName = profile.name;
			profile = std::move(edited);
			if (profile.name != previousName)
				for (MRKeymapBindingRecord &binding : profile.bindings)
					binding.profileName = profile.name;
			return true;
		}
		return false;
	}

	void addProfile() {
		MRKeymapProfile profile;

		profile.name = makeUniqueProfileName(workingDraft);
		profile.description = "Custom keymap profile";
		if (!editProfileWithDialog(profile, kNoIndex)) return;
		workingDraft.profiles.push_back(std::move(profile));
		viewedProfileName = workingDraft.profiles.back().name;
		refreshAllViews();
	}

	void editSelectedProfile() {
		const std::size_t profileIndex = selectedProfileIndex();
		if (profileIndex == kNoIndex || profileIndex >= workingDraft.profiles.size()) return;

		const std::string previousName = workingDraft.profiles[profileIndex].name;
		MRKeymapProfile edited = workingDraft.profiles[profileIndex];
		if (!editProfileWithDialog(edited, profileIndex)) return;
		workingDraft.profiles[profileIndex] = std::move(edited);
		if (workingDraft.activeProfileName == previousName) workingDraft.activeProfileName = workingDraft.profiles[profileIndex].name;
		if (viewedProfileName == previousName) viewedProfileName = workingDraft.profiles[profileIndex].name;
		refreshAllViews();
	}

	void deleteSelectedProfile() {
		const std::size_t profileIndex = selectedProfileIndex();
		if (profileIndex == kNoIndex || profileIndex >= workingDraft.profiles.size()) return;
		const MRKeymapProfile &profile = workingDraft.profiles[profileIndex];
		std::string nextViewedProfileName;

		if (!mr::dialogs::runDialogConfirm("Delete profile?", "Delete", profile.name.c_str())) return;
		if (workingDraft.profiles.size() > 1) {
			const std::size_t nextIndex = profileIndex + 1 < workingDraft.profiles.size() ? profileIndex + 1 : profileIndex - 1;
			nextViewedProfileName = workingDraft.profiles[nextIndex].name;
		}
		workingDraft.profiles.erase(workingDraft.profiles.begin() + static_cast<std::ptrdiff_t>(profileIndex));
		if (activeProfileIndex(workingDraft) == kNoIndex) workingDraft.activeProfileName.clear();
		viewedProfileName = nextViewedProfileName;
		normalizeViewedProfileName();
		refreshAllViews();
	}

	std::size_t selectedBindingIndex() const {
		const short index = mBindingList != nullptr ? mBindingList->selectedIndex() : -1;
		const std::size_t visibleIndex = index < 0 ? kNoIndex : static_cast<std::size_t>(index);

		if (visibleIndex == kNoIndex || visibleIndex >= visibleBindingIndexes.size()) return kNoIndex;
		return visibleBindingIndexes[visibleIndex];
	}

	std::string bindingFilterText() const {
		std::vector<char> buffer(kBindingFilterFieldSize + 1, '\0');

		if (mBindingFilterField == nullptr) return std::string();
		const_cast<TInputLine *>(mBindingFilterField)->getData(buffer.data());
		return trimAscii(buffer.data());
	}

	bool editBindingWithDialog(MRKeymapBindingRecord &binding) {
		TBindingEditorDialog *dialog = new TBindingEditorDialog(binding);
		MRKeymapBindingRecord edited = binding;
		ushort result = cmCancel;

		suspendVisualFocus();
		result = dialog != nullptr ? dialog->run(edited) : cmCancel;
		resumeVisualFocus();
		if (dialog != nullptr) TObject::destroy(dialog);
		if (result == cmOK) {
			binding = std::move(edited);
			return true;
		}
		return false;
	}

	void addBinding() {
		const std::size_t profileIndex = selectedProfileIndex();
		if (profileIndex == kNoIndex || profileIndex >= workingDraft.profiles.size()) return;

		MRKeymapBindingRecord binding;
		binding.profileName = workingDraft.profiles[profileIndex].name;
		binding.context = MRKeymapContext::Edit;
		binding.target.type = MRKeymapBindingType::Action;
		binding.target.target = "MRMAC_FILE_SAVE";
		binding.sequence = *MRKeymapSequence::parse("<F2>");
		binding.description = "Save file";
		if (!editBindingWithDialog(binding)) return;
		workingDraft.profiles[profileIndex].bindings.push_back(std::move(binding));
		refreshBindingList();
		runDialogValidation();
	}

	void editSelectedBinding() {
		const std::size_t profileIndex = selectedProfileIndex();
		const std::size_t bindingIndex = selectedBindingIndex();
		if (profileIndex == kNoIndex || profileIndex >= workingDraft.profiles.size() || bindingIndex == kNoIndex || bindingIndex >= workingDraft.profiles[profileIndex].bindings.size()) return;

		MRKeymapBindingRecord edited = workingDraft.profiles[profileIndex].bindings[bindingIndex];
		if (!editBindingWithDialog(edited)) return;
		workingDraft.profiles[profileIndex].bindings[bindingIndex] = std::move(edited);
		refreshBindingList();
		runDialogValidation();
	}

	void deleteSelectedBinding() {
		const std::size_t profileIndex = selectedProfileIndex();
		const std::size_t bindingIndex = selectedBindingIndex();
		if (profileIndex == kNoIndex || profileIndex >= workingDraft.profiles.size() || bindingIndex == kNoIndex || bindingIndex >= workingDraft.profiles[profileIndex].bindings.size()) return;

		const MRKeymapBindingRecord &binding = workingDraft.profiles[profileIndex].bindings[bindingIndex];
		if (!mr::dialogs::runDialogConfirm("Delete binding?", "Delete", binding.target.target.c_str())) return;
		workingDraft.profiles[profileIndex].bindings.erase(workingDraft.profiles[profileIndex].bindings.begin() + static_cast<std::ptrdiff_t>(bindingIndex));
		refreshBindingList();
		runDialogValidation();
	}

	void loadFromSelectedFile() {
		KeymapManagerDraft loadedDraft;
		std::vector<MRKeymapDiagnostic> diagnostics;
		std::string errorText;
		std::string selectedUri;

		if (!chooseKeymapFileForLoad(selectedUri)) return;
		if (!loadKeymapDraftFromFile(selectedUri, loadedDraft, errorText, &diagnostics)) {
			const std::string loadError = errorText;

			forgetLoadDialogPath(MRDialogHistoryScope::KeymapProfileLoad, selectedUri.c_str());
			postDialogError(loadError);
			return;
		}
		rememberLoadDialogPath(MRDialogHistoryScope::KeymapProfileLoad, selectedUri.c_str());
		logKeymapDiagnostics("keymap load", loadedDraft.profiles, diagnostics);
		if (const std::string summary = summarizeKeymapDiagnosticsForMessageLine(diagnostics, "Keymap load"); !summary.empty()) postDialogError(summary);
		fileUri = selectedUri;
		workingDraft = loadedDraft;
		viewedProfileName = preferredViewedProfileName();
		refreshAllViews();
	}

	void saveToSelectedFile() {
		std::string errorText;
		std::string autoexecEntry;

		if (fileUri.empty()) {
			saveAs();
			return;
		}
		applyCommitCanonicalization(workingDraft, "Keymap save");
		refreshAllViews();
		fileUri = mr::dialogs::ensureMrmacExtension(fileUri);
		if (!resolvePersistentKeymapAutoexecEntry(fileUri, autoexecEntry, errorText)) {
			postDialogError(errorText);
			return;
		}
		if (!confirmOverwriteForPath("Overwrite", "Keymap file exists. Overwrite?", fileUri)) return;
		if (!saveKeymapDraftToFile(workingDraft, fileUri, errorText)) {
			postDialogError(errorText);
			return;
		}
		if (!saveKeymapDraftToConfiguredState(workingDraft, fileUri, errorText)) {
			postDialogError(errorText);
			return;
		}
		persistedBaselineDraft = workingDraft;
		persistedFileUri = fileUri;
	}

	void saveAs() {
		std::string selectedUri;
		std::string errorText;
		std::string autoexecEntry;

		if (!chooseKeymapFileForSave(selectedUri)) return;
		applyCommitCanonicalization(workingDraft, "Keymap save");
		refreshAllViews();
		fileUri = selectedUri;
		if (!resolvePersistentKeymapAutoexecEntry(fileUri, autoexecEntry, errorText)) {
			postDialogError(errorText);
			return;
		}
		if (!confirmOverwriteForPath("Overwrite", "Keymap file exists. Overwrite?", fileUri)) return;
		if (!saveKeymapDraftToFile(workingDraft, fileUri, errorText)) {
			postDialogError(errorText);
			return;
		}
		if (!saveKeymapDraftToConfiguredState(workingDraft, fileUri, errorText)) {
			postDialogError(errorText);
			return;
		}
		persistedBaselineDraft = workingDraft;
		persistedFileUri = fileUri;
	}

	KeymapManagerDraft persistedBaselineDraft;
	KeymapManagerDraft workingDraft;
	std::string fileUri;
	std::string persistedFileUri;
	std::string viewedProfileName;
	MRColumnListView *mProfileList = nullptr;
	TScrollBar *mProfileScrollBar = nullptr;
	KeymapBindingListView *mBindingList = nullptr;
	TScrollBar *mBindingScrollBar = nullptr;
	TScrollBar *mBindingHorizontalScrollBar = nullptr;
	TInputLine *mBindingFilterField = nullptr;
	TActiveProfileField *mActiveProfileField = nullptr;
	std::vector<std::size_t> visibleBindingIndexes;
	bool visualFocusSuspended = false;
	bool visualFocusWasActive = false;
	bool visualFocusWasFocused = false;
	bool visualFrameWasActive = false;
	bool visualFrameWasFocused = false;
	TView *visualFocusedView = nullptr;
	bool visualFocusedViewWasActive = false;
	bool visualFocusedViewWasFocused = false;
};

KeymapManagerDraft currentConfiguredKeymapDraft() {
	KeymapManagerDraft draft;

	draft.profiles = configuredKeymapProfiles();
	draft.activeProfileName = configuredActiveKeymapProfile();
	mrLogMessage("Keymap dialog opened from configured state.");
	mrLogMessage(summarizeDraftForLog(draft));
	return draft;
}

} // namespace

void runKeymapManagerDialogFlow() {
	KeymapManagerDraft baselineDraft = currentConfiguredKeymapDraft();
	KeymapManagerDraft workingDraft = baselineDraft;
	std::string currentFileUri = initialKeymapManagerFileUri(baselineDraft);
	std::string persistedFileUri = currentFileUri;
	bool running = true;

	while (running) {
		TKeymapManagerDialog *dialog = new TKeymapManagerDialog(workingDraft, currentFileUri);
		ushort result = cmCancel;
		std::string errorText;

		if (dialog == nullptr) return;
		result = dialog->run(workingDraft, currentFileUri, baselineDraft, persistedFileUri);
		TObject::destroy(dialog);
		const bool changed = mr::dialogs::isDialogDraftDirty(baselineDraft, workingDraft, [](const auto &lhs, const auto &rhs) { return lhs == rhs; });

		switch (result) {
			case cmOK:
				applyCommitCanonicalization(workingDraft, "Keymap done");
				if (!applyKeymapDraftToConfiguredState(workingDraft, errorText)) {
					postDialogError(errorText);
					break;
				}
				baselineDraft = workingDraft;
				persistedFileUri = currentFileUri;
				running = false;
				break;
			case cmCancel:
			case cmClose:
				if (changed) {
					const mr::dialogs::UnsavedChangesChoice choice = mr::dialogs::runDialogDirtyGating("Discard changed keymap profiles?");
					if (choice == mr::dialogs::UnsavedChangesChoice::Save) {
						applyCommitCanonicalization(workingDraft, "Keymap done");
						if (!currentFileUri.empty()) {
							if (!saveKeymapDraftToFile(workingDraft, currentFileUri, errorText)) {
								postDialogError(errorText);
								break;
							}
							if (!saveKeymapDraftToConfiguredState(workingDraft, currentFileUri, errorText)) {
								postDialogError(errorText);
								break;
							}
						} else {
							if (!applyKeymapDraftToConfiguredState(workingDraft, errorText)) {
								postDialogError(errorText);
								break;
							}
						}
						baselineDraft = workingDraft;
						persistedFileUri = currentFileUri;
						running = false;
						break;
					}
					if (choice == mr::dialogs::UnsavedChangesChoice::Cancel) break;
				}
				running = false;
				break;
			default:
				running = false;
				break;
		}
	}
}
