#include "MRVMScreen.hpp"

#define Uses_MsgBox
#define Uses_TProgram
#define Uses_TApplication
#define Uses_TScreen
#define Uses_TDisplay
#define Uses_TDrawBuffer
#define Uses_TView
#include <tvision/tv.h>

#include "../mrmac.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../../app/commands/MRWindowCommands.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/MRMenuBar.hpp"
#include "../../ui/MRMessageLineController.hpp"
#include "../../ui/MRStatusLine.hpp"
#include "../../ui/MRWindowSupport.hpp"

namespace {
using Value = VirtualMachine::Value;

class MacroCellGrid;

struct ScreenStateCoordinator {
	std::atomic<std::uint64_t> baseGeneration{1};
	std::atomic<std::uint64_t> overlayGeneration{1};
	std::atomic<bool> baseInvalidated{false};

	void noteMacroOverlayMutation(std::uint64_t generation) noexcept {
		overlayGeneration.store(generation, std::memory_order_relaxed);
	}

	void noteDirectScreenMutation(std::uint64_t generation) noexcept {
		baseGeneration.store(generation, std::memory_order_relaxed);
		baseInvalidated.store(true, std::memory_order_relaxed);
	}

	void noteBaseRedraw(std::uint64_t generation) noexcept {
		baseGeneration.store(generation, std::memory_order_relaxed);
		baseInvalidated.store(false, std::memory_order_relaxed);
	}

	[[nodiscard]] bool needsOverlayReprojection() const noexcept {
		return baseInvalidated.load(std::memory_order_relaxed);
	}
};

struct UiScreenStateFacade {
	static std::uint64_t nextGeneration() noexcept;
	static void noteMacroOverlayMutation() noexcept;
	static void noteBaseMutation() noexcept;
	static void noteBaseRedraw() noexcept;
	[[nodiscard]] static bool needsOverlayReprojection() noexcept;
	[[nodiscard]] static std::pair<bool, bool> renderBaseThenOverlayIfNeeded(MacroCellGrid &grid) noexcept;
	[[nodiscard]] static bool renderOverlay(MacroCellGrid &grid) noexcept;
};

struct MacroScreenLineColOverlayState {
	bool haveLine = false;
	bool haveCol = false;
	int line = 0;
	int col = 0;
};

struct MacroCell {
	char ch = ' ';
	uchar attr = 0x07;
	bool known = false;
};

struct MacroScreenBoxSnapshot {
	int width = 0;
	int height = 0;
	int x1 = 0;
	int y1 = 0;
	int x2 = 0;
	int y2 = 0;
	std::vector<MacroCell> cells;
};

class MacroCellView final : public TView {
  public:
	MacroCellView(const TRect &bounds, MacroCellGrid &grid) noexcept;
	void draw() override;

  private:
	MacroCellGrid &grid;
};

class MacroCellGrid {
  public:
	bool putBox(int x1, int y1, int x2, int y2, int bgColor, int fgColor, const std::string &title, bool shadow);
	bool writeText(const std::string &text, int x, int y, int bgColor, int fgColor);
	bool clearLine(int col, int row, int count);
	bool clearScreen(int attr);
	bool scrollBox(int x1, int y1, int x2, int y2, int attr, bool down);
	bool putLineColOverlay(int line, int col, bool haveLine, bool haveCol);
	bool killBox();
	void drawKnownCells(MacroCellView &view);
	void beginProjectionBatch() noexcept;
	void endProjectionBatch() noexcept;

  private:
	friend struct UiScreenStateFacade;

	int width = 0;
	int height = 0;
	std::vector<MacroCell> cells;
	std::vector<MacroScreenBoxSnapshot> boxStack;
	MacroCellView *view = nullptr;

	bool ensureGeometry();
	bool ensureView();
	[[nodiscard]] std::size_t indexFor(int x, int y) const noexcept;
	[[nodiscard]] static uchar composeAttribute(int bgColor, int fgColor) noexcept;
	bool writeCell(int x, int y, char ch, uchar attr);
	bool copyCell(int dstX, int dstY, int srcX, int srcY);
	bool fillRect(int x1, int y1, int x2, int y2, char ch, uchar attr);
	bool writeString(int x, int y, const std::string &text, uchar attr);
	void pushSnapshot(int x1, int y1, int x2, int y2);
	void projectAll();
	void projectRowSpan(MacroCellView &targetView, int y, int x1, int x2);
	void projectDirtyRows(MacroCellView &targetView);
	void redrawBaseAndOverlay();
	void markDirtyRow(int y) noexcept;
	void clearDirtyRows() noexcept;
	void markFullProjection() noexcept;
	[[nodiscard]] bool hasDirtyRows() const noexcept;
	[[nodiscard]] bool hasKnownCells() const noexcept;

