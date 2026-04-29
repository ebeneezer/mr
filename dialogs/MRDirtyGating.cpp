#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TButton
#define Uses_TDialog
#define Uses_TObject
#define Uses_TRect
#define Uses_TStaticText
#define Uses_TWindowInit
#include <tvision/tv.h>

#include "MRDirtyGating.hpp"

#include "MRSetupCommon.hpp"

#include <array>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace mr {
namespace dialogs {
namespace {

TRect centeredRect(int width, int height) {
	return mr::dialogs::centeredDialogRect(width, height);
}

void insertStaticLine(TDialog *dialog, int x, int y, const std::string &text) {
	const int width = dialog != nullptr ? dialog->size.x : 0;
	int left = x;

	if (width > 0) left = std::max(2, (width - strwidth(text.c_str())) / 2);
	dialog->insert(new TStaticText(TRect(left, y, left + static_cast<int>(text.size()) + 1, y + 1), text.c_str()));
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
	TDirtyItemDialog(const char *dialogTitle, const char *headline, const char *itemsLabel, const char *joinedItems, const char *primaryLabel) : TWindowInit(&TDialog::initFrame), MRDialogFoundation(centeredSetupDialogRect(74, 11), dialogTitle != nullptr ? dialogTitle : "UNSAVED CHANGES", 74, 11) {
		const std::array buttons{mr::dialogs::DialogButtonSpec{primaryLabel != nullptr ? primaryLabel : "~S~ave", cmYes, bfDefault}, mr::dialogs::DialogButtonSpec{"~D~iscard", cmNo, bfNormal}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 3);
		const int buttonLeft = (74 - metrics.rowWidth) / 2;

		insert(new TStaticText(TRect(2, 2, 70, 3), headline != nullptr ? headline : "Discard changed items?"));
		insert(new TStaticText(TRect(2, 4, 70, 5), itemsLabel != nullptr ? itemsLabel : "Dirty items:"));
		insert(new TStaticText(TRect(2, 5, 70, 7), joinedItems != nullptr ? joinedItems : ""));
		mr::dialogs::insertUniformButtonRow(*this, buttonLeft, 8, 3, buttons);
	}
};

} // namespace

UnsavedChangesChoice showUnsavedChangesDialog(const char *primaryLabel, const char *headline, const char *detail) {
	const bool hasDetail = detail != nullptr && *detail != '\0';
	std::string label = primaryLabel != nullptr && *primaryLabel != '\0' ? primaryLabel : "Save";
	std::string primaryButtonLabel = addMnemonic(label, 's');
	std::string discardButtonLabel = addMnemonic("Discard", 'd');
	std::string cancelButtonLabel = addMnemonic("Cancel", 'c');
	const int gap = 2;
	const int desktopWidth = TProgram::deskTop != nullptr ? TProgram::deskTop->size.x : 80;
	const int maxTextWidth = std::max(32, desktopWidth - 12);
	std::vector<std::string> textLines = wrapText(headline != nullptr ? headline : "Window has unsaved changes.", static_cast<std::size_t>(maxTextWidth));

	if (hasDetail) {
		std::vector<std::string> detailLines = wrapText(detail, static_cast<std::size_t>(maxTextWidth));
		textLines.insert(textLines.end(), detailLines.begin(), detailLines.end());
	}

	const std::array buttons{mr::dialogs::DialogButtonSpec{primaryButtonLabel.c_str(), cmYes, bfDefault}, mr::dialogs::DialogButtonSpec{discardButtonLabel.c_str(), cmNo, bfNormal}, mr::dialogs::DialogButtonSpec{cancelButtonLabel.c_str(), cmCancel, bfNormal}};
	const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, gap);
	const int textWidth = std::max(widestLineWidth(textLines), metrics.rowWidth);
	const int width = std::min(std::max(46, textWidth + 6), std::max(46, desktopWidth - 4));
	const int height = std::max(hasDetail ? 10 : 8, static_cast<int>(textLines.size()) + 6);
	MRDialogFoundation *dialog = new MRDialogFoundation(centeredRect(width, height), "Confirm", width, height);
	int y = 2;

	for (const std::string &line : textLines)
		insertStaticLine(dialog, 3, y++, line);
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

	const std::array buttons{mr::dialogs::DialogButtonSpec{confirmButtonLabel.c_str(), cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{cancelButtonLabel.c_str(), cmCancel, bfNormal}};
	const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, gap);
	const int textWidth = std::max(widestLineWidth(textLines), metrics.rowWidth);
	const int width = std::min(std::max(46, textWidth + 6), std::max(46, desktopWidth - 4));
	const int height = std::max(8, static_cast<int>(textLines.size()) + 6);
	MRDialogFoundation *dialog = new MRDialogFoundation(centeredRect(width, height), dialogTitle != nullptr ? dialogTitle : "Confirm", width, height);
	int y = 2;

	for (const std::string &line : textLines)
		insertStaticLine(dialog, 3, y++, line);
	mr::dialogs::insertUniformButtonRow(*dialog, (width - metrics.rowWidth) / 2, height - 3, gap, buttons);
	return mr::dialogs::execDialog(dialog) == cmOK;
}

UnsavedChangesChoice runDialogDirtyListGating(const char *dialogTitle, const char *headline, const char *itemsLabel, const std::vector<std::string> &dirtyItems, const char *primaryLabel) {
	if (dirtyItems.empty()) return runDialogDirtyGating(headline, primaryLabel);
	std::string joinedItems = joinCommaSeparatedItems(dirtyItems);

	TDirtyItemDialog *dialog = new TDirtyItemDialog(dialogTitle, headline, itemsLabel, joinedItems.c_str(), primaryLabel);
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
