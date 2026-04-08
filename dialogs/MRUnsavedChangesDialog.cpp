#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TRect
#define Uses_TDialog
#define Uses_TButton
#define Uses_TStaticText
#define Uses_TObject
#include <tvision/tv.h>

#include "MRUnsavedChangesDialog.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace mr {
namespace dialogs {
namespace {

TRect centeredRect(int width, int height) {
	TRect r = TProgram::deskTop->getExtent();
	int left = r.a.x + (r.b.x - r.a.x - width) / 2;
	int top = r.a.y + (r.b.y - r.a.y - height) / 2;
	return TRect(left, top, left + width, top + height);
}

ushort execDialog(TDialog *dialog) {
	ushort result = cmCancel;
	if (dialog != nullptr) {
		result = TProgram::deskTop->execView(dialog);
		TObject::destroy(dialog);
	}
	return result;
}

void insertStaticLine(TDialog *dialog, int x, int y, const std::string &text) {
	dialog->insert(new TStaticText(TRect(x, y, x + static_cast<int>(text.size()) + 1, y + 1), text.c_str()));
}

std::vector<std::string> wrapText(const char *text, std::size_t maxLen) {
	std::vector<std::string> lines;
	std::string value = text != nullptr ? text : "";

	if (value.empty())
		return lines;
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
		if (cut == std::string::npos || cut == 0)
			cut = maxLen;
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

	if (text.empty() || text.find('~') != std::string::npos)
		return text;

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
	if (mark == std::string::npos)
		return text;

	std::string out;
	out.reserve(text.size() + 2);
	out.append(text, 0, mark);
	out.push_back('~');
	out.push_back(text[mark]);
	out.push_back('~');
	out.append(text, mark + 1, std::string::npos);
	return out;
}

} // namespace

UnsavedChangesChoice showUnsavedChangesDialog(const char *primaryLabel, const char *headline,
                                              const char *detail) {
	const bool hasDetail = detail != nullptr && *detail != '\0';
	std::string label = primaryLabel != nullptr && *primaryLabel != '\0' ? primaryLabel : "Save";
	std::string primaryButtonLabel = addMnemonic(label, 's');
	std::string discardButtonLabel = addMnemonic("Discard", 'd');
	std::string cancelButtonLabel = addMnemonic("Cancel", 'c');
	const int primaryButtonWidth = std::max(10, strwidth(label.c_str()) + 4);
	const int discardButtonWidth = 11;
	const int cancelButtonWidth = 10;
	const int gap = 2;
	const int buttonRowWidth = primaryButtonWidth + discardButtonWidth + cancelButtonWidth + 2 * gap;
	const int desktopWidth = TProgram::deskTop != nullptr ? TProgram::deskTop->size.x : 80;
	const int maxTextWidth = std::max(32, desktopWidth - 12);
	std::vector<std::string> textLines = wrapText(headline != nullptr ? headline : "Window has unsaved changes.",
	                                              static_cast<std::size_t>(maxTextWidth));

	if (hasDetail) {
		std::vector<std::string> detailLines = wrapText(detail, static_cast<std::size_t>(maxTextWidth));
		textLines.insert(textLines.end(), detailLines.begin(), detailLines.end());
	}

	const int textWidth = std::max(widestLineWidth(textLines), buttonRowWidth);
	const int width = std::min(std::max(46, textWidth + 6), std::max(46, desktopWidth - 4));
	const int height = std::max(hasDetail ? 10 : 8, static_cast<int>(textLines.size()) + 6);
	TDialog *dialog = new TDialog(centeredRect(width, height), "Confirm");
	int y = 2;

	for (const std::string &line : textLines)
		insertStaticLine(dialog, 3, y++, line);
	int buttonY = height - 3;
	int buttonLeft = (width - buttonRowWidth) / 2;

	dialog->insert(new TButton(TRect(buttonLeft, buttonY, buttonLeft + primaryButtonWidth, buttonY + 2),
	                           primaryButtonLabel.c_str(), cmYes, bfDefault));
	buttonLeft += primaryButtonWidth + gap;
	dialog->insert(new TButton(TRect(buttonLeft, buttonY, buttonLeft + discardButtonWidth, buttonY + 2),
	                           discardButtonLabel.c_str(), cmNo, bfNormal));
	buttonLeft += discardButtonWidth + gap;
	dialog->insert(new TButton(TRect(buttonLeft, buttonY, buttonLeft + cancelButtonWidth, buttonY + 2),
	                           cancelButtonLabel.c_str(), cmCancel, bfNormal));

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
