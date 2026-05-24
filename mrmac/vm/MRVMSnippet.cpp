#include "MRVMSnippet.hpp"

#include "MRVMHash.hpp"
#include "../mrmac.h"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/MRFileEditor/MRFileEditor.hpp"
#include "../../ui/MRSidekickEditor.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace {
using Value = VirtualMachine::Value;

struct MacroSnippetStartData {
	bool found = false;
	std::size_t start = 0;
	std::size_t end = 0;
	std::string key;
};

struct PreparedSnippetText {
	std::string body;
	std::vector<MRSidekickSpan> placeholders;
};

static std::string snippetUpperKey(const std::string &value) {
	std::string out = value;
	for (char &i : out)
		i = static_cast<char>(std::toupper(static_cast<unsigned char>(i)));
	return out;
}

static std::string snippetCharToString(unsigned char c) {
	if (c == 0) return std::string();
	return std::string(1, static_cast<char>(c));
}

static bool snippetIsStringLike(const Value &value) {
	return value.type == TYPE_STR || value.type == TYPE_CHAR;
}

static std::string snippetValueAsString(const Value &value) {
	if (value.type == TYPE_STR) return value.s;
	if (value.type == TYPE_CHAR) return snippetCharToString(value.c);
	return std::string();
}

static bool isSnippetWordChar(char ch) noexcept {
	const unsigned char uch = static_cast<unsigned char>(ch);
	return std::isalnum(uch) != 0 || ch == '_';
}

static MacroSnippetStartData findSnippetTriggerAtCursor(MRFileEditor *editor) {
	MacroSnippetStartData result;
	std::size_t cursor = 0;
	std::size_t lineStart = 0;
	std::size_t lineEnd = 0;
	std::size_t probe = 0;
	std::size_t rightProbe = 0;

	if (editor == nullptr) return result;
	cursor = editor->cursorOffset();
	lineStart = editor->lineStartOffset(cursor);
	lineEnd = editor->lineEndOffset(cursor);
	if (cursor < lineEnd && isSnippetWordChar(editor->charAtOffset(cursor))) {
		probe = cursor;
	} else if (cursor > lineStart && isSnippetWordChar(editor->charAtOffset(cursor - 1))) {
		probe = cursor - 1;
	} else {
		rightProbe = cursor;
		while (rightProbe < lineEnd && std::isspace(static_cast<unsigned char>(editor->charAtOffset(rightProbe))) != 0)
			++rightProbe;
		if (rightProbe < lineEnd && isSnippetWordChar(editor->charAtOffset(rightProbe)))
			probe = rightProbe;
		else
			return result;
	}
	result.start = probe;
	while (result.start > lineStart && isSnippetWordChar(editor->charAtOffset(result.start - 1)))
		--result.start;
	result.end = probe + 1;
	while (result.end < lineEnd && isSnippetWordChar(editor->charAtOffset(result.end)))
		++result.end;
	if (result.end <= result.start) return result;
	for (std::size_t pos = result.start; pos < result.end; ++pos)
		result.key.push_back(editor->charAtOffset(pos));
	result.key = snippetUpperKey(result.key);
	result.found = !result.key.empty();
	return result;
}

static std::string snippetLanguageKey(MRSyntaxLanguage language) {
	switch (language) {
		case MRSyntaxLanguage::C:
			return "C";
		case MRSyntaxLanguage::Cpp:
			return "CPP";
		case MRSyntaxLanguage::JavaScript:
			return "JAVASCRIPT";
		case MRSyntaxLanguage::Python:
			return "PYTHON";
		case MRSyntaxLanguage::Json:
			return "JSON";
		case MRSyntaxLanguage::Yaml:
			return "YAML";
		case MRSyntaxLanguage::Xml:
			return "XML";
		case MRSyntaxLanguage::Bash:
			return "BASH";
		case MRSyntaxLanguage::Zsh:
			return "ZSH";
		case MRSyntaxLanguage::Fish:
			return "FISH";
		case MRSyntaxLanguage::Perl:
			return "PERL";
		case MRSyntaxLanguage::Swift:
			return "SWIFT";
		case MRSyntaxLanguage::Rust:
			return "RUST";
		case MRSyntaxLanguage::Go:
			return "GO";
		case MRSyntaxLanguage::Kotlin:
			return "KOTLIN";
		case MRSyntaxLanguage::CSharp:
			return "CSHARP";
		case MRSyntaxLanguage::Pascal:
			return "PASCAL";
		case MRSyntaxLanguage::Systemd:
			return "SYSTEMD";
		case MRSyntaxLanguage::MRMAC:
			return "MRMAC";
		case MRSyntaxLanguage::Make:
			return "MAKE";
		case MRSyntaxLanguage::Markdown:
			return "MARKDOWN";
		case MRSyntaxLanguage::PlainText:
		default:
			return std::string();
	}
}