	std::vector<unsigned char> dirtyRows;
	bool fullProjectionPending = true;
	bool geometryResetPending = false;
	int projectionBatchDepth = 0;
	bool flushPending = false;
};

static std::atomic<std::uint64_t> g_macroScreenMutationEpoch(1);
static std::atomic<std::uint64_t> g_macroScreenFlushCount(0);
static ScreenStateCoordinator g_screenStateCoordinator;
static MacroScreenLineColOverlayState g_macroScreenLineColOverlay;
static MacroCellGrid g_macroCellGrid;

static bool isStringLike(const Value &value) noexcept {
	return value.type == TYPE_STR || value.type == TYPE_CHAR;
}

static bool isNumeric(const Value &value) noexcept {
	return value.type == TYPE_INT || value.type == TYPE_REAL;
}

static int valueAsInt(const Value &value) {
	if (value.type == TYPE_INT) return value.i;
	throw std::runtime_error("integer value expected");
}

static std::string charToString(unsigned char c) {
	if (c == 0) return std::string();
	return std::string(1, static_cast<char>(c));
}

static std::string valueAsString(const Value &value) {
	if (value.type == TYPE_STR) return value.s;
	if (value.type == TYPE_CHAR) return charToString(value.c);
	throw std::runtime_error("string value expected");
}

static Value makeIntValue(int value) {
	Value v;
	v.type = TYPE_INT;
	v.i = value;
	return v;
}

static Value makeStringValue(const std::string &value) {
	Value v;
	v.type = TYPE_STR;
	v.s = value;
	return v;
}

static uchar composeScreenAttribute(int bgColor, int fgColor) noexcept {
	if ((bgColor & 0xFF) == 0) return static_cast<uchar>(fgColor & 0xFF);
	return static_cast<uchar>(((bgColor & 0x0F) << 4) | (fgColor & 0x0F));
}

static void noteMacroScreenFlush() noexcept {
	g_macroScreenFlushCount.fetch_add(1, std::memory_order_relaxed);
}

// Render sink classification for the Strangler foundation:
// ordinary-view-draw: MacroCellView::draw() projects buffered cells only.
// base-redraw-trigger: forceMacroUiMessageRefresh(), projectAll() and redrawBaseAndOverlay().
// overlay-render: UiScreenStateFacade plus line/column overlay replay.
// unsafe-physical-write: the physical flush sink remains confined to facade-approved sinks.
static void forceMacroUiMessageRefresh(TApplication *app) {
	if (app == nullptr) return;
	if (app->menuBar != nullptr) app->menuBar->drawView();
	if (app->statusLine != nullptr) app->statusLine->drawView();
	noteMacroScreenFlush();
	TScreen::flushScreen();
}

static bool applyMakeMessageText(const std::string &text) {
	TApplication *app = dynamic_cast<TApplication *>(TProgram::application);
	mr::messageline::VisibleMessage existingMessage;

	if (app == nullptr || dynamic_cast<MRMenuBar *>(app->menuBar) == nullptr) throw std::runtime_error("MAKE_MESSAGE requires an active menu bar.");
	if (text.empty()) {
		if (!mr::messageline::currentOwnerMessage(mr::messageline::Owner::MacroMessage, existingMessage)) return true;
		mr::messageline::clearOwner(mr::messageline::Owner::MacroMessage);
	} else {
		if (mr::messageline::currentOwnerMessage(mr::messageline::Owner::MacroMessage, existingMessage) && existingMessage.kind == mr::messageline::Kind::Info && existingMessage.text == text) return true;
		mr::messageline::postAutoTimed(mr::messageline::Owner::MacroMessage, text, mr::messageline::Kind::Info, mr::messageline::kPriorityMedium);
	}
	forceMacroUiMessageRefresh(app);
	return returnWithDirectScreenMutation(true);
}

static bool renderMacroLineColOverlay() {
	return g_macroCellGrid.putLineColOverlay(g_macroScreenLineColOverlay.line, g_macroScreenLineColOverlay.col, g_macroScreenLineColOverlay.haveLine, g_macroScreenLineColOverlay.haveCol);
}

static bool reapplyMacroLineColOverlayIfActive() {
	if (!g_macroScreenLineColOverlay.haveLine && !g_macroScreenLineColOverlay.haveCol) return true;
	return renderMacroLineColOverlay();
}

std::uint64_t UiScreenStateFacade::nextGeneration() noexcept {
	return g_macroScreenMutationEpoch.fetch_add(1, std::memory_order_relaxed) + 1;
}

void UiScreenStateFacade::noteMacroOverlayMutation() noexcept {
	g_screenStateCoordinator.noteMacroOverlayMutation(nextGeneration());
}

void UiScreenStateFacade::noteBaseMutation() noexcept {
	g_screenStateCoordinator.noteDirectScreenMutation(nextGeneration());
}

void UiScreenStateFacade::noteBaseRedraw() noexcept {
	g_screenStateCoordinator.noteBaseRedraw(mrvmUiScreenMutationEpoch());
}

bool UiScreenStateFacade::needsOverlayReprojection() noexcept {
	return g_screenStateCoordinator.needsOverlayReprojection();
}

} // namespace

bool returnWithMacroScreenMutation(bool ok) noexcept {
	if (ok) UiScreenStateFacade::noteMacroOverlayMutation();
	return ok;
}

bool returnWithDirectScreenMutation(bool ok) noexcept {
	if (ok) UiScreenStateFacade::noteBaseMutation();
	return ok;
}

std::uint64_t mrvmUiScreenMutationEpoch() noexcept {
	return g_macroScreenMutationEpoch.load(std::memory_order_relaxed);
}

void mrvmUiInvalidateScreenBase() noexcept {
	UiScreenStateFacade::noteBaseMutation();
}

void mrvmUiTouchScreenMutationEpoch() noexcept {
	UiScreenStateFacade::noteBaseMutation();
}

void mrvmUiBeginMacroScreenBatch() noexcept {
	g_macroCellGrid.beginProjectionBatch();
}

void mrvmUiEndMacroScreenBatch() noexcept {
	g_macroCellGrid.endProjectionBatch();
}

std::uint64_t mrvmUiMacroScreenFlushCount() noexcept {
	return g_macroScreenFlushCount.load(std::memory_order_relaxed);
}

void mrvmUiResetMacroScreenFlushCount() noexcept {
	g_macroScreenFlushCount.store(0, std::memory_order_relaxed);
}

std::pair<bool, bool> UiScreenStateFacade::renderBaseThenOverlayIfNeeded(MacroCellGrid &grid) noexcept {
	const bool baseReprojectionNeeded = grid.geometryResetPending || UiScreenStateFacade::needsOverlayReprojection();
	if (baseReprojectionNeeded && TProgram::application != nullptr) {
		TProgram::application->drawView();
		grid.markFullProjection();
	}
	return {baseReprojectionNeeded, renderOverlay(grid)};
}

bool UiScreenStateFacade::renderOverlay(MacroCellGrid &grid) noexcept {
	if (!grid.ensureView() || grid.view == nullptr || !grid.hasKnownCells()) return false;
	if (grid.fullProjectionPending) {
		grid.projectDirtyRows(*grid.view);
		if (grid.hasDirtyRows()) return true;
		grid.drawKnownCells(*grid.view);
		return true;
	}
	if (!grid.hasDirtyRows()) return false;
	grid.projectDirtyRows(*grid.view);
	return true;
}

