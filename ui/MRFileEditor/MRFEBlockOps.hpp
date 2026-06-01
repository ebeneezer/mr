#ifndef MRFEBLOCKOPS_HPP
#define MRFEBLOCKOPS_HPP

#include <cstddef>
#include <string>

class MRFileEditor;

enum class MRFEBlockMode : int {
	None = 0,
	Line = 1,
	Column = 2,
	Stream = 3
};

enum class MRFEBlockStatus : int {
	Inactive = 0,
	Marking = 1,
	Committed = 2
};

struct MRFEBlockGeometry {
	MRFEBlockMode mode = MRFEBlockMode::None;
	MRFEBlockStatus status = MRFEBlockStatus::Inactive;
	bool hidden = false;
	std::size_t anchor = 0;
	std::size_t cursor = 0;
	std::size_t rangeStart = 0;
	std::size_t rangeEnd = 0;
	std::size_t line1 = 0;
	std::size_t line2 = 0;
	int col1 = 0;
	int col2 = 0;
	int anchorColumn = 0;
	int cursorColumn = 0;
};

class MRFEBlockOps {
  public:
	MRFEBlockOps();

	bool beginLine(MRFileEditor &editor);
	bool beginColumn(MRFileEditor &editor);
	bool beginStream(MRFileEditor &editor);
	bool end(MRFileEditor &editor);
	bool clear(MRFileEditor &editor);
	bool toggleVisibility(MRFileEditor &editor);
	bool updateFromEditor(MRFileEditor &editor);
	bool adoptMouseSelection(MRFileEditor &editor, unsigned short modifiers);
	bool refreshVisual(MRFileEditor &editor);
	bool moveCursorToStart(MRFileEditor &editor);
	bool moveCursorToEnd(MRFileEditor &editor);

	bool hasStoredBlock() const noexcept;
	bool hasVisibleBlock() const noexcept;
	bool isMarking() const noexcept;
	bool isHidden() const noexcept;
	MRFEBlockMode mode() const noexcept;
	MRFEBlockStatus status() const noexcept;
	const MRFEBlockGeometry &geometry() const noexcept;

  private:
	bool begin(MRFileEditor &editor, MRFEBlockMode mode);
	void normalize(MRFileEditor &editor);
	void applySelection(MRFileEditor &editor);
	void applyOverlay(MRFileEditor &editor);
	void deactivateVisual(MRFileEditor &editor);

	MRFEBlockGeometry mGeometry;
};

bool mrfeBlockOpsRegressionHarness(std::string &failureReason);

#endif
