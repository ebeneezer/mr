#define Uses_TDeskTop
#include "MRBentoBox.hpp"

#include "MRFrame.hpp"
#include "MRSidekickEditor.hpp"
#include "MRWindowSupport.hpp"

#include "../config/settings/MRSettingsRuntime.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <sstream>
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
static const char *kPaneCloseIcon = "[\xFE]";
static const char *kPaneMaximizeIcon = "[▴]";
static const char *kPaneRestoreIcon = "[▾]";
static const char *kBentoPaneActionReplace = "replace";
static const char *kBentoPaneActionSplitRight = "split \xC4";
static const char *kBentoPaneActionSplitDown = "split \xB3";
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
	{bprSplitEditor, "Split editor", true},
};

const char *bentoPaneRoleTitle(MRBentoPaneRole role) noexcept {
	for (const BentoPaneRoleDescriptor &descriptor : kBentoPaneRoles)
		if (descriptor.role == role) return descriptor.title;
	return "Pane";
}

MRBentoPaneRole bentoPaneRoleForTitle(const std::string &title) {
	for (const BentoPaneRoleDescriptor &descriptor : kBentoPaneRoles)
		if (descriptor.listed && title == descriptor.title) return descriptor.role;
	return bprCompilerOutput;
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

class MRPaneFrame : public MRFrame {
  public:
	explicit MRPaneFrame(const TRect &bounds) noexcept : MRFrame(bounds) {
		eventMask = 0;
	}

	virtual void draw() override {
	}

	virtual void handleEvent(TEvent &) override {
	}
};

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

std::string pathBaseName(const std::string &path) {
	std::filesystem::path fsPath(path);
	std::string base = fsPath.filename().string();

	return base.empty() ? path : base;
}

std::string normalizedDiagnosticPath(const std::string &path) {
	std::string normalized = path;

	for (char &ch : normalized)
		if (ch == '\\') ch = '/';
	normalized = std::filesystem::path(normalized).lexically_normal().generic_string();
	if (normalized.rfind("./", 0) == 0) normalized.erase(0, 2);
	return normalized;
}

bool pathSuffixMatches(const std::string &candidatePath, const std::string &sourcePath) {
	if (candidatePath.size() > sourcePath.size()) return false;
	if (candidatePath == sourcePath) return true;
	if (sourcePath.size() <= candidatePath.size()) return false;
	if (sourcePath.compare(sourcePath.size() - candidatePath.size(), candidatePath.size(), candidatePath) != 0) return false;
	return sourcePath[sourcePath.size() - candidatePath.size() - 1] == '/';
}

bool compilerDiagnosticPathMatches(const std::string &candidatePath, const std::string &sourcePath) {
	const std::string candidate = normalizedDiagnosticPath(candidatePath);
	const std::string source = normalizedDiagnosticPath(sourcePath);

	if (candidatePath.empty() || sourcePath.empty()) return false;
	if (candidate == source || pathSuffixMatches(candidate, source)) return true;
	return pathBaseName(candidate) == pathBaseName(source);
}

bool parseUnsignedField(const std::string &text, std::size_t start, std::size_t end, std::size_t &value) {
	std::size_t parsed = 0;

	if (start >= end || std::isdigit(static_cast<unsigned char>(text[start])) == 0) return false;
	value = 0;
	while (start + parsed < end && std::isdigit(static_cast<unsigned char>(text[start + parsed])) != 0) {
		value = value * 10 + static_cast<std::size_t>(text[start + parsed] - '0');
		++parsed;
	}
	return parsed != 0 && start + parsed == end;
}

struct DiagnosticSeverityMarker {
	const char *marker;
	const char *severity;
};

bool parseCompilerDiagnosticLine(const std::string &line, const std::string &sourcePath, std::size_t outputOffset, MRCompilerDiagnostic &diagnostic) {
	static const DiagnosticSeverityMarker markers[] = {{": fatal error:", "fatal error"}, {": error:", "error"}, {": warning:", "warning"}, {": note:", "note"}};
	std::size_t markerPos = std::string::npos;
	const DiagnosticSeverityMarker *matchedMarker = nullptr;

	for (const DiagnosticSeverityMarker &marker : markers) {
		const std::size_t pos = line.find(marker.marker);
		if (pos != std::string::npos && (matchedMarker == nullptr || pos < markerPos)) {
			markerPos = pos;
			matchedMarker = &marker;
		}
	}
	if (matchedMarker == nullptr) return false;

	const std::string location = line.substr(0, markerPos);
	const std::size_t lastColon = location.rfind(':');
	std::size_t lineColon = std::string::npos;
	std::size_t sourceLine = 0;
	std::size_t sourceColumn = 1;

	if (lastColon == std::string::npos) return false;
	if (parseUnsignedField(location, lastColon + 1, location.size(), sourceColumn)) {
		lineColon = location.rfind(':', lastColon - 1);
		if (lineColon == std::string::npos) return false;
		if (!parseUnsignedField(location, lineColon + 1, lastColon, sourceLine)) return false;
	} else {
		lineColon = lastColon;
		if (!parseUnsignedField(location, lineColon + 1, location.size(), sourceLine)) return false;
		sourceColumn = 1;
	}
	if (sourceLine == 0 || sourceColumn == 0) return false;
	const std::string diagnosticPath = location.substr(0, lineColon);

	diagnostic.sourcePath = diagnosticPath;
	diagnostic.sourceLine = sourceLine;
	diagnostic.sourceColumn = sourceColumn;
	diagnostic.severity = matchedMarker->severity;
	diagnostic.text = line.substr(markerPos + std::strlen(matchedMarker->marker));
	while (!diagnostic.text.empty() && diagnostic.text.front() == ' ')
		diagnostic.text.erase(diagnostic.text.begin());
	diagnostic.sourceOffset = 0;
	diagnostic.outputOffset = outputOffset;
	diagnostic.problemOffset = 0;
	diagnostic.sourceAvailable = compilerDiagnosticPathMatches(diagnosticPath, sourcePath);
	return true;
}

bool parseCompilerDriverDiagnosticLine(const std::string &line, std::size_t outputOffset, MRCompilerDiagnostic &diagnostic) {
	static const DiagnosticSeverityMarker markers[] = {{"warning:", "warning"}, {"note:", "note"}};
	std::size_t markerPos = std::string::npos;
	const DiagnosticSeverityMarker *matchedMarker = nullptr;

	for (const DiagnosticSeverityMarker &marker : markers) {
		const std::size_t pos = line.find(marker.marker);
		if (pos != std::string::npos && (matchedMarker == nullptr || pos < markerPos)) {
			markerPos = pos;
			matchedMarker = &marker;
		}
	}
	if (matchedMarker == nullptr || markerPos == 0) return false;

	diagnostic.sourcePath = line.substr(0, markerPos);
	while (!diagnostic.sourcePath.empty() && diagnostic.sourcePath.back() == ' ')
		diagnostic.sourcePath.pop_back();
	if (!diagnostic.sourcePath.empty() && diagnostic.sourcePath.back() == ':') diagnostic.sourcePath.pop_back();
	while (!diagnostic.sourcePath.empty() && diagnostic.sourcePath.back() == ' ')
		diagnostic.sourcePath.pop_back();
	diagnostic.sourceLine = 0;
	diagnostic.sourceColumn = 0;
	diagnostic.severity = matchedMarker->severity;
	diagnostic.text = line.substr(markerPos + std::strlen(matchedMarker->marker));
	while (!diagnostic.text.empty() && diagnostic.text.front() == ' ')
		diagnostic.text.erase(diagnostic.text.begin());
	diagnostic.sourceOffset = 0;
	diagnostic.outputOffset = outputOffset;
	diagnostic.problemOffset = 0;
	diagnostic.sourceAvailable = false;
	return true;
}

bool compilerDiagnosticSeverityEnabled(const MRCompilerDiagnostic &diagnostic) {
	if (diagnostic.severity == "error" || diagnostic.severity == "fatal error") return true;
	if (diagnostic.severity == "warning") return configuredTrackCompilerWarnings();
	if (diagnostic.severity == "note") return configuredTrackCompilerNotes();
	return false;
}

std::size_t sourceOffsetForCompilerColumn(MRFileEditor *editor, std::size_t lineStart, std::size_t lineEnd, std::size_t column) {
	std::size_t offset = lineStart;

	for (std::size_t currentColumn = 1; currentColumn < column && offset < lineEnd; ++currentColumn)
		offset = editor->nextCharOffset(offset);
	return offset;
}

std::size_t lineColumnForOffset(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t lineStart, std::size_t offset) {
	return offset >= lineStart ? offset - lineStart + 1 : 1;
}

bool editTouchesDiagnosticLine(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t diagnosticOffset, std::size_t editStart, std::size_t editEnd) {
	const std::size_t lineStart = snapshot.lineStart(diagnosticOffset);
	const std::size_t lineEnd = snapshot.lineEnd(diagnosticOffset);

	if (editEnd > editStart) return editStart < lineEnd && editEnd > lineStart;
	return editStart >= lineStart && editStart <= lineEnd;
}

std::vector<MRCompilerDiagnostic> parseCompilerDiagnostics(const std::string &text, const std::string &sourcePath) {
	std::vector<MRCompilerDiagnostic> diagnostics;
	const std::size_t textLength = text.size();
	std::size_t lineStart = 0;

	while (lineStart < textLength) {
		std::size_t lineEnd = text.find('\n', lineStart);
		MRCompilerDiagnostic diagnostic;

		if (lineEnd == std::string::npos) lineEnd = textLength;
		const std::string line = text.substr(lineStart, lineEnd - lineStart);
		if (parseCompilerDiagnosticLine(line, sourcePath, lineStart, diagnostic) || parseCompilerDriverDiagnosticLine(line, lineStart, diagnostic)) diagnostics.push_back(std::move(diagnostic));
		if (lineEnd == textLength) break;
		lineStart = lineEnd + 1;
	}
	return diagnostics;
}

std::string compilerDiagnosticProblemLine(const MRCompilerDiagnostic &diagnostic) {
	std::ostringstream line;

	line << diagnostic.severity << " ";
	if (diagnostic.sourceLine == 0)
		line << (diagnostic.sourcePath.empty() ? "build" : diagnostic.sourcePath);
	else
		line << pathBaseName(diagnostic.sourcePath) << ":" << diagnostic.sourceLine << ":" << diagnostic.sourceColumn;
	if (!diagnostic.text.empty()) line << " " << diagnostic.text;
	return line.str();
}

std::string compilerDiagnosticDetailText(const MRCompilerDiagnostic &diagnostic) {
	std::ostringstream text;

	if (diagnostic.sourceLine == 0)
		text << diagnostic.severity << " from " << (diagnostic.sourcePath.empty() ? "build" : diagnostic.sourcePath) << "\n";
	else
		text << diagnostic.severity << " at " << diagnostic.sourcePath << ":" << diagnostic.sourceLine << ":" << diagnostic.sourceColumn << "\n";
	text << diagnostic.text;
	return text.str();
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

MRCompilerDiagnostic::MRCompilerDiagnostic() noexcept : sourcePath(), sourceLine(1), sourceColumn(1), severity(), text(), sourceOffset(0), outputOffset(0), problemOffset(0), sourceAvailable(false) {
}

MRPaneEditWindow::MRPaneEditWindow(const TRect &bounds, const char *title, int number) : TWindowInit(&MRPaneEditWindow::initFrame), MREditWindow(bounds, title, number), mPaneSpec(), mPaneFocused(false) {
	flags = 0;
	state &= static_cast<ushort>(~sfShadow);
	options &= static_cast<ushort>(~(ofTileable | ofTopSelect));
	eventMask = 0;
	layoutPaneChrome();
}

MRPaneEditWindow::~MRPaneEditWindow() {
}

bool MRPaneEditWindow::paneOwned() const noexcept {
	return true;
}

MRBentoPaneRole MRPaneEditWindow::paneRole() const noexcept {
	return mPaneSpec.role;
}

void MRPaneEditWindow::changeBounds(const TRect &bounds) {
	MREditWindow::changeBounds(bounds);
	layoutPaneChrome();
}

void MRPaneEditWindow::draw() {
	MREditWindow::draw();
	drawPaneScrollBars();
}

TColorAttr MRPaneEditWindow::mapColor(uchar index) {
	if (index == 4 || index == 5) return MREditWindow::mapColor(mPaneFocused ? 13 : 1);
	return MREditWindow::mapColor(index);
}

Boolean MRPaneEditWindow::valid(ushort command) {
	if (command == cmClose) return True;
	return MREditWindow::valid(command);
}

void MRPaneEditWindow::setPaneSpec(const MRBentoPaneSpec &spec, const MRFileEditor *sourceEditor) noexcept {
	const bool sameSpec = mPaneSpec.role == spec.role && mPaneSpec.bufferPolicy == spec.bufferPolicy && mPaneSpec.readOnly == spec.readOnly && mPaneSpec.suppressMiniMap == spec.suppressMiniMap && mPaneSpec.suppressWordWrap == spec.suppressWordWrap && mPaneSpec.scrollBarsAlwaysVisible == spec.scrollBarsAlwaysVisible && mPaneSpec.titleMenu == spec.titleMenu;
	const MRBentoPaneBufferPolicy oldBufferPolicy = mPaneSpec.bufferPolicy;
	mPaneSpec = spec;
	if (oldBufferPolicy == bpbSharedSourceBuffer && spec.bufferPolicy == bpbOwnBuffer && getEditor() != nullptr) getEditor()->detachContentStateCopy();
	applyPanePolicy(spec.bufferPolicy == bpbSharedSourceBuffer ? sourceEditor : nullptr);
	if (sameSpec) return;
	layoutPaneChrome();
}

void MRPaneEditWindow::setPaneFocused(bool focused) noexcept {
	if (mPaneFocused == focused) return;
	mPaneFocused = focused;
	drawPaneScrollBars();
}

void MRPaneEditWindow::applyPanePolicy(const MRFileEditor *sourceEditor) noexcept {
	MRFileEditor *paneEditor = getEditor();

	if (paneEditor == nullptr) return;
	if (mPaneSpec.bufferPolicy == bpbSharedSourceBuffer && sourceEditor != nullptr) paneEditor->shareContentStateFrom(*sourceEditor);
	setReadOnly(mPaneSpec.readOnly);
	paneEditor->setCommunicationViewerMode(mPaneSpec.readOnly, mPaneSpec.role != bprProblems);
	paneEditor->setMiniMapSuppressed(mPaneSpec.suppressMiniMap);
	paneEditor->setWordWrapSuppressed(mPaneSpec.suppressWordWrap);
	paneEditor->setScrollBarsAlwaysVisible(mPaneSpec.scrollBarsAlwaysVisible);
}

void MRPaneEditWindow::layoutPaneChrome() noexcept {
	TRect editorBounds(getExtent());
	TScrollBar *horizontalScrollBar = horizontalEditorScrollBar();
	TScrollBar *verticalScrollBar = verticalEditorScrollBar();
	MRIndicator *paneIndicator = editorIndicator();

	if (frame != nullptr) frame->hide();
	MRFileEditor *paneEditor = getEditor();
	if (paneEditor != nullptr) {
		applyPanePolicy(nullptr);
		if (mPaneSpec.scrollBarsAlwaysVisible) {
			const bool showWithoutRange = configuredScrollbarVisibility() == MRScrollbarVisibility::Always;
			bool reserveHorizontal = showWithoutRange;
			bool reserveVertical = showWithoutRange;
			if (paneIndicator != nullptr) paneIndicator->hide();
			if (!showWithoutRange) {
				paneEditor->changeBounds(editorBounds);
				paneEditor->updateMetrics();
				reserveHorizontal = horizontalScrollBar != nullptr && horizontalScrollBar->maxVal > horizontalScrollBar->minVal;
				reserveVertical = verticalScrollBar != nullptr && verticalScrollBar->maxVal > verticalScrollBar->minVal;
			}
			for (int pass = 0; pass < 3; ++pass) {
				editorBounds = getExtent();
				if (reserveVertical && editorBounds.b.x - editorBounds.a.x > 1) editorBounds.b.x = std::max<short>(editorBounds.a.x + 1, editorBounds.b.x - 1);
				if (reserveHorizontal && editorBounds.b.y - editorBounds.a.y > 1) editorBounds.b.y = std::max<short>(editorBounds.a.y + 1, editorBounds.b.y - 1);
				if (horizontalScrollBar != nullptr) {
					if (reserveHorizontal) {
						const short right = reserveVertical ? std::max<short>(1, size.x - 1) : std::max<short>(1, size.x);
						TRect horizontalRect(0, std::max(0, size.y - 1), right, size.y);
						horizontalScrollBar->locate(horizontalRect);
						if (showWithoutRange) horizontalScrollBar->show();
					} else {
						TRect horizontalRect(0, size.y, 0, size.y);
						horizontalScrollBar->locate(horizontalRect);
					}
				}
				if (verticalScrollBar != nullptr) {
					if (reserveVertical) {
						const short bottom = reserveHorizontal ? std::max<short>(1, size.y - 1) : std::max<short>(1, size.y);
						TRect verticalRect(std::max(0, size.x - 1), 0, size.x, bottom);
						verticalScrollBar->locate(verticalRect);
						if (showWithoutRange) verticalScrollBar->show();
					} else {
						TRect verticalRect(size.x, 0, size.x, 0);
						verticalScrollBar->locate(verticalRect);
					}
				}
				paneEditor->changeBounds(editorBounds);
				paneEditor->updateMetrics();
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
		} else {
			if (horizontalScrollBar != nullptr) horizontalScrollBar->hide();
			if (verticalScrollBar != nullptr) verticalScrollBar->hide();
			paneEditor->changeBounds(editorBounds);
			paneEditor->updateMetrics();
		}
		drawPaneScrollBars();
	}
}

void MRPaneEditWindow::drawPaneScrollBars() noexcept {
	TScrollBar *horizontalScrollBar = horizontalEditorScrollBar();
	TScrollBar *verticalScrollBar = verticalEditorScrollBar();
	auto fillRect = [this](const TRect &rect) {
		const short left = std::max<short>(0, rect.a.x);
		const short top = std::max<short>(0, rect.a.y);
		const short right = std::min<short>(size.x, rect.b.x);
		const short bottom = std::min<short>(size.y, rect.b.y);
		const int width = right - left;

		if (width <= 0 || bottom <= top) return;
		TDrawBuffer buffer;
		buffer.moveChar(0, ' ', TAttrPair(mapColor(1)), width);
		for (short y = top; y < bottom; ++y)
			writeLine(left, y, width, 1, buffer);
	};

	normalizeScrollBarTrackGlyph(horizontalScrollBar);
	normalizeScrollBarTrackGlyph(verticalScrollBar);
	if (horizontalScrollBar != nullptr) horizontalScrollBar->drawView();
	if (verticalScrollBar != nullptr) verticalScrollBar->drawView();
	if (horizontalScrollBar != nullptr && (horizontalScrollBar->state & sfVisible) == 0) fillRect(horizontalScrollBar->getBounds());
	if (verticalScrollBar != nullptr && (verticalScrollBar->state & sfVisible) == 0) fillRect(verticalScrollBar->getBounds());
	if (horizontalScrollBar != nullptr && verticalScrollBar != nullptr && (horizontalScrollBar->state & sfVisible) != 0 && (verticalScrollBar->state & sfVisible) != 0) {
		const TRect horizontalBounds = horizontalScrollBar->getBounds();
		const TRect verticalBounds = verticalScrollBar->getBounds();
		if (horizontalBounds.a.y < horizontalBounds.b.y && verticalBounds.a.x < verticalBounds.b.x) {
			TDrawBuffer buffer;
			buffer.moveChar(0, ' ', TAttrPair(mapColor(4)), 1);
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
}

TFrame *MRPaneEditWindow::initFrame(TRect bounds) {
	return new MRPaneFrame(bounds);
}


MRBentoBox::BentoLayoutNode::BentoLayoutNode() noexcept : kind(blnPane), orientation(bsoHorizontal), dividerPosition(0), firstChild(-1), secondChild(-1), leafId(-1) {
}

MRBentoBox::BentoLeaf::BentoLeaf() noexcept : id(-1), role(bprCompilerOutput), spec(), title(), pane(nullptr), bounds(0, 0, 0, 0), visible(false) {
}

MRBentoBox::MRBentoBox(const TRect &bounds, const char *title, int number, MRBentoBoxMode mode)
    : TWindowInit(&MRBentoBox::initFrame), MREditWindow(bounds, title, number), secondaryPane(nullptr), layoutTree(), leaves(), paneFrameViews(), rootNode(-1), activeLeafId(0), nextLeafId(0), maximizedLeafId(-1), bentoMode(mode), sourceScrollBarPaletteActive(false), secondaryPaneVisible(false), windowCloseInProgress(false), bentoProjectionDirty(bpdNone), paneRoleDropList(), paneActionDropList(), paneRoleListAnchor(), pendingPaneRole(bprCompilerOutput), pendingPaneRoleTargetLeafId(0), compilerOutputStatus(), compilerProblemsStatus(), compilerDiagnostics(), compilerSidekickTracked(false), compilerSidekickUpdating(false), compilerSidekickDiagnosticIndex(0) {
	initializeLayoutTree();
	layoutSplitPanes();
}

MRBentoBox::~MRBentoBox() {
}

MREditWindow *MRBentoBox::secondaryEditWindow() const noexcept {
	return paneWindowForLeaf(firstToolLeafId());
}

MREditWindow *MRBentoBox::buildOutputPane() const noexcept {
	return paneWindowForLeaf(leafIdForRole(bprCompilerOutput));
}

MREditWindow *MRBentoBox::problemsPane() const noexcept {
	return paneWindowForLeaf(leafIdForRole(bprProblems));
}

MREditWindow *MRBentoBox::paneForBufferId(int bufferId) const noexcept {
	if (bufferId == this->bufferId()) return const_cast<MRBentoBox *>(this);
	for (const BentoLeaf &leaf : leaves)
		if (leaf.visible && leaf.pane != nullptr && leaf.pane->bufferId() == bufferId) return leaf.pane;
	return nullptr;
}

void MRBentoBox::collectVisiblePaneWindows(std::vector<MREditWindow *> &windows) const noexcept {
	for (const BentoLeaf &leaf : leaves)
		if (leaf.visible && leaf.pane != nullptr) windows.push_back(leaf.pane);
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

void MRBentoBox::setCompilerOutputStatus(const char *status) {
	const std::string nextStatus = status != nullptr ? status : "";

	if (compilerOutputStatus == nextStatus) return;
	compilerOutputStatus = nextStatus;
	updateActivePaneFrame();
	bentoProjectionDirty |= bpdChrome;
	flushBentoProjection();
}

void MRBentoBox::clearCompilerDiagnostics() {
	MREditWindow *problemsWindow = problemsPane();
	MRFileEditor *problemsEditor = problemsWindow != nullptr ? problemsWindow->getEditor() : nullptr;

	compilerDiagnostics.clear();
	compilerProblemsStatus.clear();
	clearTrackedCompilerSidekick(true);
	if (problemsEditor != nullptr) problemsEditor->clearFindMarkerRanges();
	if (getEditor() != nullptr) getEditor()->clearCompilerDiagnosticRanges();
	if (problemsWindow != nullptr) {
		static_cast<void>(problemsWindow->replaceTextBuffer("", bentoPaneRoleTitle(bprProblems)));
		problemsWindow->setReadOnly(true);
		problemsWindow->setFileChanged(false);
	}
	if (hasPaneSplit()) {
		bentoProjectionDirty |= bpdLayout;
		flushBentoProjection();
	}
}

bool MRBentoBox::hasCompilerProblems() const noexcept {
	return !compilerDiagnostics.empty();
}

void MRBentoBox::refreshCompilerProblemsPane() {
	MREditWindow *problemsWindow = problemsPane();
	MRFileEditor *problemsEditor = problemsWindow != nullptr ? problemsWindow->getEditor() : nullptr;
	std::ostringstream problemText;
	std::ostringstream statusText;
	std::size_t problemOffset = 0;
	int errorCount = 0;
	int warningCount = 0;
	int noteCount = 0;

	for (MRCompilerDiagnostic &diagnostic : compilerDiagnostics) {
		const std::string line = compilerDiagnosticProblemLine(diagnostic);
		if (diagnostic.severity == "error" || diagnostic.severity == "fatal error")
			++errorCount;
		else if (diagnostic.severity == "warning")
			++warningCount;
		else if (diagnostic.severity == "note")
			++noteCount;
		diagnostic.problemOffset = problemOffset;
		problemText << line << '\n';
		problemOffset += line.size() + 1;
	}
	if (errorCount > 0) statusText << errorCount << " error" << (errorCount == 1 ? "" : "s");
	if (warningCount > 0) {
		if (statusText.tellp() > 0) statusText << ", ";
		statusText << warningCount << " warning" << (warningCount == 1 ? "" : "s");
	}
	if (noteCount > 0) {
		if (statusText.tellp() > 0) statusText << ", ";
		statusText << noteCount << " note" << (noteCount == 1 ? "" : "s");
	}
	compilerProblemsStatus = statusText.str();
	if (problemsWindow != nullptr) {
		static_cast<void>(problemsWindow->replaceTextBuffer(problemText.str().c_str(), bentoPaneRoleTitle(bprProblems)));
		problemsWindow->setReadOnly(true);
		problemsWindow->setFileChanged(false);
	}
	if (problemsEditor != nullptr) problemsEditor->clearFindMarkerRanges();
	refreshSourceCompilerDiagnosticRanges();
	updateActivePaneFrame();
	if (hasPaneSplit()) {
		bentoProjectionDirty |= bpdLayout;
		flushBentoProjection();
	}
}

void MRBentoBox::refreshSourceCompilerDiagnosticRanges() {
	MRFileEditor *sourceEditor = getEditor();
	std::vector<std::pair<std::size_t, std::size_t>> errorRanges;
	std::vector<std::pair<std::size_t, std::size_t>> warningRanges;

	if (sourceEditor == nullptr) return;
	for (const MRCompilerDiagnostic &diagnostic : compilerDiagnostics) {
		if (!diagnostic.sourceAvailable) continue;
		const std::size_t start = diagnostic.sourceOffset;
		const std::size_t end = sourceEditor->nextCharOffset(start);
		if (diagnostic.severity == "warning" || diagnostic.severity == "note") warningRanges.push_back(std::make_pair(start, end));
		else if (diagnostic.severity == "error" || diagnostic.severity == "fatal error")
			errorRanges.push_back(std::make_pair(start, end));
	}
	sourceEditor->setCompilerDiagnosticRanges(errorRanges, warningRanges);
}

void MRBentoBox::clearTrackedCompilerSidekick(bool dropSidekick) noexcept {
	compilerSidekickTracked = false;
	compilerSidekickDiagnosticIndex = 0;
	if (dropSidekick) mrDropSidekickForParent(this);
}

void MRBentoBox::trackCompilerSidekick(std::size_t diagnosticIndex) noexcept {
	static_cast<void>(mrConsumeReadOnlySidekickDismissedForParent(this));
	compilerSidekickTracked = true;
	compilerSidekickDiagnosticIndex = diagnosticIndex;
}

void MRBentoBox::updateTrackedCompilerSidekick() {
	MRFileEditor *sourceEditor = getEditor();

	if (!compilerSidekickTracked) return;
	if (compilerSidekickUpdating) return;
	if (TProgram::deskTop == nullptr || TProgram::deskTop->current != this) {
		if (mrHasReadOnlySidekickForParent(this)) mrDropSidekickForParent(this);
		return;
	}
	if (mrConsumeReadOnlySidekickDismissedForParent(this)) {
		clearTrackedCompilerSidekick(false);
		return;
	}
	if (sourceEditor == nullptr || compilerSidekickDiagnosticIndex >= compilerDiagnostics.size()) {
		clearTrackedCompilerSidekick(true);
		return;
	}

	const MRCompilerDiagnostic &diagnostic = compilerDiagnostics[compilerSidekickDiagnosticIndex];
	if (!diagnostic.sourceAvailable || !compilerDiagnosticPathMatches(diagnostic.sourcePath, currentFileName())) {
		clearTrackedCompilerSidekick(true);
		return;
	}

	MRTextBufferModel::ReadSnapshot sourceSnapshot = buffer().readSnapshot();
	const std::size_t sourceOffset = sourceSnapshot.clampOffset(diagnostic.sourceOffset);
	const std::size_t lineIndex = sourceSnapshot.lineIndex(sourceOffset);
	const std::size_t lineStart = sourceSnapshot.lineStartByIndex(lineIndex);
	const std::size_t sourceLineEnd = sourceEditor->lineEndOffset(sourceOffset);
	const int diagnosticViewColumn = sourceEditor->charColumn(lineStart, sourceOffset) - sourceEditor->delta.x + 1;
	const int diagnosticViewRow = static_cast<int>(lineIndex) - sourceEditor->delta.y + 1;
	const TRect textViewport = sourceEditor->visibleTextViewportBounds();
	const int viewportWidth = std::max(1, textViewport.b.x - textViewport.a.x);
	const int viewportRows = sourceEditor->visibleViewportRows();

	if (diagnosticViewRow < 1 || diagnosticViewRow > viewportRows || diagnosticViewColumn < 1 || diagnosticViewColumn > viewportWidth) {
		if (mrHasReadOnlySidekickForParent(this)) mrDropSidekickForParent(this);
		return;
	}

	const int lineEndViewColumn = sourceEditor->charColumn(lineStart, sourceLineEnd) - sourceEditor->delta.x + 1;
	const int sidekickViewColumn = std::max(diagnosticViewColumn, lineEndViewColumn + 2);
	const bool rightMarginSidekick = configuredCompilerErrorMessagePlacement() == MRCompilerErrorMessagePlacement::RightMargin;
	compilerSidekickUpdating = true;
	static_cast<void>(mrOpenReadOnlySidekickAt(this, compilerDiagnosticDetailText(diagnostic), "Compiler diagnostic", diagnosticViewColumn, diagnosticViewRow, rightMarginSidekick ? sidekickViewColumn : diagnosticViewColumn,
	                                          rightMarginSidekick ? MRReadOnlySidekickPlacement::RightMargin : MRReadOnlySidekickPlacement::UnderCode));
	compilerSidekickUpdating = false;
}

bool MRBentoBox::refreshCompilerDiagnosticsFromOutput() {
	MREditWindow *outputWindow = buildOutputPane();
	MREditWindow *problemsWindow = problemsPane();
	std::vector<MRCompilerDiagnostic> diagnostics;
	std::vector<MRCompilerDiagnostic> filteredDiagnostics;
	MRFileEditor *sourceEditor = getEditor();
	MRTextBufferModel::ReadSnapshot sourceSnapshot;

	if (outputWindow == nullptr || problemsWindow == nullptr || sourceEditor == nullptr) return false;
	sourceSnapshot = buffer().readSnapshot();
	diagnostics = parseCompilerDiagnostics(outputWindow->buffer().readSnapshot().text(), currentFileName());
	for (MRCompilerDiagnostic &diagnostic : diagnostics) {
		if (!compilerDiagnosticSeverityEnabled(diagnostic)) continue;
		if (!diagnostic.sourceAvailable) continue;
		const std::size_t lineStart = sourceSnapshot.lineStartByIndex(diagnostic.sourceLine > 0 ? diagnostic.sourceLine - 1 : 0);
		const std::size_t lineEnd = sourceSnapshot.lineEnd(lineStart);
		diagnostic.sourceOffset = sourceOffsetForCompilerColumn(sourceEditor, lineStart, lineEnd, diagnostic.sourceColumn);
		filteredDiagnostics.push_back(std::move(diagnostic));
	}
	compilerDiagnostics = std::move(filteredDiagnostics);
	clearTrackedCompilerSidekick(true);
	refreshCompilerProblemsPane();
	return true;
}

void MRBentoBox::syncCompilerDiagnosticsAfterSourceMutation(const MRTextBufferModel::ReadSnapshot &oldSnapshot, const MRTextBufferModel::DocumentChangeSet &changeSet) {
	MRTextBufferModel::ReadSnapshot newSnapshot;
	std::size_t newLength = 0;
	const std::size_t editStart = changeSet.touchedRange.start;
	std::size_t oldEditEnd = std::min(changeSet.touchedRange.end, changeSet.oldLength);

	if (compilerDiagnostics.empty() || !changeSet.changed || changeSet.oldVersion != oldSnapshot.version()) return;
	clearTrackedCompilerSidekick(true);
	newSnapshot = buffer().readSnapshot();
	if (changeSet.newVersion != newSnapshot.version()) return;
	newLength = newSnapshot.length();
	const long long delta = static_cast<long long>(changeSet.newLength) - static_cast<long long>(changeSet.oldLength);
	if (delta > 0 && changeSet.touchedRange.end - editStart == static_cast<std::size_t>(delta)) oldEditEnd = editStart;

	std::vector<MRCompilerDiagnostic> remapped;
	for (MRCompilerDiagnostic diagnostic : compilerDiagnostics) {
		if (!diagnostic.sourceAvailable) {
			remapped.push_back(std::move(diagnostic));
			continue;
		}
		if (editTouchesDiagnosticLine(oldSnapshot, diagnostic.sourceOffset, editStart, oldEditEnd)) continue;
		if (diagnostic.sourceOffset >= oldEditEnd) {
			const long long shifted = static_cast<long long>(diagnostic.sourceOffset) + delta;
			if (shifted < 0 || static_cast<std::size_t>(shifted) > newLength) continue;
			diagnostic.sourceOffset = static_cast<std::size_t>(shifted);
		}
		const std::size_t lineIndex = newSnapshot.lineIndex(diagnostic.sourceOffset);
		const std::size_t lineStart = newSnapshot.lineStartByIndex(lineIndex);
		diagnostic.sourceLine = lineIndex + 1;
		diagnostic.sourceColumn = lineColumnForOffset(newSnapshot, lineStart, diagnostic.sourceOffset);
		remapped.push_back(std::move(diagnostic));
	}
	compilerDiagnostics = std::move(remapped);
	refreshCompilerProblemsPane();
}

bool MRBentoBox::jumpToProblemAtCursor() {
	MREditWindow *problemsWindow = problemsPane();
	MREditWindow *outputWindow = buildOutputPane();
	MRFileEditor *sourceEditor = getEditor();
	MRFileEditor *problemsEditor = problemsWindow != nullptr ? problemsWindow->getEditor() : nullptr;
	MRFileEditor *outputEditor = outputWindow != nullptr ? outputWindow->getEditor() : nullptr;
	MRTextBufferModel::ReadSnapshot sourceSnapshot;
	std::size_t cursorOffset;
	const MRCompilerDiagnostic *selected = nullptr;
	std::size_t selectedIndex = 0;

	if (sourceEditor == nullptr || problemsEditor == nullptr || outputEditor == nullptr) return false;
	cursorOffset = problemsEditor->cursorOffset();
	for (std::size_t i = 0; i < compilerDiagnostics.size(); ++i) {
		const MRCompilerDiagnostic &diagnostic = compilerDiagnostics[i];
		const std::size_t lineEnd = problemsEditor->lineEndOffset(diagnostic.problemOffset);
		if (cursorOffset >= diagnostic.problemOffset && cursorOffset <= lineEnd) {
			selected = &diagnostic;
			selectedIndex = i;
			break;
		}
	}
	if (selected == nullptr) return false;
	if (selected->sourceAvailable && !compilerDiagnosticPathMatches(selected->sourcePath, currentFileName())) return false;

	const std::size_t outputLineEnd = outputEditor->lineEndOffset(selected->outputOffset);
	outputEditor->setCursorOffset(selected->outputOffset);
	outputEditor->setSelectionOffsets(selected->outputOffset, outputLineEnd);

	problemsEditor->setCursorOffset(selected->problemOffset);
	problemsEditor->setSelectionOffsets(selected->problemOffset, problemsEditor->lineEndOffset(selected->problemOffset));
	problemsEditor->setFindMarkerRanges({std::make_pair(selected->problemOffset, problemsEditor->lineEndOffset(selected->problemOffset))});
	if (!selected->sourceAvailable) {
		const int outputLeaf = leafIdForRole(bprCompilerOutput);
		clearTrackedCompilerSidekick(true);
		if (outputLeaf >= 0) setActivePane(outputLeaf);
		return true;
	}

	sourceSnapshot = buffer().readSnapshot();
	const std::size_t sourceOffset = sourceSnapshot.clampOffset(selected->sourceOffset);
	const std::size_t lineStart = sourceSnapshot.lineStart(sourceOffset);
	const std::size_t sourceLineEnd = sourceEditor->lineEndOffset(sourceOffset);
	std::size_t sourceSelectionEnd = sourceOffset < sourceLineEnd ? sourceEditor->nextCharOffset(sourceOffset) : sourceOffset;
	if (sourceSelectionEnd == sourceOffset && lineStart < sourceLineEnd) sourceSelectionEnd = sourceLineEnd;
	sourceEditor->setCursorOffset(sourceOffset);
	sourceEditor->setSelectionOffsets(sourceOffset, sourceSelectionEnd);
	sourceEditor->revealCursor(True);

	activatePrimaryPane();
	trackCompilerSidekick(selectedIndex);
	updateTrackedCompilerSidekick();
	return true;
}

bool MRBentoBox::jumpToNextProblem() {
	MREditWindow *problemsWindow = problemsPane();
	MRFileEditor *problemsEditor = problemsWindow != nullptr ? problemsWindow->getEditor() : nullptr;
	std::size_t cursorOffset;
	const MRCompilerDiagnostic *next = nullptr;

	if (problemsEditor == nullptr || compilerDiagnostics.empty()) return false;
	cursorOffset = problemsEditor->cursorOffset();
	for (const MRCompilerDiagnostic &diagnostic : compilerDiagnostics)
		if (diagnostic.problemOffset > cursorOffset) {
			next = &diagnostic;
			break;
		}
	if (next == nullptr) next = &compilerDiagnostics.front();
	problemsEditor->setCursorOffset(next->problemOffset);
	problemsEditor->setSelectionOffsets(next->problemOffset, problemsEditor->lineEndOffset(next->problemOffset));
	return jumpToProblemAtCursor();
}

bool MRBentoBox::jumpToPreviousProblem() {
	MREditWindow *problemsWindow = problemsPane();
	MRFileEditor *problemsEditor = problemsWindow != nullptr ? problemsWindow->getEditor() : nullptr;
	std::size_t cursorOffset;
	const MRCompilerDiagnostic *previous = nullptr;

	if (problemsEditor == nullptr || compilerDiagnostics.empty()) return false;
	cursorOffset = problemsEditor->cursorOffset();
	for (const MRCompilerDiagnostic &diagnostic : compilerDiagnostics) {
		if (diagnostic.problemOffset >= cursorOffset) break;
		previous = &diagnostic;
	}
	if (previous == nullptr) previous = &compilerDiagnostics.back();
	problemsEditor->setCursorOffset(previous->problemOffset);
	problemsEditor->setSelectionOffsets(previous->problemOffset, problemsEditor->lineEndOffset(previous->problemOffset));
	return jumpToProblemAtCursor();
}

bool MRBentoBox::placePaneRole(MRBentoPaneRole role, MRBentoPanePlacement placement) {
	return placePaneRoleInContext(role, placement, activeLeafId);
}

TColorAttr MRBentoBox::mapColor(uchar index) {
	if (sourceScrollBarPaletteActive && (index == 4 || index == 5)) return MREditWindow::mapColor(activeLeafId == 0 ? 13 : 1);
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

bool MRBentoBox::showsFrameGrowHandle() const noexcept {
	return false;
}

bool MRBentoBox::placePaneRoleInContext(MRBentoPaneRole role, MRBentoPanePlacement placement, int targetLeafId) {
	MRPaneEditWindow *targetPane = paneWindowForLeaf(targetLeafId);
	const MRBentoPaneSpec spec = paneSpecForRole(role);

	if (targetLeafId == 0 && placement == bppReplace) return false;
	switch (placement) {
		case bppReplace:
			if (targetPane == nullptr) return false;
			targetPane->setPaneSpec(spec, getEditor());
			for (BentoLeaf &leaf : leaves)
				if (leaf.id == targetLeafId) {
					leaf.role = role;
					leaf.spec = spec;
					leaf.title = bentoPaneRoleTitle(role);
				}
			setActivePane(targetLeafId);
			layoutSplitPanes();
			return true;
		case bppSplitRight:
			return splitLeafNode(targetLeafId, bsoVertical, spec) >= 0;
		case bppSplitDown:
			return splitLeafNode(targetLeafId, bsoHorizontal, spec) >= 0;
		default:
			return false;
	}
}

bool MRBentoBox::splitActiveEditorPane(MRBentoPanePlacement placement) {
	MRBentoPaneSpec spec = paneSpecForRole(bprSplitEditor);

	switch (placement) {
		case bppSplitRight:
			return splitLeafNode(activeLeafId, bsoVertical, spec) >= 0;
		case bppSplitDown:
			return splitLeafNode(activeLeafId, bsoHorizontal, spec) >= 0;
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
	updatePaneRoleListChrome();
	MREditWindow::changeBounds(bounds);
	if (hasPaneSplit()) layoutSplitPanes();
}

void MRBentoBox::close() {
	windowCloseInProgress = true;
	MREditWindow::close();
}

void MRBentoBox::shutDown() {
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
	}
	if (mouseEvent) setActivePaneForMouse(event.mouse.where);
	if (activeLeafId != 0 && splitEventTargetsSecondaryPane(event)) {
		MRPaneEditWindow *targetPane = paneWindowForLeaf(activeLeafId);
		TRect targetBounds = paneBoundsForLeaf(activeLeafId);
		const bool problemsPaneActive = roleForLeaf(activeLeafId) == bprProblems;
		const bool enterProblem = problemsPaneActive && event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter;
		const bool clickProblem = problemsPaneActive && event.what == evMouseDown && (event.mouse.buttons & mbLeftButton) != 0;
		if (mouseEvent && !pointInRect(localMouse, contentBounds(targetBounds))) {
			MREditWindow::handleEvent(event);
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
		MRFileEditor *sourceEditor = getEditor();
		const bool trackSourceMutation = !compilerDiagnostics.empty() && sourceEditor != nullptr;
		MRTextBufferModel::ReadSnapshot oldSnapshot;
		TScrollBar *targetHorizontalScrollBar = targetPane != nullptr ? targetPane->horizontalEditorScrollBar() : nullptr;
		TScrollBar *targetVerticalScrollBar = targetPane != nullptr ? targetPane->verticalEditorScrollBar() : nullptr;
		const std::pair<bool, bool> targetRangeBefore = std::make_pair(targetHorizontalScrollBar != nullptr && targetHorizontalScrollBar->maxVal > targetHorizontalScrollBar->minVal, targetVerticalScrollBar != nullptr && targetVerticalScrollBar->maxVal > targetVerticalScrollBar->minVal);
		if (trackSourceMutation) oldSnapshot = buffer().readSnapshot();
		if (targetPane != nullptr) {
			MRFileEditor *targetEditor = targetPane->getEditor();
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
		targetHorizontalScrollBar = targetPane != nullptr ? targetPane->horizontalEditorScrollBar() : nullptr;
		targetVerticalScrollBar = targetPane != nullptr ? targetPane->verticalEditorScrollBar() : nullptr;
		const std::pair<bool, bool> targetRangeAfter = std::make_pair(targetHorizontalScrollBar != nullptr && targetHorizontalScrollBar->maxVal > targetHorizontalScrollBar->minVal, targetVerticalScrollBar != nullptr && targetVerticalScrollBar->maxVal > targetVerticalScrollBar->minVal);
		if (targetPane != nullptr && targetRangeAfter != targetRangeBefore) targetPane->layoutPaneChrome();
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
		flushBentoProjection();
		return;
	}
	MRFileEditor *sourceEditor = getEditor();
	const bool trackSourceMutation = !compilerDiagnostics.empty() && sourceEditor != nullptr;
	TScrollBar *sourceHorizontalScrollBar = horizontalEditorScrollBar();
	TScrollBar *sourceVerticalScrollBar = verticalEditorScrollBar();
	const std::pair<bool, bool> sourceRangeBefore = std::make_pair(sourceHorizontalScrollBar != nullptr && sourceHorizontalScrollBar->maxVal > sourceHorizontalScrollBar->minVal, sourceVerticalScrollBar != nullptr && sourceVerticalScrollBar->maxVal > sourceVerticalScrollBar->minVal);
	MRTextBufferModel::ReadSnapshot oldSnapshot;
	if (trackSourceMutation) oldSnapshot = buffer().readSnapshot();
	MREditWindow::handleEvent(event);
	if (trackSourceMutation) syncCompilerDiagnosticsAfterSourceMutation(oldSnapshot, sourceEditor->lastDocumentChangeSet());
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
	TRect inner = getExtent();
	inner.grow(-1, -1);
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
}

void MRBentoBox::flushBentoProjection() noexcept {
	if (windowCloseInProgress || bentoProjectionDirty == bpdNone) return;
	const unsigned dirty = bentoProjectionDirty;
	bentoProjectionDirty = bpdNone;

	if (!hasPaneSplit()) return;
	if ((dirty & bpdLayout) != 0) {
		layoutSplitPanes();
		if ((dirty & bpdOverlay) != 0) updateTrackedCompilerSidekick();
		return;
	}
	if ((dirty & bpdContent) != 0) drawSharedEditorPanes();
	if ((dirty & bpdScrollBar) != 0) drawSourcePaneScrollBars();
	if ((dirty & bpdOverlay) != 0) updateTrackedCompilerSidekick();
	if ((dirty & (bpdChrome | bpdScrollBar)) != 0) drawPaneFrames();
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
		if (sourceIndicator != nullptr) {
			if (reserveHorizontal) {
				const short indicatorLeft = std::min<short>(content.b.x, content.a.x + 1);
				const short indicatorRight = std::max<short>(indicatorLeft, std::min<short>(static_cast<short>(indicatorLeft + 36), static_cast<short>(reserveVertical ? content.b.x - 1 : content.b.x)));
				TRect indicatorRect(indicatorLeft, std::max<short>(content.a.y, content.b.y - 1), indicatorRight, content.b.y);
				sourceIndicator->show();
				sourceIndicator->locate(indicatorRect);
			} else
				sourceIndicator->hide();
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
	if (sourceIndicator != nullptr && reserveHorizontal) sourceIndicator->drawView();
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

void MRBentoBox::drawSourcePaneScrollBars() noexcept {
	if (windowCloseInProgress) return;
	TScrollBar *horizontalScrollBar = horizontalEditorScrollBar();
	TScrollBar *verticalScrollBar = verticalEditorScrollBar();
	const bool showWithoutRange = configuredScrollbarVisibility() == MRScrollbarVisibility::Always;
	auto fillRect = [this](const TRect &rect) {
		const short left = std::max<short>(0, rect.a.x);
		const short top = std::max<short>(0, rect.a.y);
		const short right = std::min<short>(size.x, rect.b.x);
		const short bottom = std::min<short>(size.y, rect.b.y);
		const int width = right - left;

		if (width <= 0 || bottom <= top) return;
		TDrawBuffer buffer;
		buffer.moveChar(0, ' ', TAttrPair(MREditWindow::mapColor(1)), width);
		for (short y = top; y < bottom; ++y)
			writeLine(left, y, width, 1, buffer);
	};
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
		const TColorAttr baseAttr = MREditWindow::mapColor(activeLeafId == 0 ? 13 : 1);
		const int logicalSize = std::max(3, scrollBar->size.x == 1 ? scrollBar->size.y : scrollBar->size.x);
		const int markerPos = scrollBar->getPos();

		if (scrollBar->size.x == 1) {
			for (int row = 0; row < logicalSize; ++row) {
				char ch = scrollBar->chars[2];
				if (row == 0) ch = scrollBar->chars[0];
				else if (row == logicalSize - 1)
					ch = scrollBar->chars[1];
				else if (scrollBar->maxVal == scrollBar->minVal)
					ch = scrollBar->chars[4];
				else if (row == markerPos)
					ch = scrollBar->chars[3];
				drawCell(bounds.a.x, static_cast<short>(bounds.a.y + row), ch, baseAttr);
			}
		} else {
			const int width = std::max(0, bounds.b.x - bounds.a.x);
			if (width <= 0) return false;
			TDrawBuffer buffer;
			for (int column = 0; column < std::min(width, logicalSize); ++column) {
				char ch = scrollBar->chars[2];
				if (column == 0) ch = scrollBar->chars[0];
				else if (column == logicalSize - 1)
					ch = scrollBar->chars[1];
				else if (scrollBar->maxVal == scrollBar->minVal)
					ch = scrollBar->chars[4];
				else if (column == markerPos)
					ch = scrollBar->chars[3];
				buffer.moveChar(static_cast<ushort>(column), ch, baseAttr, 1);
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
			buffer.moveChar(0, ' ', TAttrPair(mapColor(4)), 1);
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
	const TAttrPair frameColor = TAttrPair(mapColor(focused ? 13 : 1));
	TDrawBuffer buffer;

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
}

void MRBentoBox::closeSecondaryPane() noexcept {
	int toolLeaf = firstToolLeafId();
	while (toolLeaf >= 0) {
		collapseLeafNode(toolLeaf);
		toolLeaf = firstToolLeafId();
	}
	setActivePane(0);
	layoutSplitPanes();
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
	paneActionDropList.hide();
	paneActionDropList.toggle(*this, anchor, paneActionChoices(), kBentoPaneActionReplace, this, cmMrBentoPaneActionAccepted, listHeight);
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

bool MRBentoBox::handlePaneDropListEvent(TEvent &event) {
	const bool roleListVisible = paneRoleDropList.visible();
	const bool actionListVisible = paneActionDropList.visible();
	if (!roleListVisible && !actionListVisible) return false;
	if (paneActionDropList.handleOpenListEvent(event, false)) {
		updatePaneRoleListChrome();
		return true;
	}
	if (paneRoleDropList.handleOpenListEvent(event, false)) {
		updatePaneRoleListChrome();
		return true;
	}
	if (event.what == evMouseDown && !paneRoleDropList.containsPoint(event.mouse.where) && !paneActionDropList.containsPoint(event.mouse.where)) {
		paneRoleDropList.hide();
		paneActionDropList.hide();
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
	if (leafId < 0 || (leafId == 0 && bentoMode != bbmDocumentViewports) || nodeIndexForLeaf(leafId) < 0) return;
	maximizedLeafId = maximizedLeafId == leafId ? -1 : leafId;
	setActivePane(leafId);
	layoutSplitPanes();
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
	if (leaf.role == bprCompilerOutput && !compilerOutputStatus.empty()) return std::string(bentoPaneRoleTitle(leaf.role)) + " [" + compilerOutputStatus + "]";
	if (leaf.role == bprProblems && !compilerProblemsStatus.empty()) return std::string(bentoPaneRoleTitle(leaf.role)) + " [" + compilerProblemsStatus + "]";
	if (!leaf.title.empty()) return leaf.title;
	return bentoPaneRoleTitle(leaf.role);
}

TRect MRBentoBox::nodeBounds(int nodeIndex) const noexcept {
	TRect inner = getExtent();
	inner.grow(-1, -1);
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

	for (const BentoPaneRoleDescriptor &descriptor : kBentoPaneRoles)
		if (descriptor.listed) choices.push_back(descriptor.title);
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
