#ifndef TMREDITWINDOW_HPP
#define TMREDITWINDOW_HPP

#define Uses_TWindow
#define Uses_TScrollBar
#define Uses_TIndicator
#define Uses_TFileEditor
#define Uses_TRect
#define Uses_TEvent
#define Uses_TEditor
#define Uses_TObject
#include <tvision/tv.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <unistd.h>
#include <vector>

#include "TMRFrame.hpp"
#include "TMRIndicator.hpp"
#include "TMRTextBuffer.hpp"

void mrTraceCoprocessorTaskCancel(int bufferId, std::uint64_t taskId);

class TMREditWindow : public TWindow {
  public:
	struct TrackedTask {
		std::uint64_t id;
		mr::coprocessor::TaskKind kind;
		std::string label;
		std::chrono::steady_clock::time_point startedAt;

		TrackedTask() noexcept
		    : id(0), kind(mr::coprocessor::TaskKind::Custom), label(), startedAt(std::chrono::steady_clock::now()) {
		}

		TrackedTask(std::uint64_t aId, mr::coprocessor::TaskKind aKind, std::string aLabel = std::string())
		    : id(aId), kind(aKind), label(std::move(aLabel)), startedAt(std::chrono::steady_clock::now()) {
		}
	};

	enum WindowRole {
		wrText = 0,
		wrFile,
		wrCommunicationCommand,
		wrCommunicationPipe,
		wrCommunicationDevice,
		wrLog,
		wrHelp
	};

	TMREditWindow(const TRect &bounds, const char *title, int aNumber)
	    : TWindowInit(&TMREditWindow::initFrame), TWindow(bounds, 0, aNumber), vScrollBar(nullptr),
	      hScrollBar(nullptr), indicator(nullptr), editor(nullptr), bufferId_(allocateBufferId()),
	      firstSaveDone_(false), temporaryFileUsed_(false), temporaryFileName_(), indentLevel_(1),
	      blockMode_(bmNone), blockMarkingOn_(false), blockAnchor_(0), blockEnd_(0),
	      trackedCoprocessorTasks_(), windowRole_(wrText), windowRoleDetail_() {
		options |= ofTileable;

		std::strncpy(displayTitle, (title != nullptr && *title != '\0') ? title : "Untitled",
		             sizeof(displayTitle) - 1);
		displayTitle[sizeof(displayTitle) - 1] = '\0';

		hScrollBar = new TScrollBar(TRect(1, size.y - 1, size.x - 1, size.y));
		hScrollBar->hide();
		insert(hScrollBar);

		vScrollBar = new TScrollBar(TRect(size.x - 1, 1, size.x, size.y - 1));
		vScrollBar->hide();
		insert(vScrollBar);

		indicator = new TMRIndicator(TRect(2, size.y - 1, 38, size.y));
		indicator->hide();
		insert(indicator);
		if (frame != nullptr) {
			TMRFrame *mrFrame = static_cast<TMRFrame *>(frame);
			mrFrame->setMarkerStateProvider([this]() {
				return TMRFrame::MarkerState(isFileChanged(), insertModeEnabled(),
				                             indicator != nullptr && indicator->shouldDrawTaskMarker(),
				                             indicator != nullptr && indicator->shouldDrawReadOnlyMarker());
			});
			mrFrame->setTaskOverviewProvider([this]() { return describeRunningTasks(); });
		}

		TRect r(getExtent());
		r.grow(-1, -1);

		editor = createEditor(r, "");
		insert(editor);
		refreshSyntaxContext();
	}

	virtual ~TMREditWindow() override {
		cancelTrackedCoprocessorTasks();
	}

	virtual TPalette &getPalette() const override {
		return TWindow::getPalette();
	}

	virtual const char *getTitle(short) override {
		if (editor != nullptr && editor->hasPersistentFileName())
			return editor->persistentFileName();
		return displayTitle;
	}

