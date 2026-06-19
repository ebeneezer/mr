#include "MRBentoBox.hpp"

#include "MRFrame.hpp"
#include "MRSidekickEditor.hpp"
#include "MRWindowSupport.hpp"

#include "../app/commands/MRWindowCommands.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../coprocessor/MRCoprocessor.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace {

constexpr int kMinimumPaneHeight = 3;
constexpr int kMinimumPaneWidth = 20;
constexpr int kPaneChromeGap = 1;
constexpr int kPaneCloseButtonWidth = 3;
constexpr int kPaneMaximizeButtonWidth = 3;
constexpr int kPaneChromeFrameRest = 2;
constexpr ushort cmMrBentoPaneRoleAccepted = 0x7A20;
constexpr ushort cmMrBentoPaneActionAccepted = 0x7A21;
constexpr ushort cmMrFileComparePaneActionAccepted = 0x7A22;
static const char *kPaneCloseIcon = "[\xFE]";
static const char *kPaneMaximizeIcon = "[▴]";
static const char *kPaneRestoreIcon = "[▾]";
static const char *kBentoPaneActionReplace = "replace";
static const char *kBentoPaneActionSplitRight = "split \xC4";
static const char *kBentoPaneActionSplitDown = "split \xB3";
static const char *kFileCompareActionNext = "next diff";
static const char *kFileCompareActionPrevious = "prev diff";
static const char *kFileCompareActionApply = "apply diff";
static const MRBentoPaneTitleMenuSpec kBentoRoleTitleMenu{"role"};

struct BentoPaneActionDescriptor {
	const char *action;
	MRBentoPanePlacement placement;
};

static const BentoPaneActionDescriptor kBentoPaneActions[] = {
	{kBentoPaneActionReplace, bppReplace},
	{kBentoPaneActionSplitRight, bppSplitDown},
	{kBentoPaneActionSplitDown, bppSplitRight},
};

struct BentoPaneRoleDescriptor {
	MRBentoPaneRole role;
	const char *title;
	bool listed;
};

static const BentoPaneRoleDescriptor kBentoPaneRoles[] = {
	{bprSource, "Source", false},
	{bprCompilerOutput, "Compiler Output", true},
	{bprAppOutput, "App Output", true},
	{bprProblems, "Problems", true},
	{bprDebuggerOutput, "Debugger Output", true},
	{bprWatches, "Watches", true},
	{bprVariables, "Variables", true},
	{bprStructure, "Structure", true},
	{bprFunctions, "Functions", true},
	{bprSplitEditor, "Split editor", true},
	{bprDiffOriginal, "Diff Original", true},
	{bprDiffCompare, "Diff Compare", true},
};

const char *bentoPaneRoleTitle(MRBentoPaneRole role) noexcept {
	for (const BentoPaneRoleDescriptor &descriptor : kBentoPaneRoles)
		if (descriptor.role == role) return descriptor.title;
	return "Pane";
}

bool bentoRoleIsOutline(MRBentoPaneRole role) noexcept {
	switch (role) {
		case bprStructure:
		case bprFunctions:
			return true;
		default:
			return false;
	}
}

bool bentoRoleIsDiff(MRBentoPaneRole role) noexcept {
	return role == bprDiffOriginal || role == bprDiffCompare;
}

bool fileCompareGuttersContain(const std::string &gutters, char marker) noexcept {
	for (char ch : gutters)
		if (static_cast<char>(std::toupper(static_cast<unsigned char>(ch))) == marker) return true;
	return false;
}

void markFileCompareLineRange(std::vector<unsigned char> &lineKinds, std::size_t startLine, std::size_t lineCount, unsigned char lineKind) {
	for (std::size_t i = 0; i < lineCount && startLine + i < lineKinds.size(); ++i)
		lineKinds[startLine + i] = lineKind;
}

void markFileCompareAnchorLine(std::vector<unsigned char> &lineKinds, std::size_t lineIndex, unsigned char lineKind) {
	if (lineKinds.empty()) return;
	lineKinds[std::min(lineIndex, lineKinds.size() - 1)] = lineKind;
}

std::size_t fileCompareLineTextLength(const std::vector<std::string> &lines, std::size_t startLine, std::size_t lineCount) noexcept {
	std::size_t length = 0;

	for (std::size_t i = 0; i < lineCount && startLine + i < lines.size(); ++i)
		length += lines[startLine + i].size();
	return length;
}

std::string fileCompareJoinedLineRange(const std::vector<std::string> &lines, std::size_t startLine, std::size_t lineCount, bool prefixNewline, bool suffixNewline) {
	std::string text;

	if (lineCount == 0) return text;
	if (prefixNewline) text.push_back('\n');
	for (std::size_t i = 0; i < lineCount && startLine + i < lines.size(); ++i) {
		if (i != 0) text.push_back('\n');
		text += lines[startLine + i];
	}
	if (suffixNewline) text.push_back('\n');
	return text;
}

bool fileCompareEditorLineRange(const MRFileEditor &editor, std::size_t startLine, std::size_t lineCount, std::size_t &rangeStart, std::size_t &rangeEnd) noexcept {
	const MRTextBufferModel &model = editor.bufferModel();
	const std::size_t editorLineCount = model.lineCount();

	rangeStart = startLine < editorLineCount ? model.lineStartByIndex(startLine) : model.length();
	if (lineCount == 0) {
		rangeEnd = rangeStart;
		return true;
	}
	const std::size_t endLine = startLine + lineCount;
	rangeEnd = endLine < editorLineCount ? model.lineStartByIndex(endLine) : model.length();
	return rangeEnd >= rangeStart;
}

std::size_t mappedFileCompareLineForRole(const std::vector<mr::diff::MRDiffHunk> &hunks, MRBentoPaneRole sourceRole, std::size_t sourceLine) noexcept {
	for (const mr::diff::MRDiffHunk &hunk : hunks) {
		switch (hunk.op) {
			case mr::diff::MRDiffOp::Equal:
				if (sourceRole == bprDiffOriginal && sourceLine >= hunk.leftStart && sourceLine < hunk.leftStart + hunk.count) return hunk.rightStart + (sourceLine - hunk.leftStart);
				if (sourceRole == bprDiffCompare && sourceLine >= hunk.rightStart && sourceLine < hunk.rightStart + hunk.count) return hunk.leftStart + (sourceLine - hunk.rightStart);
				break;
			case mr::diff::MRDiffOp::Delete:
				if (sourceRole == bprDiffOriginal && sourceLine >= hunk.leftStart && sourceLine < hunk.leftStart + hunk.count) return hunk.rightStart;
				break;
			case mr::diff::MRDiffOp::Insert:
				if (sourceRole == bprDiffCompare && sourceLine >= hunk.rightStart && sourceLine < hunk.rightStart + hunk.count) return hunk.leftStart;
				break;
			default:
				break;
		}
	}
	return sourceLine;
}

bool bentoWorkspaceModeIsValid(int mode) noexcept {
	return mode == bbmToolWorkspace || mode == bbmDocumentViewports || mode == bbmFileCompare;
}

bool bentoWorkspaceRoleIsValid(int role) noexcept {
	return role >= bprSource && role <= bprDiffCompare;
}

MRBentoPaneRole bentoPaneRoleForTitle(const std::string &title) {
	for (const BentoPaneRoleDescriptor &descriptor : kBentoPaneRoles)
		if (descriptor.listed && title == descriptor.title) return descriptor.role;
	return bprCompilerOutput;
}

std::string diffDisplayTitle(const MRBentoCompareSource &source, const char *fallback) {
	return source.title.empty() ? std::string(fallback) : source.title;
}

void appendDiffDisplayLine(std::string &text, std::vector<unsigned char> *lineKinds, std::size_t &displayLineCount, const std::string &line, unsigned char lineKind) {
	if (displayLineCount > 0) text.push_back('\n');
	text += line;
	if (lineKinds != nullptr) lineKinds->push_back(lineKind);
	++displayLineCount;
}

void hideFileCompareSourceWindow(const MRBentoCompareSource &source) {
	MREditWindow *window = findEditWindowByBufferId(source.bufferId);

	if (window == nullptr) return;
	setWindowManuallyHidden(window, true);
	window->hide();
}

bool fileCompareSourceStillMatches(const MRBentoCompareSource &source) {
	MREditWindow *window = findEditWindowByBufferId(source.bufferId);

	return window != nullptr && window->documentId() == source.documentId && window->documentVersion() == source.version;
}

bool fileCompareHunkCanExtend(const mr::diff::MRDiffHunk &hunk, mr::diff::MRDiffOp op, std::size_t leftStart, std::size_t rightStart) noexcept {
	if (hunk.op != op) return false;
	switch (op) {
		case mr::diff::MRDiffOp::Equal:
			return leftStart == hunk.leftStart + hunk.count && rightStart == hunk.rightStart + hunk.count;
		case mr::diff::MRDiffOp::Delete:
			return leftStart == hunk.leftStart + hunk.count && rightStart == hunk.rightStart;
		case mr::diff::MRDiffOp::Insert:
			return leftStart == hunk.leftStart && rightStart == hunk.rightStart + hunk.count;
		default:
			break;
	}
	return false;
}

void appendFileCompareHunk(std::vector<mr::diff::MRDiffHunk> &hunks, mr::diff::MRDiffOp op, std::size_t leftStart, std::size_t rightStart, std::size_t count) {
	if (count == 0) return;
	if (!hunks.empty() && fileCompareHunkCanExtend(hunks.back(), op, leftStart, rightStart)) {
		hunks.back().count += count;
		return;
	}
	hunks.push_back(mr::diff::MRDiffHunk(op, leftStart, rightStart, count));
}

void appendNormalizedFileCompareChangeGroup(std::vector<mr::diff::MRDiffHunk> &hunks, const std::vector<std::string> &originalLines, const std::vector<std::string> &compareLines, std::size_t originalStart, std::size_t compareStart, std::size_t deletedLineCount, std::size_t insertedLineCount) {
	if (deletedLineCount == 0) {
		appendFileCompareHunk(hunks, mr::diff::MRDiffOp::Insert, originalStart, compareStart, insertedLineCount);
		return;
	}
	if (insertedLineCount == 0) {
		appendFileCompareHunk(hunks, mr::diff::MRDiffOp::Delete, originalStart, compareStart, deletedLineCount);
		return;
	}

	const std::size_t maxLineCount = std::max(deletedLineCount, insertedLineCount);
	std::size_t runStart = 0;
	std::size_t i = 0;
	while (i < maxLineCount) {
		const bool equalPair = i < deletedLineCount && i < insertedLineCount && originalStart + i < originalLines.size() && compareStart + i < compareLines.size() && originalLines[originalStart + i] == compareLines[compareStart + i];
		if (!equalPair) {
			++i;
			continue;
		}

		if (runStart < i) {
			const std::size_t deletedRunCount = runStart < deletedLineCount ? std::min(i, deletedLineCount) - runStart : 0;
			const std::size_t insertedRunCount = runStart < insertedLineCount ? std::min(i, insertedLineCount) - runStart : 0;
			appendFileCompareHunk(hunks, mr::diff::MRDiffOp::Delete, originalStart + runStart, compareStart + std::min(runStart, insertedLineCount), deletedRunCount);
			appendFileCompareHunk(hunks, mr::diff::MRDiffOp::Insert, originalStart + std::min(runStart + deletedRunCount, deletedLineCount), compareStart + runStart, insertedRunCount);
		}
		appendFileCompareHunk(hunks, mr::diff::MRDiffOp::Equal, originalStart + i, compareStart + i, 1);
		++i;
		runStart = i;
	}
	if (runStart < maxLineCount) {
		const std::size_t deletedRunCount = runStart < deletedLineCount ? deletedLineCount - runStart : 0;
		const std::size_t insertedRunCount = runStart < insertedLineCount ? insertedLineCount - runStart : 0;
		appendFileCompareHunk(hunks, mr::diff::MRDiffOp::Delete, originalStart + runStart, compareStart + std::min(runStart, insertedLineCount), deletedRunCount);
		appendFileCompareHunk(hunks, mr::diff::MRDiffOp::Insert, originalStart + std::min(runStart + deletedRunCount, deletedLineCount), compareStart + runStart, insertedRunCount);
	}
}

void normalizeFileCompareHunks(const std::vector<std::string> &originalLines, const std::vector<std::string> &compareLines, std::vector<mr::diff::MRDiffHunk> &hunks) {
	std::vector<mr::diff::MRDiffHunk> normalizedHunks;
	bool groupOpen = false;
	std::size_t groupOriginalStart = 0;
	std::size_t groupCompareStart = 0;
	std::size_t groupDeletedLineCount = 0;
	std::size_t groupInsertedLineCount = 0;
	auto flushGroup = [&]() {
		if (!groupOpen) return;
		appendNormalizedFileCompareChangeGroup(normalizedHunks, originalLines, compareLines, groupOriginalStart, groupCompareStart, groupDeletedLineCount, groupInsertedLineCount);
		groupOpen = false;
		groupDeletedLineCount = 0;
		groupInsertedLineCount = 0;
	};

	normalizedHunks.reserve(hunks.size());
	for (const mr::diff::MRDiffHunk &hunk : hunks) {
		switch (hunk.op) {
			case mr::diff::MRDiffOp::Equal:
				flushGroup();
				appendFileCompareHunk(normalizedHunks, hunk.op, hunk.leftStart, hunk.rightStart, hunk.count);
				break;
			case mr::diff::MRDiffOp::Delete:
				if (!groupOpen) {
					groupOpen = true;
					groupOriginalStart = hunk.leftStart;
					groupCompareStart = hunk.rightStart;
				}
				groupDeletedLineCount += hunk.count;
				break;
			case mr::diff::MRDiffOp::Insert:
				if (!groupOpen) {
					groupOpen = true;
					groupOriginalStart = hunk.leftStart;
					groupCompareStart = hunk.rightStart;
				}
				groupInsertedLineCount += hunk.count;
				break;
			default:
				break;
		}
	}
	flushGroup();
	hunks.swap(normalizedHunks);
}

struct BentoFrameGlyphs {
	char singleHorizontal = '\xC4';
	char singleVertical = '\xB3';
	char singleLeftT = '\xB4';
	char singleRightT = '\xC3';
	char singleDownT = '\xC2';
	char doubleHorizontal = '\xCD';
	char doubleVertical = '\xBA';
	char doubleLeftT = '\xB9';
	char doubleRightT = '\xCC';
	char mixedDoubleHorizontalDown = '\xD2';
	char mixedSingleHorizontalDoubleVerticalLeft = '\xB6';
	char mixedSingleHorizontalDoubleVerticalRight = '\xC7';
};

constexpr BentoFrameGlyphs kBentoFrameGlyphs;

void normalizeScrollBarTrackGlyph(TScrollBar *scrollBar) noexcept {
	if (scrollBar == nullptr) return;
	scrollBar->chars[4] = scrollBar->chars[2];
}

bool splitCommandTargetsSecondaryPane(ushort command) noexcept {
	switch (command) {
		case cmClose:
		case cmMrOtherBuildCurrentFile:
		case cmMrOtherStopProgram:
		case cmMrOtherRestartProgram:
		case cmMrOtherClearOutput:
		case cmMrOtherFindNextCompilerError:
		case cmMrOtherFindPreviousCompilerError:
			return false;
		default:
			return true;
	}
}

bool splitEventTargetsSecondaryPane(const TEvent &event) noexcept {
	if ((event.what & (evMouseDown | evMouseMove | evMouseUp | evMouseAuto | evMouseWheel | evKeyDown)) != 0) return true;
	if (event.what == evCommand) return splitCommandTargetsSecondaryPane(event.message.command);
	return false;
}

}

class MRBentoPaneFrameView : public TView {
  public:
	enum HitKind {
		hitNone = 0,
		hitTitle,
		hitClose,
		hitMaximize
	};

	explicit MRBentoPaneFrameView(const TRect &bounds) noexcept : TView(bounds), leafId(-1), focused(false), source(false), maximized(false), roleListTitleOpen(false), roleListTitleX(0), roleListTitleWidth(0), title("Pane") {
		eventMask = evMouseDown;
	}

	void setPane(int nextLeafId, const char *nextTitle, bool isSource, bool isFocused, bool isMaximized) {
		std::string newTitle = nextTitle != nullptr && *nextTitle != '\0' ? nextTitle : "Pane";
		if (leafId == nextLeafId && title == newTitle && source == isSource && focused == isFocused && maximized == isMaximized) return;
		leafId = nextLeafId;
		title = newTitle;
		source = isSource;
		focused = isFocused;
		maximized = isMaximized;
	}

	int paneLeafId() const noexcept {
		return leafId;
	}

	TRect paneRoleListAnchor(int listWidth) const noexcept {
		Layout layout = paneChromeLayout(false);
		const int rightBracketX = std::max(1, layout.titleX + layout.titleWidth - 1);
		const int left = std::max(1, rightBracketX - std::max(1, listWidth));

		return TRect(left, 0, rightBracketX, 0);
	}

	void setPaneRoleListTitleOpen(bool open, const TRect &listAnchor) noexcept {
		const int titleX = std::max(0, static_cast<int>(listAnchor.a.x) - 1);
		const int titleWidth = std::max(2, static_cast<int>(listAnchor.b.x - listAnchor.a.x) + 2);

		if (roleListTitleOpen == open && roleListTitleX == titleX && roleListTitleWidth == titleWidth) return;
		roleListTitleOpen = open;
		roleListTitleX = titleX;
		roleListTitleWidth = titleWidth;
	}

	HitKind hitTest(TPoint local) const {
		if (local.y != 0) return hitNone;
		Layout layout = paneChromeLayout();

		if (local.x >= layout.closeX && local.x < layout.closeX + layout.closeWidth) return hitClose;
		if (local.x >= layout.maximizeX && local.x < layout.maximizeX + layout.maximizeWidth) return hitMaximize;
		if (local.x >= layout.titleX && local.x < layout.titleX + layout.titleWidth) return hitTitle;
		return hitNone;
	}

	virtual void draw() override {
		TDrawBuffer buffer;
		const TAttrPair frameColor = TAttrPair(owner != nullptr ? owner->mapColor(focused ? 13 : 1) : mapColor(1));
		const Layout layout = paneChromeLayout();

		for (int y = 0; y < size.y; ++y) {
			if (size.x > 0) {
				if (y == 0 || y == size.y - 1) {
					buffer.moveChar(0, kBentoFrameGlyphs.singleHorizontal, frameColor, size.x);
					buffer.putChar(0, y == 0 ? '\xDA' : '\xC0');
					if (size.x > 1) buffer.putChar(static_cast<ushort>(size.x - 1), y == 0 ? '\xBF' : '\xD9');
					if (y == 0) drawPaneChrome(buffer, layout, frameColor, frameColor, focused, maximized);
					writeBuf(0, y, size.x, 1, buffer);
				} else {
					buffer.moveChar(0, kBentoFrameGlyphs.singleVertical, frameColor, 1);
					writeBuf(0, y, 1, 1, buffer);
					if (size.x > 1) {
						buffer.moveChar(0, kBentoFrameGlyphs.singleVertical, frameColor, 1);
						writeBuf(size.x - 1, y, 1, 1, buffer);
					}
				}
			}
		}
	}

	virtual TPalette &getPalette() const override {
		static TPalette palette("\x06\x05\x04\x02", 4);
		return palette;
	}

  private:
	struct Layout {
		std::string title;
		int titleX;
		int titleWidth;
		int maximizeX;
		int maximizeWidth;
		int closeX;
		int closeWidth;

		Layout() noexcept : title(), titleX(0), titleWidth(0), maximizeX(0), maximizeWidth(0), closeX(0), closeWidth(0) {
		}
	};

