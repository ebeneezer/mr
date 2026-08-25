#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TObject
#define Uses_TEvent
#define Uses_TRect
#define Uses_TView
#define Uses_TButton
#define Uses_TInputLine
#define Uses_TLabel
#define Uses_TListViewer
#define Uses_TKeys
#define Uses_TDrawBuffer
#define Uses_TCheckBoxes
#define Uses_TScrollBar
#define Uses_TStaticText
#define Uses_TSItem
#include <tvision/tv.h>

#include "MRCommandRouterSearchMultiFileCollect.hpp"
#include "MRCommandRouterSearchMultiFileSession.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../dialogs/setup/MRSetup.hpp"
#include "../../dialogs/setup/MRSetupCommon.hpp"
#include "../../ui/MRFrame.hpp"
#include "../../ui/widgets/MRNumericSlider.hpp"
#include "../MRCommands.hpp"
#include "../MRHelpTopics.generated.hpp"

namespace {

TFrame *initMrDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}

enum : ushort {
	cmMrMultiFileSelectionChanged = 4901,
	cmMrMultiFileMatchPrev = 4902,
	cmMrMultiFileMatchNext = 4903,
	cmMrMultiLoad = 4904,
	cmMrMultiLoadAll = 4905,
	cmMrMultiLoadKeepDialog = 4906,
	cmMrMultiDone = 4951,
	cmMrMultiReplace = 4952,
	cmMrMultiReplaceAll = 4953,
	cmMrMultiSkip = 4954
};

constexpr const char *kSearchTextRequiredMessage = "Search text must not be empty.";

TAttrPair inactiveDialogColor(TView *view) {
	TColorAttr configured;

	if (configuredColorSlotOverride(kMrPaletteDialogInactiveElements, configured)) return TAttrPair(configured);
	return view != nullptr ? view->getColor(1) : TAttrPair(0x70);
}

class GhostedInputLine final : public TInputLine {
  public:
	GhostedInputLine(const TRect &bounds, int maxLen) noexcept : TInputLine(bounds, maxLen) {
	}

	void draw() override {
		if ((state & sfDisabled) == 0) {
			TInputLine::draw();
			return;
		}

		TDrawBuffer buffer;
		const TAttrPair color = inactiveDialogColor(this);

		buffer.moveChar(0, ' ', color, size.x);
		if (size.x > 1) buffer.moveStr(1, data, color, size.x - 1, firstPos);
		writeLine(0, 0, size.x, size.y, buffer);
		setCursor(0, 0);
	}

	void setState(ushort aState, Boolean enable) override {
		const ushort oldState = state;

		TInputLine::setState(aState, enable);
		if (oldState != state && (aState & (sfFocused | sfDisabled | sfSelected | sfActive))) drawView();
	}
};

class GhostedLabel final : public TLabel {
  public:
	GhostedLabel(const TRect &bounds, const char *text, TView *link) noexcept : TLabel(bounds, text, link) {
	}

	void draw() override {
		if ((state & sfDisabled) == 0) {
			TLabel::draw();
			return;
		}

		TDrawBuffer buffer;
		const TAttrPair inactive = inactiveDialogColor(this);
		const TAttrPair color(inactive[0], inactive[0]);

		buffer.moveChar(0, ' ', inactive, size.x);
		if (text != nullptr) buffer.moveCStr(1, text, color, size.x > 1 ? size.x - 1 : 0);
		if (showMarkers) buffer.putChar(0, specialChars[4]);
		writeLine(0, 0, size.x, 1, buffer);
	}

	void setState(ushort aState, Boolean enable) override {
		const ushort oldState = state;

		TLabel::setState(aState, enable);
		if (oldState != state && (aState & (sfFocused | sfDisabled | sfSelected | sfActive))) drawView();
	}
};

class WorkspaceOptionsCheckBoxes final : public TCheckBoxes {
  public:
	WorkspaceOptionsCheckBoxes(const TRect &bounds, TSItem *items, ushort restrictMask) noexcept : TCheckBoxes(bounds, items), restrictMask(restrictMask) {
	}

	void setStartPathViews(TLabel *label, TInputLine *field) noexcept {
		startPathLabel = label;
		startPathField = field;
		updateStartPathState();
	}

	void press(int item) override {
		TCheckBoxes::press(item);
		updateStartPathState();
	}

	void setData(void *record) override {
		TCheckBoxes::setData(record);
		updateStartPathState();
	}

	void updateStartPathState() {
		const Boolean disabled = (value & restrictMask) != 0 ? True : False;

		if (startPathLabel != nullptr) startPathLabel->setState(sfDisabled, disabled);
		if (startPathField != nullptr) startPathField->setState(sfDisabled, disabled);
	}

  private:
	const ushort restrictMask;
	TLabel *startPathLabel = nullptr;
	TInputLine *startPathField = nullptr;
};

class SubmitInterceptDialog : public MRDialogFoundation {
  public:
	using SubmitHook = std::function<void()>;
	using ResultHook = std::function<void(const mr::coprocessor::Result &)>;

	SubmitInterceptDialog(const char *title, int virtualWidth, int virtualHeight) : TWindowInit(initMrDialogFrame), MRDialogFoundation(mr::dialogs::centeredDialogRect(virtualWidth, virtualHeight), title, virtualWidth, virtualHeight, initMrDialogFrame) {
		eventMask |= evBroadcast;
	}

	void setSubmitHook(SubmitHook hook) {
		submitHook = std::move(hook);
	}

	void setResultHook(ResultHook hook) {
		resultHook = std::move(hook);
	}

	void setSubmitButton(TButton *button) noexcept {
		submitButton = button;
	}

	void setProgressView(MRProgressSlider *view) noexcept {
		progressView = view;
	}

	void addTaskBlockedView(TView *view) {
		if (view != nullptr) taskBlockedViews.push_back(view);
	}

	void beginTask(std::uint64_t id) {
		taskId = id;
		cancelRequested = false;
		if (taskId == 0) {
			clearProgress();
			postSearchError("Unable to start search.");
			return;
		}
		if (progressView != nullptr) progressView->setProgress(0, 0, "0/?");
		setSubmitTitle("~C~ancel");
		for (TView *view : taskBlockedViews)
			view->setState(sfDisabled, True);
	}

	void finishTask() {
		taskId = 0;
		cancelRequested = false;
		setSubmitTitle("~S~earch");
		if (submitButton != nullptr) submitButton->setState(sfDisabled, False);
		for (TView *view : taskBlockedViews)
			view->setState(sfDisabled, False);
		if (closeAfterTask) endModal(cmCancel);
	}

	void clearProgress() {
		if (progressView != nullptr) progressView->setText(std::string());
	}

