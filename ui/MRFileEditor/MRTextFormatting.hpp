#pragma once

#include <string>
#include <string_view>

#include "../../config/MRDialogPaths.hpp"

class MRTextFormatting {
  public:
	struct NormalizedFormatLine {
		std::string line;
		int leftMargin = 1;
		int rightMargin = 78;
	};

	static NormalizedFormatLine normalizedFormatLine(const MREditSetupSettings &settings);
	static void effectiveMargins(const MREditSetupSettings &settings, int &leftMargin, int &rightMargin) noexcept;
	static std::string formatParagraphText(std::string_view paragraphText, int leftMargin, int rightMargin);
	static std::string justifyParagraphText(std::string_view paragraphText, int leftMargin, int rightMargin);
};
