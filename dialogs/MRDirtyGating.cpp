#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TButton
#define Uses_TDialog
#define Uses_TListBox
#define Uses_TObject
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TStaticText
#define Uses_TWindowInit
#include <tvision/tv.h>

#include "MRDirtyGating.hpp"
#include "../app/MRHelpTopics.generated.hpp"

#include "setup/MRSetupCommon.hpp"
#include "../ui/MRFrame.hpp"
#include "../ui/widgets/MRColumnListView.hpp"

#include <array>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace mr {
namespace dialogs {
namespace {

TFrame *initMrDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}

void insertStaticLine(MRDialogFoundation &dialog, int width, int y, const std::string &text) {
	const int left = std::max(2, (width - strwidth(text.c_str())) / 2);
	dialog.insert(new TStaticText(TRect(left, y, left + static_cast<int>(text.size()) + 1, y + 1), text.c_str()));
}

std::vector<std::string> wrapText(const char *text, std::size_t maxLen) {
	std::vector<std::string> lines;
	std::string value = text != nullptr ? text : "";

	if (value.empty()) return lines;
	if (maxLen == 0) {
		lines.push_back(value);
		return lines;
	}

	while (!value.empty()) {
		if (value.size() <= maxLen) {
			lines.push_back(value);
			break;
		}
		std::size_t cut = value.rfind(' ', maxLen);
		if (cut == std::string::npos || cut == 0) cut = maxLen;
		lines.push_back(value.substr(0, cut));
		value.erase(0, cut);
		while (!value.empty() && value.front() == ' ')
			value.erase(value.begin());
	}
	return lines;
}

int widestLineWidth(const std::vector<std::string> &lines) {
	int width = 0;

	for (const std::string &line : lines)
		width = std::max(width, strwidth(line.c_str()));
	return width;
}

std::string addMnemonic(const std::string &text, char preferred) {
	std::size_t i = 0;
	std::size_t mark = std::string::npos;

	if (text.empty() || text.find('~') != std::string::npos) return text;

	for (i = 0; i < text.size(); ++i) {
		unsigned char ch = static_cast<unsigned char>(text[i]);
		if (std::tolower(ch) == std::tolower(static_cast<unsigned char>(preferred))) {
			mark = i;
			break;
		}
	}
	if (mark == std::string::npos)
		for (i = 0; i < text.size(); ++i)
			if (std::isalpha(static_cast<unsigned char>(text[i])) != 0) {
				mark = i;
				break;
			}
	if (mark == std::string::npos) return text;

	std::string out;
	out.reserve(text.size() + 2);
	out.append(text, 0, mark);
	out.push_back('~');
	out.push_back(text[mark]);
	out.push_back('~');
	out.append(text, mark + 1, std::string::npos);
	return out;
}

std::string joinCommaSeparatedItems(const std::vector<std::string> &values) {
	std::string joined;

	for (std::size_t i = 0; i < values.size(); ++i) {
		if (i != 0) joined += ", ";
		joined += values[i];
	}
	return joined;
}

class TDirtyItemDialog : public MRDialogFoundation {
  public:
	TDirtyItemDialog(const char *dialogTitle, const char *headline, const char *itemsLabel, const char *joinedItems, const char *primaryLabel, const char *discardLabel) : TWindowInit(initMrDialogFrame), MRDialogFoundation(centeredSetupDialogRect(74, 11), dialogTitle != nullptr ? dialogTitle : "UNSAVED CHANGES", 74, 11, initMrDialogFrame) {
		const std::string primaryButtonLabel = addMnemonic(primaryLabel != nullptr ? primaryLabel : "Save", 's');
		const std::string discardButtonLabel = addMnemonic(discardLabel != nullptr ? discardLabel : "Discard", 'd');
		const std::array buttons{mr::dialogs::DialogButtonSpec{primaryButtonLabel.c_str(), cmYes, bfDefault}, mr::dialogs::DialogButtonSpec{discardButtonLabel.c_str(), cmNo, bfNormal}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 3);
		const int buttonLeft = (74 - metrics.rowWidth) / 2;

		helpCtx = hcDialogConfirm;
		insert(new TStaticText(TRect(2, 2, 70, 3), headline != nullptr ? headline : "Discard changed items?"));
		insert(new TStaticText(TRect(2, 4, 70, 5), itemsLabel != nullptr ? itemsLabel : "Dirty items:"));
		insert(new TStaticText(TRect(2, 5, 70, 7), joinedItems != nullptr ? joinedItems : ""));
		mr::dialogs::insertUniformButtonRow(*this, buttonLeft, 8, 3, buttons);
	}
};

} // namespace

