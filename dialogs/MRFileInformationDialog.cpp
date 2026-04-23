#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TRect
#define Uses_TWindow
#define Uses_TDialog
#define Uses_TButton
#define Uses_TStaticText
#define Uses_MsgBox
#include <tvision/tv.h>

#include "MRFileInformationDialog.hpp"
#include "MRSetupDialogCommon.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "../app/MRCommands.hpp"
#include "../coprocessor/MRCoprocessor.hpp"
#include "../coprocessor/MRPerformance.hpp"
#include "../ui/TMREditWindow.hpp"
#include "../ui/TMRFileEditor.hpp"
#include "../ui/TMRTextBuffer.hpp"
#include "../ui/MRWindowSupport.hpp"

namespace {
struct FileInformationPage {
	std::string title;
	std::vector<std::string> lines;
};

void insertStaticLine(TDialog *dialog, int x, int y, const char *text) {
	dialog->insert(new TStaticText(TRect(x, y, x + std::strlen(text) + 1, y + 1), text));
}

ushort execDialog(TDialog *dialog) {
	ushort result = mr::dialogs::execDialog(dialog);
	if (result == cmHelp)
		static_cast<void>(mrShowProjectHelp());
	return result;
}

std::string shortenForDialog(const std::string &value, std::size_t maxLen) {
	if (value.size() <= maxLen)
		return value;
	if (maxLen <= 3)
		return value.substr(0, maxLen);
	return value.substr(0, maxLen - 3) + "...";
}

std::string yesNo(bool value) {
	return value ? "Yes" : "No";
}

std::string formatByteCount(std::size_t bytes) {
	char field[128];
	double scaled = static_cast<double>(bytes);

	if (bytes >= static_cast<std::size_t>(1024) * 1024 * 1024) {
		scaled /= 1024.0 * 1024.0 * 1024.0;
		std::snprintf(field, sizeof(field), "%.2f GiB (%llu bytes)", scaled,
		              static_cast<unsigned long long>(bytes));
	} else if (bytes >= static_cast<std::size_t>(1024) * 1024) {
		scaled /= 1024.0 * 1024.0;
		std::snprintf(field, sizeof(field), "%.2f MiB (%llu bytes)", scaled,
		              static_cast<unsigned long long>(bytes));
	} else if (bytes >= static_cast<std::size_t>(1024)) {
		scaled /= 1024.0;
		std::snprintf(field, sizeof(field), "%.2f KiB (%llu bytes)", scaled,
		              static_cast<unsigned long long>(bytes));
	} else
		std::snprintf(field, sizeof(field), "%llu bytes", static_cast<unsigned long long>(bytes));
	return field;
}

std::string formatPercent(std::size_t part, std::size_t total) {
	char field[32];
	double percent = 0.0;

	if (total != 0)
		percent = (100.0 * static_cast<double>(part)) / static_cast<double>(total);
	std::snprintf(field, sizeof(field), "%.1f%%", percent);
	return field;
}

std::string formatContribution(std::size_t bytes, std::size_t total) {
	if (total == 0)
		return formatByteCount(bytes);
	return formatByteCount(bytes) + " [" + formatPercent(bytes, total) + "]";
}

std::string formatCursorProgress(std::size_t cursorOffset, std::size_t currentLength,
                                 bool hasDiskSize, std::size_t diskSize) {
	std::ostringstream out;

	out << formatByteCount(cursorOffset) << " of " << formatByteCount(currentLength) << " ("
	    << formatPercent(cursorOffset, currentLength) << ")";
	if (hasDiskSize && diskSize != currentLength)
		out << ", disk ref " << formatPercent(std::min(cursorOffset, diskSize), diskSize);
	return out.str();
}

std::string formatTaskSummary(TMREditWindow *win) {
	std::ostringstream out;
	const std::size_t count = win != nullptr ? win->trackedCoprocessorTaskCount() : 0;

	out << count;
	if (count == 0)
		return out.str();
	out << " active";
	for (std::size_t i = 0; i < count && i < 3; ++i)
		out << (i == 0 ? " (#" : ", #") << win->trackedCoprocessorTaskId(i);
	out << ")";
	if (count > 3)
		out << " ...";
	return out.str();
}

std::string formatWarmupState(const char *label, std::uint64_t taskId, bool exactReady = false) {
	std::ostringstream out;

	out << label << ": ";
	if (taskId != 0)
		out << "running (#" << taskId << ")";
	else if (exactReady)
		out << "idle (exact)";
	else
		out << "idle";
	return out.str();
}

const char *blockModeLabel(TMREditWindow *win) {
	if (win == nullptr)
		return "None";
	switch (win->blockStatus()) {
		case TMREditWindow::bmLine:
			return "Line";
		case TMREditWindow::bmColumn:
			return "Column";
		case TMREditWindow::bmStream:
			return "Stream";
		default:
			return "None";
	}
}

class FileInformationDialog : public MRDialogFoundation {
  public:
	FileInformationDialog(const FileInformationPage &page, std::size_t pageIndex, std::size_t pageCount,
	                      bool hasPrev, bool hasNext)
	    : TWindowInit(&TDialog::initFrame),
	      MRDialogFoundation(centeredBounds(computeWidth(page), computeHeight(page)), "FILE INFORMATION",
	                         computeWidth(page), computeHeight(page)),
	      hasPrevInfo(hasPrev), hasNextInfo(hasNext) {
		int width = size.x;
		int height = size.y;
		int y = 2;
		std::ostringstream header;

		header << page.title << "  (" << (pageIndex + 1) << "/" << pageCount << ")";
		insertStaticLine(this, 2, y++, header.str().c_str());
		for (std::vector<std::string>::const_iterator it = page.lines.begin(); it != page.lines.end(); ++it, ++y)
			insertStaticLine(this, 2, y, it->c_str());
		if (hasPrevInfo)
			insert(new TButton(TRect(width - 38, height - 3, width - 28, height - 1), "~P~rev",
			                   cmMrPreviewPrev, bfNormal));
		if (hasNextInfo)
			insert(new TButton(TRect(width - 27, height - 3, width - 17, height - 1), "~N~ext",
			                   cmMrPreviewNext, bfNormal));
		insert(new TButton(TRect(width - 16, height - 3, width - 2, height - 1), "~D~one", cmOK,
		                   bfDefault));
	}