bool applyMarqueeProc(const std::string &name, const std::vector<Value> &args) {
	TApplication *app = dynamic_cast<TApplication *>(TProgram::application);
	mr::messageline::Kind kind = mr::messageline::Kind::Info;
	mr::messageline::VisibleMessage existingMessage;
	std::string text;

	if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error(name + " expects one string argument.");
	if (app == nullptr || dynamic_cast<MRMenuBar *>(app->menuBar) == nullptr) throw std::runtime_error(name + " requires an active menu bar.");
	if (name == "MARQUEE_WARNING") kind = mr::messageline::Kind::Warning;
	else if (name == "MARQUEE_ERROR")
		kind = mr::messageline::Kind::Error;

	text = valueAsString(args[0]);
	if (text.empty()) {
		if (!mr::messageline::currentOwnerMessage(mr::messageline::Owner::MacroMarquee, existingMessage)) return true;
		mr::messageline::clearOwner(mr::messageline::Owner::MacroMarquee);
	} else {
		if (mr::messageline::currentOwnerMessage(mr::messageline::Owner::MacroMarquee, existingMessage) && existingMessage.kind == kind && existingMessage.text == text) return true;
		mr::messageline::postAutoTimed(mr::messageline::Owner::MacroMarquee, text, kind, mr::messageline::kPriorityMedium);
	}
	forceMacroUiMessageRefresh(app);
	return returnWithDirectScreenMutation(true);
}

bool applyMakeMessageProc(const std::vector<Value> &args) {
	if (args.size() != 1 || !isStringLike(args[0])) throw std::runtime_error("MAKE_MESSAGE expects one string argument.");
	return applyMakeMessageText(valueAsString(args[0]));
}

bool applyBrainProc(const std::string &name, const std::vector<Value> &args) {
	bool enabled = false;
	bool activeChanged = false;
	bool visibleChanged = false;

	if (args.size() != 1 || !isNumeric(args[0])) throw std::runtime_error(name + " expects one integer argument.");

	enabled = valueAsInt(args[0]) != 0;
	activeChanged = mrIsMacroBrainMarkerActive() != enabled;
	visibleChanged = mrIsMacroBrainMarkerVisible() != enabled;
	if (!activeChanged && !visibleChanged) return true;
	mrSetMacroBrainMarkerActive(enabled);
	mrSetMacroBrainMarkerVisible(enabled);
	(void)mrvmUiRedrawCurrentWindow();
	return returnWithDirectScreenMutation(true);
}

MacroCellView::MacroCellView(const TRect &bounds, MacroCellGrid &aGrid) noexcept : TView(bounds), grid(aGrid) {
	growMode = gfGrowHiX | gfGrowHiY;
	options &= static_cast<ushort>(~ofSelectable);
}

void MacroCellView::draw() {
	grid.drawKnownCells(*this);
}

bool MacroCellGrid::ensureGeometry() {
	const int nextWidth = static_cast<int>(TDisplay::getCols());
	const int nextHeight = static_cast<int>(TDisplay::getRows());
	if (nextWidth <= 0 || nextHeight <= 0) return false;
	if (nextWidth == width && nextHeight == height && cells.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) return true;

	width = nextWidth;
	height = nextHeight;
	cells.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), MacroCell());
	dirtyRows.assign(static_cast<std::size_t>(height), 0);
	boxStack.clear();
	fullProjectionPending = true;
	geometryResetPending = true;
	if (view != nullptr) {
		TRect bounds(0, 0, static_cast<short>(width), static_cast<short>(height));
		view->locate(bounds);
	}
	return true;
}

bool MacroCellGrid::ensureView() {
	if (!ensureGeometry() || TProgram::application == nullptr) return false;
	if (view != nullptr && view->owner != nullptr) return true;

	TRect bounds(0, 0, static_cast<short>(width), static_cast<short>(height));
	view = new MacroCellView(bounds, *this);
	TProgram::application->insert(view);
	return true;
}

std::size_t MacroCellGrid::indexFor(int x, int y) const noexcept {
	return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
}

uchar MacroCellGrid::composeAttribute(int bgColor, int fgColor) noexcept {
	return composeScreenAttribute(bgColor, fgColor);
}

bool MacroCellGrid::writeCell(int x, int y, char ch, uchar attr) {
	if (x < 0 || y < 0 || x >= width || y >= height) return false;
	MacroCell &cell = cells[indexFor(x, y)];
	const bool changed = !cell.known || cell.ch != ch || cell.attr != attr;
	cell.ch = ch;
	cell.attr = attr;
	cell.known = true;
	if (changed) markDirtyRow(y);
	return changed;
}

bool MacroCellGrid::copyCell(int dstX, int dstY, int srcX, int srcY) {
	if (dstX < 0 || dstY < 0 || srcX < 0 || srcY < 0 || dstX >= width || dstY >= height || srcX >= width || srcY >= height) return false;
	MacroCell &dst = cells[indexFor(dstX, dstY)];
	const MacroCell src = cells[indexFor(srcX, srcY)];
	const bool changed = dst.known != src.known || dst.ch != src.ch || dst.attr != src.attr;
	dst = src;
	if (changed) markDirtyRow(dstY);
	return changed;
}

bool MacroCellGrid::fillRect(int x1, int y1, int x2, int y2, char ch, uchar attr) {
	bool changed = false;
	x1 = std::max(0, std::min(x1, width - 1));
	x2 = std::max(0, std::min(x2, width - 1));
	y1 = std::max(0, std::min(y1, height - 1));
	y2 = std::max(0, std::min(y2, height - 1));
	if (x1 > x2 || y1 > y2) return false;
	for (int y = y1; y <= y2; ++y)
		for (int x = x1; x <= x2; ++x)
			changed = writeCell(x, y, ch, attr) || changed;
	return changed;
}

bool MacroCellGrid::writeString(int x, int y, const std::string &text, uchar attr) {
	if (text.empty() || y < 0 || y >= height) return false;
	bool changed = false;
	for (std::size_t i = 0; i < text.size(); ++i) {
		const int xx = x + static_cast<int>(i);
		if (xx < 0) continue;
		if (xx >= width) break;
		changed = writeCell(xx, y, text[i], attr) || changed;
	}
	return changed;
}

