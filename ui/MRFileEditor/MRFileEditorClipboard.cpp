#include "MRFileEditor.hpp"
#include "../MREditWindow.hpp"

void MRFileEditor::copySelection() {
	MREditWindow *window = dynamic_cast<MREditWindow *>(owner);
	if (window != nullptr && window->isBlockMarking()) window->endBlock(false);
	std::string text;
	if (window != nullptr && window->hasBlock() && !mBufferModel.hasSelection()) {
		std::string error;
		if (!window->captureBlockPayload(text, &error)) {
			mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, error.empty() ? "Unable to copy block." : error, mr::messageline::Kind::Warning, mr::messageline::kPriorityMedium);
			return;
		}
	} else {
		if (!mBufferModel.hasSelection()) return;
		const MRTextBufferModel::Range range = mBufferModel.selection().range();
		text = mBufferModel.text().substr(range.start, range.length());
	}
	TClipboard::setText(TStringView(text.data(), text.size()));
}

void MRFileEditor::cutSelection() {
	MREditWindow *window = dynamic_cast<MREditWindow *>(owner);
	if (window != nullptr && window->isBlockMarking()) window->endBlock(false);
	if (mReadOnly) return;
	if (window != nullptr && window->hasBlock() && !mBufferModel.hasSelection()) {
		std::string text;
		std::string error;
		if (window->captureBlockPayload(text, &error)) {
			TClipboard::setText(TStringView(text.data(), text.size()));
			if (window->deleteBlock(&error)) return;
		}
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, error.empty() ? "Unable to cut block." : error, mr::messageline::Kind::Warning, mr::messageline::kPriorityMedium);
		return;
	}
	if (!mBufferModel.hasSelection()) return;
	copySelection();
	replaceSelectionText(std::string());
}

void MRFileEditor::requestSystemClipboardPaste(std::size_t offset) {
	if (mReadOnly) return;
	mClipboardPasteOffset = offset == std::string::npos ? offset : std::min(offset, bufferLength());
	mClipboardPasteVersion = mBufferModel.version();
	TClipboard::requestText();
}

bool MRFileEditor::hasPositionedClipboardPaste() const noexcept {
	return mClipboardPasteOffset != std::string::npos;
}

void MRFileEditor::replaceSelectionText(const std::string &text) {
	if (!mBufferModel.hasSelection()) {
		if (!text.empty()) insertBufferText(text);
		return;
	}
	MRTextBufferModel::Range range = mBufferModel.selection().range();
	replaceRangeAndSelect(static_cast<uint>(range.start), static_cast<uint>(range.end), text.data(), static_cast<uint>(text.size()));
}

void MRFileEditor::convertSelectionToUpperCase() {
	if (mReadOnly || !mBufferModel.hasSelection()) return;
	MRTextBufferModel::Range range = mBufferModel.selection().range();
	std::string text = mBufferModel.text().substr(range.start, range.length());
	for (char &c : text)
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	replaceSelectionText(text);
	setSelectionOffsets(range.start, range.start + text.length());
}

void MRFileEditor::convertSelectionToLowerCase() {
	if (mReadOnly || !mBufferModel.hasSelection()) return;
	MRTextBufferModel::Range range = mBufferModel.selection().range();
	std::string text = mBufferModel.text().substr(range.start, range.length());
	for (char &c : text)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	replaceSelectionText(text);
	setSelectionOffsets(range.start, range.start + text.length());
}