	Layout paneChromeLayout(bool includeRoleListSpan = true) const {
		Layout layout;
		const int left = 0;
		const int right = std::max(0, size.x);
		const int closeLeftX = left + kPaneChromeFrameRest;
		const int maximizeRightX = right - kPaneChromeFrameRest;
		const bool withControls = focused;
		const int leftContentX = closeLeftX + (withControls ? kPaneCloseButtonWidth + kPaneChromeGap : 0);

		layout.title = "[" + title + "]";
		layout.closeWidth = withControls && right >= closeLeftX + kPaneCloseButtonWidth ? kPaneCloseButtonWidth : 0;
		layout.closeX = layout.closeWidth > 0 ? closeLeftX : 0;
		layout.maximizeWidth = withControls && maximizeRightX >= left + kPaneMaximizeButtonWidth ? kPaneMaximizeButtonWidth : 0;
		layout.maximizeX = layout.maximizeWidth > 0 ? maximizeRightX - kPaneMaximizeButtonWidth : size.x;

		const int titleRightX = layout.maximizeWidth > 0 ? layout.maximizeX - kPaneChromeGap : std::max(0, size.x - kPaneChromeFrameRest);
		const int boundedTitleRightX = std::min(titleRightX, std::max(left, right - kPaneChromeFrameRest));
		const int titleAvailable = std::max(0, boundedTitleRightX - leftContentX);
		layout.titleWidth = std::min(static_cast<int>(layout.title.size()), titleAvailable);
		layout.titleX = boundedTitleRightX - layout.titleWidth;
		if (withControls && includeRoleListSpan && roleListTitleOpen) {
			layout.titleX = std::clamp(roleListTitleX, leftContentX, std::max(leftContentX, titleRightX - 2));
			layout.titleWidth = std::clamp(roleListTitleWidth, 2, std::max(2, titleRightX - layout.titleX));
			layout.title.assign(static_cast<std::size_t>(layout.titleWidth), ' ');
			layout.title.front() = '[';
			layout.title.back() = ']';
		}
		return layout;
	}

	void drawPaneChrome(TDrawBuffer &buffer, const Layout &layout, TAttrPair frameColor, TAttrPair titleColor, bool withControls, bool maximized) const {
		if (withControls && layout.closeWidth > 0) buffer.moveStr(static_cast<ushort>(layout.closeX), kPaneCloseIcon, frameColor, layout.closeWidth);
		if (layout.titleWidth > 0) buffer.moveStr(static_cast<ushort>(layout.titleX), layout.title.c_str(), titleColor, layout.titleWidth);
		if (withControls && layout.maximizeWidth > 0) buffer.moveStr(static_cast<ushort>(layout.maximizeX), maximized ? kPaneRestoreIcon : kPaneMaximizeIcon, frameColor, layout.maximizeWidth);
	}

	int leafId;
	bool focused;
	bool source;
	bool maximized;
	bool roleListTitleOpen;
	int roleListTitleX;
	int roleListTitleWidth;
	std::string title;
};

MRBentoPaneSpec::MRBentoPaneSpec() noexcept : role(bprCompilerOutput), bufferPolicy(bpbOwnBuffer), readOnly(true), suppressMiniMap(true), suppressWordWrap(true), scrollBarsAlwaysVisible(true), titleMenu(&kBentoRoleTitleMenu) {
}

MRBentoPaneSpec::MRBentoPaneSpec(MRBentoPaneRole aRole, MRBentoPaneBufferPolicy aBufferPolicy, bool aReadOnly, bool aSuppressMiniMap, bool aSuppressWordWrap, bool aScrollBarsAlwaysVisible, const MRBentoPaneTitleMenuSpec *aTitleMenu) noexcept
    : role(aRole), bufferPolicy(aBufferPolicy), readOnly(aReadOnly), suppressMiniMap(aSuppressMiniMap), suppressWordWrap(aSuppressWordWrap), scrollBarsAlwaysVisible(aScrollBarsAlwaysVisible), titleMenu(aTitleMenu) {
}
MRBentoBox::BentoLayoutNode::BentoLayoutNode() noexcept : kind(blnPane), orientation(bsoHorizontal), dividerPosition(0), firstChild(-1), secondChild(-1), leafId(-1) {
}

MRBentoBox::BentoLeaf::BentoLeaf() noexcept : id(-1), role(bprCompilerOutput), spec(), title(), pane(nullptr), bounds(0, 0, 0, 0), visible(false) {
}
void MRBentoBox::showSecondaryPane() noexcept {
	if (firstToolLeafId() < 0) {
		int sourceNode = nodeIndexForLeaf(0);
		if (sourceNode >= 0) static_cast<void>(splitLeafNode(0, bsoHorizontal, bprCompilerOutput));
	}
	secondaryPaneVisible = firstToolLeafId() >= 0;
	layoutSplitPanes();
}

bool MRBentoBox::ensureBuildDiagnosticsPanes(MREditWindow *&outputWindow, MREditWindow *&problemsWindow) {
	const int previousActiveLeaf = activeLeafId;
	int outputLeaf = leafIdForRole(bprCompilerOutput);

	if (bentoMode == bbmDocumentViewports) {
		bentoMode = bbmToolWorkspace;
		for (BentoLeaf &leaf : leaves) {
			leaf.spec = paneSpecForRole(leaf.role);
			leaf.title = bentoPaneRoleTitle(leaf.role);
			if (leaf.pane != nullptr) leaf.pane->setPaneSpec(leaf.spec, getEditor());
		}
	}
	if (outputLeaf < 0) {
		if (!hasPaneSplit()) showSecondaryPane();
		outputLeaf = leafIdForRole(bprCompilerOutput);
		if (outputLeaf < 0) outputLeaf = splitLeafNode(0, bsoHorizontal, bprCompilerOutput);
	}
	if (outputLeaf < 0) return false;

	int problemsLeaf = leafIdForRole(bprProblems);
	if (problemsLeaf < 0) problemsLeaf = splitLeafNode(outputLeaf, bsoVertical, bprProblems);
	if (previousActiveLeaf >= 0 && nodeIndexForLeaf(previousActiveLeaf) >= 0) setActivePane(previousActiveLeaf);
	outputWindow = buildOutputPane();
	problemsWindow = problemsPane();
	return outputWindow != nullptr && problemsWindow != nullptr;
}

void MRBentoBox::activatePrimaryPane() noexcept {
	setActivePane(0);
}

void MRBentoBox::activateSecondaryPane() noexcept {
	if (firstToolLeafId() < 0) showSecondaryPane();
	int toolLeaf = firstToolLeafId();
	if (toolLeaf >= 0) setActivePane(toolLeaf);
}

void MRBentoBox::toggleActivePane() noexcept {
	if (leaves.empty()) return;
	int currentIndex = -1;
	for (std::size_t i = 0; i < leaves.size(); ++i)
		if (leaves[i].visible && leaves[i].id == activeLeafId) currentIndex = static_cast<int>(i);
	for (std::size_t step = 1; step <= leaves.size(); ++step) {
		const std::size_t next = static_cast<std::size_t>((std::max(0, currentIndex) + static_cast<int>(step)) % static_cast<int>(leaves.size()));
		if (leaves[next].visible) {
			setActivePane(leaves[next].id);
			return;
		}
	}
}
bool MRBentoBox::placePaneRole(MRBentoPaneRole role, MRBentoPanePlacement placement) {
	return placePaneRoleInContext(role, placement, activeLeafId);
}

TColorAttr MRBentoBox::mapColor(uchar index) {
	if (bentoMode == bbmFileCompare && index == 1) {
		unsigned char value = 0;

		if (configuredColorSlotOverride(kMrPaletteFileCompareBentoBorder, value)) return static_cast<TColorAttr>(value);
	}
	if (bentoMode == bbmFileCompare && index == 13) {
		unsigned char value = 0;

		if (configuredColorSlotOverride(kMrPaletteFileCompareFocusedPaneBorder, value)) return static_cast<TColorAttr>(value);
	}
	if (sourceScrollBarPaletteActive && (index == 4 || index == 5)) {
		if (bentoMode == bbmFileCompare) {
			MRFileEditor *sourceEditor = getEditor();

			if (sourceEditor != nullptr) return sourceEditor->editorTextFillColor();
		}
		return MREditWindow::mapColor(activeLeafId == 0 ? 13 : 1);
	}
	return MREditWindow::mapColor(index);
}

bool MRBentoBox::allowsDocumentViewportSplit() const noexcept {
	return bentoMode == bbmDocumentViewports;
}

MREditWindow *MRBentoBox::editorCommandTarget() noexcept {
	MRPaneEditWindow *pane = activeLeafId != 0 ? paneWindowForLeaf(activeLeafId) : nullptr;

	return pane != nullptr ? static_cast<MREditWindow *>(pane) : static_cast<MREditWindow *>(this);
}

const MREditWindow *MRBentoBox::editorCommandTarget() const noexcept {
	const MRPaneEditWindow *pane = activeLeafId != 0 ? paneWindowForLeaf(activeLeafId) : nullptr;

	return pane != nullptr ? static_cast<const MREditWindow *>(pane) : static_cast<const MREditWindow *>(this);
}

bool MRBentoBox::activatePaneWindow(MREditWindow *pane) noexcept {
	if (pane == nullptr) return false;
	if (pane == this) {
		setActivePane(0);
		return mrActivateEditWindow(this);
	}
	for (const BentoLeaf &leaf : leaves) {
		if (!leaf.visible || leaf.pane != pane) continue;
		setActivePane(leaf.id);
		return mrActivateEditWindow(this);
	}
	return false;
}

bool MRBentoBox::showsFrameGrowHandle() const noexcept {
	return false;
}

bool MRBentoBox::placePaneRoleInContext(MRBentoPaneRole role, MRBentoPanePlacement placement, int targetLeafId) {
	MRPaneEditWindow *targetPane = paneWindowForLeaf(targetLeafId);
	const MRBentoPaneSpec spec = paneSpecForRole(role);
	const MRBentoPaneRole previousRole = roleForLeaf(targetLeafId);

	if (bentoMode == bbmFileCompare && !bentoRoleIsDiff(role)) return false;
	if (targetLeafId == 0 && placement == bppReplace && bentoMode != bbmFileCompare) return false;
	switch (placement) {
		case bppReplace:
			if (targetLeafId != 0 && targetPane == nullptr) return false;
			if (targetPane != nullptr) targetPane->setPaneSpec(spec, getEditor());
			for (BentoLeaf &leaf : leaves) {
				if (leaf.id == targetLeafId) {
					leaf.role = role;
					leaf.spec = spec;
					leaf.title = bentoPaneRoleTitle(role);
				}
			}
			if (bentoMode == bbmFileCompare && bentoRoleIsDiff(role)) {
				refreshFileComparePanes();
			} else if (!bentoRoleIsOutline(role)) {
				static_cast<void>(targetPane->replaceTextBuffer("", bentoPaneRoleTitle(role)));
				targetPane->setReadOnly(spec.readOnly);
				targetPane->setFileChanged(false);
			}
			if (previousRole != role && bentoRoleIsOutline(previousRole)) {
				bool previousRoleStillVisible = false;
				for (const BentoLeaf &leaf : leaves)
					if (leaf.visible && leaf.id != targetLeafId && leaf.role == previousRole) previousRoleStillVisible = true;
				if (!previousRoleStillVisible) {
					if (previousRole == bprStructure) {
						structureOutlineStatus.clear();
						structureOutlineEntries.clear();
						structureOutlineState = MRBentoOutlinePaneState();
					} else if (previousRole == bprFunctions) {
						functionsOutlineStatus.clear();
						functionsOutlineEntries.clear();
						functionsOutlineState = MRBentoOutlinePaneState();
					}
				}
			}
			setActivePane(targetLeafId);
			layoutSplitPanes();
			if (bentoRoleIsOutline(role)) refreshOutlinePanes(true);
			if (bentoMode == bbmFileCompare) refreshFileComparePanes();
			mrMarkWorkspaceAutosaveDirty();
			return true;
		case bppSplitRight:
			if (!bentoRoleIsOutline(role)) {
				if (bentoMode == bbmFileCompare) {
					const bool ok = splitLeafNode(targetLeafId, bsoVertical, spec) >= 0;
					if (ok) refreshFileComparePanes();
					if (ok) mrMarkWorkspaceAutosaveDirty();
					return ok;
				}
				const bool ok = splitLeafNode(targetLeafId, bsoVertical, spec) >= 0;
				if (ok) mrMarkWorkspaceAutosaveDirty();
				return ok;
			}
			if (splitLeafNode(targetLeafId, bsoVertical, spec) < 0) return false;
			refreshOutlinePanes(true);
			mrMarkWorkspaceAutosaveDirty();
			return true;
		case bppSplitDown:
			if (!bentoRoleIsOutline(role)) {
				if (bentoMode == bbmFileCompare) {
					const bool ok = splitLeafNode(targetLeafId, bsoHorizontal, spec) >= 0;
					if (ok) refreshFileComparePanes();
					if (ok) mrMarkWorkspaceAutosaveDirty();
					return ok;
				}
				const bool ok = splitLeafNode(targetLeafId, bsoHorizontal, spec) >= 0;
				if (ok) mrMarkWorkspaceAutosaveDirty();
				return ok;
			}
			if (splitLeafNode(targetLeafId, bsoHorizontal, spec) < 0) return false;
			refreshOutlinePanes(true);
			mrMarkWorkspaceAutosaveDirty();
			return true;
		default:
			return false;
	}
}

bool MRBentoBox::initializeFileCompare(const MRBentoCompareSetup &setup) {
	if (bentoMode != bbmFileCompare) return false;

	const bool compareFirst = configuredFileCompareStartConfiguration() == MRFileCompareStartConfiguration::CompareOriginal;
	const MRBentoPaneRole primaryRole = compareFirst ? bprDiffCompare : bprDiffOriginal;
	const MRBentoPaneRole secondaryRole = compareFirst ? bprDiffOriginal : bprDiffCompare;
	fileCompareSetup = setup;
	fileCompareHunks.clear();
	fileCompareChangeGroups.clear();
	fileCompareTaskId = 0;
	fileCompareSourcesRestored = false;
	fileCompareDiffReady = false;
	fileCompareStale = false;

	for (BentoLeaf &leaf : leaves) {
		if (leaf.id == 0) {
			leaf.role = primaryRole;
			leaf.spec = paneSpecForRole(primaryRole);
			leaf.title = bentoPaneRoleTitle(primaryRole);
		}
	}
	if (!loadTextBuffer(fileCompareTextForRole(primaryRole, nullptr).c_str(), bentoPaneRoleTitle(primaryRole))) return false;
	setReadOnly(true);
	setFileChanged(false);
	if (leafIdForRole(secondaryRole) < 0) {
		if (splitLeafNode(0, bsoVertical, secondaryRole) < 0) return false;
	}
	refreshFileComparePanes();
	hideFileCompareSourceWindow(fileCompareSetup.original);
	hideFileCompareSourceWindow(fileCompareSetup.compare);
	static_cast<void>(mrActivateEditWindow(this));
	return true;
}

bool MRBentoBox::isFileCompareBox() const noexcept {
	return bentoMode == bbmFileCompare;
}

bool MRBentoBox::fileCompareWorkspaceSourcePaths(std::string &originalPath, std::string &comparePath) const {
	MREditWindow *originalWindow = nullptr;
	MREditWindow *compareWindow = nullptr;
	MRFileEditor *originalEditor = nullptr;
	MRFileEditor *compareEditor = nullptr;

	originalPath.clear();
	comparePath.clear();
	if (bentoMode != bbmFileCompare) return false;

	originalWindow = findEditWindowByBufferId(fileCompareSetup.original.bufferId);
	compareWindow = findEditWindowByBufferId(fileCompareSetup.compare.bufferId);
	originalEditor = originalWindow != nullptr ? originalWindow->getEditor() : nullptr;
	compareEditor = compareWindow != nullptr ? compareWindow->getEditor() : nullptr;
	if (originalEditor == nullptr || compareEditor == nullptr) return false;
	originalPath = originalEditor->persistentFileName();
	comparePath = compareEditor->persistentFileName();
	return !originalPath.empty() && !comparePath.empty();
}

bool MRBentoBox::containsFileCompareSourceWindow(const MREditWindow *window) const noexcept {
	if (bentoMode != bbmFileCompare || window == nullptr) return false;
	return window->bufferId() == fileCompareSetup.original.bufferId || window->bufferId() == fileCompareSetup.compare.bufferId;
}

bool MRBentoBox::refreshFileCompareAfterEditorMutation(const MREditWindow *window) {
	if (bentoMode != bbmFileCompare || window == nullptr || !fileComparePanesEditable()) return false;
	for (const BentoLeaf &leaf : leaves) {
		if (!leaf.visible || !bentoRoleIsDiff(leaf.role)) continue;
		if ((leaf.id == 0 && window == this) || (leaf.id != 0 && window == leaf.pane)) {
			refreshFileCompareAfterSourceMutation();
			return true;
		}
	}
	return false;
}

bool MRBentoBox::fileComparePanesEditable() const noexcept {
	return bentoMode == bbmFileCompare && !configuredFileCompareComparePanelReadOnly();
}

void MRBentoBox::refreshFileCompareAfterSourceMutation() {
	if (!fileComparePanesEditable()) return;
	MREditWindow *originalWindow = findEditWindowByBufferId(fileCompareSetup.original.bufferId);
	MREditWindow *compareWindow = findEditWindowByBufferId(fileCompareSetup.compare.bufferId);
	MRFileEditor *originalEditor = originalWindow != nullptr ? originalWindow->getEditor() : nullptr;
	MRFileEditor *compareEditor = compareWindow != nullptr ? compareWindow->getEditor() : nullptr;
	std::vector<std::string> originalLines;
	std::vector<std::string> compareLines;
	std::uint64_t taskId;

	if (originalWindow == nullptr || compareWindow == nullptr || originalEditor == nullptr || compareEditor == nullptr) return;

	if (fileCompareTaskId != 0) {
		static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(fileCompareTaskId));
		releaseCoprocessorTask(fileCompareTaskId);
		fileCompareTaskId = 0;
	}

	fileCompareSetup.original.window = originalWindow;
	fileCompareSetup.original.documentId = originalWindow->documentId();
	fileCompareSetup.original.version = originalWindow->documentVersion();
	if (const char *title = originalWindow->getTitle(0); title != nullptr && *title != '\0') fileCompareSetup.original.title = title;
	fileCompareSetup.original.text = originalEditor->snapshotText();

	fileCompareSetup.compare.window = compareWindow;
	fileCompareSetup.compare.documentId = compareWindow->documentId();
	fileCompareSetup.compare.version = compareWindow->documentVersion();
	if (const char *title = compareWindow->getTitle(0); title != nullptr && *title != '\0') fileCompareSetup.compare.title = title;
	fileCompareSetup.compare.text = compareEditor->snapshotText();

	mr::diff::mrSplitTextLinesForDiff(fileCompareSetup.original.text, originalLines);
	mr::diff::mrSplitTextLinesForDiff(fileCompareSetup.compare.text, compareLines);
	fileCompareHunks.clear();
	fileCompareChangeGroups.clear();
	fileCompareDiffReady = false;
	fileCompareStale = false;
	taskId = mr::coprocessor::globalCoprocessor().submit(mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::FileCompare, fileCompareSetup.original.documentId, fileCompareSetup.original.version, "file compare", [originalLines, compareLines, originalDocumentId = fileCompareSetup.original.documentId, originalVersion = fileCompareSetup.original.version, compareDocumentId = fileCompareSetup.compare.documentId, compareVersion = fileCompareSetup.compare.version](const mr::coprocessor::TaskInfo &task, std::stop_token stopToken) {
		mr::coprocessor::Result result;
		std::vector<mr::diff::MRDiffHunk> hunks;
		std::string errorText;

		result.task = task;
		if (!mr::diff::mrComputeMyersDiff(originalLines, compareLines, hunks, &errorText, stopToken)) {
			result.status = stopToken.stop_requested() ? mr::coprocessor::TaskStatus::Cancelled : mr::coprocessor::TaskStatus::Failed;
			result.error = errorText;
			return result;
		}
		result.status = mr::coprocessor::TaskStatus::Completed;
		result.payload = std::make_shared<mr::coprocessor::FileComparePayload>(originalDocumentId, originalVersion, compareDocumentId, compareVersion, originalLines.size(), compareLines.size(), std::move(hunks));
		return result;
	});
	if (taskId != 0) {
		setFileCompareTask(taskId);
		trackCoprocessorTask(taskId, mr::coprocessor::TaskKind::FileCompare, "file compare");
	}
	refreshFileComparePanes();
	bentoProjectionDirty |= bpdContent | bpdChrome | bpdScrollBar | bpdOverlay;
}

void MRBentoBox::refreshFileCompareConfiguration() {
	if (bentoMode != bbmFileCompare) return;
	refreshFileComparePanes();
}

