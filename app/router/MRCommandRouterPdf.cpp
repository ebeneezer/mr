#define Uses_MsgBox
#include <tvision/tv.h>

#include "MRCommandRouterPdf.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "../export/MRPdfTextExporter.hpp"
#include "../utils/MRStringUtils.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../config/settings/MRSettingsStorage.hpp"
#include "../../dialogs/MRPdfExportDialog.hpp"
#include "../../dialogs/setup/MRSetupCommon.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/MRFileEditor/MRFileEditor.hpp"
#include "../../ui/MRMessageLineController.hpp"
#include "../../ui/MRWindowSupport.hpp"

namespace {

void postPdfExportError(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, std::string(text), mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}

bool parseIntFieldInRange(std::string_view text, int minValue, int maxValue, int &value) {
	char *end = nullptr;
	const std::string trimmed = trimAscii(text);
	long parsed = 0;

	if (trimmed.empty()) return false;
	errno = 0;
	parsed = std::strtol(trimmed.c_str(), &end, 10);
	if (errno != 0 || end == nullptr || *end != '\0' || parsed < minValue || parsed > maxValue || parsed > std::numeric_limits<int>::max()) return false;
	value = static_cast<int>(parsed);
	return true;
}

bool decodePdfExportSeparatorLiteral(std::string_view input, std::string &decoded, std::string &errorText) {
	decoded.clear();
	for (std::size_t i = 0; i < input.size(); ++i) {
		const char ch = input[i];
		if (ch != '\\') {
			decoded.push_back(ch);
			continue;
		}
		if (i + 1 >= input.size()) {
			errorText = "Page separator literal ends with a dangling backslash.";
			return false;
		}
		const char next = input[++i];
		switch (next) {
			case 'f':
				decoded.push_back('\f');
				break;
			case 'n':
				decoded.push_back('\n');
				break;
			case 'r':
				decoded.push_back('\r');
				break;
			case 't':
				decoded.push_back('\t');
				break;
			case '\\':
				decoded.push_back('\\');
				break;
			default:
				errorText = "Unsupported page separator escape sequence.";
				return false;
		}
	}
	if (decoded.empty()) {
		errorText = "Page separator literal must not be empty.";
		return false;
	}
	errorText.clear();
	return true;
}

std::string defaultPdfExportPathForWindow(const MREditWindow *window) {
	std::filesystem::path path;

	if (window != nullptr && window->currentFileName()[0] != '\0') {
		path = std::filesystem::path(normalizeConfiguredPathInput(window->currentFileName()));
		path.replace_extension(".pdf");
		return path.string();
	}
	path = std::filesystem::path("mr-export.pdf");
	return path.string();
}

MRPdfExportSettings pdfExportSettingsFromDialogData(const MRPdfExportDialogData &data) {
	MRPdfExportSettings settings;

	settings.outputPath = data.outputPath;
	settings.pageSeparatorLiteral = data.pageSeparatorLiteral;
	settings.fontFamily = data.fontFamily;
	settings.fontSizePoints = std::clamp<int>(data.fontSizePoints, 1, 40);
	settings.headerLine = data.headerLine;
	settings.footerLine = data.footerLine;
	settings.textWidth = data.textWidth;
	settings.leftMarginPoints = data.leftMarginPoints;
	settings.rightMarginPoints = data.rightMarginPoints;
	settings.topMarginPoints = data.topMarginPoints;
	settings.bottomMarginPoints = data.bottomMarginPoints;
	return settings;
}

void loadPdfExportDialogData(MRPdfExportDialogData &dialogData, const MRPdfExportSettings &settings, const MREditWindow *window, const MREditSetupSettings &editSettings) {
	mr::dialogs::writeRecordField(dialogData.outputPath, sizeof(dialogData.outputPath), settings.outputPath.empty() ? defaultPdfExportPathForWindow(window) : settings.outputPath);
	mr::dialogs::writeRecordField(dialogData.pageSeparatorLiteral, sizeof(dialogData.pageSeparatorLiteral), settings.pageSeparatorLiteral);
	mr::dialogs::writeRecordField(dialogData.fontFamily, sizeof(dialogData.fontFamily), settings.fontFamily);
	dialogData.fontSizePoints = std::clamp<int>(settings.fontSizePoints, 1, 40);
	mr::dialogs::writeRecordField(dialogData.headerLine, sizeof(dialogData.headerLine), settings.headerLine);
	mr::dialogs::writeRecordField(dialogData.footerLine, sizeof(dialogData.footerLine), settings.footerLine);
	mr::dialogs::writeRecordField(dialogData.textWidth, sizeof(dialogData.textWidth), settings.textWidth.empty() ? std::to_string(editSettings.rightMargin > 0 ? editSettings.rightMargin : 78) : settings.textWidth);
	mr::dialogs::writeRecordField(dialogData.leftMarginPoints, sizeof(dialogData.leftMarginPoints), settings.leftMarginPoints);
	mr::dialogs::writeRecordField(dialogData.rightMarginPoints, sizeof(dialogData.rightMarginPoints), settings.rightMarginPoints);
	mr::dialogs::writeRecordField(dialogData.topMarginPoints, sizeof(dialogData.topMarginPoints), settings.topMarginPoints);
	mr::dialogs::writeRecordField(dialogData.bottomMarginPoints, sizeof(dialogData.bottomMarginPoints), settings.bottomMarginPoints);
}

bool persistPdfExportDialogState(const MRPdfExportDialogData &dialogData, std::string &errorText) {
	if (!setConfiguredPdfExportSettings(pdfExportSettingsFromDialogData(dialogData), &errorText)) return false;
	if (!persistConfiguredSettingsSnapshot(&errorText)) return false;
	errorText.clear();
	return true;
}

bool buildPdfExportSettings(const MRPdfExportDialogData &data, MRPdfTextExporter::Settings &settings, std::string &errorText) {
	std::string separatorLiteral;
	std::filesystem::path outputPath;

	settings = MRPdfTextExporter::Settings();
	settings.outputPath = normalizeConfiguredPathInput(trimAscii(data.outputPath));
	if (settings.outputPath.empty()) {
		errorText = "Output path must not be empty.";
		return false;
	}
	outputPath = std::filesystem::path(settings.outputPath);
	if (!outputPath.has_extension()) outputPath.replace_extension(".pdf");
	settings.outputPath = outputPath.string();
	if (std::filesystem::exists(outputPath) && std::filesystem::is_directory(outputPath)) {
		errorText = "Output path points to a directory.";
		return false;
	}
	if (outputPath.has_parent_path() && !std::filesystem::exists(outputPath.parent_path())) {
		errorText = "Output directory does not exist.";
		return false;
	}
	if (!decodePdfExportSeparatorLiteral(trimAscii(data.pageSeparatorLiteral), separatorLiteral, errorText)) return false;
	settings.pageSeparatorLiteral = separatorLiteral;
	settings.fontFamily = trimAscii(data.fontFamily);
	if (settings.fontFamily.empty()) {
		errorText = "Font family must not be empty.";
		return false;
	}
	settings.fontSizePoints = std::clamp<int>(data.fontSizePoints, 1, 40);
	settings.headerLine = data.headerLine;
	settings.footerLine = data.footerLine;
	if (!parseIntFieldInRange(data.textWidth, 0, 9999, settings.textWidthColumns)) {
		errorText = "Right margin column must be an integer within 0..9999.";
		return false;
	}
	int marginValue = 0;
	if (!parseIntFieldInRange(data.leftMarginPoints, 0, 9999, marginValue)) {
		errorText = "Margins must be integers within 0..9999.";
		return false;
	}
	settings.leftMarginPoints = static_cast<double>(marginValue);
	if (!parseIntFieldInRange(data.rightMarginPoints, 0, 9999, marginValue)) {
		errorText = "Margins must be integers within 0..9999.";
		return false;
	}
	settings.rightMarginPoints = static_cast<double>(marginValue);
	if (!parseIntFieldInRange(data.topMarginPoints, 0, 9999, marginValue)) {
		errorText = "Margins must be integers within 0..9999.";
		return false;
	}
	settings.topMarginPoints = static_cast<double>(marginValue);
	if (!parseIntFieldInRange(data.bottomMarginPoints, 0, 9999, marginValue)) {
		errorText = "Margins must be integers within 0..9999.";
		return false;
	}
	settings.bottomMarginPoints = static_cast<double>(marginValue);
	errorText.clear();
	return true;
}

bool capturePdfExportText(MREditWindow &window, MRPdfExportSource source, std::string &text, std::string &errorText) {
	MRFileEditor *editor = window.getEditor();

	text.clear();
	errorText.clear();
	if (editor == nullptr) {
		errorText = "No active file window.";
		return false;
	}
	if (source == pdfExportCurrentFile) {
		text = editor->snapshotText();
		return true;
	}
	if (!window.captureBlockPayload(text, &errorText)) return false;
	if (window.blockStatus() != MREditWindow::bmColumn) return true;

	const int rowCountValue = window.blockLine2() - window.blockLine1() + 1;
	const int columnWidthValue = window.blockCol2() - window.blockCol1();
	if (rowCountValue <= 0 || columnWidthValue <= 0) {
		errorText = "Column block payload geometry is invalid.";
		text.clear();
		return false;
	}
	const std::size_t rowCount = static_cast<std::size_t>(rowCountValue);
	const std::size_t columnWidth = static_cast<std::size_t>(columnWidthValue);
	if (text.size() != rowCount * columnWidth) {
		errorText = "Column block payload geometry is invalid.";
		text.clear();
		return false;
	}
	std::string projected;
	projected.reserve(text.size() + rowCount - 1);
	for (std::size_t row = 0; row < rowCount; ++row) {
		if (row != 0) projected.push_back('\n');
		projected.append(text, row * columnWidth, columnWidth);
	}
	text = std::move(projected);
	return true;
}

bool confirmPdfExportOverwrite(const std::string &path) {
	return !std::filesystem::exists(path) || messageBox(mfConfirmation | mfYesButton | mfNoButton, "PDF file exists.\nOverwrite?\n%s", path.c_str()) == cmYes;
}

} // namespace

