#ifndef MRTEXTDOCUMENT_HPP
#define MRTEXTDOCUMENT_HPP

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace mr {
namespace editor {

using Offset = std::size_t;

struct Range {
	Offset start;
	Offset end;

	Range() noexcept : start(0), end(0) {
	}

	Range(Offset aStart, Offset aEnd) noexcept : start(aStart), end(aEnd) {
	}

	bool empty() const noexcept;
	Offset length() const noexcept;
	void normalize() noexcept;
	Range normalized() const noexcept;
	Range clamped(Offset maxOffset) const noexcept;
};

struct Cursor {
	Offset offset;

	Cursor() noexcept : offset(0) {
	}

	explicit Cursor(Offset aOffset) noexcept : offset(aOffset) {
	}

	void clamp(Offset maxOffset) noexcept;
};

struct Selection {
	Offset anchor;
	Offset cursor;

	Selection() noexcept : anchor(0), cursor(0) {
	}

	Selection(Offset anAnchor, Offset aCursor) noexcept : anchor(anAnchor), cursor(aCursor) {
	}

	bool empty() const noexcept;
	Range range() const noexcept;
	void clamp(Offset maxOffset) noexcept;
};

struct Snapshot {
	std::string text;
	std::size_t version;

	Snapshot() noexcept : text(), version(0) {
	}

	Snapshot(const std::string &aText, std::size_t aVersion) : text(aText), version(aVersion) {
	}
};

enum class BufferKind : unsigned char {
	Original,
	Add
};

struct TextSpan {
	Offset start;
	Offset length;

	TextSpan() noexcept : start(0), length(0) {
	}

	TextSpan(Offset aStart, Offset aLength) noexcept : start(aStart), length(aLength) {
	}

	bool empty() const noexcept;
	Offset end() const noexcept;
	TextSpan clamped(Offset maxLength) const noexcept;
};

struct Piece {
	BufferKind source;
	TextSpan span;

	Piece() noexcept : source(BufferKind::Original), span() {
	}

	Piece(BufferKind aSource, TextSpan aSpan) noexcept : source(aSource), span(aSpan) {
	}

	bool empty() const noexcept;
};

class MappedFileSource {
  public:
	MappedFileSource() noexcept : state_() {
	}

	bool openReadOnly(const std::string &path, std::string &error);
	void reset() noexcept;

	bool mapped() const noexcept {
		return static_cast<bool>(state_);
	}

	bool empty() const noexcept {
		return size() == 0;
	}

	std::size_t size() const noexcept;
	const char *data() const noexcept;
	const std::string &path() const noexcept;
	std::string sliceText(TextSpan span) const;

  private:
	struct State;
	std::shared_ptr<State> state_;
};

class AppendBuffer {
  public:
	AppendBuffer() noexcept : text_() {
	}

	TextSpan append(const std::string &text);
	void clear() noexcept;

	const std::string &text() const noexcept {
		return text_;
	}

	std::size_t size() const noexcept {
		return text_.size();
	}

	std::string sliceText(TextSpan span) const;

 private:
	std::string text_;
};

// Separate producer-side buffer for future async macro work; commits remain serialized.
class StagedAddBuffer {
  public:
	StagedAddBuffer() noexcept : text_() {
	}

	TextSpan append(const std::string &text);
	void clear() noexcept;

	bool empty() const noexcept {
		return text_.empty();
	}

	std::size_t size() const noexcept {
		return text_.size();
	}

	const std::string &text() const noexcept {
		return text_;
	}

	std::string sliceText(TextSpan span) const;

  private:
	std::string text_;
};

enum class EditKind : unsigned char {
	SetText,
	Insert,
	Erase,
	Replace
};

struct EditOperation {
	EditKind kind;
	Range range;
	std::string text;

	EditOperation() noexcept : kind(EditKind::Replace), range(), text() {
	}

	EditOperation(EditKind aKind, Range aRange, const std::string &aText)
	    : kind(aKind), range(aRange), text(aText) {
	}
};

class EditTransaction {
  public:
	EditTransaction() noexcept : label_(), operations_() {
	}

	explicit EditTransaction(const std::string &label) : label_(label), operations_() {
	}

	const std::string &label() const noexcept {
		return label_;
	}

	void setLabel(const std::string &label) {
		label_ = label;
	}

	bool empty() const noexcept {
		return operations_.empty();
	}

	const std::vector<EditOperation> &operations() const noexcept {
		return operations_;
	}

	void setText(const std::string &text);
	void insert(Offset offset, const std::string &text);
	void erase(Range range);
	void replace(Range range, const std::string &text);

  private:
	std::string label_;
	std::vector<EditOperation> operations_;
};

struct StagedEditOperation {
	EditKind kind;
	Range range;
	TextSpan span;

	StagedEditOperation() noexcept : kind(EditKind::Replace), range(), span() {
	}

	StagedEditOperation(EditKind aKind, Range aRange, TextSpan aSpan) noexcept
	    : kind(aKind), range(aRange), span(aSpan) {
	}
};

class StagedEditTransaction {
  public:
	StagedEditTransaction() noexcept : baseVersion_(0), label_(), addBuffer_(), operations_() {
	}

	explicit StagedEditTransaction(std::size_t aBaseVersion, const std::string &label = std::string())
	    : baseVersion_(aBaseVersion), label_(label), addBuffer_(), operations_() {
	}

	explicit StagedEditTransaction(const Snapshot &snapshot, const std::string &label = std::string())
	    : baseVersion_(snapshot.version), label_(label), addBuffer_(), operations_() {
	}

	std::size_t baseVersion() const noexcept {
		return baseVersion_;
	}

	void setBaseVersion(std::size_t value) noexcept {
		baseVersion_ = value;
	}

	const std::string &label() const noexcept {
		return label_;
	}

	void setLabel(const std::string &label) {
		label_ = label;
	}

	bool empty() const noexcept {
		return operations_.empty();
	}

	const StagedAddBuffer &buffer() const noexcept {
		return addBuffer_;
	}

	StagedAddBuffer &buffer() noexcept {
		return addBuffer_;
	}

	const std::vector<StagedEditOperation> &operations() const noexcept {
		return operations_;
	}

	void setText(const std::string &text);
	void insert(Offset offset, const std::string &text);
	void erase(Range range);
	void replace(Range range, const std::string &text);

  private:
	std::size_t baseVersion_;
	std::string label_;
	StagedAddBuffer addBuffer_;
	std::vector<StagedEditOperation> operations_;
};

struct DocumentChangeSet {
	bool changed;
	Range touchedRange;
	Offset oldLength;
	Offset newLength;
	std::size_t oldVersion;
	std::size_t newVersion;

	DocumentChangeSet() noexcept
	    : changed(false), touchedRange(), oldLength(0), newLength(0), oldVersion(0), newVersion(0) {
	}
};

enum class CommitStatus : unsigned char {
	Applied,
	VersionConflict,
	NoOp
};

struct CommitResult {
	CommitStatus status;
	std::size_t expectedVersion;
	std::size_t actualVersion;
	DocumentChangeSet change;

	CommitResult() noexcept
	    : status(CommitStatus::NoOp), expectedVersion(0), actualVersion(0), change() {
	}

	bool applied() const noexcept {
		return status == CommitStatus::Applied;
	}

	bool conflicted() const noexcept {
		return status == CommitStatus::VersionConflict;
	}

	bool changed() const noexcept {
		return change.changed;
	}
};

struct PieceChunkView {
	const char *data;
	Offset length;

	PieceChunkView() noexcept : data(nullptr), length(0) {
	}

	PieceChunkView(const char *aData, Offset aLength) noexcept : data(aData), length(aLength) {
	}
};

struct LineIndexCheckpoint {
	Offset offset;
	std::size_t lineIndex;

	LineIndexCheckpoint() noexcept : offset(0), lineIndex(0) {
	}

	LineIndexCheckpoint(Offset anOffset, std::size_t aLineIndex) noexcept
	    : offset(anOffset), lineIndex(aLineIndex) {
	}
};

class TextDocument {
  public:
	TextDocument() noexcept;
	explicit TextDocument(const std::string &text);

	const std::string &text() const noexcept;
	Offset length() const noexcept;
	bool empty() const noexcept;
	char charAt(Offset pos) const noexcept;
	Snapshot snapshot() const;

	std::size_t version() const noexcept {
		return version_;
	}

	bool matchesVersion(std::size_t expectedVersion) const noexcept {
		return expectedVersion == version_;
	}

	bool matchesSnapshot(const Snapshot &snapshot) const noexcept {
		return matchesVersion(snapshot.version);
	}

	std::size_t originalLength() const noexcept {
		return mappedOriginal_.mapped() ? mappedOriginal_.size() : originalBuffer_.size();
	}

	std::size_t addBufferLength() const noexcept {
		return addBuffer_.size();
	}

	std::size_t pieceCount() const noexcept {
		return pieces_.size();
	}

	bool loadMappedFile(const std::string &path, std::string &error);
	bool hasMappedOriginal() const noexcept {
		return mappedOriginal_.mapped();
	}

	const std::string &mappedPath() const noexcept {
		return mappedOriginal_.path();
	}

	PieceChunkView pieceChunk(std::size_t index) const noexcept;

	void setText(const std::string &text);
	void apply(const EditTransaction &transaction);
	CommitResult tryApply(const EditTransaction &transaction, std::size_t expectedVersion);
	CommitResult tryApply(const StagedEditTransaction &transaction);
	void insert(Offset offset, const std::string &text);
	void erase(Range range);
	void replace(Range range, const std::string &text);
	void insertFromStaged(Offset offset, const StagedAddBuffer &buffer, TextSpan span);
	void replaceFromStaged(Range range, const StagedAddBuffer &buffer, TextSpan span);

	Offset clampOffset(Offset pos) const noexcept;
	std::size_t lineCount() const noexcept;
	Offset lineStart(Offset pos) const noexcept;
	Offset lineEnd(Offset pos) const noexcept;
	Offset nextLine(Offset pos) const noexcept;
	Offset prevLine(Offset pos) const noexcept;
	std::size_t lineIndex(Offset pos) const noexcept;
	Offset lineStartByIndex(std::size_t index) const noexcept;
	std::size_t estimatedLineCount() const noexcept;
	bool exactLineCountKnown() const noexcept;
	std::size_t column(Offset pos) const noexcept;
	std::string lineText(Offset pos) const;

  private:
	bool isLineBreakChar(char ch) const noexcept;
	void initializeFromOriginal(const std::string &text, bool bumpVersionFlag);
	void initializeFromMappedSource(const MappedFileSource &source, bool bumpVersionFlag);
	void bumpVersion() noexcept;
	void markDirty() noexcept;
	void ensureMaterialized() const noexcept;
	std::string pieceText(const Piece &piece) const;
	const char *originalData() const noexcept;
	bool setTextNoVersionBump(const std::string &text);
	bool applyOperationNoVersionBump(const EditOperation &operation);
	bool applyStagedOperationNoVersionBump(const StagedEditOperation &operation,
	                                       const StagedAddBuffer &buffer);
	bool replaceNoVersionBump(Range range, const std::string &text);
	bool hasDirectOriginalView() const noexcept;
	const char *directTextData() const noexcept;
	void resetLazyLineIndex() noexcept;
	bool directAdvanceLine(Offset &offset) const noexcept;
	void ensureLazyIndexSeeded() const noexcept;
	void advanceLazyIndexByStride() const noexcept;
	void ensureLazyIndexForLine(std::size_t targetLine) const noexcept;
	void ensureLazyIndexForOffset(Offset targetOffset) const noexcept;
	void ensureLazyIndexComplete() const noexcept;

	std::size_t splitAt(Offset offset);
	bool eraseNoVersionBump(Range range);
	bool insertAddSpanNoVersionBump(Offset offset, TextSpan span);
	void compactPieces();

	std::string originalBuffer_;
	MappedFileSource mappedOriginal_;
	AppendBuffer addBuffer_;
	std::vector<Piece> pieces_;
	Offset length_;
	std::size_t version_;
	mutable bool cacheDirty_;
	mutable std::string materializedText_;
	mutable std::vector<LineIndexCheckpoint> lineIndexCheckpoints_;
	mutable Offset lazyIndexedOffset_;
	mutable std::size_t lazyIndexedLine_;
	mutable bool lazyLineIndexComplete_;
	mutable std::size_t lazyTotalLineCount_;
};

} // namespace editor
} // namespace mr

#endif
