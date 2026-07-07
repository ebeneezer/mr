#ifndef MRCOMMANDROUTERTEXT_HPP
#define MRCOMMANDROUTERTEXT_HPP

#include <tvision/tv.h>

class MREditWindow;

struct MRRouterIntegerInputLayout {
	short width = 52;
	short height = 10;
	short inputLeft = 18;
	short inputRight = 48;
	short buttonY = 6;
	short buttonLeft = 8;
	short buttonGap = 2;
	bool showHelp = true;
};

[[nodiscard]] MRRouterIntegerInputLayout defaultRouterIntegerInputLayout();
[[nodiscard]] bool promptRouterIntegerValue(const char *title, const char *label, const char *helpText, int initialValue, int minValue, int maxValue, int &outValue);
[[nodiscard]] bool promptRouterIntegerValue(const char *title, const char *label, const char *helpText, int initialValue, int minValue, int maxValue, int &outValue, const MRRouterIntegerInputLayout &layout);
[[nodiscard]] bool handleSetRightMargin();
[[nodiscard]] bool handleSetLeftMargin(MREditWindow *window);
[[nodiscard]] bool handleToggleWordWrap();
[[nodiscard]] bool handleToggleFormatRuler();
[[nodiscard]] bool handleReformatParagraph(MREditWindow *window);
[[nodiscard]] bool handleReformatDocument(MREditWindow *window);
[[nodiscard]] bool handleJustifyParagraph(MREditWindow *window);
[[nodiscard]] bool handleCenterLine(MREditWindow *window);

#endif
