#ifndef MRTEXTDOCUMENT_HPP
#define MRTEXTDOCUMENT_HPP

#include <atomic>
#include <cstddef>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
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

	[[nodiscard]] bool empty() const noexcept;
	[[nodiscard]] Offset length() const noexcept;
	void normalize() noexcept;
	[[nodiscard]] Range normalized() const noexcept;
	[[nodiscard]] Range clamped(Offset maxOffset) const noexcept;
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

	[[nodiscard]] bool empty() const noexcept;
	[[nodiscard]] Range range() const noexcept;
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

	[[nodiscard]] bool empty() const noexcept;
	[[nodiscard]] Offset end() const noexcept;
	[[nodiscard]] TextSpan clamped(Offset maxLength) const noexcept;
};

struct Piece {
	BufferKind source;
	TextSpan span;

	Piece() noexcept : source(BufferKind::Original), span() {
	}

	Piece(BufferKind aSource, TextSpan aSpan) noexcept : source(aSource), span(aSpan) {
	}

	[[nodiscard]] bool empty() const noexcept;
};

class MappedFileSource {
  public:
	MappedFileSource() noexcept : mState() {
	}

	bool openReadOnly(const std::string &path, std::string &error);
	void reset() noexcept;

	[[nodiscard]] bool mapped() const noexcept {
		return static_cast<bool>(mState);
	}

	[[nodiscard]] bool empty() const noexcept {
		return size() == 0;
	}

	[[nodiscard]] std::size_t size() const noexcept;
	[[nodiscard]] const char *data() const noexcept;
	[[nodiscard]] const std::string &path() const noexcept;
	[[nodiscard]] std::string sliceText(TextSpan span) const;

  private:
	struct State;
	std::shared_ptr<State> mState;
};

class AppendBuffer {
  public:
	AppendBuffer() noexcept : mText() {
	}

	TextSpan append(std::string_view text);
	void clear() noexcept;

	[[nodiscard]] const std::string &text() const noexcept {
		return mText;
	}

	[[nodiscard]] std::size_t size() const noexcept {
		return mText.size();
	}

	[[nodiscard]] std::string sliceText(TextSpan span) const;

  private:
	std::string mText;
};

class ReadSnapshot;

// Separate producer-side buffer for future async macro work; commits remain serialized.
class StagedAddBuffer {
  public:
	StagedAddBuffer() noexcept : mText() {
	}

	TextSpan append(std::string_view text);
	void clear() noexcept;

	[[nodiscard]] bool empty() const noexcept {
		return mText.empty();
	}

	[[nodiscard]] std::size_t size() const noexcept {
		return mText.size();
	}

	[[nodiscard]] const std::string &text() const noexcept {
		return mText;
	}

	[[nodiscard]] std::string sliceText(TextSpan span) const;

  private:
	std::string mText;
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

	EditOperation(EditKind aKind, Range aRange, std::string_view aText) : kind(aKind), range(aRange), text(aText) {
	}
};

class EditTransaction {
  public:
	EditTransaction() noexcept : mLabel(), mOperations() {
	}

	explicit EditTransaction(std::string_view label) : mLabel(label), mOperations() {
	}

	[[nodiscard]] const std::string &label() const noexcept {
		return mLabel;
	}

	void setLabel(std::string_view label) {
		mLabel = label;
	}

	[[nodiscard]] bool empty() const noexcept {
		return mOperations.empty();
	}

	[[nodiscard]] const std::vector<EditOperation> &operations() const noexcept {
		return mOperations;
	}

	void setText(std::string_view text);
	void insert(Offset offset, std::string_view text);
	void erase(Range range);
	void replace(Range range, std::string_view text);

  private:
	std::string mLabel;
	std::vector<EditOperation> mOperations;
};

struct StagedEditOperation {
	EditKind kind;
	Range range;
	TextSpan span;

	StagedEditOperation() noexcept : kind(EditKind::Replace), range(), span() {
	}

	StagedEditOperation(EditKind aKind, Range aRange, TextSpan aSpan) noexcept : kind(aKind), range(aRange), span(aSpan) {
	}
};

class StagedEditTransaction {
  public:
	StagedEditTransaction() noexcept : mBaseVersion(0), mLabel(), mAddBuffer(), mOperations() {
	}