	[[nodiscard]] bool closeRequested() const noexcept {
		return closeAfterTask;
	}

	Boolean valid(ushort command) override {
		if ((command == cmCancel || command == cmClose) && taskId != 0) {
			closeAfterTask = true;
			cancelTask();
			return False;
		}
		return MRDialogFoundation::valid(command);
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evBroadcast && event.message.command == cmMrCoprocessorDialogResult && event.message.infoPtr != nullptr) {
			const mr::coprocessor::Result &result = *static_cast<const mr::coprocessor::Result *>(event.message.infoPtr);
			if (taskId != 0 && result.task.id == taskId) {
				const mr::coprocessor::TaskProgressPayload *progress = dynamic_cast<const mr::coprocessor::TaskProgressPayload *>(result.payload.get());
				if (progress != nullptr) {
					updateProgress(*progress);
				} else if (resultHook)
					resultHook(result);
				clearEvent(event);
				return;
			}
		}
		if (event.what == evCommand && event.message.command == cmOK) {
			clearEvent(event);
			if (taskId != 0) cancelTask();
			else if (submitHook)
				submitHook();
			return;
		}
		MRDialogFoundation::handleEvent(event);
	}

  private:
	void setSubmitTitle(const char *title) {
		if (submitButton == nullptr || title == nullptr) return;
		delete[] const_cast<char *>(submitButton->title);
		submitButton->title = newStr(title);
		submitButton->drawView();
	}

	void cancelTask() {
		if (taskId == 0 || cancelRequested) return;
		if (!mr::coprocessor::globalCoprocessor().cancelTask(taskId)) return;
		cancelRequested = true;
		if (submitButton != nullptr) submitButton->setState(sfDisabled, True);
	}

	void updateProgress(const mr::coprocessor::TaskProgressPayload &progress) {
		if (progress.phase == "Collecting") {
			if (progressView != nullptr) progressView->setProgress(progress.completedUnits, 0, std::to_string(progress.completedUnits) + "/?");
			return;
		}
		if (progressView != nullptr)
			progressView->setProgress(progress.completedUnits, progress.totalUnits, std::to_string(progress.completedUnits) + "/" + std::to_string(progress.totalUnits));
	}

	SubmitHook submitHook;
	ResultHook resultHook;
	std::vector<TView *> taskBlockedViews;
	TButton *submitButton = nullptr;
	MRProgressSlider *progressView = nullptr;
	std::uint64_t taskId = 0;
	bool cancelRequested = false;
	bool closeAfterTask = false;
};

struct MultiPreviewLine {
	std::string text;
	std::size_t highlightStart = 0;
	std::size_t highlightEnd = 0;
};

struct MultiPreviewBlock {
	std::vector<MultiPreviewLine> lines;
	std::size_t matchLineIndex = 0;
};

std::string sanitizePreviewLine(std::string value) {
	for (char &ch : value) {
		unsigned char uch = static_cast<unsigned char>(ch);
		if (ch == '\t' || ch == '\r' || ch == '\n' || uch < 32 || uch >= 127) ch = ' ';
	}
	return value;
}

bool buildMultiPreviewBlock(const std::string &text, const SearchMatchEntry &match, std::size_t rowCount, MultiPreviewBlock &out) {
	const std::size_t safeStart = std::min(match.start, text.size());
	const std::size_t safeEnd = std::min(std::max(match.end, safeStart), text.size());
	std::size_t lineStart = safeStart;
	std::size_t lineEnd = text.find('\n', safeStart);
	std::vector<std::pair<std::size_t, std::size_t>> ranges;

	out.lines.clear();
	out.matchLineIndex = 0;
	if (lineEnd == std::string::npos) lineEnd = text.size();
	if (lineStart > 0) {
		std::size_t pos = safeStart;
		std::size_t breakPos = text.rfind('\n', pos == 0 ? 0 : pos - 1);
		lineStart = breakPos == std::string::npos ? 0 : breakPos + 1;
	}

	{
		std::vector<std::pair<std::size_t, std::size_t>> before;
		std::size_t cursor = lineStart;
		while (cursor > 0 && before.size() < rowCount / 2) {
			std::size_t prevEnd = cursor - 1;
			std::size_t prevBreak = text.rfind('\n', prevEnd == 0 ? 0 : prevEnd - 1);
			std::size_t prevStart = prevBreak == std::string::npos ? 0 : prevBreak + 1;
			before.push_back(std::make_pair(prevStart, prevEnd));
			cursor = prevStart;
		}
		ranges.assign(before.rbegin(), before.rend());
	}
	ranges.push_back(std::make_pair(lineStart, lineEnd));
	while (ranges.size() < rowCount && lineEnd < text.size()) {
		std::size_t nextStart = lineEnd + 1;
		std::size_t nextEnd = text.find('\n', nextStart);
		if (nextEnd == std::string::npos) nextEnd = text.size();
		ranges.push_back(std::make_pair(nextStart, nextEnd));
		lineEnd = nextEnd;
	}

	out.matchLineIndex = ranges.size() > rowCount / 2 ? rowCount / 2 : ranges.size() - 1;
	for (std::size_t i = 0; i < ranges.size(); ++i) {
		MultiPreviewLine line;
		std::size_t start = ranges[i].first;
		std::size_t end = ranges[i].second;
		line.text = sanitizePreviewLine(text.substr(start, end - start));
		if (start <= safeStart && safeStart <= end) {
			line.highlightStart = safeStart - start;
			line.highlightEnd = std::min(end, safeEnd) - start;
			out.matchLineIndex = i;
		}
		out.lines.push_back(line);
	}
	return !out.lines.empty();
}

class MultiFileListView : public TListViewer {
  public:
	MultiFileListView(const TRect &bounds, TScrollBar *aScrollBar, MultiFileSearchSession &session, TView *dialogOwner, ushort &pendingCommand) noexcept : TListViewer(bounds, 1, nullptr, aScrollBar), session(session), dialogOwner(dialogOwner), pendingCommand(pendingCommand) {
		setRange(static_cast<short>(session.files.size()));
	}