UnsavedChangesChoice showUnsavedChangesDialog(const char *primaryLabel, const char *headline, const char *detail) {
	return showUnsavedChangesDialog(primaryLabel, headline, detail, nullptr);
}

UnsavedChangesChoice showUnsavedChangesDialog(const char *primaryLabel, const char *headline, const char *detail, const char *discardLabel) {
	const bool hasDetail = detail != nullptr && *detail != '\0';
	std::string label = primaryLabel != nullptr && *primaryLabel != '\0' ? primaryLabel : "Save";
	std::string discardLabelText = discardLabel != nullptr && *discardLabel != '\0' ? discardLabel : "Discard";
	std::string primaryButtonLabel = addMnemonic(label, 's');
	std::string discardButtonLabel = addMnemonic(discardLabelText, 'd');
	std::string cancelButtonLabel = addMnemonic("Cancel", 'c');
	const int gap = 2;
	const int desktopWidth = TProgram::deskTop != nullptr ? TProgram::deskTop->size.x : 80;
	const int maxTextWidth = std::max(32, desktopWidth - 12);
	std::vector<std::string> textLines = wrapText(headline != nullptr ? headline : "Window has unsaved changes.", static_cast<std::size_t>(maxTextWidth));

	if (hasDetail) {
		std::vector<std::string> detailLines = wrapText(detail, static_cast<std::size_t>(maxTextWidth));
		textLines.insert(textLines.end(), detailLines.begin(), detailLines.end());
	}

	const std::array buttons{mr::dialogs::DialogButtonSpec{primaryButtonLabel.c_str(), cmYes, bfDefault}, mr::dialogs::DialogButtonSpec{discardButtonLabel.c_str(), cmNo, bfNormal}, mr::dialogs::DialogButtonSpec{cancelButtonLabel.c_str(), cmCancel, bfNormal}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
	const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, gap);
	const int textWidth = std::max(widestLineWidth(textLines), metrics.rowWidth);
	const int width = std::min(std::max(46, textWidth + 6), std::max(46, desktopWidth - 4));
	const int height = std::max(hasDetail ? 10 : 8, static_cast<int>(textLines.size()) + 6);
	MRDialogFoundation *dialog = new MRDialogFoundation(mr::dialogs::centeredDialogRect(width, height), "CONFIRM", width, height);
	int y = 2;

	dialog->helpCtx = hcDialogConfirm;
	for (const std::string &line : textLines)
		insertStaticLine(*dialog, width, y++, line);
	mr::dialogs::insertUniformButtonRow(*dialog, (width - metrics.rowWidth) / 2, height - 3, gap, buttons);

	switch (mr::dialogs::execDialog(dialog)) {
		case cmYes:
			return UnsavedChangesChoice::Save;
		case cmNo:
			return UnsavedChangesChoice::Discard;
		default:
			return UnsavedChangesChoice::Cancel;
	}
}

