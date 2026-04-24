#include "../app/utils/MRStringUtils.hpp"
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
#include <functional>
#include <string>
#include <string_view>
#include <vector>

class TDialog;
class TGroup;
class TRect;
class TButton;
class MRDialogFoundation;

namespace mr::dialogs {

[[nodiscard]] TRect centeredDialogRect(int width, int height);
[[nodiscard]] ushort execDialog(TDialog *dialog);
[[nodiscard]] ushort execDialogWithData(TDialog *dialog, void *data);
[[nodiscard]] MRDialogFoundation *createScrollableDialog(const char *title, int virtualWidth,
                                                         int virtualHeight);

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
	return execDialog(dialog);
}

[[nodiscard]] inline ushort execDialogRawWithData(TDialog *dialog, void *data) {
	return execDialogWithData(dialog, data);
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
	struct DialogValidationResult {
		bool valid = true;
		std::string warningText;
	};
	using DialogValidationHook = std::function<DialogValidationResult()>;

	struct ManagedItem {
		TView *view;
		TRect base;
	};

	MRScrollableDialog(const TRect &bounds, const char *title, int virtualWidth,
	                   int virtualHeight);
	~MRScrollableDialog() override;
	void handleEvent(TEvent &event) override;

	void addManaged(TView *view, const TRect &base);
	void initScrollIfNeeded();
	void selectContent();
	void scrollToOrigin();
	void setDialogValidationHook(DialogValidationHook hook);
	void runDialogValidation();
	[[nodiscard]] TGroup *managedContent() const noexcept { return content_; }

  private:
	void detectDoneButton(TView *view);
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
	TButton *doneButton = nullptr;
	DialogValidationHook dialogValidationHook;
	bool isRunningDialogValidation = false;
	bool hasDialogValidationWarning = false;
	std::string lastDialogValidationWarning;
};

class MRDialogFoundation : public MRScrollableDialog {
  public:
	MRDialogFoundation(const TRect &bounds, const char *title, int virtualWidth, int virtualHeight);

	void insert(TView *view);
	void finalizeLayout();

  private:
	bool layoutFinalized_ = false;
};

#endif