	void getText(char *dest, short item, short maxLen) override {
		auto trimFileNameForWidth = [](const std::string &name, std::size_t width) {
			if (name.size() <= width) return name;
			if (width <= 3) return name.substr(0, width);
			return name.substr(0, width - 3) + "...";
		};
		auto hitColumnWidth = [this]() {
			std::size_t width = 0;
			for (const MultiFileSearchFileResult &file : session.files) {
				const std::size_t hitTotal = file.matches.size();
				const std::size_t hitIndex = hitTotal == 0 ? 0 : std::min(file.selectedMatchIndex + 1, hitTotal);
				const std::size_t labelLen = 3 + std::to_string(hitIndex).size() + std::to_string(hitTotal).size();
				width = std::max(width, labelLen);
			}
			return std::max<std::size_t>(7, width);
		};

		if (dest == nullptr || maxLen <= 0) return;
		if (item < 0 || static_cast<std::size_t>(item) >= session.files.size()) {
			dest[0] = EOS;
			return;
		}
		const MultiFileSearchFileResult &file = session.files[static_cast<std::size_t>(item)];
		const std::size_t hitTotal = file.matches.size();
		const std::size_t hitIndex = hitTotal == 0 ? 0 : std::min(file.selectedMatchIndex + 1, hitTotal);
		const std::string hitLabel = "[" + std::to_string(hitIndex) + "/" + std::to_string(hitTotal) + "]";
		const std::size_t width = static_cast<std::size_t>(maxLen - 1);
		const std::size_t firstColWidth = std::min<std::size_t>(hitColumnWidth(), width);
		const std::string fileName = file.fileName.empty() ? baseNameFromPath(file.normalizedPath) : file.fileName;
		std::string label = hitLabel.substr(0, firstColWidth);

		if (label.size() < firstColWidth) label.append(firstColWidth - label.size(), ' ');
		if (label.size() < width) {
			const std::size_t secondColWidth = width - label.size();
			label += trimFileNameForWidth(fileName, secondColWidth);
		}
		std::strncpy(dest, label.c_str(), static_cast<std::size_t>(maxLen - 1));
		dest[maxLen - 1] = EOS;
	}

	void focusItemNum(short item) override {
		TListViewer::focusItemNum(item);
		if (item >= 0 && static_cast<std::size_t>(item) < session.files.size()) session.selectedFileIndex = static_cast<std::size_t>(item);
		message(dialogOwner != nullptr ? dialogOwner : owner, evBroadcast, cmMrMultiFileSelectionChanged, this);
	}

	void selectItem(short item) override {
		if (item >= 0 && static_cast<std::size_t>(item) < session.files.size()) {
			session.selectedFileIndex = static_cast<std::size_t>(item);
			message(dialogOwner != nullptr ? dialogOwner : owner, evBroadcast, cmMrMultiFileSelectionChanged, this);
			pendingCommand = session.replaceMode ? cmMrMultiReplace : cmMrMultiLoad;
		}
	}

  private:
	MultiFileSearchSession &session;
	TView *dialogOwner = nullptr;
	ushort &pendingCommand;
};

class MultiPreviewHeaderView : public TView {
  public:
	MultiPreviewHeaderView(const TRect &bounds, MultiFileSearchSession &session) noexcept : TView(bounds), session(session) {
	}

	void draw() override {
		TDrawBuffer b;
		const TColorAttr color = getColor(1);
		const std::size_t width = static_cast<std::size_t>(std::max<short>(0, size.x));
		const std::size_t totalMatches = sessionTotalMatchCount(session);
		const std::size_t currentMatch = sessionCurrentMatchOrdinal(session);
		const MultiFileSearchFileResult *file = (session.selectedFileIndex < session.files.size()) ? &session.files[session.selectedFileIndex] : nullptr;
		const std::string path = file == nullptr ? std::string() : (file->normalizedPath.empty() ? file->fileName : file->normalizedPath);
		std::string header = "[" + std::to_string(currentMatch) + "/" + std::to_string(totalMatches) + "]";

		if (!path.empty()) header += " " + path;
		if (header.size() > width) {
			if (width <= 3) header = header.substr(0, width);
			else
				header = "..." + header.substr(header.size() - (width - 3));
		}
		b.moveChar(0, ' ', color, size.x);
		b.moveStr(0, header.c_str(), color, size.x);
		writeLine(0, 0, size.x, 1, b);
	}

  private:
	MultiFileSearchSession &session;
};

class MultiPreviewView : public TView {
  public:
	MultiPreviewView(const TRect &bounds, MultiFileSearchSession &session) noexcept : TView(bounds), session(session) {
		options |= ofSelectable;
		eventMask |= evMouseWheel | evMouseDown | evKeyDown;
	}

	void draw() override {
		TDrawBuffer b;
		TColorAttr editorTextAttr;
		TColorAttr editorHighlightAttr;
		const TColorAttr normal = configuredColorSlotOverride(13, editorTextAttr) ? editorTextAttr : static_cast<TColorAttr>(getColor(1));
		const TColorAttr highlight = configuredColorSlotOverride(14, editorHighlightAttr) ? editorHighlightAttr : static_cast<TColorAttr>(getColor(3));
		MultiFileSearchFileResult *file = currentSessionFile(session);
		SearchMatchEntry *match = currentSessionMatch(session);
		std::string text;
		std::string error;
		MultiPreviewBlock block;
		std::size_t width = static_cast<std::size_t>(std::max<short>(0, size.x));
		std::size_t textWidth = width;
		std::size_t left = 0;

		for (short y = 0; y < size.y; ++y) {
			b.moveChar(0, ' ', normal, size.x);
			writeLine(0, y, size.x, 1, b);
		}
		if (file == nullptr || match == nullptr) return;
		if (!loadSessionFileText(*file, text, error) || !buildMultiPreviewBlock(text, *match, static_cast<std::size_t>(size.y), block)) {
			b.moveChar(0, ' ', normal, size.x);
			b.moveStr(0, error.empty() ? "No preview." : error.c_str(), highlight, size.x);
			writeLine(0, std::max<short>(0, size.y / 2), size.x, 1, b);
			return;
		}
		{
			const MultiPreviewLine &matchLine = block.lines[block.matchLineIndex];
			left = centeredPreviewLeft(matchLine.text, matchLine.highlightStart, matchLine.highlightEnd > matchLine.highlightStart ? matchLine.highlightEnd - matchLine.highlightStart : 1, textWidth);
		}
		for (short y = 0; y < size.y; ++y) {
			b.moveChar(0, ' ', normal, size.x);
			if (static_cast<std::size_t>(y) < block.lines.size()) {
				const MultiPreviewLine &line = block.lines[static_cast<std::size_t>(y)];
				for (ushort x = 0; x < static_cast<ushort>(textWidth); ++x) {
					const std::size_t source = left + static_cast<std::size_t>(x);
					const bool inHighlight = source >= line.highlightStart && source < line.highlightEnd && line.highlightEnd > line.highlightStart;
					const char ch = source < line.text.size() ? line.text[source] : ' ';
					b.putChar(x, static_cast<uchar>(ch));
					b.putAttribute(x, inHighlight ? highlight : normal);
				}
			}
			writeLine(0, y, size.x, 1, b);
		}
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evMouseDown && containsMouse(event) && (event.mouse.buttons & mbLeftButton) != 0 && (event.mouse.eventFlags & meDoubleClick) != 0) {
			message(owner, evCommand, session.replaceMode ? cmMrMultiReplace : cmMrMultiLoad, this);
			clearEvent(event);
			return;
		}
		if (event.what == evMouseWheel && containsMouse(event)) {
			if (event.mouse.wheel == mwUp || event.mouse.wheel == mwLeft) {
				message(owner, evCommand, cmMrMultiFileMatchPrev, this);
				clearEvent(event);
				return;
			}
			if (event.mouse.wheel == mwDown || event.mouse.wheel == mwRight) {
				message(owner, evCommand, cmMrMultiFileMatchNext, this);
				clearEvent(event);
				return;
			}
		}
		if (event.what == evKeyDown) {
			ushort keyCode = ctrlToArrow(event.keyDown.keyCode);
			if (keyCode == kbUp || keyCode == kbPgUp) {
				message(owner, evCommand, cmMrMultiFileMatchPrev, this);
				clearEvent(event);
				return;
			}
			if (keyCode == kbDown || keyCode == kbPgDn) {
				message(owner, evCommand, cmMrMultiFileMatchNext, this);
				clearEvent(event);
				return;
			}
		}
		TView::handleEvent(event);
	}

  private:
	MultiFileSearchSession &session;
};

} // namespace