	virtual void handleEvent(TEvent &event) override {
		if (frame != nullptr) {
			TMRFrame *mrFrame = static_cast<TMRFrame *>(frame);
			if ((event.what & (evMouseDown | evMouseMove | evMouseUp)) != 0)
				mrFrame->updateTaskHover(event.mouse.where, false);
			else if ((event.what & (evKeyDown | evCommand)) != 0)
				mrFrame->updateTaskHover(TPoint(), true);
		}
		TWindow::handleEvent(event);
		if (event.what == evBroadcast && event.message.command == cmUpdateTitle) {
			updateTaskMarkers();
			if (frame != nullptr)
				frame->drawView();
			clearEvent(event);
		}
	}

	bool loadFromFile(const char *fileName) {
		std::string expandedName;
		std::string loadError;

		if (editor == nullptr || fileName == nullptr || *fileName == '\0')
			return false;

		expandedName = fileName;
		if (expandedName.size() >= editor->persistentFileNameCapacity())
			return false;
		char expandedPath[MAXPATH];
		strnzcpy(expandedPath, expandedName.c_str(), sizeof(expandedPath));
		fexpand(expandedPath);
		expandedName = expandedPath;

		if (!editor->loadMappedFile(expandedName.c_str(), loadError))
			return false;

		resetTransientEditorState();
		setReadOnly(isExistingPathReadOnly(editor->persistentFileName()));
		temporaryFileUsed_ = false;
		temporaryFileName_.clear();
		setWindowRole(wrFile);
		updateTitleFromEditor();
		return true;
	}

	bool loadTextBuffer(const char *text, const char *title = nullptr) {
		if (editor == nullptr)
			return false;
		if (!editor->replaceBufferText(text))
			return false;

		resetTransientEditorState();
		setReadOnly(false);
		temporaryFileUsed_ = false;
		temporaryFileName_.clear();
		editor->clearPersistentFileName();
		setWindowRole(wrText);
		if (title != nullptr && *title != '\0')
			setDisplayTitle(title);
		else
			updateTitleFromEditor();
		refreshSyntaxContext();
		return true;
	}

	bool saveCurrentFile() {
		if (editor == nullptr || isReadOnly() || !editor->canSaveInPlace())
			return false;

		bool ok = editor->saveInPlace() == True;
		if (ok) {
			firstSaveDone_ = true;
			temporaryFileUsed_ = false;
			temporaryFileName_.clear();
			setWindowRole(wrFile);
			updateTitleFromEditor();
		}
		return ok;
	}

	bool saveCurrentFileAs() {
		if (editor == nullptr || isReadOnly() || !editor->canSaveAs())
			return false;

		bool ok = editor->saveAsWithPrompt() == True;
		if (ok) {
			firstSaveDone_ = true;
			temporaryFileUsed_ = false;
			temporaryFileName_.clear();
			setReadOnly(isExistingPathReadOnly(editor->persistentFileName()));
			setWindowRole(wrFile);
			updateTitleFromEditor();
		}
		return ok;
	}

	const char *currentFileName() const {
		if (editor != nullptr && editor->hasPersistentFileName())
			return editor->persistentFileName();
		return "";
	}

	TMRFileEditor *getEditor() const {
		return editor;
	}

	TMRTextBuffer buffer() const {
		return TMRTextBuffer(editor);
	}

	bool isBufferEmpty() const {
		return buffer().isEmpty();
	}

	std::size_t bufferLength() const {
		return buffer().length();
	}

	std::size_t bufferLineCount() const {
		return buffer().lineCount();
	}

	bool hasSelection() const {
		return buffer().hasSelection();
	}

	bool hasUndoHistory() const {
		return buffer().hasUndoHistory();
	}

	TPoint cursorPoint() const {
		return buffer().cursorPoint();
	}

	unsigned long cursorLineNumber() const {
		return buffer().cursorLineNumber();
	}

	unsigned long cursorColumnNumber() const {
		return buffer().cursorColumnNumber();
	}

