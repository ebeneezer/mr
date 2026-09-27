#include "MRLibraryReference.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string_view>

#if __has_include(<clang-c/Index.h>)
#include <clang-c/Index.h>
#include <dlfcn.h>
#define MR_HAS_CLANG_C_INDEX 1
#endif

namespace {

bool validCSymbol(const std::string &symbol) noexcept {
	if (symbol.empty() || !(std::isalpha(static_cast<unsigned char>(symbol[0])) || symbol[0] == '_')) return false;
	for (char ch : symbol)
		if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')) return false;
	return true;
}

std::string trimReferenceLine(std::string_view line) {
	while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.remove_prefix(1);
	while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.remove_suffix(1);
	return std::string(line);
}

std::string localManualDescription(const std::string &symbol) {
	if (!validCSymbol(symbol)) return std::string();
	const std::string command = "LC_ALL=C MANWIDTH=80 man -P cat 3 " + symbol + " 2>/dev/null";
	FILE *stream = ::popen(command.c_str(), "r");
	if (stream == nullptr) return std::string();
	std::string raw;
	char buffer[512];
	while (raw.size() < 128 * 1024 && std::fgets(buffer, sizeof(buffer), stream) != nullptr) raw += buffer;
	const int status = ::pclose(stream);
	if (status != 0 || raw.empty()) return std::string();

	std::string plain;
	plain.reserve(raw.size());
	for (char ch : raw) {
		if (ch == '\b') {
			if (!plain.empty()) plain.pop_back();
			continue;
		}
		plain.push_back(ch);
	}

	std::istringstream lines(plain);
	std::string line;
	std::string description;
	bool includeSection = false;
	constexpr std::array<std::string_view, 9> includedSections = {"SYNOPSIS", "DESCRIPTION", "RETURN VALUE", "ERRORS", "NOTES", "CAVEATS", "BUGS", "EXAMPLES", "SEE ALSO"};
	while (std::getline(lines, line)) {
		const std::string trimmed = trimReferenceLine(line);
		if (line == trimmed && std::find(includedSections.begin(), includedSections.end(), trimmed) != includedSections.end()) {
			includeSection = true;
			if (!description.empty()) description += "\n\n";
			description += trimmed;
			continue;
		}
		if (line == trimmed && !trimmed.empty()) includeSection = false;
		if (!includeSection || trimmed.empty()) continue;
		description.push_back('\n');
		description += trimmed;
	}
	return description;
}

std::string cleanHeaderComment(const std::string &raw) {
	std::istringstream lines(raw);
	std::string line;
	std::string cleaned;
	while (std::getline(lines, line)) {
		std::string_view text(line);
		while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
		if (text.substr(0, 3) == "/**" || text.substr(0, 3) == "///" || text.substr(0, 3) == "//!") text.remove_prefix(3);
		else if (text.substr(0, 2) == "/*" || text.substr(0, 2) == "//") text.remove_prefix(2);
		else if (!text.empty() && text.front() == '*') text.remove_prefix(1);
		if (text.size() >= 2 && text.substr(text.size() - 2) == "*/") text.remove_suffix(2);
		const std::string content = trimReferenceLine(text);
		if (!content.empty()) {
			if (!cleaned.empty()) cleaned.push_back('\n');
			cleaned += content;
		}
	}
	return cleaned;
}

#if defined(MR_HAS_CLANG_C_INDEX)
std::string clangResourceDirectory() {
	FILE *stream = ::popen("clang -print-resource-dir 2>/dev/null", "r");
	if (stream == nullptr) return std::string();
	char buffer[512]{};
	const bool read = std::fgets(buffer, sizeof(buffer), stream) != nullptr;
	const int status = ::pclose(stream);
	return read && status == 0 ? trimReferenceLine(buffer) : std::string();
}

std::string compilerReportedClangLibrary() {
	FILE *stream = ::popen("clang -print-file-name=libclang.so 2>/dev/null", "r");
	if (stream == nullptr) return std::string();
	char buffer[512]{};
	const bool read = std::fgets(buffer, sizeof(buffer), stream) != nullptr;
	const int status = ::pclose(stream);
	const std::string path = read && status == 0 ? trimReferenceLine(buffer) : std::string();
	return !path.empty() && path.front() == '/' ? path : std::string();
}

struct ClangApi {
	void *library = nullptr;
	decltype(&clang_createIndex) createIndex = nullptr;
	decltype(&clang_disposeIndex) disposeIndex = nullptr;
	decltype(&clang_parseTranslationUnit) parseTranslationUnit = nullptr;
	decltype(&clang_disposeTranslationUnit) disposeTranslationUnit = nullptr;
	decltype(&clang_getFile) getFile = nullptr;
	decltype(&clang_getLocationForOffset) getLocationForOffset = nullptr;
	decltype(&clang_getCursor) getCursor = nullptr;
	decltype(&clang_getTranslationUnitCursor) getTranslationUnitCursor = nullptr;
	decltype(&clang_getCursorLocation) getCursorLocation = nullptr;
	decltype(&clang_getSpellingLocation) getSpellingLocation = nullptr;
	decltype(&clang_visitChildren) visitChildren = nullptr;
	decltype(&clang_getCursorReferenced) getCursorReferenced = nullptr;
	decltype(&clang_Cursor_isNull) cursorIsNull = nullptr;
	decltype(&clang_getCursorKind) getCursorKind = nullptr;
	decltype(&clang_getCursorSpelling) getCursorSpelling = nullptr;
	decltype(&clang_getCursorResultType) getCursorResultType = nullptr;
	decltype(&clang_getTypeSpelling) getTypeSpelling = nullptr;
	decltype(&clang_Cursor_getNumArguments) getNumArguments = nullptr;
	decltype(&clang_Cursor_getArgument) getArgument = nullptr;
	decltype(&clang_getCursorType) getCursorType = nullptr;
	decltype(&clang_isFunctionTypeVariadic) isFunctionTypeVariadic = nullptr;
	decltype(&clang_Cursor_getRawCommentText) getRawCommentText = nullptr;
	decltype(&clang_getCString) getCString = nullptr;
	decltype(&clang_disposeString) disposeString = nullptr;

