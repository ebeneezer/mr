#ifndef MRFEBLOCKOPS_HPP
#define MRFEBLOCKOPS_HPP

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

class MRFileEditor;
class MREditWindow;
class MRFEBlockOpsTestPeer;

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

  private:
	bool hasVisibleBlock() const noexcept;
	bool isMarking() const noexcept;
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
	bool loadBlockFromFile(MRFileEditor &editor, const std::string &path, std::string *errorText = nullptr);
	bool saveBlockToFile(MRFileEditor &editor, const std::string &path, std::string *errorText = nullptr);
	bool setCommittedStream(MRFileEditor &editor, std::size_t start, std::size_t end);
	bool setCommittedBlock(MRFileEditor &editor, MRFEBlockMode mode, std::size_t anchor, std::size_t cursor, int anchorColumn = -1, int cursorColumn = -1);

	bool begin(MRFileEditor &editor, MRFEBlockMode mode);
	void normalize(MRFileEditor &editor);
	void applySelection(MRFileEditor &editor);
	void applyOverlay(MRFileEditor &editor);
	void deactivateVisual(MRFileEditor &editor);

	enum class TransferMode : int {
		Copy = 0,
		Move = 1
	};

	enum class BlockOperation : int {
		Copy = 0,
		Move = 1,
		Delete = 2,
		Indent = 3,
		Undent = 4
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

	bool runBlockOperation(MRFileEditor &editor, BlockOperation operation, std::string *errorText = nullptr);
	bool runWindowBlockOperation(MRFileEditor &sourceEditor, MRFEBlockOps &targetOps, MRFileEditor &targetEditor, int sourceWindowId, int targetWindowId, BlockOperation operation, std::string *errorText = nullptr);
	bool executeCursorMove(MRFileEditor &editor, std::string *errorText = nullptr);
	bool executeDelete(MRFileEditor &editor, std::string *errorText = nullptr);
	bool shiftCurrentBlockToTab(MRFileEditor &editor, bool indent, std::string *errorText = nullptr);
	bool shiftCurrentColumnBlockToTab(MRFileEditor &editor, bool indent, std::string *errorText = nullptr);
	bool shiftCurrentLineBlockToTab(MRFileEditor &editor, bool indent, std::string *errorText = nullptr);
	bool shiftCurrentStreamBlockToTab(MRFileEditor &editor, bool indent, std::string *errorText = nullptr);
	bool captureTransferPayload(MRFileEditor &editor, MRFEArenaAllocator &arena, std::string *errorText = nullptr);
	bool prepareTransferMessage(MRFileEditor &editor, int sourceWindowId, int targetWindowId, TransferMode mode, MRFEArenaAllocator &arena, TransferMessage &message, std::string *errorText = nullptr);
	bool insertTransferMessage(MRFileEditor &editor, const TransferMessage &message, std::string *errorText = nullptr);
	bool removeCurrentBlockForMove(MRFileEditor &editor, std::string *errorText = nullptr);
	bool shiftCurrentColumnBlockHorizontally(MRFileEditor &editor, int destCol, ColumnHorizontalShiftMode mode, std::string *errorText = nullptr);
	bool shiftCurrentStreamBlockHorizontally(MRFileEditor &editor, int destCol, StreamHorizontalShiftMode mode, std::string *errorText = nullptr);

	MRFEBlockGeometry mGeometry;
	MRFEArenaAllocator mArena;

	friend class MREditWindow;
	friend class MRFEBlockOpsTestPeer;
	friend bool mrfeBlockOpsRegressionHarness(std::string &failureReason);
};

#endif
