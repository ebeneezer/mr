#include "MRPdfTextExporter.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <cairo-pdf.h>
#include <glib.h>
#include <pango/pangocairo.h>

namespace {

bool setError(std::string *errorMessage, const std::string &text) {
	if (errorMessage != nullptr) *errorMessage = text;
	return false;
}

std::vector<std::string> splitLogicalPages(std::string_view text, std::string_view separator) {
	std::vector<std::string> pages;
	std::size_t start = 0;

	if (separator.empty()) {
		pages.emplace_back(text);
		return pages;
	}
	while (start <= text.size()) {
		const std::size_t pos = text.find(separator, start);
		if (pos == std::string_view::npos) {
			pages.emplace_back(text.substr(start));
			break;
		}
		pages.emplace_back(text.substr(start, pos - start));
		start = pos + separator.size();
		if (start == text.size()) {
			pages.emplace_back(std::string());
			break;
		}
	}
	return pages;
}

std::vector<std::string> splitHardLines(std::string_view text) {
	std::vector<std::string> lines;
	std::size_t start = 0;

	while (start <= text.size()) {
		const std::size_t pos = text.find('\n', start);
		if (pos == std::string_view::npos) {
			std::string line(text.substr(start));
			if (!line.empty() && line.back() == '\r') line.pop_back();
			lines.push_back(std::move(line));
			break;
		}
		std::string line(text.substr(start, pos - start));
		if (!line.empty() && line.back() == '\r') line.pop_back();
		lines.push_back(std::move(line));
		start = pos + 1;
		if (start == text.size()) {
			lines.emplace_back();
			break;
		}
	}
	return lines;
}

std::vector<std::string> wrapVisualLines(PangoLayout *layout, std::string_view line) {
	std::vector<std::string> wrapped;
	const GSList *node = nullptr;

	if (layout == nullptr) return wrapped;
	if (line.empty()) {
		wrapped.emplace_back();
		return wrapped;
	}
	pango_layout_set_text(layout, line.data(), static_cast<int>(line.size()));
	for (node = pango_layout_get_lines_readonly(layout); node != nullptr; node = node->next) {
		const PangoLayoutLine *visualLine = static_cast<const PangoLayoutLine *>(node->data);
		if (visualLine == nullptr) continue;
		wrapped.emplace_back(line.substr(static_cast<std::size_t>(visualLine->start_index), static_cast<std::size_t>(visualLine->length)));
	}
	if (wrapped.empty()) wrapped.emplace_back(line);
	return wrapped;
}

double layoutHeightForText(PangoLayout *layout, std::string_view text, double fallbackHeight) {
	int width = 0;
	int height = 0;

	if (layout == nullptr) return fallbackHeight;
	pango_layout_set_text(layout, text.data(), static_cast<int>(text.size()));
	pango_layout_get_size(layout, &width, &height);
	if (height <= 0) return fallbackHeight;
	return static_cast<double>(height) / static_cast<double>(PANGO_SCALE);
}

bool validateSettings(const MRPdfTextExporter::Settings &settings, std::string *errorMessage) {
	if (settings.outputPath.empty()) return setError(errorMessage, "PDF export path is empty.");
	if (settings.pageSeparatorLiteral.empty()) return setError(errorMessage, "Page separator literal must not be empty.");
	if (settings.fontFamily.empty()) return setError(errorMessage, "Font family must not be empty.");
	if (settings.fontSizePoints < 1 || settings.fontSizePoints > 40) return setError(errorMessage, "Font size must be within 1..40.");
	if (!(settings.pageWidthPoints > 0.0) || !(settings.pageHeightPoints > 0.0)) return setError(errorMessage, "Page size must be positive.");
	if (settings.leftMarginPoints < 0.0 || settings.rightMarginPoints < 0.0 || settings.topMarginPoints < 0.0 || settings.bottomMarginPoints < 0.0) {
		return setError(errorMessage, "Margins must not be negative.");
	}
	if (settings.leftMarginPoints + settings.rightMarginPoints >= settings.pageWidthPoints) return setError(errorMessage, "Horizontal margins leave no printable width.");
	if (settings.topMarginPoints + settings.bottomMarginPoints >= settings.pageHeightPoints) return setError(errorMessage, "Vertical margins leave no printable height.");
	if (settings.textWidthColumns < 0) return setError(errorMessage, "Text width must not be negative.");
	return true;
}

std::string buildFontDescription(const MRPdfTextExporter::Settings &settings) {
	char buffer[512] = {0};
	std::snprintf(buffer, sizeof(buffer), "%s %d", settings.fontFamily.c_str(), settings.fontSizePoints);
	return std::string(buffer);
}

} // namespace

