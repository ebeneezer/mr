#include <cstring>
#include <cstdint>
#include <iostream>
#include <string>

#include "../app/services/MRLspEditorSource.hpp"
#include "../ui/MRFileEditor/MRFileEditor.hpp"

namespace {
bool expect(bool condition, const std::string &name, std::string &failureReason) {
	if (condition) return true;
	failureReason = name;
	return false;
}

mr::services::MRWorkspaceDocumentSnapshot documentForEditor(const MRFileEditor &editor, const std::string &path) {
	mr::services::MRWorkspaceDocumentSnapshot document;

	document.bufferId = 10;
	document.documentId = editor.documentId();
	document.documentVersion = editor.documentVersion();
	document.path = path;
	document.languageName = editor.syntaxLanguageName();
	return document;
}

bool replaceText(MRFileEditor &editor, const std::string &text, std::string &failureReason) {
	if (editor.replaceBufferData(text.data(), static_cast<uint>(text.size()))) return true;
	failureReason = "replace buffer data failed";
	return false;
}

bool testLanguageIds(std::string &failureReason) {
	if (!expect(std::strcmp(mr::services::lspLanguageIdForSyntaxLanguage(MRSyntaxLanguage::Cpp), "cpp") == 0, "cpp language id", failureReason)) return false;
	if (!expect(std::strcmp(mr::services::lspLanguageIdForSyntaxLanguage(MRSyntaxLanguage::Python), "python") == 0, "python language id", failureReason)) return false;
	if (!expect(std::strcmp(mr::services::lspLanguageIdForSyntaxLanguage(MRSyntaxLanguage::Zsh), "shellscript") == 0, "zsh language id", failureReason)) return false;
	if (!expect(std::strcmp(mr::services::lspLanguageIdForSyntaxLanguage(MRSyntaxLanguage::CSharp), "csharp") == 0, "csharp language id", failureReason)) return false;
	if (!expect(std::strcmp(mr::services::lspLanguageIdForSyntaxLanguage(MRSyntaxLanguage::Make), "makefile") == 0, "make language id", failureReason)) return false;
	if (!expect(std::strcmp(mr::services::lspLanguageIdForSyntaxLanguage(MRSyntaxLanguage::Latex), "latex") == 0, "latex language id", failureReason)) return false;
	if (!expect(std::strcmp(mr::services::lspLanguageIdForSyntaxLanguage(MRSyntaxLanguage::PlainText), "plaintext") == 0, "plain text language id", failureReason)) return false;
	return true;
}

bool testEditorSourceHappyPath(std::string &failureReason) {
	const std::string path = "/tmp/mr/project/src/main.cpp";
	const std::string text = "int main() { return 0; }\n";
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, path.c_str());
	mr::lsp::LspDocumentSourceSnapshot source;
	std::string errorMessage;

	if (!replaceText(editor, text, failureReason)) return false;
	const mr::services::MRWorkspaceDocumentSnapshot document = documentForEditor(editor, path);
	if (!expect(mr::services::buildLspDocumentSourceSnapshotFromEditor(document, editor, source, errorMessage), "build source: " + errorMessage, failureReason)) return false;
	if (!expect(source.absolutePath == path, "source path", failureReason)) return false;
	if (!expect(source.languageId == "cpp", "source language", failureReason)) return false;
	if (!expect(source.version == static_cast<std::int64_t>(editor.documentVersion()), "source version", failureReason)) return false;
	if (!expect(source.text == text, "source text", failureReason)) return false;
	return true;
}

bool testEditorSourceGuards(std::string &failureReason) {
	const std::string path = "/tmp/mr/project/src/main.cpp";
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, path.c_str());
	mr::services::MRWorkspaceDocumentSnapshot document;
	mr::lsp::LspDocumentSourceSnapshot source;
	std::string errorMessage;

	if (!replaceText(editor, "int value = 1;\n", failureReason)) return false;
	document = documentForEditor(editor, "");
	if (!expect(!mr::services::buildLspDocumentSourceSnapshotFromEditor(document, editor, source, errorMessage), "empty path accepted", failureReason)) return false;
	document = documentForEditor(editor, path);
	document.documentId += 1;
	if (!expect(!mr::services::buildLspDocumentSourceSnapshotFromEditor(document, editor, source, errorMessage), "document id mismatch accepted", failureReason)) return false;
	document = documentForEditor(editor, path);
	document.documentVersion += 1;
	if (!expect(!mr::services::buildLspDocumentSourceSnapshotFromEditor(document, editor, source, errorMessage), "document version mismatch accepted", failureReason)) return false;
	document = documentForEditor(editor, "/tmp/mr/project/src/other.cpp");
	if (!expect(!mr::services::buildLspDocumentSourceSnapshotFromEditor(document, editor, source, errorMessage), "path mismatch accepted", failureReason)) return false;
	return true;
}

bool runProbe(std::string &failureReason) {
	if (!testLanguageIds(failureReason)) return false;
	if (!testEditorSourceHappyPath(failureReason)) return false;
	if (!testEditorSourceGuards(failureReason)) return false;
	return true;
}
} // namespace

int main() {
	std::string failureReason;

	if (!runProbe(failureReason)) {
		std::cerr << "mr_lsp_editor_source_probe failed: " << failureReason << "\n";
		return 1;
	}

	std::cout << "mr_lsp_editor_source_probe passed\n";
	return 0;
}