MRBentoWorkspaceSnapshot MRBentoBox::workspaceSnapshot() const {
	MRBentoWorkspaceSnapshot snapshot;

	snapshot.mode = bentoMode;
	snapshot.rootNode = rootNode;
	snapshot.activeLeafId = activeLeafId;
	snapshot.maximizedLeafId = maximizedLeafId;
	for (const BentoLayoutNode &node : layoutTree) {
		MRBentoWorkspaceNode workspaceNode;
		workspaceNode.kind = static_cast<int>(node.kind);
		workspaceNode.orientation = static_cast<int>(node.orientation);
		workspaceNode.dividerPosition = node.dividerPosition;
		workspaceNode.firstChild = node.firstChild;
		workspaceNode.secondChild = node.secondChild;
		workspaceNode.leafId = node.leafId;
		snapshot.nodes.push_back(workspaceNode);
	}
	for (const BentoLeaf &leaf : leaves) {
		MRBentoWorkspaceLeaf workspaceLeaf;
		workspaceLeaf.id = leaf.id;
		workspaceLeaf.role = leaf.role;
		workspaceLeaf.visible = leaf.visible;
		snapshot.leaves.push_back(workspaceLeaf);
	}
	return snapshot;
}

bool MRBentoBox::restoreWorkspaceSnapshot(const MRBentoWorkspaceSnapshot &snapshot) {
	bool hasSourceLeaf = false;
	int nextId = 0;

	if (!bentoWorkspaceModeIsValid(static_cast<int>(snapshot.mode))) return false;
	if (snapshot.nodes.empty() || snapshot.leaves.empty()) return false;
	if (snapshot.rootNode < 0 || snapshot.rootNode >= static_cast<int>(snapshot.nodes.size())) return false;

	for (const MRBentoWorkspaceLeaf &leaf : snapshot.leaves) {
		if (leaf.id < 0 || !bentoWorkspaceRoleIsValid(static_cast<int>(leaf.role))) return false;
		if (leaf.id == 0) hasSourceLeaf = true;
		for (const MRBentoWorkspaceLeaf &other : snapshot.leaves)
			if (&leaf != &other && leaf.id == other.id) return false;
		if (snapshot.mode == bbmFileCompare) {
			if (!bentoRoleIsDiff(leaf.role)) return false;
		} else if (bentoRoleIsDiff(leaf.role))
			return false;
		nextId = std::max(nextId, leaf.id + 1);
	}
	if (!hasSourceLeaf) return false;

	for (std::size_t i = 0; i < snapshot.nodes.size(); ++i) {
		const MRBentoWorkspaceNode &node = snapshot.nodes[i];
		if (node.kind != blnPane && node.kind != blnSplit) return false;
		if (node.orientation != bsoHorizontal && node.orientation != bsoVertical) return false;
		if (node.kind == blnPane) {
			bool leafFound = false;
			for (const MRBentoWorkspaceLeaf &leaf : snapshot.leaves)
				if (leaf.id == node.leafId) leafFound = true;
			if (!leafFound) return false;
		} else {
			if (node.firstChild < 0 || node.firstChild >= static_cast<int>(snapshot.nodes.size())) return false;
			if (node.secondChild < 0 || node.secondChild >= static_cast<int>(snapshot.nodes.size())) return false;
			if (node.firstChild == static_cast<int>(i) || node.secondChild == static_cast<int>(i)) return false;
		}
	}
	{
		std::vector<bool> visited(snapshot.nodes.size(), false);
		std::vector<int> stack;
		stack.push_back(snapshot.rootNode);
		while (!stack.empty()) {
			const int nodeIndex = stack.back();
			stack.pop_back();
			if (nodeIndex < 0 || nodeIndex >= static_cast<int>(snapshot.nodes.size())) return false;
			if (visited[static_cast<std::size_t>(nodeIndex)]) return false;
			visited[static_cast<std::size_t>(nodeIndex)] = true;
			const MRBentoWorkspaceNode &node = snapshot.nodes[static_cast<std::size_t>(nodeIndex)];
			if (node.kind == blnSplit) {
				stack.push_back(node.secondChild);
				stack.push_back(node.firstChild);
			}
		}
	}

	for (BentoLeaf &leaf : leaves) {
		if (leaf.pane != nullptr) {
			remove(leaf.pane);
			TObject::destroy(leaf.pane);
			leaf.pane = nullptr;
		}
	}
	layoutTree.clear();
	leaves.clear();
	secondaryPane = nullptr;
	bentoMode = snapshot.mode;

	for (const MRBentoWorkspaceNode &workspaceNode : snapshot.nodes) {
		BentoLayoutNode node;
		node.kind = static_cast<BentoLayoutNodeKind>(workspaceNode.kind);
		node.orientation = static_cast<BentoSplitOrientation>(workspaceNode.orientation);
		node.dividerPosition = workspaceNode.dividerPosition;
		node.firstChild = workspaceNode.firstChild;
		node.secondChild = workspaceNode.secondChild;
		node.leafId = workspaceNode.leafId;
		layoutTree.push_back(node);
	}
	for (const MRBentoWorkspaceLeaf &workspaceLeaf : snapshot.leaves) {
		BentoLeaf leaf;
		leaf.id = workspaceLeaf.id;
		leaf.role = workspaceLeaf.role;
		leaf.spec = paneSpecForRole(workspaceLeaf.role);
		leaf.title = bentoMode == bbmDocumentViewports ? "" : bentoPaneRoleTitle(workspaceLeaf.role);
		leaf.visible = workspaceLeaf.visible;
		if (leaf.id != 0) {
			const std::string initialTitle = bentoMode == bbmDocumentViewports ? "Viewport" : leaf.title;
			leaf.pane = new MRPaneEditWindow(TRect(0, 0, 1, 1), initialTitle.c_str(), number);
			leaf.pane->setPaneSpec(leaf.spec, getEditor());
			insert(leaf.pane);
			if (secondaryPane == nullptr && leaf.visible) secondaryPane = leaf.pane;
		}
		leaves.push_back(leaf);
	}

	rootNode = snapshot.rootNode;
	nextLeafId = nextId;
	activeLeafId = nodeIndexForLeaf(snapshot.activeLeafId) >= 0 ? snapshot.activeLeafId : 0;
	maximizedLeafId = nodeIndexForLeaf(snapshot.maximizedLeafId) >= 0 ? snapshot.maximizedLeafId : -1;
	sourceScrollBarPaletteActive = false;
	secondaryPaneVisible = firstToolLeafId() >= 0;
	paneRoleDropList.hide();
	paneActionDropList.hide();
	layoutSplitPanes();
	refreshOutlinePanes(true);
	return true;
}

void MRBentoBox::setFileCompareTask(std::uint64_t taskId) noexcept {
	fileCompareTaskId = taskId;
}

void MRBentoBox::restoreFileCompareSources() noexcept {
	if (bentoMode != bbmFileCompare || fileCompareSourcesRestored) return;
	fileCompareSourcesRestored = true;

	MREditWindow *originalWindow = findEditWindowByBufferId(fileCompareSetup.original.bufferId);
	if (originalWindow != nullptr) {
		setWindowManuallyHidden(originalWindow, fileCompareSetup.original.wasManuallyHidden);
		if (fileCompareSetup.original.wasVisible && !fileCompareSetup.original.wasManuallyHidden) originalWindow->show();
	}
	MREditWindow *compareWindow = findEditWindowByBufferId(fileCompareSetup.compare.bufferId);
	if (compareWindow != nullptr) {
		setWindowManuallyHidden(compareWindow, fileCompareSetup.compare.wasManuallyHidden);
		if (fileCompareSetup.compare.wasVisible && !fileCompareSetup.compare.wasManuallyHidden) compareWindow->show();
	}
}

bool MRBentoBox::splitActiveEditorPane(MRBentoPanePlacement placement) {
	MRBentoPaneSpec spec = paneSpecForRole(bprSplitEditor);

	switch (placement) {
		case bppSplitRight: {
			const bool ok = splitLeafNode(activeLeafId, bsoVertical, spec) >= 0;
			if (ok) mrMarkWorkspaceAutosaveDirty();
			return ok;
		}
		case bppSplitDown: {
			const bool ok = splitLeafNode(activeLeafId, bsoHorizontal, spec) >= 0;
			if (ok) mrMarkWorkspaceAutosaveDirty();
			return ok;
		}
		default:
			return false;
	}
}

void MRBentoBox::draw() {
	refreshEditorTaskMarkers();
	MREditWindow::draw();
	if (hasPaneSplit()) {
		drawSourcePaneScrollBars();
		drawPaneFrames();
	}
}

void MRBentoBox::changeBounds(const TRect &bounds) {
	paneRoleDropList.hide();
	paneActionDropList.hide();
	fileCompareActionDropList.hide();
	updatePaneRoleListChrome();
	MREditWindow::changeBounds(bounds);
	if (hasPaneSplit()) layoutSplitPanes();
}

void MRBentoBox::close() {
	restoreFileCompareSources();
	windowCloseInProgress = true;
	MREditWindow::close();
}

void MRBentoBox::shutDown() {
	restoreFileCompareSources();
	windowCloseInProgress = true;
	MREditWindow::shutDown();
}

void MRBentoBox::handleEvent(TEvent &event) {
	const bool mouseEvent = (event.what & (evMouseDown | evMouseMove | evMouseUp | evMouseAuto | evMouseWheel)) != 0;
	const TPoint localMouse = mouseEvent ? makeLocal(event.mouse.where) : TPoint();

	if (!hasPaneSplit()) {
		MRFileEditor *sourceEditor = getEditor();
		const bool trackSourceMutation = !compilerDiagnostics.empty() && sourceEditor != nullptr;
		MRTextBufferModel::ReadSnapshot oldSnapshot;
		if (trackSourceMutation) oldSnapshot = buffer().readSnapshot();
		MREditWindow::handleEvent(event);
		if (trackSourceMutation) syncCompilerDiagnosticsAfterSourceMutation(oldSnapshot, sourceEditor->lastDocumentChangeSet());
		return;
	}
	if (event.what == evBroadcast && event.message.command == cmUpdateTitle) {
		MREditWindow::handleEvent(event);
		refreshOutlinePanes(false);
		bentoProjectionDirty |= bpdContent | bpdChrome;
		flushBentoProjection();
		return;
	}
	if (event.what == evCommand && event.message.command == cmMrBentoPaneRoleAccepted) {
		acceptPaneRoleChoice();
		clearEvent(event);
		bentoProjectionDirty |= bpdChrome;
		flushBentoProjection();
		return;
	}
	if (event.what == evCommand && event.message.command == cmMrBentoPaneActionAccepted) {
		acceptPaneActionChoice();
		clearEvent(event);
		bentoProjectionDirty |= bpdChrome;
		flushBentoProjection();
		return;
	}
	if (event.what == evCommand && event.message.command == cmMrFileComparePaneActionAccepted) {
		acceptFileCompareActionChoice();
		clearEvent(event);
		bentoProjectionDirty |= bpdContent | bpdChrome | bpdScrollBar | bpdOverlay;
		flushBentoProjection();
		return;
	}
	if (event.what == evCommand && event.message.command == cmClose) {
		if ((flags & wfClose) != 0 && (event.message.infoPtr == nullptr || event.message.infoPtr == this)) {
			clearEvent(event);
			if ((state & sfModal) == 0)
				close();
			else {
				event.what = evCommand;
				event.message.command = cmCancel;
				putEvent(event);
				clearEvent(event);
			}
		}
		return;
	}
	if (handleOuterFrameCloseMouse(event)) {
		bentoProjectionDirty |= bpdChrome;
		flushBentoProjection();
		return;
	}
	if (handlePaneDropListEvent(event)) {
		bentoProjectionDirty |= bpdChrome;
		flushBentoProjection();
		return;
	}
	if (event.what == evKeyDown && TKey(event.keyDown.keyCode, event.keyDown.controlKeyState) == TKey(kbCtrlTab)) {
		toggleActivePane();
		clearEvent(event);
		bentoProjectionDirty |= bpdChrome;
		flushBentoProjection();
		return;
	}
	if (bentoMode == bbmFileCompare && event.what == evKeyDown && bentoRoleIsDiff(roleForLeaf(activeLeafId))) {
		const bool nextChangeKey = event.keyDown.keyCode == kbF8 && (event.keyDown.controlKeyState & kbShift) == 0;
		const bool previousChangeKey = event.keyDown.keyCode == kbShiftF8 || (event.keyDown.keyCode == kbF8 && (event.keyDown.controlKeyState & kbShift) != 0);

		if (nextChangeKey || previousChangeKey) {
			if (navigateFileCompareChange(nextChangeKey)) {
				clearEvent(event);
				return;
			}
		}
	}
	if (event.what == evKeyDown && compilerSidekickTracked && ctrlToArrow(event.keyDown.keyCode) == kbEsc) {
		clearTrackedCompilerSidekick(true);
		clearEvent(event);
		bentoProjectionDirty |= bpdContent | bpdChrome;
		flushBentoProjection();
		return;
	}
	if (event.what == evKeyDown && !compilerDiagnostics.empty()) {
		const bool nextProblemKey = event.keyDown.keyCode == kbF8 && (event.keyDown.controlKeyState & kbShift) == 0;
		const bool previousProblemKey = event.keyDown.keyCode == kbShiftF8 || (event.keyDown.keyCode == kbF8 && (event.keyDown.controlKeyState & kbShift) != 0);
		if (nextProblemKey || previousProblemKey) {
			MREditWindow *targetWindow = editorCommandTarget();
			if (mrHandleRuntimeKeymapEvent(event, targetWindow != nullptr && targetWindow->isReadOnly() ? MRKeymapContext::ReadOnly : MRKeymapContext::Edit, targetWindow)) return;
			static_cast<void>(nextProblemKey ? jumpToNextProblem() : jumpToPreviousProblem());
			clearEvent(event);
			bentoProjectionDirty |= bpdContent | bpdChrome | bpdOverlay;
			flushBentoProjection();
			return;
		}
	}
	if (event.what == evMouseDown) {
		if (handleDividerChromeMouse(event)) {
			clearEvent(event);
			bentoProjectionDirty |= bpdChrome;
			flushBentoProjection();
			return;
		}
		const int dividerNode = nodeAtDivider(localMouse);
		if (dividerNode >= 0 && (event.mouse.buttons & mbLeftButton) != 0) {
			dragDivider(event, dividerNode);
			clearEvent(event);
			bentoProjectionDirty |= bpdLayout;
			flushBentoProjection();
			return;
		}
		if (bentoMode == bbmFileCompare && (event.mouse.buttons & mbRightButton) != 0) {
			const int targetLeafId = leafAt(localMouse);
			if (bentoRoleIsDiff(roleForLeaf(targetLeafId)) && pointInRect(localMouse, contentBounds(paneBoundsForLeaf(targetLeafId)))) {
				showFileCompareActionList(event.mouse.where, targetLeafId);
				clearEvent(event);
				bentoProjectionDirty |= bpdChrome;
				flushBentoProjection();
				return;
			}
		}
	}
	if (bentoMode == bbmFileCompare && event.what == evMouseWheel) {
		const int wheelLeafId = leafAt(localMouse);
		const MRBentoPaneRole wheelRole = roleForLeaf(wheelLeafId);

		if (bentoRoleIsDiff(wheelRole)) {
			MREditWindow *wheelWindow = wheelLeafId == 0 ? static_cast<MREditWindow *>(this) : static_cast<MREditWindow *>(paneWindowForLeaf(wheelLeafId));
			MRFileEditor *wheelEditor = wheelWindow != nullptr ? wheelWindow->getEditor() : nullptr;

			if (wheelEditor != nullptr) {
				const int wheelStep = event.mouse.wheel == mwRight || event.mouse.wheel == mwDown ? 3 : -3;

				setActivePane(wheelLeafId);
				if (event.mouse.wheel == mwLeft || event.mouse.wheel == mwRight)
					wheelEditor->scrollTo(std::max(0, wheelEditor->delta.x + wheelStep), wheelEditor->delta.y);
				else
					wheelEditor->scrollTo(wheelEditor->delta.x, std::max(0, wheelEditor->delta.y + wheelStep));
				clearEvent(event);
				syncFileCompareLinkedPaneFrom(wheelLeafId, false);
				if (wheelLeafId != 0 && wheelWindow != nullptr) wheelWindow->drawView();
				bentoProjectionDirty |= bpdContent | bpdChrome | bpdScrollBar | bpdOverlay;
				flushBentoProjection();
				return;
			}
		}
	}
	if (mouseEvent) setActivePaneForMouse(event.mouse.where);
	if (activeLeafId != 0 && splitEventTargetsSecondaryPane(event)) {
		MRPaneEditWindow *targetPane = paneWindowForLeaf(activeLeafId);
		TRect targetBounds = paneBoundsForLeaf(activeLeafId);
		const MRBentoPaneRole activeRole = roleForLeaf(activeLeafId);
		const bool problemsPaneActive = activeRole == bprProblems;
		const bool outlinePaneActive = bentoRoleIsOutline(activeRole);
		const bool enterProblem = problemsPaneActive && event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter;
		const bool clickProblem = problemsPaneActive && event.what == evMouseDown && (event.mouse.buttons & mbLeftButton) != 0;
		const bool enterOutline = outlinePaneActive && event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter;
		const bool clickOutline = outlinePaneActive && event.what == evMouseDown && (event.mouse.buttons & mbLeftButton) != 0;
		if (mouseEvent && !pointInRect(localMouse, contentBounds(targetBounds))) {
			if (targetPane != nullptr) targetPane->handleEvent(event);
			if (bentoMode == bbmFileCompare && bentoRoleIsDiff(activeRole)) syncFileCompareLinkedPaneFrom(activeLeafId);
			bentoProjectionDirty |= bpdContent | bpdChrome;
			flushBentoProjection();
			return;
		}
		if (enterProblem) {
			static_cast<void>(jumpToProblemAtCursor());
			clearEvent(event);
			bentoProjectionDirty |= bpdContent | bpdChrome | bpdOverlay;
			flushBentoProjection();
			return;
		}
		if (enterOutline) {
			static_cast<void>(jumpToOutlineAtCursor(activeRole));
			clearEvent(event);
			bentoProjectionDirty |= bpdContent | bpdChrome | bpdOverlay;
			flushBentoProjection();
			return;
		}
		MRFileEditor *sourceEditor = getEditor();
		const bool trackSourceMutation = !compilerDiagnostics.empty() && sourceEditor != nullptr;
		MRTextBufferModel::ReadSnapshot oldSnapshot;
		TScrollBar *targetHorizontalScrollBar = targetPane != nullptr ? targetPane->horizontalEditorScrollBar() : nullptr;
		TScrollBar *targetVerticalScrollBar = targetPane != nullptr ? targetPane->verticalEditorScrollBar() : nullptr;
		const std::pair<bool, bool> targetRangeBefore = std::make_pair(targetHorizontalScrollBar != nullptr && targetHorizontalScrollBar->maxVal > targetHorizontalScrollBar->minVal, targetVerticalScrollBar != nullptr && targetVerticalScrollBar->maxVal > targetVerticalScrollBar->minVal);
		const bool trackFileCompareMutation = bentoMode == bbmFileCompare && fileComparePanesEditable() && bentoRoleIsDiff(activeRole);
		std::size_t fileCompareVersionBefore = 0;
		if (trackSourceMutation) oldSnapshot = buffer().readSnapshot();
		if (targetPane != nullptr) {
			MRFileEditor *targetEditor = targetPane->getEditor();
			if (trackFileCompareMutation && targetEditor != nullptr) fileCompareVersionBefore = targetEditor->documentVersion();
			if (event.what == evMouseWheel && targetEditor != nullptr) {
				const int wheelStep = event.mouse.wheel == mwRight || event.mouse.wheel == mwDown ? 3 : -3;
				if (event.mouse.wheel == mwLeft || event.mouse.wheel == mwRight)
					targetEditor->scrollTo(std::max(0, targetEditor->delta.x + wheelStep), targetEditor->delta.y);
				else
					targetEditor->scrollTo(targetEditor->delta.x, std::max(0, targetEditor->delta.y + wheelStep));
				clearEvent(event);
				bentoProjectionDirty |= bpdScrollBar;
			} else
				targetPane->handleEvent(event);
		}
		if (trackSourceMutation) syncCompilerDiagnosticsAfterSourceMutation(oldSnapshot, sourceEditor->lastDocumentChangeSet());
		if (clickProblem) {
			static_cast<void>(jumpToProblemAtCursor());
			clearEvent(event);
			bentoProjectionDirty |= bpdContent | bpdChrome | bpdOverlay;
			flushBentoProjection();
			return;
		}
		if (clickOutline) {
			static_cast<void>(jumpToOutlineAtCursor(activeRole));
			clearEvent(event);
			bentoProjectionDirty |= bpdContent | bpdChrome | bpdOverlay;
			flushBentoProjection();
			return;
		}
		targetHorizontalScrollBar = targetPane != nullptr ? targetPane->horizontalEditorScrollBar() : nullptr;
		targetVerticalScrollBar = targetPane != nullptr ? targetPane->verticalEditorScrollBar() : nullptr;
		const std::pair<bool, bool> targetRangeAfter = std::make_pair(targetHorizontalScrollBar != nullptr && targetHorizontalScrollBar->maxVal > targetHorizontalScrollBar->minVal, targetVerticalScrollBar != nullptr && targetVerticalScrollBar->maxVal > targetVerticalScrollBar->minVal);
		if (targetPane != nullptr && targetRangeAfter != targetRangeBefore) targetPane->layoutPaneChrome();
		if (trackFileCompareMutation && targetPane != nullptr) targetPane->setReadOnly(false);
		if (trackFileCompareMutation && targetPane != nullptr && targetPane->getEditor() != nullptr && targetPane->getEditor()->documentVersion() != fileCompareVersionBefore) refreshFileCompareAfterSourceMutation();
		if (bentoMode == bbmFileCompare && bentoRoleIsDiff(activeRole)) syncFileCompareLinkedPaneFrom(activeLeafId);
		bentoProjectionDirty |= bpdContent | bpdChrome;
		flushBentoProjection();
		return;
	}
	if (event.what == evMouseWheel && activeLeafId == 0 && getEditor() != nullptr && pointInRect(localMouse, contentBounds(paneBoundsForLeaf(0)))) {
		MRFileEditor *sourceEditor = getEditor();
		const int wheelStep = event.mouse.wheel == mwRight || event.mouse.wheel == mwDown ? 3 : -3;
		if (event.mouse.wheel == mwLeft || event.mouse.wheel == mwRight)
			sourceEditor->scrollTo(std::max(0, sourceEditor->delta.x + wheelStep), sourceEditor->delta.y);
		else
			sourceEditor->scrollTo(sourceEditor->delta.x, std::max(0, sourceEditor->delta.y + wheelStep));
		clearEvent(event);
		bentoProjectionDirty |= bpdScrollBar | bpdChrome | bpdOverlay;
		syncFileCompareLinkedPaneFrom(0);
		flushBentoProjection();
		return;
	}
	MRFileEditor *sourceEditor = getEditor();
	const bool trackSourceMutation = !compilerDiagnostics.empty() && sourceEditor != nullptr;
	const bool trackFileCompareMutation = bentoMode == bbmFileCompare && fileComparePanesEditable() && bentoRoleIsDiff(roleForLeaf(0)) && sourceEditor != nullptr;
	const std::size_t fileCompareVersionBefore = trackFileCompareMutation ? sourceEditor->documentVersion() : 0;
	TScrollBar *sourceHorizontalScrollBar = horizontalEditorScrollBar();
	TScrollBar *sourceVerticalScrollBar = verticalEditorScrollBar();
	const std::pair<bool, bool> sourceRangeBefore = std::make_pair(sourceHorizontalScrollBar != nullptr && sourceHorizontalScrollBar->maxVal > sourceHorizontalScrollBar->minVal, sourceVerticalScrollBar != nullptr && sourceVerticalScrollBar->maxVal > sourceVerticalScrollBar->minVal);
	MRTextBufferModel::ReadSnapshot oldSnapshot;
	if (trackSourceMutation) oldSnapshot = buffer().readSnapshot();
	MREditWindow::handleEvent(event);
	if (trackFileCompareMutation && sourceEditor->documentVersion() != fileCompareVersionBefore) refreshFileCompareAfterSourceMutation();
	syncFileCompareLinkedPaneFrom(0);
	if (trackSourceMutation) syncCompilerDiagnosticsAfterSourceMutation(oldSnapshot, sourceEditor->lastDocumentChangeSet());
	refreshOutlinePanes(false);
	sourceHorizontalScrollBar = horizontalEditorScrollBar();
	sourceVerticalScrollBar = verticalEditorScrollBar();
	const std::pair<bool, bool> sourceRangeAfter = std::make_pair(sourceHorizontalScrollBar != nullptr && sourceHorizontalScrollBar->maxVal > sourceHorizontalScrollBar->minVal, sourceVerticalScrollBar != nullptr && sourceVerticalScrollBar->maxVal > sourceVerticalScrollBar->minVal);
	if (sourceRangeAfter != sourceRangeBefore) {
		bentoProjectionDirty |= bpdLayout | bpdOverlay;
	}
	else {
		bentoProjectionDirty |= bpdContent | bpdChrome | bpdOverlay;
	}
	flushBentoProjection();
}

