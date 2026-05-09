#include "../config/MRDialogPaths.hpp"
#include "../ui/MRFileEditor/MRFileEditor.hpp"
#include "../ui/MRSyntax.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace {

struct EditSettingsGuard {
	MREditSetupSettings previous;
	bool restore = false;

	EditSettingsGuard() : previous(configuredEditSetupSettings()) {
	}

	~EditSettingsGuard() {
		if (restore) static_cast<void>(setConfiguredEditSetupSettings(previous, nullptr));
	}
};

struct DedentSimulation {
	bool changed = false;
	int beforeColumn = 0;
	int afterColumn = 0;
};

struct EnterSimulation {
	bool changed = false;
	std::string previousLine;
	std::string currentLine;
};

bool parseLanguageName(const std::string &name, MRSyntaxLanguage &language) noexcept {
	if (name == "auto")
		language = MRSyntaxLanguage::PlainText;
	else if (name == "plain" || name == "plaintext")
		language = MRSyntaxLanguage::PlainText;
	else if (name == "c")
		language = MRSyntaxLanguage::C;
	else if (name == "cpp" || name == "c++")
		language = MRSyntaxLanguage::Cpp;
	else if (name == "javascript" || name == "js" || name == "typescript" || name == "ts" || name == "tsx")
		language = MRSyntaxLanguage::JavaScript;
	else if (name == "json")
		language = MRSyntaxLanguage::Json;
	else if (name == "python" || name == "py")
		language = MRSyntaxLanguage::Python;
	else if (name == "markdown" || name == "md")
		language = MRSyntaxLanguage::Markdown;
	else if (name == "bash" || name == "sh")
		language = MRSyntaxLanguage::Bash;
	else if (name == "zsh")
		language = MRSyntaxLanguage::Zsh;
	else if (name == "perl" || name == "pl")
		language = MRSyntaxLanguage::Perl;
	else if (name == "swift" || name == "sw")
		language = MRSyntaxLanguage::Swift;
	else if (name == "rust" || name == "rs")
		language = MRSyntaxLanguage::Rust;
	else if (name == "go")
		language = MRSyntaxLanguage::Go;
	else if (name == "systemd" || name == "sd")
		language = MRSyntaxLanguage::Systemd;
	else if (name == "make" || name == "mk")
		language = MRSyntaxLanguage::Make;
	else if (name == "mrmac" || name == "mm")
		language = MRSyntaxLanguage::MRMAC;
	else
		return false;
	return true;
}

const char *languageName(MRSyntaxLanguage language) noexcept {
	switch (language) {
		case MRSyntaxLanguage::PlainText:
			return "PlainText";
		case MRSyntaxLanguage::C:
			return "C";
		case MRSyntaxLanguage::Cpp:
			return "C++";
		case MRSyntaxLanguage::JavaScript:
			return "JavaScript";
		case MRSyntaxLanguage::Python:
			return "Python";
		case MRSyntaxLanguage::Json:
			return "JSON";
		case MRSyntaxLanguage::Bash:
			return "Bash";
		case MRSyntaxLanguage::Zsh:
			return "zsh";
		case MRSyntaxLanguage::Perl:
			return "Perl";
		case MRSyntaxLanguage::Swift:
			return "Swift";
		case MRSyntaxLanguage::Rust:
			return "Rust";
		case MRSyntaxLanguage::Go:
			return "Go";
		case MRSyntaxLanguage::Systemd:
			return "systemd";
		case MRSyntaxLanguage::MRMAC:
			return "MRMAC";
		case MRSyntaxLanguage::Make:
			return "Make";
		case MRSyntaxLanguage::Markdown:
			return "Markdown";
	}
	return "PlainText";
}

std::string languageSettingName(MRSyntaxLanguage language, bool automatic) {
	if (automatic) return "AUTO";
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
		case MRSyntaxLanguage::Bash:
			return "BASH";
		case MRSyntaxLanguage::Zsh:
			return "ZSH";
		case MRSyntaxLanguage::Perl:
			return "PERL";
		case MRSyntaxLanguage::Swift:
			return "SWIFT";
		case MRSyntaxLanguage::Rust:
			return "RUST";
		case MRSyntaxLanguage::Go:
			return "GO";
		case MRSyntaxLanguage::Systemd:
			return "SYSTEMD";
		case MRSyntaxLanguage::Make:
			return "MAKE";
		case MRSyntaxLanguage::MRMAC:
			return "MRMAC";
		case MRSyntaxLanguage::Markdown:
			return "MARKDOWN";
		case MRSyntaxLanguage::PlainText:
		default:
			return "NONE";
	}
}

