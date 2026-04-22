#ifndef TMRTEXTBUFFERMODEL_HPP
#define TMRTEXTBUFFERMODEL_HPP

#include <cstddef>
#include <string>

#include "TMRSyntax.hpp"
#include "MRTextDocument.hpp"

class TMRTextBufferModel {
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

	TMRTextBufferModel() noexcept
	    : document_(), cursor_(), selection_(), modified_(false),
	      language_(TMRSyntaxLanguage::PlainText), syntaxPathHint_(), syntaxTitleHint_(),
	      undoStack_(), redoStack_() {
	}

	void setText(const char *data, std::size_t length) {
		if (data == nullptr || length == 0)
			document_.setText(std::string());
		else
			document_.setText(std::string(data, length));
		clampState();
	}

	void setText(const std::string &text) {
		document_.setText(text);
		clampState();
	}

	const std::string &text() const noexcept {
		return document_.text();
	}

	std::size_t length() const noexcept {
		return document_.length();
	}

	bool isEmpty() const noexcept {
		return document_.empty();
	}

	char charAt(std::size_t pos) const noexcept {
		return document_.charAt(pos);
	}

	std::size_t lineCount() const noexcept {
		return document_.lineCount();
	}

	const Document &document() const noexcept {
		return document_;
	}

	Document &document() noexcept {
		return document_;
	}

	Snapshot snapshot() const {
		return document_.snapshot();
	}

	ReadSnapshot readSnapshot() const {
		return document_.readSnapshot();
	}

	std::size_t version() const noexcept {
		return document_.version();
	}

	std::size_t documentId() const noexcept {
		return document_.documentId();
	}

	bool matchesSnapshot(const Snapshot &snapshot) const noexcept {
		return document_.matchesSnapshot(snapshot);
	}

	void applyEditTransaction(const EditTransaction &transaction) {
		document_.apply(transaction);
		modified_ = true;
		clampState();
	}

	CommitResult tryApplyEditTransaction(const EditTransaction &transaction,
	                                     std::size_t expectedVersion) {
		CommitResult result = document_.tryApply(transaction, expectedVersion);
		if (result.applied()) {
			modified_ = true;
			clampState();
		}
		return result;
	}

	CommitResult tryApplyStagedTransaction(const StagedTransaction &transaction) {
		CommitResult result = document_.tryApply(transaction);
		if (result.applied()) {
			modified_ = true;
			clampState();
		}
		return result;
	}

	bool adoptLineIndexWarmup(const mr::editor::LineIndexWarmupData &warmup,
	                          std::size_t expectedVersion) noexcept {
		return document_.adoptLineIndexWarmup(warmup, expectedVersion);
	}

	std::size_t cursor() const noexcept {
		return cursor_.offset;
	}

	void setCursor(std::size_t pos) noexcept {
		cursor_.offset = clampOffset(pos);
	}

	void setSelection(std::size_t start, std::size_t end) noexcept {
		selection_.anchor = clampOffset(start);
		selection_.cursor = clampOffset(end);
	}

	void setCursorAndSelection(std::size_t cursor, std::size_t start, std::size_t end) noexcept {
		cursor_.offset = clampOffset(cursor);
		setSelection(start, end);
	}

	bool hasSelection() const noexcept {
		return !selection_.empty();
	}

	std::size_t selectionStart() const noexcept {
		return selection_.range().start;
	}

	std::size_t selectionEnd() const noexcept {
		return selection_.range().end;
	}

	const Selection &selection() const noexcept {
		return selection_;
	}

	bool isModified() const noexcept {
		return modified_;
	}

	void setModified(bool changed) noexcept {
		modified_ = changed;
	}

	std::size_t undoStackDepth() const noexcept {
		return undoStack_.size();
	}

	std::size_t redoStackDepth() const noexcept {
		return redoStack_.size();
	}

	void clearUndoRedo() noexcept {
		undoStack_.clear();
		redoStack_.clear();
	}

	void pushUndoSnapshot(const CustomUndoRecord &record) {
		undoStack_.push_back(record);
		redoStack_.clear();
	}

	void popUndoSnapshot() {
		if (!undoStack_.empty())
			undoStack_.pop_back();
	}