void MacroCellGrid::pushSnapshot(int x1, int y1, int x2, int y2) {
	MacroScreenBoxSnapshot snapshot;
	x1 = std::max(0, std::min(x1, width - 1));
	x2 = std::max(0, std::min(x2, width - 1));
	y1 = std::max(0, std::min(y1, height - 1));
	y2 = std::max(0, std::min(y2, height - 1));
	if (x1 > x2 || y1 > y2) return;

	snapshot.width = width;
	snapshot.height = height;
	snapshot.x1 = x1;
	snapshot.y1 = y1;
	snapshot.x2 = x2;
	snapshot.y2 = y2;
	snapshot.cells.reserve(static_cast<std::size_t>(x2 - x1 + 1) * static_cast<std::size_t>(y2 - y1 + 1));
	for (int y = y1; y <= y2; ++y) {
		const MacroCell *row = &cells[indexFor(x1, y)];
		snapshot.cells.insert(snapshot.cells.end(), row, row + (x2 - x1 + 1));
	}
	boxStack.push_back(std::move(snapshot));
}

void MacroCellGrid::projectRowSpan(MacroCellView &targetView, int y, int x1, int x2) {
	if (x1 > x2 || y < 0 || y >= height) return;
	std::vector<TScreenCell> row(static_cast<std::size_t>(x2 - x1 + 1));
	for (int x = x1; x <= x2; ++x) {
		const MacroCell &cell = cells[indexFor(x, y)];
		setCell(row[static_cast<std::size_t>(x - x1)], cell.ch, TColorAttr(cell.attr));
	}
	targetView.writeBuf(static_cast<short>(x1), static_cast<short>(y), static_cast<short>(x2 - x1 + 1), 1, row.data());
}

void MacroCellGrid::drawKnownCells(MacroCellView &targetView) {
	if (!ensureGeometry()) return;
	for (int y = 0; y < height; ++y) {
		int spanStart = -1;
		for (int x = 0; x <= width; ++x) {
			const bool known = x < width && cells[indexFor(x, y)].known;
			if (known && spanStart < 0) spanStart = x;
			else if (!known && spanStart >= 0) {
				projectRowSpan(targetView, y, spanStart, x - 1);
				spanStart = -1;
			}
		}
	}
}

void MacroCellGrid::projectDirtyRows(MacroCellView &targetView) {
	if (!ensureGeometry()) return;
	for (int y = 0; y < height; ++y) {
		if (y >= static_cast<int>(dirtyRows.size()) || dirtyRows[static_cast<std::size_t>(y)] == 0) continue;
		int spanStart = -1;
		for (int x = 0; x <= width; ++x) {
			const bool known = x < width && cells[indexFor(x, y)].known;
			if (known && spanStart < 0) spanStart = x;
			else if (!known && spanStart >= 0) {
				projectRowSpan(targetView, y, spanStart, x - 1);
				spanStart = -1;
			}
		}
	}
}

void MacroCellGrid::markDirtyRow(int y) noexcept {
	if (y < 0 || y >= height) return;
	if (dirtyRows.size() != static_cast<std::size_t>(height)) dirtyRows.assign(static_cast<std::size_t>(height), 0);
	dirtyRows[static_cast<std::size_t>(y)] = 1;
}

void MacroCellGrid::clearDirtyRows() noexcept {
	if (dirtyRows.empty()) return;
	std::fill(dirtyRows.begin(), dirtyRows.end(), static_cast<unsigned char>(0));
}

void MacroCellGrid::markFullProjection() noexcept {
	fullProjectionPending = true;
}

void MacroCellGrid::beginProjectionBatch() noexcept {
	++projectionBatchDepth;
}

void MacroCellGrid::endProjectionBatch() noexcept {
	if (projectionBatchDepth <= 0) return;
	--projectionBatchDepth;
	if (projectionBatchDepth == 0 && flushPending) {
		noteMacroScreenFlush();
		TScreen::flushScreen();
		flushPending = false;
	}
}

bool MacroCellGrid::hasDirtyRows() const noexcept {
	if (dirtyRows.size() != static_cast<std::size_t>(height)) return false;
	return std::find(dirtyRows.begin(), dirtyRows.end(), static_cast<unsigned char>(1)) != dirtyRows.end();
}

bool MacroCellGrid::hasKnownCells() const noexcept {
	return std::find_if(cells.begin(), cells.end(), [](const MacroCell &cell) { return cell.known; }) != cells.end();
}

void MacroCellGrid::projectAll() {
	if (!ensureView()) return;
	const auto [baseReprojectionNeeded, projectedOverlay] = UiScreenStateFacade::renderBaseThenOverlayIfNeeded(*this);
	if (baseReprojectionNeeded || projectedOverlay) {
		if (projectionBatchDepth > 0) flushPending = true;
		else {
			noteMacroScreenFlush();
			TScreen::flushScreen();
		}
	}
	if (baseReprojectionNeeded) {
		UiScreenStateFacade::noteBaseRedraw();
		geometryResetPending = false;
	}
	clearDirtyRows();
	fullProjectionPending = false;
}

void MacroCellGrid::redrawBaseAndOverlay() {
	if (!ensureView()) return;
	if (TProgram::application != nullptr) TProgram::application->drawView();
	markFullProjection();
	const bool projectedOverlay = UiScreenStateFacade::renderOverlay(*this);
	(void)projectedOverlay;
	if (projectionBatchDepth > 0) flushPending = true;
	else {
		noteMacroScreenFlush();
		TScreen::flushScreen();
	}
	UiScreenStateFacade::noteBaseRedraw();
	geometryResetPending = false;
	clearDirtyRows();
	fullProjectionPending = false;
}

