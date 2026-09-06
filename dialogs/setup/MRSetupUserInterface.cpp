#define Uses_TApplication
#define Uses_TButton
#define Uses_TCheckBoxes
#define Uses_TChDirDialog
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TFileDialog
#define Uses_TDrawBuffer
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TLabel
#define Uses_TObject
#define Uses_TRadioButtons
#define Uses_TRect
#define Uses_TStaticText
#define Uses_TSItem
#define Uses_TView
#define Uses_TWindow
#include <tvision/tv.h>

#include "MRSetup.hpp"
#include "MRSetupSections.hpp"

#include "../../app/MRCommands.hpp"
#include "../../app/MRCommandRouter.hpp"
#include "../../app/MRHelpTopics.generated.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../config/settings/MRSettingsStorage.hpp"
#include "../../ui/MRFrame.hpp"
#include "../../ui/MRBentoBox/MRBentoBox.hpp"
#include "../../ui/MRMenuBar.hpp"
#include "../../ui/MRMessageLineController.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/widgets/MRNumericSlider.hpp"
#include "../../ui/widgets/MRDropList.hpp"
#include "../../ui/widgets/MRSpinner.hpp"
#include "../../ui/MRPalette.hpp"
#include "../MRDirtyGating.hpp"
#include "MRSetupCommon.hpp"
#include "../../app/commands/MRWindowCommands.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

using mr::dialogs::discardQueuedCancelEvent;
using mr::dialogs::execDialogWithDataCapture;
using mr::dialogs::initSetupDialogFrame;
using mr::dialogs::postSetupFlowError;
using mr::dialogs::readRecordField;
using mr::dialogs::writeRecordField;

void setSetupDialogStatus(const std::string &text, MRMenuBar::MarqueeKind) {
	if (text.empty()) {
		mr::messageline::clearOwner(mr::messageline::Owner::DialogValidation);
		return;
	}
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogValidation, text, mr::messageline::Kind::Warning, mr::messageline::kPriorityHigh);
}

void clearSetupDialogStatus() {
	mr::messageline::clearOwner(mr::messageline::Owner::DialogValidation);
}

struct UserInterfaceSettingsDialogData {
	ushort flags = 0;
	ushort heroFlags = 0;
	ushort heroFileThresholdMb = 8;
	ushort virtualDesktops = 1;
	ushort cursorBehaviourChoice = 1;
	ushort compilerErrorMessageChoice = 1;
	ushort fileCompareStartChoice = 0;
	ushort compilerDiagnosticFlags = 0;
	ushort scrollbarVisibilityChoice = 0;
	ushort colorOutputModeChoice = 0;
	ushort uiIndentStyleChoice = 0;
	char cursorPositionMarker[12] = {0};
	char fileCompareOriginalLeadingGutters[8] = {0};
	char fileCompareOriginalTrailingGutters[8] = {0};
	char fileCompareCompareLeadingGutters[8] = {0};
	char fileCompareCompareTrailingGutters[8] = {0};
};

bool validateCursorPositionMarkerInput(std::string_view value, std::string &errorText) {
	std::string trimmed = trimAscii(value);
	int rowPlaceholderCount = 0;
	int colPlaceholderCount = 0;

	if (trimmed.empty()) {
		errorText = "Cursor position marker must not be empty.";
		return false;
	}
	if (trimmed.size() > 10) {
		errorText = "Cursor position marker must be at most 10 characters.";
		return false;
	}
	for (char ch : trimmed) {
		if (ch == 'R') {
			++rowPlaceholderCount;
			if (rowPlaceholderCount > 1) {
				errorText = "Cursor position marker may contain R only once.";
				return false;
			}
			continue;
		}
		if (ch == 'C') {
			++colPlaceholderCount;
			if (colPlaceholderCount > 1) {
				errorText = "Cursor position marker may contain C only once.";
				return false;
			}
		}
	}
	if (rowPlaceholderCount == 0 || colPlaceholderCount == 0) {
		errorText = "Cursor position marker must contain both R and C.";
		return false;
	}
	errorText.clear();
	return true;
}

bool validateFileCompareGuttersInput(std::string_view value, std::string &errorText) {
	for (char ch : trimAscii(value)) {
		switch (static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(ch)))) {
			case 'M':
			case 'D':
			case 'L':
			case 'C':
				break;
			default:
				errorText = "File compare gutters may contain only M, D, L or C.";
				return false;
		}
	}
	errorText.clear();
	return true;
}

std::vector<std::string> fileCompareGutterSpinnerValues() {
	std::vector<std::string> values;

	values.reserve(5);
	values.push_back(" ");
	values.push_back("M");
	values.push_back("D");
	values.push_back("L");
	values.push_back("C");
	return values;
}

bool userInterfaceSettingsDialogDataEqual(const UserInterfaceSettingsDialogData &lhs, const UserInterfaceSettingsDialogData &rhs) {
	return lhs.flags == rhs.flags && lhs.heroFlags == rhs.heroFlags && lhs.heroFileThresholdMb == rhs.heroFileThresholdMb && lhs.virtualDesktops == rhs.virtualDesktops && lhs.cursorBehaviourChoice == rhs.cursorBehaviourChoice && lhs.compilerErrorMessageChoice == rhs.compilerErrorMessageChoice &&
	       lhs.fileCompareStartChoice == rhs.fileCompareStartChoice && lhs.compilerDiagnosticFlags == rhs.compilerDiagnosticFlags && lhs.scrollbarVisibilityChoice == rhs.scrollbarVisibilityChoice &&
	       lhs.colorOutputModeChoice == rhs.colorOutputModeChoice && lhs.uiIndentStyleChoice == rhs.uiIndentStyleChoice &&
	       readRecordField(lhs.cursorPositionMarker) == readRecordField(rhs.cursorPositionMarker) && readRecordField(lhs.fileCompareOriginalLeadingGutters) == readRecordField(rhs.fileCompareOriginalLeadingGutters) &&
	       readRecordField(lhs.fileCompareOriginalTrailingGutters) == readRecordField(rhs.fileCompareOriginalTrailingGutters) && readRecordField(lhs.fileCompareCompareLeadingGutters) == readRecordField(rhs.fileCompareCompareLeadingGutters) &&
	       readRecordField(lhs.fileCompareCompareTrailingGutters) == readRecordField(rhs.fileCompareCompareTrailingGutters);
}