	ClangApi() {
		library = ::dlopen("libclang.so", RTLD_LAZY | RTLD_LOCAL);
		if (library == nullptr) {
			const std::string reportedPath = compilerReportedClangLibrary();
			if (!reportedPath.empty()) library = ::dlopen(reportedPath.c_str(), RTLD_LAZY | RTLD_LOCAL);
		}
		const char *names[] = {"libclang.so.22", "libclang.so.21", "libclang.so.20", "libclang.so.19", "libclang.so.18", "libclang.so.17", "libclang.so.16", "libclang.so.15", "libclang.so.14", "libclang-14.so.1"};
		for (const char *name : names) {
			if (library != nullptr) break;
			library = ::dlopen(name, RTLD_LAZY | RTLD_LOCAL);
		}
		if (library == nullptr) return;
#define MR_LOAD_CLANG(member, symbol) member = reinterpret_cast<decltype(member)>(::dlsym(library, #symbol))
		MR_LOAD_CLANG(createIndex, clang_createIndex);
		MR_LOAD_CLANG(disposeIndex, clang_disposeIndex);
		MR_LOAD_CLANG(parseTranslationUnit, clang_parseTranslationUnit);
		MR_LOAD_CLANG(disposeTranslationUnit, clang_disposeTranslationUnit);
		MR_LOAD_CLANG(getFile, clang_getFile);
		MR_LOAD_CLANG(getLocationForOffset, clang_getLocationForOffset);
		MR_LOAD_CLANG(getCursor, clang_getCursor);
		MR_LOAD_CLANG(getTranslationUnitCursor, clang_getTranslationUnitCursor);
		MR_LOAD_CLANG(getCursorLocation, clang_getCursorLocation);
		MR_LOAD_CLANG(getSpellingLocation, clang_getSpellingLocation);
		MR_LOAD_CLANG(visitChildren, clang_visitChildren);
		MR_LOAD_CLANG(getCursorReferenced, clang_getCursorReferenced);
		MR_LOAD_CLANG(cursorIsNull, clang_Cursor_isNull);
		MR_LOAD_CLANG(getCursorKind, clang_getCursorKind);
		MR_LOAD_CLANG(getCursorSpelling, clang_getCursorSpelling);
		MR_LOAD_CLANG(getCursorResultType, clang_getCursorResultType);
		MR_LOAD_CLANG(getTypeSpelling, clang_getTypeSpelling);
		MR_LOAD_CLANG(getNumArguments, clang_Cursor_getNumArguments);
		MR_LOAD_CLANG(getArgument, clang_Cursor_getArgument);
		MR_LOAD_CLANG(getCursorType, clang_getCursorType);
		MR_LOAD_CLANG(isFunctionTypeVariadic, clang_isFunctionTypeVariadic);
		MR_LOAD_CLANG(getRawCommentText, clang_Cursor_getRawCommentText);
		MR_LOAD_CLANG(getCString, clang_getCString);
		MR_LOAD_CLANG(disposeString, clang_disposeString);
#undef MR_LOAD_CLANG
	}

