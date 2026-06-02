#ifndef MRFEBLOCKOPS_HPP
#define MRFEBLOCKOPS_HPP

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

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

class MRFEArenaAllocator {
  public:
	MRFEArenaAllocator();

	void clear() noexcept;
	bool assign(std::string_view text);
	bool append(std::string_view text);
	bool appendFill(std::size_t count, char value);
	bool loadFile(const std::string &path, std::string *errorText = nullptr);
	bool writeFile(const std::string &path, std::string *errorText = nullptr) const;
	std::vector<char> release() noexcept;
	void logContents(std::string_view label) const;

	const char *data() const noexcept;
	std::size_t size() const noexcept;
	bool empty() const noexcept;
	std::string_view view() const noexcept;

  private:
	std::vector<char> mStorage;
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
	bool captureCurrentBlockPayload(MRFileEditor &editor, MRFEArenaAllocator &arena, std::string *errorText = nullptr);
	bool insertPayloadAsStreamBlock(MRFileEditor &editor, const MRFEArenaAllocator &arena, std::string *errorText = nullptr);
	bool copyCurrentBlockToCursor(MRFileEditor &editor, std::string *errorText = nullptr);
	bool copyCurrentBlockToEditor(MRFileEditor &sourceEditor, MRFEBlockOps &targetOps, MRFileEditor &targetEditor, int sourceWindowId, int targetWindowId, std::string *errorText = nullptr);
	bool moveCurrentBlockToCursor(MRFileEditor &editor, std::string *errorText = nullptr);
	bool moveCurrentBlockToEditor(MRFileEditor &sourceEditor, MRFEBlockOps &targetOps, MRFileEditor &targetEditor, int sourceWindowId, int targetWindowId, std::string *errorText = nullptr);
	bool deleteCurrentBlock(MRFileEditor &editor, std::string *errorText = nullptr);
	bool indentCurrentColumnBlock(MRFileEditor &editor, std::string *errorText = nullptr);
	bool undentCurrentColumnBlock(MRFileEditor &editor, std::string *errorText = nullptr);
	bool indentCurrentLineBlock(MRFileEditor &editor, std::string *errorText = nullptr);
	bool undentCurrentLineBlock(MRFileEditor &editor, std::string *errorText = nullptr);
	bool indentCurrentStreamBlock(MRFileEditor &editor, std::string *errorText = nullptr);
	bool undentCurrentStreamBlock(MRFileEditor &editor, std::string *errorText = nullptr);
	bool loadStreamBlockFromFile(MRFileEditor &editor, const std::string &path, std::string *errorText = nullptr);
	bool saveStreamBlockToFile(MRFileEditor &editor, const std::string &path, std::string *errorText = nullptr);
	bool setCommittedStream(MRFileEditor &editor, std::size_t start, std::size_t end);
	bool setCommittedBlock(MRFileEditor &editor, MRFEBlockMode mode, std::size_t anchor, std::size_t cursor, int anchorColumn = -1, int cursorColumn = -1);

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

	enum class TransferMode : int {
		Copy = 0,
		Move = 1
	};

	enum class ColumnHorizontalShiftMode : int {
		UseEditorInsertMode = 0,
		ForceOverwritePayload = 1
	};

	enum class StreamHorizontalShiftMode : int {
		MoveToCursor = 0,
		IndentToTab = 1
	};

	struct TransferMessage {
		int sourceWindowId = 0;
		int targetWindowId = 0;
		TransferMode mode = TransferMode::Copy;
		MRFEBlockMode blockMode = MRFEBlockMode::None;
		MRFEBlockGeometry geometry;
		std::size_t rowCount = 0;
		std::size_t columnWidth = 0;
		std::vector<char> payload;
	};

	bool captureTransferPayload(MRFileEditor &editor, MRFEArenaAllocator &arena, std::string *errorText = nullptr);
	bool prepareTransferMessage(MRFileEditor &editor, int sourceWindowId, int targetWindowId, TransferMode mode, MRFEArenaAllocator &arena, TransferMessage &message, std::string *errorText = nullptr);
	bool insertTransferMessage(MRFileEditor &editor, const TransferMessage &message, std::string *errorText = nullptr);
	bool removeCurrentBlockForMove(MRFileEditor &editor, std::string *errorText = nullptr);
	bool shiftCurrentColumnBlockHorizontally(MRFileEditor &editor, int destCol, ColumnHorizontalShiftMode mode, std::string *errorText = nullptr);
	bool shiftCurrentStreamBlockHorizontally(MRFileEditor &editor, int destCol, StreamHorizontalShiftMode mode, std::string *errorText = nullptr);

	MRFEBlockGeometry mGeometry;
	MRFEArenaAllocator mArena;

	friend bool mrfeBlockOpsRegressionHarness(std::string &failureReason);
};

bool mrfeBlockOpsRegressionHarness(std::string &failureReason);

#endif