	bool insertModeEnabled() const noexcept {
		return editor != nullptr && editor->insertModeEnabled();
	}

	std::size_t cursorOffset() const noexcept {
		return editor != nullptr ? editor->cursorOffset() : 0;
	}

	std::size_t selectionLength() const noexcept {
		return editor != nullptr ? editor->selectionLength() : 0;
	}

	const char *syntaxLanguageName() const {
		return editor != nullptr ? editor->syntaxLanguageName() : "Plain Text";
	}

	TMRSyntaxLanguage syntaxLanguage() const {
		return editor != nullptr ? editor->syntaxLanguage() : TMRSyntaxLanguage::PlainText;
	}

	bool hasPersistentFileName() const {
		return editor != nullptr && editor->hasPersistentFileName();
	}

	bool canSaveInPlace() const {
		return editor != nullptr && editor->canSaveInPlace();
	}

	bool canSaveAs() const {
		return editor != nullptr && editor->canSaveAs();
	}

	bool isReadOnly() const {
		return editor != nullptr && editor->isReadOnly();
	}

	void setReadOnly(bool readOnly) {
		if (editor != nullptr)
			editor->setReadOnly(readOnly);
		if (indicator != nullptr)
			indicator->setReadOnly(readOnly);
		if (readOnly && editor != nullptr)
			editor->setDocumentModified(false);
	}

	bool hasBeenSavedInSession() const {
		return firstSaveDone_;
	}

	bool eofInMemory() const {
		return editor != nullptr;
	}

	int bufferId() const {
		return bufferId_;
	}

	std::size_t documentId() const noexcept {
		return editor != nullptr ? editor->documentId() : 0;
	}

	WindowRole windowRole() const noexcept {
		return windowRole_;
	}

	const char *windowRoleName() const noexcept {
		switch (windowRole_) {
			case wrFile:
				return "File text";
			case wrCommunicationCommand:
				return "Communication command";
			case wrCommunicationPipe:
				return "Communication pipe";
			case wrCommunicationDevice:
				return "Communication device";
			case wrLog:
				return "Log window";
			case wrHelp:
				return "Help window";
			case wrText:
			default:
				return "Text window";
		}
	}

	void setWindowRole(WindowRole role, const std::string &detail = std::string()) {
		windowRole_ = role;
		windowRoleDetail_ = detail;
	}

	const std::string &windowRoleDetail() const noexcept {
		return windowRoleDetail_;
	}

	bool isTemporaryFile() const {
		return temporaryFileUsed_;
	}

	const char *temporaryFileName() const {
		return temporaryFileName_.c_str();
	}

	bool isFileChanged() const {
		return editor != nullptr && !isReadOnly() && editor->isDocumentModified();
	}

	void setFileChanged(bool changed) {
		if (editor != nullptr)
			editor->setDocumentModified(changed && !isReadOnly());
	}

	void setCurrentFileName(const char *fileName) {
		if (editor == nullptr)
			return;

		if (fileName == nullptr || *fileName == '\0')
			editor->clearPersistentFileName();
		else {
			editor->setPersistentFileName(fileName);
			setWindowRole(wrFile);
		}
		if ((fileName == nullptr || *fileName == '\0') && windowRole_ == wrFile)
			setWindowRole(wrText);
		updateTitleFromEditor();
		refreshSyntaxContext();
	}

	bool confirmAbandonForReload() {
		if (editor == nullptr)
			return false;
		return editor->valid(cmClose) == True;
	}

	bool replaceTextBuffer(const char *text, const char *title = nullptr) {
		if (editor == nullptr || !editor->replaceBufferText(text))
			return false;
		if (title != nullptr && *title != '\0')
			setDisplayTitle(title);
		return true;
	}

	bool appendTextBuffer(const char *text) {
		if (editor == nullptr)
			return false;
		return editor->appendBufferText(text);
	}

