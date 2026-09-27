#pragma once

#include "../coprocessor/MRCoprocessor.hpp"

#include <cstddef>
#include <memory>
#include <string>

struct MRLibraryReferenceQuery {
	std::string sourcePath;
	std::string sourceText;
	std::string symbol;
	std::size_t offset = 0;
};

struct MRLibraryReferenceEntry {
	std::string title;
	std::string text;
	bool found = false;
};

class MRLibraryReferenceProvider {
  public:
	virtual ~MRLibraryReferenceProvider() = default;
	[[nodiscard]] virtual MRLibraryReferenceEntry lookup(const MRLibraryReferenceQuery &query) const = 0;
};

[[nodiscard]] std::unique_ptr<MRLibraryReferenceProvider> mrLibraryReferenceProviderForLanguage(const std::string &language);

struct MRLibraryReferencePayload final : mr::coprocessor::Payload {
	int bufferId = 0;
	std::size_t documentId = 0;
	std::size_t documentVersion = 0;
	std::size_t cursorOffset = 0;
	std::size_t symbolOffset = 0;
	std::string symbol;
	MRLibraryReferenceEntry entry;
};
