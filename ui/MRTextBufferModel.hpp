#ifndef MRTEXTBUFFERMODEL_HPP
#define MRTEXTBUFFERMODEL_HPP

#include <array>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>

#include "../app/utils/MRStringUtils.hpp"
#include "MRSyntax.hpp"
#include "MRTextDocument.hpp"

namespace {

static constexpr auto kSlowUndoTraceThreshold = std::chrono::microseconds(2000);

inline std::string undoTraceTimestamp() {
	std::array<char, 32> buffer{};
	const std::time_t now = std::time(nullptr);
	const std::tm *tmNow = std::localtime(&now);

	if (tmNow == nullptr) return std::string("--:--:--");
	if (std::strftime(buffer.data(), buffer.size(), "%H:%M:%S", tmNow) == 0) return std::string("--:--:--");
	return std::string(buffer.data());
}

inline void appendUndoTrace(std::string_view message) {
	std::ofstream out("misc/mr.log", std::ios::out | std::ios::app | std::ios::binary);

	if (!out) return;
	out << "[" << undoTraceTimestamp() << "] " << message << '\n';
	out.flush();
}

template <class Duration> inline long long undoTraceMicros(Duration duration) {
	return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

} // namespace

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

	MRTextBufferModel() noexcept : mDocument(), mCursor(), mSelection(), mModified(false), mLanguage(MRSyntaxLanguage::PlainText), mLanguageAutomatic(false), mLanguageConfidence(0), mSyntaxPathHint(), mSyntaxTitleHint(), mUndoStack(), mRedoStack() {
	}

