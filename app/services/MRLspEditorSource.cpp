#include "MRLspEditorSource.hpp"

#include "../../ui/MRFileEditor/MRFileEditor.hpp"

namespace mr::services {

const char *lspLanguageIdForSyntaxLanguage(MRSyntaxLanguage language) noexcept {
	switch (language) {
		case MRSyntaxLanguage::C:
			return "c";
		case MRSyntaxLanguage::Cpp:
			return "cpp";
		case MRSyntaxLanguage::JavaScript:
			return "javascript";
		case MRSyntaxLanguage::Python:
			return "python";
		case MRSyntaxLanguage::Json:
			return "json";
		case MRSyntaxLanguage::Yaml:
			return "yaml";
		case MRSyntaxLanguage::Xml:
			return "xml";
		case MRSyntaxLanguage::Bash:
		case MRSyntaxLanguage::Zsh:
		case MRSyntaxLanguage::Fish:
			return "shellscript";
		case MRSyntaxLanguage::Perl:
			return "perl";
		case MRSyntaxLanguage::Swift:
			return "swift";
		case MRSyntaxLanguage::Rust:
			return "rust";
		case MRSyntaxLanguage::Go:
			return "go";
		case MRSyntaxLanguage::Kotlin:
			return "kotlin";
		case MRSyntaxLanguage::CSharp:
			return "csharp";
		case MRSyntaxLanguage::Pascal:
			return "pascal";
		case MRSyntaxLanguage::Systemd:
			return "systemd";
		case MRSyntaxLanguage::MRMAC:
			return "mrmac";
		case MRSyntaxLanguage::Make:
			return "makefile";
		case MRSyntaxLanguage::Markdown:
			return "markdown";
		case MRSyntaxLanguage::PlainText:
		default:
			return "plaintext";
	}
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
