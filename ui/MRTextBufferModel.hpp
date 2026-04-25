#ifndef MRTEXTBUFFERMODEL_HPP
#define MRTEXTBUFFERMODEL_HPP

#include <cstddef>
#include <string>

#include "MRSyntax.hpp"
#include "MRTextDocument.hpp"

class MRTextBufferModel {
  public:
	using Document = mr::editor::TextDocument;
	using ReadSnapshot = mr::editor::ReadSnapshot;
	using Cursor = mr::editor::Cursor;
	using Range = mr::editor::Range;
	using Selection = mr::editor::Selection;
	using Snapshot = mr::editor::Snapshot;
	using EditTransaction = mr::editor::EditTransaction;
	using StagedAddBuffer = mr::editor::StagedAddBuffer;
	using StagedTransaction = mr::editor::StagedEditTransaction;
	using DocumentChangeSet = mr::editor::DocumentChangeSet;
	using CommitResult = mr::editor::CommitResult;
	using CommitStatus = mr::editor::CommitStatus;

	struct CustomUndoRecord {
		mr::editor::ReadSnapshot preSnapshot;
		std::size_t cursor;
		std::size_t selAnchor;
		std::size_t selCursor;
		bool modifiedState = false;
		int blockMode = 0;
		std::size_t blockAnchor = 0;
		std::size_t blockEnd = 0;
		bool blockMarkingOn = false;
	};

	MRTextBufferModel() noexcept
	    : mDocument(), mCursor(), mSelection(), mModified(false),
	      mLanguage(MRSyntaxLanguage::PlainText), mSyntaxPathHint(), mSyntaxTitleHint(),
	      mUndoStack(), mRedoStack() {
	}

	void setText(const char *data, std::size_t length) {
		if (data == nullptr || length == 0)
			mDocument.setText(std::string());
		else
			mDocument.setText(std::string(data, length));
		clampState();
	}

	void setText(const std::string &text) {
		mDocument.setText(text);
		clampState();
	}

	const std::string &text() const noexcept {
		return mDocument.text();
	}

	std::size_t length() const noexcept {
		return mDocument.length();
	}

	bool isEmpty() const noexcept {
		return mDocument.empty();
	}

	char charAt(std::size_t pos) const noexcept {
		return mDocument.charAt(pos);
	}

	std::size_t lineCount() const noexcept {
		return mDocument.lineCount();
	}

	const Document &document() const noexcept {
		return mDocument;
	}

	Document &document() noexcept {
		return mDocument;
	}

	Snapshot snapshot() const {
		return mDocument.snapshot();
	}

	ReadSnapshot readSnapshot() const {
		return mDocument.readSnapshot();
	}

	std::size_t version() const noexcept {
		return mDocument.version();
	}

	std::size_t documentId() const noexcept {
		return mDocument.documentId();
	}

	bool matchesSnapshot(const Snapshot &snapshot) const noexcept {
		return mDocument.matchesSnapshot(snapshot);
	}

	void applyEditTransaction(const EditTransaction &transaction) {
		mDocument.apply(transaction);
		mModified = true;
		clampState();
	}

	CommitResult tryApplyEditTransaction(const EditTransaction &transaction,
	                                     std::size_t expectedVersion) {
		CommitResult result = mDocument.tryApply(transaction, expectedVersion);
		if (result.applied()) {
			mModified = true;
			clampState();
		}
		return result;
	}

	CommitResult tryApplyStagedTransaction(const StagedTransaction &transaction) {
		CommitResult result = mDocument.tryApply(transaction);
		if (result.applied()) {
			mModified = true;
			clampState();
		}
		return result;
	}

	bool adoptLineIndexWarmup(const mr::editor::LineIndexWarmupData &warmup,
	                          std::size_t expectedVersion) noexcept {
		return mDocument.adoptLineIndexWarmup(warmup, expectedVersion);
	}

	std::size_t cursor() const noexcept {
		return mCursor.offset;
	}

	void setCursor(std::size_t pos) noexcept {
		mCursor.offset = clampOffset(pos);
	}

	void setSelection(std::size_t start, std::size_t end) noexcept {
		mSelection.anchor = clampOffset(start);
		mSelection.cursor = clampOffset(end);
	}

	void setCursorAndSelection(std::size_t cursor, std::size_t start, std::size_t end) noexcept {
		mCursor.offset = clampOffset(cursor);
		setSelection(start, end);
	}

	bool hasSelection() const noexcept {
		return !mSelection.empty();
	}

	std::size_t selectionStart() const noexcept {
		return mSelection.range().start;
	}

	std::size_t selectionEnd() const noexcept {
		return mSelection.range().end;
	}

	const Selection &selection() const noexcept {
		return mSelection;
	}

	bool isModified() const noexcept {
		return mModified;
	}

	void setModified(bool changed) noexcept {
		mModified = changed;
	}

	std::size_t undoStackDepth() const noexcept {
		return mUndoStack.size();
	}

	std::size_t redoStackDepth() const noexcept {
		return mRedoStack.size();
	}