void MRBentoBox::setState(ushort aState, Boolean enable) {
	if (windowCloseInProgress) {
		TWindow::setState(aState, enable);
		return;
	}
	MREditWindow::setState(aState, enable);
	if ((aState & (sfActive | sfSelected)) != 0 && enable == False) mrDropSidekickForParent(this);
	if (hasPaneSplit() && (aState & (sfFocused | sfSelected | sfActive)) != 0) {
		updateActivePaneFrame();
		bentoProjectionDirty |= bpdChrome;
		flushBentoProjection();
	}
}

void MRBentoBox::initializeLayoutTree() noexcept {
	layoutTree.clear();
	leaves.clear();
	rootNode = -1;
	nextLeafId = 0;
	BentoLeaf source;
	source.id = nextLeafId++;
	source.role = bprSource;
	source.spec = paneSpecForRole(bprSource);
	source.title = bentoMode == bbmDocumentViewports ? "" : bentoPaneRoleTitle(bprSource);
	if (bentoMode == bbmFileCompare) {
		source.role = bprDiffOriginal;
		source.spec = paneSpecForRole(bprDiffOriginal);
		source.title = bentoPaneRoleTitle(bprDiffOriginal);
	}
	source.pane = nullptr;
	source.visible = true;
	leaves.push_back(source);
	rootNode = createLeafNode(source.id);
	activeLeafId = source.id;
	maximizedLeafId = -1;
	sourceScrollBarPaletteActive = false;
	secondaryPaneVisible = false;
}

void MRBentoBox::ensurePaneFrameViews() {
	while (paneFrameViews.size() < leaves.size()) {
		MRBentoPaneFrameView *view = new MRBentoPaneFrameView(TRect(0, 0, 0, 0));
		paneFrameViews.push_back(view);
	}
}

void MRBentoBox::layoutSplitPanes() {
	MRFileEditor *primaryEditor = getEditor();
	TRect inner = paneLayoutBounds();
	if (!hasPaneSplit()) {
		for (BentoLeaf &leaf : leaves) {
			leaf.visible = leaf.id == 0;
			if (leaf.id == 0) leaf.bounds = inner;
			if (leaf.pane != nullptr) leaf.pane->hide();
		}
		activeLeafId = 0;
		maximizedLeafId = -1;
		secondaryPaneVisible = false;
		sourceScrollBarPaletteActive = false;
		paneRoleDropList.hide();
		paneActionDropList.hide();
		if (primaryEditor != nullptr) primaryEditor->setScrollBarsAlwaysVisible(false);
		MREditWindow::changeBounds(getBounds());
		if (primaryEditor != nullptr) primaryEditor->drawView();
		bentoProjectionDirty = bpdNone;
		return;
	}
	ensurePaneFrameViews();
	for (BentoLeaf &leaf : leaves) leaf.visible = false;
	if (maximizedLeafId >= 0 && nodeIndexForLeaf(maximizedLeafId) >= 0) {
		for (BentoLeaf &leaf : leaves) {
			if (leaf.id == maximizedLeafId) {
				leaf.bounds = inner;
				leaf.visible = true;
				break;
			}
		}
	} else {
		maximizedLeafId = -1;
		if (rootNode >= 0) layoutNode(rootNode, inner);
	}
	secondaryPaneVisible = firstToolLeafId() >= 0;

	for (std::size_t i = 0; i < leaves.size(); ++i) {
		BentoLeaf &leaf = leaves[i];
		MRBentoPaneFrameView *view = i < paneFrameViews.size() ? paneFrameViews[i] : nullptr;
		if (leaf.visible) {
			const TRect content = contentBounds(leaf.bounds);
			if (leaf.id == 0) {
				layoutSourcePaneChrome(content);
			} else if (leaf.pane != nullptr) {
				leaf.pane->setPaneFocused(leaf.id == activeLeafId && (state & sfFocused) != 0);
				leaf.pane->show();
				leaf.pane->changeBounds(content);
			}
			if (view != nullptr) {
				view->changeBounds(leaf.bounds);
				view->setPane(leaf.id, paneTitleForLeaf(leaf).c_str(), leaf.id == 0 && bentoMode != bbmDocumentViewports, leaf.id == activeLeafId && (state & sfFocused) != 0, leaf.id == maximizedLeafId);
			}
		} else {
			if (leaf.id == 0) hideSourcePaneChrome();
			if (leaf.pane != nullptr) leaf.pane->hide();
		}
	}
	if (frame != nullptr) frame->drawView();
	if (primaryEditor != nullptr) {
		primaryEditor->drawView();
		if (leaves.front().visible) drawSourcePaneScrollBars();
	}
	for (BentoLeaf &leaf : leaves)
		if (leaf.visible && leaf.pane != nullptr) leaf.pane->drawView();
	drawPaneFrames();
	bentoProjectionDirty = bpdNone;
}

void MRBentoBox::flushBentoProjection() noexcept {
	if (windowCloseInProgress || bentoProjectionDirty == bpdNone) return;
	const unsigned dirty = bentoProjectionDirty;
	bentoProjectionDirty = bpdNone;

	if (!hasPaneSplit()) return;
	if ((dirty & bpdLayout) != 0) {
		layoutSplitPanes();
		if ((dirty & bpdOverlay) != 0) updateTrackedCompilerSidekick();
		paneRoleDropList.drawOpenList();
		paneActionDropList.drawOpenList();
		return;
	}
	if ((dirty & bpdContent) != 0) drawSharedEditorPanes();
	if ((dirty & bpdScrollBar) != 0) drawSourcePaneScrollBars();
	if ((dirty & bpdOverlay) != 0) updateTrackedCompilerSidekick();
	if ((dirty & (bpdChrome | bpdScrollBar)) != 0) drawPaneFrames();
	paneRoleDropList.drawOpenList();
	paneActionDropList.drawOpenList();
}

void MRBentoBox::layoutSourcePaneChrome(const TRect &content) noexcept {
	MRFileEditor *primaryEditor = getEditor();
	TScrollBar *horizontalScrollBar = horizontalEditorScrollBar();
	TScrollBar *verticalScrollBar = verticalEditorScrollBar();
	MRIndicator *sourceIndicator = editorIndicator();
	TRect editorBounds = content;
	const bool showWithoutRange = configuredScrollbarVisibility() == MRScrollbarVisibility::Always;
	bool reserveHorizontal = showWithoutRange;
	bool reserveVertical = showWithoutRange;

	if (primaryEditor != nullptr && (primaryEditor->state & sfVisible) == 0) primaryEditor->show();
	if (primaryEditor != nullptr) primaryEditor->setScrollBarsAlwaysVisible(true);
	if (sourceIndicator != nullptr) sourceIndicator->hide();
	if (primaryEditor != nullptr && !showWithoutRange) {
		primaryEditor->changeBounds(editorBounds);
		primaryEditor->updateMetrics();
		reserveHorizontal = horizontalScrollBar != nullptr && horizontalScrollBar->maxVal > horizontalScrollBar->minVal;
		reserveVertical = verticalScrollBar != nullptr && verticalScrollBar->maxVal > verticalScrollBar->minVal;
	}
	for (int pass = 0; pass < 3; ++pass) {
		editorBounds = content;
		if (reserveVertical && content.b.x - content.a.x > 1) editorBounds.b.x = std::max<short>(editorBounds.a.x + 1, editorBounds.b.x - 1);
		if (reserveHorizontal && content.b.y - content.a.y > 1) editorBounds.b.y = std::max<short>(editorBounds.a.y + 1, editorBounds.b.y - 1);
		if (horizontalScrollBar != nullptr) {
			if (reserveHorizontal) {
				const short right = reserveVertical ? std::max<short>(content.a.x + 1, content.b.x - 1) : std::max<short>(content.a.x + 1, content.b.x);
				TRect horizontalRect(content.a.x, std::max<short>(content.a.y, content.b.y - 1), right, content.b.y);
				horizontalScrollBar->locate(horizontalRect);
			} else {
				TRect horizontalRect(content.a.x, content.b.y, content.a.x, content.b.y);
				horizontalScrollBar->locate(horizontalRect);
			}
		}
		if (verticalScrollBar != nullptr) {
			if (reserveVertical) {
				const short bottom = reserveHorizontal ? std::max<short>(content.a.y + 1, content.b.y - 1) : std::max<short>(content.a.y + 1, content.b.y);
				TRect verticalRect(std::max<short>(content.a.x, content.b.x - 1), content.a.y, content.b.x, bottom);
				verticalScrollBar->locate(verticalRect);
			} else {
				TRect verticalRect(content.b.x, content.a.y, content.b.x, content.a.y);
				verticalScrollBar->locate(verticalRect);
			}
		}
		if (primaryEditor != nullptr) primaryEditor->changeBounds(editorBounds);
		if (primaryEditor != nullptr) primaryEditor->updateMetrics();
		if (showWithoutRange) break;
		const bool nextReserveHorizontal = horizontalScrollBar != nullptr && horizontalScrollBar->maxVal > horizontalScrollBar->minVal;
		const bool nextReserveVertical = verticalScrollBar != nullptr && verticalScrollBar->maxVal > verticalScrollBar->minVal;
		if (nextReserveHorizontal == reserveHorizontal && nextReserveVertical == reserveVertical) break;
		reserveHorizontal = nextReserveHorizontal;
		reserveVertical = nextReserveVertical;
	}
	if (horizontalScrollBar != nullptr) {
		if (reserveHorizontal) horizontalScrollBar->show();
		else
			horizontalScrollBar->hide();
	}
	if (verticalScrollBar != nullptr) {
		if (reserveVertical) verticalScrollBar->show();
		else
			verticalScrollBar->hide();
	}
	configureSourcePaneScrollBarColors();
}

void MRBentoBox::hideSourcePaneChrome() noexcept {
	MRFileEditor *primaryEditor = getEditor();
	if (primaryEditor != nullptr) {
		primaryEditor->setScrollBarsAlwaysVisible(false);
		primaryEditor->hide();
	}
	if (horizontalEditorScrollBar() != nullptr) horizontalEditorScrollBar()->hide();
	if (verticalEditorScrollBar() != nullptr) verticalEditorScrollBar()->hide();
	if (editorIndicator() != nullptr) editorIndicator()->hide();
}

void MRBentoBox::configureSourcePaneScrollBarColors() noexcept {
	TScrollBar *horizontalScrollBar = horizontalEditorScrollBar();
	TScrollBar *verticalScrollBar = verticalEditorScrollBar();
	MRFileEditor *sourceEditor = getEditor();
	const TColorAttr fillAttr = sourceEditor != nullptr ? sourceEditor->editorTextFillColor() : MREditWindow::mapColor(6);
	TColorAttr sourceMarkerAttr = MREditWindow::mapColor(activeLeafId == 0 ? 13 : 1);

	if (bentoMode == bbmFileCompare) sourceMarkerAttr = fillAttr;
	if (auto *scrollBar = dynamic_cast<MREditScrollBar *>(horizontalScrollBar)) scrollBar->setColorOverride(true, fillAttr, fillAttr, sourceMarkerAttr);
	if (auto *scrollBar = dynamic_cast<MREditScrollBar *>(verticalScrollBar)) scrollBar->setColorOverride(true, fillAttr, fillAttr, sourceMarkerAttr);
}

void MRBentoBox::drawSourcePaneScrollBars() noexcept {
	if (windowCloseInProgress) return;
	TScrollBar *horizontalScrollBar = horizontalEditorScrollBar();
	TScrollBar *verticalScrollBar = verticalEditorScrollBar();
	const bool showWithoutRange = configuredScrollbarVisibility() == MRScrollbarVisibility::Always;
	MRFileEditor *sourceEditor = getEditor();
	const TColorAttr fillAttr = sourceEditor != nullptr ? sourceEditor->editorTextFillColor() : MREditWindow::mapColor(6);
	TColorAttr sourceMarkerAttr = MREditWindow::mapColor(activeLeafId == 0 ? 13 : 1);

	if (bentoMode == bbmFileCompare) sourceMarkerAttr = fillAttr;
	configureSourcePaneScrollBarColors();
	auto fillRect = [this, fillAttr](const TRect &rect) {
		const short left = std::max<short>(0, rect.a.x);
		const short top = std::max<short>(0, rect.a.y);
		const short right = std::min<short>(size.x, rect.b.x);
		const short bottom = std::min<short>(size.y, rect.b.y);
		const int width = right - left;

		if (width <= 0 || bottom <= top) return;
		TDrawBuffer buffer;
		buffer.moveChar(0, ' ', TAttrPair(fillAttr), width);
		for (short y = top; y < bottom; ++y)
			writeLine(left, y, width, 1, buffer);
	};
	if (sourceEditor != nullptr) {
		TRect content = sourceEditor->getBounds();

		for (const BentoLeaf &leaf : leaves)
			if (leaf.visible && leaf.id == 0) {
				content = contentBounds(leaf.bounds);
				break;
			}
		const TRect editorBounds = sourceEditor->getBounds();
		if (editorBounds.a.y > content.a.y) fillRect(TRect(content.a.x, content.a.y, content.b.x, editorBounds.a.y));
		if (editorBounds.b.y < content.b.y) fillRect(TRect(content.a.x, editorBounds.b.y, content.b.x, content.b.y));
		if (editorBounds.a.x > content.a.x) fillRect(TRect(content.a.x, editorBounds.a.y, editorBounds.a.x, editorBounds.b.y));
		if (editorBounds.b.x < content.b.x) fillRect(TRect(editorBounds.b.x, editorBounds.a.y, content.b.x, editorBounds.b.y));
	}
	auto scrollBarRequired = [showWithoutRange](TScrollBar *scrollBar) {
		if (scrollBar == nullptr) return false;
		if ((scrollBar->state & sfVisible) == 0) return false;
		if (scrollBar->size.x <= 0 || scrollBar->size.y <= 0) return false;
		if (!showWithoutRange && scrollBar->maxVal <= scrollBar->minVal) return false;
		return true;
	};
	auto drawCell = [this](short x, short y, char ch, TColorAttr attr) {
		TDrawBuffer buffer;
		buffer.moveChar(0, ch, attr, 1);
		writeBuf(x, y, 1, 1, buffer);
	};
	auto drawScrollBar = [&](TScrollBar *scrollBar) {
		if (!scrollBarRequired(scrollBar)) return false;
		const TRect bounds = scrollBar->getBounds();
		TColorAttr baseAttr = sourceMarkerAttr;
		const int logicalSize = std::max(3, scrollBar->size.x == 1 ? scrollBar->size.y : scrollBar->size.x);
		const int markerPos = scrollBar->getPos();

		if (scrollBar->size.x == 1) {
			for (int row = 0; row < logicalSize; ++row) {
				char ch = scrollBar->chars[2];
				TColorAttr attr = fillAttr;
				if (row == 0) ch = scrollBar->chars[0];
				else if (row == logicalSize - 1)
					ch = scrollBar->chars[1];
				else if (scrollBar->maxVal == scrollBar->minVal)
					ch = scrollBar->chars[4];
				else if (row == markerPos) {
					ch = scrollBar->chars[3];
					attr = baseAttr;
				}
				drawCell(bounds.a.x, static_cast<short>(bounds.a.y + row), ch, attr);
			}
		} else {
			const int width = std::max(0, bounds.b.x - bounds.a.x);
			if (width <= 0) return false;
			TDrawBuffer buffer;
			buffer.moveChar(0, ' ', fillAttr, static_cast<ushort>(width));
			for (int column = 0; column < std::min(width, logicalSize); ++column) {
				char ch = scrollBar->chars[2];
				TColorAttr attr = fillAttr;
				if (column == 0) ch = scrollBar->chars[0];
				else if (column == logicalSize - 1)
					ch = scrollBar->chars[1];
				else if (scrollBar->maxVal == scrollBar->minVal)
					ch = scrollBar->chars[4];
				else if (column == markerPos) {
					ch = scrollBar->chars[3];
					attr = baseAttr;
				}
				buffer.moveChar(static_cast<ushort>(column), ch, attr, 1);
			}
			writeLine(bounds.a.x, bounds.a.y, width, 1, buffer);
		}
		return true;
	};

	normalizeScrollBarTrackGlyph(horizontalScrollBar);
	normalizeScrollBarTrackGlyph(verticalScrollBar);
	sourceScrollBarPaletteActive = true;
	const bool drewHorizontal = drawScrollBar(horizontalScrollBar);
	const bool drewVertical = drawScrollBar(verticalScrollBar);
	if (horizontalScrollBar != nullptr && !drewHorizontal) fillRect(horizontalScrollBar->getBounds());
	if (verticalScrollBar != nullptr && !drewVertical) fillRect(verticalScrollBar->getBounds());
	if (horizontalScrollBar != nullptr && verticalScrollBar != nullptr && (horizontalScrollBar->state & sfVisible) != 0 && (verticalScrollBar->state & sfVisible) != 0) {
		const TRect horizontalBounds = horizontalScrollBar->getBounds();
		const TRect verticalBounds = verticalScrollBar->getBounds();
		if (horizontalBounds.a.y < horizontalBounds.b.y && verticalBounds.a.x < verticalBounds.b.x) {
			TDrawBuffer buffer;
			buffer.moveChar(0, ' ', TAttrPair(fillAttr), 1);
			writeBuf(verticalBounds.a.x, horizontalBounds.a.y, 1, 1, buffer);
		}
	} else if (horizontalScrollBar != nullptr && verticalScrollBar != nullptr) {
		const TRect horizontalBounds = horizontalScrollBar->getBounds();
		const TRect verticalBounds = verticalScrollBar->getBounds();
		if (horizontalBounds.a.y < horizontalBounds.b.y && verticalBounds.a.x < verticalBounds.b.x) {
			TRect corner(verticalBounds.a.x, horizontalBounds.a.y, static_cast<short>(verticalBounds.a.x + 1), static_cast<short>(horizontalBounds.a.y + 1));
			fillRect(corner);
		}
	}
	sourceScrollBarPaletteActive = false;
}