	void setDisplayTitle(const char *title) {
		std::strncpy(displayTitle, (title != nullptr && *title != '\0') ? title : "Untitled",
		             sizeof(displayTitle) - 1);
		displayTitle[sizeof(displayTitle) - 1] = '\0';
		refreshSyntaxContext();
		message(owner, evBroadcast, cmUpdateTitle, 0);
	}

	void trackCoprocessorTask(std::uint64_t taskId,
	                          mr::coprocessor::TaskKind kind = mr::coprocessor::TaskKind::Custom,
	                          const std::string &label = std::string()) {
		if (taskId == 0)
			return;
		for (std::size_t i = 0; i < trackedCoprocessorTasks_.size(); ++i)
			if (trackedCoprocessorTasks_[i].id == taskId)
				return;
		trackedCoprocessorTasks_.push_back(TrackedTask(taskId, kind, label));
		updateTaskMarkers();
	}

	std::size_t trackedCoprocessorTaskCount() const noexcept {
		return trackedCoprocessorTasks_.size();
	}

	std::uint64_t trackedCoprocessorTaskId(std::size_t index) const noexcept {
		return index < trackedCoprocessorTasks_.size() ? trackedCoprocessorTasks_[index].id : 0;
	}

	std::size_t trackedMacroTaskCount() const noexcept {
		std::size_t count = 0;
		for (std::size_t i = 0; i < trackedCoprocessorTasks_.size(); ++i)
			if (trackedCoprocessorTasks_[i].kind == mr::coprocessor::TaskKind::MacroJob)
				++count;
		return count;
	}

	bool hasTrackedMacroTasks() const noexcept {
		return trackedMacroTaskCount() != 0;
	}

	std::vector<std::string> describeRunningTasks() const {
		std::vector<std::string> lines;
		std::size_t i;

		for (i = 0; i < trackedCoprocessorTasks_.size(); ++i) {
			const TrackedTask &task = trackedCoprocessorTasks_[i];
			std::string line;

			switch (task.kind) {
				case mr::coprocessor::TaskKind::MacroJob:
					line = "Macro";
					break;
				case mr::coprocessor::TaskKind::ExternalIo:
					line = "Program";
					break;
				case mr::coprocessor::TaskKind::IndicatorBlink:
					line = "Indicator";
					break;
				case mr::coprocessor::TaskKind::SyntaxWarmup:
					line = "Syntax";
					break;
				case mr::coprocessor::TaskKind::LineIndexWarmup:
					line = "Line index";
					break;
				case mr::coprocessor::TaskKind::Custom:
				default:
					line = "Task";
					break;
			}
			if (!task.label.empty()) {
				line += ": ";
				line += compactTaskLabel(task);
			}
			line += "  ";
			line += formatTaskElapsed(task);
			lines.push_back(line);
		}
		if (editor != nullptr) {
			if (editor->pendingLineIndexWarmupTaskId() != 0)
				lines.push_back("Line indexing  running");
			if (editor->pendingSyntaxWarmupTaskId() != 0)
				lines.push_back("Syntax warmup  running");
		}
		return lines;
	}

	bool cancelTrackedMacroTasks() {
		bool cancelledAny = false;

		for (std::size_t i = 0; i < trackedCoprocessorTasks_.size(); ++i) {
			if (trackedCoprocessorTasks_[i].kind != mr::coprocessor::TaskKind::MacroJob)
				continue;
			mrTraceCoprocessorTaskCancel(bufferId_, trackedCoprocessorTasks_[i].id);
			if (mr::coprocessor::globalCoprocessor().cancelTask(trackedCoprocessorTasks_[i].id))
				cancelledAny = true;
		}
		return cancelledAny;
	}

	void showMacroNotice(const std::string &text, TMRIndicator::NoticeKind kind) {
		if (indicator != nullptr)
			indicator->showStatusNotice(text, kind);
	}

	std::size_t originalBufferLength() const noexcept {
		return editor != nullptr ? editor->originalBufferLength() : 0;
	}