std::string escapeForReport(std::string_view text) {
	std::string out;
	for (char ch : text) {
		switch (ch) {
			case '\t':
				out += "\\t";
				break;
			case '\r':
				out += "\\r";
				break;
			case '\n':
				out += "\\n";
				break;
			default:
				out.push_back(ch);
				break;
		}
	}
	return out;
}

std::string_view ltrimView(std::string_view text) {
	std::size_t index = 0;
	while (index < text.size() && (text[index] == ' ' || text[index] == '\t')) ++index;
	return text.substr(index);
}

int targetColumnForFill(std::string_view fill, const MREditSetupSettings &settings) {
	int column = 1;
	for (char ch : fill) {
		if (ch == '\t')
			column = resolvedEditFormatTabDisplayColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, column);
		else
			++column;
	}
	return column;
}

std::unique_ptr<MRFileEditor> buildEditor(const std::string &text, const std::string &inputPath, const std::string &codeLanguage) {
	TRect bounds(0, 0, 1, 1);
	auto editor = std::make_unique<MRFileEditor>(bounds, nullptr, nullptr, nullptr, TStringView());
	editor->replaceWholeBuffer(text, 0);
	editor->setPersistentFileName(inputPath);
	if (!codeLanguage.empty()) editor->bufferModel().setSyntaxContext(inputPath, inputPath, codeLanguage);
	return editor;
}

DedentSimulation simulateLiveDedent(const std::string &text, const std::string &inputPath, const std::string &codeLanguage, std::size_t lineIndex, const MREditSetupSettings &settings) {
	DedentSimulation result;
	auto editor = buildEditor(text, inputPath, codeLanguage);
	const std::size_t lineStart = editor->bufferModel().lineStartByIndex(lineIndex);
	const std::string originalLine = editor->bufferModel().lineText(lineStart);
	const std::string_view trimmed = ltrimView(originalLine);
	if (trimmed.empty()) return result;

	const int actualColumn = editor->leadingIndentColumnForLine(lineStart);
	int indentStep = 0;
	std::size_t currentLineStart = lineStart;
	while (currentLineStart > 0) {
		const std::size_t previousLineStart = editor->bufferModel().prevLine(currentLineStart);
		if (previousLineStart == currentLineStart) break;
		currentLineStart = previousLineStart;

		const std::string previousLineText = editor->bufferModel().lineText(previousLineStart);
		if (ltrimView(previousLineText).empty()) continue;

		const int previousColumn = editor->leadingIndentColumnForLine(previousLineStart);
		if (previousColumn < actualColumn) {
			indentStep = actualColumn - previousColumn;
			break;
		}
	}
	if (indentStep <= 0) {
		if (settings.tabExpand)
			indentStep = 4;
		else {
			const int nextColumn = nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, actualColumn);
			indentStep = std::max(1, nextColumn - actualColumn);
		}
	}
	const int deeperColumn = actualColumn + indentStep;
	if (deeperColumn <= actualColumn) return result;

	const std::string simulatedLine = buildEditIndentFill(settings, 1, deeperColumn, settings.tabExpand) + std::string(trimmed);
	editor->setCursorOffset(lineStart);
	if (!editor->replaceCurrentLineText(simulatedLine)) return result;

	const std::size_t currentStart = editor->bufferModel().lineStartByIndex(lineIndex);
	editor->setCursorOffset(editor->lineEndOffset(currentStart));
	result.beforeColumn = editor->leadingIndentColumnForLine(currentStart);
	editor->applyLiveSmartDedentAfterTextInput("x");
	result.afterColumn = editor->leadingIndentColumnForLine(currentStart);
	result.changed = result.afterColumn != result.beforeColumn;
	return result;
}

EnterSimulation simulateTrailingCloserEnter(const std::string &text, const std::string &inputPath, const std::string &codeLanguage, std::size_t lineIndex) {
	EnterSimulation result;
	auto editor = buildEditor(text, inputPath, codeLanguage);
	const std::size_t originalLineStart = editor->bufferModel().lineStartByIndex(lineIndex);
	const std::string originalLine = editor->bufferModel().lineText(originalLineStart);
	const std::size_t lineEnd = editor->lineEndOffset(originalLineStart);

	editor->setCursorOffset(lineEnd);
	if (!editor->newLineWithPreferredIndent()) return result;

	const std::size_t updatedLineStart = editor->bufferModel().lineStartByIndex(lineIndex);
	const std::string updatedLine = editor->bufferModel().lineText(updatedLineStart);
	if (updatedLine == originalLine) return result;

	result.changed = true;
	result.previousLine = updatedLine;
	if (lineIndex + 1 < editor->bufferModel().lineCount()) {
		const std::size_t nextLineStart = editor->bufferModel().lineStartByIndex(lineIndex + 1);
		result.currentLine = editor->bufferModel().lineText(nextLineStart);
	}
	return result;
}