class TIndentStylePreview : public TView {
  public:
	TIndentStylePreview(const TRect &bounds) noexcept : TView(bounds) {
		eventMask = 0;
	}

	void setStyle(ushort choice) {
		if (mStyleChoice != choice) {
			mStyleChoice = choice;
			drawView();
		}
	}

	void draw() override {
		TDrawBuffer buffer;
		const TAttrPair boxColor(0x1F);
		const auto lines = previewLinesFor(mStyleChoice);
		const char topLeft = static_cast<char>(0xC9);
		const char topRight = static_cast<char>(0xBB);
		const char bottomLeft = static_cast<char>(0xC8);
		const char bottomRight = static_cast<char>(0xBC);
		const char horizontal = static_cast<char>(0xCD);
		const char vertical = static_cast<char>(0xBA);

		for (int y = 0; y < size.y; ++y) {
			buffer.moveChar(0, ' ', boxColor, size.x);
			if (y == 0 || y == size.y - 1) {
				buffer.putChar(0, y == 0 ? topLeft : bottomLeft);
				buffer.moveChar(1, horizontal, boxColor, std::max(0, size.x - 2));
				buffer.putChar(size.x - 1, y == 0 ? topRight : bottomRight);
			} else {
				buffer.putChar(0, vertical);
				buffer.putChar(size.x - 1, vertical);
				const int lineIndex = y - 1;
				if (lineIndex >= 0 && lineIndex < static_cast<int>(lines.size())) buffer.moveStr(2, lines[static_cast<std::size_t>(lineIndex)], boxColor, std::max(0, size.x - 3));
			}
			writeLine(0, y, size.x, 1, buffer);
		}
	}

  private:
	static std::array<const char *, 7> previewLinesFor(ushort choice) noexcept {
		switch (choice) {
			case 5:
				return {"if (ready)", "{   work(width);", "    notify(width);", "}", "else", "{   recover(width);", "}"};
			case 4:
				return {"if (ready)", "    {", "    work(width);", "    }", "else", "    {", "    recover(width);"};
			case 3:
				return {"if (ready)", "  {", "    work(width);", "  }", "else", "  {", "    recover(width);"};
			case 2:
				return {"if (ready)", "{", "    work(width);", "}", "else", "{", "    recover(width);"};
			case 1:
				return {"if (ready) {", "    work(width);", "    notify(width);", "} else {", "    recover(width);", "    notify(width);", "}"};
			case 0:
			default:
				return {"if (ready) {", "  work(width);", "  notify(width);", "} else {", "  recover(width);", "  notify(width);", "}"};
		}
	}

	ushort mStyleChoice = 0;
};

class THeroMessageOptionText final : public TStaticText {
  public:
	THeroMessageOptionText(const TRect &bounds, TStringView text) noexcept : TStaticText(bounds, text) {}

	TPalette &getPalette() const override {
		static TPalette palette("\x10", 1);
		return palette;
	}
};

class THeroMessageThresholdSlider final : public MRNumericSlider {
  public:
	THeroMessageThresholdSlider(const TRect &bounds, int32_t value) noexcept : MRNumericSlider(bounds, 0, 16, value, 1, 4, MRNumericSlider::fmtRaw, cmMRNumericSliderChanged) {}

	TColorAttr mapColor(uchar index) override {
		if (owner != nullptr) {
			if (index == 1) return owner->mapColor(16);
			if (index == 2) return owner->mapColor(17);
			if (index == 5) return owner->mapColor(31);
		}
		return MRNumericSlider::mapColor(index);
	}
};

class TUserInterfaceSettingsDialog : public MRScrollableDialog {
  public:
	TUserInterfaceSettingsDialog(bool initialWindowManager, const MRHeroMessageSettings &initialHeroMessages, int initialVirtualDesktops, bool initialCyclicVirtualDesktops, MRCursorBehaviour initialCursorBehaviour,
	                            MRCompilerErrorMessagePlacement initialCompilerErrorMessagePlacement, MRScrollbarVisibility initialScrollbarVisibility, MRColorOutputMode initialColorOutputMode, bool initialTrackCompilerWarnings,
	                            bool initialTrackCompilerNotes,
	                            MRUiIndentStyle initialUiIndentStyle, const std::string &initialCursorPositionMarker, const std::string &initialFileCompareOriginalLeadingGutters, const std::string &initialFileCompareOriginalTrailingGutters,
	                            const std::string &initialFileCompareCompareLeadingGutters, const std::string &initialFileCompareCompareTrailingGutters, MRFileCompareStartConfiguration initialFileCompareStartConfiguration,
	                            bool initialFileCompareComparePanelReadOnly)
	    : TWindowInit(initSetupDialogFrame), MRScrollableDialog(centeredSetupDialogRect(86, 39), "USER INTERFACE SETTINGS", 86, 39, initSetupDialogFrame) {

		int const yStart = 2;
		const int leftColumnLeft = 3;
		const int leftColumnRight = 42;
		const int rightColumnLeft = 44;
		const int rightColumnRight = 83;
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 0);