	std::size_t addBufferLength() const noexcept {
		return editor != nullptr ? editor->addBufferLength() : 0;
	}

	std::size_t pieceCount() const noexcept {
		return editor != nullptr ? editor->pieceCount() : 0;
	}

	std::size_t documentVersion() const noexcept {
		return editor != nullptr ? editor->documentVersion() : 0;
	}

	bool hasMappedOriginalSource() const noexcept {
		return editor != nullptr && editor->hasMappedOriginalSource();
	}

	const std::string &mappedOriginalPath() const noexcept {
		if (editor != nullptr)
			return editor->mappedOriginalPath();
		return emptyString();
	}

	std::size_t estimatedLineCount() const noexcept {
		return editor != nullptr ? editor->estimatedLineCount() : 1;
	}

	bool exactLineCountKnown() const noexcept {
		return editor != nullptr && editor->exactLineCountKnown();
	}

	std::uint64_t pendingLineIndexWarmupTaskId() const noexcept {
		return editor != nullptr ? editor->pendingLineIndexWarmupTaskId() : 0;
	}

	std::uint64_t pendingSyntaxWarmupTaskId() const noexcept {
		return editor != nullptr ? editor->pendingSyntaxWarmupTaskId() : 0;
	}

	bool usesApproximateMetrics() const noexcept {
		return editor != nullptr && editor->usesApproximateMetrics();
	}

	void releaseCoprocessorTask(std::uint64_t taskId) {
		for (std::vector<TrackedTask>::iterator it = trackedCoprocessorTasks_.begin();
		     it != trackedCoprocessorTasks_.end(); ++it)
			if (it->id == taskId) {
				trackedCoprocessorTasks_.erase(it);
				updateTaskMarkers();
				return;
			}
	}

	int indentLevel() const {
		return indentLevel_;
	}

	void setIndentLevel(int level) {
		if (level < 1)
			level = 1;
		if (level > 254)
			level = 254;
		indentLevel_ = level;
	}

	enum BlockMode { bmNone = 0, bmLine = 1, bmColumn = 2, bmStream = 3 };

	void beginLineBlock() {
		beginBlock(bmLine);
	}

	void beginColumnBlock() {
		beginBlock(bmColumn);
	}

	void beginStreamBlock() {
		beginBlock(bmStream);
	}

	void endBlock() {
		if (editor == nullptr || blockMode_ == bmNone)
			return;
		blockEnd_ = static_cast<uint>(editor->cursorOffset());
		blockMarkingOn_ = false;
		syncBlockVisual();
	}

	void clearBlock() {
		blockMode_ = bmNone;
		blockMarkingOn_ = false;
		blockAnchor_ = 0;
		blockEnd_ = 0;
		if (editor != nullptr) {
			editor->setSelectionOffsets(editor->cursorOffset(), editor->cursorOffset(), False);
			editor->revealCursor(True);
			editor->update(ufView);
		}
	}

	bool hasBlock() const {
		return blockMode_ != bmNone;
	}

	bool isBlockMarking() const {
		return blockMode_ != bmNone && blockMarkingOn_;
	}

	int blockStatus() const {
		return static_cast<int>(blockMode_);
	}

	uint blockAnchorPtr() const {
		return blockAnchor_;
	}

	uint blockEffectiveEndPtr() const {
		return effectiveBlockEnd();
	}

	int blockLine1() const {
		return normalizedBlockLine1();
	}

	int blockLine2() const {
		return normalizedBlockLine2();
	}

	int blockCol1() const {
		return normalizedBlockCol1();
	}

	int blockCol2() const {
		return normalizedBlockCol2();
	}

	void refreshBlockVisual() {
		syncBlockVisual();
	}

  private:
	TMRFileEditor *createEditor(const TRect &bounds, const char *fileName) {
		return new TMRFileEditor(bounds, hScrollBar, vScrollBar, indicator, fileName != nullptr ? fileName : "");
	}

