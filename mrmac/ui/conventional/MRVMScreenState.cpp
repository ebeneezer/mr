#include "MRVMScreenState.hpp"
#include "MRVMScreen.hpp"
#include "../../vm/MRVMRuntimeState.hpp"

#define Uses_TApplication
#define Uses_TDisplay
#define Uses_TProgram
#define Uses_TScreen
#include <tvision/tv.h>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace mrvm_screen {
namespace {

MacroCellView *macroCellView = nullptr;

static uchar composeScreenAttribute(int bgColor, int fgColor) noexcept {
	if ((bgColor & 0xFF) == 0) return static_cast<uchar>(fgColor & 0xFF);
	return static_cast<uchar>(((bgColor & 0x0F) << 4) | (fgColor & 0x0F));
}

} // namespace

void noteMacroScreenFlush() noexcept {
	mrvmStoreRuntimeStateSize("macroScreen", "flushCount", mrvmRuntimeStateSize("macroScreen", "flushCount") + 1);
}

std::uint64_t UiScreenStateFacade::nextGeneration() noexcept {
	const std::size_t generation = mrvmRuntimeStateSize("macroScreen", "mutationEpoch", 1) + 1;
	mrvmStoreRuntimeStateSize("macroScreen", "mutationEpoch", generation);
	return generation;
}

void UiScreenStateFacade::noteMacroOverlayMutation() noexcept {
	mrvmStoreRuntimeStateSize("macroScreen", "overlayGeneration", nextGeneration());
}

void UiScreenStateFacade::noteBaseMutation() noexcept {
	mrvmStoreRuntimeStateSize("macroScreen", "baseGeneration", nextGeneration());
	mrvmStoreRuntimeStateInt("macroScreen", "baseInvalidated", 1);
}

void UiScreenStateFacade::noteBaseRedraw() noexcept {
	mrvmStoreRuntimeStateSize("macroScreen", "baseGeneration", mrvmUiScreenMutationEpoch());
	mrvmStoreRuntimeStateInt("macroScreen", "baseInvalidated", 0);
}

bool UiScreenStateFacade::needsOverlayReprojection() noexcept {
	return mrvmRuntimeStateInt("macroScreen", "baseInvalidated") != 0;
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
	if (!grid.ensureView() || macroCellView == nullptr || !grid.hasKnownCells()) return false;
	if (grid.fullProjectionPending) {
		grid.projectDirtyRows(*macroCellView);
		if (grid.hasDirtyRows()) return true;
		grid.drawKnownCells(*macroCellView);
		return true;
	}
	if (!grid.hasDirtyRows()) return false;
	grid.projectDirtyRows(*macroCellView);
	return true;
}

MacroCellView::MacroCellView(const TRect &bounds) noexcept : TView(bounds) {
	growMode = gfGrowHiX | gfGrowHiY;
	options &= static_cast<ushort>(~ofSelectable);
}

void MacroCellView::draw() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MacroCellGrid grid;
	grid.drawKnownCells(*this);
	grid.storeState();
}

MacroCellGrid::MacroCellGrid() {
	loadState();
}

std::string MacroCellGrid::serializeCells(const std::vector<MacroCell> &values) {
	std::string encoded;

	encoded.reserve(values.size() * 3);
	for (const MacroCell &cell : values) {
		encoded.push_back(cell.ch);
		encoded.push_back(static_cast<char>(cell.attr));
		encoded.push_back(cell.known ? '\1' : '\0');
	}
	return encoded;
}

std::vector<MacroCell> MacroCellGrid::deserializeCells(const std::string &encoded) {
	std::vector<MacroCell> values;

	values.reserve(encoded.size() / 3);
	for (std::size_t i = 0; i + 2 < encoded.size(); i += 3) {
		MacroCell cell;
		cell.ch = encoded[i];
		cell.attr = static_cast<uchar>(static_cast<unsigned char>(encoded[i + 1]));
		cell.known = encoded[i + 2] != '\0';
		values.push_back(cell);
	}
	return values;
}

std::string MacroCellGrid::serializeSnapshot(const MacroScreenBoxSnapshot &snapshot) {
	std::string encoded = std::to_string(snapshot.width) + "," + std::to_string(snapshot.height) + "," + std::to_string(snapshot.x1) + "," + std::to_string(snapshot.y1) + "," + std::to_string(snapshot.x2) + "," + std::to_string(snapshot.y2) + "|";

	encoded += serializeCells(snapshot.cells);
	return encoded;
}

bool MacroCellGrid::deserializeSnapshot(const std::string &encoded, MacroScreenBoxSnapshot &snapshot) {
	const std::size_t separator = encoded.find('|');
	if (separator == std::string::npos) return false;
	const std::string header = encoded.substr(0, separator);
	if (std::sscanf(header.c_str(), "%d,%d,%d,%d,%d,%d", &snapshot.width, &snapshot.height, &snapshot.x1, &snapshot.y1, &snapshot.x2, &snapshot.y2) != 6) return false;
	snapshot.cells = deserializeCells(encoded.substr(separator + 1));
	return true;
}