		helpCtx = hcDialogUserInterface;
		TCheckBoxes *cb = new TCheckBoxes(TRect(3, yStart, 37, yStart + 6),
		                                   new TSItem("~W~indow Manager",
		                                              new TSItem("~C~ycle virtual desktops",
		                                                         new TSItem("Track compiler ~w~arnings", new TSItem("Track compiler ~n~otes", new TSItem("R/~O~ file comparing", new TSItem("~A~utodetect binary files", nullptr)))))));

		mOptionsField = cb;
		addManaged(mOptionsField, mOptionsField->getBounds());

		addManaged(new TStaticText(TRect(38, 2, 56, 3), "Indent style:"), TRect(38, 2, 56, 3));
		mIndentStyleField = new TRadioButtons(TRect(38, 3, 56, 9), new TSItem("~K~&R", new TSItem("K&R~4~", new TSItem("~A~llman", new TSItem("~G~nome", new TSItem("~W~hitesmiths", new TSItem("~H~orstmann", nullptr)))))));
		addManaged(mIndentStyleField, TRect(38, 3, 56, 9));
		mIndentStylePreview = new TIndentStylePreview(TRect(58, 2, 83, 11));
		addManaged(mIndentStylePreview, TRect(58, 2, 83, 11));

		addManaged(new TStaticText(TRect(3, 12, 83, 13), "Hero messages:"), TRect(3, 12, 83, 13));
		mHeroMessagesField = new TCheckBoxes(TRect(3, 13, 83, 16), new TSItem("Show on ~m~essageline", new TSItem("Write to ~l~og", nullptr)));
		addManaged(mHeroMessagesField, TRect(3, 13, 83, 16));
		mHeroFileThresholdSlider = new THeroMessageThresholdSlider(TRect(43, 15, 82, 16), initialHeroMessages.fileThresholdMb);
		addManaged(mHeroFileThresholdSlider, TRect(43, 15, 82, 16));
		addManaged(new THeroMessageOptionText(TRect(8, 15, 43, 16), "Restrict to filesizes above:"), TRect(8, 15, 43, 16));

		addManaged(new TStaticText(TRect(leftColumnLeft, 17, leftColumnRight, 18), "Cursor behaviour:"), TRect(leftColumnLeft, 17, leftColumnRight, 18));
		mCursorBehaviourField = new TRadioButtons(TRect(leftColumnLeft, 18, leftColumnRight, 20), new TSItem("~F~ree movement", new TSItem("~B~ound to text", nullptr)));
		addManaged(mCursorBehaviourField, TRect(leftColumnLeft, 18, leftColumnRight, 20));

		addManaged(new TStaticText(TRect(rightColumnLeft, 17, rightColumnRight, 18), "Scrollbars:"), TRect(rightColumnLeft, 17, rightColumnRight, 18));
		mScrollbarVisibilityField = new TRadioButtons(TRect(rightColumnLeft, 18, rightColumnRight, 20), new TSItem("~S~mart", new TSItem("~A~lways", nullptr)));
		addManaged(mScrollbarVisibilityField, TRect(rightColumnLeft, 18, rightColumnRight, 20));

		addManaged(new TStaticText(TRect(leftColumnLeft, 21, leftColumnRight, 22), "Compiler errors:"), TRect(leftColumnLeft, 21, leftColumnRight, 22));
		mCompilerErrorMessageField = new TRadioButtons(TRect(leftColumnLeft, 22, leftColumnRight, 24), new TSItem("~U~nder code", new TSItem("~R~ight margin", nullptr)));
		addManaged(mCompilerErrorMessageField, TRect(leftColumnLeft, 22, leftColumnRight, 24));

		addManaged(new TStaticText(TRect(rightColumnLeft, 21, rightColumnRight, 22), "File compare:"), TRect(rightColumnLeft, 21, rightColumnRight, 22));
		mFileCompareStartField = new TRadioButtons(TRect(rightColumnLeft, 22, rightColumnRight, 24), new TSItem("Original <> Compare", new TSItem("Compare <> Original", nullptr)));
		addManaged(mFileCompareStartField, TRect(rightColumnLeft, 22, rightColumnRight, 24));

		addManaged(new TStaticText(TRect(leftColumnLeft, 25, leftColumnRight, 26), "Color Management:"), TRect(leftColumnLeft, 25, leftColumnRight, 26));
		mColorOutputModeField = new TRadioButtons(TRect(leftColumnLeft, 26, leftColumnRight, 28), new TSItem("~2~4-bit RGB (automatic)", new TSItem("~P~alette (256 colors)", nullptr)));
		addManaged(mColorOutputModeField, TRect(leftColumnLeft, 26, leftColumnRight, 28));

		mVirtualDesktopsSlider = new MRNumericSlider(TRect(24, 29, 70, 30), 1, 9, initialVirtualDesktops, 1, 1, MRNumericSlider::fmtRaw, cmMRNumericSliderChanged);
		addManaged(mVirtualDesktopsSlider, TRect(24, 29, 70, 30));
		addManaged(new TLabel(TRect(2, 29, 23, 30), "~V~irtual desktops:", mVirtualDesktopsSlider), TRect(2, 29, 23, 30));

		mCursorPositionMarkerField = new TInputLine(TRect(28, 30, 42, 31), 11);
		addManaged(mCursorPositionMarkerField, TRect(28, 30, 42, 31));
		addManaged(new TLabel(TRect(2, 30, 27, 31), "Cursor position ~m~arker:", mCursorPositionMarkerField), TRect(2, 30, 27, 31));