	explicit StagedEditTransaction(std::size_t aBaseVersion, std::string_view label = {}) : mBaseVersion(aBaseVersion), mLabel(label), mAddBuffer(), mOperations() {
	}

	explicit StagedEditTransaction(const Snapshot &snapshot, std::string_view label = {}) : mBaseVersion(snapshot.version), mLabel(label), mAddBuffer(), mOperations() {
	}

	explicit StagedEditTransaction(const ReadSnapshot &snapshot, std::string_view label = {});

	std::size_t baseVersion() const noexcept {
		return mBaseVersion;
	}

	void setBaseVersion(std::size_t value) noexcept {
		mBaseVersion = value;
	}

	[[nodiscard]] const std::string &label() const noexcept {
		return mLabel;
	}

	void setLabel(std::string_view label) {
		mLabel = label;
	}

	[[nodiscard]] bool empty() const noexcept {
		return mOperations.empty();
	}

	[[nodiscard]] const StagedAddBuffer &buffer() const noexcept {
		return mAddBuffer;
	}

	[[nodiscard]] StagedAddBuffer &buffer() noexcept {
		return mAddBuffer;
	}

	[[nodiscard]] const std::vector<StagedEditOperation> &operations() const noexcept {
		return mOperations;
	}

	void setText(std::string_view text);
	void insert(Offset offset, std::string_view text);
	void erase(Range range);
	void replace(Range range, std::string_view text);

  private:
	std::size_t mBaseVersion;
	std::string mLabel;
	StagedAddBuffer mAddBuffer;
	std::vector<StagedEditOperation> mOperations;
};

struct DocumentChangeSet {
	bool changed;
	Range touchedRange;
	Offset oldLength;
	Offset newLength;
	std::size_t oldVersion;
	std::size_t newVersion;

