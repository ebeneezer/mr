#ifndef MRTEXTBUFFER_HPP
#define MRTEXTBUFFER_HPP

#include <cstddef>

#include "MRFileEditor.hpp"

class MRTextBuffer {
  public:
	MRTextBuffer() noexcept : mEditor(nullptr) {
	}

	explicit MRTextBuffer(MRFileEditor *editor) noexcept : mEditor(editor) {
	}

	bool exists() const noexcept {
		return mEditor != nullptr;
	}

	MRFileEditor *nativeEditor() const noexcept {
		return mEditor;
	}

	MRTextBufferModel::Snapshot snapshot() const {
		return mEditor != nullptr ? mEditor->bufferModel().snapshot() : MRTextBufferModel::Snapshot();
	}

	MRTextBufferModel::ReadSnapshot readSnapshot() const {
		return mEditor != nullptr ? mEditor->bufferModel().readSnapshot() : MRTextBufferModel::ReadSnapshot();
	}

	MRTextBufferModel::Range selectionRange() const noexcept {
		return mEditor != nullptr ? mEditor->bufferModel().selection().range() : MRTextBufferModel::Range();
	}

	std::size_t length() const noexcept {
		return mEditor != nullptr ? mEditor->bufferModel().length() : 0;
	}

	bool isEmpty() const noexcept {
		return length() == 0;
	}

	bool hasSelection() const noexcept {
		return mEditor != nullptr && mEditor->bufferModel().hasSelection();
	}

	bool hasUndoHistory() const noexcept {
		return mEditor != nullptr && mEditor->hasUndoHistory();
	}

	bool hasRedoHistory() const noexcept {
		return mEditor != nullptr && mEditor->hasRedoHistory();
	}

	std::size_t undoStackDepth() const noexcept {
		return mEditor != nullptr ? mEditor->bufferModel().undoStackDepth() : 0;
	}

	std::size_t redoStackDepth() const noexcept {
		return mEditor != nullptr ? mEditor->bufferModel().redoStackDepth() : 0;
	}

	bool isModified() const noexcept {
		return mEditor != nullptr && mEditor->bufferModel().isModified();
	}

	MRSyntaxLanguage language() const noexcept {
		return mEditor != nullptr ? mEditor->bufferModel().language() : MRSyntaxLanguage::PlainText;
	}

	const char *languageName() const noexcept {
		return mEditor != nullptr ? mEditor->bufferModel().languageName() : "Plain Text";
	}

	void setModified(bool changed) noexcept {
		if (mEditor == nullptr) return;
		mEditor->setDocumentModified(changed);
	}

	uint cursor() const noexcept {
		return mEditor != nullptr ? static_cast<uint>(mEditor->bufferModel().cursor()) : 0;
	}

	unsigned long cursorLineNumber() const noexcept {
		if (mEditor != nullptr) {
			const MRTextBufferModel &model = mEditor->bufferModel();
			return static_cast<unsigned long>(model.lineIndex(model.cursor())) + 1UL;
		}
		return 1UL;
	}

	unsigned long cursorColumnNumber() const noexcept {
		if (mEditor != nullptr) {
			const MRTextBufferModel &model = mEditor->bufferModel();
			return static_cast<unsigned long>(model.column(model.cursor())) + 1UL;
		}
		return 1UL;
	}

	TPoint cursorPoint() const noexcept {
		TPoint point = {0, 0};
		if (mEditor != nullptr) {
			const MRTextBufferModel &model = mEditor->bufferModel();
			point.x = static_cast<short>(model.column(model.cursor()));
			point.y = static_cast<short>(model.lineIndex(model.cursor()));
		}
		return point;
	}

	std::size_t lineCount() const noexcept {
		return mEditor != nullptr ? mEditor->bufferModel().lineCount() : 1;
	}

	char charAt(uint pos) const noexcept {
		return mEditor != nullptr ? mEditor->bufferModel().charAt(pos) : '\0';
	}

  private:
	MRFileEditor *mEditor;
};

#endif