bool MacroCellGrid::putBox(int x1, int y1, int x2, int y2, int bgColor, int fgColor, const std::string &title, bool shadow) {
	if (!ensureGeometry()) return true;
	x1 -= 1;
	y1 -= 1;
	x2 -= 1;
	y2 -= 1;
	if (x1 > x2) std::swap(x1, x2);
	if (y1 > y2) std::swap(y1, y2);
	x1 = std::max(0, std::min(x1, width - 1));
	x2 = std::max(0, std::min(x2, width - 1));
	y1 = std::max(0, std::min(y1, height - 1));
	y2 = std::max(0, std::min(y2, height - 1));
	if (x1 > x2 || y1 > y2) return true;

	const uchar attr = composeAttribute(bgColor, fgColor);
	bool changed = false;
	pushSnapshot(x1, y1, shadow ? x2 + 1 : x2, shadow ? y2 + 1 : y2);
	changed = fillRect(x1, y1, x2, y2, ' ', attr) || changed;
	for (int x = x1 + 1; x < x2; ++x) {
		changed = writeCell(x, y1, '-', attr) || changed;
		changed = writeCell(x, y2, '-', attr) || changed;
	}
	for (int y = y1 + 1; y < y2; ++y) {
		changed = writeCell(x1, y, '|', attr) || changed;
		changed = writeCell(x2, y, '|', attr) || changed;
	}
	changed = writeCell(x1, y1, '+', attr) || changed;
	changed = writeCell(x2, y1, '+', attr) || changed;
	changed = writeCell(x1, y2, '+', attr) || changed;
	changed = writeCell(x2, y2, '+', attr) || changed;

	std::string clippedTitle = title;
	if (!clippedTitle.empty() && x2 - x1 >= 2) {
		const int maxTitleLen = x2 - x1 - 1;
		if (static_cast<int>(clippedTitle.size()) > maxTitleLen) clippedTitle = clippedTitle.substr(0, static_cast<std::size_t>(maxTitleLen));
		const int titleStart = x1 + 1 + std::max(0, (maxTitleLen - static_cast<int>(clippedTitle.size())) / 2);
		changed = writeString(titleStart, y1, clippedTitle, attr) || changed;
	}

	if (shadow) {
		if (x2 + 1 < width) changed = fillRect(x2 + 1, y1 + 1, x2 + 1, y2 + 1, ' ', 0x08) || changed;
		if (y2 + 1 < height) changed = fillRect(x1 + 1, y2 + 1, x2 + 1, y2 + 1, ' ', 0x08) || changed;
	}
	if (changed) projectAll();
	return true;
}

bool MacroCellGrid::writeText(const std::string &text, int x, int y, int bgColor, int fgColor) {
	if (!ensureGeometry()) return true;
	if (writeString(x - 1, y - 1, text, composeAttribute(bgColor, fgColor))) projectAll();
	return true;
}

bool MacroCellGrid::clearLine(int col, int row, int count) {
	if (!ensureGeometry()) return true;
	int x = 0;
	int y = 0;
	int widthToClear = width;
	TApplication *app = dynamic_cast<TApplication *>(TProgram::application);

	if (col != 0 || row != 0 || count != 0) {
		x = std::max(0, col - 1);
		y = row - 1;
		widthToClear = count;
		if (y < 0 || y >= height || x >= width || widthToClear <= 0) return true;
		widthToClear = std::min(widthToClear, width - x);
	} else {
		y = app != nullptr ? std::max(0, std::min(app->cursor.y, height - 1)) : 0;
	}

	uchar attr = 0x07;
	const MacroCell &rowHead = cells[indexFor(0, y)];
	if (rowHead.known) attr = rowHead.attr;
	if (fillRect(x, y, x + widthToClear - 1, y, ' ', attr)) projectAll();
	return true;
}

bool MacroCellGrid::clearScreen(int attr) {
	if (!ensureGeometry()) return true;
	boxStack.clear();
	const bool changed = fillRect(0, 0, width - 1, height - 1, ' ', static_cast<uchar>(attr & 0xFF));
	bool cursorMoved = false;
	if (TApplication *app = dynamic_cast<TApplication *>(TProgram::application)) {
		cursorMoved = app->cursor.x != 0 || app->cursor.y != 0;
		app->setCursor(0, 0);
		app->showCursor();
	}
	if (changed || cursorMoved) projectAll();
	return true;
}

bool MacroCellGrid::scrollBox(int x1, int y1, int x2, int y2, int attr, bool down) {
	if (!ensureGeometry()) return true;
	x1 -= 1;
	y1 -= 1;
	x2 -= 1;
	y2 -= 1;
	if (x1 > x2) std::swap(x1, x2);
	if (y1 > y2) std::swap(y1, y2);
	x1 = std::max(0, std::min(x1, width - 1));
	x2 = std::max(0, std::min(x2, width - 1));
	y1 = std::max(0, std::min(y1, height - 1));
	y2 = std::max(0, std::min(y2, height - 1));
	if (x1 > x2 || y1 > y2) return true;

	const uchar fillAttr = static_cast<uchar>(attr & 0xFF);
	bool changed = false;
	if (y2 - y1 + 1 <= 1) {
		changed = fillRect(x1, y1, x2, y2, ' ', fillAttr);
		if (changed) projectAll();
		return true;
	}
	if (down) {
		for (int y = y2; y > y1; --y)
			for (int x = x1; x <= x2; ++x)
				changed = copyCell(x, y, x, y - 1) || changed;
		changed = fillRect(x1, y1, x2, y1, ' ', fillAttr) || changed;
	} else {
		for (int y = y1; y < y2; ++y)
			for (int x = x1; x <= x2; ++x)
				changed = copyCell(x, y, x, y + 1) || changed;
		changed = fillRect(x1, y2, x2, y2, ' ', fillAttr) || changed;
	}
	if (changed) projectAll();
	return true;
}

bool MacroCellGrid::putLineColOverlay(int line, int col, bool haveLine, bool haveCol) {
	if (!ensureGeometry()) return true;
	const int y = height - 1;
	const int fieldStart = std::max(0, width - 24);
	const std::string text = "L:" + std::to_string(haveLine ? line : 0) + " C:" + std::to_string(haveCol ? col : 0);
	bool changed = false;
	changed = fillRect(fieldStart, y, width - 1, y, ' ', 0x07) || changed;
	changed = writeString(std::max(fieldStart, width - static_cast<int>(text.size())), y, text, 0x07) || changed;
	if (changed) projectAll();
	return true;
}