bool promptMultiFileSearchValues(const std::string &patternSeed, std::string &pattern, MRMultiSearchDialogOptions &options, MultiFileSearchSession &outSession) {
	enum {
		kFilespecBufferSize = 256,
		kSearchBufferSize = 256,
		kPathBufferSize = 256
	};
	char filespecInput[kFilespecBufferSize];
	char searchInput[kSearchBufferSize];
	char pathInput[kPathBufferSize];
	ushort optionMask = 0;
	ushort result = cmCancel;
	SubmitInterceptDialog *dialog = nullptr;
	TInputLine *filespecField = nullptr;
	TInputLine *searchField = nullptr;
	TInputLine *pathField = nullptr;
	WorkspaceOptionsCheckBoxes *optionsField = nullptr;
	TLabel *pathLabel = nullptr;
	TButton *searchButton = nullptr;
	MRProgressSlider *progressView = nullptr;
	MRMultiSearchDialogOptions pendingOptions = options;
	std::vector<std::string> filespecHistory;
	std::vector<std::string> pathHistory;

	outSession = MultiFileSearchSession();
	std::memset(filespecInput, 0, sizeof(filespecInput));
	std::memset(searchInput, 0, sizeof(searchInput));
	std::memset(pathInput, 0, sizeof(pathInput));
	if (TProgram::deskTop == nullptr) return false;
	configuredMultiFilespecHistoryEntries(filespecHistory);
	configuredMultiPathHistoryEntries(pathHistory);
	strnzcpy(filespecInput, options.filespec.empty() ? "*.*" : options.filespec.c_str(), sizeof(filespecInput));
	if (!options.searchText.empty()) strnzcpy(searchInput, options.searchText.c_str(), sizeof(searchInput));
	else if (!patternSeed.empty())
		strnzcpy(searchInput, patternSeed.c_str(), sizeof(searchInput));
	{
		std::string initialPath = normalizeConfiguredPathInput(options.startingPath);
		if (initialPath.empty()) initialPath = normalizeConfiguredPathInput(configuredMultiSearchDialogOptions().startingPath);
		if (initialPath.empty() && !pathHistory.empty()) initialPath = normalizeConfiguredPathInput(pathHistory.front());
		if (initialPath.empty()) initialPath = normalizeConfiguredPathInput(configuredLastFileDialogPath());
		if (initialPath.empty()) initialPath = ".";
		strnzcpy(pathInput, initialPath.c_str(), sizeof(pathInput));
	}

	if (options.searchSubdirectories) optionMask |= 0x0001;
	if (options.caseSensitive) optionMask |= 0x0002;
	if (options.regularExpressions) optionMask |= 0x0004;
	if (options.wholeWords) optionMask |= 0x0008;
	if (options.searchFilesInMemory) optionMask |= 0x0010;
	if (options.restrictToWorkspace) optionMask |= 0x0020;

	dialog = new SubmitInterceptDialog("MULTIPLE FILE SEARCH", 102, 19);
	dialog->helpCtx = hcDialogMultiFileSearch;
	filespecField = new TInputLine(TRect(14, 2, 96, 3), kFilespecBufferSize - 1);
	dialog->insert(new TLabel(TRect(2, 2, 14, 3), "~F~ilespecs:", filespecField));
	dialog->insert(filespecField);
	searchField = new TInputLine(TRect(14, 4, 96, 5), kSearchBufferSize - 1);
	dialog->insert(new TLabel(TRect(2, 4, 14, 5), "Se~a~rch:", searchField));
	dialog->insert(searchField);
	dialog->insert(new TStaticText(TRect(3, 6, 13, 7), "Options:"));
	optionsField = new WorkspaceOptionsCheckBoxes(TRect(3, 7, 34, 13), new TSItem("recursive ~S~earch", new TSItem("~C~ase sensitive", new TSItem("~R~egular expressions", new TSItem("whole wor~d~s", new TSItem("include loaded ~w~indows", new TSItem("restrict to wor~k~space", nullptr)))))), 0x0020);
	dialog->insert(optionsField);
	pathField = new GhostedInputLine(TRect(14, 14, 96, 15), kPathBufferSize - 1);
	pathLabel = new GhostedLabel(TRect(2, 14, 14, 15), "Start a~t~:", pathField);
	dialog->insert(pathLabel);
	dialog->insert(pathField);
	optionsField->setStartPathViews(pathLabel, pathField);
	{
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~S~earch", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 2);
		std::vector<TButton *> insertedButtons;
		mr::dialogs::insertUniformButtonRow(*dialog, (102 - metrics.rowWidth) / 2, 16, 2, buttons, 0, &insertedButtons);
		if (!insertedButtons.empty()) searchButton = insertedButtons.front();
	}
	progressView = new MRProgressSlider(TRect(2, 15, 100, 16));
	dialog->insert(progressView);
	dialog->setSubmitButton(searchButton);
	dialog->setProgressView(progressView);
	dialog->addTaskBlockedView(filespecField);
	dialog->addTaskBlockedView(searchField);
	dialog->addTaskBlockedView(pathField);
	dialog->addTaskBlockedView(optionsField);
	filespecField->setData(filespecInput);
	searchField->setData(searchInput);
	pathField->setData(pathInput);
	optionsField->setData(&optionMask);
	dialog->finalizeLayout();
	if (searchField != nullptr) searchField->select();
	dialog->setDialogValidationHook([searchField]() {
		MRScrollableDialog::DialogValidationResult result;
		char text[kSearchBufferSize] = {0};

		if (searchField != nullptr) searchField->getData(text);
		result.valid = !trimAscii(text).empty();
		if (!result.valid) result.warningText = kSearchTextRequiredMessage;
		return result;
	});
	dialog->setSubmitHook([&]() {
		char currentFilespec[kFilespecBufferSize] = {0};
		char currentSearch[kSearchBufferSize] = {0};
		char currentPath[kPathBufferSize] = {0};
		ushort currentMask = 0;
		MRMultiSearchDialogOptions currentOptions = options;
		std::vector<MultiFileSearchMemorySource> memorySources;

		if (filespecField == nullptr || searchField == nullptr || pathField == nullptr || optionsField == nullptr) return;

		filespecField->getData(currentFilespec);
		searchField->getData(currentSearch);
		pathField->getData(currentPath);
		optionsField->getData(&currentMask);
		if (trimAscii(currentSearch).empty()) {
			dialog->runDialogValidation();
			searchField->select();
			return;
		}

		currentOptions.filespec = trimAscii(currentFilespec);
		if (currentOptions.filespec.empty()) currentOptions.filespec = "*.*";
		currentOptions.searchSubdirectories = (currentMask & 0x0001) != 0;
		currentOptions.caseSensitive = (currentMask & 0x0002) != 0;
		currentOptions.regularExpressions = (currentMask & 0x0004) != 0;
		currentOptions.wholeWords = (currentMask & 0x0008) != 0;
		currentOptions.searchFilesInMemory = (currentMask & 0x0010) != 0;
		currentOptions.restrictToWorkspace = (currentMask & 0x0020) != 0;
		if (currentOptions.wholeWords) currentOptions.regularExpressions = false;
		currentOptions.startingPath = normalizeConfiguredPathInput(currentPath);
		if (currentOptions.startingPath.empty()) currentOptions.startingPath = ".";
		currentOptions.searchText = trimAscii(currentSearch);

		pendingOptions = currentOptions;
		captureMultiFileSearchMemorySources(memorySources);
		postMultiSearchStartedWarning();
		const std::size_t ownerId = reinterpret_cast<std::size_t>(dialog);
		const std::uint64_t taskId = mr::coprocessor::globalCoprocessor().submit(
		    mr::coprocessor::Lane::Io, mr::coprocessor::TaskKind::Custom, 0, 0, mr::coprocessor::ExecutionOwnerKind::Dialog, ownerId, "multi-file-search",
		    [currentOptions, memorySources = std::move(memorySources)](const mr::coprocessor::TaskInfo &info) {
			    return runMultiFileSearchTask(info, currentOptions, memorySources, currentOptions.searchText, std::string(), false, false);
		    });
		dialog->beginTask(taskId);
	});
	dialog->setResultHook([&](const mr::coprocessor::Result &result) {
		const MultiFileSearchFinishedPayload *finished = dynamic_cast<const MultiFileSearchFinishedPayload *>(result.payload.get());
		const bool closeRequested = dialog->closeRequested();

		dialog->finishTask();
		dialog->clearProgress();
		optionsField->updateStartPathState();
		if (closeRequested) return;
		if (finished == nullptr || finished->session == nullptr) {
			postSearchError(result.error.empty() ? "Multi-file search failed." : result.error);
			return;
		}
		if (finished->outcome == MultiFileCollectOutcome::Cancelled) {
			postSearchCancelledError();
			return;
		}
		if (finished->outcome == MultiFileCollectOutcome::Error) {
			static_cast<void>(showMultiFileSessionCollectionError(finished->errorText));
			return;
		}
		if (finished->outcome == MultiFileCollectOutcome::NoHits) {
			postNoHitsWarning();
			return;
		}

		MultiFileSearchSession session = std::move(*finished->session);
		std::string errorText;
		if (!validateMultiFileSessionSources(session, errorText)) {
			postSearchError(errorText);
			return;
		}
		options = pendingOptions;
		pattern = pendingOptions.searchText;
		outSession = std::move(session);
		dialog->endModal(cmOK);
	});
	result = TProgram::deskTop->execView(dialog);
	if (filespecField != nullptr && searchField != nullptr && pathField != nullptr && optionsField != nullptr) {
		MRMultiSearchDialogOptions currentOptions = options;
		char currentFilespec[kFilespecBufferSize] = {0};
		char currentSearch[kSearchBufferSize] = {0};
		char currentPath[kPathBufferSize] = {0};
		ushort currentMask = 0;

		filespecField->getData(currentFilespec);
		searchField->getData(currentSearch);
		pathField->getData(currentPath);
		optionsField->getData(&currentMask);
		currentOptions.filespec = trimAscii(currentFilespec);
		if (currentOptions.filespec.empty()) currentOptions.filespec = "*.*";
		currentOptions.searchSubdirectories = (currentMask & 0x0001) != 0;
		currentOptions.caseSensitive = (currentMask & 0x0002) != 0;
		currentOptions.regularExpressions = (currentMask & 0x0004) != 0;
		currentOptions.wholeWords = (currentMask & 0x0008) != 0;
		currentOptions.searchFilesInMemory = (currentMask & 0x0010) != 0;
		currentOptions.restrictToWorkspace = (currentMask & 0x0020) != 0;
		if (currentOptions.wholeWords) currentOptions.regularExpressions = false;
		currentOptions.startingPath = normalizeConfiguredPathInput(currentPath);
		if (currentOptions.startingPath.empty()) currentOptions.startingPath = ".";
		currentOptions.searchText = trimAscii(currentSearch);
		static_cast<void>(setConfiguredMultiSearchDialogOptions(currentOptions));
		static_cast<void>(setConfiguredLastFileDialogPath(currentOptions.startingPath));
		static_cast<void>(addConfiguredMultiFilespecHistoryEntry(currentOptions.filespec));
		static_cast<void>(addConfiguredMultiPathHistoryEntry(currentOptions.startingPath));
	}
	TObject::destroy(dialog);
	return result == cmOK;
}