bool handleExportToPdf() {
	MREditWindow *window = currentEditWindow();
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	MRPdfTextExporter exporter;
	MRPdfTextExporter::Settings settings;
	MRPdfExportDialogData dialogData;
	const MRPdfExportSettings persistedDialogSettings = configuredPdfExportSettings();
	const MREditSetupSettings editSettings = configuredEditSetupSettings();
	std::string documentText;
	std::string errorText;

	if (window == nullptr || editor == nullptr) {
		postPdfExportError("No active file window.");
		return true;
	}

	loadPdfExportDialogData(dialogData, persistedDialogSettings, window, editSettings);
	while (true) {
		const ushort result = runPdfExportDialog(dialogData, window->hasBlock());
		if (result == cmCancel || result == cmClose) return true;
		if (!persistPdfExportDialogState(dialogData, errorText)) {
			postPdfExportError("PDF export settings save failed: " + errorText);
			return true;
		}
		if (!buildPdfExportSettings(dialogData, settings, errorText)) {
			postPdfExportError(errorText);
			continue;
		}
		if (!capturePdfExportText(*window, static_cast<MRPdfExportSource>(dialogData.source), documentText, errorText)) {
			postPdfExportError(errorText.empty() ? "No block marked." : errorText);
			continue;
		}
		if (!confirmPdfExportOverwrite(settings.outputPath)) continue;
		if (!exporter.exportText(documentText, settings, &errorText)) {
			postPdfExportError(errorText);
			continue;
		}
		mr::dialogs::writeRecordField(dialogData.outputPath, sizeof(dialogData.outputPath), settings.outputPath);
		if (!persistPdfExportDialogState(dialogData, errorText)) {
			postPdfExportError("PDF export settings save failed: " + errorText);
			return true;
		}
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "PDF exported: " + settings.outputPath, mr::messageline::Kind::Info, mr::messageline::kPriorityMedium);
		return true;
	}
}
