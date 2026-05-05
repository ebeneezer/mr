#include "MRTreeSitterDocument.hpp"

#include <algorithm>
#include <cstdint>
#include <new>
#include <string>
#include <utility>

#include <tree_sitter/api.h>

#include "../../app/utils/MRStringUtils.hpp"
#include "../../piecetable/MRTextDocument.hpp"
#include "../MRSyntax.hpp"

extern "C" const TSLanguage *tree_sitter_c(void);
extern "C" const TSLanguage *tree_sitter_cpp(void);
extern "C" const TSLanguage *tree_sitter_javascript(void);
extern "C" const TSLanguage *tree_sitter_python(void);
extern "C" const TSLanguage *tree_sitter_json(void);
extern "C" const TSLanguage *tree_sitter_mrmac(void);

namespace {

struct TreeSitterParserOwner {
	TSParser *value = nullptr;

	TreeSitterParserOwner() = default;
	explicit TreeSitterParserOwner(TSParser *parser) noexcept : value(parser) {
	}

	TreeSitterParserOwner(const TreeSitterParserOwner &) = delete;
	TreeSitterParserOwner &operator=(const TreeSitterParserOwner &) = delete;

	TreeSitterParserOwner(TreeSitterParserOwner &&other) noexcept : value(other.release()) {
	}

	TreeSitterParserOwner &operator=(TreeSitterParserOwner &&other) noexcept {
		if (this != &other) reset(other.release());
		return *this;
	}

	~TreeSitterParserOwner() noexcept {
		reset();
	}

	TSParser *get() const noexcept {
		return value;
	}

	TSParser *release() noexcept {
		TSParser *released = value;

		value = nullptr;
		return released;
	}

	void reset(TSParser *parser = nullptr) noexcept {
		if (value != nullptr) ts_parser_delete(value);
		value = parser;
	}
};

struct TreeSitterTreeOwner {
	TSTree *value = nullptr;

	TreeSitterTreeOwner() = default;
	explicit TreeSitterTreeOwner(TSTree *tree) noexcept : value(tree) {
	}

	TreeSitterTreeOwner(const TreeSitterTreeOwner &) = delete;
	TreeSitterTreeOwner &operator=(const TreeSitterTreeOwner &) = delete;

	TreeSitterTreeOwner(TreeSitterTreeOwner &&other) noexcept : value(other.release()) {
	}

	TreeSitterTreeOwner &operator=(TreeSitterTreeOwner &&other) noexcept {
		if (this != &other) reset(other.release());
		return *this;
	}

	~TreeSitterTreeOwner() noexcept {
		reset();
	}

	TSTree *get() const noexcept {
		return value;
	}

	TSTree *release() noexcept {
		TSTree *released = value;

		value = nullptr;
		return released;
	}

	void reset(TSTree *tree = nullptr) noexcept {
		if (value != nullptr) ts_tree_delete(value);
		value = tree;
	}
};

bool isIndentWhitespace(char ch) noexcept {
	return ch == ' ' || ch == '\t';
}

void stripLeadingUtf8Bom(std::string &text) {
	if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEFu && static_cast<unsigned char>(text[1]) == 0xBBu && static_cast<unsigned char>(text[2]) == 0xBFu)
		text.erase(0, 3);
}

std::string normalizedIndentLineText(const mr::editor::ReadSnapshot &snapshot, std::size_t cursorOffset) {
	std::string lineText = snapshot.lineText(snapshot.lineStart(cursorOffset));

	stripLeadingUtf8Bom(lineText);
	return upperAscii(trimAscii(lineText));
}

MRTreeSitterDocument::Language configuredTreeSitterLanguage(std::string_view configuredLanguage) noexcept {
	const std::string normalized = upperAscii(trimAscii(std::string(configuredLanguage)));

	if (normalized == "C") return MRTreeSitterDocument::Language::C;
	if (normalized == "CPP") return MRTreeSitterDocument::Language::Cpp;
	if (normalized == "JAVASCRIPT") return MRTreeSitterDocument::Language::JavaScript;
	if (normalized == "PYTHON") return MRTreeSitterDocument::Language::Python;
	if (normalized == "JSON") return MRTreeSitterDocument::Language::Json;
	if (normalized == "MRMAC") return MRTreeSitterDocument::Language::MRMAC;
	return MRTreeSitterDocument::Language::None;
}

