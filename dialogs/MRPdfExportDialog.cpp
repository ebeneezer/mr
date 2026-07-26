#define Uses_TDeskTop
#define Uses_TFileDialog
#define Uses_TInputLine
#define Uses_TLabel
#define Uses_TObject
#define Uses_TProgram
#define Uses_TRadioButtons
#define Uses_TStaticText
#define Uses_TSItem
#include <tvision/tv.h>

#include "MRPdfExportDialog.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "../app/export/MRPdfTextExporter.hpp"
#include "../ui/widgets/MRDropList.hpp"
#include "../ui/MRFrame.hpp"
#include "../ui/widgets/MRNumericSlider.hpp"
#include "setup/MRSetupCommon.hpp"

namespace {

TFrame *initMrDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}

enum : ushort {
	cmMrPdfExportChooseOutputPath = 3868,
	cmMrPdfExportAcceptOutputPath,
	cmMrPdfExportBrowseOutputPath,
	cmMrPdfExportChooseFontFamily,
	cmMrPdfExportAcceptFontFamily
};

constexpr int kDialogWidth = 78;
constexpr int kDialogHeight = 22;
constexpr int kLabelLeft = 2;
constexpr int kInputLeft = 25;
constexpr int kFieldWidth = 45;
constexpr int kFieldRight = kInputLeft + kFieldWidth;
constexpr int kDropButtonWidth = 3;
constexpr int kBrowseButtonWidth = 2;
constexpr int kSourceHeadingY = 2;
constexpr int kSourceOptionsY = 3;
constexpr int kFieldsY = 5;
constexpr int kTextWidthY = kFieldsY + 6;
constexpr int kMarginFieldWidth = 4;
constexpr int kMarginAxisY = 16;
constexpr int kMarginAxisGap = 8;