	void clearUndoRedo() noexcept {
		mUndoStack.clear();
		mRedoStack.clear();
	}

	void pushUndoSnapshot(const CustomUndoRecord &record) {
		mUndoStack.push_back(record);
		mRedoStack.clear();
	}

	void popUndoSnapshot() {
		if (!mUndoStack.empty())
			mUndoStack.pop_back();
	}

	bool undo(CustomUndoRecord *outRecord = nullptr) {
		if (mUndoStack.empty())
			return false;

		CustomUndoRecord redoRecord;
		redoRecord.preSnapshot = mDocument.readSnapshot();
		redoRecord.cursor = mCursor.offset;
		redoRecord.selAnchor = mSelection.anchor;
		redoRecord.selCursor = mSelection.cursor;
		redoRecord.modifiedState = mModified;
		mRedoStack.push_back(redoRecord);

		const CustomUndoRecord &undoRecord = mUndoStack.back();
		mDocument.restoreFromSnapshot(undoRecord.preSnapshot);
		static_cast<void>(mDocument.adoptLineIndexWarmup(undoRecord.preSnapshot.completeLineIndexWarmup(), 0));
		mCursor.offset = undoRecord.cursor;
		mSelection.anchor = undoRecord.selAnchor;
		mSelection.cursor = undoRecord.selCursor;
		mModified = undoRecord.modifiedState;
		if (outRecord)
			*outRecord = undoRecord;

		mUndoStack.pop_back();
		clampState();
		return true;
	}

	bool redo(CustomUndoRecord *outRecord = nullptr) {
		if (mRedoStack.empty())
			return false;

		CustomUndoRecord undoRecord;
		undoRecord.preSnapshot = mDocument.readSnapshot();
		undoRecord.cursor = mCursor.offset;
		undoRecord.selAnchor = mSelection.anchor;
		undoRecord.selCursor = mSelection.cursor;
		undoRecord.modifiedState = mModified;
		mUndoStack.push_back(undoRecord);

		const CustomUndoRecord &redoRecord = mRedoStack.back();
		mDocument.restoreFromSnapshot(redoRecord.preSnapshot);
		static_cast<void>(mDocument.adoptLineIndexWarmup(redoRecord.preSnapshot.completeLineIndexWarmup(), 0));
		mCursor.offset = redoRecord.cursor;
		mSelection.anchor = redoRecord.selAnchor;
		mSelection.cursor = redoRecord.selCursor;
		mModified = redoRecord.modifiedState;
		if (outRecord)
			*outRecord = redoRecord;

		mRedoStack.pop_back();
		clampState();
		return true;
	}

	void setSyntaxContext(const std::string &path, const std::string &title = std::string()) {
		mSyntaxPathHint = path;
		mSyntaxTitleHint = title;
		mLanguage = tmrDetectSyntaxLanguage(mSyntaxPathHint, mSyntaxTitleHint);
	}

	MRSyntaxLanguage language() const noexcept {
		return mLanguage;
	}

	const char *languageName() const noexcept {
		return tmrSyntaxLanguageName(mLanguage);
	}

	MRSyntaxTokenMap tokenMapForLine(std::size_t pos) const {
		return tmrBuildTokenMapForTextLine(mLanguage, mDocument.lineText(pos));
	}

	std::size_t lineStart(std::size_t pos) const noexcept {
		return mDocument.lineStart(pos);
	}

	std::size_t lineEnd(std::size_t pos) const noexcept {
		return mDocument.lineEnd(pos);
	}

	std::size_t nextLine(std::size_t pos) const noexcept {
		return mDocument.nextLine(pos);
	}

	std::size_t prevLine(std::size_t pos) const noexcept {
		return mDocument.prevLine(pos);
	}

	std::size_t lineIndex(std::size_t pos) const noexcept {
		return mDocument.lineIndex(pos);
	}

	std::size_t lineStartByIndex(std::size_t index) const noexcept {
		return mDocument.lineStartByIndex(index);
	}

	std::size_t estimatedLineCount() const noexcept {
		return mDocument.estimatedLineCount();
	}

	bool exactLineCountKnown() const noexcept {
		return mDocument.exactLineCountKnown();
	}

	std::size_t column(std::size_t pos) const noexcept {
		return mDocument.column(pos);
	}

	std::string lineText(std::size_t pos) const {
		return mDocument.lineText(pos);
	}

  private:
	std::size_t clampOffset(std::size_t pos) const noexcept {
		return mDocument.clampOffset(pos);
	}

	void clampState() noexcept {
		mCursor.clamp(mDocument.length());
		mSelection.clamp(mDocument.length());
	}

	Document mDocument;
	Cursor mCursor;
	Selection mSelection;
	bool mModified;
	MRSyntaxLanguage mLanguage;
	std::string mSyntaxPathHint;
	std::string mSyntaxTitleHint;
	std::vector<CustomUndoRecord> mUndoStack;
	std::vector<CustomUndoRecord> mRedoStack;
};

#endif