std::vector<std::string> MRPdfTextExporter::availableFontFamilies() {
	std::vector<std::string> familiesOut;
	PangoFontMap *fontMap = pango_cairo_font_map_get_default();
	PangoFontFamily **families = nullptr;
	int familyCount = 0;
	std::set<std::string> uniqueFamilies;

	if (fontMap != nullptr) pango_font_map_list_families(fontMap, &families, &familyCount);
	for (int i = 0; i < familyCount; ++i) {
		const char *name = families[i] != nullptr ? pango_font_family_get_name(families[i]) : nullptr;
		if (name != nullptr && *name != '\0') uniqueFamilies.insert(name);
	}
	if (families != nullptr) g_free(families);
	familiesOut.assign(uniqueFamilies.begin(), uniqueFamilies.end());
	if (familiesOut.empty()) familiesOut = {"DejaVu Sans Mono", "Monospace"};
	return familiesOut;
}

bool MRPdfTextExporter::exportText(const std::string &utf8Text, const Settings &settings, std::string *errorMessage) const {
	cairo_surface_t *surface = nullptr;
	cairo_t *cr = nullptr;
	PangoLayout *wrapLayout = nullptr;
	PangoLayout *renderLayout = nullptr;
	PangoFontDescription *font = nullptr;
	PangoFontMetrics *metrics = nullptr;
	const std::string fontDescription = buildFontDescription(settings);
	double effectiveWidth = 0.0;
	double contentTop = settings.topMarginPoints;
	double contentBottom = settings.pageHeightPoints - settings.bottomMarginPoints;
	double y = 0.0;
	double baseLineHeight = 0.0;
	double headerHeight = 0.0;
	double footerHeight = 0.0;
	double headerGap = 0.0;
	double footerGap = 0.0;
	std::vector<std::string> pages;

	if (!validateSettings(settings, errorMessage)) return false;
	if (!g_utf8_validate(utf8Text.c_str(), static_cast<gssize>(utf8Text.size()), nullptr)) return setError(errorMessage, "Current window text is not valid UTF-8.");

	surface = cairo_pdf_surface_create(settings.outputPath.c_str(), settings.pageWidthPoints, settings.pageHeightPoints);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		const std::string text = cairo_status_to_string(cairo_surface_status(surface));
		if (surface != nullptr) cairo_surface_destroy(surface);
		return setError(errorMessage, "Unable to create PDF surface: " + text);
	}
	cr = cairo_create(surface);
	if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
		const std::string text = cairo_status_to_string(cairo_status(cr));
		cairo_destroy(cr);
		cairo_surface_destroy(surface);
		return setError(errorMessage, "Unable to create Cairo context: " + text);
	}

	wrapLayout = pango_cairo_create_layout(cr);
	renderLayout = pango_cairo_create_layout(cr);
	font = pango_font_description_from_string(fontDescription.c_str());
	if (wrapLayout == nullptr || renderLayout == nullptr || font == nullptr) {
		if (font != nullptr) pango_font_description_free(font);
		if (wrapLayout != nullptr) g_object_unref(wrapLayout);
		if (renderLayout != nullptr) g_object_unref(renderLayout);
		cairo_destroy(cr);
		cairo_surface_destroy(surface);
		return setError(errorMessage, "Unable to initialize Pango layout.");
	}

	pango_layout_set_font_description(wrapLayout, font);
	pango_layout_set_font_description(renderLayout, font);
	pango_layout_set_wrap(wrapLayout, PANGO_WRAP_WORD_CHAR);

	effectiveWidth = settings.pageWidthPoints - settings.leftMarginPoints - settings.rightMarginPoints;
	if (settings.textWidthColumns > 0) {
		metrics = pango_context_get_metrics(pango_layout_get_context(wrapLayout), font, pango_language_get_default());
		if (metrics != nullptr) {
			const double characterWidth = static_cast<double>(pango_font_metrics_get_approximate_digit_width(metrics)) / static_cast<double>(PANGO_SCALE);
			if (characterWidth > 0.0) effectiveWidth = std::min(effectiveWidth, characterWidth * static_cast<double>(settings.textWidthColumns));
			pango_font_metrics_unref(metrics);
			metrics = nullptr;
		}
	}
	if (!(effectiveWidth > 0.0)) {
		pango_font_description_free(font);
		g_object_unref(wrapLayout);
		g_object_unref(renderLayout);
		cairo_destroy(cr);
		cairo_surface_destroy(surface);
		return setError(errorMessage, "Effective PDF layout width is not positive.");
	}

	pango_layout_set_width(wrapLayout, static_cast<int>(std::lround(effectiveWidth * static_cast<double>(PANGO_SCALE))));
	baseLineHeight = layoutHeightForText(renderLayout, "Ag", 12.0);
	if (!settings.headerLine.empty()) {
		headerHeight = layoutHeightForText(renderLayout, settings.headerLine, baseLineHeight);
		headerGap = baseLineHeight * 0.5;
	}
	if (!settings.footerLine.empty()) {
		footerHeight = layoutHeightForText(renderLayout, settings.footerLine, baseLineHeight);
		footerGap = baseLineHeight * 0.5;
	}
	contentTop += headerHeight + headerGap;
	contentBottom -= footerHeight + footerGap;
	if (contentBottom <= contentTop) {
		pango_font_description_free(font);
		g_object_unref(wrapLayout);
		g_object_unref(renderLayout);
		cairo_destroy(cr);
		cairo_surface_destroy(surface);
		return setError(errorMessage, "Header, footer and margins leave no printable height.");
	}
	y = contentTop;
	pages = splitLogicalPages(utf8Text, settings.pageSeparatorLiteral);
	if (pages.empty()) pages.emplace_back();

	for (std::size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
		const std::string &pageText = pages[pageIndex];
		auto renderPageFurniture = [&]() {
			if (!settings.headerLine.empty()) {
				pango_layout_set_text(renderLayout, settings.headerLine.c_str(), static_cast<int>(settings.headerLine.size()));
				cairo_move_to(cr, settings.leftMarginPoints, settings.topMarginPoints);
				pango_cairo_show_layout(cr, renderLayout);
			}
			if (!settings.footerLine.empty()) {
				pango_layout_set_text(renderLayout, settings.footerLine.c_str(), static_cast<int>(settings.footerLine.size()));
				cairo_move_to(cr, settings.leftMarginPoints, settings.pageHeightPoints - settings.bottomMarginPoints - footerHeight);
				pango_cairo_show_layout(cr, renderLayout);
			}
		};

		renderPageFurniture();
		if (!pageText.empty()) {
			const std::vector<std::string> hardLines = splitHardLines(pageText);
			for (const std::string &hardLine : hardLines) {
				const std::vector<std::string> visualLines = wrapVisualLines(wrapLayout, hardLine);
				for (const std::string &visualLine : visualLines) {
					const double lineHeight = visualLine.empty() ? baseLineHeight : layoutHeightForText(renderLayout, visualLine, baseLineHeight);
					if (y + lineHeight > contentBottom && y > contentTop) {
						cairo_show_page(cr);
						renderPageFurniture();
						y = contentTop;
					}
					if (!visualLine.empty()) {
						pango_layout_set_text(renderLayout, visualLine.c_str(), static_cast<int>(visualLine.size()));
						cairo_move_to(cr, settings.leftMarginPoints, y);
						pango_cairo_show_layout(cr, renderLayout);
					}
					y += lineHeight;
				}
			}
		}
		if (pageIndex + 1 < pages.size()) {
			cairo_show_page(cr);
			y = contentTop;
		}
	}

	cairo_surface_finish(surface);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		const std::string text = cairo_status_to_string(cairo_surface_status(surface));
		pango_font_description_free(font);
		g_object_unref(wrapLayout);
		g_object_unref(renderLayout);
		cairo_destroy(cr);
		cairo_surface_destroy(surface);
		return setError(errorMessage, "PDF export failed: " + text);
	}

	if (errorMessage != nullptr) errorMessage->clear();
	pango_font_description_free(font);
	g_object_unref(wrapLayout);
	g_object_unref(renderLayout);
	cairo_destroy(cr);
	cairo_surface_destroy(surface);
	return true;
}