	static int allocateBufferId() {
		static int nextId = 1;
		return nextId++;
	}

	static const std::string &emptyString() noexcept {
		static const std::string value;
		return value;
	}

	static TFrame *initFrame(TRect r) {
		return new TMRFrame(r);
	}

	static bool isExistingPathReadOnly(const char *fileName) {
		if (fileName == nullptr || *fileName == '\0')
			return false;
		if (access(fileName, F_OK) != 0)
			return false;
		return access(fileName, W_OK) != 0;
	}

	void resetTransientEditorState() {
		if (editor == nullptr)
			return;
		editor->resetUndoState();
		clearBlock();
	}

	void updateTitleFromEditor() {
		if (editor != nullptr && editor->hasPersistentFileName()) {
			std::strncpy(displayTitle, editor->persistentFileName(), sizeof(displayTitle) - 1);
			displayTitle[sizeof(displayTitle) - 1] = '\0';
		}
		refreshSyntaxContext();
		message(owner, evBroadcast, cmUpdateTitle, 0);
	}

	void refreshSyntaxContext() {
		if (editor != nullptr)
			editor->setSyntaxTitleHint(displayTitle);
	}

	void updateTaskMarkers() {
		std::size_t taskCount = trackedCoprocessorTasks_.size();
		if (editor != nullptr) {
			if (editor->pendingLineIndexWarmupTaskId() != 0)
				++taskCount;
			if (editor->pendingSyntaxWarmupTaskId() != 0)
				++taskCount;
		}
		if (indicator != nullptr)
			indicator->setTaskCount(taskCount);
	}

	void cancelTrackedCoprocessorTasks() {
		for (std::size_t i = 0; i < trackedCoprocessorTasks_.size(); ++i) {
			mrTraceCoprocessorTaskCancel(bufferId_, trackedCoprocessorTasks_[i].id);
			mr::coprocessor::globalCoprocessor().cancelTask(trackedCoprocessorTasks_[i].id);
		}
		trackedCoprocessorTasks_.clear();
		updateTaskMarkers();
	}

	void beginBlock(BlockMode mode) {
		if (editor == nullptr)
			return;
		blockMode_ = mode;
		blockMarkingOn_ = true;
		blockAnchor_ = static_cast<uint>(editor->cursorOffset());
		blockEnd_ = static_cast<uint>(editor->cursorOffset());
		syncBlockVisual();
	}

	uint effectiveBlockEnd() const {
		if (editor != nullptr && blockMarkingOn_)
			return static_cast<uint>(editor->cursorOffset());
		return blockEnd_;
	}

	void blockPtrRange(uint &a, uint &b) const {
		a = blockAnchor_;
		b = effectiveBlockEnd();
		if (a > b)
			std::swap(a, b);
	}

	int lineNumberForPtr(uint ptr) const {
		uint pos = 0;
		int line = 1;
		if (editor == nullptr)
			return 0;
		if (ptr > editor->bufferLength())
			ptr = static_cast<uint>(editor->bufferLength());
		while (pos < ptr && pos < editor->bufferLength()) {
			uint next = static_cast<uint>(editor->nextLineOffset(pos));
			if (next <= pos || next > ptr)
				break;
			pos = next;
			++line;
		}
		return line;
	}

	int columnForPtr(uint ptr) const {
		uint start;
		if (editor == nullptr)
			return 0;
		if (ptr > editor->bufferLength())
			ptr = static_cast<uint>(editor->bufferLength());
		start = static_cast<uint>(editor->lineStartOffset(ptr));
		return editor->charColumn(start, ptr) + 1;
	}

	static std::string baseNameOf(const std::string &path) {
		std::size_t pos = path.find_last_of("\\/");
		return pos == std::string::npos ? path : path.substr(pos + 1);
	}

	static std::string stripExtension(const std::string &name) {
		std::size_t dot = name.find_last_of('.');
		if (dot == std::string::npos || dot == 0)
			return name;
		return name.substr(0, dot);
	}