UnsavedChangesChoice showWorkspaceLoadDialog(const char *primaryLabel, const std::vector<std::string> &fileUrls, const char *discardLabel) {
	std::string label = primaryLabel != nullptr && *primaryLabel != '\0' ? primaryLabel : "Load workspace";
	std::string discardLabelText = discardLabel != nullptr && *discardLabel != '\0' ? discardLabel : "Discard workspace";
	std::string primaryButtonLabel = addMnemonic(label, 'l');
	std::string discardButtonLabel = addMnemonic(discardLabelText, 'd');
	const std::string fileCountText = std::to_string(fileUrls.size());
	const int gap = 2;
	const TRect desktopBounds = TProgram::deskTop != nullptr ? TProgram::deskTop->getExtent() : TRect(0, 0, 80, 25);
	const int desktopWidth = std::max(1, desktopBounds.b.x - desktopBounds.a.x);
	const int desktopHeight = std::max(1, desktopBounds.b.y - desktopBounds.a.y);
	const int maximumDialogWidth = std::max(1, desktopWidth - 4);
	const int maximumDialogHeight = std::max(1, desktopHeight - 4);
	const int maximumTextWidth = std::max(1, maximumDialogWidth - 6);
	const bool showFileList = fileUrls.size() >= 10;
	std::vector<std::string> textLines;
	std::vector<MRColumnListView::Row> fileRows;

	if (showFileList) {
		const std::size_t numberWidth = std::to_string(fileUrls.size()).size();

		fileRows.reserve(fileUrls.size());
		for (std::size_t i = 0; i < fileUrls.size(); ++i) {
			const std::string number = std::to_string(i + 1);
			const std::string row = std::string(numberWidth - number.size(), ' ') + number + " " + fileUrls[i];

			fileRows.push_back(MRColumnListView::Row{row});
		}
	} else {
		std::vector<std::string> fileNames;

		fileNames.reserve(fileUrls.size());
		for (const std::string &url : fileUrls) {
			const std::size_t separator = url.find_last_of("\\/");

			fileNames.push_back(separator == std::string::npos || separator + 1 >= url.size() ? url : url.substr(separator + 1));
		}
		const std::string joinedNames = joinCommaSeparatedItems(fileNames) + " (" + fileCountText + (fileUrls.size() == 1 ? " file)" : " files)");
		const std::vector<std::string> wrappedNames = wrapText(joinedNames.c_str(), static_cast<std::size_t>(maximumTextWidth));

		textLines.insert(textLines.end(), wrappedNames.begin(), wrappedNames.end());
	}

	const std::array buttons{mr::dialogs::DialogButtonSpec{primaryButtonLabel.c_str(), cmYes, bfDefault}, mr::dialogs::DialogButtonSpec{discardButtonLabel.c_str(), cmNo, bfNormal}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
	const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, gap);
	int contentWidth = widestLineWidth(textLines);
	if (showFileList)
		for (const MRColumnListView::Row &row : fileRows)
			if (!row.empty()) contentWidth = std::max(contentWidth, strwidth(row.front().c_str()));
	const int minimumDialogWidth = std::max(46, metrics.rowWidth + 4);
	const int desiredDialogWidth = std::max(minimumDialogWidth, contentWidth + 6);
	const int virtualWidth = std::max(minimumDialogWidth, std::min(desiredDialogWidth, maximumDialogWidth));
	const int physicalWidth = std::min(virtualWidth, maximumDialogWidth);
	const int listTop = 2;
	const int maximumListRows = std::max(3, maximumDialogHeight - listTop - 6);
	const int visibleListRows = showFileList ? std::min(static_cast<int>(fileRows.size()), maximumListRows) : 0;
	const int virtualHeight = showFileList ? listTop + visibleListRows + 6 : std::max(7, static_cast<int>(textLines.size()) + 6);
	const int physicalHeight = std::min(virtualHeight, maximumDialogHeight);
	const int dialogLeft = desktopBounds.a.x + std::max(0, (desktopWidth - physicalWidth) / 2);
	const int dialogTop = desktopBounds.a.y + std::max(0, (desktopHeight - physicalHeight) / 2);
	MRDialogFoundation *dialog = new MRDialogFoundation(TRect(dialogLeft, dialogTop, dialogLeft + physicalWidth, dialogTop + physicalHeight), "RESTORE WORKSPACE", virtualWidth, virtualHeight);
	int y = 2;

	dialog->helpCtx = hcDialogWorkspaceRestore;
	for (const std::string &line : textLines)
		insertStaticLine(*dialog, virtualWidth, y++, line);
	if (showFileList) {
		const int listBottom = listTop + visibleListRows;
		const int listRight = virtualWidth - 3;
		const int countWidth = strwidth(fileCountText.c_str());
		const std::string fileCountLine(static_cast<std::size_t>(std::max(0, listRight - 2 - countWidth)), ' ');
		TScrollBar *verticalScrollBar = new TScrollBar(TRect(virtualWidth - 3, listTop, virtualWidth - 2, listBottom));
		TScrollBar *horizontalScrollBar = new TScrollBar(TRect(2, listBottom, listRight, listBottom + 1));
		MRColumnListView *fileList = new MRColumnListView(TRect(2, listTop, listRight, listBottom), verticalScrollBar, horizontalScrollBar, nullptr, 0, 0);

		dialog->insert(verticalScrollBar);
		dialog->insert(horizontalScrollBar);
		dialog->insert(fileList);
		dialog->insert(new TStaticText(TRect(2, listBottom + 1, listRight, listBottom + 2), (fileCountLine + fileCountText).c_str()));
		fileList->setRows(fileRows);
		if (fileRows.size() <= static_cast<std::size_t>(visibleListRows)) verticalScrollBar->hide();
	}
	mr::dialogs::insertUniformButtonRow(*dialog, (virtualWidth - metrics.rowWidth) / 2, virtualHeight - 3, gap, buttons);

	switch (mr::dialogs::execDialog(dialog)) {
		case cmYes:
			return UnsavedChangesChoice::Save;
		case cmNo:
			return UnsavedChangesChoice::Discard;
		default:
			return UnsavedChangesChoice::Cancel;
	}
}