int usage(const char *argv0) {
	std::cerr << "Usage: " << argv0 << " [--language=<name>|auto] <input> <output>\n";
	return 2;
}

} // namespace

int main(int argc, char **argv) {
	std::string languageOption = "auto";
	std::string inputPath;
	std::string outputPath;

	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg.starts_with("--language="))
			languageOption = arg.substr(std::string("--language=").size());
		else if (inputPath.empty())
			inputPath = arg;
		else if (outputPath.empty())
			outputPath = arg;
		else
			return usage(argv[0]);
	}
	if (inputPath.empty() || outputPath.empty()) return usage(argv[0]);

	std::ifstream input(inputPath, std::ios::binary);
	if (!input) {
		std::cerr << "cannot open input: " << inputPath << "\n";
		return 1;
	}
	std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	MRSyntaxLanguage language = MRSyntaxLanguage::PlainText;
	if (!parseLanguageName(languageOption, language)) {
		std::cerr << "unknown language: " << languageOption << "\n";
		return 2;
	}
	const bool automaticLanguage = languageOption == "auto";
	if (automaticLanguage) {
		MRSyntaxClassification classification = tmrClassifySyntaxLanguage(inputPath, inputPath, text);
		language = classification.language;
		if (language == MRSyntaxLanguage::PlainText) language = tmrDetectSyntaxLanguage(inputPath, inputPath);
	}

	EditSettingsGuard guard;
	MREditSetupSettings settings = guard.previous;
	settings.smartIndenting = true;
	settings.indentStyle = "SMART";
	settings.codeLanguage = languageSettingName(language, automaticLanguage);
	std::string errorText;
	if (!setConfiguredEditSetupSettings(settings, &errorText)) {
		std::cerr << "cannot configure edit settings: " << errorText << "\n";
		return 1;
	}
	guard.restore = true;

	auto editor = buildEditor(text, inputPath, settings.codeLanguage);
	std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
	if (!output) {
		std::cerr << "cannot open output: " << outputPath << "\n";
		return 1;
	}

	output << "LANGUAGE: " << languageName(editor->syntaxLanguage()) << "\n";
	output << "INDENT_STYLE: " << settings.indentStyle << "\n";
	output << "TAB_SIZE: " << settings.tabSize << "\n";
	output << "TAB_EXPAND: " << (settings.tabExpand ? "ON" : "OFF") << "\n";

	const std::size_t lineCount = editor->bufferModel().lineCount();
	for (std::size_t lineIndex = 0; lineIndex < lineCount; ++lineIndex) {
		const std::size_t lineStart = editor->bufferModel().lineStartByIndex(lineIndex);
		const std::size_t lineEnd = editor->lineEndOffset(lineStart);
		const std::string lineText = editor->bufferModel().lineText(lineStart);
		editor->setCursorOffset(lineEnd);

		const std::string autoFill = editor->automaticIndentFillForCursor();
		const std::string smartFill = editor->smartIndentFillForCursor();
		const DedentSimulation dedent = simulateLiveDedent(text, inputPath, settings.codeLanguage, lineIndex, settings);
		const EnterSimulation enter = simulateTrailingCloserEnter(text, inputPath, settings.codeLanguage, lineIndex);
		const int leadColumn = editor->leadingIndentColumnForLine(lineStart);
		const int autoColumn = targetColumnForFill(autoFill, settings);
		const int smartColumn = targetColumnForFill(smartFill, settings);

		output << std::setw(5) << (lineIndex + 1) << " lead=" << leadColumn << " auto=" << autoColumn << "(\"" << escapeForReport(autoFill) << "\")"
		       << " smart=" << smartColumn << "(\"" << escapeForReport(smartFill) << "\")";
		if (dedent.changed)
			output << " dedent=" << dedent.beforeColumn << "->" << dedent.afterColumn;
		else
			output << " dedent=-";
		if (enter.changed)
			output << " enter=prev:'" << escapeForReport(enter.previousLine) << "' next:'" << escapeForReport(enter.currentLine) << "'";
		else
			output << " enter=-";
		output << " text=" << escapeForReport(lineText) << "\n";
	}
	return 0;
}