	static std::string trimTaskLabel(const std::string &label) {
		std::size_t start = label.find_first_not_of(' ');
		std::size_t end = label.find_last_not_of(' ');
		if (start == std::string::npos)
			return std::string();
		return label.substr(start, end - start + 1);
	}

	static std::string compactTaskLabel(const TrackedTask &task) {
		std::string label = trimTaskLabel(task.label);

		if (label.empty())
			return label;
		if (task.kind == mr::coprocessor::TaskKind::MacroJob)
			return stripExtension(baseNameOf(label));
		if (task.kind == mr::coprocessor::TaskKind::ExternalIo) {
			std::size_t split = label.find_first_of(" \t");
			std::string head = split == std::string::npos ? label : label.substr(0, split);
			return baseNameOf(head);
		}
		return baseNameOf(label);
	}

	static std::string formatTaskElapsed(const TrackedTask &task) {
		using namespace std::chrono;
		char buffer[32];
		double elapsedMs =
		    duration_cast<milliseconds>(steady_clock::now() - task.startedAt).count();

		if (elapsedMs < 1000.0) {
			std::snprintf(buffer, sizeof(buffer), "%.0f ms", elapsedMs);
			return buffer;
		}
		std::snprintf(buffer, sizeof(buffer), "%.2f s", elapsedMs / 1000.0);
		return buffer;
	}

	int normalizedBlockLine1() const {
		uint a, b;
		if (blockMode_ == bmNone)
			return 0;
		blockPtrRange(a, b);
		return lineNumberForPtr(a);
	}

	int normalizedBlockLine2() const {
		uint a, b;
		if (blockMode_ == bmNone)
			return 0;
		blockPtrRange(a, b);
		return lineNumberForPtr(b);
	}

	int normalizedBlockCol1() const {
		int aCol;
		int bCol;
		if (blockMode_ == bmNone)
			return 0;
		if (blockMode_ == bmLine)
			return 1;
		aCol = columnForPtr(blockAnchor_);
		bCol = columnForPtr(effectiveBlockEnd());
		return std::min(aCol, bCol);
	}

	int normalizedBlockCol2() const {
		int aCol;
		int bCol;
		if (blockMode_ == bmNone)
			return 0;
		if (blockMode_ != bmColumn)
			return 256;
		aCol = columnForPtr(blockAnchor_);
		bCol = columnForPtr(effectiveBlockEnd());
		return std::max(aCol, bCol);
	}

	void syncBlockVisual() {
		uint a;
		uint b;
		if (editor == nullptr)
			return;
		if (blockMode_ == bmStream) {
			blockPtrRange(a, b);
			editor->setSelectionOffsets(a, b, False);
		} else if (blockMode_ == bmLine) {
			blockPtrRange(a, b);
			a = static_cast<uint>(editor->lineStartOffset(a));
			b = static_cast<uint>(editor->nextLineOffset(b));
			if (b > editor->bufferLength())
				b = static_cast<uint>(editor->bufferLength());
			editor->setSelectionOffsets(a, b, False);
		} else
			editor->setSelectionOffsets(editor->cursorOffset(), editor->cursorOffset(), False);
		editor->revealCursor(True);
		editor->update(ufView);
	}

	TScrollBar *vScrollBar;
	TScrollBar *hScrollBar;
	TMRIndicator *indicator;
	TMRFileEditor *editor;
	int bufferId_;
	bool firstSaveDone_;
	bool temporaryFileUsed_;
	std::string temporaryFileName_;
	int indentLevel_;
	BlockMode blockMode_;
	bool blockMarkingOn_;
	uint blockAnchor_;
	uint blockEnd_;
	std::vector<TrackedTask> trackedCoprocessorTasks_;
	WindowRole windowRole_;
	std::string windowRoleDetail_;
	char displayTitle[MAXPATH];
};

#endif