	~ClangApi() {
		if (library != nullptr) ::dlclose(library);
	}

	bool ready() const noexcept {
		return createIndex != nullptr && disposeIndex != nullptr && parseTranslationUnit != nullptr && disposeTranslationUnit != nullptr && getFile != nullptr && getLocationForOffset != nullptr && getCursor != nullptr && getTranslationUnitCursor != nullptr && getCursorLocation != nullptr && getSpellingLocation != nullptr && visitChildren != nullptr &&
		       getCursorReferenced != nullptr && cursorIsNull != nullptr && getCursorKind != nullptr && getCursorSpelling != nullptr && getCursorResultType != nullptr && getTypeSpelling != nullptr && getNumArguments != nullptr &&
		       getArgument != nullptr && getCursorType != nullptr && isFunctionTypeVariadic != nullptr && getRawCommentText != nullptr && getCString != nullptr && disposeString != nullptr;
	}

	std::string stringValue(CXString value) const {
		const char *text = getCString(value);
		std::string result = text != nullptr ? text : "";
		disposeString(value);
		return result;
	}
};

struct CursorSearch {
	const ClangApi *api;
	CXFile file;
	unsigned offset;
	const std::string *symbol;
	CXCursor declaration;
	bool found;
};

CXChildVisitResult visitSourceCursor(CXCursor cursor, CXCursor, CXClientData data) {
	CursorSearch &search = *static_cast<CursorSearch *>(data);
	CXFile cursorFile = nullptr;
	unsigned cursorOffset = 0;
	search.api->getSpellingLocation(search.api->getCursorLocation(cursor), &cursorFile, nullptr, nullptr, &cursorOffset);
	if (cursorFile != search.file) return CXChildVisit_Continue;
	if (cursorOffset == search.offset && search.api->stringValue(search.api->getCursorSpelling(cursor)) == *search.symbol) {
		CXCursor referenced = search.api->getCursorReferenced(cursor);
		if (!search.api->cursorIsNull(referenced) && search.api->getCursorKind(referenced) == CXCursor_FunctionDecl) {
			search.declaration = referenced;
			search.found = true;
			return CXChildVisit_Break;
		}
	}
	return CXChildVisit_Recurse;
}

bool headerReference(const MRLibraryReferenceQuery &query, MRLibraryReferenceEntry &entry) {
	ClangApi api;
	if (!api.ready() || query.sourcePath.empty() || query.offset >= query.sourceText.size()) return false;
	CXIndex index = api.createIndex(0, 0);
	if (index == nullptr) return false;
	const std::string resourceDirectory = clangResourceDirectory();
	const std::string resourceArgument = resourceDirectory.empty() ? std::string() : "-resource-dir=" + resourceDirectory;
	const char *arguments[] = {"-x", "c", "-std=gnu11", resourceArgument.c_str()};
	CXUnsavedFile unsaved{query.sourcePath.c_str(), query.sourceText.c_str(), static_cast<unsigned long>(query.sourceText.size())};
	CXTranslationUnit unit = api.parseTranslationUnit(index, query.sourcePath.c_str(), arguments, resourceArgument.empty() ? 3 : 4, &unsaved, 1, CXTranslationUnit_KeepGoing);
	if (unit == nullptr) {
		api.disposeIndex(index);
		return false;
	}
	CXFile file = api.getFile(unit, query.sourcePath.c_str());
	bool found = false;
	if (file != nullptr) {
		CXSourceLocation location = api.getLocationForOffset(unit, file, static_cast<unsigned>(query.offset));
		CXCursor cursor = api.getCursor(unit, location);
		CXCursor declaration = api.getCursorReferenced(cursor);
		if (api.cursorIsNull(declaration) || api.getCursorKind(declaration) != CXCursor_FunctionDecl) {
			CursorSearch search{&api, file, static_cast<unsigned>(query.offset), &query.symbol, CXCursor{}, false};
			api.visitChildren(api.getTranslationUnitCursor(unit), visitSourceCursor, &search);
			if (search.found) declaration = search.declaration;
		}
		if (!api.cursorIsNull(declaration) && api.getCursorKind(declaration) == CXCursor_FunctionDecl && api.stringValue(api.getCursorSpelling(declaration)) == query.symbol) {
			std::string signature = api.stringValue(api.getTypeSpelling(api.getCursorResultType(declaration))) + " " + query.symbol + "(";
			const int argumentCount = api.getNumArguments(declaration);
			for (int i = 0; i < argumentCount; ++i) {
				if (i != 0) signature += ", ";
				CXCursor argument = api.getArgument(declaration, static_cast<unsigned>(i));
				signature += api.stringValue(api.getTypeSpelling(api.getCursorType(argument)));
				const std::string name = api.stringValue(api.getCursorSpelling(argument));
				if (!name.empty()) signature += " " + name;
			}
			if (api.isFunctionTypeVariadic(api.getCursorType(declaration))) signature += argumentCount > 0 ? ", ..." : "...";
			if (argumentCount == 0 && !api.isFunctionTypeVariadic(api.getCursorType(declaration))) signature += "void";
			signature += ");";
			entry.title = query.symbol;
			entry.text = signature;
			const std::string comment = cleanHeaderComment(api.stringValue(api.getRawCommentText(declaration)));
			if (!comment.empty()) entry.text += "\n\n" + comment;
			entry.found = true;
			found = true;
		}
	}
	api.disposeTranslationUnit(unit);
	api.disposeIndex(index);
	return found;
}
#else
bool headerReference(const MRLibraryReferenceQuery &, MRLibraryReferenceEntry &) {
	return false;
}
#endif

class MRCHeaderReferenceProvider final : public MRLibraryReferenceProvider {
  public:
	[[nodiscard]] MRLibraryReferenceEntry lookup(const MRLibraryReferenceQuery &query) const override {
		MRLibraryReferenceEntry entry;
		if (!validCSymbol(query.symbol)) return entry;
		static_cast<void>(headerReference(query, entry));
		const std::string manual = localManualDescription(query.symbol);
		if (!manual.empty()) {
			if (entry.text.empty()) entry.text = query.symbol;
			entry.text += "\n\n" + manual;
			entry.title = query.symbol;
			entry.found = true;
		}
		return entry;
	}
};

} // namespace

std::unique_ptr<MRLibraryReferenceProvider> mrLibraryReferenceProviderForLanguage(const std::string &language) {
	if (language == "C") return std::make_unique<MRCHeaderReferenceProvider>();
	return nullptr;
}
