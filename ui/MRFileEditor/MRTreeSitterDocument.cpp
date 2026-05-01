#include "MRTreeSitterDocument.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <new>
#include <string>
#include <utility>

#include <tree_sitter/api.h>

#include "../../app/utils/MRStringUtils.hpp"
#include "../../piecetable/MRTextDocument.hpp"

extern "C" const TSLanguage *tree_sitter_c(void);
extern "C" const TSLanguage *tree_sitter_cpp(void);

namespace {

bool isIndentWhitespace(char ch) noexcept {
	return ch == ' ' || ch == '\t';
}

bool isTreeSitterCommentOrLiteral(const char *typeName) noexcept {
	if (typeName == nullptr || *typeName == '\0') return false;
	const std::string_view type(typeName);
	return type.find("comment") != std::string_view::npos || type.find("string") != std::string_view::npos || type.find("char_literal") != std::string_view::npos;
}

std::size_t previousNonWhitespaceOnLine(const mr::editor::ReadSnapshot &snapshot, std::size_t cursorOffset) noexcept {
	const std::size_t safeOffset = std::min(cursorOffset, snapshot.length());
	const std::size_t lineStart = snapshot.lineStart(safeOffset);
	std::size_t probe = safeOffset;

	while (probe > lineStart) {
		const std::size_t previous = probe - 1;
		if (!isIndentWhitespace(snapshot.charAt(previous))) return previous;
		probe = previous;
	}
	return snapshot.length();
}

struct TreeSitterSnapshotInput {
	mr::editor::ReadSnapshot snapshot;
	std::size_t pieceIndex = 0;
	std::size_t pieceStart = 0;
	std::size_t pieceEnd = 0;
	mr::editor::PieceChunkView piece;
	bool pieceValid = false;
};

const char *readTreeSitterSnapshot(void *payload, uint32_t byteIndex, TSPoint, uint32_t *bytesRead) {
	TreeSitterSnapshotInput &input = *static_cast<TreeSitterSnapshotInput *>(payload);
	std::size_t pieceCount = input.snapshot.pieceCount();
	std::size_t target = static_cast<std::size_t>(byteIndex);
	std::size_t scanIndex = 0;
	std::size_t scanOffset = 0;

	*bytesRead = 0;
	if (target >= input.snapshot.length()) return nullptr;
	if (input.pieceValid && target >= input.pieceStart && target < input.pieceEnd) {
		const std::size_t chunkOffset = target - input.pieceStart;
		const std::size_t available = input.piece.length - chunkOffset;
		*bytesRead = static_cast<uint32_t>(std::min<std::size_t>(available, UINT32_MAX));
		return input.piece.data + chunkOffset;
	}
	if (input.pieceValid && target >= input.pieceEnd) {
		scanIndex = input.pieceIndex + 1;
		scanOffset = input.pieceEnd;
	}
	input.pieceValid = false;
	for (std::size_t i = scanIndex; i < pieceCount; ++i) {
		const mr::editor::PieceChunkView nextPiece = input.snapshot.pieceChunk(i);
		if (nextPiece.length == 0) continue;
		if (target < scanOffset + nextPiece.length) {
			input.pieceIndex = i;
			input.pieceStart = scanOffset;
			input.pieceEnd = scanOffset + nextPiece.length;
			input.piece = nextPiece;
			input.pieceValid = true;
			break;
		}
		scanOffset += nextPiece.length;
	}
	if (!input.pieceValid) return nullptr;
	const std::size_t chunkOffset = target - input.pieceStart;
	const std::size_t available = input.piece.length - chunkOffset;
	*bytesRead = static_cast<uint32_t>(std::min<std::size_t>(available, UINT32_MAX));
	return input.piece.data + chunkOffset;
}

TSTree *parseTreeSitterSnapshot(TSParser *parser, const TSTree *oldTree, const mr::editor::ReadSnapshot &snapshot) {
	TreeSitterSnapshotInput input;
	TSInput source;

	input.snapshot = snapshot;
	source.payload = &input;
	source.read = readTreeSitterSnapshot;
	source.encoding = TSInputEncodingUTF8;
	source.decode = nullptr;
	return ts_parser_parse(parser, oldTree, source);
}

} // namespace

struct MRTreeSitterDocument::Impl {
	struct Point {
		std::uint32_t row = 0;
		std::uint32_t column = 0;
	};

	struct PendingEdit {
		bool valid = false;
		std::size_t startByte = 0;
		std::size_t oldEndByte = 0;
		std::size_t newEndByte = 0;
		Point startPoint;
		Point oldEndPoint;
		Point newEndPoint;
	};

	Language language = Language::None;
	PendingEdit pendingEdit;
	TSParser *parser = nullptr;
	TSTree *tree = nullptr;
	std::size_t documentId = 0;
	std::size_t version = 0;