	DocumentChangeSet() noexcept : changed(false), touchedRange(), oldLength(0), newLength(0), oldVersion(0), newVersion(0) {
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

	CommitResult() noexcept : status(CommitStatus::NoOp), expectedVersion(0), actualVersion(0), change() {
	}

	[[nodiscard]] bool applied() const noexcept {
		return status == CommitStatus::Applied;
	}

	[[nodiscard]] bool conflicted() const noexcept {
		return status == CommitStatus::VersionConflict;
	}

	[[nodiscard]] bool changed() const noexcept {
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

	LineIndexCheckpoint(Offset anOffset, std::size_t aLineIndex) noexcept : offset(anOffset), lineIndex(aLineIndex) {
	}
};

struct LineIndexWarmupData {
	std::vector<LineIndexCheckpoint> checkpoints;
	Offset lazyIndexedOffset;
	std::size_t lazyIndexedLine;
	bool lazyLineIndexComplete;
	std::size_t lazyTotalLineCount;

	LineIndexWarmupData() noexcept : checkpoints(), lazyIndexedOffset(0), lazyIndexedLine(0), lazyLineIndexComplete(false), lazyTotalLineCount(1) {
	}
};

class ReadSnapshot {
  public:
	ReadSnapshot() noexcept;

	[[nodiscard]] std::size_t documentId() const noexcept {
		return mDocumentId;
	}

	[[nodiscard]] std::size_t version() const noexcept {
		return mVersion;
	}

	[[nodiscard]] Offset length() const noexcept;
	[[nodiscard]] bool empty() const noexcept;
	[[nodiscard]] char charAt(Offset pos) const noexcept;
	[[nodiscard]] std::string text() const;

	[[nodiscard]] bool hasMappedOriginal() const noexcept {
		return mMappedOriginal.mapped();
	}

	[[nodiscard]] const std::string &mappedPath() const noexcept {
		return mMappedOriginal.path();
	}

	[[nodiscard]] std::size_t addBufferLength() const noexcept;
	[[nodiscard]] std::size_t pieceCount() const noexcept;
	[[nodiscard]] PieceChunkView pieceChunk(std::size_t index) const noexcept;

	[[nodiscard]] Offset clampOffset(Offset pos) const noexcept;
	[[nodiscard]] std::size_t lineCount() const noexcept;
	[[nodiscard]] Offset lineStart(Offset pos) const noexcept;
	[[nodiscard]] Offset lineEnd(Offset pos) const noexcept;
	[[nodiscard]] Offset nextLine(Offset pos) const noexcept;
	[[nodiscard]] Offset prevLine(Offset pos) const noexcept;
	[[nodiscard]] std::size_t lineIndex(Offset pos) const noexcept;
	[[nodiscard]] Offset lineStartByIndex(std::size_t index) const noexcept;
	[[nodiscard]] std::size_t estimatedLineCount() const noexcept;
	[[nodiscard]] bool exactLineCountKnown() const noexcept;
	[[nodiscard]] std::size_t column(Offset pos) const noexcept;
	[[nodiscard]] std::string lineText(Offset pos) const;
	[[nodiscard]] LineIndexWarmupData completeLineIndexWarmup() const;
	bool completeLineIndexWarmup(LineIndexWarmupData &warmup, std::stop_token stopToken, const std::atomic_bool *cancelFlag = nullptr) const;

  private:
	friend class TextDocument;

	bool isLineBreakChar(char ch) const noexcept;
	bool hasDirectOriginalView() const noexcept;
	const char *directTextData() const noexcept;
	void resetLazyLineIndex() noexcept;
	bool advanceLine(Offset &offset) const noexcept;
	bool directAdvanceLine(Offset &offset) const noexcept;
	void ensureLazyIndexSeeded() const noexcept;
	void advanceLazyIndexByStride() const noexcept;
	void ensureLazyIndexForLine(std::size_t targetLine) const noexcept;
	void ensureLazyIndexForOffset(Offset targetOffset) const noexcept;
	void ensureLazyIndexComplete() const noexcept;
	const char *originalData() const noexcept;
	std::string pieceText(const Piece &piece) const;
	void ensureMaterialized() const noexcept;

	std::size_t mDocumentId;
	std::size_t mVersion;
	MappedFileSource mMappedOriginal;
	std::shared_ptr<const std::string> mOriginalBuffer;
	std::shared_ptr<const std::string> mAddBuffer;
	std::shared_ptr<const std::vector<Piece>> mPieces;
	Offset mLength;
	mutable bool mCacheDirty;
	mutable std::string mMaterializedText;
	mutable std::vector<LineIndexCheckpoint> mLineIndexCheckpoints;
	mutable Offset mLazyIndexedOffset;
	mutable std::size_t mLazyIndexedLine;
	mutable bool mLazyLineIndexComplete;
	mutable std::size_t mLazyTotalLineCount;
};

class TextDocument {
  public:
	TextDocument() noexcept;
	explicit TextDocument(std::string_view text);

	[[nodiscard]] const std::string &text() const noexcept;
	[[nodiscard]] Offset length() const noexcept;
	[[nodiscard]] bool empty() const noexcept;
	[[nodiscard]] char charAt(Offset pos) const noexcept;
	[[nodiscard]] Snapshot snapshot() const;
	[[nodiscard]] ReadSnapshot readSnapshot() const;
	void restoreFromSnapshot(const ReadSnapshot &snapshot);
	[[nodiscard]] bool adoptLineIndexWarmup(const LineIndexWarmupData &warmup, std::size_t expectedVersion) noexcept;

	[[nodiscard]] std::size_t version() const noexcept {
		return mVersion;
	}

	[[nodiscard]] std::size_t documentId() const noexcept {
		return mDocumentId;
	}

	[[nodiscard]] bool matchesVersion(std::size_t expectedVersion) const noexcept {
		return expectedVersion == mVersion;
	}

	[[nodiscard]] bool matchesSnapshot(const Snapshot &snapshot) const noexcept {
		return matchesVersion(snapshot.version);
	}

	[[nodiscard]] std::size_t originalLength() const noexcept {
		return mMappedOriginal.mapped() ? mMappedOriginal.size() : mOriginalBuffer.size();
	}

	[[nodiscard]] std::size_t addBufferLength() const noexcept {
		return mAddBuffer.size();
	}

	[[nodiscard]] std::size_t pieceCount() const noexcept {
		return mPieces.size();
	}

	bool loadMappedFile(const std::string &path, std::string &error);
	[[nodiscard]] bool hasMappedOriginal() const noexcept {
		return mMappedOriginal.mapped();
	}

	[[nodiscard]] const std::string &mappedPath() const noexcept {
		return mMappedOriginal.path();
	}

	[[nodiscard]] PieceChunkView pieceChunk(std::size_t index) const noexcept;

	void setText(std::string_view text);
	void apply(const EditTransaction &transaction);
	[[nodiscard]] CommitResult tryApply(const EditTransaction &transaction, std::size_t expectedVersion);
	[[nodiscard]] CommitResult tryApply(const StagedEditTransaction &transaction);
	void insert(Offset offset, std::string_view text);
	void erase(Range range);
	void replace(Range range, std::string_view text);
	void insertFromStaged(Offset offset, const StagedAddBuffer &buffer, TextSpan span);
	void replaceFromStaged(Range range, const StagedAddBuffer &buffer, TextSpan span);
	void flatten();

	[[nodiscard]] Offset clampOffset(Offset pos) const noexcept;
	[[nodiscard]] std::size_t lineCount() const noexcept;
	[[nodiscard]] Offset lineStart(Offset pos) const noexcept;
	[[nodiscard]] Offset lineEnd(Offset pos) const noexcept;
	[[nodiscard]] Offset nextLine(Offset pos) const noexcept;
	[[nodiscard]] Offset prevLine(Offset pos) const noexcept;
	[[nodiscard]] std::size_t lineIndex(Offset pos) const noexcept;
	[[nodiscard]] Offset lineStartByIndex(std::size_t index) const noexcept;
	[[nodiscard]] std::size_t estimatedLineCount() const noexcept;
	[[nodiscard]] bool exactLineCountKnown() const noexcept;
	[[nodiscard]] std::size_t column(Offset pos) const noexcept;
	[[nodiscard]] std::string lineText(Offset pos) const;

  private:
	bool isLineBreakChar(char ch) const noexcept;
	void initializeFromOriginal(std::string_view text, bool bumpVersionFlag);
	void initializeFromMappedSource(const MappedFileSource &source, bool bumpVersionFlag);
	void bumpVersion() noexcept;
	void markDirty() noexcept;
	void ensureMaterialized() const noexcept;
	std::string pieceText(const Piece &piece) const;
	const char *originalData() const noexcept;
	bool setTextNoVersionBump(std::string_view text);
	bool applyOperationNoVersionBump(const EditOperation &operation);
	bool applyStagedOperationNoVersionBump(const StagedEditOperation &operation, const StagedAddBuffer &buffer);
	bool replaceNoVersionBump(Range range, std::string_view text);
	bool hasDirectOriginalView() const noexcept;
	const char *directTextData() const noexcept;
	void resetLazyLineIndex() noexcept;
	bool advanceLine(Offset &offset) const noexcept;
	bool directAdvanceLine(Offset &offset) const noexcept;
	void ensureLazyIndexSeeded() const noexcept;
	void advanceLazyIndexByStride() const noexcept;
	void ensureLazyIndexForLine(std::size_t targetLine) const noexcept;
	void ensureLazyIndexForOffset(Offset targetOffset) const noexcept;
	void ensureLazyIndexComplete() const noexcept;
	void invalidateLazyLineIndexFrom(Offset offset) noexcept;

	std::size_t splitAt(Offset offset);
	bool eraseNoVersionBump(Range range);
	bool insertAddSpanNoVersionBump(Offset offset, TextSpan span);
	void compactPieces();

	std::string mOriginalBuffer;
	MappedFileSource mMappedOriginal;
	AppendBuffer mAddBuffer;
	std::vector<Piece> mPieces;
	Offset mLength;
	std::size_t mDocumentId;
	std::size_t mVersion;
	mutable bool mCacheDirty;
	mutable std::string mMaterializedText;
	mutable std::vector<LineIndexCheckpoint> mLineIndexCheckpoints;
	mutable Offset mLazyIndexedOffset;
	mutable std::size_t mLazyIndexedLine;
	mutable bool mLazyLineIndexComplete;
	mutable std::size_t mLazyTotalLineCount;
};

} // namespace editor
} // namespace mr

#endif
