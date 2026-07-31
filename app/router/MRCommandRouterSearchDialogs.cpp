#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TObject
#define Uses_TRect
#define Uses_TView
#define Uses_TDrawBuffer
#include <tvision/tv.h>

#include "MRCommandRouterSearchDialogs.hpp"
#include "MRCommandRouterSearchCore.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>

#include "../../dialogs/setup/MRSetupCommon.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../MRHelpTopics.generated.hpp"
#include "../commands/MRWindowCommands.hpp"

std::string searchSeedFromCurrentSelection() {
	MREditWindow *window = currentEditWindow();
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	std::size_t start = 0;
	std::size_t end = 0;
	std::string text;

	if (editor == nullptr) return std::string();
	start = editor->selectionStartOffset();
	end = editor->selectionEndOffset();
	if (end < start) std::swap(start, end);
	if (start >= end) return std::string();
	text = editor->snapshotText();
	if (start >= text.size()) return std::string();
	end = std::min(end, text.size());
	text = text.substr(start, end - start);
	for (char &ch : text) {
		const unsigned char uch = static_cast<unsigned char>(ch);
		if (ch == '\r' || ch == '\n' || ch == '\t' || uch < 32) ch = ' ';
	}
	return text;
}

PromptReplaceDecision promptReplaceDecisionDialog(const std::string &title, const SearchPreviewParts &preview, const std::string &replacement) {
	class PromptPreviewView : public TView {
	  public:
		PromptPreviewView(const TRect &bounds, const SearchPreviewParts &preview, const std::string &replacement) : TView(bounds), beforeText(preview.matchLine), beforeMatchOffset(preview.matchLineOffset), beforeMatchLength(preview.matchLineLength), afterText(), afterMatchOffset(preview.matchLineOffset), afterMatchLength(replacement.size()) {
			const std::size_t safeOffset = std::min(beforeMatchOffset, beforeText.size());
			const std::size_t safeLength = std::min(beforeMatchLength, beforeText.size() - safeOffset);

			afterText = beforeText.substr(0, safeOffset);
			afterText += replacement;
			if (safeOffset + safeLength <= beforeText.size()) afterText += beforeText.substr(safeOffset + safeLength);
			for (char &ch : afterText)
				if (ch == '\t' || ch == '\r' || ch == '\n' || static_cast<unsigned char>(ch) < 32 || static_cast<unsigned char>(ch) >= 127) ch = ' ';
			beforeMatchOffset = safeOffset;
			beforeMatchLength = safeLength;
		}

		void draw() override {
			TDrawBuffer b;
			const TColorAttr normal = getColor(1);
			const TColorAttr accent = static_cast<TColorAttr>(getColor(3));
			const std::size_t width = static_cast<std::size_t>(std::max<int>(0, size.x));
			const std::size_t beforeLeft = centeredPreviewLeft(beforeText, beforeMatchOffset, beforeMatchLength, width);
			const std::size_t afterLeft = centeredPreviewLeft(afterText, afterMatchOffset, afterMatchLength, width);
			auto drawLine = [&](short y, const std::string &line, std::size_t lineLeft, std::size_t markOffset, std::size_t markLength) {
				const std::size_t effectiveLeft = line.size() <= width ? 0 : std::min(lineLeft, line.size() - width);
				b.moveChar(0, ' ', normal, size.x);
				for (ushort x = 0; x < static_cast<ushort>(size.x); ++x) {
					const std::size_t source = effectiveLeft + static_cast<std::size_t>(x);
					char ch = source < line.size() ? line[source] : ' ';
					const bool inMark = source >= markOffset && source < (markOffset + markLength);
					b.putChar(x, static_cast<uchar>(ch));
					b.putAttribute(x, inMark ? accent : normal);
				}
				writeLine(0, y, size.x, 1, b);
			};

			for (short y = 0; y < size.y; ++y) {
				b.moveChar(0, ' ', normal, size.x);
				writeLine(0, y, size.x, 1, b);
			}
			const short centerY = size.y / 2;
			drawLine(static_cast<short>(std::max(0, centerY - 1)), beforeText, beforeLeft, beforeMatchOffset, beforeMatchLength);
			b.moveChar(0, ' ', normal, size.x);
			b.moveStr(static_cast<ushort>(std::max(0, (size.x - 2) / 2)), "->", getColor(3), size.x);
			writeLine(0, centerY, size.x, 1, b);
			drawLine(static_cast<short>(std::min<int>(size.y - 1, centerY + 1)), afterText, afterLeft, afterMatchOffset, afterMatchLength);
		}

	  private:
		std::string beforeText;
		std::size_t beforeMatchOffset;
		std::size_t beforeMatchLength;
		std::string afterText;
		std::size_t afterMatchOffset;
		std::size_t afterMatchLength;
	};

	MRDialogFoundation *dialog = nullptr;
	ushort result = cmCancel;

	if (TProgram::deskTop == nullptr) return PromptReplaceDecision::Cancel;
	dialog = mr::dialogs::createScrollableDialog(title.c_str(), 88, 10);
	dialog->helpCtx = hcDialogReplacePrompt;
	dialog->insert(new PromptPreviewView(TRect(2, 2, 86, 5), preview, replacement));
	{
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~R~eplace", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~S~kip", cmNo, bfNormal}, mr::dialogs::DialogButtonSpec{"Replace ~A~ll", cmYes, bfNormal}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 2);
		mr::dialogs::insertUniformButtonRow(*dialog, (88 - metrics.rowWidth) / 2, 6, 2, buttons);
	}
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	TObject::destroy(dialog);
	if (result == cmOK) return PromptReplaceDecision::Replace;
	if (result == cmNo) return PromptReplaceDecision::Skip;
	if (result == cmYes) return PromptReplaceDecision::ReplaceAll;
	return PromptReplaceDecision::Cancel;
}