		addManaged(new TStaticText(TRect(3, 33, 25, 34), "File compare gutters:"), TRect(3, 33, 25, 34));
		addManaged(new TStaticText(TRect(26, 33, 36, 34), "Original:"), TRect(26, 33, 36, 34));
		addFileCompareGutterSpinners(mFileCompareOriginalLeadingGutterSpinners, 37, 32);
		addFileCompareGutterSpinners(mFileCompareOriginalTrailingGutterSpinners, 43, 32);
		addManaged(new TStaticText(TRect(54, 33, 63, 34), "Compare:"), TRect(54, 33, 63, 34));
		addFileCompareGutterSpinners(mFileCompareCompareLeadingGutterSpinners, 64, 32);
		addFileCompareGutterSpinners(mFileCompareCompareTrailingGutterSpinners, 70, 32);
		mr::dialogs::addManagedUniformButtonRow(*this, (86 - metrics.rowWidth) / 2, 36, 0, buttons);

		mInitialCursorBehaviourChoice = initialCursorBehaviour == MRCursorBehaviour::FreeMovement ? 0 : 1;
		mInitialCompilerErrorMessageChoice = initialCompilerErrorMessagePlacement == MRCompilerErrorMessagePlacement::UnderCode ? 0 : 1;
		mInitialFileCompareStartChoice = initialFileCompareStartConfiguration == MRFileCompareStartConfiguration::CompareOriginal ? 1 : 0;
		mInitialScrollbarVisibilityChoice = initialScrollbarVisibility == MRScrollbarVisibility::Always ? 1 : 0;
		mInitialColorOutputModeChoice = initialColorOutputMode == MRColorOutputMode::TerminalPalette ? 1 : 0;
		mInitialCompilerDiagnosticFlags = (initialTrackCompilerWarnings ? 1 : 0) | (initialTrackCompilerNotes ? 2 : 0);
		writeRecordField(mDataCursorMarker, sizeof(mDataCursorMarker), initialCursorPositionMarker);
		mInitialFileCompareComparePanelReadOnly = initialFileCompareComparePanelReadOnly;
		if (mIndentStyleField != nullptr) {
			ushort styleChoice = 0;
			switch (initialUiIndentStyle) {
				case MRUiIndentStyle::KandR4:
					styleChoice = 1;
					break;
				case MRUiIndentStyle::Allman:
					styleChoice = 2;
					break;
				case MRUiIndentStyle::Gnome:
					styleChoice = 3;
					break;
				case MRUiIndentStyle::Whitesmiths:
					styleChoice = 4;
					break;
				case MRUiIndentStyle::Horstmann:
					styleChoice = 5;
					break;
				case MRUiIndentStyle::KandR:
				default:
					styleChoice = 0;
					break;
			}
			mIndentStyleField->setData(&styleChoice);
			mLastIndentStyleChoice = styleChoice;
			if (mIndentStylePreview != nullptr) mIndentStylePreview->setStyle(styleChoice);
		}
		setDialogValidationHook([this]() { return validateDialogValues(); });