bool MacroCellGrid::killBox() {
	if (!ensureGeometry()) return true;
	if (boxStack.empty()) {
		if (geometryResetPending) {
			redrawBaseAndOverlay();
			reapplyMacroLineColOverlayIfActive();
		}
		return true;
	}
	MacroScreenBoxSnapshot snapshot = std::move(boxStack.back());
	boxStack.pop_back();
	if (snapshot.width != width || snapshot.height != height) {
		boxStack.clear();
		markFullProjection();
		redrawBaseAndOverlay();
		reapplyMacroLineColOverlayIfActive();
		return true;
	}

	const int sourceWidth = snapshot.x2 - snapshot.x1 + 1;
	if (sourceWidth <= 0 || snapshot.y2 < snapshot.y1) return true;
	bool changed = false;
	for (int y = snapshot.y1; y <= snapshot.y2; ++y) {
		const std::size_t rowIndex = static_cast<std::size_t>(y - snapshot.y1) * static_cast<std::size_t>(sourceWidth);
		if (rowIndex + static_cast<std::size_t>(sourceWidth) > snapshot.cells.size()) break;
		for (int x = snapshot.x1; x <= snapshot.x2; ++x) {
			MacroCell &cell = cells[indexFor(x, y)];
			const MacroCell &restored = snapshot.cells[rowIndex + static_cast<std::size_t>(x - snapshot.x1)];
			if (cell.known != restored.known || cell.ch != restored.ch || cell.attr != restored.attr) {
				changed = true;
				markDirtyRow(y);
			}
			cell = restored;
		}
	}
	if (changed) {
		markFullProjection();
		redrawBaseAndOverlay();
		reapplyMacroLineColOverlayIfActive();
	}
	return true;
}

bool applyPutBoxProc(const std::string &name, const std::vector<Value> &args) {
	int x1 = 0;
	int y1 = 0;
	int x2 = 0;
	int y2 = 0;
	int bgColor = 0;
	int fgColor = 0;
	std::string title;
	bool shadow = false;

	if (args.size() != 8 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT || args[5].type != TYPE_INT || !isStringLike(args[6]) || args[7].type != TYPE_INT) throw std::runtime_error(name + " expects (int, int, int, int, int, int, string, int).");

	x1 = valueAsInt(args[0]);
	y1 = valueAsInt(args[1]);
	x2 = valueAsInt(args[2]);
	y2 = valueAsInt(args[3]);
	bgColor = valueAsInt(args[4]);
	fgColor = valueAsInt(args[5]);
	title = valueAsString(args[6]);
	shadow = valueAsInt(args[7]) != 0;

	g_macroCellGrid.putBox(x1, y1, x2, y2, bgColor, fgColor, title, shadow);
	return returnWithMacroScreenMutation(true);
}

bool applyWriteProc(const std::string &name, const std::vector<Value> &args) {
	std::string text;
	int x = 0;
	int y = 0;
	int bgColor = 0;
	int fgColor = 0;

	if (args.size() != 5 || !isStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT) throw std::runtime_error(name + " expects (string, int, int, int, int).");

	text = valueAsString(args[0]);
	x = valueAsInt(args[1]);
	y = valueAsInt(args[2]);
	bgColor = valueAsInt(args[3]);
	fgColor = valueAsInt(args[4]);

	g_macroCellGrid.writeText(text, x, y, bgColor, fgColor);
	return returnWithMacroScreenMutation(true);
}

bool applyClrLineProc(const std::string &name, const std::vector<Value> &args) {
	int col = 0;
	int row = 0;
	int count = 0;

	if (!(args.empty() || (args.size() == 3 && args[0].type == TYPE_INT && args[1].type == TYPE_INT && args[2].type == TYPE_INT))) throw std::runtime_error(name + " expects no arguments or (int, int, int).");

	if (!args.empty()) {
		col = valueAsInt(args[0]);
		row = valueAsInt(args[1]);
		count = valueAsInt(args[2]);
	}
	g_macroCellGrid.clearLine(col, row, count);
	return returnWithMacroScreenMutation(true);
}

bool applyGotoxyProc(const std::string &name, const std::vector<Value> &args) {
	TApplication *app = dynamic_cast<TApplication *>(TProgram::application);
	int width = static_cast<int>(TDisplay::getCols());
	int height = static_cast<int>(TDisplay::getRows());
	int x = 1;
	int y = 1;

	if (args.size() != 2 || args[0].type != TYPE_INT || args[1].type != TYPE_INT) throw std::runtime_error(name + " expects (int, int).");
	if (app == nullptr || width <= 0 || height <= 0) return true;

	x = std::max(1, std::min(valueAsInt(args[0]), width));
	y = std::max(1, std::min(valueAsInt(args[1]), height));
	app->setCursor(x - 1, y - 1);
	app->showCursor();
	app->drawCursor();
	return returnWithDirectScreenMutation(true);
}

bool applyPutLineColNumberProc(const std::string &name, const std::vector<Value> &args) {
	if (args.size() != 1 || args[0].type != TYPE_INT) throw std::runtime_error(name + " expects one integer argument.");

	if (name == "PUT_LINE_NUM") {
		g_macroScreenLineColOverlay.line = valueAsInt(args[0]);
		g_macroScreenLineColOverlay.haveLine = true;
	} else {
		g_macroScreenLineColOverlay.col = valueAsInt(args[0]);
		g_macroScreenLineColOverlay.haveCol = true;
	}
	renderMacroLineColOverlay();
	return returnWithMacroScreenMutation(true);
}

bool applyScrollBoxProc(const std::string &name, const std::vector<Value> &args, bool down) {
	int x1 = 0;
	int y1 = 0;
	int x2 = 0;
	int y2 = 0;
	int attr = 0x07;

	if (args.size() != 5 || args[0].type != TYPE_INT || args[1].type != TYPE_INT || args[2].type != TYPE_INT || args[3].type != TYPE_INT || args[4].type != TYPE_INT) throw std::runtime_error(name + " expects (int, int, int, int, int).");

	x1 = valueAsInt(args[0]);
	y1 = valueAsInt(args[1]);
	x2 = valueAsInt(args[2]);
	y2 = valueAsInt(args[3]);
	attr = valueAsInt(args[4]);
	g_macroCellGrid.scrollBox(x1, y1, x2, y2, attr, down);
	return returnWithMacroScreenMutation(true);
}

bool applyClearScreenProc(const std::string &name, const std::vector<Value> &args) {
	int attr = 0x07;

	if (!(args.empty() || (args.size() == 1 && args[0].type == TYPE_INT))) throw std::runtime_error(name + " expects no arguments or one integer argument.");
	if (!args.empty()) attr = valueAsInt(args[0]);
	g_macroCellGrid.clearScreen(attr);
	return returnWithMacroScreenMutation(true);
}