bool parseRangeInt(std::string_view text, int minValue, int maxValue, int &value) {
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

class TInlineGlyphButton final : public TView {
  public:
	TInlineGlyphButton(const TRect &bounds, const char *glyph, ushort command) : TView(bounds), mGlyph(glyph != nullptr ? glyph : ""), mCommand(command) {
		options |= ofSelectable;
		options |= ofFirstClick;
		eventMask |= evMouseDown | evKeyDown;
	}

	void draw() override {
		TDrawBuffer b;
		const ushort color = getColor((state & sfFocused) != 0 ? 2 : 1);
		const int glyphWidth = strwidth(mGlyph.c_str());
		const int x = std::max(0, (size.x - glyphWidth) / 2);

		b.moveChar(0, ' ', color, size.x);
		b.moveStr(static_cast<ushort>(x), mGlyph.c_str(), color, size.x - x);
		writeLine(0, 0, size.x, size.y, b);
	}

	void handleEvent(TEvent &event) override {
		if ((state & sfDisabled) != 0) {
			TView::handleEvent(event);
			return;
		}
		if (event.what == evMouseDown) {
			dispatchCommand();
			clearEvent(event);
			return;
		}
		if (event.what == evKeyDown) {
			const TKey key(event.keyDown);

			if (key == TKey(kbEnter) || key == TKey(' ')) {
				dispatchCommand();
				clearEvent(event);
				return;
			}
		}
		TView::handleEvent(event);
	}

  private:
	void dispatchCommand() {
		TView *target = owner;

		while (target != nullptr && dynamic_cast<TDialog *>(target) == nullptr)
			target = target->owner;
		message(target != nullptr ? target : owner, evCommand, mCommand, this);
	}

	std::string mGlyph;
	ushort mCommand;
};

class TPdfExportDialog final : public MRDialogFoundation {
  public:
	explicit TPdfExportDialog(bool markedBlockAvailable) : TWindowInit(initMrDialogFrame), MRDialogFoundation(mr::dialogs::centeredDialogRect(kDialogWidth, kDialogHeight), "EXPORT TO PDF", kDialogWidth, kDialogHeight, initMrDialogFrame), mMarkedBlockAvailable(markedBlockAvailable) {
		static constexpr std::array buttons{
		    mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault},
		};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 0);
		const int buttonLeft = (kDialogWidth - metrics.rowWidth) / 2;
		const int outputHistoryLeft = kFieldRight;
		const int outputBrowseLeft = outputHistoryLeft + kDropButtonWidth;
		const int outputBrowseRight = outputBrowseLeft + kBrowseButtonWidth;
		const int fontHistoryLeft = kFieldRight;
		const int fontHistoryRight = fontHistoryLeft + kDropButtonWidth;
		const int marginCenterX = kDialogWidth / 2;
		const int marginTopLeft = marginCenterX - (kMarginFieldWidth / 2);
		const int marginTopRight = marginTopLeft + kMarginFieldWidth;
		const int marginLeftLeft = marginCenterX - kMarginAxisGap - kMarginFieldWidth;
		const int marginLeftRight = marginLeftLeft + kMarginFieldWidth;
		const int marginRightLeft = marginCenterX + kMarginAxisGap;
		const int marginRightRight = marginRightLeft + kMarginFieldWidth;

		insert(new TStaticText(TRect(kLabelLeft, kSourceHeadingY, kInputLeft, kSourceHeadingY + 1), "Source:"));
		mSource = new TRadioButtons(TRect(kLabelLeft + 1, kSourceOptionsY, kInputLeft + 18, kSourceOptionsY + 2), new TSItem(" ~C~urrent file ", new TSItem(" ~M~arked block ", nullptr)));
		insert(mSource);
		if (!mMarkedBlockAvailable) mSource->setButtonState(0x0002, False);

		mOutputPath = new TInputLine(TRect(kInputLeft, kFieldsY, kFieldRight, kFieldsY + 1), sizeof(MRPdfExportDialogData::outputPath) - 1);
		insert(mOutputPath);
		insert(new TLabel(TRect(kLabelLeft, kFieldsY, kInputLeft, kFieldsY + 1), "~O~utput URI:", mOutputPath));
		mOutputPathListAnchor = TRect(kInputLeft, kFieldsY, kFieldRight, kFieldsY + 1);
		outputPathDropList.createButton(*this, TRect(outputHistoryLeft, kFieldsY, outputBrowseLeft, kFieldsY + 1), mOutputPath, this, cmMrPdfExportChooseOutputPath, false);
		insert(new TInlineGlyphButton(TRect(outputBrowseLeft, kFieldsY, outputBrowseRight, kFieldsY + 1), "🔎", cmMrPdfExportBrowseOutputPath));

		mHeaderLine = new TInputLine(TRect(kInputLeft, kFieldsY + 1, kFieldRight, kFieldsY + 2), sizeof(MRPdfExportDialogData::headerLine) - 1);
		insert(mHeaderLine);
		insert(new TLabel(TRect(kLabelLeft, kFieldsY + 1, kInputLeft, kFieldsY + 2), "~H~eader:", mHeaderLine));

		mFooterLine = new TInputLine(TRect(kInputLeft, kFieldsY + 2, kFieldRight, kFieldsY + 3), sizeof(MRPdfExportDialogData::footerLine) - 1);
		insert(mFooterLine);
		insert(new TLabel(TRect(kLabelLeft, kFieldsY + 2, kInputLeft, kFieldsY + 3), "~F~ooter:", mFooterLine));

		mPageSeparator = new TInputLine(TRect(kInputLeft, kFieldsY + 3, kFieldRight, kFieldsY + 4), sizeof(MRPdfExportDialogData::pageSeparatorLiteral) - 1);
		insert(mPageSeparator);
		insert(new TLabel(TRect(kLabelLeft, kFieldsY + 3, kInputLeft, kFieldsY + 4), "~P~age separator:", mPageSeparator));

		mFontFamily = new TInputLine(TRect(kInputLeft, kFieldsY + 4, kFieldRight, kFieldsY + 5), sizeof(MRPdfExportDialogData::fontFamily) - 1);
		insert(mFontFamily);
		insert(new TLabel(TRect(kLabelLeft, kFieldsY + 4, kInputLeft, kFieldsY + 5), "Font ~f~amily:", mFontFamily));
		mFontFamilyListAnchor = mFontFamily->getBounds();
		fontFamilyDropList.createButton(*this, TRect(fontHistoryLeft, kFieldsY + 4, fontHistoryRight, kFieldsY + 5), mFontFamily, this, cmMrPdfExportChooseFontFamily, false);

		mFontSize = new MRNumericSlider(TRect(kInputLeft, kFieldsY + 5, kFieldRight, kFieldsY + 6), 1, 40, 10, 1, 5, MRNumericSlider::fmtRaw);
		insert(mFontSize);
		insert(new TLabel(TRect(kLabelLeft, kFieldsY + 5, kInputLeft, kFieldsY + 6), "Font si~z~e:", mFontSize));

		mTextWidth = new TInputLine(TRect(kInputLeft, kTextWidthY, kInputLeft + 5, kTextWidthY + 1), 4);
		insert(mTextWidth);
		insert(new TLabel(TRect(kLabelLeft, kTextWidthY, kInputLeft, kTextWidthY + 1), "~R~ight margin (column):", mTextWidth));

		insert(new TStaticText(TRect(kLabelLeft, kMarginAxisY - 3, kInputLeft + 8, kMarginAxisY - 2), "Page margins (points):"));

		mTopMargin = new TInputLine(TRect(marginTopLeft, kMarginAxisY - 2, marginTopRight, kMarginAxisY - 1), 4);
		insert(mTopMargin);

		mLeftMargin = new TInputLine(TRect(marginLeftLeft, kMarginAxisY, marginLeftRight, kMarginAxisY + 1), 4);
		insert(mLeftMargin);

		mRightMargin = new TInputLine(TRect(marginRightLeft, kMarginAxisY, marginRightRight, kMarginAxisY + 1), 4);
		insert(mRightMargin);

		mBottomMargin = new TInputLine(TRect(marginTopLeft, kMarginAxisY + 2, marginTopRight, kMarginAxisY + 3), 4);
		insert(mBottomMargin);

		setDialogValidationHook([this]() { return validateDialogValues(); });
		mr::dialogs::insertUniformButtonRow(*this, buttonLeft, kDialogHeight - 3, 1, buttons);
		finalizeLayout();
		selectNext(False);
	}

	ushort dataSize() override {
		return sizeof(MRPdfExportDialogData);
	}

	void getData(void *rec) override {
		MRPdfExportDialogData *data = static_cast<MRPdfExportDialogData *>(rec);
		int32_t fontSize = 10;

		if (data == nullptr) return;
		if (mSource != nullptr) mSource->getData(&data->source);
		if (mOutputPath != nullptr) mOutputPath->getData(data->outputPath);
		if (mPageSeparator != nullptr) mPageSeparator->getData(data->pageSeparatorLiteral);
		if (mFontFamily != nullptr) mFontFamily->getData(data->fontFamily);
		if (mFontSize != nullptr) mFontSize->getData(&fontSize);
		data->fontSizePoints = fontSize;
		if (mHeaderLine != nullptr) mHeaderLine->getData(data->headerLine);
		if (mFooterLine != nullptr) mFooterLine->getData(data->footerLine);
		if (mTextWidth != nullptr) mTextWidth->getData(data->textWidth);
		if (mLeftMargin != nullptr) mLeftMargin->getData(data->leftMarginPoints);
		if (mRightMargin != nullptr) mRightMargin->getData(data->rightMarginPoints);
		if (mTopMargin != nullptr) mTopMargin->getData(data->topMarginPoints);
		if (mBottomMargin != nullptr) mBottomMargin->getData(data->bottomMarginPoints);
	}

	void setData(void *rec) override {
		MRPdfExportDialogData *data = static_cast<MRPdfExportDialogData *>(rec);
		int32_t fontSize = 10;
		ushort source = pdfExportCurrentFile;

		if (data == nullptr) return;
		source = mMarkedBlockAvailable && data->source == pdfExportMarkedBlock ? pdfExportMarkedBlock : pdfExportCurrentFile;
		if (mSource != nullptr) mSource->setData(&source);
		if (mOutputPath != nullptr) mOutputPath->setData(data->outputPath);
		if (mPageSeparator != nullptr) mPageSeparator->setData(data->pageSeparatorLiteral);
		if (mFontFamily != nullptr) mFontFamily->setData(data->fontFamily);
		fontSize = std::clamp<int32_t>(data->fontSizePoints, 1, 40);
		if (mFontSize != nullptr) mFontSize->setData(&fontSize);
		if (mHeaderLine != nullptr) mHeaderLine->setData(data->headerLine);
		if (mFooterLine != nullptr) mFooterLine->setData(data->footerLine);
		if (mTextWidth != nullptr) mTextWidth->setData(data->textWidth);
		if (mLeftMargin != nullptr) mLeftMargin->setData(data->leftMarginPoints);
		if (mRightMargin != nullptr) mRightMargin->setData(data->rightMarginPoints);
		if (mTopMargin != nullptr) mTopMargin->setData(data->topMarginPoints);
		if (mBottomMargin != nullptr) mBottomMargin->setData(data->bottomMarginPoints);
	}

	void handleEvent(TEvent &event) override {
		if (outputPathDropList.handleLinkedInputEvent(event, *this, mOutputPathListAnchor, outputPathChoices(false), mOutputPath, this, cmMrPdfExportAcceptOutputPath, outputPathVisibleRows())) return;
		if (fontFamilyDropList.handleLinkedInputEvent(event, *this, mFontFamilyListAnchor, MRPdfTextExporter::availableFontFamilies(), mFontFamily, this, cmMrPdfExportAcceptFontFamily, fontFamilyVisibleRows())) return;
		if (event.what == evCommand && event.message.command == cmMrPdfExportChooseOutputPath) {
			toggleOutputPathList();
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmMrPdfExportAcceptOutputPath) {
			acceptOutputPathListSelection();
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmMrPdfExportBrowseOutputPath) {
			browseOutputPath();
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmMrPdfExportChooseFontFamily) {
			toggleFontFamilyList();
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmMrPdfExportAcceptFontFamily) {
			acceptFontFamilyListSelection();
			clearEvent(event);
			return;
		}
		MRDialogFoundation::handleEvent(event);
	}

  private:
	DialogValidationResult validateDialogValues() const {
		DialogValidationResult result;
		int parsed = 0;

		if (!parseRangeInt(inputLineValue(mTextWidth), 0, 9999, parsed)) {
			result.valid = false;
			result.error = true;
			result.warningText = "Right margin column must be an integer within 0..9999.";
			return result;
		}
		if (!parseRangeInt(inputLineValue(mTopMargin), 0, 9999, parsed) || !parseRangeInt(inputLineValue(mLeftMargin), 0, 9999, parsed) ||
		    !parseRangeInt(inputLineValue(mRightMargin), 0, 9999, parsed) || !parseRangeInt(inputLineValue(mBottomMargin), 0, 9999, parsed)) {
			result.valid = false;
			result.error = true;
			result.warningText = "Margins must be integers within 0..9999.";
			return result;
		}
		return result;
	}

	std::string inputLineValue(TInputLine *inputLine) const {
		std::vector<char> buffer(512, '\0');

		if (inputLine == nullptr) return std::string();
		inputLine->getData(buffer.data());
		return std::string(buffer.data());
	}

	void setInputLineValue(TInputLine *inputLine, const std::string &value) {
		std::vector<char> buffer(512, '\0');

		if (inputLine == nullptr) return;
		std::strncpy(buffer.data(), value.c_str(), buffer.size() - 1);
		buffer.back() = '\0';
		inputLine->setData(buffer.data());
		inputLine->drawView();
	}

	void toggleOutputPathList() {
		std::vector<std::string> values = outputPathChoices(true);
		const std::string currentValue = inputLineValue(mOutputPath);

		if (values.empty()) return;
		outputPathDropList.toggle(*this, mOutputPathListAnchor, values, currentValue, this, cmMrPdfExportAcceptOutputPath, outputPathVisibleRows());
	}

	void acceptOutputPathListSelection() {
		std::string selectedValue;

		if (!outputPathDropList.acceptSelection(selectedValue)) return;
		setInputLineValue(mOutputPath, selectedValue);
		if (mOutputPath != nullptr) mOutputPath->selectAll(True);
	}

	void browseOutputPath() {
		char buffer[sizeof(MRPdfExportDialogData::outputPath)] = {0};

		if (mOutputPath == nullptr) return;
		mOutputPath->getData(buffer);
		if (trimAscii(buffer).empty()) mr::dialogs::seedFileDialogPath(MRDialogHistoryScope::PdfExport, buffer, sizeof(buffer), "*.pdf");
		if (mr::dialogs::execRememberingFileDialogWithData(MRDialogHistoryScope::PdfExport, "*.pdf", "EXPORT TO PDF", "~N~ame", fdOKButton, buffer) != cmCancel) {
			mOutputPath->setData(buffer);
			mOutputPath->selectAll(True);
			mOutputPath->drawView();
		}
	}

	void toggleFontFamilyList() {
		std::vector<std::string> values = MRPdfTextExporter::availableFontFamilies();
		const std::string currentValue = inputLineValue(mFontFamily);

		if (values.empty()) return;
		fontFamilyDropList.toggle(*this, mFontFamilyListAnchor, values, currentValue, this, cmMrPdfExportAcceptFontFamily, fontFamilyVisibleRows());
	}

	std::vector<std::string> outputPathChoices(bool includeCurrentValue) const {
		std::vector<std::string> values;
		const std::string currentValue = inputLineValue(mOutputPath);

		configuredScopedDialogFileHistoryEntries(MRDialogHistoryScope::PdfExport, values);
		if (includeCurrentValue && !trimAscii(currentValue).empty() && std::find(values.begin(), values.end(), currentValue) == values.end()) values.insert(values.begin(), currentValue);
		return values;
	}

	short outputPathVisibleRows() const {
		short visibleRows = 7;

		if (visibleRows > size.y - mOutputPathListAnchor.a.y - 1) visibleRows = static_cast<short>(size.y - mOutputPathListAnchor.a.y - 1);
		if (visibleRows < 1) visibleRows = 1;
		return visibleRows;
	}

	short fontFamilyVisibleRows() const {
		short visibleRows = 10;

		if (visibleRows > size.y - mFontFamilyListAnchor.a.y - 1) visibleRows = static_cast<short>(size.y - mFontFamilyListAnchor.a.y - 1);
		if (visibleRows < 1) visibleRows = 1;
		return visibleRows;
	}

	void acceptFontFamilyListSelection() {
		std::string selectedValue;

		if (!fontFamilyDropList.acceptSelection(selectedValue)) return;
		setInputLineValue(mFontFamily, selectedValue);
		if (mFontFamily != nullptr) mFontFamily->selectAll(True);
	}

	TRadioButtons *mSource = nullptr;
	TInputLine *mOutputPath = nullptr;
	TInputLine *mPageSeparator = nullptr;
	TInputLine *mFontFamily = nullptr;
	MRNumericSlider *mFontSize = nullptr;
	TInputLine *mHeaderLine = nullptr;
	TInputLine *mFooterLine = nullptr;
	TInputLine *mTextWidth = nullptr;
	TInputLine *mLeftMargin = nullptr;
	TInputLine *mRightMargin = nullptr;
	TInputLine *mTopMargin = nullptr;
	TInputLine *mBottomMargin = nullptr;
	TRect mOutputPathListAnchor;
	TRect mFontFamilyListAnchor;
	MRDropList outputPathDropList;
	MRDropList fontFamilyDropList;
	bool mMarkedBlockAvailable;
};

} // namespace

ushort runPdfExportDialog(MRPdfExportDialogData &data, bool markedBlockAvailable) {
	MRPdfExportDialogData draft = data;
	TPdfExportDialog *dialog = new TPdfExportDialog(markedBlockAvailable);
	ushort result = cmCancel;

	if (dialog == nullptr || TProgram::deskTop == nullptr) return cmCancel;
	dialog->setData(&draft);
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	if (result == cmOK) {
		dialog->getData(&draft);
		data = draft;
	}
	TObject::destroy(dialog);
	return result;
}