static bool readHashStringCase(const MRVMHashStore &localStore, const MRVMHashStore &globalStore, const Value &hash, const char *lowerKey, std::string &out) {
	Value value;

	if (hash.type != TYPE_HASH) return false;
	if (mrvmHashContainsValue(localStore, globalStore, hash, lowerKey))
		value = mrvmHashReadValue(localStore, globalStore, hash, lowerKey);
	else {
		const std::string upper = snippetUpperKey(lowerKey);
		if (!mrvmHashContainsValue(localStore, globalStore, hash, upper)) return false;
		value = mrvmHashReadValue(localStore, globalStore, hash, upper);
	}
	if (!snippetIsStringLike(value)) return false;
	out = snippetValueAsString(value);
	return true;
}

static PreparedSnippetText expandSnippetDefaults(const MRVMHashStore &localStore, const MRVMHashStore &globalStore, const Value &snippetHash, std::string body) {
	Value placeholders;
	PreparedSnippetText prepared{body, {}};

	if (snippetHash.type != TYPE_HASH) return prepared;
	if (mrvmHashContainsValue(localStore, globalStore, snippetHash, "placeholders"))
		placeholders = mrvmHashReadValue(localStore, globalStore, snippetHash, "placeholders");
	else if (mrvmHashContainsValue(localStore, globalStore, snippetHash, "PLACEHOLDERS"))
		placeholders = mrvmHashReadValue(localStore, globalStore, snippetHash, "PLACEHOLDERS");
	else
		return prepared;
	if (placeholders.type != TYPE_HASH) return prepared;
	for (const std::string &key : mrvmHashRuntimeStoreForValue(localStore, globalStore, placeholders).keys(placeholders.hashHandle)) {
		Value placeholder = mrvmHashReadValue(localStore, globalStore, placeholders, key);
		std::string defaultText;
		const std::string marker = "//" + key;
		std::size_t pos = 0;

		if (placeholder.type != TYPE_HASH) continue;
		if (!readHashStringCase(localStore, globalStore, placeholder, "default", defaultText)) continue;
		while ((pos = prepared.body.find(marker, pos)) != std::string::npos) {
			prepared.body.replace(pos, marker.size(), defaultText);
			prepared.placeholders.push_back(MRSidekickSpan{pos, pos + defaultText.size()});
			pos += defaultText.size();
		}
	}
	std::sort(prepared.placeholders.begin(), prepared.placeholders.end(), [](const MRSidekickSpan &a, const MRSidekickSpan &b) { return a.start < b.start; });
	return prepared;
}
} // namespace

bool mrvmSnippetOpenSidekickFromActiveEditor(MREditWindow *win, MRVMHashStore &localStore, MRVMHashStore &globalStore, const Value &snippetRootHash) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	MacroSnippetStartData trigger;
	Value languageHash;
	Value snippetHash;
	std::string languageKey;
	std::string body;
	std::string title;
	PreparedSnippetText prepared;

	if (snippetRootHash.type != TYPE_HASH) return false;
	if (win == nullptr || editor == nullptr || editor->isReadOnly()) return false;
	trigger = findSnippetTriggerAtCursor(editor);
	if (!trigger.found) return false;
	languageKey = snippetLanguageKey(win->syntaxLanguage());
	if (languageKey.empty()) return false;
	if (!mrvmHashContainsValue(localStore, globalStore, snippetRootHash, languageKey)) return false;
	languageHash = mrvmHashReadValue(localStore, globalStore, snippetRootHash, languageKey);
	if (languageHash.type != TYPE_HASH || !mrvmHashContainsValue(localStore, globalStore, languageHash, trigger.key)) return false;
	snippetHash = mrvmHashReadValue(localStore, globalStore, languageHash, trigger.key);
	if (snippetHash.type != TYPE_HASH) return false;
	if (!readHashStringCase(localStore, globalStore, snippetHash, "body", body)) return false;
	if (!readHashStringCase(localStore, globalStore, snippetHash, "name", title)) title = trigger.key;
	prepared = expandSnippetDefaults(localStore, globalStore, snippetHash, body);
	return mrOpenSnippetSidekick(win, trigger.start, trigger.end, prepared.body, title, prepared.placeholders);
}

void mrvmSnippetUnloadLanguage(MRVMHashStore &localStore, MRVMHashStore &globalStore, const Value &snippetRootHash, const std::string &language) {
	if (snippetRootHash.type != TYPE_HASH) return;
	mrvmHashEraseValue(localStore, globalStore, snippetRootHash, snippetUpperKey(language));
}