PromptSearchDecision promptSearchDecisionDialog(const SearchPreviewParts &preview) {
	class SearchPromptPreviewView : public TView {
	  public:
		SearchPromptPreviewView(const TRect &bounds, const SearchPreviewParts &preview) : TView(bounds), preview(preview) {
		}

		void draw() override {
			TDrawBuffer b;
			const TColorAttr normal = getColor(1);
			const TColorAttr accent = static_cast<TColorAttr>(getColor(3));
			const std::size_t width = static_cast<std::size_t>(std::max<int>(0, size.x));
			const std::size_t lineLeft = centeredPreviewLeft(preview.matchLine, preview.matchLineOffset, preview.matchLineLength, width);
			auto drawLine = [&](short y, const std::string &line, bool highlightMatch) {
				const std::size_t effectiveLeft = line.size() <= width ? 0 : std::min(lineLeft, line.size() - width);
				b.moveChar(0, ' ', normal, size.x);
				for (ushort x = 0; x < static_cast<ushort>(size.x); ++x) {
					const std::size_t source = effectiveLeft + static_cast<std::size_t>(x);
					char ch = source < line.size() ? line[source] : ' ';
					const bool inMatch = highlightMatch && source >= preview.matchLineOffset && source < (preview.matchLineOffset + preview.matchLineLength);
					b.putChar(x, static_cast<uchar>(ch));
					b.putAttribute(x, inMatch ? accent : normal);
				}
				writeLine(0, y, size.x, 1, b);
			};

			for (short y = 0; y < size.y; ++y) {
				b.moveChar(0, ' ', normal, size.x);
				writeLine(0, y, size.x, 1, b);
			}
			const short centerY = size.y / 2;
			drawLine(static_cast<short>(std::max(0, centerY - 1)), preview.previousLine, false);
			drawLine(centerY, preview.matchLine, true);
			drawLine(static_cast<short>(std::min<int>(size.y - 1, centerY + 1)), preview.nextLine, false);
		}

	  private:
		SearchPreviewParts preview;
	};

	MRDialogFoundation *dialog = nullptr;
	ushort result = cmCancel;

	if (TProgram::deskTop == nullptr) return PromptSearchDecision::Cancel;
	dialog = mr::dialogs::createScrollableDialog("SEARCH", 88, 10);
	dialog->helpCtx = hcDialogSearchContinue;
	dialog->insert(new SearchPromptPreviewView(TRect(2, 2, 86, 5), preview));
	{
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~N~ext", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~S~top", cmNo, bfNormal}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 2);
		mr::dialogs::insertUniformButtonRow(*dialog, (88 - metrics.rowWidth) / 2, 6, 2, buttons);
	}
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	TObject::destroy(dialog);
	if (result == cmOK) return PromptSearchDecision::Next;
	if (result == cmNo) return PromptSearchDecision::Stop;
	return PromptSearchDecision::Cancel;
}