UnsavedChangesChoice runDialogDirtyGating(const char *headline, const char *primaryLabel, const char *detail) {
	return showUnsavedChangesDialog(primaryLabel, headline, detail);
}

bool runDialogConfirm(const char *headline, const char *confirmLabel, const char *detail, const char *dialogTitle) {
	const std::string label = confirmLabel != nullptr && *confirmLabel != '\0' ? confirmLabel : "Done";
	const std::string confirmButtonLabel = addMnemonic(label, 'd');
	const std::string cancelButtonLabel = addMnemonic("Cancel", 'c');
	const int gap = 2;
	const int desktopWidth = TProgram::deskTop != nullptr ? TProgram::deskTop->size.x : 80;
	const int maxTextWidth = std::max(32, desktopWidth - 12);
	std::vector<std::string> textLines = wrapText(headline != nullptr ? headline : "Confirm action.", static_cast<std::size_t>(maxTextWidth));

	if (detail != nullptr && *detail != '\0') {
		std::vector<std::string> detailLines = wrapText(detail, static_cast<std::size_t>(maxTextWidth));
		textLines.insert(textLines.end(), detailLines.begin(), detailLines.end());
	}

	const std::array buttons{mr::dialogs::DialogButtonSpec{confirmButtonLabel.c_str(), cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{cancelButtonLabel.c_str(), cmCancel, bfNormal}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
	const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, gap);
	const int textWidth = std::max(widestLineWidth(textLines), metrics.rowWidth);
	const int width = std::min(std::max(46, textWidth + 6), std::max(46, desktopWidth - 4));
	const int height = std::max(8, static_cast<int>(textLines.size()) + 6);
	MRDialogFoundation *dialog = new MRDialogFoundation(mr::dialogs::centeredDialogRect(width, height), dialogTitle != nullptr ? dialogTitle : "CONFIRM", width, height);
	int y = 2;

	dialog->helpCtx = hcDialogConfirm;
	for (const std::string &line : textLines)
		insertStaticLine(*dialog, width, y++, line);
	mr::dialogs::insertUniformButtonRow(*dialog, (width - metrics.rowWidth) / 2, height - 3, gap, buttons);
	return mr::dialogs::execDialog(dialog) == cmOK;
}

UnsavedChangesChoice runDialogDirtyListGating(const char *dialogTitle, const char *headline, const char *itemsLabel, const std::vector<std::string> &dirtyItems, const char *primaryLabel) {
	return runDialogDirtyListGating(dialogTitle, headline, itemsLabel, dirtyItems, primaryLabel, "Discard");
}

UnsavedChangesChoice runDialogDirtyListGating(const char *dialogTitle, const char *headline, const char *itemsLabel, const std::vector<std::string> &dirtyItems, const char *primaryLabel, const char *discardLabel) {
	if (dirtyItems.empty()) return runDialogDirtyGating(headline, primaryLabel);
	std::string joinedItems = joinCommaSeparatedItems(dirtyItems);

	TDirtyItemDialog *dialog = new TDirtyItemDialog(dialogTitle, headline, itemsLabel, joinedItems.c_str(), primaryLabel, discardLabel);
	if (dialog == nullptr) return UnsavedChangesChoice::Cancel;

	switch (execDialog(dialog)) {
		case cmYes:
			return UnsavedChangesChoice::Save;
		case cmNo:
			return UnsavedChangesChoice::Discard;
		default:
			return UnsavedChangesChoice::Cancel;
	}
}

} // namespace dialogs
} // namespace mr
