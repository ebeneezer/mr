#ifndef MRPDFEXPORTDIALOG_HPP
#define MRPDFEXPORTDIALOG_HPP

#include <tvision/tv.h>

#include <cstdint>
#include <cstring>

enum MRPdfExportSource : ushort {
	pdfExportCurrentFile = 0,
	pdfExportMarkedBlock = 1
};

struct MRPdfExportDialogData {
	ushort source;
	char outputPath[512];
	char pageSeparatorLiteral[64];
	char fontFamily[128];
	int32_t fontSizePoints;
	char headerLine[128];
	char footerLine[128];
	char textWidth[16];
	char leftMarginPoints[16];
	char rightMarginPoints[16];
	char topMarginPoints[16];
	char bottomMarginPoints[16];

	MRPdfExportDialogData() noexcept : source(pdfExportCurrentFile), outputPath(), pageSeparatorLiteral(), fontFamily(), fontSizePoints(10), headerLine(), footerLine(), textWidth(), leftMarginPoints(), rightMarginPoints(), topMarginPoints(), bottomMarginPoints() {
		std::memset(outputPath, 0, sizeof(outputPath));
		std::memset(pageSeparatorLiteral, 0, sizeof(pageSeparatorLiteral));
		std::memset(fontFamily, 0, sizeof(fontFamily));
		std::memset(headerLine, 0, sizeof(headerLine));
		std::memset(footerLine, 0, sizeof(footerLine));
		std::memset(textWidth, 0, sizeof(textWidth));
		std::memset(leftMarginPoints, 0, sizeof(leftMarginPoints));
		std::memset(rightMarginPoints, 0, sizeof(rightMarginPoints));
		std::memset(topMarginPoints, 0, sizeof(topMarginPoints));
		std::memset(bottomMarginPoints, 0, sizeof(bottomMarginPoints));
	}
};

[[nodiscard]] ushort runPdfExportDialog(MRPdfExportDialogData &data, bool markedBlockAvailable);

#endif