	bool undo(CustomUndoRecord *outRecord = nullptr) {
		if (undoStack_.empty())
			return false;

		CustomUndoRecord redoRecord;
		redoRecord.preSnapshot = document_.readSnapshot();
		redoRecord.cursor = cursor_.offset;
		redoRecord.selAnchor = selection_.anchor;
		redoRecord.selCursor = selection_.cursor;
		redoRecord.modifiedState = modified_;
		redoStack_.push_back(redoRecord);

		const CustomUndoRecord &undoRecord = undoStack_.back();
		document_.restoreFromSnapshot(undoRecord.preSnapshot);
		static_cast<void>(document_.adoptLineIndexWarmup(undoRecord.preSnapshot.completeLineIndexWarmup(), 0));
		cursor_.offset = undoRecord.cursor;
		selection_.anchor = undoRecord.selAnchor;
		selection_.cursor = undoRecord.selCursor;
		modified_ = undoRecord.modifiedState;
		if (outRecord)
			*outRecord = undoRecord;

		undoStack_.pop_back();
		clampState();
		return true;
	}

	bool redo(CustomUndoRecord *outRecord = nullptr) {
		if (redoStack_.empty())
			return false;

		CustomUndoRecord undoRecord;
		undoRecord.preSnapshot = document_.readSnapshot();
		undoRecord.cursor = cursor_.offset;
		undoRecord.selAnchor = selection_.anchor;
		undoRecord.selCursor = selection_.cursor;
		undoRecord.modifiedState = modified_;
		undoStack_.push_back(undoRecord);

		const CustomUndoRecord &redoRecord = redoStack_.back();
		document_.restoreFromSnapshot(redoRecord.preSnapshot);
		static_cast<void>(document_.adoptLineIndexWarmup(redoRecord.preSnapshot.completeLineIndexWarmup(), 0));
		cursor_.offset = redoRecord.cursor;
		selection_.anchor = redoRecord.selAnchor;
		selection_.cursor = redoRecord.selCursor;
		modified_ = redoRecord.modifiedState;
		if (outRecord)
			*outRecord = redoRecord;

		redoStack_.pop_back();
		clampState();
		return true;
	}

	void setSyntaxContext(const std::string &path, const std::string &title = std::string()) {
		syntaxPathHint_ = path;
		syntaxTitleHint_ = title;
		language_ = tmrDetectSyntaxLanguage(syntaxPathHint_, syntaxTitleHint_);
	}

	TMRSyntaxLanguage language() const noexcept {
		return language_;
	}

	const char *languageName() const noexcept {
		return tmrSyntaxLanguageName(language_);
	}

	TMRSyntaxTokenMap tokenMapForLine(std::size_t pos) const {
		return tmrBuildTokenMapForTextLine(language_, document_.lineText(pos));
	}

	std::size_t lineStart(std::size_t pos) const noexcept {
		return document_.lineStart(pos);
	}

	std::size_t lineEnd(std::size_t pos) const noexcept {
		return document_.lineEnd(pos);
	}

	std::size_t nextLine(std::size_t pos) const noexcept {
		return document_.nextLine(pos);
	}

	std::size_t prevLine(std::size_t pos) const noexcept {
		return document_.prevLine(pos);
	}

	std::size_t lineIndex(std::size_t pos) const noexcept {
		return document_.lineIndex(pos);
	}

	std::size_t lineStartByIndex(std::size_t index) const noexcept {
		return document_.lineStartByIndex(index);
	}

	std::size_t estimatedLineCount() const noexcept {
		return document_.estimatedLineCount();
	}

	bool exactLineCountKnown() const noexcept {
		return document_.exactLineCountKnown();
	}

	std::size_t column(std::size_t pos) const noexcept {
		return document_.column(pos);
	}

	std::string lineText(std::size_t pos) const {
		return document_.lineText(pos);
	}

  private:
	std::size_t clampOffset(std::size_t pos) const noexcept {
		return document_.clampOffset(pos);
	}

	void clampState() noexcept {
		cursor_.clamp(document_.length());
		selection_.clamp(document_.length());
	}

	Document document_;
	Cursor cursor_;
	Selection selection_;
	bool modified_;
	TMRSyntaxLanguage language_;
	std::string syntaxPathHint_;
	std::string syntaxTitleHint_;
	std::vector<CustomUndoRecord> undoStack_;
	std::vector<CustomUndoRecord> redoStack_;
};

#endif