void MRBentoBox::drawSharedEditorPanes() noexcept {
	bool hasSharedPane = false;

	for (const BentoLeaf &leaf : leaves)
		if (leaf.visible && leaf.spec.bufferPolicy == bpbSharedSourceBuffer && leaf.id != 0) hasSharedPane = true;
	if (!hasSharedPane) return;
	if (getEditor() != nullptr) getEditor()->drawView();
	for (BentoLeaf &leaf : leaves)
		if (leaf.visible && leaf.pane != nullptr && leaf.spec.bufferPolicy == bpbSharedSourceBuffer) leaf.pane->drawView();
}

void MRBentoBox::drawPaneFrames() noexcept {
	if (windowCloseInProgress || !hasPaneSplit()) return;
	for (BentoLeaf &leaf : leaves)
		if (leaf.visible && leaf.pane != nullptr) leaf.pane->drawPaneScrollBars();
	for (std::size_t i = 0; i < leaves.size(); ++i)
		if (leaves[i].visible) drawPaneFrame(i);
}

void MRBentoBox::refreshBentoColorTheme() noexcept {
	if (windowCloseInProgress || (state & sfVisible) == 0) return;
	drawSourcePaneScrollBars();
	drawPaneFrames();
}

void MRBentoBox::drawPaneFrame(std::size_t leafIndex) noexcept {
	if (leafIndex >= leaves.size()) return;
	const BentoLeaf &leaf = leaves[leafIndex];
	const TRect bounds = leaf.bounds;
	const int width = bounds.b.x - bounds.a.x;
	const int height = bounds.b.y - bounds.a.y;
	if (width <= 0 || height <= 0) return;

	const bool focused = leaf.id == activeLeafId && (state & sfFocused) != 0;
	const bool maximized = leaf.id == maximizedLeafId;
	const bool withControls = focused;
	TColorAttr frameAttr = MREditWindow::mapColor(focused ? 13 : 1);
	unsigned char fileComparePaneColor = 0;
	TDrawBuffer buffer;

	if (bentoMode == bbmFileCompare && focused && configuredColorSlotOverride(kMrPaletteFileCompareFocusedPaneBorder, fileComparePaneColor)) frameAttr = static_cast<TColorAttr>(fileComparePaneColor);
	else if (bentoMode == bbmFileCompare && configuredColorSlotOverride(kMrPaletteFileComparePaneBorder, fileComparePaneColor))
		frameAttr = static_cast<TColorAttr>(fileComparePaneColor);
	const TAttrPair frameColor = TAttrPair(frameAttr);

	buffer.moveChar(0, kBentoFrameGlyphs.singleHorizontal, frameColor, static_cast<ushort>(width));
	buffer.putChar(0, '\xDA');
	if (width > 1) buffer.putChar(static_cast<ushort>(width - 1), '\xBF');

	const int closeLeftX = kPaneChromeFrameRest;
	const int maximizeRightX = width - kPaneChromeFrameRest;
	const int closeWidth = withControls && width >= closeLeftX + kPaneCloseButtonWidth ? kPaneCloseButtonWidth : 0;
	const int closeX = closeWidth > 0 ? closeLeftX : 0;
	const int maximizeWidth = withControls && maximizeRightX >= kPaneMaximizeButtonWidth ? kPaneMaximizeButtonWidth : 0;
	const int maximizeX = maximizeWidth > 0 ? maximizeRightX - kPaneMaximizeButtonWidth : width;
	const int leftContentX = closeLeftX + (withControls ? kPaneCloseButtonWidth + kPaneChromeGap : 0);
	const int titleRightX = maximizeWidth > 0 ? maximizeX - kPaneChromeGap : std::max(0, width - kPaneChromeFrameRest);
	const int boundedTitleRightX = std::min(titleRightX, std::max(0, width - kPaneChromeFrameRest));
	int titleAvailable = std::max(0, boundedTitleRightX - leftContentX);
	std::string title = std::string("[") + paneTitleForLeaf(leaf) + "]";
	int titleWidth = std::min(static_cast<int>(title.size()), titleAvailable);
	int titleX = boundedTitleRightX - titleWidth;

	if (withControls && paneRoleDropList.visible() && pendingPaneRoleTargetLeafId == leaf.id) {
		titleX = std::clamp<int>(paneRoleListAnchor.a.x - bounds.a.x - 1, leftContentX, std::max(leftContentX, titleRightX - 2));
		titleWidth = std::clamp<int>(paneRoleListAnchor.b.x - paneRoleListAnchor.a.x + 2, 2, std::max(2, titleRightX - titleX));
		title.assign(static_cast<std::size_t>(titleWidth), ' ');
		title.front() = '[';
		title.back() = ']';
	}

	if (withControls && closeWidth > 0) buffer.moveStr(static_cast<ushort>(closeX), kPaneCloseIcon, frameColor, closeWidth);
	if (titleWidth > 0) buffer.moveStr(static_cast<ushort>(titleX), title.c_str(), frameColor, titleWidth);
	if (withControls && maximizeWidth > 0) buffer.moveStr(static_cast<ushort>(maximizeX), maximized ? kPaneRestoreIcon : kPaneMaximizeIcon, frameColor, maximizeWidth);
	writeBuf(bounds.a.x, bounds.a.y, static_cast<short>(width), 1, buffer);

	if (height > 1) {
		buffer.moveChar(0, kBentoFrameGlyphs.singleHorizontal, frameColor, static_cast<ushort>(width));
		buffer.putChar(0, '\xC0');
		if (width > 1) buffer.putChar(static_cast<ushort>(width - 1), '\xD9');
		writeBuf(bounds.a.x, bounds.b.y - 1, static_cast<short>(width), 1, buffer);
	}

	if (height > 2) {
		buffer.moveChar(0, kBentoFrameGlyphs.singleVertical, frameColor, 1);
		for (int y = bounds.a.y + 1; y < bounds.b.y - 1; ++y) {
			writeBuf(bounds.a.x, static_cast<short>(y), 1, 1, buffer);
			if (width > 1) writeBuf(bounds.b.x - 1, static_cast<short>(y), 1, 1, buffer);
		}
	}
}

void MRBentoBox::layoutNode(int nodeIndex, const TRect &bounds) {
	if (nodeIndex < 0 || nodeIndex >= static_cast<int>(layoutTree.size())) return;
	BentoLayoutNode &node = layoutTree[nodeIndex];
	if (node.kind == blnPane) {
		for (BentoLeaf &leaf : leaves) {
			if (leaf.id == node.leafId) {
				leaf.bounds = bounds;
				leaf.visible = true;
				return;
			}
		}
		return;
	}
	const int position = currentDividerPosition(nodeIndex);
	if (node.orientation == bsoVertical) {
		TRect first(bounds.a.x, bounds.a.y, std::max<int>(bounds.a.x + 1, position), bounds.b.y);
		TRect second(std::min<int>(bounds.b.x - 1, position), bounds.a.y, bounds.b.x, bounds.b.y);
		layoutNode(node.firstChild, first);
		layoutNode(node.secondChild, second);
	} else {
		TRect first(bounds.a.x, bounds.a.y, bounds.b.x, std::max<int>(bounds.a.y + 1, position));
		TRect second(bounds.a.x, std::min<int>(bounds.b.y - 1, position), bounds.b.x, bounds.b.y);
		layoutNode(node.firstChild, first);
		layoutNode(node.secondChild, second);
	}
}

void MRBentoBox::postCloseCommand() noexcept {
	TEvent event;

	windowCloseInProgress = true;
	std::memset(&event, 0, sizeof(event));
	event.what = evCommand;
	event.message.command = cmClose;
	event.message.infoPtr = this;
	putEvent(event);
}

void MRBentoBox::closePane(int leafId) noexcept {
	if (leafId == 0) {
		postCloseCommand();
		return;
	}
	if (maximizedLeafId == leafId) maximizedLeafId = -1;
	collapseLeafNode(leafId);
	if (activeLeafId == leafId || nodeIndexForLeaf(activeLeafId) < 0) setActivePane(0);
	layoutSplitPanes();
	mrMarkWorkspaceAutosaveDirty();
}

void MRBentoBox::closeSecondaryPane() noexcept {
	int toolLeaf = firstToolLeafId();
	bool changed = false;
	while (toolLeaf >= 0) {
		collapseLeafNode(toolLeaf);
		changed = true;
		toolLeaf = firstToolLeafId();
	}
	setActivePane(0);
	layoutSplitPanes();
	if (changed) mrMarkWorkspaceAutosaveDirty();
}

void MRBentoBox::showPaneRoleList(TPoint, int targetLeafId) {
	const int listWidth = 17;
	const int listHeight = 6;
	const bool openingRoleList = !paneRoleDropList.visible();
	TRect paneRect = paneBoundsForLeaf(targetLeafId);
	MRBentoPaneFrameView *chromeView = nullptr;

	if (!titleMenuEnabledForLeaf(targetLeafId)) return;
	for (MRBentoPaneFrameView *view : paneFrameViews)
		if (view != nullptr && view->paneLeafId() == targetLeafId) chromeView = view;
	TRect localAnchor = chromeView != nullptr ? chromeView->paneRoleListAnchor(listWidth) : TRect(1, 0, 1 + listWidth, 0);
	int left = std::clamp<int>(paneRect.a.x + localAnchor.a.x, 1, std::max(1, size.x - listWidth - 1));
	int top = std::clamp<int>(paneRect.a.y + localAnchor.a.y, 1, std::max(1, size.y - listHeight - 1));

	paneActionDropList.hide();
	fileCompareActionDropList.hide();
	pendingPaneRoleTargetLeafId = targetLeafId;
	paneRoleListAnchor = TRect(left, top, left + listWidth, top);
	if (openingRoleList && chromeView != nullptr) chromeView->setPaneRoleListTitleOpen(true, paneRoleListAnchor);
	paneRoleDropList.toggle(*this, paneRoleListAnchor, paneRoleChoices(), bentoPaneRoleTitle(roleForLeaf(targetLeafId)), this, cmMrBentoPaneRoleAccepted, listHeight);
	if (!openingRoleList) updatePaneRoleListChrome();
}

void MRBentoBox::showPaneActionList() {
	const int listWidth = 9;
	const int listHeight = 3;
	const short selectedIndex = paneRoleDropList.selectedIndex();
	const int selectedRow = paneRoleListAnchor.a.y + std::max<short>(0, selectedIndex);
	int left = paneRoleListAnchor.a.x - listWidth;
	int top = std::clamp<int>(selectedRow, 1, std::max(1, size.y - listHeight - 1));
	if (left < 1) left = std::clamp<int>(paneRoleListAnchor.b.x, 1, std::max(1, size.x - listWidth - 1));
	TRect anchor(left, top, left + listWidth, top);
	fileCompareActionDropList.hide();
	paneActionDropList.hide();
	paneActionDropList.toggle(*this, anchor, paneActionChoices(), kBentoPaneActionReplace, this, cmMrBentoPaneActionAccepted, listHeight);
}

void MRBentoBox::showFileCompareActionList(TPoint globalMouse, int targetLeafId) {
	const int listWidth = 12;
	const int listHeight = 3;
	const TPoint localMouse = makeLocal(globalMouse);
	const int left = std::clamp<int>(localMouse.x, 1, std::max(1, size.x - listWidth - 1));
	const int top = std::clamp<int>(localMouse.y, 1, std::max(1, size.y - listHeight - 1));
	const TRect anchor(left, top, left + listWidth, top);
	const std::vector<std::string> actions{kFileCompareActionNext, kFileCompareActionPrevious, kFileCompareActionApply};

	if (bentoMode != bbmFileCompare || !bentoRoleIsDiff(roleForLeaf(targetLeafId))) return;
	paneRoleDropList.hide();
	paneActionDropList.hide();
	updatePaneRoleListChrome();
	pendingFileCompareActionLeafId = targetLeafId;
	pendingFileCompareActionGroupIndex = -1;
	const MRBentoPaneRole targetRole = roleForLeaf(targetLeafId);
	MREditWindow *targetWindow = targetLeafId == 0 ? static_cast<MREditWindow *>(this) : paneWindowForLeaf(targetLeafId);
	MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
	if (targetEditor != nullptr) {
		const std::size_t clickedOffset = targetEditor->offsetForGlobalPoint(globalMouse);
		const std::size_t clickedLine = targetEditor->lineIndexOfOffset(clickedOffset);
		pendingFileCompareActionGroupIndex = fileCompareChangeGroupIndexAtLine(targetRole, clickedLine, fileComparePanesEditable());
	}
	setActivePane(targetLeafId);
	fileCompareActionDropList.toggle(*this, anchor, actions, kFileCompareActionApply, this, cmMrFileComparePaneActionAccepted, listHeight);
}

void MRBentoBox::acceptPaneRoleChoice() {
	std::string roleTitle;
	if (!paneRoleDropList.selectedValue(roleTitle)) return;
	pendingPaneRole = bentoPaneRoleForTitle(roleTitle);
	showPaneActionList();
}

void MRBentoBox::acceptPaneActionChoice() {
	std::string action;
	if (!paneActionDropList.acceptSelection(action)) return;
	const MRBentoPanePlacement placement = panePlacementForAction(action);
	paneRoleDropList.hide();
	updatePaneRoleListChrome();
	static_cast<void>(placePaneRoleInContext(pendingPaneRole, placement, pendingPaneRoleTargetLeafId));
	pendingPaneRoleTargetLeafId = activeLeafId;
}

void MRBentoBox::acceptFileCompareActionChoice() {
	std::string action;
	if (!fileCompareActionDropList.acceptSelection(action)) return;
	if (nodeIndexForLeaf(pendingFileCompareActionLeafId) < 0 || !bentoRoleIsDiff(roleForLeaf(pendingFileCompareActionLeafId))) return;
	setActivePane(pendingFileCompareActionLeafId);
	if (action == kFileCompareActionNext) {
		static_cast<void>(navigateFileCompareChange(true));
		return;
	}
	if (action == kFileCompareActionPrevious) {
		static_cast<void>(navigateFileCompareChange(false));
		return;
	}
	if (action == kFileCompareActionApply) {
		const MRBentoPaneRole role = roleForLeaf(pendingFileCompareActionLeafId);
		const bool originalToCompare = role == bprDiffOriginal;
		const std::size_t groupIndex = pendingFileCompareActionGroupIndex >= 0 ? static_cast<std::size_t>(pendingFileCompareActionGroupIndex) : fileCompareChangeGroups.size();
		if (groupIndex < fileCompareChangeGroups.size())
			static_cast<void>(applyFileCompareChangeGroup(originalToCompare, fileCompareChangeGroups[groupIndex]));
		else
			static_cast<void>(applyFileCompareChange(originalToCompare));
		pendingFileCompareActionGroupIndex = -1;
	}
}

bool MRBentoBox::handlePaneDropListEvent(TEvent &event) {
	const bool roleListVisible = paneRoleDropList.visible();
	const bool actionListVisible = paneActionDropList.visible();
	const bool fileCompareActionListVisible = fileCompareActionDropList.visible();
	if (!roleListVisible && !actionListVisible && !fileCompareActionListVisible) return false;
	if (fileCompareActionDropList.handleOpenListEvent(event, false)) return true;
	if (paneActionDropList.handleOpenListEvent(event, false)) {
		updatePaneRoleListChrome();
		return true;
	}
	if (paneRoleDropList.handleOpenListEvent(event, false)) {
		updatePaneRoleListChrome();
		return true;
	}
	if (event.what == evMouseDown && !paneRoleDropList.containsPoint(event.mouse.where) && !paneActionDropList.containsPoint(event.mouse.where) && !fileCompareActionDropList.containsPoint(event.mouse.where)) {
		paneRoleDropList.hide();
		paneActionDropList.hide();
		fileCompareActionDropList.hide();
		updatePaneRoleListChrome();
		clearEvent(event);
		return true;
	}
	if (event.what == evMouseDown && (event.mouse.buttons & mbRightButton) != 0 && paneRoleDropList.containsPoint(event.mouse.where)) {
		paneRoleDropList.focusIndex(paneRoleIndexAt(event.mouse.where));
		acceptPaneRoleChoice();
		clearEvent(event);
		return true;
	}
	if (event.what == evMouseDown && paneRoleDropList.containsPoint(event.mouse.where)) {
		TWindow::handleEvent(event);
		acceptPaneRoleChoice();
		clearEvent(event);
		return true;
	}
	if (event.what == evMouseDown && paneActionDropList.containsPoint(event.mouse.where)) {
		TWindow::handleEvent(event);
		acceptPaneActionChoice();
		clearEvent(event);
		return true;
	}
	if (event.what == evMouseDown && fileCompareActionDropList.containsPoint(event.mouse.where)) {
		TWindow::handleEvent(event);
		acceptFileCompareActionChoice();
		clearEvent(event);
		return true;
	}
	if (event.what == evKeyDown) {
		TWindow::handleEvent(event);
		clearEvent(event);
		return true;
	}
	return false;
}

bool MRBentoBox::handleOuterFrameCloseMouse(TEvent &event) {
	if (event.what != evMouseDown || (event.mouse.buttons & mbLeftButton) == 0 || (flags & wfClose) == 0) return false;
	TPoint mouse = makeLocal(event.mouse.where);
	if (mouse.y != 0 || mouse.x < 2 || mouse.x > 4) return false;
	while (mouseEvent(event, evMouse))
		;
	mouse = makeLocal(event.mouse.where);
	if (mouse.y == 0 && mouse.x >= 2 && mouse.x <= 4) postCloseCommand();
	clearEvent(event);
	return true;
}

void MRBentoBox::updatePaneRoleListChrome() noexcept {
	for (MRBentoPaneFrameView *view : paneFrameViews)
		if (view != nullptr) view->setPaneRoleListTitleOpen(false, paneRoleListAnchor);
	for (MRBentoPaneFrameView *view : paneFrameViews)
		if (view != nullptr && view->paneLeafId() == pendingPaneRoleTargetLeafId) view->setPaneRoleListTitleOpen(paneRoleDropList.visible(), paneRoleListAnchor);
	bentoProjectionDirty |= bpdChrome;
}

