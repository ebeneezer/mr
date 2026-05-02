#pragma once

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

#include "../MRSyntax.hpp"

namespace mr::editor {

class TextDocument;
class ReadSnapshot;
struct DocumentChangeSet;

}

class MRFileEditor;

class MRTreeSitterDocument {
  public:
	enum class Language : unsigned char {
		None,
		C,
		Cpp,
		JavaScript,
		Python,
		Json,
		MRMAC
	};

	MRTreeSitterDocument() noexcept;
	~MRTreeSitterDocument() noexcept;

	MRTreeSitterDocument(const MRTreeSitterDocument &) = delete;
	MRTreeSitterDocument &operator=(const MRTreeSitterDocument &) = delete;

	void clear() noexcept;
	void setLanguageContext(std::string_view path, std::string_view title, std::string_view configuredLanguage) noexcept;
	void prepareDocumentAdoption(const mr::editor::TextDocument &currentDocument, const mr::editor::TextDocument &nextDocument, const mr::editor::DocumentChangeSet *changeSet) noexcept;
	bool syncToDocument(const mr::editor::ReadSnapshot &snapshot, std::size_t documentId, std::size_t version) noexcept;
	[[nodiscard]] bool shouldIncreaseIndentOnNewLine(const mr::editor::ReadSnapshot &snapshot, std::size_t cursorOffset) const noexcept;
	[[nodiscard]] bool shouldDedentCurrentLine(const mr::editor::ReadSnapshot &snapshot, std::size_t cursorOffset) const noexcept;
	[[nodiscard]] Language activeLanguage() const noexcept;
	[[nodiscard]] bool hasTree() const noexcept;

  private:
	friend class MRFileEditor;

	[[nodiscard]] static std::vector<MRSyntaxTokenMap> buildTokenMapsForSnapshotLines(Language language, const mr::editor::ReadSnapshot &snapshot, const std::vector<std::size_t> &lineStarts);
	struct Impl;
	std::unique_ptr<Impl> mImpl;
};
