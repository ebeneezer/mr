#ifndef MRPDFTEXTEXPORTER_HPP
#define MRPDFTEXTEXPORTER_HPP

#include <string>
#include <vector>

class MRPdfTextExporter {
  public:
	struct Settings {
		std::string outputPath;
		std::string pageSeparatorLiteral = "\f";
		double pageWidthPoints = 595.0;
		double pageHeightPoints = 842.0;
		double leftMarginPoints = 50.0;
		double rightMarginPoints = 50.0;
		double topMarginPoints = 50.0;
		double bottomMarginPoints = 50.0;
		std::string fontFamily = "DejaVu Sans Mono";
		int fontSizePoints = 10;
		std::string headerLine;
		std::string footerLine;
		int textWidthColumns = 78;
	};

	[[nodiscard]] static std::vector<std::string> availableFontFamilies();
	[[nodiscard]] bool exportText(const std::string &utf8Text, const Settings &settings, std::string *errorMessage = nullptr) const;
};

#endif
