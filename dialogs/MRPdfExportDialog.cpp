#define Uses_TDeskTop
#define Uses_TFileDialog
#define Uses_TInputLine
#define Uses_TLabel
#define Uses_TObject
#define Uses_TProgram
#define Uses_TStaticText
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
#include "../ui/MRDropList.hpp"
#include "../ui/MRFrame.hpp"
#include "../ui/MRNumericSlider.hpp"
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
constexpr int kDialogHeight = 19;
constexpr int kLabelLeft = 2;
constexpr int kInputLeft = 22;
constexpr int kFieldWidth = 48;
constexpr int kFieldRight = kInputLeft + kFieldWidth;
constexpr int kDropButtonWidth = 3;
constexpr int kBrowseButtonWidth = 2;
constexpr int kTextWidthY = 8;
constexpr int kMarginFieldWidth = 4;
constexpr int kMarginAxisY = 12;
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
	TPdfExportDialog() : TWindowInit(initMrDialogFrame), MRDialogFoundation(mr::dialogs::centeredDialogRect(kDialogWidth, kDialogHeight), "Export to PDF", kDialogWidth, kDialogHeight, initMrDialogFrame) {
		static constexpr std::array buttons{
		    mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault},
		    mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal},
		};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 1);
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

		mOutputPath = new TInputLine(TRect(kInputLeft, 2, kFieldRight, 3), sizeof(MRPdfExportDialogData::outputPath) - 1);
		insert(mOutputPath);
		insert(new TLabel(TRect(kLabelLeft, 2, kInputLeft, 3), "~O~utput URI:", mOutputPath));
		mOutputPathListAnchor = TRect(kInputLeft, 2, kFieldRight, 3);
		outputPathDropList.createButton(*this, TRect(outputHistoryLeft, 2, outputBrowseLeft, 3), mOutputPath, this, cmMrPdfExportChooseOutputPath, false);
		insert(new TInlineGlyphButton(TRect(outputBrowseLeft, 2, outputBrowseRight, 3), "🔎", cmMrPdfExportBrowseOutputPath));

		mHeaderLine = new TInputLine(TRect(kInputLeft, 3, kFieldRight, 4), sizeof(MRPdfExportDialogData::headerLine) - 1);
		insert(mHeaderLine);
		insert(new TLabel(TRect(kLabelLeft, 3, kInputLeft, 4), "~H~eader:", mHeaderLine));

		mFooterLine = new TInputLine(TRect(kInputLeft, 4, kFieldRight, 5), sizeof(MRPdfExportDialogData::footerLine) - 1);
		insert(mFooterLine);
		insert(new TLabel(TRect(kLabelLeft, 4, kInputLeft, 5), "~F~ooter:", mFooterLine));

		mPageSeparator = new TInputLine(TRect(kInputLeft, 5, kFieldRight, 6), sizeof(MRPdfExportDialogData::pageSeparatorLiteral) - 1);
		insert(mPageSeparator);
		insert(new TLabel(TRect(kLabelLeft, 5, kInputLeft, 6), "~P~age separator:", mPageSeparator));

		mFontFamily = new TInputLine(TRect(kInputLeft, 6, kFieldRight, 7), sizeof(MRPdfExportDialogData::fontFamily) - 1);
		insert(mFontFamily);
		insert(new TLabel(TRect(kLabelLeft, 6, kInputLeft, 7), "Font ~f~amily:", mFontFamily));
		mFontFamilyListAnchor = mFontFamily->getBounds();
		fontFamilyDropList.createButton(*this, TRect(fontHistoryLeft, 6, fontHistoryRight, 7), mFontFamily, this, cmMrPdfExportChooseFontFamily, false);

		mFontSize = new MRNumericSlider(TRect(kInputLeft, 7, kFieldRight, 8), 1, 40, 10, 1, 5, MRNumericSlider::fmtRaw);
		insert(mFontSize);
		insert(new TLabel(TRect(kLabelLeft, 7, kInputLeft, 8), "Font si~z~e:", mFontSize));

		mTextWidth = new TInputLine(TRect(kInputLeft, kTextWidthY, kInputLeft + 5, kTextWidthY + 1), 4);
		insert(mTextWidth);
		insert(new TLabel(TRect(kLabelLeft, kTextWidthY, kInputLeft, kTextWidthY + 1), "~T~ext width:", mTextWidth));

		insert(new TStaticText(TRect(kLabelLeft + 1, kMarginAxisY, kInputLeft, kMarginAxisY + 1), "Margins:"));

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

		if (data == nullptr) return;
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
		if (outputPathDropList.handleOpenListEvent(event)) return;
		if (fontFamilyDropList.handleOpenListEvent(event)) return;
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
			result.warningText = "Text width must be an integer within 0..9999.";
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
		std::vector<std::string> values;
		const std::string currentValue = inputLineValue(mOutputPath);
		short visibleRows = 7;

		configuredScopedDialogFileHistoryEntries(MRDialogHistoryScope::PdfExport, values);
		if (!trimAscii(currentValue).empty() && std::find(values.begin(), values.end(), currentValue) == values.end()) values.insert(values.begin(), currentValue);
		if (values.empty()) return;
		if (visibleRows > size.y - mOutputPathListAnchor.a.y - 1) visibleRows = static_cast<short>(size.y - mOutputPathListAnchor.a.y - 1);
		if (visibleRows < 1) visibleRows = 1;
		outputPathDropList.toggle(*this, mOutputPathListAnchor, values, currentValue, this, cmMrPdfExportAcceptOutputPath, visibleRows);
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
		if (mr::dialogs::execRememberingFileDialogWithData(MRDialogHistoryScope::PdfExport, "*.pdf", "Export to PDF", "~N~ame", fdOKButton, buffer) != cmCancel) {
			mOutputPath->setData(buffer);
			mOutputPath->selectAll(True);
			mOutputPath->drawView();
		}
	}

	void toggleFontFamilyList() {
		std::vector<std::string> values = MRPdfTextExporter::availableFontFamilies();
		const std::string currentValue = inputLineValue(mFontFamily);
		short visibleRows = 10;

		if (values.empty()) return;
		if (visibleRows > size.y - mFontFamilyListAnchor.a.y - 1) visibleRows = static_cast<short>(size.y - mFontFamilyListAnchor.a.y - 1);
		if (visibleRows < 1) visibleRows = 1;
		fontFamilyDropList.toggle(*this, mFontFamilyListAnchor, values, currentValue, this, cmMrPdfExportAcceptFontFamily, visibleRows);
	}

	void acceptFontFamilyListSelection() {
		std::string selectedValue;

		if (!fontFamilyDropList.acceptSelection(selectedValue)) return;
		setInputLineValue(mFontFamily, selectedValue);
		if (mFontFamily != nullptr) mFontFamily->selectAll(True);
	}

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
};

} // namespace

ushort runPdfExportDialog(MRPdfExportDialogData &data) {
	TPdfExportDialog *dialog = new TPdfExportDialog();
	ushort result = cmCancel;

	if (dialog == nullptr || TProgram::deskTop == nullptr) return cmCancel;
	dialog->setData(&data);
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	dialog->getData(&data);
	TObject::destroy(dialog);
	return result;
}