bool promptMultiFileSarValues(const std::string &patternSeed, const std::string &replacementSeed, std::string &pattern, std::string &replacement, MRMultiSarDialogOptions &options, MultiFileSearchSession &outSession) {
	enum {
		kFilespecBufferSize = 256,
		kSearchBufferSize = 256,
		kReplacementBufferSize = 256,
		kPathBufferSize = 256
	};
	char filespecInput[kFilespecBufferSize];
	char searchInput[kSearchBufferSize];
	char replacementInput[kReplacementBufferSize];
	char pathInput[kPathBufferSize];
	ushort optionMask = 0;
	ushort result = cmCancel;
	SubmitInterceptDialog *dialog = nullptr;
	TInputLine *filespecField = nullptr;
	TInputLine *searchField = nullptr;
	TInputLine *replacementField = nullptr;
	TInputLine *pathField = nullptr;
	WorkspaceOptionsCheckBoxes *optionsField = nullptr;
	TLabel *pathLabel = nullptr;
	TButton *searchButton = nullptr;
	MRProgressSlider *progressView = nullptr;
	MRMultiSarDialogOptions pendingOptions = options;
	std::vector<std::string> filespecHistory;
	std::vector<std::string> pathHistory;

	outSession = MultiFileSearchSession();
	std::memset(filespecInput, 0, sizeof(filespecInput));
	std::memset(searchInput, 0, sizeof(searchInput));
	std::memset(replacementInput, 0, sizeof(replacementInput));
	std::memset(pathInput, 0, sizeof(pathInput));
	if (TProgram::deskTop == nullptr) return false;
	configuredMultiFilespecHistoryEntries(filespecHistory);
	configuredMultiPathHistoryEntries(pathHistory);
	strnzcpy(filespecInput, options.filespec.empty() ? "*.*" : options.filespec.c_str(), sizeof(filespecInput));
	if (!options.searchText.empty()) strnzcpy(searchInput, options.searchText.c_str(), sizeof(searchInput));
	else if (!patternSeed.empty())
		strnzcpy(searchInput, patternSeed.c_str(), sizeof(searchInput));
	if (!options.replacementText.empty()) strnzcpy(replacementInput, options.replacementText.c_str(), sizeof(replacementInput));
	else if (!replacementSeed.empty())
		strnzcpy(replacementInput, replacementSeed.c_str(), sizeof(replacementInput));
	{
		std::string initialPath = normalizeConfiguredPathInput(options.startingPath);
		if (initialPath.empty()) initialPath = normalizeConfiguredPathInput(configuredMultiSarDialogOptions().startingPath);
		if (initialPath.empty() && !pathHistory.empty()) initialPath = normalizeConfiguredPathInput(pathHistory.front());
		if (initialPath.empty()) initialPath = normalizeConfiguredPathInput(configuredLastFileDialogPath());
		if (initialPath.empty()) initialPath = ".";
		strnzcpy(pathInput, initialPath.c_str(), sizeof(pathInput));
	}

	if (options.searchSubdirectories) optionMask |= 0x0001;
	if (options.caseSensitive) optionMask |= 0x0002;
	if (options.regularExpressions) optionMask |= 0x0004;
	if (options.wholeWords) optionMask |= 0x0008;
	if (options.searchFilesInMemory) optionMask |= 0x0010;
	if (options.keepFilesOpen) optionMask |= 0x0020;
	if (options.restrictToWorkspace) optionMask |= 0x0040;

	dialog = new SubmitInterceptDialog("MULTIPLE FILE SEARCH AND REPLACE", 102, 23);
	dialog->helpCtx = hcDialogMultiFileSearch;
	filespecField = new TInputLine(TRect(14, 2, 96, 3), kFilespecBufferSize - 1);
	dialog->insert(new TLabel(TRect(2, 2, 14, 3), "~F~ilespecs:", filespecField));
	dialog->insert(filespecField);
	searchField = new TInputLine(TRect(14, 4, 96, 5), kSearchBufferSize - 1);
	dialog->insert(new TLabel(TRect(2, 4, 14, 5), "Se~a~rch:", searchField));
	dialog->insert(searchField);
	replacementField = new TInputLine(TRect(14, 6, 96, 7), kReplacementBufferSize - 1);
	dialog->insert(new TLabel(TRect(2, 6, 14, 7), "Replac~e~:", replacementField));
	dialog->insert(replacementField);
	dialog->insert(new TStaticText(TRect(3, 8, 13, 9), "Options:"));
	optionsField = new WorkspaceOptionsCheckBoxes(TRect(3, 9, 34, 16), new TSItem("recursive ~S~earch", new TSItem("~C~ase sensitive", new TSItem("~R~egular expressions", new TSItem("whole wor~d~s", new TSItem("include loaded ~w~indows", new TSItem("~K~eep all files open", new TSItem("restrict to wor~k~space", nullptr))))))), 0x0040);
	dialog->insert(optionsField);
	pathField = new GhostedInputLine(TRect(14, 18, 96, 19), kPathBufferSize - 1);
	pathLabel = new GhostedLabel(TRect(2, 18, 16, 19), "Start ~a~t:", pathField);
	dialog->insert(pathLabel);
	dialog->insert(pathField);
	optionsField->setStartPathViews(pathLabel, pathField);
	{
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~S~earch", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 2);
		std::vector<TButton *> insertedButtons;
		mr::dialogs::insertUniformButtonRow(*dialog, (102 - metrics.rowWidth) / 2, 20, 2, buttons, 0, &insertedButtons);
		if (!insertedButtons.empty()) searchButton = insertedButtons.front();
	}
	progressView = new MRProgressSlider(TRect(2, 19, 100, 20));
	dialog->insert(progressView);
	dialog->setSubmitButton(searchButton);
	dialog->setProgressView(progressView);
	dialog->addTaskBlockedView(filespecField);
	dialog->addTaskBlockedView(searchField);
	dialog->addTaskBlockedView(replacementField);
	dialog->addTaskBlockedView(pathField);
	dialog->addTaskBlockedView(optionsField);
	filespecField->setData(filespecInput);
	searchField->setData(searchInput);
	replacementField->setData(replacementInput);
	pathField->setData(pathInput);
	optionsField->setData(&optionMask);
	dialog->finalizeLayout();
	if (searchField != nullptr) searchField->select();
	dialog->setDialogValidationHook([searchField]() {
		MRScrollableDialog::DialogValidationResult result;
		char text[kSearchBufferSize] = {0};

		if (searchField != nullptr) searchField->getData(text);
		result.valid = !trimAscii(text).empty();
		if (!result.valid) result.warningText = kSearchTextRequiredMessage;
		return result;
	});
	dialog->setSubmitHook([&]() {
		char currentFilespec[kFilespecBufferSize] = {0};
		char currentSearch[kSearchBufferSize] = {0};
		char currentReplacement[kReplacementBufferSize] = {0};
		char currentPath[kPathBufferSize] = {0};
		ushort currentMask = 0;
		MRMultiSarDialogOptions currentOptions = options;
		MRMultiSearchDialogOptions searchOptions;
		std::vector<MultiFileSearchMemorySource> memorySources;

		if (filespecField == nullptr || searchField == nullptr || replacementField == nullptr || pathField == nullptr || optionsField == nullptr) return;

		filespecField->getData(currentFilespec);
		searchField->getData(currentSearch);
		replacementField->getData(currentReplacement);
		pathField->getData(currentPath);
		optionsField->getData(&currentMask);
		if (trimAscii(currentSearch).empty()) {
			dialog->runDialogValidation();
			searchField->select();
			return;
		}

		currentOptions.filespec = trimAscii(currentFilespec);
		if (currentOptions.filespec.empty()) currentOptions.filespec = "*.*";
		currentOptions.searchSubdirectories = (currentMask & 0x0001) != 0;
		currentOptions.caseSensitive = (currentMask & 0x0002) != 0;
		currentOptions.regularExpressions = (currentMask & 0x0004) != 0;
		currentOptions.wholeWords = (currentMask & 0x0008) != 0;
		currentOptions.searchFilesInMemory = (currentMask & 0x0010) != 0;
		currentOptions.keepFilesOpen = (currentMask & 0x0020) != 0;
		currentOptions.restrictToWorkspace = (currentMask & 0x0040) != 0;
		if (currentOptions.wholeWords) currentOptions.regularExpressions = false;
		currentOptions.startingPath = normalizeConfiguredPathInput(currentPath);
		if (currentOptions.startingPath.empty()) currentOptions.startingPath = ".";
		currentOptions.searchText = trimAscii(currentSearch);
		currentOptions.replacementText = currentReplacement;

		searchOptions.searchSubdirectories = currentOptions.searchSubdirectories;
		searchOptions.caseSensitive = currentOptions.caseSensitive;
		searchOptions.wholeWords = currentOptions.wholeWords;
		searchOptions.regularExpressions = currentOptions.regularExpressions;
		searchOptions.searchFilesInMemory = currentOptions.searchFilesInMemory;
		searchOptions.restrictToWorkspace = currentOptions.restrictToWorkspace;
		searchOptions.filespec = currentOptions.filespec;
		searchOptions.startingPath = currentOptions.startingPath;

		pendingOptions = currentOptions;
		captureMultiFileSearchMemorySources(memorySources);
		postMultiSearchStartedWarning();
		const std::size_t ownerId = reinterpret_cast<std::size_t>(dialog);
		const std::uint64_t taskId = mr::coprocessor::globalCoprocessor().submit(
		    mr::coprocessor::Lane::Io, mr::coprocessor::TaskKind::Custom, 0, 0, mr::coprocessor::ExecutionOwnerKind::Dialog, ownerId, "multi-file-search-replace",
		    [searchOptions, currentOptions, memorySources = std::move(memorySources)](const mr::coprocessor::TaskInfo &info) {
			    return runMultiFileSearchTask(info, searchOptions, memorySources, currentOptions.searchText, currentOptions.replacementText, true, currentOptions.keepFilesOpen);
		    });
		dialog->beginTask(taskId);
	});
	dialog->setResultHook([&](const mr::coprocessor::Result &result) {
		const MultiFileSearchFinishedPayload *finished = dynamic_cast<const MultiFileSearchFinishedPayload *>(result.payload.get());
		const bool closeRequested = dialog->closeRequested();

		dialog->finishTask();
		dialog->clearProgress();
		optionsField->updateStartPathState();
		if (closeRequested) return;
		if (finished == nullptr || finished->session == nullptr) {
			postSearchError(result.error.empty() ? "Multi-file search and replace failed." : result.error);
			return;
		}
		if (finished->outcome == MultiFileCollectOutcome::Cancelled) {
			postSearchCancelledError();
			return;
		}
		if (finished->outcome == MultiFileCollectOutcome::Error) {
			static_cast<void>(showMultiFileSessionCollectionError(finished->errorText));
			return;
		}
		if (finished->outcome == MultiFileCollectOutcome::NoHits) {
			postNoHitsWarning();
			return;
		}

		MultiFileSearchSession session = std::move(*finished->session);
		std::string errorText;
		if (!validateMultiFileSessionSources(session, errorText)) {
			postSearchError(errorText);
			return;
		}
		options = pendingOptions;
		pattern = pendingOptions.searchText;
		replacement = pendingOptions.replacementText;
		outSession = std::move(session);
		dialog->endModal(cmOK);
	});
	result = TProgram::deskTop->execView(dialog);
	if (filespecField != nullptr && searchField != nullptr && replacementField != nullptr && pathField != nullptr && optionsField != nullptr) {
		MRMultiSarDialogOptions currentOptions = options;
		char currentFilespec[kFilespecBufferSize] = {0};
		char currentSearch[kSearchBufferSize] = {0};
		char currentReplacement[kReplacementBufferSize] = {0};
		char currentPath[kPathBufferSize] = {0};
		ushort currentMask = 0;

		filespecField->getData(currentFilespec);
		searchField->getData(currentSearch);
		replacementField->getData(currentReplacement);
		pathField->getData(currentPath);
		optionsField->getData(&currentMask);
		currentOptions.filespec = trimAscii(currentFilespec);
		if (currentOptions.filespec.empty()) currentOptions.filespec = "*.*";
		currentOptions.searchSubdirectories = (currentMask & 0x0001) != 0;
		currentOptions.caseSensitive = (currentMask & 0x0002) != 0;
		currentOptions.regularExpressions = (currentMask & 0x0004) != 0;
		currentOptions.wholeWords = (currentMask & 0x0008) != 0;
		currentOptions.searchFilesInMemory = (currentMask & 0x0010) != 0;
		currentOptions.keepFilesOpen = (currentMask & 0x0020) != 0;
		currentOptions.restrictToWorkspace = (currentMask & 0x0040) != 0;
		if (currentOptions.wholeWords) currentOptions.regularExpressions = false;
		currentOptions.startingPath = normalizeConfiguredPathInput(currentPath);
		if (currentOptions.startingPath.empty()) currentOptions.startingPath = ".";
		currentOptions.searchText = trimAscii(currentSearch);
		currentOptions.replacementText = currentReplacement;
		static_cast<void>(setConfiguredMultiSarDialogOptions(currentOptions));
		static_cast<void>(setConfiguredLastFileDialogPath(currentOptions.startingPath));
		static_cast<void>(addConfiguredMultiFilespecHistoryEntry(currentOptions.filespec));
		static_cast<void>(addConfiguredMultiPathHistoryEntry(currentOptions.startingPath));
	}
	TObject::destroy(dialog);
	return result == cmOK;
}