short MRBentoBox::paneRoleIndexAt(TPoint globalMouse) {
	const TPoint localMouse = makeLocal(globalMouse);
	const std::vector<std::string> roles = paneRoleChoices();
	const int maxIndex = std::max(0, static_cast<int>(roles.size()) - 1);
	return static_cast<short>(std::clamp(localMouse.y - paneRoleListAnchor.a.y, 0, maxIndex));
}

void MRBentoBox::dragDivider(TEvent &event, int nodeIndex) noexcept {
	if (maximizedLeafId >= 0) return;
	if (nodeIndex < 0 || nodeIndex >= static_cast<int>(layoutTree.size())) return;
	const TPoint initialLocal = makeLocal(event.mouse.where);
	const bool vertical = layoutTree[nodeIndex].orientation == bsoVertical;
	const int dragOffset = (vertical ? initialLocal.x : initialLocal.y) - currentDividerPosition(nodeIndex);
	setDividerPosition(nodeIndex, (vertical ? initialLocal.x : initialLocal.y) - dragOffset);
	while (mouseEvent(event, evMouseMove | evMouseAuto | evMouseUp)) {
		if (event.what == evMouseUp) break;
		const TPoint local = makeLocal(event.mouse.where);
		setDividerPosition(nodeIndex, (vertical ? local.x : local.y) - dragOffset);
	}
}

void MRBentoBox::setDividerY(int y) noexcept {
	setDividerPosition(y);
}

void MRBentoBox::setDividerPosition(int position) noexcept {
	setDividerPosition(rootNode, position);
}

void MRBentoBox::setDividerPosition(int nodeIndex, int position) noexcept {
	if (nodeIndex < 0 || nodeIndex >= static_cast<int>(layoutTree.size())) return;
	if (layoutTree[nodeIndex].kind != blnSplit) return;
	const int clampedPosition = clampedDividerPosition(nodeIndex, position);
	if (layoutTree[nodeIndex].dividerPosition == clampedPosition) return;
	layoutTree[nodeIndex].dividerPosition = clampedPosition;
	layoutSplitPanes();
	mrMarkWorkspaceAutosaveDirty();
}

void MRBentoBox::setActivePane(int leafId) noexcept {
	if (nodeIndexForLeaf(leafId) < 0) return;
	activeLeafId = leafId;
	if (leafId == 0) {
		MRFileEditor *sourceEditor = getEditor();
		if (sourceEditor != nullptr && sourceEditor->getState(sfVisible)) sourceEditor->select();
	} else {
		MRPaneEditWindow *pane = paneWindowForLeaf(leafId);
		MRFileEditor *paneEditor = pane != nullptr ? pane->getEditor() : nullptr;
		if (pane != nullptr && pane->getState(sfVisible)) pane->select();
		if (paneEditor != nullptr && paneEditor->getState(sfVisible)) paneEditor->select();
	}
	updateActivePaneFrame();
}

void MRBentoBox::updateActivePaneFrame() noexcept {
	for (std::size_t i = 0; i < leaves.size() && i < paneFrameViews.size(); ++i) {
		MRBentoPaneFrameView *view = paneFrameViews[i];
		if (view == nullptr || !leaves[i].visible) continue;
		if (leaves[i].pane != nullptr) leaves[i].pane->setPaneFocused(leaves[i].id == activeLeafId && (state & sfFocused) != 0);
		view->setPane(leaves[i].id, paneTitleForLeaf(leaves[i]).c_str(), leaves[i].id == 0 && bentoMode != bbmDocumentViewports, leaves[i].id == activeLeafId && (state & sfFocused) != 0, leaves[i].id == maximizedLeafId);
	}
}

void MRBentoBox::setActivePaneForMouse(TPoint globalMouse) noexcept {
	const int leaf = leafAt(makeLocal(globalMouse));
	if (leaf >= 0) setActivePane(leaf);
}

void MRBentoBox::toggleLeafMaximized(int leafId) noexcept {
	if (leafId < 0 || (leafId == 0 && bentoMode != bbmDocumentViewports && !hasPaneSplit()) || nodeIndexForLeaf(leafId) < 0) return;
	maximizedLeafId = maximizedLeafId == leafId ? -1 : leafId;
	setActivePane(leafId);
	layoutSplitPanes();
	mrMarkWorkspaceAutosaveDirty();
}

bool MRBentoBox::handleDividerChromeMouse(TEvent &event) {
	if (event.what != evMouseDown) return false;
	const TPoint localMouse = makeLocal(event.mouse.where);
	for (std::size_t i = 0; i < leaves.size() && i < paneFrameViews.size(); ++i) {
		MRBentoPaneFrameView *view = paneFrameViews[i];
		if (view == nullptr || !leaves[i].visible) continue;
		TRect bounds = leaves[i].bounds;
		if (!pointInRect(localMouse, bounds)) continue;
		TPoint paneMouse;
		paneMouse.x = localMouse.x - bounds.a.x;
		paneMouse.y = localMouse.y - bounds.a.y;
		MRBentoPaneFrameView::HitKind hit = view->hitTest(paneMouse);
		const int leafId = view->paneLeafId();
		switch (hit) {
			case MRBentoPaneFrameView::hitTitle:
				if ((event.mouse.buttons & (mbLeftButton | mbRightButton)) == 0) return false;
				setActivePane(leafId);
				if (titleMenuEnabledForLeaf(leafId)) showPaneRoleList(event.mouse.where, leafId);
				return true;
			case MRBentoPaneFrameView::hitClose:
				if ((event.mouse.buttons & mbLeftButton) == 0) return false;
				closePane(leafId);
				return true;
			case MRBentoPaneFrameView::hitMaximize:
				if ((event.mouse.buttons & mbLeftButton) == 0) return false;
				toggleLeafMaximized(leafId);
				return true;
			case MRBentoPaneFrameView::hitNone:
				return false;
		}
	}
	return false;
}

TRect MRBentoBox::paneBoundsForLeaf(int leafId) const noexcept {
	for (const BentoLeaf &leaf : leaves)
		if (leaf.id == leafId) return leaf.bounds;
	return TRect(0, 0, 0, 0);
}

int MRBentoBox::defaultDividerPosition() const noexcept {
	return defaultDividerPosition(rootNode);
}

int MRBentoBox::defaultDividerPosition(int nodeIndex) const noexcept {
	const TRect bounds = nodeBounds(nodeIndex);
	if (nodeIndex < 0 || nodeIndex >= static_cast<int>(layoutTree.size())) return 0;
	if (layoutTree[nodeIndex].orientation == bsoVertical) return bounds.a.x + std::max(1, bounds.b.x - bounds.a.x) / 2;
	return bounds.a.y + std::max(1, bounds.b.y - bounds.a.y) / 2;
}

int MRBentoBox::clampedDividerPosition(int position) const noexcept {
	return clampedDividerPosition(rootNode, position);
}

int MRBentoBox::clampedDividerPosition(int nodeIndex, int position) const noexcept {
	const TRect bounds = nodeBounds(nodeIndex);
	if (nodeIndex < 0 || nodeIndex >= static_cast<int>(layoutTree.size())) return position;
	if (layoutTree[nodeIndex].orientation == bsoVertical) {
		const int minX = std::min<int>(bounds.b.x - 1, bounds.a.x + kMinimumPaneWidth);
		const int maxX = std::max(minX, bounds.b.x - kMinimumPaneWidth);
		return std::clamp(position, minX, maxX);
	}
	const int minY = std::min<int>(bounds.b.y - 1, bounds.a.y + kMinimumPaneHeight);
	const int maxY = std::max(minY, bounds.b.y - kMinimumPaneHeight);
	return std::clamp(position, minY, maxY);
}

int MRBentoBox::currentDividerPosition() const noexcept {
	return currentDividerPosition(rootNode);
}

int MRBentoBox::currentDividerPosition(int nodeIndex) const noexcept {
	if (nodeIndex < 0 || nodeIndex >= static_cast<int>(layoutTree.size())) return 0;
	const int value = layoutTree[nodeIndex].dividerPosition;
	return clampedDividerPosition(nodeIndex, value > 0 ? value : defaultDividerPosition(nodeIndex));
}

bool MRBentoBox::hasPaneSplit() const noexcept {
	return rootNode >= 0 && rootNode < static_cast<int>(layoutTree.size()) && layoutTree[rootNode].kind == blnSplit;
}

bool MRBentoBox::pointInRect(TPoint point, const TRect &rect) const noexcept {
	return point.x >= rect.a.x && point.x < rect.b.x && point.y >= rect.a.y && point.y < rect.b.y;
}

int MRBentoBox::leafAt(TPoint point) const noexcept {
	for (const BentoLeaf &leaf : leaves)
		if (leaf.visible && pointInRect(point, leaf.bounds)) return leaf.id;
	return -1;
}

int MRBentoBox::nodeAtDivider(TPoint point) const noexcept {
	if (maximizedLeafId >= 0) return -1;
	for (int i = 0; i < static_cast<int>(layoutTree.size()); ++i) {
		const BentoLayoutNode &node = layoutTree[i];
		if (node.kind != blnSplit) continue;
		const TRect bounds = nodeBounds(i);
		const int position = currentDividerPosition(i);
		if (node.orientation == bsoVertical) {
			if ((point.x == position || point.x == position - 1) && point.y >= bounds.a.y && point.y < bounds.b.y) return i;
		} else if ((point.y == position || point.y == position - 1) && point.x >= bounds.a.x && point.x < bounds.b.x)
			return i;
	}
	return -1;
}

MRPaneEditWindow *MRBentoBox::paneWindowForLeaf(int leafId) const noexcept {
	for (const BentoLeaf &leaf : leaves)
		if (leaf.id == leafId) return leaf.pane;
	return nullptr;
}

MRBentoPaneRole MRBentoBox::roleForLeaf(int leafId) const noexcept {
	for (const BentoLeaf &leaf : leaves)
		if (leaf.id == leafId) return leaf.role;
	return bprCompilerOutput;
}

bool MRBentoBox::titleMenuEnabledForLeaf(int leafId) const noexcept {
	for (const BentoLeaf &leaf : leaves)
		if (leaf.id == leafId) return leaf.spec.titleMenu != nullptr;
	return false;
}

int MRBentoBox::firstToolLeafId() const noexcept {
	for (const BentoLeaf &leaf : leaves)
		if (leaf.id != 0 && leaf.visible) return leaf.id;
	return -1;
}

int MRBentoBox::leafIdForRole(MRBentoPaneRole role) const noexcept {
	for (const BentoLeaf &leaf : leaves)
		if (leaf.visible && leaf.role == role) return leaf.id;
	return -1;
}

int MRBentoBox::nodeIndexForLeaf(int leafId) const noexcept {
	std::vector<int> stack;

	if (rootNode >= 0) stack.push_back(rootNode);
	while (!stack.empty()) {
		const int nodeIndex = stack.back();
		stack.pop_back();
		if (nodeIndex < 0 || nodeIndex >= static_cast<int>(layoutTree.size())) continue;
		const BentoLayoutNode &node = layoutTree[nodeIndex];
		if (node.kind == blnPane && node.leafId == leafId) return nodeIndex;
		if (node.kind == blnSplit) {
			stack.push_back(node.firstChild);
			stack.push_back(node.secondChild);
		}
	}
	return -1;
}

int MRBentoBox::parentNodeOf(int childNodeIndex) const noexcept {
	for (int i = 0; i < static_cast<int>(layoutTree.size()); ++i)
		if (layoutTree[i].kind == blnSplit && (layoutTree[i].firstChild == childNodeIndex || layoutTree[i].secondChild == childNodeIndex)) return i;
	return -1;
}

int MRBentoBox::viewportNumberForLeaf(int leafId) const noexcept {
	if (bentoMode != bbmDocumentViewports || rootNode < 0) return 0;

	int number = 0;
	std::vector<int> stack;
	stack.push_back(rootNode);
	while (!stack.empty()) {
		const int nodeIndex = stack.back();
		stack.pop_back();
		if (nodeIndex < 0 || nodeIndex >= static_cast<int>(layoutTree.size())) continue;
		const BentoLayoutNode &node = layoutTree[nodeIndex];
		if (node.kind == blnPane) {
			++number;
			if (node.leafId == leafId) return number;
		} else if (node.kind == blnSplit) {
			stack.push_back(node.secondChild);
			stack.push_back(node.firstChild);
		}
	}
	return 0;
}

MRBentoPaneSpec MRBentoBox::paneSpecForRole(MRBentoPaneRole role) const noexcept {
	const MRBentoPaneTitleMenuSpec *titleMenu = bentoMode == bbmDocumentViewports ? nullptr : &kBentoRoleTitleMenu;
	switch (role) {
		case bprSource:
			return MRBentoPaneSpec(bprSource, bpbSharedSourceBuffer, false, false, false, false, titleMenu);
		case bprSplitEditor:
			return MRBentoPaneSpec(bprSplitEditor, bpbSharedSourceBuffer, false, false, false, bentoMode == bbmDocumentViewports, titleMenu);
		case bprDiffCompare:
			return MRBentoPaneSpec(role, bpbOwnBuffer, !fileComparePanesEditable(), false, true, true, titleMenu);
		case bprDiffOriginal:
			return MRBentoPaneSpec(role, bpbOwnBuffer, !fileComparePanesEditable(), true, true, true, titleMenu);
		case bprCompilerOutput:
		case bprAppOutput:
		case bprProblems:
		case bprDebuggerOutput:
		case bprWatches:
		case bprVariables:
		default:
			return MRBentoPaneSpec(role, bpbOwnBuffer, true, true, true, true, titleMenu);
	}
}

std::string MRBentoBox::paneTitleForLeaf(const BentoLeaf &leaf) const {
	if (bentoMode == bbmDocumentViewports) {
		const int viewportNumber = viewportNumberForLeaf(leaf.id);
		return "Viewport #" + std::to_string(std::max(1, viewportNumber));
	}
	if (bentoMode == bbmFileCompare && bentoRoleIsDiff(leaf.role)) {
		const std::string status = fileCompareStatusForLeaf(leaf);
		if (!status.empty()) return std::string(bentoPaneRoleTitle(leaf.role)) + " [" + status + "]";
	}
	if (leaf.role == bprCompilerOutput && !compilerOutputStatus.empty()) return std::string(bentoPaneRoleTitle(leaf.role)) + " [" + compilerOutputStatus + "]";
	if (leaf.role == bprProblems && !compilerProblemsStatus.empty()) return std::string(bentoPaneRoleTitle(leaf.role)) + " [" + compilerProblemsStatus + "]";
	if (leaf.role == bprStructure && !structureOutlineStatus.empty()) return std::string(bentoPaneRoleTitle(leaf.role)) + " [" + structureOutlineStatus + "]";
	if (leaf.role == bprFunctions && !functionsOutlineStatus.empty()) return std::string(bentoPaneRoleTitle(leaf.role)) + " [" + functionsOutlineStatus + "]";
	if (!leaf.title.empty()) return leaf.title;
	return bentoPaneRoleTitle(leaf.role);
}

void MRBentoBox::rebuildFileCompareChangeGroups() {
	fileCompareChangeGroups.clear();
	std::size_t displayLine = 0;
	bool groupOpen = false;

	for (const mr::diff::MRDiffHunk &hunk : fileCompareHunks) {
		switch (hunk.op) {
			case mr::diff::MRDiffOp::Equal:
				displayLine += hunk.count;
				groupOpen = false;
				break;
			case mr::diff::MRDiffOp::Delete:
				if (!groupOpen) {
					fileCompareChangeGroups.push_back(FileCompareChangeGroup());
					fileCompareChangeGroups.back().displayStartLine = displayLine;
					fileCompareChangeGroups.back().originalStartLine = hunk.leftStart;
					fileCompareChangeGroups.back().compareStartLine = hunk.rightStart;
					groupOpen = true;
				}
				fileCompareChangeGroups.back().displayLineCount += hunk.count;
				fileCompareChangeGroups.back().deletedLineCount += hunk.count;
				displayLine += hunk.count;
				break;
			case mr::diff::MRDiffOp::Insert:
				if (!groupOpen) {
					fileCompareChangeGroups.push_back(FileCompareChangeGroup());
					fileCompareChangeGroups.back().displayStartLine = displayLine;
					fileCompareChangeGroups.back().originalStartLine = hunk.leftStart;
					fileCompareChangeGroups.back().compareStartLine = hunk.rightStart;
					groupOpen = true;
				}
				fileCompareChangeGroups.back().displayLineCount += hunk.count;
				fileCompareChangeGroups.back().insertedLineCount += hunk.count;
				displayLine += hunk.count;
				break;
			default:
				break;
		}
	}
}

std::size_t MRBentoBox::fileCompareGroupStartLineForRole(const FileCompareChangeGroup &group, MRBentoPaneRole role, bool editablePanes) const noexcept {
	if (!editablePanes) return group.displayStartLine;
	if (role == bprDiffOriginal) return group.originalStartLine;
	if (role == bprDiffCompare) return group.compareStartLine;
	return group.displayStartLine;
}

std::size_t MRBentoBox::fileCompareGroupLineCountForRole(const FileCompareChangeGroup &group, MRBentoPaneRole role, bool editablePanes) const noexcept {
	if (!editablePanes) return std::max<std::size_t>(1, group.displayLineCount);
	if (role == bprDiffOriginal) return std::max<std::size_t>(1, group.deletedLineCount);
	if (role == bprDiffCompare) return std::max<std::size_t>(1, group.insertedLineCount);
	return std::max<std::size_t>(1, group.displayLineCount);
}

std::size_t MRBentoBox::fileCompareGroupNavigationLineForRole(const FileCompareChangeGroup &group, MRBentoPaneRole role, const MRFileEditor &editor, bool editablePanes) const {
	const std::size_t documentLineCount = std::max<std::size_t>(1, editor.bufferModel().lineCount());
	std::size_t targetLine = fileCompareGroupStartLineForRole(group, role, editablePanes);
	if (!editablePanes || !bentoRoleIsDiff(role)) return std::min(targetLine, documentLineCount - 1);

	std::vector<unsigned char> lineKinds;
	fileCompareEditableLineKindsForRole(role, lineKinds, nullptr);
	if (lineKinds.empty()) return std::min(targetLine, documentLineCount - 1);

	const std::size_t lineLimit = std::min(lineKinds.size(), documentLineCount);
	if (lineLimit == 0) return 0;
	if (targetLine >= lineLimit) targetLine = lineLimit - 1;

	std::size_t groupLineCount = fileCompareGroupLineCountForRole(group, role, editablePanes);
	if (role == bprDiffCompare && group.deletedLineCount > group.insertedLineCount) groupLineCount = std::max<std::size_t>(groupLineCount, group.insertedLineCount + 1);
	if (role == bprDiffOriginal && group.insertedLineCount > group.deletedLineCount) groupLineCount = std::max<std::size_t>(groupLineCount, group.deletedLineCount + 1);

	const std::size_t scanEndLine = std::min(lineLimit, targetLine + std::max<std::size_t>(1, groupLineCount) + 1);
	for (std::size_t line = targetLine; line < scanEndLine; ++line)
		if (lineKinds[line] != mrfclkEqual && lineKinds[line] != mrfclkNone) return line;
	return targetLine;
}

std::size_t MRBentoBox::fileCompareMappedLineForRole(MRBentoPaneRole sourceRole, std::size_t sourceLine, const MRFileEditor &targetEditor, bool editablePanes) const noexcept {
	std::size_t targetLine = sourceLine;
	if (!bentoRoleIsDiff(sourceRole)) return targetLine;
	const MRBentoPaneRole targetRole = sourceRole == bprDiffOriginal ? bprDiffCompare : bprDiffOriginal;

	if (editablePanes && fileCompareDiffReady) {
		bool mappedInChangeGroup = false;
		for (const FileCompareChangeGroup &group : fileCompareChangeGroups) {
			const std::size_t sourceStart = fileCompareGroupStartLineForRole(group, sourceRole, true);
			std::size_t sourceLineCount = fileCompareGroupLineCountForRole(group, sourceRole, true);
			if (sourceRole == bprDiffCompare && group.deletedLineCount > group.insertedLineCount) sourceLineCount = std::max<std::size_t>(sourceLineCount, group.insertedLineCount + 1);
			if (sourceRole == bprDiffOriginal && group.insertedLineCount > group.deletedLineCount) sourceLineCount = std::max<std::size_t>(sourceLineCount, group.deletedLineCount + 1);
			if (sourceLine < sourceStart || sourceLine >= sourceStart + sourceLineCount) continue;

			const std::size_t targetStart = fileCompareGroupStartLineForRole(group, targetRole, true);
			const std::size_t targetLineCount = targetRole == bprDiffOriginal ? group.deletedLineCount : group.insertedLineCount;
			mappedInChangeGroup = true;
			if (targetLineCount == 0) {
				targetLine = targetStart;
				break;
			}
			const std::size_t relativeLine = sourceLine - sourceStart;
			targetLine = targetStart + std::min(relativeLine, targetLineCount - 1);
			break;
		}
		if (!mappedInChangeGroup) targetLine = mappedFileCompareLineForRole(fileCompareHunks, sourceRole, sourceLine);
	}
	const std::size_t targetDocumentLines = std::max<std::size_t>(1, targetEditor.bufferModel().lineCount());
	return std::min(targetLine, targetDocumentLines - 1);
}

