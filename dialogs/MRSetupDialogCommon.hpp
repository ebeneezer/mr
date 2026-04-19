#ifndef MRSETUPDIALOGCOMMON_HPP
#define MRSETUPDIALOGCOMMON_HPP

#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TEvent
#define Uses_TGroup
#define Uses_TProgram
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TView
#include <tvision/tv.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

class TDialog;
class TGroup;
class TRect;

namespace mr::dialogs {

[[nodiscard]] inline std::string trimAscii(std::string_view value) {
	std::size_t start = 0;
	std::size_t end = value.size();

	while (start < end && std::isspace(static_cast<unsigned char>(value[start])) != 0)
		++start;
	while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
		--end;
	return std::string(value.substr(start, end - start));
}

[[nodiscard]] inline std::string normalizeTvPathSeparators(std::string_view value) {
	std::string path(value);
	for (char &ch : path)
		if (ch == '\\')
			ch = '/';
	return path;
}

[[nodiscard]] inline bool hasMrmacExtension(std::string_view path) {
	const std::size_t dotPos = path.rfind('.');
	if (dotPos == std::string_view::npos)
		return false;

	const std::string_view ext = path.substr(dotPos);
	constexpr std::string_view kMrmacExtension = ".mrmac";
	if (ext.size() != kMrmacExtension.size())
		return false;
	for (std::size_t i = 0; i < ext.size(); ++i)
		if (std::tolower(static_cast<unsigned char>(ext[i])) !=
		    std::tolower(static_cast<unsigned char>(kMrmacExtension[i])))
			return false;
	return true;
}

[[nodiscard]] inline std::string ensureMrmacExtension(std::string_view path) {
	return hasMrmacExtension(path) ? std::string(path) : std::string(path) + ".mrmac";
}

[[nodiscard]] inline std::string readRecordField(const char *value) {
	return trimAscii(value != nullptr ? value : "");
}

inline void writeRecordField(char *dest, std::size_t destSize, std::string_view value) {
	if (dest == nullptr || destSize == 0)
		return;
	std::memset(dest, 0, destSize);
	std::strncpy(dest, std::string(value).c_str(), destSize - 1);
	dest[destSize - 1] = '\0';
}

[[nodiscard]] inline ushort execDialogRaw(TDialog *dialog) {
	ushort result = cmCancel;
	if (dialog != nullptr) {
		result = TProgram::deskTop->execView(dialog);
		TObject::destroy(dialog);
	}
	return result;
}

[[nodiscard]] inline ushort execDialogRawWithData(TDialog *dialog, void *data) {
	ushort result = cmCancel;
	if (dialog != nullptr) {
		if (data != nullptr)
			dialog->setData(data);
		result = TProgram::deskTop->execView(dialog);
		if (result != cmCancel && data != nullptr)
			dialog->getData(data);
		TObject::destroy(dialog);
	}
	return result;
}

} // namespace mr::dialogs

[[nodiscard]] TRect centeredSetupDialogRect(int width, int height);
[[nodiscard]] TGroup *createSetupDialogContentGroup(const TRect &bounds);
void insertSetupStaticLine(TDialog *dialog, int x, int y, const char *text);
[[nodiscard]] TDialog *createSetupSimplePreviewDialog(const char *title, int width, int height,
                                                      const std::vector<std::string> &lines,
                                                      bool showOkCancelHelp);

class MRScrollableDialog : public TDialog {
  public:
	struct ManagedItem {
		TView *view;
		TRect base;
	};

	MRScrollableDialog(const TRect &bounds, const char *title, int virtualWidth,
	                   int virtualHeight);
	void handleEvent(TEvent &event) override;

	void addManaged(TView *view, const TRect &base);
	void initScrollIfNeeded();
	void selectContent();
	void scrollToOrigin();
	[[nodiscard]] TGroup *managedContent() const noexcept { return content_; }

  private:
	void applyScroll();
	void ensureViewVisible(TView *view);
	void ensureCurrentVisible();

	int virtualWidth_ = 0;
	int virtualHeight_ = 0;
	TRect contentRect_;
	TGroup *content_ = nullptr;
	std::vector<ManagedItem> managedViews_;
	TScrollBar *hScrollBar_ = nullptr;
	TScrollBar *vScrollBar_ = nullptr;
};

#endif