	static Language detectLanguage(std::string_view path, std::string_view title) noexcept {
		static const std::array<std::pair<std::string_view, Language>, 16> kExtensionMap = {{
		    {".C", Language::C},
		    {".H", Language::C},
		    {".CC", Language::Cpp},
		    {".CPP", Language::Cpp},
		    {".CPPM", Language::Cpp},
		    {".CXX", Language::Cpp},
		    {".C++", Language::Cpp},
		    {".CP", Language::Cpp},
		    {".HH", Language::Cpp},
		    {".HPP", Language::Cpp},
		    {".HXX", Language::Cpp},
		    {".H++", Language::Cpp},
		    {".IPP", Language::Cpp},
		    {".IXX", Language::Cpp},
		    {".TPP", Language::Cpp},
		    {".TXX", Language::Cpp},
		}};
		const std::string candidate = !path.empty() ? std::string(path) : std::string(title);
		const std::size_t slashPos = candidate.find_last_of("/\\");
		const std::size_t fileNameStart = slashPos == std::string::npos ? 0 : slashPos + 1;
		const std::size_t dotPos = candidate.find_last_of('.');

		if (candidate.empty() || dotPos == std::string::npos || dotPos < fileNameStart) return Language::None;
		const std::string extension = upperAscii(candidate.substr(dotPos));
		for (const auto &entry : kExtensionMap)
			if (extension == entry.first) return entry.second;
		return Language::None;
	}

	static Point pointAt(const mr::editor::TextDocument &document, std::size_t offset) noexcept {
		Point point;

		offset = std::min(offset, document.length());
		point.row = static_cast<std::uint32_t>(std::min<std::size_t>(document.lineIndex(offset), UINT32_MAX));
		point.column = static_cast<std::uint32_t>(std::min<std::size_t>(document.column(offset), UINT32_MAX));
		return point;
	}

	void clearPendingEdit() noexcept {
		pendingEdit = PendingEdit();
	}

	void clearTree() noexcept {
		if (tree != nullptr) {
			ts_tree_delete(tree);
			tree = nullptr;
		}
		documentId = 0;
		version = 0;
	}

	void releaseParser() noexcept {
		if (parser != nullptr) {
			ts_parser_delete(parser);
			parser = nullptr;
		}
	}

	void clearAll() noexcept {
		clearPendingEdit();
		clearTree();
		releaseParser();
		language = Language::None;
	}

	const TSLanguage *parserLanguage() const noexcept {
		switch (language) {
			case Language::C:
				return tree_sitter_c();
			case Language::Cpp:
				return tree_sitter_cpp();
			case Language::None:
				break;
		}
		return nullptr;
	}

	bool ensureParserLanguage() noexcept {
		const TSLanguage *nextLanguage = parserLanguage();

		if (nextLanguage == nullptr) return false;
		if (parser == nullptr) parser = ts_parser_new();
		if (parser == nullptr) return false;
		if (ts_parser_language(parser) == nextLanguage) return true;
		if (ts_parser_set_language(parser, nextLanguage)) return true;
		clearTree();
		releaseParser();
		return false;
	}

	void capturePendingEdit(const mr::editor::TextDocument &currentDocument, const mr::editor::TextDocument &nextDocument, const mr::editor::DocumentChangeSet *changeSet) noexcept {
		clearPendingEdit();
		if (changeSet == nullptr || !changeSet->changed) return;
		if (currentDocument.documentId() != nextDocument.documentId()) return;
		const mr::editor::Range touched = changeSet->touchedRange.normalized();
		const std::size_t start = std::min<std::size_t>(touched.start, changeSet->oldLength);
		const long long delta = static_cast<long long>(changeSet->newLength) - static_cast<long long>(changeSet->oldLength);
		std::size_t oldEnd = touched.end;
		std::size_t newEnd = touched.end;

		if (delta >= 0) oldEnd = oldEnd >= static_cast<std::size_t>(delta) ? oldEnd - static_cast<std::size_t>(delta) : start;
		else
			newEnd = newEnd >= static_cast<std::size_t>(-delta) ? newEnd - static_cast<std::size_t>(-delta) : start;
		oldEnd = std::clamp(oldEnd, start, changeSet->oldLength);
		newEnd = std::clamp(newEnd, start, changeSet->newLength);
		pendingEdit.valid = true;
		pendingEdit.startByte = start;
		pendingEdit.oldEndByte = oldEnd;
		pendingEdit.newEndByte = newEnd;
		pendingEdit.startPoint = pointAt(currentDocument, start);
		pendingEdit.oldEndPoint = pointAt(currentDocument, oldEnd);
		pendingEdit.newEndPoint = pointAt(nextDocument, newEnd);
	}
};

MRTreeSitterDocument::MRTreeSitterDocument() noexcept : mImpl(new (std::nothrow) Impl()) {
}

