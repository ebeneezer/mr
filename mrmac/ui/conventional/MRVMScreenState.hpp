#ifndef MRVMSCREENSTATE_HPP
#define MRVMSCREENSTATE_HPP

#define Uses_TView
#include <tvision/tv.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mrvm_screen {

class MacroCellGrid;

struct UiScreenStateFacade {
	static std::uint64_t nextGeneration() noexcept;
	static void noteMacroOverlayMutation() noexcept;
	static void noteBaseMutation() noexcept;
	static void noteBaseRedraw() noexcept;
	[[nodiscard]] static bool needsOverlayReprojection() noexcept;
	[[nodiscard]] static std::pair<bool, bool> renderBaseThenOverlayIfNeeded(MacroCellGrid &grid) noexcept;
	[[nodiscard]] static bool renderOverlay(MacroCellGrid &grid) noexcept;
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
	explicit MacroCellView(const TRect &bounds) noexcept;
	void draw() override;
};

class MacroCellGrid {
  public:
	MacroCellGrid();
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
	void storeState();

  private:
	friend struct UiScreenStateFacade;

	int width = 0;
	int height = 0;
	std::vector<MacroCell> cells;
	std::vector<MacroScreenBoxSnapshot> boxStack;
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
	void loadState();
	static std::string serializeCells(const std::vector<MacroCell> &values);
	static std::vector<MacroCell> deserializeCells(const std::string &encoded);
	static std::string serializeSnapshot(const MacroScreenBoxSnapshot &snapshot);
	static bool deserializeSnapshot(const std::string &encoded, MacroScreenBoxSnapshot &snapshot);

	std::vector<unsigned char> dirtyRows;
	bool fullProjectionPending = true;
	bool geometryResetPending = false;
	int projectionBatchDepth = 0;
	bool flushPending = false;
};

void noteMacroScreenFlush() noexcept;

} // namespace mrvm_screen

#endif