		selectContent();
	}

	void handleEvent(TEvent &event) override {
		MRScrollableDialog::handleEvent(event);
		refreshIndentStylePreview();
	}

	void getData(void *rec) override {
		UserInterfaceSettingsDialogData *data = static_cast<UserInterfaceSettingsDialogData *>(rec);
		if (mOptionsField != nullptr) {
			ushort visualFlags = 0;
			mOptionsField->getData(&visualFlags);
			data->flags = visualFlags & 0x0001;
			if ((visualFlags & 0x0002) != 0) data->flags |= 0x0002;
			data->compilerDiagnosticFlags = static_cast<ushort>((visualFlags >> 2) & 0x0003);
			if ((visualFlags & 0x0010) != 0) data->flags |= 0x0004;
			if ((visualFlags & 0x0020) != 0) data->flags |= 0x0008;
		}
		if (mHeroMessagesField != nullptr) mHeroMessagesField->getData(&data->heroFlags);
		if (mHeroFileThresholdSlider != nullptr) {
			int32_t value = 8;
			mHeroFileThresholdSlider->getData(&value);
			data->heroFileThresholdMb = static_cast<ushort>(value);
		}
		if (mVirtualDesktopsSlider != nullptr) {
			int32_t val = 1;
			mVirtualDesktopsSlider->getData(&val);
			data->virtualDesktops = static_cast<ushort>(val);
		}
		if (mCursorBehaviourField != nullptr) mCursorBehaviourField->getData(&data->cursorBehaviourChoice);
		if (mCompilerErrorMessageField != nullptr) mCompilerErrorMessageField->getData(&data->compilerErrorMessageChoice);
		if (mFileCompareStartField != nullptr) mFileCompareStartField->getData(&data->fileCompareStartChoice);
		if (mCompilerDiagnosticsField != nullptr) mCompilerDiagnosticsField->getData(&data->compilerDiagnosticFlags);
		if (mScrollbarVisibilityField != nullptr) mScrollbarVisibilityField->getData(&data->scrollbarVisibilityChoice);
		if (mColorOutputModeField != nullptr) mColorOutputModeField->getData(&data->colorOutputModeChoice);
		if (mIndentStyleField != nullptr) mIndentStyleField->getData(&data->uiIndentStyleChoice);
		if (mCursorPositionMarkerField != nullptr) mCursorPositionMarkerField->getData(data->cursorPositionMarker);
		writeRecordField(data->fileCompareOriginalLeadingGutters, sizeof(data->fileCompareOriginalLeadingGutters), fileCompareGutterSpinnersText(mFileCompareOriginalLeadingGutterSpinners));
		writeRecordField(data->fileCompareOriginalTrailingGutters, sizeof(data->fileCompareOriginalTrailingGutters), fileCompareGutterSpinnersText(mFileCompareOriginalTrailingGutterSpinners));
		writeRecordField(data->fileCompareCompareLeadingGutters, sizeof(data->fileCompareCompareLeadingGutters), fileCompareGutterSpinnersText(mFileCompareCompareLeadingGutterSpinners));
		writeRecordField(data->fileCompareCompareTrailingGutters, sizeof(data->fileCompareCompareTrailingGutters), fileCompareGutterSpinnersText(mFileCompareCompareTrailingGutterSpinners));
	}

	void setData(void *rec) override {
		UserInterfaceSettingsDialogData *data = static_cast<UserInterfaceSettingsDialogData *>(rec);
		if (mOptionsField != nullptr) {
			ushort visualFlags = static_cast<ushort>(data->flags & 0x0001);
			if ((data->flags & 0x0002) != 0) visualFlags |= 0x0002;
			visualFlags |= static_cast<ushort>((data->compilerDiagnosticFlags & 0x0003) << 2);
			if ((data->flags & 0x0004) != 0) visualFlags |= 0x0010;
			if ((data->flags & 0x0008) != 0) visualFlags |= 0x0020;
			mOptionsField->setData(&visualFlags);
		}
		if (mHeroMessagesField != nullptr) {
			ushort heroFlags = data->heroFlags & 3;
			mHeroMessagesField->setData(&heroFlags);
		}
		if (mHeroFileThresholdSlider != nullptr) {
			int32_t value = std::min<ushort>(data->heroFileThresholdMb, 16);
			mHeroFileThresholdSlider->setData(&value);
		}
		if (mVirtualDesktopsSlider != nullptr) {
			int32_t val = data->virtualDesktops;
			mVirtualDesktopsSlider->setData(&val);
		}
		if (mCursorBehaviourField != nullptr) {
			if (data->cursorBehaviourChoice > 1) data->cursorBehaviourChoice = mInitialCursorBehaviourChoice;
			mCursorBehaviourField->setData(&data->cursorBehaviourChoice);
		}
		if (mCompilerErrorMessageField != nullptr) {
			if (data->compilerErrorMessageChoice > 1) data->compilerErrorMessageChoice = mInitialCompilerErrorMessageChoice;
			mCompilerErrorMessageField->setData(&data->compilerErrorMessageChoice);
		}
		if (mFileCompareStartField != nullptr) {
			if (data->fileCompareStartChoice > 1) data->fileCompareStartChoice = mInitialFileCompareStartChoice;
			mFileCompareStartField->setData(&data->fileCompareStartChoice);
		}
		if (mCompilerDiagnosticsField != nullptr) {
			data->compilerDiagnosticFlags &= 3;
			if (data->compilerDiagnosticFlags > 3) data->compilerDiagnosticFlags = mInitialCompilerDiagnosticFlags;
			mCompilerDiagnosticsField->setData(&data->compilerDiagnosticFlags);
		}
		if (mScrollbarVisibilityField != nullptr) {
			if (data->scrollbarVisibilityChoice > 1) data->scrollbarVisibilityChoice = mInitialScrollbarVisibilityChoice;
			mScrollbarVisibilityField->setData(&data->scrollbarVisibilityChoice);
		}
		if (mColorOutputModeField != nullptr) {
			if (data->colorOutputModeChoice > 1) data->colorOutputModeChoice = mInitialColorOutputModeChoice;
			mColorOutputModeField->setData(&data->colorOutputModeChoice);
		}
		if (mIndentStyleField != nullptr) {
			if (data->uiIndentStyleChoice > 5) data->uiIndentStyleChoice = 0;
			mIndentStyleField->setData(&data->uiIndentStyleChoice);
		}
		if (mCursorPositionMarkerField != nullptr) {
			if (data->cursorPositionMarker[0] == '\0') writeRecordField(data->cursorPositionMarker, sizeof(data->cursorPositionMarker), mDataCursorMarker);
			mCursorPositionMarkerField->setData(data->cursorPositionMarker);
		}
		setFileCompareGutterSpinners(mFileCompareOriginalLeadingGutterSpinners, readRecordField(data->fileCompareOriginalLeadingGutters));
		setFileCompareGutterSpinners(mFileCompareOriginalTrailingGutterSpinners, readRecordField(data->fileCompareOriginalTrailingGutters));
		setFileCompareGutterSpinners(mFileCompareCompareLeadingGutterSpinners, readRecordField(data->fileCompareCompareLeadingGutters));
		setFileCompareGutterSpinners(mFileCompareCompareTrailingGutterSpinners, readRecordField(data->fileCompareCompareTrailingGutters));
	}

  private:
	static const std::size_t kFileCompareGutterSpinnerCount = 4;

	void addFileCompareGutterSpinners(std::array<MRSpinner *, kFileCompareGutterSpinnerCount> &spinners, int x, int y) {
		for (std::size_t i = 0; i < spinners.size(); ++i) {
			const int left = x + static_cast<int>(i);

			spinners[i] = new MRSpinner(TRect(left, y, left + 1, y + 3), fileCompareGutterSpinnerValues());
			addManaged(spinners[i], TRect(left, y, left + 1, y + 3));
		}
	}

	std::string fileCompareGutterSpinnersText(const std::array<MRSpinner *, kFileCompareGutterSpinnerCount> &spinners) const {
		std::string text;

		text.reserve(spinners.size());
		for (MRSpinner *spinner : spinners) {
			if (spinner == nullptr) continue;
			const std::string &value = spinner->currentValue();

			if (value != " ") text += value;
		}
		return text;
	}

	void setFileCompareGutterSpinners(std::array<MRSpinner *, kFileCompareGutterSpinnerCount> &spinners, std::string_view text) {
		std::size_t index = 0;

		for (MRSpinner *spinner : spinners)
			if (spinner != nullptr) spinner->setCurrentValue(" ");
		for (char ch : text) {
			if (index >= spinners.size()) break;
			char value = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));

			switch (value) {
				case 'M':
				case 'D':
				case 'L':
				case 'C':
					if (spinners[index] != nullptr) spinners[index]->setCurrentValue(std::string(1, value));
					++index;
					break;
				default:
					break;
			}
		}
	}

	std::string currentCursorMarkerInput() const {
		char value[12] = {0};
		if (mCursorPositionMarkerField != nullptr) mCursorPositionMarkerField->getData(value);
		return readRecordField(value);
	}

	std::string currentFileCompareOriginalLeadingGuttersInput() const {
		return fileCompareGutterSpinnersText(mFileCompareOriginalLeadingGutterSpinners);
	}

	std::string currentFileCompareOriginalTrailingGuttersInput() const {
		return fileCompareGutterSpinnersText(mFileCompareOriginalTrailingGutterSpinners);
	}

	std::string currentFileCompareCompareLeadingGuttersInput() const {
		return fileCompareGutterSpinnersText(mFileCompareCompareLeadingGutterSpinners);
	}

	std::string currentFileCompareCompareTrailingGuttersInput() const {
		return fileCompareGutterSpinnersText(mFileCompareCompareTrailingGutterSpinners);
	}

	DialogValidationResult validateDialogValues() const {
		DialogValidationResult result;
		std::string errorText;
		result.valid = validateCursorPositionMarkerInput(currentCursorMarkerInput(), errorText);
		if (result.valid) result.valid = validateFileCompareGuttersInput(currentFileCompareOriginalLeadingGuttersInput(), errorText);
		if (result.valid) result.valid = validateFileCompareGuttersInput(currentFileCompareOriginalTrailingGuttersInput(), errorText);
		if (result.valid) result.valid = validateFileCompareGuttersInput(currentFileCompareCompareLeadingGuttersInput(), errorText);
		if (result.valid) result.valid = validateFileCompareGuttersInput(currentFileCompareCompareTrailingGuttersInput(), errorText);
		result.warningText = errorText;
		return result;
	}

	ushort currentIndentStyleChoice() const {
		ushort value = 0;
		if (mIndentStyleField != nullptr) const_cast<TRadioButtons *>(mIndentStyleField)->getData(&value);
		return value;
	}

	void refreshIndentStylePreview() {
		const ushort choice = currentIndentStyleChoice();
		if (choice == mLastIndentStyleChoice) return;
		mLastIndentStyleChoice = choice;
		if (mIndentStylePreview != nullptr) mIndentStylePreview->setStyle(choice);
	}

	TCheckBoxes *mOptionsField = nullptr;
	TCheckBoxes *mHeroMessagesField = nullptr;
	MRNumericSlider *mHeroFileThresholdSlider = nullptr;
	MRNumericSlider *mVirtualDesktopsSlider = nullptr;
	TRadioButtons *mCursorBehaviourField = nullptr;
	TRadioButtons *mCompilerErrorMessageField = nullptr;
	TRadioButtons *mFileCompareStartField = nullptr;
	TCheckBoxes *mCompilerDiagnosticsField = nullptr;
	TRadioButtons *mScrollbarVisibilityField = nullptr;
	TRadioButtons *mColorOutputModeField = nullptr;
	TInputLine *mCursorPositionMarkerField = nullptr;
	std::array<MRSpinner *, kFileCompareGutterSpinnerCount> mFileCompareOriginalLeadingGutterSpinners{};
	std::array<MRSpinner *, kFileCompareGutterSpinnerCount> mFileCompareOriginalTrailingGutterSpinners{};
	std::array<MRSpinner *, kFileCompareGutterSpinnerCount> mFileCompareCompareLeadingGutterSpinners{};
	std::array<MRSpinner *, kFileCompareGutterSpinnerCount> mFileCompareCompareTrailingGutterSpinners{};
	TRadioButtons *mIndentStyleField = nullptr;
	TIndentStylePreview *mIndentStylePreview = nullptr;
	ushort mInitialCursorBehaviourChoice = 1;
	ushort mInitialCompilerErrorMessageChoice = 1;
	ushort mInitialFileCompareStartChoice = 0;
	ushort mInitialScrollbarVisibilityChoice = 0;
	ushort mInitialColorOutputModeChoice = 0;
	ushort mInitialCompilerDiagnosticFlags = 0;
	bool mInitialFileCompareComparePanelReadOnly = true;
	ushort mLastIndentStyleChoice = 0;
	char mDataCursorMarker[12] = {0};
};

} // namespace