bool applyKillBoxProc(const std::string &name, const std::vector<Value> &args) {
	if (!args.empty()) throw std::runtime_error(name + " expects no arguments.");
	g_macroCellGrid.killBox();
	return returnWithMacroScreenMutation(true);
}

bool mrvmUiMarquee(int kind, const std::string &text) {
	try {
		std::vector<Value> args;
		std::string name = "MARQUEE";

		args.push_back(makeStringValue(text));
		if (kind > 0) name = (kind == 1) ? "MARQUEE_WARNING" : "MARQUEE_ERROR";
		return applyMarqueeProc(name, args);
	} catch (...) {
		return false;
	}
}

bool mrvmUiBrain(bool enabled) {
	try {
		std::vector<Value> args;
		args.push_back(makeIntValue(enabled ? 1 : 0));
		return applyBrainProc("BRAIN", args);
	} catch (...) {
		return false;
	}
}

bool mrvmUiPutBox(int x1, int y1, int x2, int y2, int bgColor, int fgColor, const std::string &title, int shadow) {
	try {
		std::vector<Value> args;
		args.push_back(makeIntValue(x1));
		args.push_back(makeIntValue(y1));
		args.push_back(makeIntValue(x2));
		args.push_back(makeIntValue(y2));
		args.push_back(makeIntValue(bgColor));
		args.push_back(makeIntValue(fgColor));
		args.push_back(makeStringValue(title));
		args.push_back(makeIntValue(shadow));
		return applyPutBoxProc("PUT_BOX", args);
	} catch (...) {
		return false;
	}
}

bool mrvmUiWrite(const std::string &text, int x, int y, int bgColor, int fgColor) {
	try {
		std::vector<Value> args;
		args.push_back(makeStringValue(text));
		args.push_back(makeIntValue(x));
		args.push_back(makeIntValue(y));
		args.push_back(makeIntValue(bgColor));
		args.push_back(makeIntValue(fgColor));
		return applyWriteProc("WRITE", args);
	} catch (...) {
		return false;
	}
}

bool mrvmUiClrLine(int col, int row, int count) {
	try {
		std::vector<Value> args;
		if (col != 0 || row != 0 || count != 0) {
			args.push_back(makeIntValue(col));
			args.push_back(makeIntValue(row));
			args.push_back(makeIntValue(count));
		}
		return applyClrLineProc("CLR_LINE", args);
	} catch (...) {
		return false;
	}
}

bool mrvmUiGotoxy(int x, int y) {
	try {
		std::vector<Value> args;
		args.push_back(makeIntValue(x));
		args.push_back(makeIntValue(y));
		return applyGotoxyProc("GOTOXY", args);
	} catch (...) {
		return false;
	}
}

bool mrvmUiPutLineNum(int line) {
	try {
		std::vector<Value> args;
		args.push_back(makeIntValue(line));
		return applyPutLineColNumberProc("PUT_LINE_NUM", args);
	} catch (...) {
		return false;
	}
}

bool mrvmUiPutColNum(int col) {
	try {
		std::vector<Value> args;
		args.push_back(makeIntValue(col));
		return applyPutLineColNumberProc("PUT_COL_NUM", args);
	} catch (...) {
		return false;
	}
}

bool mrvmUiScrollBoxUp(int x1, int y1, int x2, int y2, int attr) {
	try {
		std::vector<Value> args;
		args.push_back(makeIntValue(x1));
		args.push_back(makeIntValue(y1));
		args.push_back(makeIntValue(x2));
		args.push_back(makeIntValue(y2));
		args.push_back(makeIntValue(attr));
		return applyScrollBoxProc("SCROLL_BOX_UP", args, false);
	} catch (...) {
		return false;
	}
}

bool mrvmUiScrollBoxDn(int x1, int y1, int x2, int y2, int attr) {
	try {
		std::vector<Value> args;
		args.push_back(makeIntValue(x1));
		args.push_back(makeIntValue(y1));
		args.push_back(makeIntValue(x2));
		args.push_back(makeIntValue(y2));
		args.push_back(makeIntValue(attr));
		return applyScrollBoxProc("SCROLL_BOX_DN", args, true);
	} catch (...) {
		return false;
	}
}

bool mrvmUiClearScreen(int attr) {
	try {
		std::vector<Value> args;
		args.push_back(makeIntValue(attr));
		return applyClearScreenProc("CLEAR_SCREEN", args);
	} catch (...) {
		return false;
	}
}

bool mrvmUiKillBox() {
	try {
		std::vector<Value> args;
		return applyKillBoxProc("KILL_BOX", args);
	} catch (...) {
		return false;
	}
}

bool mrvmUiRegisterMenuItem(const std::string &menuTitle, const std::string &itemTitle, const std::string &macroSpec, const std::string &ownerSpec, std::string *errorMessage) {
	auto *app = dynamic_cast<TApplication *>(TProgram::application);
	auto *menuBar = app != nullptr ? dynamic_cast<MRMenuBar *>(app->menuBar) : nullptr;

	if (errorMessage != nullptr) errorMessage->clear();
	if (menuBar == nullptr) {
		if (errorMessage != nullptr) *errorMessage = "REGISTER_MENU_ITEM requires an active MRMenuBar.";
		return false;
	}
	return returnWithDirectScreenMutation(menuBar->registerRuntimeMenuItem(menuTitle, itemTitle, macroSpec, ownerSpec, errorMessage));
}

bool mrvmUiRemoveMenuItem(const std::string &menuTitle, const std::string &itemTitle, const std::string &ownerSpec, std::string *errorMessage) {
	auto *app = dynamic_cast<TApplication *>(TProgram::application);
	auto *menuBar = app != nullptr ? dynamic_cast<MRMenuBar *>(app->menuBar) : nullptr;

	if (errorMessage != nullptr) errorMessage->clear();
	if (menuBar == nullptr) {
		if (errorMessage != nullptr) *errorMessage = "REMOVE_MENU_ITEM requires an active MRMenuBar.";
		return false;
	}
	return returnWithDirectScreenMutation(menuBar->removeRuntimeMenuItem(menuTitle, itemTitle, ownerSpec, errorMessage));
}

