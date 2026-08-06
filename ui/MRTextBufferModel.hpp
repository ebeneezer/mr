#ifndef MRTEXTBUFFERMODEL_HPP
#define MRTEXTBUFFERMODEL_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../app/utils/MRStringUtils.hpp"
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
		DocumentChangeSet changeSet;
		int blockMode = 0;
		std::size_t blockAnchor = 0;
		std::size_t blockEnd = 0;
		int blockAnchorColumn = -1;
		int blockEndColumn = -1;
		bool blockMarkingOn = false;
	};

  private:
	struct SharedState {
		SharedState() noexcept : document(), modified(false), language(MRSyntaxLanguage::PlainText), languageAutomatic(false), languageConfidence(0), syntaxPathHint(), syntaxTitleHint(), undoStack(), redoStack() {
		}

		Document document;
		bool modified;
		MRSyntaxLanguage language;
		bool languageAutomatic;
		std::uint16_t languageConfidence;
		std::string syntaxPathHint;
		std::string syntaxTitleHint;
		std::vector<CustomUndoRecord> undoStack;
		std::vector<CustomUndoRecord> redoStack;
	};

  public:
	MRTextBufferModel() noexcept : mShared(std::make_shared<SharedState>()), mCursor(), mSelection() {
	}

	void shareContentStateFrom(const MRTextBufferModel &source) noexcept {
		mShared = source.mShared;
		clampState();
	}

	void detachContentStateCopy() {
		mShared = std::make_shared<SharedState>(*mShared);
		clampState();
	}

	void setText(const char *data, std::size_t length) {
		if (data == nullptr || length == 0) mShared->document.setText(std::string());
		else
			mShared->document.setText(std::string(data, length));
		clampState();
	}

	void setText(const std::string &text) {
		mShared->document.setText(text);
		clampState();
	}

	const std::string &text() const noexcept {
		return mShared->document.text();
	}

	std::size_t length() const noexcept {
		return mShared->document.length();
	}

	bool isEmpty() const noexcept {
		return mShared->document.empty();
	}

	char charAt(std::size_t pos) const noexcept {
		return mShared->document.charAt(pos);
	}

	std::size_t lineCount() const noexcept {
		return mShared->document.lineCount();
	}

	const Document &document() const noexcept {
		return mShared->document;
	}

	Document &document() noexcept {
		return mShared->document;
	}

	Snapshot snapshot() const {
		return mShared->document.snapshot();
	}

	ReadSnapshot readSnapshot() const {
		return mShared->document.readSnapshot();
	}

	std::size_t version() const noexcept {
		return mShared->document.version();
	}

	std::size_t documentId() const noexcept {
		return mShared->document.documentId();
	}

	bool matchesSnapshot(const Snapshot &snapshot) const noexcept {
		return mShared->document.matchesSnapshot(snapshot);
	}

	void applyEditTransaction(const EditTransaction &transaction) {
		mShared->document.apply(transaction);
		mShared->modified = true;
		clampState();
	}

	CommitResult tryApplyEditTransaction(const EditTransaction &transaction, std::size_t expectedVersion) {
		CommitResult result = mShared->document.tryApply(transaction, expectedVersion);
		if (result.applied()) {
			mShared->modified = true;
			clampState();
		}
		return result;
	}

	CommitResult tryApplyStagedTransaction(const StagedTransaction &transaction) {
		CommitResult result = mShared->document.tryApply(transaction);
		if (result.applied()) {
			mShared->modified = true;
			clampState();
		}
		return result;
	}

	CommitResult adoptReadOnlyProjectionText(const std::shared_ptr<const std::string> &text, std::size_t expectedDocumentId, std::size_t expectedVersion) {
		CommitResult result = mShared->document.adoptReadOnlyProjectionText(text, expectedDocumentId, expectedVersion);
		if (result.applied()) {
			mShared->modified = false;
			clearUndoRedo();
			clampState();
		}
		return result;
	}

	bool adoptLineIndexWarmup(const mr::editor::LineIndexWarmupData &warmup, std::size_t expectedVersion) noexcept {
		return mShared->document.adoptLineIndexWarmup(warmup, expectedVersion);
	}

	std::vector<mr::editor::LineIndexScanReservation> reserveLineIndexScanSpans(std::size_t focusOffset, std::size_t maximumCount, std::size_t targetSpanLength) {
		return mShared->document.reserveLineIndexScanSpans(focusOffset, maximumCount, targetSpanLength);
	}

	void releaseLineIndexScanReservation(std::uint64_t reservationId) noexcept {
		mShared->document.releaseLineIndexScanReservation(reservationId);
	}

	bool adoptLineIndexScanPacket(const mr::editor::LineIndexScanPacket &packet, std::size_t expectedVersion) noexcept {
		return mShared->document.adoptLineIndexScanPacket(packet, expectedVersion);
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
		return mShared->modified;
	}

	void setModified(bool changed) noexcept {
		mShared->modified = changed;
	}

	std::size_t undoStackDepth() const noexcept {
		return mShared->undoStack.size();
	}

	std::size_t redoStackDepth() const noexcept {
		return mShared->redoStack.size();
	}

	void clearUndoRedo() noexcept {
		mShared->undoStack.clear();
		mShared->redoStack.clear();
	}

	void pushUndoSnapshot(CustomUndoRecord &&record) {
		mShared->undoStack.push_back(std::move(record));
		mShared->redoStack.clear();
	}

	void popUndoSnapshot() {
		if (!mShared->undoStack.empty()) mShared->undoStack.pop_back();
	}

	void updateUndoTopBlockState(const CustomUndoRecord &record) {
		if (mShared->undoStack.empty()) return;
		copyBlockState(mShared->undoStack.back(), record);
	}

	void updateUndoTopChangeSet(const DocumentChangeSet &changeSet) {
		if (mShared->undoStack.empty()) return;
		mShared->undoStack.back().changeSet = changeSet;
	}

	void updateRedoTopBlockState(const CustomUndoRecord &record) {
		if (mShared->redoStack.empty()) return;
		copyBlockState(mShared->redoStack.back(), record);
	}

	bool revertUndoSuffix(std::size_t baseDepth, CustomUndoRecord *outRecord = nullptr) {
		if (baseDepth >= mShared->undoStack.size()) return false;

		CustomUndoRecord record = mShared->undoStack[baseDepth];
		mShared->document.restoreFromSnapshot(record.preSnapshot);
		mCursor.offset = record.cursor;
		mSelection.anchor = record.selAnchor;
		mSelection.cursor = record.selCursor;
		mShared->modified = record.modifiedState;
		mShared->undoStack.resize(baseDepth);
		mShared->redoStack.clear();
		clampState();
		if (outRecord != nullptr) *outRecord = std::move(record);
		return true;
	}

	bool undo(CustomUndoRecord *outRecord = nullptr) {
		if (mShared->undoStack.empty()) return false;

		CustomUndoRecord redoRecord;
		redoRecord.preSnapshot = mShared->document.readSnapshot();
		redoRecord.preSnapshot.compactLineIndexForUndo(mCursor.offset);
		redoRecord.cursor = mCursor.offset;
		redoRecord.selAnchor = mSelection.anchor;
		redoRecord.selCursor = mSelection.cursor;
		redoRecord.modifiedState = mShared->modified;
		mShared->redoStack.push_back(std::move(redoRecord));

		const CustomUndoRecord &undoRecord = mShared->undoStack.back();
		mShared->redoStack.back().changeSet = undoRecord.changeSet;
		mShared->document.restoreFromSnapshot(undoRecord.preSnapshot);
		mCursor.offset = undoRecord.cursor;
		mSelection.anchor = undoRecord.selAnchor;
		mSelection.cursor = undoRecord.selCursor;
		mShared->modified = undoRecord.modifiedState;
		if (outRecord) *outRecord = undoRecord;

		mShared->undoStack.pop_back();
		clampState();
		return true;
	}

	bool redo(CustomUndoRecord *outRecord = nullptr) {
		if (mShared->redoStack.empty()) return false;

		CustomUndoRecord undoRecord;
		undoRecord.preSnapshot = mShared->document.readSnapshot();
		undoRecord.preSnapshot.compactLineIndexForUndo(mCursor.offset);
		undoRecord.cursor = mCursor.offset;
		undoRecord.selAnchor = mSelection.anchor;
		undoRecord.selCursor = mSelection.cursor;
		undoRecord.modifiedState = mShared->modified;
		mShared->undoStack.push_back(std::move(undoRecord));

		const CustomUndoRecord &redoRecord = mShared->redoStack.back();
		mShared->undoStack.back().changeSet = redoRecord.changeSet;
		mShared->document.restoreFromSnapshot(redoRecord.preSnapshot);
		mCursor.offset = redoRecord.cursor;
		mSelection.anchor = redoRecord.selAnchor;
		mSelection.cursor = redoRecord.selCursor;
		mShared->modified = redoRecord.modifiedState;
		if (outRecord) *outRecord = redoRecord;

		mShared->redoStack.pop_back();
		clampState();
		return true;
	}

	void setSyntaxContext(const std::string &path, const std::string &title = std::string(), const std::string &codeLanguage = std::string()) {
		const std::string normalizedCodeLanguage = upperAscii(trimAscii(codeLanguage));
		const MRSyntaxLanguage detectedByPath = tmrDetectSyntaxLanguage(path, title);

		mShared->syntaxPathHint = path;
		mShared->syntaxTitleHint = title;
		mShared->languageAutomatic = normalizedCodeLanguage == "AUTO";
		mShared->languageConfidence = 0;
		if (normalizedCodeLanguage.empty() || normalizedCodeLanguage == "NONE") {
			mShared->language = detectedByPath;
			return;
		}
		if (normalizedCodeLanguage == "AUTO") {
			const std::string sample = syntaxClassificationSample();
			const MRSyntaxClassification classification = tmrClassifySyntaxLanguage(mShared->syntaxPathHint, mShared->syntaxTitleHint, sample);
			mShared->languageConfidence = classification.confidence;
			mShared->language = classification.language != MRSyntaxLanguage::PlainText ? classification.language : detectedByPath;
			return;
		}
		if (normalizedCodeLanguage == "C") {
			mShared->language = MRSyntaxLanguage::C;
			return;
		}
		if (normalizedCodeLanguage == "CPP") {
			mShared->language = MRSyntaxLanguage::Cpp;
			return;
		}
		if (normalizedCodeLanguage == "PYTHON") {
			mShared->language = MRSyntaxLanguage::Python;
			return;
		}
		if (normalizedCodeLanguage == "JAVASCRIPT" || normalizedCodeLanguage == "TYPESCRIPT" || normalizedCodeLanguage == "TSX") {
			mShared->language = MRSyntaxLanguage::JavaScript;
			return;
		}
		if (normalizedCodeLanguage == "BASH") {
			mShared->language = MRSyntaxLanguage::Bash;
			return;
		}
		if (normalizedCodeLanguage == "ZSH") {
			mShared->language = MRSyntaxLanguage::Zsh;
			return;
		}
		if (normalizedCodeLanguage == "FISH") {
			mShared->language = MRSyntaxLanguage::Fish;
			return;
		}
		if (normalizedCodeLanguage == "JSON") {
			mShared->language = MRSyntaxLanguage::Json;
			return;
		}
		if (normalizedCodeLanguage == "YAML") {
			mShared->language = MRSyntaxLanguage::Yaml;
			return;
		}
		if (normalizedCodeLanguage == "XML") {
			mShared->language = MRSyntaxLanguage::Xml;
			return;
		}
		if (normalizedCodeLanguage == "PERL") {
			mShared->language = MRSyntaxLanguage::Perl;
			return;
		}
		if (normalizedCodeLanguage == "SWIFT") {
			mShared->language = MRSyntaxLanguage::Swift;
			return;
		}
		if (normalizedCodeLanguage == "RUST") {
			mShared->language = MRSyntaxLanguage::Rust;
			return;
		}
		if (normalizedCodeLanguage == "GO") {
			mShared->language = MRSyntaxLanguage::Go;
			return;
		}
		if (normalizedCodeLanguage == "KOTLIN" || normalizedCodeLanguage == "KT" || normalizedCodeLanguage == "KTS") {
			mShared->language = MRSyntaxLanguage::Kotlin;
			return;
		}
		if (normalizedCodeLanguage == "CSHARP" || normalizedCodeLanguage == "C#" || normalizedCodeLanguage == "CS") {
			mShared->language = MRSyntaxLanguage::CSharp;
			return;
		}
		if (normalizedCodeLanguage == "PASCAL") {
			mShared->language = MRSyntaxLanguage::Pascal;
			return;
		}
		if (normalizedCodeLanguage == "BASIC" || normalizedCodeLanguage == "FREEBASIC" || normalizedCodeLanguage == "QB64" || normalizedCodeLanguage == "QB64PE" || normalizedCodeLanguage == "GAMBAS") {
			mShared->language = MRSyntaxLanguage::Basic;
			return;
		}
		if (normalizedCodeLanguage == "SYSTEMD") {
			mShared->language = MRSyntaxLanguage::Systemd;
			return;
		}
		if (normalizedCodeLanguage == "MAKE") {
			mShared->language = MRSyntaxLanguage::Make;
			return;
		}
		if (normalizedCodeLanguage == "MRMAC") {
			mShared->language = MRSyntaxLanguage::MRMAC;
			return;
		}
		if (normalizedCodeLanguage == "MARKDOWN") {
			mShared->language = MRSyntaxLanguage::Markdown;
			return;
		}
		if (normalizedCodeLanguage == "LATEX" || normalizedCodeLanguage == "TEX") {
			mShared->language = MRSyntaxLanguage::Latex;
			return;
		}
		mShared->language = MRSyntaxLanguage::PlainText;
	}

	MRSyntaxLanguage language() const noexcept {
		return mShared->language;
	}

	bool languageAutomatic() const noexcept {
		return mShared->languageAutomatic;
	}

	std::uint16_t languageConfidence() const noexcept {
		return mShared->languageConfidence;
	}

	const char *languageName() const noexcept {
		return tmrSyntaxLanguageName(mShared->language);
	}

	std::size_t lineStart(std::size_t pos) const noexcept {
		return mShared->document.lineStart(pos);
	}

	std::size_t lineEnd(std::size_t pos) const noexcept {
		return mShared->document.lineEnd(pos);
	}

	std::size_t nextLine(std::size_t pos) const noexcept {
		return mShared->document.nextLine(pos);
	}

	std::size_t prevLine(std::size_t pos) const noexcept {
		return mShared->document.prevLine(pos);
	}

	std::size_t lineIndex(std::size_t pos) const noexcept {
		return mShared->document.lineIndex(pos);
	}

	std::size_t estimatedLineIndex(std::size_t pos) const noexcept {
		return mShared->document.estimatedLineIndex(pos);
	}

	std::size_t lineStartByIndex(std::size_t index) const noexcept {
		return mShared->document.lineStartByIndex(index);
	}

	bool lineStartByIndexKnown(std::size_t index) const noexcept {
		return mShared->document.lineStartByIndexKnown(index);
	}

	std::size_t estimatedLineCount() const noexcept {
		return mShared->document.estimatedLineCount();
	}

	bool exactLineCountKnown() const noexcept {
		return mShared->document.exactLineCountKnown();
	}

	std::size_t column(std::size_t pos) const noexcept {
		return mShared->document.column(pos);
	}

	std::string lineText(std::size_t pos) const {
		return mShared->document.lineText(pos);
	}

  private:
	std::string syntaxClassificationSample() const {
		static constexpr std::size_t maximumLength = 64 * 1024;
		const std::size_t documentLength = mShared->document.length();
		const std::size_t sampleLength = documentLength < maximumLength ? documentLength : maximumLength;
		std::string sample;

		sample.reserve(sampleLength);
		for (std::size_t index = 0; index < mShared->document.pieceCount() && sample.size() < sampleLength; ++index) {
			const mr::editor::PieceChunkView chunk = mShared->document.pieceChunk(index);
			if (chunk.data == nullptr || chunk.length == 0) continue;
			const std::size_t remainingLength = sampleLength - sample.size();
			const std::size_t copyLength = chunk.length < remainingLength ? chunk.length : remainingLength;
			sample.append(chunk.data, copyLength);
		}
		return sample;
	}

	std::size_t clampOffset(std::size_t pos) const noexcept {
		return mShared->document.clampOffset(pos);
	}

	static void copyBlockState(CustomUndoRecord &target, const CustomUndoRecord &source) noexcept {
		target.blockMode = source.blockMode;
		target.blockAnchor = source.blockAnchor;
		target.blockEnd = source.blockEnd;
		target.blockAnchorColumn = source.blockAnchorColumn;
		target.blockEndColumn = source.blockEndColumn;
		target.blockMarkingOn = source.blockMarkingOn;
	}

	void clampState() noexcept {
		mCursor.clamp(mShared->document.length());
		mSelection.clamp(mShared->document.length());
	}

	std::shared_ptr<SharedState> mShared;
	Cursor mCursor;
	Selection mSelection;
};

#endif
