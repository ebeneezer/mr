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
	if (dialog != 0) {
		result = TProgram::deskTop->execView(dialog);
		TObject::destroy(dialog);
	}
	return result;
}

void insertStaticLine(TDialog *dialog, int x, int y, const std::string &text) {
	dialog->insert(new TStaticText(TRect(x, y, x + static_cast<int>(text.size()) + 1, y + 1), text.c_str()));
}

std::string shortenDetail(const char *detail, std::size_t maxLen) {
	std::string value = detail != nullptr ? detail : "";
	if (value.size() <= maxLen)
		return value;
	if (maxLen <= 3)
		return value.substr(0, maxLen);
	return value.substr(0, maxLen - 3) + "...";
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
	const int headlineWidth = headline != nullptr ? strwidth(headline) : strwidth("Window has unsaved changes.");
	const int primaryButtonWidth = std::max(10, strwidth(label.c_str()) + 4);
	const int discardButtonWidth = 11;
	const int cancelButtonWidth = 10;
	const int gap = 2;
	const int buttonRowWidth = primaryButtonWidth + discardButtonWidth + cancelButtonWidth + 2 * gap;
	const int width = std::min(50, std::max(hasDetail ? 44 : 40, std::max(headlineWidth + 6, buttonRowWidth + 6)));
	const int height = hasDetail ? 9 : 8;
	TDialog *dialog = new TDialog(centeredRect(width, height), "Confirm");

	insertStaticLine(dialog, 3, 2, headline != nullptr ? headline : "Window has unsaved changes.");
	if (hasDetail)
		insertStaticLine(dialog, 3, 4, shortenDetail(detail, static_cast<std::size_t>(width - 6)).c_str());
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