	virtual void handleEvent(TEvent &event) override {
		MRDialogFoundation::handleEvent(event);
		if (event.what != evKeyDown)
			return;
		switch (ctrlToArrow(event.keyDown.keyCode)) {
			case kbF7:
			case kbPgUp:
				if (hasPrevInfo) {
					endModal(cmMrPreviewPrev);
					clearEvent(event);
				}
				break;
			case kbF8:
			case kbPgDn:
				if (hasNextInfo) {
					endModal(cmMrPreviewNext);
					clearEvent(event);
				}
				break;
		}
	}

  private:
	static int computeWidth(const FileInformationPage &page) {
		TRect desk = TProgram::deskTop->getExtent();
		std::size_t maxLen = page.title.size();
		for (const auto & line : page.lines)
			if (line.size() > maxLen)
				maxLen = line.size();
		return std::max(76, std::min(static_cast<int>(maxLen) + 6, (desk.b.x - desk.a.x) - 2));
	}

	static int computeHeight(const FileInformationPage &page) {
		TRect desk = TProgram::deskTop->getExtent();
		int desired = static_cast<int>(page.lines.size()) + 6;
		return std::max(14, std::min(desired, (desk.b.y - desk.a.y) - 1));
	}

	static TRect centeredBounds(int width, int height) {
		TRect desk = TProgram::deskTop->getExtent();
		int left = desk.a.x + (desk.b.x - desk.a.x - width) / 2;
		int top = desk.a.y + (desk.b.y - desk.a.y - height) / 2;
		return TRect(left, top, left + width, top + height);
	}