void MacroCellGrid::loadState() {
	width = mrvmRuntimeStateInt("macroScreen", "width");
	height = mrvmRuntimeStateInt("macroScreen", "height");
	cells = deserializeCells(mrvmRuntimeStateString("macroScreen", "cells"));
	const std::vector<int> storedDirtyRows = mrvmRuntimeStateIntList("macroScreen", "dirtyRows");
	dirtyRows.reserve(storedDirtyRows.size());
	for (int dirty : storedDirtyRows)
		dirtyRows.push_back(dirty != 0 ? 1 : 0);
	boxStack.clear();
	for (const std::string &encoded : mrvmRuntimeStateStringList("macroScreen", "boxStack")) {
		MacroScreenBoxSnapshot snapshot;
		if (deserializeSnapshot(encoded, snapshot)) boxStack.push_back(std::move(snapshot));
	}
	fullProjectionPending = mrvmRuntimeStateInt("macroScreen", "fullProjectionPending", 1) != 0;
	geometryResetPending = mrvmRuntimeStateInt("macroScreen", "geometryResetPending") != 0;
	projectionBatchDepth = mrvmRuntimeStateInt("macroScreen", "projectionBatchDepth");
	flushPending = mrvmRuntimeStateInt("macroScreen", "flushPending") != 0;
}

void MacroCellGrid::storeState() {
	std::vector<int> storedDirtyRows;
	std::vector<std::string> storedSnapshots;

	storedDirtyRows.reserve(dirtyRows.size());
	for (unsigned char dirty : dirtyRows)
		storedDirtyRows.push_back(dirty != 0 ? 1 : 0);
	storedSnapshots.reserve(boxStack.size());
	for (const MacroScreenBoxSnapshot &snapshot : boxStack)
		storedSnapshots.push_back(serializeSnapshot(snapshot));
	mrvmStoreRuntimeStateInt("macroScreen", "width", width);
	mrvmStoreRuntimeStateInt("macroScreen", "height", height);
	mrvmStoreRuntimeStateString("macroScreen", "cells", serializeCells(cells));
	mrvmStoreRuntimeStateIntList("macroScreen", "dirtyRows", storedDirtyRows);
	mrvmStoreRuntimeStateStringList("macroScreen", "boxStack", storedSnapshots);
	mrvmStoreRuntimeStateInt("macroScreen", "fullProjectionPending", fullProjectionPending ? 1 : 0);
	mrvmStoreRuntimeStateInt("macroScreen", "geometryResetPending", geometryResetPending ? 1 : 0);
	mrvmStoreRuntimeStateInt("macroScreen", "projectionBatchDepth", projectionBatchDepth);
	mrvmStoreRuntimeStateInt("macroScreen", "flushPending", flushPending ? 1 : 0);
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
	if (macroCellView != nullptr) {
		TRect bounds(0, 0, static_cast<short>(width), static_cast<short>(height));
		macroCellView->locate(bounds);
	}
	return true;
}

bool MacroCellGrid::ensureView() {
	if (!ensureGeometry() || TProgram::application == nullptr) return false;
	if (macroCellView != nullptr && macroCellView->owner != nullptr) return true;

	TRect bounds(0, 0, static_cast<short>(width), static_cast<short>(height));
	macroCellView = new MacroCellView(bounds);
	TProgram::application->insert(macroCellView);
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
			const bool haveLine = mrvmRuntimeStateInt("macroScreen", "haveLine") != 0;
			const bool haveCol = mrvmRuntimeStateInt("macroScreen", "haveCol") != 0;
			if (haveLine || haveCol) putLineColOverlay(mrvmRuntimeStateInt("macroScreen", "line"), mrvmRuntimeStateInt("macroScreen", "col"), haveLine, haveCol);
		}
		return true;
	}
	MacroScreenBoxSnapshot snapshot = std::move(boxStack.back());
	boxStack.pop_back();
	if (snapshot.width != width || snapshot.height != height) {
		boxStack.clear();
		markFullProjection();
		redrawBaseAndOverlay();
		const bool haveLine = mrvmRuntimeStateInt("macroScreen", "haveLine") != 0;
		const bool haveCol = mrvmRuntimeStateInt("macroScreen", "haveCol") != 0;
		if (haveLine || haveCol) putLineColOverlay(mrvmRuntimeStateInt("macroScreen", "line"), mrvmRuntimeStateInt("macroScreen", "col"), haveLine, haveCol);
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
		const bool haveLine = mrvmRuntimeStateInt("macroScreen", "haveLine") != 0;
		const bool haveCol = mrvmRuntimeStateInt("macroScreen", "haveCol") != 0;
		if (haveLine || haveCol) putLineColOverlay(mrvmRuntimeStateInt("macroScreen", "line"), mrvmRuntimeStateInt("macroScreen", "col"), haveLine, haveCol);
	}
	return true;
}


} // namespace mrvm_screen