	void setText(const char *data, std::size_t length) {
		if (data == nullptr || length == 0) mDocument.setText(std::string());
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

	CommitResult tryApplyEditTransaction(const EditTransaction &transaction, std::size_t expectedVersion) {
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

	bool adoptLineIndexWarmup(const mr::editor::LineIndexWarmupData &warmup, std::size_t expectedVersion) noexcept {
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
		const auto startedAt = std::chrono::steady_clock::now();
		const std::size_t undoBefore = mUndoStack.size();
		const std::size_t redoBefore = mRedoStack.size();
		mUndoStack.clear();
		mRedoStack.clear();
		const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
		if (totalElapsed >= kSlowUndoTraceThreshold) {
			std::ostringstream line;
			line << "Phase1 undo clearUndoRedo total_us=" << undoTraceMicros(totalElapsed) << " undo_before=" << undoBefore << " redo_before=" << redoBefore << " len=" << mDocument.length()
			     << " add=" << mDocument.addBufferLength() << " pieces=" << mDocument.pieceCount();
			appendUndoTrace(line.str());
		}
	}

	void pushUndoSnapshot(CustomUndoRecord &&record) {
		const auto startedAt = std::chrono::steady_clock::now();
		mUndoStack.push_back(std::move(record));
		mRedoStack.clear();
		const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
		if (totalElapsed >= kSlowUndoTraceThreshold) {
			std::ostringstream line;
			line << "Phase1 undo pushUndoSnapshot total_us=" << undoTraceMicros(totalElapsed) << " undo=" << mUndoStack.size() << " redo=" << mRedoStack.size() << " len=" << mDocument.length()
			     << " add=" << mDocument.addBufferLength() << " pieces=" << mDocument.pieceCount();
			appendUndoTrace(line.str());
		}
	}

	void popUndoSnapshot() {
		if (!mUndoStack.empty()) mUndoStack.pop_back();
	}

	bool undo(CustomUndoRecord *outRecord = nullptr) {
		const auto startedAt = std::chrono::steady_clock::now();
		if (mUndoStack.empty()) return false;

		CustomUndoRecord redoRecord;
		const auto redoSnapshotStartedAt = std::chrono::steady_clock::now();
		redoRecord.preSnapshot = mDocument.readSnapshot();
		const auto redoSnapshotElapsed = std::chrono::steady_clock::now() - redoSnapshotStartedAt;
		redoRecord.cursor = mCursor.offset;
		redoRecord.selAnchor = mSelection.anchor;
		redoRecord.selCursor = mSelection.cursor;
		redoRecord.modifiedState = mModified;
		mRedoStack.push_back(std::move(redoRecord));

		const CustomUndoRecord &undoRecord = mUndoStack.back();
		const auto restoreStartedAt = std::chrono::steady_clock::now();
		mDocument.restoreFromSnapshot(undoRecord.preSnapshot);
		const auto restoreElapsed = std::chrono::steady_clock::now() - restoreStartedAt;
		mCursor.offset = undoRecord.cursor;
		mSelection.anchor = undoRecord.selAnchor;
		mSelection.cursor = undoRecord.selCursor;
		mModified = undoRecord.modifiedState;
		if (outRecord) *outRecord = undoRecord;

		mUndoStack.pop_back();
		clampState();
		const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
		if (totalElapsed >= kSlowUndoTraceThreshold) {
			std::ostringstream line;
			line << "Phase1 undo undo total_us=" << undoTraceMicros(totalElapsed) << " snapshot_us=" << undoTraceMicros(redoSnapshotElapsed) << " restore_us=" << undoTraceMicros(restoreElapsed)
			     << " undo=" << mUndoStack.size() << " redo=" << mRedoStack.size() << " len=" << mDocument.length() << " add=" << mDocument.addBufferLength() << " pieces=" << mDocument.pieceCount();
			appendUndoTrace(line.str());
		}
		return true;
	}

	bool redo(CustomUndoRecord *outRecord = nullptr) {
		const auto startedAt = std::chrono::steady_clock::now();
		if (mRedoStack.empty()) return false;

		CustomUndoRecord undoRecord;
		const auto undoSnapshotStartedAt = std::chrono::steady_clock::now();
		undoRecord.preSnapshot = mDocument.readSnapshot();
		const auto undoSnapshotElapsed = std::chrono::steady_clock::now() - undoSnapshotStartedAt;
		undoRecord.cursor = mCursor.offset;
		undoRecord.selAnchor = mSelection.anchor;
		undoRecord.selCursor = mSelection.cursor;
		undoRecord.modifiedState = mModified;
		mUndoStack.push_back(std::move(undoRecord));

		const CustomUndoRecord &redoRecord = mRedoStack.back();
		const auto restoreStartedAt = std::chrono::steady_clock::now();
		mDocument.restoreFromSnapshot(redoRecord.preSnapshot);
		const auto restoreElapsed = std::chrono::steady_clock::now() - restoreStartedAt;
		mCursor.offset = redoRecord.cursor;
		mSelection.anchor = redoRecord.selAnchor;
		mSelection.cursor = redoRecord.selCursor;
		mModified = redoRecord.modifiedState;
		if (outRecord) *outRecord = redoRecord;

		mRedoStack.pop_back();
		clampState();
		const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
		if (totalElapsed >= kSlowUndoTraceThreshold) {
			std::ostringstream line;
			line << "Phase1 undo redo total_us=" << undoTraceMicros(totalElapsed) << " snapshot_us=" << undoTraceMicros(undoSnapshotElapsed) << " restore_us=" << undoTraceMicros(restoreElapsed)
			     << " undo=" << mUndoStack.size() << " redo=" << mRedoStack.size() << " len=" << mDocument.length() << " add=" << mDocument.addBufferLength() << " pieces=" << mDocument.pieceCount();
			appendUndoTrace(line.str());
		}
		return true;
	}

	void setSyntaxContext(const std::string &path, const std::string &title = std::string(), const std::string &codeLanguage = std::string()) {
		const std::string normalizedCodeLanguage = upperAscii(trimAscii(codeLanguage));
		const MRSyntaxLanguage detectedByPath = tmrDetectSyntaxLanguage(path, title);

		mSyntaxPathHint = path;
		mSyntaxTitleHint = title;
		mLanguageAutomatic = normalizedCodeLanguage == "AUTO";
		mLanguageConfidence = 0;
		if (normalizedCodeLanguage.empty() || normalizedCodeLanguage == "NONE") {
			mLanguage = detectedByPath;
			return;
		}
		if (normalizedCodeLanguage == "AUTO") {
			const MRSyntaxClassification classification = tmrClassifySyntaxLanguage(mSyntaxPathHint, mSyntaxTitleHint, text());
			mLanguageConfidence = classification.confidence;
			mLanguage = classification.language != MRSyntaxLanguage::PlainText ? classification.language : detectedByPath;
			return;
		}
		if (normalizedCodeLanguage == "C") {
			mLanguage = MRSyntaxLanguage::C;
			return;
		}
		if (normalizedCodeLanguage == "CPP") {
			mLanguage = MRSyntaxLanguage::Cpp;
			return;
		}
		if (normalizedCodeLanguage == "PYTHON") {
			mLanguage = MRSyntaxLanguage::Python;
			return;
		}
		if (normalizedCodeLanguage == "JAVASCRIPT" || normalizedCodeLanguage == "TYPESCRIPT" || normalizedCodeLanguage == "TSX") {
			mLanguage = MRSyntaxLanguage::JavaScript;
			return;
		}
		if (normalizedCodeLanguage == "BASH") {
			mLanguage = MRSyntaxLanguage::Bash;
			return;
		}
		if (normalizedCodeLanguage == "ZSH") {
			mLanguage = MRSyntaxLanguage::Zsh;
			return;
		}
		if (normalizedCodeLanguage == "FISH") {
			mLanguage = MRSyntaxLanguage::Fish;
			return;
		}
		if (normalizedCodeLanguage == "JSON") {
			mLanguage = MRSyntaxLanguage::Json;
			return;
		}
		if (normalizedCodeLanguage == "YAML") {
			mLanguage = MRSyntaxLanguage::Yaml;
			return;
		}
		if (normalizedCodeLanguage == "XML") {
			mLanguage = MRSyntaxLanguage::Xml;
			return;
		}
		if (normalizedCodeLanguage == "PERL") {
			mLanguage = MRSyntaxLanguage::Perl;
			return;
		}
		if (normalizedCodeLanguage == "SWIFT") {
			mLanguage = MRSyntaxLanguage::Swift;
			return;
		}
		if (normalizedCodeLanguage == "RUST") {
			mLanguage = MRSyntaxLanguage::Rust;
			return;
		}
		if (normalizedCodeLanguage == "GO") {
			mLanguage = MRSyntaxLanguage::Go;
			return;
		}
		if (normalizedCodeLanguage == "PASCAL") {
			mLanguage = MRSyntaxLanguage::Pascal;
			return;
		}
		if (normalizedCodeLanguage == "SYSTEMD") {
			mLanguage = MRSyntaxLanguage::Systemd;
			return;
		}
		mLanguage = MRSyntaxLanguage::PlainText;
	}

	MRSyntaxLanguage language() const noexcept {
		return mLanguage;
	}

	bool languageAutomatic() const noexcept {
		return mLanguageAutomatic;
	}

	std::uint16_t languageConfidence() const noexcept {
		return mLanguageConfidence;
	}

	const char *languageName() const noexcept {
		return tmrSyntaxLanguageName(mLanguage);
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
	bool mLanguageAutomatic;
	std::uint16_t mLanguageConfidence;
	std::string mSyntaxPathHint;
	std::string mSyntaxTitleHint;
	std::vector<CustomUndoRecord> mUndoStack;
	std::vector<CustomUndoRecord> mRedoStack;
};

#endif