const MRBentoBox::FileCompareChangeGroup *MRBentoBox::fileCompareChangeGroupAtOrVisibleForRole(MRBentoPaneRole role, const MRFileEditor &editor, bool editablePanes) const noexcept {
	if (!bentoRoleIsDiff(role)) return nullptr;

	const int cursorGroupIndex = fileCompareChangeGroupIndexAtCursor(role, editor, editablePanes);
	if (cursorGroupIndex >= 0) return &fileCompareChangeGroups[static_cast<std::size_t>(cursorGroupIndex)];

	const std::size_t visibleStartLine = static_cast<std::size_t>(std::max(0, editor.delta.y));
	const std::size_t visibleEndLine = visibleStartLine + static_cast<std::size_t>(std::max(1, editor.visibleViewportRows()));
	for (const FileCompareChangeGroup &group : fileCompareChangeGroups) {
		const std::size_t groupStart = fileCompareGroupStartLineForRole(group, role, editablePanes);
		const std::size_t groupEnd = groupStart + fileCompareGroupLineCountForRole(group, role, editablePanes);
		if (groupEnd > visibleStartLine && groupStart < visibleEndLine) return &group;
	}
	return nullptr;
}

int MRBentoBox::fileCompareChangeGroupIndexAtCursor(MRBentoPaneRole role, const MRFileEditor &editor, bool editablePanes) const noexcept {
	if (!bentoRoleIsDiff(role)) return -1;

	const std::size_t cursorLine = editor.lineIndexOfOffset(editor.cursorOffset());
	return fileCompareChangeGroupIndexAtLine(role, cursorLine, editablePanes);
}

int MRBentoBox::fileCompareChangeGroupIndexAtLine(MRBentoPaneRole role, std::size_t line, bool editablePanes) const noexcept {
	if (!bentoRoleIsDiff(role)) return -1;

	for (std::size_t i = 0; i < fileCompareChangeGroups.size(); ++i) {
		const FileCompareChangeGroup &group = fileCompareChangeGroups[i];
		const std::size_t groupStart = fileCompareGroupStartLineForRole(group, role, editablePanes);
		std::size_t groupLineCount = fileCompareGroupLineCountForRole(group, role, editablePanes);
		if (editablePanes && role == bprDiffCompare && group.deletedLineCount > group.insertedLineCount) groupLineCount = std::max<std::size_t>(groupLineCount, group.insertedLineCount + 1);
		if (editablePanes && role == bprDiffOriginal && group.insertedLineCount > group.deletedLineCount) groupLineCount = std::max<std::size_t>(groupLineCount, group.deletedLineCount + 1);
		const std::size_t groupEnd = groupStart + groupLineCount;
		if (line >= groupStart && line < groupEnd) return static_cast<int>(i);
	}
	return -1;
}

bool MRBentoBox::moveFileCompareEditorToGroup(MRFileEditor &editor, MRBentoPaneRole role, const FileCompareChangeGroup &group, bool editablePanes) {
	const std::size_t documentLineCount = std::max<std::size_t>(1, editor.bufferModel().lineCount());
	std::size_t targetLine = fileCompareGroupNavigationLineForRole(group, role, editor, editablePanes);

	if (targetLine >= documentLineCount) targetLine = documentLineCount - 1;

	editor.moveCursorToDocumentLineTop(targetLine, 0);
	return true;
}

std::string MRBentoBox::fileCompareStatusForLeaf(const BentoLeaf &leaf) const {
	if (bentoMode != bbmFileCompare || !bentoRoleIsDiff(leaf.role)) return std::string();
	if (fileCompareStale) return "stale";
	if (!fileCompareDiffReady) return fileCompareTaskId != 0 ? std::string("comparing") : std::string();

	const MREditWindow *targetWindow = leaf.id == 0 ? static_cast<const MREditWindow *>(this) : static_cast<const MREditWindow *>(leaf.pane);
	MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
	if (targetEditor == nullptr || fileCompareChangeGroups.empty()) return std::string();

	const std::size_t visibleStartLine = static_cast<std::size_t>(std::max(0, targetEditor->delta.y));
	const std::size_t visibleLineCount = static_cast<std::size_t>(std::max(1, targetEditor->visibleViewportRows()));
	const std::size_t visibleEndLine = visibleStartLine + visibleLineCount;
	std::size_t firstVisibleChange = 0;
	std::size_t lastVisibleChange = 0;
	std::size_t visibleDeletedLines = 0;
	std::size_t visibleInsertedLines = 0;
	std::size_t totalDeletedLines = 0;
	std::size_t totalInsertedLines = 0;
	std::size_t activeChange = 0;
	std::size_t activeDeletedLines = 0;
	std::size_t activeInsertedLines = 0;
	std::size_t activeDisplayStartLine = 0;
	bool hasVisibleChange = false;
	bool hasActiveChange = false;

	std::size_t displayLine = 0;
	std::size_t currentChange = 0;
	bool groupOpen = false;

	const bool editablePanes = fileComparePanesEditable();
	const FileCompareChangeGroup *activeGroup = fileCompareChangeGroupAtOrVisibleForRole(leaf.role, *targetEditor, editablePanes);
	if (activeGroup != nullptr) {
		activeChange = static_cast<std::size_t>(activeGroup - fileCompareChangeGroups.data()) + 1;
		activeDeletedLines = activeGroup->deletedLineCount;
		activeInsertedLines = activeGroup->insertedLineCount;
		activeDisplayStartLine = fileCompareGroupStartLineForRole(*activeGroup, leaf.role, editablePanes);
		hasActiveChange = true;
	}

	for (const mr::diff::MRDiffHunk &hunk : fileCompareHunks) {
		switch (hunk.op) {
			case mr::diff::MRDiffOp::Equal:
				displayLine += hunk.count;
				groupOpen = false;
				break;
			case mr::diff::MRDiffOp::Delete:
			case mr::diff::MRDiffOp::Insert: {
				if (!groupOpen) {
					++currentChange;
					groupOpen = true;
				}

				const std::size_t hunkStartLine = displayLine;
				const std::size_t hunkEndLine = hunkStartLine + hunk.count;
				const bool hunkVisible = hunkEndLine > visibleStartLine && hunkStartLine < visibleEndLine;

				if (hunk.op == mr::diff::MRDiffOp::Delete)
					totalDeletedLines += hunk.count;
				else
					totalInsertedLines += hunk.count;
				if (hunkVisible) {
					const std::size_t visibleHunkStartLine = std::max(hunkStartLine, visibleStartLine);
					const std::size_t visibleHunkEndLine = std::min(hunkEndLine, visibleEndLine);
					const std::size_t visibleHunkLineCount = visibleHunkEndLine - visibleHunkStartLine;

					if (!hasVisibleChange) {
						firstVisibleChange = currentChange;
						hasVisibleChange = true;
					}
					lastVisibleChange = currentChange;
					if (hunk.op == mr::diff::MRDiffOp::Delete)
						visibleDeletedLines += visibleHunkLineCount;
					else
						visibleInsertedLines += visibleHunkLineCount;
				}
				displayLine = hunkEndLine;
				break;
			}
			default:
				break;
		}
	}
	if (hasVisibleChange || hasActiveChange) {
		std::string status;

		if (hasVisibleChange) {
			if (firstVisibleChange == lastVisibleChange)
				status += std::to_string(firstVisibleChange);
			else
				status += std::to_string(firstVisibleChange) + "-" + std::to_string(lastVisibleChange);
			status += "/" + std::to_string(fileCompareChangeGroups.size());
			status += " -" + std::to_string(visibleDeletedLines) + "|+" + std::to_string(visibleInsertedLines);
			status += " -" + std::to_string(totalDeletedLines) + "|+" + std::to_string(totalInsertedLines);
		}
		if (hasActiveChange) {
			if (!status.empty()) status += " ";
			status += "@" + std::to_string(activeChange) + "/" + std::to_string(fileCompareChangeGroups.size());
			status += " -" + std::to_string(activeDeletedLines) + "|+" + std::to_string(activeInsertedLines);
			status += " L" + std::to_string(activeDisplayStartLine + 1);
		}
		return status;
	}
	return std::string();
}

bool MRBentoBox::jumpToFileCompareChange(bool next) {
	if (bentoMode != bbmFileCompare || fileCompareChangeGroups.empty()) return false;
	const MRBentoPaneRole activeRole = roleForLeaf(activeLeafId);
	if (!bentoRoleIsDiff(activeRole)) return false;

	MREditWindow *activeWindow = activeLeafId == 0 ? static_cast<MREditWindow *>(this) : static_cast<MREditWindow *>(paneWindowForLeaf(activeLeafId));
	MRFileEditor *activeEditor = activeWindow != nullptr ? activeWindow->getEditor() : nullptr;
	if (activeEditor == nullptr) return false;

	const bool editablePanes = fileComparePanesEditable();
	const int cursorGroupIndex = fileCompareChangeGroupIndexAtCursor(activeRole, *activeEditor, editablePanes);
	std::size_t targetIndex = 0;

	if (cursorGroupIndex >= 0) {
		const std::size_t currentIndex = static_cast<std::size_t>(cursorGroupIndex);
		targetIndex = next ? (currentIndex + 1) % fileCompareChangeGroups.size() : (currentIndex == 0 ? fileCompareChangeGroups.size() - 1 : currentIndex - 1);
	} else {
		const std::size_t cursorLine = activeEditor->lineIndexOfOffset(activeEditor->cursorOffset());
		bool targetFound = false;

		if (next) {
			for (std::size_t i = 0; i < fileCompareChangeGroups.size(); ++i) {
				const std::size_t groupLine = fileCompareGroupNavigationLineForRole(fileCompareChangeGroups[i], activeRole, *activeEditor, editablePanes);
				if (groupLine > cursorLine) {
					targetIndex = i;
					targetFound = true;
					break;
				}
			}
			if (!targetFound) targetIndex = 0;
		} else {
			for (std::size_t i = fileCompareChangeGroups.size(); i > 0; --i) {
				const std::size_t groupLine = fileCompareGroupNavigationLineForRole(fileCompareChangeGroups[i - 1], activeRole, *activeEditor, editablePanes);
				if (groupLine < cursorLine) {
					targetIndex = i - 1;
					targetFound = true;
					break;
				}
			}
			if (!targetFound) targetIndex = fileCompareChangeGroups.size() - 1;
		}
	}

	if (!moveFileCompareEditorToGroup(*activeEditor, activeRole, fileCompareChangeGroups[targetIndex], editablePanes)) return false;
	if (activeWindow != nullptr) activeWindow->drawView();
	syncFileCompareLinkedPaneFrom(activeLeafId);
	return true;
}

bool MRBentoBox::navigateFileCompareChange(bool next) {
	if (!jumpToFileCompareChange(next)) return false;
	bentoProjectionDirty |= bpdContent | bpdChrome | bpdScrollBar | bpdOverlay;
	flushBentoProjection();
	return true;
}

bool MRBentoBox::applyFileCompareChange(bool originalToCompare) {
	if (bentoMode != bbmFileCompare || !fileCompareDiffReady || fileCompareStale || fileCompareChangeGroups.empty() || !fileComparePanesEditable()) return false;
	const MRBentoPaneRole activeRole = roleForLeaf(activeLeafId);
	if (!bentoRoleIsDiff(activeRole)) return false;

	MREditWindow *activeWindow = activeLeafId == 0 ? static_cast<MREditWindow *>(this) : static_cast<MREditWindow *>(paneWindowForLeaf(activeLeafId));
	MRFileEditor *activeEditor = activeWindow != nullptr ? activeWindow->getEditor() : nullptr;
	if (activeEditor == nullptr) return false;

	const FileCompareChangeGroup *activeGroup = fileCompareChangeGroupAtOrVisibleForRole(activeRole, *activeEditor, true);
	if (activeGroup == nullptr) return false;

	return applyFileCompareChangeGroup(originalToCompare, *activeGroup);
}

bool MRBentoBox::applyFileCompareChangeGroup(bool originalToCompare, const FileCompareChangeGroup &group) {
	if (bentoMode != bbmFileCompare || !fileCompareDiffReady || fileCompareStale || fileCompareChangeGroups.empty() || !fileComparePanesEditable()) return false;

	std::vector<std::string> originalLines;
	std::vector<std::string> compareLines;
	mr::diff::mrSplitTextLinesForDiff(fileCompareSetup.original.text, originalLines);
	mr::diff::mrSplitTextLinesForDiff(fileCompareSetup.compare.text, compareLines);

	const MRBentoPaneRole targetRole = originalToCompare ? bprDiffCompare : bprDiffOriginal;
	const int targetLeafId = leafIdForRole(targetRole);
	MREditWindow *targetWindow = nullptr;
	if (targetLeafId == 0)
		targetWindow = this;
	else if (targetLeafId > 0)
		targetWindow = paneWindowForLeaf(targetLeafId);
	if (targetWindow == nullptr) targetWindow = findEditWindowByBufferId(originalToCompare ? fileCompareSetup.compare.bufferId : fileCompareSetup.original.bufferId);
	MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
	if (targetEditor == nullptr) return false;

	const std::vector<std::string> &sourceLines = originalToCompare ? originalLines : compareLines;
	const std::size_t sourceStartLine = originalToCompare ? group.originalStartLine : group.compareStartLine;
	const std::size_t sourceLineCount = originalToCompare ? group.deletedLineCount : group.insertedLineCount;
	const std::size_t targetStartLine = originalToCompare ? group.compareStartLine : group.originalStartLine;
	const std::size_t targetLineCount = originalToCompare ? group.insertedLineCount : group.deletedLineCount;
	if (sourceLineCount == 0 && targetLineCount == 0) return false;

	std::size_t rangeStart = 0;
	std::size_t rangeEnd = 0;
	if (!fileCompareEditorLineRange(*targetEditor, targetStartLine, targetLineCount, rangeStart, rangeEnd)) return false;

	const MRTextBufferModel &targetModel = targetEditor->bufferModel();
	const bool prefixNewline = sourceLineCount > 0 && targetLineCount == 0 && rangeStart == targetModel.length() && rangeStart > 0 && targetModel.charAt(rangeStart - 1) != '\n';
	const bool suffixNewline = sourceLineCount > 0 && rangeEnd < targetModel.length();
	const std::string replacement = fileCompareJoinedLineRange(sourceLines, sourceStartLine, sourceLineCount, prefixNewline, suffixNewline);
	const std::size_t uintMax = static_cast<std::size_t>(std::numeric_limits<unsigned int>::max());
	if (rangeStart > uintMax || rangeEnd > uintMax || replacement.size() > uintMax) return false;

	if (!targetEditor->replaceRangeAndSelect(static_cast<uint>(rangeStart), static_cast<uint>(rangeEnd), replacement.data(), static_cast<uint>(replacement.size()))) return false;
	const std::size_t selectionEnd = std::min<std::size_t>(rangeStart + replacement.size(), targetEditor->bufferModel().length());
	targetEditor->setSelectionOffsets(selectionEnd, selectionEnd, False);
	if (targetWindow != nullptr) targetWindow->setFileChanged(targetEditor->isDocumentModified());
	refreshFileCompareAfterSourceMutation();
	if (targetLeafId >= 0) syncFileCompareLinkedPaneFrom(targetLeafId);
	bentoProjectionDirty |= bpdContent | bpdChrome | bpdScrollBar | bpdOverlay;
	flushBentoProjection();
	return true;
}

std::string MRBentoBox::fileCompareTextForRole(MRBentoPaneRole role, std::vector<unsigned char> *lineKinds) const {
	if (role != bprDiffOriginal && role != bprDiffCompare) return std::string();
	if (lineKinds != nullptr) lineKinds->clear();
	if (!fileCompareDiffReady) {
		return role == bprDiffOriginal ? fileCompareSetup.original.text : fileCompareSetup.compare.text;
	}

	std::vector<std::string> originalLines;
	std::vector<std::string> compareLines;
	std::string text;
	std::size_t displayLineCount = 0;

	mr::diff::mrSplitTextLinesForDiff(fileCompareSetup.original.text, originalLines);
	mr::diff::mrSplitTextLinesForDiff(fileCompareSetup.compare.text, compareLines);
	for (const mr::diff::MRDiffHunk &hunk : fileCompareHunks) {
		switch (hunk.op) {
			case mr::diff::MRDiffOp::Equal:
				for (std::size_t i = 0; i < hunk.count; ++i) {
					const std::size_t originalIndex = hunk.leftStart + i;
					const std::size_t compareIndex = hunk.rightStart + i;
					if (role == bprDiffOriginal && originalIndex < originalLines.size()) appendDiffDisplayLine(text, lineKinds, displayLineCount, originalLines[originalIndex], mrfclkEqual);
					if (role == bprDiffCompare && compareIndex < compareLines.size()) appendDiffDisplayLine(text, lineKinds, displayLineCount, compareLines[compareIndex], mrfclkEqual);
				}
				break;
			case mr::diff::MRDiffOp::Delete:
				for (std::size_t i = 0; i < hunk.count; ++i) {
					const std::size_t originalIndex = hunk.leftStart + i;
					if (role == bprDiffOriginal && originalIndex < originalLines.size()) appendDiffDisplayLine(text, lineKinds, displayLineCount, originalLines[originalIndex], mrfclkMissing);
					if (role == bprDiffCompare) appendDiffDisplayLine(text, lineKinds, displayLineCount, std::string(), mrfclkOffset);
				}
				break;
			case mr::diff::MRDiffOp::Insert:
				for (std::size_t i = 0; i < hunk.count; ++i) {
					const std::size_t compareIndex = hunk.rightStart + i;
					if (role == bprDiffOriginal) appendDiffDisplayLine(text, lineKinds, displayLineCount, std::string(), mrfclkOffset);
					if (role == bprDiffCompare && compareIndex < compareLines.size()) appendDiffDisplayLine(text, lineKinds, displayLineCount, compareLines[compareIndex], mrfclkInsert);
				}
				break;
			default:
				break;
		}
	}
	return text;
}