MultiDialogAction runMultiFileResultsDialog(MultiFileSearchSession &session) {
	class MultiFileResultsDialog : public MRScrollableDialog {
	  public:
		MultiFileResultsDialog(MultiFileSearchSession &session) : TWindowInit(initMrDialogFrame), MRScrollableDialog(centeredSetupDialogRect(118, 28), session.replaceMode ? "MULTIPLE FILE SEARCH AND REPLACE" : "MULTIPLE FILE SEARCH", 118, 28, initMrDialogFrame), session(session) {
			const short buttonTop = 24;
			const short rows = static_cast<short>(buttonTop - 4);
			const short listTop = 2;
			const short listBottom = static_cast<short>(listTop + rows);
			const int gap = 2;
			helpCtx = hcDialogMultiFileResults;
			listScrollBar = new TScrollBar(TRect(29, listTop, 30, listBottom));
			addManaged(listScrollBar, TRect(29, listTop, 30, listBottom));
			listView = new MultiFileListView(TRect(2, listTop, 29, listBottom), listScrollBar, session, this, pendingListCommand);
			addManaged(listView, TRect(2, listTop, 29, listBottom));
			previewView = new MultiPreviewView(TRect(32, listTop, 116, listBottom), session);
			addManaged(previewView, TRect(32, listTop, 116, listBottom));
			previewHeaderView = new MultiPreviewHeaderView(TRect(32, 1, 116, 2), session);
			addManaged(previewHeaderView, TRect(32, 1, 116, 2));
			if (session.replaceMode) {
				const std::array buttons{mr::dialogs::DialogButtonSpec{"~R~eplace", cmMrMultiReplace, bfDefault}, mr::dialogs::DialogButtonSpec{"Replace ~A~ll", cmMrMultiReplaceAll, bfNormal}, mr::dialogs::DialogButtonSpec{"~S~kip", cmMrMultiSkip, bfNormal}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
				const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, gap);
				mr::dialogs::addManagedUniformButtonRow(*this, (118 - metrics.rowWidth) / 2, buttonTop, gap, buttons);
			} else {
				const std::array buttons{mr::dialogs::DialogButtonSpec{"~L~oad", cmMrMultiLoad, bfDefault},
				                         mr::dialogs::DialogButtonSpec{"Load ~A~ll", cmMrMultiLoadAll, bfNormal},
				                         mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
				const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, gap);
				mr::dialogs::addManagedUniformButtonRow(*this, (118 - metrics.rowWidth) / 2, buttonTop, gap, buttons);
			}
			initScrollIfNeeded();
			selectContent();
			if (session.selectedFileIndex < session.files.size()) listView->focusItemNum(static_cast<short>(session.selectedFileIndex));
			else
				listView->focusItemNum(0);
			setDialogValidationHook([this]() {
				MRScrollableDialog::DialogValidationResult result;
				result.valid = currentSessionMatch(this->session) != nullptr;
				if (!result.valid) result.warningText = "Select a match.";
				return result;
			});
		}

		void handleEvent(TEvent &event) override {
			if (event.what == evBroadcast && event.message.command == cmMrMultiFileSelectionChanged) {
				refreshPreview(true);
				clearEvent(event);
				return;
			}
			if (event.what == evCommand) {
				switch (event.message.command) {
					case cmMrMultiLoad:
					case cmMrMultiLoadAll:
					case cmMrMultiDone:
					case cmMrMultiReplace:
					case cmMrMultiReplaceAll:
					case cmMrMultiSkip:
						endModal(event.message.command);
						clearEvent(event);
						return;
				}
			}
			if (event.what == evMouseWheel) {
				if (event.mouse.wheel == mwUp || event.mouse.wheel == mwLeft) message(this, evCommand, cmMrMultiFileMatchPrev, this);
				else if (event.mouse.wheel == mwDown || event.mouse.wheel == mwRight)
					message(this, evCommand, cmMrMultiFileMatchNext, this);
				clearEvent(event);
				return;
			}
			MRScrollableDialog::handleEvent(event);
			if (pendingListCommand != 0) {
				const ushort command = pendingListCommand;

				pendingListCommand = 0;
				endModal(command);
				clearEvent(event);
				return;
			}
			if (event.what == evCommand && event.message.command == cmMrMultiFileMatchPrev) {
				if (moveSessionMatch(session, -1, true)) {
					if (listView != nullptr) listView->focusItemNum(static_cast<short>(session.selectedFileIndex));
				}
				clearEvent(event);
				return;
			}
			if (event.what == evCommand && event.message.command == cmMrMultiFileMatchNext) {
				if (moveSessionMatch(session, 1, true)) {
					if (listView != nullptr) listView->focusItemNum(static_cast<short>(session.selectedFileIndex));
				}
				clearEvent(event);
				return;
			}
			if (event.what == evCommand && event.message.command == cmMrMultiLoadKeepDialog) {
				static_cast<void>(activateSessionCurrentMatch(session));
				if (owner != nullptr) makeFirst();
				if (TProgram::deskTop != nullptr) TProgram::deskTop->setCurrent(this, TView::normalSelect);
				else
					select();
				if (listView != nullptr) listView->select();
				clearEvent(event);
				return;
			}
		}

	  private:
		void refreshPreview(bool updateEditorViewport) {
			if (updateEditorViewport) {
				static_cast<void>(previewSessionCurrentMatch(session));
				if (owner != nullptr) makeFirst();
				if (listView != nullptr) listView->select();
			}
			if (previewHeaderView != nullptr) previewHeaderView->drawView();
			if (previewView != nullptr) previewView->drawView();
		}

		MultiFileSearchSession &session;
		ushort pendingListCommand = 0;
		TScrollBar *listScrollBar = nullptr;
		MultiFileListView *listView = nullptr;
		MultiPreviewHeaderView *previewHeaderView = nullptr;
		MultiPreviewView *previewView = nullptr;
	};

	ushort result = cmCancel;
	MultiFileResultsDialog *dialog = nullptr;

	if (session.files.empty() || TProgram::deskTop == nullptr) return MultiDialogAction::Cancel;
	dialog = new MultiFileResultsDialog(session);
	result = mr::dialogs::execDialog(dialog);
	if (result == cmMrMultiLoad) return MultiDialogAction::Load;
	if (result == cmMrMultiLoadAll) return MultiDialogAction::LoadAll;
	if (result == cmMrMultiDone) return MultiDialogAction::Done;
	if (result == cmMrMultiReplace) return MultiDialogAction::Replace;
	if (result == cmMrMultiReplaceAll) return MultiDialogAction::ReplaceAll;
	if (result == cmMrMultiSkip) return MultiDialogAction::Skip;
	if (result == cmCancel) return MultiDialogAction::Done;
	return MultiDialogAction::Cancel;
}