void runUserInterfaceSettingsDialogFlow() {
	bool running = true;

	while (running) {
		bool currentWm = configuredWindowManager();
		MRHeroMessageSettings currentHeroMessages = configuredHeroMessageSettings();
		bool currentAutoDetectBinaryFiles = configuredAutoDetectBinaryFiles();
		int currentVd = configuredVirtualDesktops();
		bool currentCv = configuredCyclicVirtualDesktops();
		MRCursorBehaviour currentCb = configuredCursorBehaviour();
		MRCompilerErrorMessagePlacement currentCemp = configuredCompilerErrorMessagePlacement();
		MRScrollbarVisibility currentScrollbarVisibility = configuredScrollbarVisibility();
		MRColorOutputMode currentColorOutputMode = configuredColorOutputMode();
		bool currentTrackWarnings = configuredTrackCompilerWarnings();
		bool currentTrackNotes = configuredTrackCompilerNotes();
		MRUiIndentStyle currentUiIndentStyle = configuredUiIndentStyle();
		std::string currentCp = configuredCursorPositionMarker();
		std::string currentFileCompareOriginalLeadingGutters = configuredFileCompareOriginalLeadingGutters();
		std::string currentFileCompareOriginalTrailingGutters = configuredFileCompareOriginalTrailingGutters();
		std::string currentFileCompareCompareLeadingGutters = configuredFileCompareCompareLeadingGutters();
		std::string currentFileCompareCompareTrailingGutters = configuredFileCompareCompareTrailingGutters();
		MRFileCompareStartConfiguration currentFileCompareStartConfiguration = configuredFileCompareStartConfiguration();
		bool currentFileCompareComparePanelReadOnly = configuredFileCompareComparePanelReadOnly();

		TUserInterfaceSettingsDialog *dialog = new TUserInterfaceSettingsDialog(currentWm, currentHeroMessages, currentVd, currentCv, currentCb, currentCemp, currentScrollbarVisibility, currentColorOutputMode, currentTrackWarnings, currentTrackNotes, currentUiIndentStyle, currentCp,
		                                                                         currentFileCompareOriginalLeadingGutters, currentFileCompareOriginalTrailingGutters, currentFileCompareCompareLeadingGutters, currentFileCompareCompareTrailingGutters,
		                                                                         currentFileCompareStartConfiguration, currentFileCompareComparePanelReadOnly);
		UserInterfaceSettingsDialogData dialogData;
		if (currentWm) dialogData.flags |= 1;
		if (currentCv) dialogData.flags |= 2;
		if (currentFileCompareComparePanelReadOnly) dialogData.flags |= 4;
		if (currentAutoDetectBinaryFiles) dialogData.flags |= 8;
		if (currentHeroMessages.onMessageLine) dialogData.heroFlags |= 1;
		if (currentHeroMessages.inLogFile) dialogData.heroFlags |= 2;
		dialogData.heroFileThresholdMb = static_cast<ushort>(currentHeroMessages.fileThresholdMb);

		dialogData.virtualDesktops = static_cast<ushort>(currentVd);
		dialogData.cursorBehaviourChoice = currentCb == MRCursorBehaviour::FreeMovement ? 0 : 1;
		dialogData.compilerErrorMessageChoice = currentCemp == MRCompilerErrorMessagePlacement::UnderCode ? 0 : 1;
		dialogData.fileCompareStartChoice = currentFileCompareStartConfiguration == MRFileCompareStartConfiguration::CompareOriginal ? 1 : 0;
		dialogData.scrollbarVisibilityChoice = currentScrollbarVisibility == MRScrollbarVisibility::Always ? 1 : 0;
		dialogData.colorOutputModeChoice = currentColorOutputMode == MRColorOutputMode::TerminalPalette ? 1 : 0;
		if (currentTrackWarnings) dialogData.compilerDiagnosticFlags |= 1;
		if (currentTrackNotes) dialogData.compilerDiagnosticFlags |= 2;
		dialogData.uiIndentStyleChoice = static_cast<ushort>(currentUiIndentStyle);
		writeRecordField(dialogData.cursorPositionMarker, sizeof(dialogData.cursorPositionMarker), currentCp);
		writeRecordField(dialogData.fileCompareOriginalLeadingGutters, sizeof(dialogData.fileCompareOriginalLeadingGutters), currentFileCompareOriginalLeadingGutters);
		writeRecordField(dialogData.fileCompareOriginalTrailingGutters, sizeof(dialogData.fileCompareOriginalTrailingGutters), currentFileCompareOriginalTrailingGutters);
		writeRecordField(dialogData.fileCompareCompareLeadingGutters, sizeof(dialogData.fileCompareCompareLeadingGutters), currentFileCompareCompareLeadingGutters);
		writeRecordField(dialogData.fileCompareCompareTrailingGutters, sizeof(dialogData.fileCompareCompareTrailingGutters), currentFileCompareCompareTrailingGutters);

		UserInterfaceSettingsDialogData baselineData = dialogData;
		ushort result = execDialogWithDataCapture(dialog, &dialogData);
		bool newWm = (dialogData.flags & 1) != 0;
		bool newCv = (dialogData.flags & 2) != 0;
		bool newFileCompareComparePanelReadOnly = (dialogData.flags & 4) != 0;
		bool newAutoDetectBinaryFiles = (dialogData.flags & 8) != 0;
		MRHeroMessageSettings newHeroMessages;
		newHeroMessages.onMessageLine = (dialogData.heroFlags & 1) != 0;
		newHeroMessages.inLogFile = (dialogData.heroFlags & 2) != 0;
		newHeroMessages.fileThresholdMb = static_cast<int>(dialogData.heroFileThresholdMb);
		int newVd = static_cast<int>(dialogData.virtualDesktops);
		MRCursorBehaviour newCb = dialogData.cursorBehaviourChoice == 0 ? MRCursorBehaviour::FreeMovement : MRCursorBehaviour::BoundToText;
		MRCompilerErrorMessagePlacement newCemp = dialogData.compilerErrorMessageChoice == 0 ? MRCompilerErrorMessagePlacement::UnderCode : MRCompilerErrorMessagePlacement::RightMargin;
		MRFileCompareStartConfiguration newFileCompareStartConfiguration = dialogData.fileCompareStartChoice == 1 ? MRFileCompareStartConfiguration::CompareOriginal : MRFileCompareStartConfiguration::OriginalCompare;
		MRScrollbarVisibility newScrollbarVisibility = dialogData.scrollbarVisibilityChoice == 1 ? MRScrollbarVisibility::Always : MRScrollbarVisibility::Smart;
		MRColorOutputMode newColorOutputMode = dialogData.colorOutputModeChoice == 1 ? MRColorOutputMode::TerminalPalette : MRColorOutputMode::RgbAutomatic;
		bool newTrackWarnings = (dialogData.compilerDiagnosticFlags & 1) != 0;
		bool newTrackNotes = (dialogData.compilerDiagnosticFlags & 2) != 0;
		MRUiIndentStyle newUiIndentStyle = static_cast<MRUiIndentStyle>(dialogData.uiIndentStyleChoice);
		std::string newCp = readRecordField(dialogData.cursorPositionMarker);
		std::string newFileCompareOriginalLeadingGutters = readRecordField(dialogData.fileCompareOriginalLeadingGutters);
		std::string newFileCompareOriginalTrailingGutters = readRecordField(dialogData.fileCompareOriginalTrailingGutters);
		std::string newFileCompareCompareLeadingGutters = readRecordField(dialogData.fileCompareCompareLeadingGutters);
		std::string newFileCompareCompareTrailingGutters = readRecordField(dialogData.fileCompareCompareTrailingGutters);
		const bool changed = mr::dialogs::isDialogDraftDirty(baselineData, dialogData, userInterfaceSettingsDialogDataEqual);
		const bool compilerDiagnosticFilterChanged = currentTrackWarnings != newTrackWarnings || currentTrackNotes != newTrackNotes;
		const bool scrollbarVisibilityChanged = currentScrollbarVisibility != newScrollbarVisibility;
		const bool colorOutputModeChanged = currentColorOutputMode != newColorOutputMode;
		auto applyAndPersistUiSettings = [&]() -> bool {
			std::string errorText;
			if (!setConfiguredCursorBehaviour(newCb, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredCompilerErrorMessagePlacement(newCemp, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredScrollbarVisibility(newScrollbarVisibility, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredColorOutputMode(newColorOutputMode, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredTrackCompilerWarnings(newTrackWarnings, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredTrackCompilerNotes(newTrackNotes, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredUiIndentStyle(newUiIndentStyle, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredCursorPositionMarker(newCp, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredFileCompareOriginalLeadingGutters(newFileCompareOriginalLeadingGutters, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredFileCompareOriginalTrailingGutters(newFileCompareOriginalTrailingGutters, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredFileCompareCompareLeadingGutters(newFileCompareCompareLeadingGutters, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredFileCompareCompareTrailingGutters(newFileCompareCompareTrailingGutters, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredFileCompareStartConfiguration(newFileCompareStartConfiguration, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredFileCompareComparePanelReadOnly(newFileCompareComparePanelReadOnly, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			setConfiguredWindowManager(newWm, &errorText);
			if (!setConfiguredHeroMessageSettings(newHeroMessages, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			setConfiguredAutoDetectBinaryFiles(newAutoDetectBinaryFiles, &errorText);
			setConfiguredCyclicVirtualDesktops(newCv, &errorText);
			applyVirtualDesktopConfigurationChange(newVd);
			for (MREditWindow *window : allEditWindowsInZOrder())
				if (window != nullptr && window->getEditor() != nullptr) window->getEditor()->refreshConfiguredVisualSettings();
			for (MREditWindow *window : allEditWindowsInZOrder())
				if (MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window); bentoBox != nullptr && bentoBox->isFileCompareBox()) bentoBox->refreshFileCompareConfiguration();
			if (scrollbarVisibilityChanged)
				for (MREditWindow *window : allEditWindowsInZOrder())
					if (MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window); bentoBox != nullptr) bentoBox->changeBounds(bentoBox->getBounds());
			if (compilerDiagnosticFilterChanged)
				for (MREditWindow *window : allEditWindowsInZOrder())
					if (MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window); bentoBox != nullptr && bentoBox->buildOutputPane() != nullptr && bentoBox->problemsPane() != nullptr)
						static_cast<void>(bentoBox->refreshCompilerDiagnosticsFromOutput());
			if (colorOutputModeChanged) {
				if (TProgram::application != nullptr) TProgram::application->redraw();
				mrUpdateAllWindowsColorTheme();
				if (TProgram::deskTop != nullptr) {
					TProgram::deskTop->redraw();
					TProgram::deskTop->drawView();
				}
			}
			if (!persistConfiguredSettingsSnapshot(&errorText)) postSetupFlowError("Installation / User interface settings", errorText);
			return true;
		};

		switch (result) {
			case cmOK:
				if (changed && !applyAndPersistUiSettings()) break;
				running = false;
				break;

			case cmClose:
			case cmCancel:
				if (!changed) {
					running = false;
					break;
				}
				switch (mr::dialogs::runDialogDirtyGating("User interface settings have unsaved changes.")) {
					case mr::dialogs::UnsavedChangesChoice::Save:
						if (!applyAndPersistUiSettings()) break;
						running = false;
						break;
					case mr::dialogs::UnsavedChangesChoice::Discard:
						running = false;
						break;
					case mr::dialogs::UnsavedChangesChoice::Cancel:
						discardQueuedCancelEvent();
						break;
					default:
						break;
				}
				break;

			default:
				running = false;
				break;
		}
	}
	clearSetupDialogStatus();
}
