#pragma once

#include <cstddef>
#include <string>

#include "../../config/settings/MRSettingsRuntime.hpp"

class MRTextViewportLayout {
  public:
	struct Inputs {
		int viewWidth = 0;
		int visibleRows = 0;
		int deltaX = 0;
		int deltaY = 0;
		int codeFoldingColumns = 1;
		bool fileCompareGutterEnabled = false;
		bool debugGutterEnabled = false;
		std::string debugGutterPosition = "LEADING";
		bool gutterSidesConfigured = false;
		std::string leadingGutters;
		std::string trailingGutters;
		bool exactLineCountKnown = false;
		std::size_t exactLineCount = 0;
		std::size_t estimatedLineCount = 0;
	};

	struct Geometry {
		int gutterWidth = 0;
		int rightInset = 0;
		int topInset = 0;
		int lineNumberX = 0;
		int lineNumberWidth = 0;
		int codeFoldingX = 0;
		int codeFoldingWidth = 0;
		int debugGutterX = 0;
		int debugGutterWidth = 0;
		int fileCompareLeadingGutterX = 0;
		int fileCompareLeadingGutterWidth = 0;
		int fileCompareTrailingGutterX = 0;
		int fileCompareTrailingGutterWidth = 0;
		int miniMapBodyWidth = 0;
		int miniMapTotalWidth = 0;
		int miniMapLeadingInfoX = -1;
		int miniMapLeadingBodyX = -1;
		int miniMapLeadingSeparatorX = -1;
		int miniMapTrailingInfoX = -1;
		int miniMapTrailingBodyX = -1;
		int miniMapTrailingSeparatorX = -1;
		int textLeft = 0;
		int textRight = 1;
		int width = 1;
		int deltaX = 0;
		int deltaY = 0;

		bool containsTextX(long long x) const noexcept;
		bool containsTextPoint(long long x, long long y, int viewHeight) const noexcept;
		int textColumnFromLocalX(int localX) const noexcept;
		long long localXFromVisualColumn(long long visualColumn) const noexcept;
	};

	static std::string normalizedLineNumbersPosition(const MREditSetupSettings &settings);
	static std::string normalizedCodeFoldingPosition(const MREditSetupSettings &settings);
	static Geometry geometryFor(const MREditSetupSettings &settings, const Inputs &inputs) noexcept;
	static bool shouldShowCursor(const Geometry &geometry, long long x, long long y, int viewHeight, bool viewActive, bool viewSelected) noexcept;
};