MRTreeSitterDocument::~MRTreeSitterDocument() noexcept = default;

void MRTreeSitterDocument::clear() noexcept {
	if (mImpl != nullptr) mImpl->clearAll();
}

void MRTreeSitterDocument::setLanguageContext(std::string_view path, std::string_view title) noexcept {
	if (mImpl == nullptr) return;
	const Language nextLanguage = Impl::detectLanguage(path, title);

	if (nextLanguage == mImpl->language) return;
	mImpl->clearPendingEdit();
	mImpl->clearTree();
	if (nextLanguage == Language::None) mImpl->releaseParser();
	mImpl->language = nextLanguage;
}

void MRTreeSitterDocument::prepareDocumentAdoption(const mr::editor::TextDocument &currentDocument, const mr::editor::TextDocument &nextDocument, const mr::editor::DocumentChangeSet *changeSet) noexcept {
	if (mImpl == nullptr) return;
	mImpl->capturePendingEdit(currentDocument, nextDocument, changeSet);
}

bool MRTreeSitterDocument::syncToDocument(const mr::editor::ReadSnapshot &snapshot, std::size_t documentId, std::size_t version) noexcept {
	if (mImpl == nullptr) return false;
	if (mImpl->language == Language::None) {
		mImpl->clearPendingEdit();
		mImpl->clearTree();
		mImpl->releaseParser();
		return false;
	}
	if (mImpl->tree != nullptr && mImpl->documentId == documentId && mImpl->version == version && !mImpl->pendingEdit.valid) return true;
	if (!mImpl->ensureParserLanguage()) {
		mImpl->clearPendingEdit();
		mImpl->clearTree();
		return false;
	}
	const bool reuseTree = mImpl->tree != nullptr && mImpl->documentId == documentId && mImpl->pendingEdit.valid;
	TSTree *previousTree = mImpl->tree;
	TSTree *parsedTree = nullptr;

	if (reuseTree) {
		TSInputEdit edit;

		edit.start_byte = static_cast<uint32_t>(std::min<std::size_t>(mImpl->pendingEdit.startByte, UINT32_MAX));
		edit.old_end_byte = static_cast<uint32_t>(std::min<std::size_t>(mImpl->pendingEdit.oldEndByte, UINT32_MAX));
		edit.new_end_byte = static_cast<uint32_t>(std::min<std::size_t>(mImpl->pendingEdit.newEndByte, UINT32_MAX));
		edit.start_point = TSPoint{mImpl->pendingEdit.startPoint.row, mImpl->pendingEdit.startPoint.column};
		edit.old_end_point = TSPoint{mImpl->pendingEdit.oldEndPoint.row, mImpl->pendingEdit.oldEndPoint.column};
		edit.new_end_point = TSPoint{mImpl->pendingEdit.newEndPoint.row, mImpl->pendingEdit.newEndPoint.column};
		ts_tree_edit(previousTree, &edit);
	}
	parsedTree = parseTreeSitterSnapshot(mImpl->parser, reuseTree ? previousTree : nullptr, snapshot);
	mImpl->clearPendingEdit();
	if (parsedTree == nullptr) {
		mImpl->clearTree();
		return false;
	}
	if (previousTree != nullptr) ts_tree_delete(previousTree);
	mImpl->tree = parsedTree;
	mImpl->documentId = documentId;
	mImpl->version = version;
	return true;
}

bool MRTreeSitterDocument::shouldIncreaseIndentOnNewLine(const mr::editor::ReadSnapshot &snapshot, std::size_t cursorOffset) const noexcept {
	if (mImpl == nullptr || mImpl->tree == nullptr) return false;
	const std::size_t previousOffset = previousNonWhitespaceOnLine(snapshot, cursorOffset);
	if (previousOffset >= snapshot.length()) return false;
	const char previousChar = snapshot.charAt(previousOffset);
	if (previousChar != '{' && previousChar != '(' && previousChar != '[') return false;

	TSNode node = ts_node_descendant_for_byte_range(ts_tree_root_node(mImpl->tree), static_cast<uint32_t>(std::min<std::size_t>(previousOffset, UINT32_MAX)), static_cast<uint32_t>(std::min<std::size_t>(previousOffset + 1, UINT32_MAX)));
	while (!ts_node_is_null(node)) {
		const char *typeName = ts_node_type(node);
		if (isTreeSitterCommentOrLiteral(typeName)) return false;
		node = ts_node_parent(node);
	}
	return true;
}

MRTreeSitterDocument::Language MRTreeSitterDocument::activeLanguage() const noexcept {
	return mImpl != nullptr ? mImpl->language : Language::None;
}

bool MRTreeSitterDocument::hasTree() const noexcept {
	return mImpl != nullptr && mImpl->tree != nullptr;
}