void MRBentoBox::fileCompareEditableLineKindsForRole(MRBentoPaneRole role, std::vector<unsigned char> &lineKinds, std::vector<MRFileCompareMiniMapSlice> *miniMapSlices) const {
	lineKinds.clear();
	if (miniMapSlices != nullptr) miniMapSlices->clear();
	if (role != bprDiffOriginal && role != bprDiffCompare) return;

	std::vector<std::string> originalLines;
	std::vector<std::string> compareLines;

	mr::diff::mrSplitTextLinesForDiff(fileCompareSetup.original.text, originalLines);
	mr::diff::mrSplitTextLinesForDiff(fileCompareSetup.compare.text, compareLines);
	lineKinds.assign(role == bprDiffOriginal ? originalLines.size() : compareLines.size(), mrfclkEqual);
	if (!fileCompareDiffReady) return;

	bool groupOpen = false;
	std::size_t groupOriginalStart = 0;
	std::size_t groupCompareStart = 0;
	std::size_t groupDeletedLineCount = 0;
	std::size_t groupInsertedLineCount = 0;
	auto appendFullMiniMapSlice = [&](std::size_t lineIndex, unsigned char lineKind) {
		if (miniMapSlices == nullptr || lineIndex >= lineKinds.size()) return;
		miniMapSlices->push_back(MRFileCompareMiniMapSlice{lineIndex, 0, 0, lineKind, true});
	};
	auto appendChangedMiniMapSlice = [&](std::size_t lineIndex, const std::string &baseLine, const std::string &changedLine, unsigned char lineKind) {
		if (miniMapSlices == nullptr || lineIndex >= lineKinds.size()) return;
		std::size_t prefix = 0;
		const std::size_t commonLimit = std::min(baseLine.size(), changedLine.size());
		while (prefix < commonLimit && baseLine[prefix] == changedLine[prefix])
			++prefix;
		std::size_t suffix = 0;
		while (suffix < commonLimit - prefix && baseLine[baseLine.size() - 1 - suffix] == changedLine[changedLine.size() - 1 - suffix])
			++suffix;
		std::size_t sliceStart = std::min(prefix, changedLine.size());
		std::size_t sliceEnd = changedLine.size() >= suffix ? changedLine.size() - suffix : changedLine.size();
		if (sliceEnd <= sliceStart && !changedLine.empty()) {
			sliceStart = sliceStart >= changedLine.size() ? changedLine.size() - 1 : sliceStart;
			sliceEnd = sliceStart + 1;
		}
		miniMapSlices->push_back(MRFileCompareMiniMapSlice{lineIndex, sliceStart, sliceEnd, lineKind, changedLine.empty()});
	};
	auto flushGroup = [&]() {
		if (!groupOpen) return;
		const bool replaceGroup = groupDeletedLineCount > 0 && groupInsertedLineCount > 0;

		if (role == bprDiffOriginal) {
			if (groupDeletedLineCount > 0) {
				markFileCompareLineRange(lineKinds, groupOriginalStart, groupDeletedLineCount, mrfclkMissing);
				for (std::size_t i = 0; i < groupDeletedLineCount && groupOriginalStart + i < lineKinds.size(); ++i) {
					const std::size_t originalIndex = groupOriginalStart + i;
					if (replaceGroup && i < groupInsertedLineCount && originalIndex < originalLines.size() && groupCompareStart + i < compareLines.size())
						appendChangedMiniMapSlice(originalIndex, compareLines[groupCompareStart + i], originalLines[originalIndex], mrfclkMissing);
					else
						appendFullMiniMapSlice(originalIndex, mrfclkMissing);
				}
			} else if (groupInsertedLineCount > 0) {
				markFileCompareAnchorLine(lineKinds, groupOriginalStart, mrfclkInsert);
				appendFullMiniMapSlice(std::min(groupOriginalStart, lineKinds.empty() ? 0 : lineKinds.size() - 1), mrfclkInsert);
			}
		} else {
			if (replaceGroup) {
				for (std::size_t i = 0; i < groupInsertedLineCount && groupCompareStart + i < lineKinds.size(); ++i) {
					const std::size_t originalLength = fileCompareLineTextLength(originalLines, groupOriginalStart + i, 1);
					const std::size_t compareLength = fileCompareLineTextLength(compareLines, groupCompareStart + i, 1);
					const unsigned char lineKind = compareLength < originalLength ? mrfclkMissing : mrfclkInsert;
					lineKinds[groupCompareStart + i] = lineKind;
					if (i < groupDeletedLineCount && groupOriginalStart + i < originalLines.size() && groupCompareStart + i < compareLines.size())
						appendChangedMiniMapSlice(groupCompareStart + i, originalLines[groupOriginalStart + i], compareLines[groupCompareStart + i], lineKind);
					else
						appendFullMiniMapSlice(groupCompareStart + i, lineKind);
				}
				if (groupDeletedLineCount > groupInsertedLineCount) {
					const std::size_t anchorLine = std::min(groupCompareStart + groupInsertedLineCount, lineKinds.empty() ? 0 : lineKinds.size() - 1);
					markFileCompareAnchorLine(lineKinds, groupCompareStart + groupInsertedLineCount, mrfclkMissing);
					appendFullMiniMapSlice(anchorLine, mrfclkMissing);
				}
			} else if (groupInsertedLineCount > 0) {
				markFileCompareLineRange(lineKinds, groupCompareStart, groupInsertedLineCount, mrfclkInsert);
				for (std::size_t i = 0; i < groupInsertedLineCount && groupCompareStart + i < lineKinds.size(); ++i)
					appendFullMiniMapSlice(groupCompareStart + i, mrfclkInsert);
			} else if (groupDeletedLineCount > 0) {
				markFileCompareAnchorLine(lineKinds, groupCompareStart, mrfclkMissing);
				appendFullMiniMapSlice(std::min(groupCompareStart, lineKinds.empty() ? 0 : lineKinds.size() - 1), mrfclkMissing);
			}
		}
		groupOpen = false;
		groupDeletedLineCount = 0;
		groupInsertedLineCount = 0;
	};

	for (const mr::diff::MRDiffHunk &hunk : fileCompareHunks) {
		switch (hunk.op) {
			case mr::diff::MRDiffOp::Equal:
				flushGroup();
				break;
			case mr::diff::MRDiffOp::Delete:
				if (!groupOpen) {
					groupOpen = true;
					groupOriginalStart = hunk.leftStart;
					groupCompareStart = hunk.rightStart;
				}
				groupDeletedLineCount += hunk.count;
				break;
			case mr::diff::MRDiffOp::Insert:
				if (!groupOpen) {
					groupOpen = true;
					groupOriginalStart = hunk.leftStart;
					groupCompareStart = hunk.rightStart;
				}
				groupInsertedLineCount += hunk.count;
				break;
			default:
				break;
		}
	}
	flushGroup();
}

void MRBentoBox::refreshFileComparePane(BentoLeaf &leaf) {
	MREditWindow *targetWindow = leaf.id == 0 ? static_cast<MREditWindow *>(this) : static_cast<MREditWindow *>(leaf.pane);
	MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
	std::string title;
	std::string text;
	std::vector<unsigned char> lineKinds;
	std::vector<MRFileCompareMiniMapSlice> miniMapSlices;

	if (targetWindow == nullptr || !bentoRoleIsDiff(leaf.role)) return;
	leaf.spec = paneSpecForRole(leaf.role);
	if (leaf.id != 0 && leaf.pane != nullptr) leaf.pane->setPaneSpec(leaf.spec, getEditor());
	title = leaf.role == bprDiffOriginal ? diffDisplayTitle(fileCompareSetup.original, "Diff Original") : diffDisplayTitle(fileCompareSetup.compare, "Diff Compare");
	if (fileComparePanesEditable()) {
		MREditWindow *sourceWindow = findEditWindowByBufferId(leaf.role == bprDiffOriginal ? fileCompareSetup.original.bufferId : fileCompareSetup.compare.bufferId);
		MRFileEditor *sourceEditor = sourceWindow != nullptr ? sourceWindow->getEditor() : nullptr;
		if (targetEditor != nullptr && sourceEditor != nullptr && targetEditor->documentId() != sourceEditor->documentId()) targetEditor->shareContentStateFrom(*sourceEditor);
		const std::string leadingGutters = leaf.role == bprDiffOriginal ? configuredFileCompareOriginalLeadingGutters() : configuredFileCompareCompareLeadingGutters();
		const std::string trailingGutters = leaf.role == bprDiffOriginal ? configuredFileCompareOriginalTrailingGutters() : configuredFileCompareCompareTrailingGutters();
		const bool miniMapConfigured = fileCompareGuttersContain(leadingGutters, 'M') || fileCompareGuttersContain(trailingGutters, 'M');
		if (targetEditor != nullptr) fileCompareEditableLineKindsForRole(leaf.role, lineKinds, &miniMapSlices);
		if (leaf.id != 0 && leaf.pane != nullptr) leaf.pane->layoutPaneChrome();
		if (targetEditor != nullptr) {
			targetEditor->setMiniMapSuppressed(!miniMapConfigured);
			targetEditor->setFileCompareGutters(leadingGutters, trailingGutters);
			targetEditor->setFileCompareLineKinds(lineKinds, miniMapSlices);
			targetEditor->setFileCompareGutterVisible(true);
			targetEditor->updateMetrics();
			targetEditor->continueComputeWarmupIfNeeded("file-compare-edit-refresh");
		}
		targetWindow->setDisplayTitle(title.c_str());
		targetWindow->setReadOnly(false);
		leaf.title = bentoPaneRoleTitle(leaf.role);
		return;
	}
	text = fileCompareTextForRole(leaf.role, &lineKinds);
	if (fileCompareStale) {
		text = "[source changed while compare was running]\n\n" + text;
		lineKinds.insert(lineKinds.begin(), {mrfclkOffset, mrfclkOffset});
	}
	if (targetEditor != nullptr) {
		targetEditor->detachContentStateCopy();
		targetWindow->setCurrentFileName(nullptr);
	}
	static_cast<void>(targetWindow->replaceTextBuffer(text.c_str(), title.c_str()));
	if (leaf.id != 0 && leaf.pane != nullptr) leaf.pane->layoutPaneChrome();
	if (targetEditor != nullptr) {
		const std::string leadingGutters = leaf.role == bprDiffOriginal ? configuredFileCompareOriginalLeadingGutters() : configuredFileCompareCompareLeadingGutters();
		const std::string trailingGutters = leaf.role == bprDiffOriginal ? configuredFileCompareOriginalTrailingGutters() : configuredFileCompareCompareTrailingGutters();
		const bool miniMapConfigured = fileCompareGuttersContain(leadingGutters, 'M') || fileCompareGuttersContain(trailingGutters, 'M');

		targetEditor->setMiniMapSuppressed(!miniMapConfigured);
		targetEditor->setFileCompareGutters(leadingGutters, trailingGutters);
		targetEditor->setFileCompareLineKinds(lineKinds);
		targetEditor->setFileCompareGutterVisible(true);
		targetEditor->updateMetrics();
		targetEditor->continueComputeWarmupIfNeeded("file-compare-refresh");
	}
	targetWindow->setReadOnly(true);
	targetWindow->setFileChanged(false);
	leaf.title = bentoPaneRoleTitle(leaf.role);
}

void MRBentoBox::refreshFileComparePanes() {
	if (bentoMode != bbmFileCompare) return;
	for (BentoLeaf &leaf : leaves)
		if (leaf.visible) refreshFileComparePane(leaf);
	bentoProjectionDirty |= bpdContent | bpdChrome | bpdScrollBar | bpdOverlay;
	flushBentoProjection();
}

void MRBentoBox::syncFileCompareLinkedPaneFrom(int sourceLeafId, bool syncCursor) {
	if (bentoMode != bbmFileCompare) return;

	const MRBentoPaneRole sourceRole = roleForLeaf(sourceLeafId);
	if (!bentoRoleIsDiff(sourceRole)) return;
	const MRBentoPaneRole targetRole = sourceRole == bprDiffOriginal ? bprDiffCompare : bprDiffOriginal;
	const int targetLeafId = leafIdForRole(targetRole);
	if (targetLeafId < 0 || targetLeafId == sourceLeafId) return;

	MREditWindow *sourceWindow = sourceLeafId == 0 ? static_cast<MREditWindow *>(this) : static_cast<MREditWindow *>(paneWindowForLeaf(sourceLeafId));
	MREditWindow *targetWindow = targetLeafId == 0 ? static_cast<MREditWindow *>(this) : static_cast<MREditWindow *>(paneWindowForLeaf(targetLeafId));
	MRFileEditor *sourceEditor = sourceWindow != nullptr ? sourceWindow->getEditor() : nullptr;
	MRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
	if (sourceEditor == nullptr || targetEditor == nullptr) return;
	auto mappedTargetLine = [this, sourceRole, targetEditor](std::size_t sourceLine) {
		return fileCompareMappedLineForRole(sourceRole, sourceLine, *targetEditor, fileComparePanesEditable());
	};

	if (syncCursor) {
		const std::size_t sourceLine = sourceEditor->lineIndexOfOffset(sourceEditor->cursorOffset());
		const std::size_t targetLine = mappedTargetLine(sourceLine);
		const int targetLineDelta = static_cast<int>(std::min(targetLine, static_cast<std::size_t>(std::numeric_limits<int>::max())));
		const int visualColumn = sourceEditor->displayedCursorColumn();
		const std::size_t targetOffset = targetEditor->lineMoveOffset(0, targetLineDelta, visualColumn);

		targetEditor->setCursorOffsetAtVisualColumn(targetOffset, visualColumn);
	}
	{
		const std::size_t sourceScrollLine = static_cast<std::size_t>(std::max(0, sourceEditor->delta.y));
		const std::size_t targetScrollLine = mappedTargetLine(sourceScrollLine);
		const int targetScrollY = static_cast<int>(std::min(targetScrollLine, static_cast<std::size_t>(std::numeric_limits<int>::max())));

		targetEditor->scrollTo(std::max(0, sourceEditor->delta.x), targetScrollY);
	}
	targetEditor->refreshViewState();
	if (targetWindow != nullptr) targetWindow->drawView();
}

bool MRBentoBox::applyFileCompareResult(const mr::coprocessor::Result &result) {
	if (bentoMode != bbmFileCompare || fileCompareTaskId == 0 || result.task.id != fileCompareTaskId) return false;
	releaseCoprocessorTask(result.task.id);
	fileCompareTaskId = 0;

	if (result.failed()) {
		mrLogMessage((std::string("File compare failed: ") + result.error).c_str());
		return true;
	}
	if (result.cancelled()) return true;

	const mr::coprocessor::FileComparePayload *payload = dynamic_cast<const mr::coprocessor::FileComparePayload *>(result.payload.get());
	if (payload == nullptr) {
		mrLogMessage("File compare result discarded: missing payload.");
		return true;
	}
	if (payload->originalDocumentId != fileCompareSetup.original.documentId || payload->originalBaseVersion != fileCompareSetup.original.version || payload->compareDocumentId != fileCompareSetup.compare.documentId || payload->compareBaseVersion != fileCompareSetup.compare.version || !fileCompareSourceStillMatches(fileCompareSetup.original) || !fileCompareSourceStillMatches(fileCompareSetup.compare)) {
		fileCompareStale = true;
		fileCompareChangeGroups.clear();
		refreshFileComparePanes();
		mrLogMessage("File compare result discarded: source document changed.");
		return true;
	}

	std::vector<std::string> originalLines;
	std::vector<std::string> compareLines;
	mr::diff::mrSplitTextLinesForDiff(fileCompareSetup.original.text, originalLines);
	mr::diff::mrSplitTextLinesForDiff(fileCompareSetup.compare.text, compareLines);
	fileCompareHunks = payload->hunks;
	normalizeFileCompareHunks(originalLines, compareLines, fileCompareHunks);
	rebuildFileCompareChangeGroups();
	fileCompareDiffReady = true;
	fileCompareStale = false;
	refreshFileComparePanes();
	return true;
}

TRect MRBentoBox::paneLayoutBounds() const noexcept {
	TRect inner = getExtent();

	if (!(fullscreenPresentation() && hasPaneSplit())) inner.grow(-1, -1);
	return inner;
}

TRect MRBentoBox::nodeBounds(int nodeIndex) const noexcept {
	TRect inner = paneLayoutBounds();
	if (nodeIndex == rootNode) return inner;
	for (const BentoLeaf &leaf : leaves)
		if (nodeIndexForLeaf(leaf.id) == nodeIndex) return leaf.bounds;
	int parent = parentNodeOf(nodeIndex);
	if (parent < 0) return inner;
	TRect parentBounds = nodeBounds(parent);
	const int position = currentDividerPosition(parent);
	if (layoutTree[parent].orientation == bsoVertical) {
		if (layoutTree[parent].firstChild == nodeIndex) return TRect(parentBounds.a.x, parentBounds.a.y, position, parentBounds.b.y);
		return TRect(position, parentBounds.a.y, parentBounds.b.x, parentBounds.b.y);
	}
	if (layoutTree[parent].firstChild == nodeIndex) return TRect(parentBounds.a.x, parentBounds.a.y, parentBounds.b.x, position);
	return TRect(parentBounds.a.x, position, parentBounds.b.x, parentBounds.b.y);
}

TRect MRBentoBox::contentBounds(const TRect &paneBounds) const noexcept {
	TRect content = paneBounds;
	if (content.b.x - content.a.x > 2 && content.b.y - content.a.y > 2) content.grow(-1, -1);
	return content;
}

int MRBentoBox::createToolLeaf(MRBentoPaneRole role) {
	return createPaneLeaf(paneSpecForRole(role));
}

int MRBentoBox::createPaneLeaf(const MRBentoPaneSpec &spec) {
	BentoLeaf leaf;
	leaf.id = nextLeafId++;
	leaf.role = spec.role;
	leaf.spec = spec;
	leaf.title = bentoMode == bbmDocumentViewports ? "" : bentoPaneRoleTitle(spec.role);
	const std::string initialTitle = bentoMode == bbmDocumentViewports ? "Viewport" : leaf.title;
	leaf.pane = new MRPaneEditWindow(TRect(0, 0, 1, 1), initialTitle.c_str(), number);
	leaf.pane->setPaneSpec(spec, getEditor());
	leaf.visible = true;
	if (secondaryPane == nullptr) secondaryPane = leaf.pane;
	leaves.push_back(leaf);
	insert(leaf.pane);
	return leaf.id;
}

int MRBentoBox::createLeafNode(int leafId) {
	BentoLayoutNode node;
	node.kind = blnPane;
	node.leafId = leafId;
	layoutTree.push_back(node);
	return static_cast<int>(layoutTree.size()) - 1;
}

int MRBentoBox::splitLeafNode(int leafId, BentoSplitOrientation orientation, MRBentoPaneRole newRole) {
	return splitLeafNode(leafId, orientation, paneSpecForRole(newRole));
}

int MRBentoBox::splitLeafNode(int leafId, BentoSplitOrientation orientation, const MRBentoPaneSpec &spec) {
	int targetNode = nodeIndexForLeaf(leafId);
	if (targetNode < 0) return -1;
	int newLeaf = createPaneLeaf(spec);
	int existingLeafNode = createLeafNode(leafId);
	int newLeafNode = createLeafNode(newLeaf);
	BentoLayoutNode &target = layoutTree[targetNode];
	target.kind = blnSplit;
	target.orientation = orientation;
	target.firstChild = existingLeafNode;
	target.secondChild = newLeafNode;
	target.leafId = -1;
	target.dividerPosition = defaultDividerPosition(targetNode);
	setActivePane(newLeaf);
	layoutSplitPanes();
	return newLeaf;
}

void MRBentoBox::collapseLeafNode(int leafId) noexcept {
	int leafNode = nodeIndexForLeaf(leafId);
	if (leafNode < 0) return;
	int parent = parentNodeOf(leafNode);
	if (parent < 0) return;
	int survivor = layoutTree[parent].firstChild == leafNode ? layoutTree[parent].secondChild : layoutTree[parent].firstChild;
	layoutTree[parent] = layoutTree[survivor];
	for (BentoLeaf &leaf : leaves) {
		if (leaf.id == leafId) {
			leaf.visible = false;
			if (leaf.pane != nullptr) leaf.pane->hide();
		}
	}
	if (secondaryPane != nullptr) {
		bool stillVisible = false;
		for (const BentoLeaf &leaf : leaves)
			if (leaf.pane == secondaryPane && leaf.visible) stillVisible = true;
		if (!stillVisible) secondaryPane = paneWindowForLeaf(firstToolLeafId());
	}
	if (activeLeafId == leafId) setActivePane(0);
	if (maximizedLeafId == leafId) maximizedLeafId = -1;
}

std::vector<std::string> MRBentoBox::paneRoleChoices() const {
	std::vector<std::string> choices;

	for (const BentoPaneRoleDescriptor &descriptor : kBentoPaneRoles) {
		if (!descriptor.listed) continue;
		if (bentoMode == bbmFileCompare && !bentoRoleIsDiff(descriptor.role)) continue;
		if (bentoMode != bbmFileCompare && bentoRoleIsDiff(descriptor.role)) continue;
		choices.push_back(descriptor.title);
	}
	return choices;
}

std::vector<std::string> MRBentoBox::paneActionChoices() const {
	std::vector<std::string> choices;

	for (const BentoPaneActionDescriptor &descriptor : kBentoPaneActions)
		choices.push_back(descriptor.action);
	return choices;
}

MRBentoPanePlacement MRBentoBox::panePlacementForAction(const std::string &action) const noexcept {
	for (const BentoPaneActionDescriptor &descriptor : kBentoPaneActions)
		if (action == descriptor.action) return descriptor.placement;
	return bppReplace;
}

TFrame *MRBentoBox::initFrame(TRect bounds) {
	return new MRFrame(bounds);
}