bool mrvmUiRemoveRuntimeMenusOwnedByMacroSpec(const std::string &ownerSpec, std::string *errorMessage) {
	auto *app = dynamic_cast<TApplication *>(TProgram::application);
	auto *menuBar = app != nullptr ? dynamic_cast<MRMenuBar *>(app->menuBar) : nullptr;

	if (errorMessage != nullptr) errorMessage->clear();
	if (menuBar == nullptr) return true;
	return returnWithDirectScreenMutation(menuBar->removeRuntimeNodesOwnedByMacroSpec(ownerSpec, errorMessage));
}

bool mrvmUiRemoveRuntimeMenusOwnedByFile(const std::string &fileSpec, std::string *errorMessage) {
	auto *app = dynamic_cast<TApplication *>(TProgram::application);
	auto *menuBar = app != nullptr ? dynamic_cast<MRMenuBar *>(app->menuBar) : nullptr;

	if (errorMessage != nullptr) errorMessage->clear();
	if (menuBar == nullptr) return true;
	return returnWithDirectScreenMutation(menuBar->removeRuntimeNodesOwnedByFile(fileSpec, errorMessage));
}

bool mrvmUiSetRuntimeMenuKeyLabelForMacroSpec(const std::string &macroSpec, const std::string &keyLabel, std::string *errorMessage) {
	auto *app = dynamic_cast<TApplication *>(TProgram::application);
	auto *menuBar = app != nullptr ? dynamic_cast<MRMenuBar *>(app->menuBar) : nullptr;

	if (errorMessage != nullptr) errorMessage->clear();
	if (menuBar == nullptr) return true;
	return returnWithDirectScreenMutation(menuBar->setRuntimeMenuKeyLabelForMacroSpec(macroSpec, keyLabel));
}

bool mrvmUiClearRuntimeMenuKeyLabels(std::string *errorMessage) {
	auto *app = dynamic_cast<TApplication *>(TProgram::application);
	auto *menuBar = app != nullptr ? dynamic_cast<MRMenuBar *>(app->menuBar) : nullptr;

	if (errorMessage != nullptr) errorMessage->clear();
	if (menuBar == nullptr) return true;
	return returnWithDirectScreenMutation(menuBar->clearRuntimeMenuKeyLabels());
}

bool mrvmUiRefreshRuntimeMenus(std::string *errorMessage) {
	auto *app = dynamic_cast<TApplication *>(TProgram::application);
	auto *menuBar = app != nullptr ? dynamic_cast<MRMenuBar *>(app->menuBar) : nullptr;

	if (errorMessage != nullptr) errorMessage->clear();
	if (menuBar == nullptr) return true;
	return returnWithDirectScreenMutation(menuBar->refreshRuntimeMenus(errorMessage));
}

bool mrvmUiMessageBox(const std::string &text) {
	try {
		messageBox(mfInformation | mfOKButton, "%s", text.c_str());
		return returnWithDirectScreenMutation(true);
	} catch (...) {
		return false;
	}
}

struct ScreenRenderFacade {
	static bool renderDeferredCommand(const MRMacroDeferredUiCommand &command) {
		switch (command.type) {
			case mrducCreateWindow:
				return mrvmUiCreateWindow();
			case mrducDeleteWindow:
				return mrvmUiDeleteCurrentWindow();
			case mrducModifyWindow:
				return mrvmUiModifyCurrentWindow();
			case mrducLinkWindow:
				return mrvmUiLinkCurrentWindow();
			case mrducUnlinkWindow:
				return mrvmUiUnlinkCurrentWindow();
			case mrducZoom:
				return mrvmUiZoomCurrentWindow();
			case mrducRedraw:
				return mrvmUiRedrawCurrentWindow();
			case mrducNewScreen:
				return mrvmUiNewScreen();
			case mrducSwitchWindow:
				return mrvmUiSwitchWindow(command.a1);
			case mrducSizeWindow:
				return mrvmUiSizeCurrentWindow(command.a1, command.a2, command.a3, command.a4);
			case mrducMarqueeInfo:
				return mrvmUiMarquee(0, command.text);
			case mrducMarqueeWarning:
				return mrvmUiMarquee(1, command.text);
			case mrducMarqueeError:
				return mrvmUiMarquee(2, command.text);
			case mrducMakeMessage:
				return applyMakeMessageProc(std::vector<Value>{makeStringValue(command.text)});
			case mrducBrain:
				return mrvmUiBrain(command.a1 != 0);
			case mrducPutBox:
				return mrvmUiPutBox(command.a1, command.a2, command.a3, command.a4, command.a5, command.a6, command.text, command.a7);
			case mrducWrite:
				return mrvmUiWrite(command.text, command.a1, command.a2, command.a3, command.a4);
			case mrducClrLine:
				return mrvmUiClrLine(command.a1, command.a2, command.a3);
			case mrducGotoxy:
				return mrvmUiGotoxy(command.a1, command.a2);
			case mrducPutLineNum:
				return mrvmUiPutLineNum(command.a1);
			case mrducPutColNum:
				return mrvmUiPutColNum(command.a1);
			case mrducScrollBoxUp:
				return mrvmUiScrollBoxUp(command.a1, command.a2, command.a3, command.a4, command.a5);
			case mrducScrollBoxDn:
				return mrvmUiScrollBoxDn(command.a1, command.a2, command.a3, command.a4, command.a5);
			case mrducClearScreen:
				return mrvmUiClearScreen(command.a1);
			case mrducKillBox:
				return mrvmUiKillBox();
			case mrducRegisterMenuItem:
				return mrvmUiRegisterMenuItem(command.text, command.text2, command.text3, command.text4);
			case mrducRemoveMenuItem:
				return mrvmUiRemoveMenuItem(command.text, command.text2, command.text3);
			case mrducMessageBox:
				return mrvmUiMessageBox(command.text);
			case mrducDelay:
				return true;
			default:
				return false;
		}
	}
};

bool mrvmUiScreenRenderDeferredCommand(const MRMacroDeferredUiCommand &command) {
	return ScreenRenderFacade::renderDeferredCommand(command);
}