const TSLanguage *treeSitterParserLanguage(MRTreeSitterDocument::Language language) noexcept {
	switch (language) {
		case MRTreeSitterDocument::Language::C:
			return tree_sitter_c();
		case MRTreeSitterDocument::Language::Cpp:
			return tree_sitter_cpp();
		case MRTreeSitterDocument::Language::JavaScript:
			return tree_sitter_javascript();
		case MRTreeSitterDocument::Language::Python:
			return tree_sitter_python();
		case MRTreeSitterDocument::Language::Json:
			return tree_sitter_json();
		case MRTreeSitterDocument::Language::MRMAC:
			return tree_sitter_mrmac();
		case MRTreeSitterDocument::Language::None:
			break;
	}
	return nullptr;
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

std::size_t firstNonWhitespaceOnLine(const mr::editor::ReadSnapshot &snapshot, std::size_t cursorOffset) noexcept {
	const std::size_t safeOffset = std::min(cursorOffset, snapshot.length());
	const std::size_t lineStart = snapshot.lineStart(safeOffset);
	const std::size_t lineEnd = snapshot.lineEnd(safeOffset);
	std::size_t probe = lineStart;

	while (probe < lineEnd) {
		if (!isIndentWhitespace(snapshot.charAt(probe))) return probe;
		probe = std::min(lineEnd, probe + 1);
	}
	return snapshot.length();
}

bool isCommentOrLiteralAtOffset(const TSTree *tree, std::size_t offset) noexcept {
	if (tree == nullptr) return false;
	TSNode node = ts_node_descendant_for_byte_range(ts_tree_root_node(tree), static_cast<uint32_t>(std::min<std::size_t>(offset, UINT32_MAX)),
													static_cast<uint32_t>(std::min<std::size_t>(offset + 1, UINT32_MAX)));
	while (!ts_node_is_null(node)) {
		const char *typeName = ts_node_type(node);
		if (isTreeSitterCommentOrLiteral(typeName)) return true;
		node = ts_node_parent(node);
	}
	return false;
}

bool shouldIncreaseMrmacIndent(const mr::editor::ReadSnapshot &snapshot, const TSTree *tree, std::size_t cursorOffset) noexcept {
	const std::size_t previousOffset = previousNonWhitespaceOnLine(snapshot, cursorOffset);

	if (tree == nullptr || previousOffset >= snapshot.length()) return false;
	if (isCommentOrLiteralAtOffset(tree, previousOffset)) return false;

	const std::string lineText = normalizedIndentLineText(snapshot, cursorOffset);
	if (lineText.empty()) return false;
	if (lineText == "ELSE") return true;
	if (lineText.starts_with("$MACRO") && lineText.back() == ';') return true;
	if (lineText.starts_with("IF ") && lineText.ends_with("THEN")) return true;
	if (lineText.starts_with("WHILE ") && lineText.ends_with("DO")) return true;
	return false;
}

bool shouldIncreasePythonIndent(const mr::editor::ReadSnapshot &snapshot, const TSTree *tree, std::size_t cursorOffset) noexcept {
	const std::size_t previousOffset = previousNonWhitespaceOnLine(snapshot, cursorOffset);

	if (tree == nullptr || previousOffset >= snapshot.length()) return false;
	if (snapshot.charAt(previousOffset) != ':') return false;
	if (isCommentOrLiteralAtOffset(tree, previousOffset)) return false;

	const std::string lineText = normalizedIndentLineText(snapshot, cursorOffset);
	if (lineText.empty() || !lineText.ends_with(":")) return false;
	if (lineText == "ELSE:" || lineText == "TRY:" || lineText == "FINALLY:") return true;
	if (lineText.starts_with("IF ") || lineText.starts_with("ELIF ") || lineText.starts_with("FOR ") || lineText.starts_with("WHILE ") || lineText.starts_with("WITH ") ||
		lineText.starts_with("MATCH ") || lineText.starts_with("CASE ") || lineText.starts_with("EXCEPT ") || lineText.starts_with("DEF ") || lineText.starts_with("CLASS "))
		return true;
	if (lineText.starts_with("ASYNC DEF ") || lineText.starts_with("ASYNC FOR ") || lineText.starts_with("ASYNC WITH ")) return true;
	return false;
}

bool shouldDedentMrmacLine(const mr::editor::ReadSnapshot &snapshot, const TSTree *tree, std::size_t cursorOffset) noexcept {
	const std::size_t firstOffset = firstNonWhitespaceOnLine(snapshot, cursorOffset);

	if (tree == nullptr || firstOffset >= snapshot.length()) return false;
	if (isCommentOrLiteralAtOffset(tree, firstOffset)) return false;
	return normalizedIndentLineText(snapshot, cursorOffset) == "ELSE";
}

bool shouldDedentPythonLine(const mr::editor::ReadSnapshot &snapshot, const TSTree *tree, std::size_t cursorOffset) noexcept {
	const std::size_t firstOffset = firstNonWhitespaceOnLine(snapshot, cursorOffset);
	const std::string lineText = normalizedIndentLineText(snapshot, cursorOffset);

	if (tree == nullptr || firstOffset >= snapshot.length()) return false;
	if (lineText.empty() || !lineText.ends_with(":")) return false;
	if (isCommentOrLiteralAtOffset(tree, firstOffset)) return false;
	if (lineText == "ELSE:" || lineText == "FINALLY:") return true;
	if (lineText.starts_with("ELIF ") || lineText.starts_with("CASE ")) return true;
	if (lineText == "EXCEPT:" || lineText.starts_with("EXCEPT ")) return true;
	return false;
}

bool shouldDedentBracketLine(const mr::editor::ReadSnapshot &snapshot, const TSTree *tree, std::size_t cursorOffset) noexcept {
	const std::size_t firstOffset = firstNonWhitespaceOnLine(snapshot, cursorOffset);
	const std::string lineText = trimAscii(snapshot.lineText(snapshot.lineStart(cursorOffset)));

	if (tree == nullptr || firstOffset >= snapshot.length() || lineText.empty()) return false;
	if (isCommentOrLiteralAtOffset(tree, firstOffset)) return false;
	return lineText.starts_with("}") || lineText.starts_with("]") || lineText.starts_with(")");
}

MRSyntaxToken syntaxTokenForTreeSitterNode(MRTreeSitterDocument::Language language, std::string_view type, std::string_view parentType) noexcept {
	if (parentType == "pair" && (type == "string" || type == "property_identifier" || type == "shorthand_property_identifier")) return MRSyntaxToken::Key;
	if (type.find("comment") != std::string_view::npos) return MRSyntaxToken::Comment;
	if (type.find("preproc") != std::string_view::npos || type.find("directive") != std::string_view::npos || type == "hash_bang_line") return MRSyntaxToken::Directive;
	if (type.find("string") != std::string_view::npos || type.find("char_literal") != std::string_view::npos || type == "escape_sequence") return MRSyntaxToken::String;
	if (type.find("number") != std::string_view::npos || type.find("integer") != std::string_view::npos || type.find("float") != std::string_view::npos) return MRSyntaxToken::Number;

	switch (language) {
		case MRTreeSitterDocument::Language::C:
			if (type == "true" || type == "false" || type == "null")
				return MRSyntaxToken::Keyword;
			else if (type == "primitive_type" || type == "type_identifier" || type == "sized_type_specifier")
				return MRSyntaxToken::Type;
			else if (type == "storage_class_specifier" || type == "type_qualifier")
				return MRSyntaxToken::Keyword;
			else if (type == "if" || type == "else" || type == "for" || type == "while" || type == "do" || type == "switch" || type == "case" || type == "default" ||
					 type == "break" || type == "continue" || type == "return" || type == "goto" || type == "sizeof" || type == "typedef" || type == "struct" ||
					 type == "union" || type == "enum" || type == "static" || type == "extern" || type == "inline" || type == "const" || type == "volatile")
				return MRSyntaxToken::Keyword;
			break;
		case MRTreeSitterDocument::Language::Cpp:
			if (type == "true" || type == "false" || type == "null" || type == "nullptr")
				return MRSyntaxToken::Keyword;
			else if (type == "primitive_type" || type == "type_identifier" || type == "sized_type_specifier" || type == "namespace_identifier" || type == "auto" ||
					 type == "template_type" ||
					 (type == "qualified_identifier" &&
					  (parentType == "base_class_clause" || parentType == "class_specifier" || parentType == "struct_specifier" || parentType == "union_specifier" ||
					   parentType == "type_requirement" || parentType == "compound_literal_expression")))
				return MRSyntaxToken::Type;
			else if (type == "storage_class_specifier" || type == "type_qualifier" || type == "this")
				return MRSyntaxToken::Keyword;
			else if (type == "if" || type == "else" || type == "for" || type == "while" || type == "do" || type == "switch" || type == "case" || type == "default" ||
					 type == "break" || type == "continue" || type == "return" || type == "goto" || type == "try" || type == "catch" || type == "throw" ||
					 type == "new" || type == "delete" || type == "void" || type == "typedef" || type == "struct" || type == "union" || type == "enum" ||
					 type == "namespace" || type == "template" || type == "typename" || type == "using" || type == "public" || type == "private" ||
					 type == "protected" || type == "virtual" || type == "override" || type == "constexpr" || type == "consteval" || type == "constinit" ||
					 type == "explicit" || type == "final" || type == "inline" || type == "static" || type == "extern" || type == "volatile" || type == "mutable" ||
					 type == "friend" || type == "operator" || type == "noexcept" || type == "requires" || type == "concept" || type == "sizeof" || type == "co_await" ||
					 type == "co_return" || type == "co_yield")
				return MRSyntaxToken::Keyword;
			break;
		case MRTreeSitterDocument::Language::JavaScript:
			if (type == "true" || type == "false" || type == "null")
				return MRSyntaxToken::Keyword;
			else if (type == "undefined" || type == "null")
				return MRSyntaxToken::Type;
			else if (type == "this" || type == "super")
				return MRSyntaxToken::Keyword;
			else if (type == "if" || type == "else" || type == "for" || type == "while" || type == "do" || type == "switch" || type == "case" || type == "default" ||
					 type == "break" || type == "continue" || type == "return" || type == "try" || type == "catch" || type == "throw" || type == "finally" ||
					 type == "import" || type == "export" || type == "from" || type == "as" || type == "function" || type == "class" || type == "new" ||
					 type == "delete" || type == "typeof" || type == "instanceof" || type == "yield" || type == "await" || type == "async" || type == "const" ||
					 type == "let" || type == "var" || type == "extends" || type == "of" || type == "static" || type == "void" || type == "debugger" || type == "with" ||
					 type == "get" || type == "set")
				return MRSyntaxToken::Keyword;
			break;
		case MRTreeSitterDocument::Language::Python:
			if (type == "true" || type == "false" || type == "none")
				return MRSyntaxToken::Keyword;
			else if (type == "identifier" && parentType == "type")
				return MRSyntaxToken::Type;
			else if (type == "if" || type == "elif" || type == "else" || type == "for" || type == "while" || type == "break" || type == "continue" || type == "return" ||
					 type == "try" || type == "except" || type == "finally" || type == "raise" || type == "import" || type == "from" || type == "as" || type == "def" ||
					 type == "class" || type == "lambda" || type == "yield" || type == "await" || type == "async" || type == "with" || type == "pass" || type == "assert" ||
					 type == "match" || type == "case" || type == "global" || type == "nonlocal" || type == "del" || type == "and" || type == "or" || type == "not" ||
					 type == "in" || type == "is" || type == "exec" || type == "print")
				return MRSyntaxToken::Keyword;
			break;
		case MRTreeSitterDocument::Language::Json:
			if (type == "true" || type == "false" || type == "null") return MRSyntaxToken::Keyword;
			break;
		case MRTreeSitterDocument::Language::MRMAC:
			if (type == "directive" || type == "keyspec")
				return MRSyntaxToken::Directive;
			else if (type == "keyword")
				return MRSyntaxToken::Keyword;
			else if (type == "string_literal")
				return MRSyntaxToken::String;
			else if (type == "integer_literal")
				return MRSyntaxToken::Number;
			break;
		case MRTreeSitterDocument::Language::None:
			break;
	}
	return MRSyntaxToken::Text;
}

struct TreeSitterSnapshotInput {
	mr::editor::ReadSnapshot snapshot;
	std::size_t pieceIndex = 0;
	std::size_t pieceStart = 0;
	std::size_t pieceEnd = 0;
	mr::editor::PieceChunkView piece;
	bool pieceValid = false;
};

struct DerivedSyntaxTokenMapRequest {
	const mr::editor::ReadSnapshot &snapshot;
	const std::vector<std::size_t> &lineStarts;
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

TreeSitterTreeOwner parseTreeSitterSnapshotTree(const TSLanguage *parserLanguage, const mr::editor::ReadSnapshot &snapshot) noexcept {
	TreeSitterParserOwner parser(ts_parser_new());

	if (parser.get() == nullptr) return TreeSitterTreeOwner();
	if (!ts_parser_set_language(parser.get(), parserLanguage)) return TreeSitterTreeOwner();

	TreeSitterSnapshotInput input;
	TSInput source;
	TreeSitterTreeOwner tree;

	input.snapshot = snapshot;
	source.payload = &input;
	source.read = readTreeSitterSnapshot;
	source.encoding = TSInputEncodingUTF8;
	source.decode = nullptr;
	tree.reset(ts_parser_parse(parser.get(), nullptr, source));
	return tree;
}

std::vector<MRSyntaxTokenMap> makeDerivedTokenMaps(const DerivedSyntaxTokenMapRequest &request) {
	std::vector<MRSyntaxTokenMap> tokenMaps;

	tokenMaps.reserve(request.lineStarts.size());
	for (std::size_t i = 0; i < request.lineStarts.size(); ++i)
		tokenMaps.push_back(MRSyntaxTokenMap(request.snapshot.lineText(request.lineStarts[i]).size(), MRSyntaxToken::Text));
	return tokenMaps;
}

// Tree-sitter stays canonical; MR token maps are derived syntax data for the requested lines.
void deriveTokenMapsFromCanonicalTree(MRTreeSitterDocument::Language language, const DerivedSyntaxTokenMapRequest &request, const TSTree *tree,
									  std::vector<MRSyntaxTokenMap> &tokenMaps) {
	const TSNode root = ts_tree_root_node(tree);

	for (std::size_t lineIndex = 0; lineIndex < request.lineStarts.size(); ++lineIndex) {
		const std::size_t safeLineStart = std::min(request.lineStarts[lineIndex], request.snapshot.length());
		const std::size_t lineEnd = safeLineStart + tokenMaps[lineIndex].size();
		std::vector<TSNode> stack;

		if (tokenMaps[lineIndex].empty()) continue;
		stack.push_back(root);
		while (!stack.empty()) {
			const TSNode node = stack.back();
			stack.pop_back();

			if (ts_node_is_null(node)) continue;
			const std::size_t nodeStart = ts_node_start_byte(node);
			const std::size_t nodeEnd = ts_node_end_byte(node);
			if (nodeEnd <= safeLineStart || nodeStart >= lineEnd || nodeEnd <= nodeStart) continue;

			const uint32_t childCount = ts_node_child_count(node);
			for (uint32_t childIndex = childCount; childIndex > 0; --childIndex) stack.push_back(ts_node_child(node, childIndex - 1));

			const char *typeName = ts_node_type(node);
			if (typeName == nullptr || *typeName == '\0') continue;

			const TSNode parent = ts_node_parent(node);
			const char *parentTypeName = !ts_node_is_null(parent) ? ts_node_type(parent) : nullptr;
			const std::string_view type(typeName);
			const std::string_view parentType = parentTypeName != nullptr ? std::string_view(parentTypeName) : std::string_view();
			const MRSyntaxToken token = syntaxTokenForTreeSitterNode(language, type, parentType);

			if (token == MRSyntaxToken::Text) continue;

			const std::size_t paintStart = std::max(nodeStart, safeLineStart) - safeLineStart;
			const std::size_t paintEnd = std::min(nodeEnd, lineEnd) - safeLineStart;
			for (std::size_t index = paintStart; index < paintEnd && index < tokenMaps[lineIndex].size(); ++index) tokenMaps[lineIndex][index] = token;
		}
	}
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
	TreeSitterParserOwner parser;
	TreeSitterTreeOwner tree;
	std::size_t documentId = 0;
	std::size_t version = 0;

	static Point pointAt(const mr::editor::TextDocument &document, std::size_t offset) noexcept {
		Point point;

		offset = std::min(offset, document.length());
		point.row = static_cast<std::uint32_t>(std::min<std::size_t>(document.lineIndex(offset), UINT32_MAX));
		point.column = static_cast<std::uint32_t>(std::min<std::size_t>(document.column(offset), UINT32_MAX));
		return point;
	}

	void clearTree() noexcept {
		tree.reset();
		documentId = 0;
		version = 0;
	}

	void releaseParser() noexcept {
		parser.reset();
	}

	const TSLanguage *parserLanguage() const noexcept {
		return treeSitterParserLanguage(language);
	}
};

MRTreeSitterDocument::MRTreeSitterDocument() noexcept : mImpl(new (std::nothrow) Impl()) {
}

MRTreeSitterDocument::~MRTreeSitterDocument() noexcept = default;

void MRTreeSitterDocument::clear() noexcept {
	if (mImpl == nullptr) return;
	mImpl->pendingEdit = Impl::PendingEdit();
	mImpl->clearTree();
	mImpl->releaseParser();
	mImpl->language = Language::None;
}

void MRTreeSitterDocument::setLanguageContext(std::string_view path, std::string_view title, std::string_view configuredLanguage) noexcept {
	if (mImpl == nullptr) return;
	Language nextLanguage = configuredTreeSitterLanguage(configuredLanguage);

	if (nextLanguage == Language::None) nextLanguage = configuredTreeSitterLanguage(tmrDetectTreeSitterLanguageName(std::string(path), std::string(title)));

	if (nextLanguage == mImpl->language) return;
	mImpl->pendingEdit = Impl::PendingEdit();
	mImpl->clearTree();
	if (nextLanguage == Language::None) mImpl->releaseParser();
	mImpl->language = nextLanguage;
}

void MRTreeSitterDocument::prepareDocumentAdoption(const mr::editor::TextDocument &currentDocument, const mr::editor::TextDocument &nextDocument, const mr::editor::DocumentChangeSet *changeSet) noexcept {
	if (mImpl == nullptr) return;
	mImpl->pendingEdit = Impl::PendingEdit();
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
	mImpl->pendingEdit.valid = true;
	mImpl->pendingEdit.startByte = start;
	mImpl->pendingEdit.oldEndByte = oldEnd;
	mImpl->pendingEdit.newEndByte = newEnd;
	mImpl->pendingEdit.startPoint = Impl::pointAt(currentDocument, start);
	mImpl->pendingEdit.oldEndPoint = Impl::pointAt(currentDocument, oldEnd);
	mImpl->pendingEdit.newEndPoint = Impl::pointAt(nextDocument, newEnd);
}

bool MRTreeSitterDocument::syncToDocument(const mr::editor::ReadSnapshot &snapshot, std::size_t documentId, std::size_t version) noexcept {
	if (mImpl == nullptr) return false;
	if (mImpl->language == Language::None) {
		mImpl->pendingEdit = Impl::PendingEdit();
		mImpl->clearTree();
		mImpl->releaseParser();
		return false;
	}
	if (mImpl->tree.get() != nullptr && mImpl->documentId == documentId && mImpl->version == version && !mImpl->pendingEdit.valid) return true;
	const TSLanguage *nextLanguage = mImpl->parserLanguage();

	if (nextLanguage == nullptr) {
		mImpl->pendingEdit = Impl::PendingEdit();
		mImpl->clearTree();
		return false;
	}
	if (mImpl->parser.get() == nullptr) mImpl->parser.reset(ts_parser_new());
	if (mImpl->parser.get() == nullptr) {
		mImpl->pendingEdit = Impl::PendingEdit();
		mImpl->clearTree();
		return false;
	}
	if (ts_parser_language(mImpl->parser.get()) != nextLanguage && !ts_parser_set_language(mImpl->parser.get(), nextLanguage)) {
		mImpl->pendingEdit = Impl::PendingEdit();
		mImpl->clearTree();
		mImpl->releaseParser();
		return false;
	}
	const bool reuseTree = mImpl->tree.get() != nullptr && mImpl->documentId == documentId && mImpl->pendingEdit.valid;
	TSTree *previousTree = reuseTree ? mImpl->tree.get() : nullptr;

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
	{
		TreeSitterSnapshotInput input;
		TSInput source;
		TreeSitterTreeOwner parsedTree;

		input.snapshot = snapshot;
		source.payload = &input;
		source.read = readTreeSitterSnapshot;
		source.encoding = TSInputEncodingUTF8;
		source.decode = nullptr;
		parsedTree.reset(ts_parser_parse(mImpl->parser.get(), previousTree, source));
		mImpl->pendingEdit = Impl::PendingEdit();
		if (parsedTree.get() == nullptr) {
			mImpl->clearTree();
			return false;
		}
		mImpl->tree.reset(parsedTree.release());
	}
	mImpl->documentId = documentId;
	mImpl->version = version;
	return true;
}

bool MRTreeSitterDocument::shouldIncreaseIndentOnNewLine(const mr::editor::ReadSnapshot &snapshot, std::size_t cursorOffset) const noexcept {
	if (mImpl == nullptr || mImpl->tree.get() == nullptr) return false;
	if (mImpl->language == Language::MRMAC) return shouldIncreaseMrmacIndent(snapshot, mImpl->tree.get(), cursorOffset);
	if (mImpl->language == Language::Python) return shouldIncreasePythonIndent(snapshot, mImpl->tree.get(), cursorOffset);
	const std::size_t previousOffset = previousNonWhitespaceOnLine(snapshot, cursorOffset);
	if (previousOffset >= snapshot.length()) return false;
	const char previousChar = snapshot.charAt(previousOffset);
	if (previousChar != '{' && previousChar != '(' && previousChar != '[') return false;
	return !isCommentOrLiteralAtOffset(mImpl->tree.get(), previousOffset);
}

bool MRTreeSitterDocument::shouldDedentCurrentLine(const mr::editor::ReadSnapshot &snapshot, std::size_t cursorOffset) const noexcept {
	if (mImpl == nullptr || mImpl->tree.get() == nullptr) return false;
	switch (mImpl->language) {
		case Language::MRMAC:
			return shouldDedentMrmacLine(snapshot, mImpl->tree.get(), cursorOffset);
		case Language::Python:
			return shouldDedentPythonLine(snapshot, mImpl->tree.get(), cursorOffset);
		case Language::C:
		case Language::Cpp:
		case Language::JavaScript:
		case Language::Json:
			return shouldDedentBracketLine(snapshot, mImpl->tree.get(), cursorOffset);
		case Language::None:
			break;
	}
	return false;
}

std::vector<MRSyntaxTokenMap> MRTreeSitterDocument::buildTokenMapsForSnapshotLines(Language language, const mr::editor::ReadSnapshot &snapshot, const std::vector<std::size_t> &lineStarts) {
	const DerivedSyntaxTokenMapRequest request{snapshot, lineStarts};
	std::vector<MRSyntaxTokenMap> tokenMaps = makeDerivedTokenMaps(request);
	const TSLanguage *parserLanguage = treeSitterParserLanguage(language);

	if (lineStarts.empty()) return tokenMaps;
	if (parserLanguage == nullptr) return tokenMaps;

	TreeSitterTreeOwner tree = parseTreeSitterSnapshotTree(parserLanguage, snapshot);
	if (tree.get() == nullptr) return tokenMaps;

	deriveTokenMapsFromCanonicalTree(language, request, tree.get(), tokenMaps);
	return tokenMaps;
}

MRTreeSitterDocument::Language MRTreeSitterDocument::activeLanguage() const noexcept {
	return mImpl != nullptr ? mImpl->language : Language::None;
}

bool MRTreeSitterDocument::hasTree() const noexcept {
	return mImpl != nullptr && mImpl->tree.get() != nullptr;
}
