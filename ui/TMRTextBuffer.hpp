#ifndef TMRTEXTBUFFER_HPP
#define TMRTEXTBUFFER_HPP

#include <cstddef>

#include "TMRFileEditor.hpp"

class TMRTextBuffer {
  public:
	TMRTextBuffer() noexcept : editor_(nullptr) {
	}

	explicit TMRTextBuffer(TMRFileEditor *editor) noexcept : editor_(editor) {
	}

	bool exists() const noexcept {
		return editor_ != nullptr;
	}

	TMRFileEditor *nativeEditor() const noexcept {
		return editor_;
	}

	TMRTextBufferModel::Snapshot snapshot() const {
		return editor_ != nullptr ? editor_->bufferModel().snapshot() : TMRTextBufferModel::Snapshot();
	}

	TMRTextBufferModel::ReadSnapshot readSnapshot() const {
		return editor_ != nullptr ? editor_->bufferModel().readSnapshot()
		                         : TMRTextBufferModel::ReadSnapshot();
	}

	TMRTextBufferModel::Range selectionRange() const noexcept {
		return editor_ != nullptr ? editor_->bufferModel().selection().range() : TMRTextBufferModel::Range();
	}

	std::size_t length() const noexcept {
		return editor_ != nullptr ? editor_->bufferModel().length() : 0;
	}

	bool isEmpty() const noexcept {
		return length() == 0;
	}

	bool hasSelection() const noexcept {
		return editor_ != nullptr && editor_->bufferModel().hasSelection();
	}

	bool hasUndoHistory() const noexcept {
		return editor_ != nullptr && editor_->hasUndoHistory();
	}

	bool hasRedoHistory() const noexcept {
		return editor_ != nullptr && editor_->hasRedoHistory();
	}

	std::size_t undoStackDepth() const noexcept {
		return editor_ != nullptr ? editor_->bufferModel().undoStackDepth() : 0;
	}

	std::size_t redoStackDepth() const noexcept {
		return editor_ != nullptr ? editor_->bufferModel().redoStackDepth() : 0;
	}

	bool isModified() const noexcept {
		return editor_ != nullptr && editor_->bufferModel().isModified();
	}

	TMRSyntaxLanguage language() const noexcept {
		return editor_ != nullptr ? editor_->bufferModel().language() : TMRSyntaxLanguage::PlainText;
	}

	const char *languageName() const noexcept {
		return editor_ != nullptr ? editor_->bufferModel().languageName() : "Plain Text";
	}

	void setModified(bool changed) noexcept {
		if (editor_ == nullptr)
			return;
		editor_->setDocumentModified(changed);
	}

	uint cursor() const noexcept {
		return editor_ != nullptr ? static_cast<uint>(editor_->bufferModel().cursor()) : 0;
	}

	unsigned long cursorLineNumber() const noexcept {
		if (editor_ != nullptr) {
			const TMRTextBufferModel &model = editor_->bufferModel();
			return static_cast<unsigned long>(model.lineIndex(model.cursor())) + 1UL;
		}
		return 1UL;
	}

	unsigned long cursorColumnNumber() const noexcept {
		if (editor_ != nullptr) {
			const TMRTextBufferModel &model = editor_->bufferModel();
			return static_cast<unsigned long>(model.column(model.cursor())) + 1UL;
		}
		return 1UL;
	}

	TPoint cursorPoint() const noexcept {
		TPoint point = {0, 0};
		if (editor_ != nullptr) {
			const TMRTextBufferModel &model = editor_->bufferModel();
			point.x = static_cast<short>(model.column(model.cursor()));
			point.y = static_cast<short>(model.lineIndex(model.cursor()));
		}
		return point;
	}

	std::size_t lineCount() const noexcept {
		return editor_ != nullptr ? editor_->bufferModel().lineCount() : 1;
	}

	char charAt(uint pos) const noexcept {
		return editor_ != nullptr ? editor_->bufferModel().charAt(pos) : '\0';
	}

  private:
	TMRFileEditor *editor_;
};

#endif
