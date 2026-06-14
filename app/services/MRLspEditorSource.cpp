#include "MRLspEditorSource.hpp"

#include "../../ui/MRFileEditor/MRFileEditor.hpp"

#include <cstddef>

namespace {

struct LspLanguageIdEntry {
	MRSyntaxLanguage language;
	const char *id;
};

const LspLanguageIdEntry lspLanguageIdTable[] = {
	{ MRSyntaxLanguage::C, "c" },
	{ MRSyntaxLanguage::Cpp, "cpp" },
	{ MRSyntaxLanguage::JavaScript, "javascript" },
	{ MRSyntaxLanguage::Python, "python" },
	{ MRSyntaxLanguage::Json, "json" },
	{ MRSyntaxLanguage::Yaml, "yaml" },
	{ MRSyntaxLanguage::Xml, "xml" },
	{ MRSyntaxLanguage::Bash, "shellscript" },
	{ MRSyntaxLanguage::Zsh, "shellscript" },
	{ MRSyntaxLanguage::Fish, "shellscript" },
	{ MRSyntaxLanguage::Perl, "perl" },
	{ MRSyntaxLanguage::Swift, "swift" },
	{ MRSyntaxLanguage::Rust, "rust" },
	{ MRSyntaxLanguage::Go, "go" },
	{ MRSyntaxLanguage::Kotlin, "kotlin" },
	{ MRSyntaxLanguage::CSharp, "csharp" },
	{ MRSyntaxLanguage::Pascal, "pascal" },
	{ MRSyntaxLanguage::Systemd, "systemd" },
	{ MRSyntaxLanguage::MRMAC, "mrmac" },
	{ MRSyntaxLanguage::Make, "makefile" },
	{ MRSyntaxLanguage::Markdown, "markdown" },
	{ MRSyntaxLanguage::PlainText, "plaintext" },
};

} // namespace

namespace mr::services {

const char *lspLanguageIdForSyntaxLanguage(MRSyntaxLanguage language) noexcept {
	const std::size_t entryCount = sizeof(lspLanguageIdTable) / sizeof(lspLanguageIdTable[0]);

	for (std::size_t index = 0; index < entryCount; ++index) {
		if (lspLanguageIdTable[index].language == language)
			return lspLanguageIdTable[index].id;
	}
	return "plaintext";
}

bool buildLspDocumentSourceSnapshotFromEditor(const MRWorkspaceDocumentSnapshot &document, const MRFileEditor &editor, mr::lsp::LspDocumentSourceSnapshot &snapshot, std::string &errorMessage) {
	const std::string path = normalizeWorkspaceServicePath(document.path);
	const std::size_t versionBefore = editor.documentVersion();
	std::string text;

	snapshot = mr::lsp::LspDocumentSourceSnapshot();
	if (path.empty()) {
		errorMessage = "LSP editor source document path is empty.";
		return false;
	}
	if (document.documentId == 0 || document.documentId != editor.documentId()) {
		errorMessage = "LSP editor source document id does not match editor state.";
		return false;
	}
	if (document.documentVersion == 0 || document.documentVersion != versionBefore) {
		errorMessage = "LSP editor source document version does not match editor state.";
		return false;
	}
	if (editor.hasPersistentFileName() && path != normalizeWorkspaceServicePath(editor.persistentFileName())) {
		errorMessage = "LSP editor source path does not match editor state.";
		return false;
	}

	text = editor.snapshotText();
	const std::size_t versionAfter = editor.documentVersion();
	if (versionBefore != versionAfter || document.documentVersion != versionAfter) {
		errorMessage = "LSP editor source changed while snapshot was being built.";
		return false;
	}

	snapshot.absolutePath = path;
	snapshot.languageId = lspLanguageIdForSyntaxLanguage(editor.syntaxLanguage());
	snapshot.version = static_cast<std::int64_t>(document.documentVersion);
	snapshot.text = text;
	errorMessage.clear();
	return true;
}

} // namespace mr::services