	bool hasPrevInfo;
	bool hasNextInfo;
};

std::vector<FileInformationPage> buildFileInformationPages(TMREditWindow *win) {
	std::vector<FileInformationPage> pages;
	FileInformationPage page1;
	FileInformationPage page2;
	FileInformationPage page3;
	TMRTextBuffer textBuffer = win != nullptr ? win->buffer() : TMRTextBuffer();
	std::string path = win != nullptr ? win->currentFileName() : std::string();
	std::string title = win != nullptr && win->getTitle(0) != nullptr ? win->getTitle(0) : "?No-File?";
	std::string roleDetail = win != nullptr ? win->windowRoleDetail() : std::string();
	struct stat st;
	bool hasStat = !path.empty() && ::stat(path.c_str(), &st) == 0;
	unsigned long cursorLine = win != nullptr ? win->cursorLineNumber() : 1UL;
	unsigned long cursorColumn = win != nullptr ? win->cursorColumnNumber() : 1UL;
	const std::size_t currentLength = textBuffer.length();
	const std::size_t diskSize = hasStat ? static_cast<std::size_t>(st.st_size) : 0;
	const std::size_t lineCount = win != nullptr ? win->bufferLineCount() : 1;
	const std::size_t estimatedLineCount = win != nullptr ? win->estimatedLineCount() : lineCount;

	page1.title = "WINDOW / CURSOR";
	page1.lines.push_back(std::string("Window kind   : ") + (win != nullptr ? win->windowRoleName() : "Text window"));
	if (!roleDetail.empty())
		page1.lines.push_back(std::string("Role detail   : ") + shortenForDialog(roleDetail, 64));
	page1.lines.push_back(std::string("Title         : ") + shortenForDialog(title, 64));
	page1.lines.push_back(std::string("Path          : ") +
	                      shortenForDialog(path.empty() ? std::string("<none>") : path, 64));
	page1.lines.push_back(std::string("Visible       : ") + yesNo(win != nullptr && (win->state & sfVisible) != 0));
	page1.lines.push_back(std::string("Read-only     : ") + yesNo(win != nullptr && win->isReadOnly()));
	page1.lines.push_back(std::string("Modified      : ") + yesNo(win != nullptr && win->isFileChanged()));
	page1.lines.push_back(std::string("Save in place : ") + yesNo(win != nullptr && win->canSaveInPlace()));
	page1.lines.push_back(std::string("Syntax        : ") + (win != nullptr ? win->syntaxLanguageName() : "Plain Text"));
	page1.lines.push_back(std::string("Block mode    : ") + blockModeLabel(win));
	page1.lines.push_back(std::string("Selection     : ") +
	                      (win != nullptr && win->hasSelection()
	                           ? formatContribution(win->selectionLength(), currentLength)
	                           : std::string("<none>")));
	page1.lines.push_back(std::string("Undo history  : ") + yesNo(win != nullptr && win->hasUndoHistory()));
	page1.lines.push_back(std::string("Cursor        : line ") + std::to_string(cursorLine) + ", column " +
	                      std::to_string(cursorColumn));
	page1.lines.push_back(std::string("Offset        : ") +
	                      formatCursorProgress(win != nullptr ? win->cursorOffset() : 0, currentLength, hasStat, diskSize));
	page1.lines.push_back(std::string("Lines         : ") +
	                      (win != nullptr && win->exactLineCountKnown()
	                           ? std::to_string(lineCount) + " exact"
	                           : std::to_string(lineCount) + " loaded, est. " + std::to_string(estimatedLineCount)));
	page1.lines.push_back(std::string("Metrics       : ") +
	                      (win != nullptr && win->usesApproximateMetrics() ? "Approximate large-file mode" : "Exact"));

	page2.title = "DOCUMENT / COPROCESSOR";
	page2.lines.push_back(std::string("Current text  : ") + formatByteCount(currentLength));
	page2.lines.push_back(std::string("Disk size     : ") +
	                      (hasStat ? formatByteCount(diskSize) : std::string("<n/a>")));
	page2.lines.push_back(std::string("Original buf  : ") +
	                      (win != nullptr ? formatContribution(win->originalBufferLength(), currentLength)
	                                 : std::string("<n/a>")));
	page2.lines.push_back(std::string("Add buffer    : ") +
	                      (win != nullptr ? formatContribution(win->addBufferLength(), currentLength)
	                                 : std::string("<n/a>")));
	page2.lines.push_back(std::string("Piece table   : ") +
	                      (win != nullptr ? std::to_string(win->pieceCount()) + " pieces" : std::string("<n/a>")));
	page2.lines.push_back(std::string("Doc version   : ") +
	                      (win != nullptr ? std::to_string(win->documentVersion()) : std::string("0")));
	page2.lines.push_back(std::string("Undo stack    : ") +
	                      (win != nullptr ? std::to_string(win->buffer().undoStackDepth()) + " items" : std::string("0")));
	page2.lines.push_back(std::string("Redo stack    : ") +
	                      (win != nullptr ? std::to_string(win->buffer().redoStackDepth()) + " items" : std::string("0")));
	page2.lines.push_back(std::string("Source        : ") +
	                      (win != nullptr && win->hasMappedOriginalSource() ? "mmap file" : "memory buffer"));
	if (win != nullptr && win->hasMappedOriginalSource())
		page2.lines.push_back(std::string("Mapped path   : ") + shortenForDialog(win->mappedOriginalPath(), 64));
	page2.lines.push_back(std::string("Temp file     : ") +
	                      (win != nullptr && win->isTemporaryFile()
	                           ? shortenForDialog(win->temporaryFileName(), 64)
	                           : std::string("<none>")));
	page2.lines.push_back(std::string("Session saved : ") + yesNo(win != nullptr && win->hasBeenSavedInSession()));
	page2.lines.push_back(std::string("Tracked tasks : ") + formatTaskSummary(win));
	page2.lines.push_back(std::string("Macro policy  : ") +
	                      (win != nullptr ? shortenForDialog(win->macroPolicySummary(), 64) : std::string("<n/a>")));
	page2.lines.push_back(std::string("Conflict rule : ") +
	                      (win != nullptr ? shortenForDialog(win->macroConflictPolicySummary(), 64)
	                                 : std::string("<n/a>")));
	page2.lines.push_back(std::string("Cancel rule   : ") +
	                      (win != nullptr ? shortenForDialog(win->macroCancelPolicySummary(), 64)
	                                 : std::string("<n/a>")));
	page2.lines.push_back(std::string("Macro stats   : ") +
	                      (win != nullptr ? win->macroCounterSummary() : std::string("<n/a>")));
	page2.lines.push_back(std::string("Macro recent  : ") +
	                      (win != nullptr ? shortenForDialog(win->lastMacroSummary(), 64) : std::string("<n/a>")));
	page2.lines.push_back(formatWarmupState("Line index", win != nullptr ? win->pendingLineIndexWarmupTaskId() : 0,
	                                        win != nullptr && win->exactLineCountKnown()));
	page2.lines.push_back(formatWarmupState("Syntax", win != nullptr ? win->pendingSyntaxWarmupTaskId() : 0));
	page2.lines.push_back(formatWarmupState("Mini map", win != nullptr ? win->pendingMiniMapWarmupTaskId() : 0));
	page2.lines.push_back(std::string("Result queue  : ") +
	                      std::to_string(mr::coprocessor::globalCoprocessor().pendingResults()) +
	                      " pending result(s)");

	page3.title = "PERFORMANCE";
	{
		mr::performance::MessageLineNotice notice;
		std::vector<mr::performance::Event> windowEvents = mr::performance::recentForWindow(
		    win != nullptr ? static_cast<std::size_t>(win->bufferId()) : 0, win != nullptr ? win->documentId() : 0, 5);
		std::vector<mr::performance::Event> globalEvents = mr::performance::recentGlobal(4);

		page3.lines.push_back(std::string("Active marquee: ") +
		                      (mr::performance::currentMessageLineNotice(notice) ? notice.text
		                                                                        : std::string("<none>")));
		page3.lines.push_back("Window recent :");
		if (windowEvents.empty())
			page3.lines.push_back("  <none>");
		else
			for (const auto & windowEvent : windowEvents)
				page3.lines.push_back("  " + shortenForDialog(mr::performance::formatEventLine(windowEvent), 68));

		page3.lines.push_back("Global recent :");
		if (globalEvents.empty())
			page3.lines.push_back("  <none>");
		else
			for (const auto & globalEvent : globalEvents)
				page3.lines.push_back("  " + shortenForDialog(mr::performance::formatEventLine(globalEvent), 68));
	}

	pages.push_back(page1);
	pages.push_back(page2);
	pages.push_back(page3);
	return pages;
}
} // namespace

void showFileInformationDialog(TMREditWindow *win) {
	std::vector<FileInformationPage> pages;
	std::size_t pageIndex = 0;
	ushort result;

	if (win == nullptr) {
		messageBox(mfInformation | mfOKButton, "No active file window.");
		return;
	}
	pages = buildFileInformationPages(win);
	if (pages.empty())
		return;
	while (true) {
		result = execDialog(new FileInformationDialog(pages[pageIndex], pageIndex, pages.size(),
		                                              pageIndex > 0, pageIndex + 1 < pages.size()));
		if (result == cmMrPreviewPrev && pageIndex > 0) {
			--pageIndex;
			continue;
		}
		if (result == cmMrPreviewNext && pageIndex + 1 < pages.size()) {
			++pageIndex;
			continue;
		}
		break;
	}
}
