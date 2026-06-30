#include "../../ui/MRFileEditor/MRFileEditor.hpp"
#include "../../ui/MRSyntax.hpp"

#include <fstream>
#include <iostream>
#include <string>

namespace {

bool parseLanguageName(const std::string &name, MRSyntaxLanguage &language) noexcept {
	if (name == "auto") language = MRSyntaxLanguage::PlainText;
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
	else if (name == "yaml" || name == "yml")
		language = MRSyntaxLanguage::Yaml;
	else if (name == "xml" || name == "xsd" || name == "xsl" || name == "xslt" || name == "svg")
		language = MRSyntaxLanguage::Xml;
	else if (name == "python" || name == "py")
		language = MRSyntaxLanguage::Python;
	else if (name == "markdown" || name == "md")
		language = MRSyntaxLanguage::Markdown;
	else if (name == "latex" || name == "tex" || name == "ltx")
		language = MRSyntaxLanguage::Latex;
	else if (name == "bash" || name == "sh")
		language = MRSyntaxLanguage::Bash;
	else if (name == "zsh")
		language = MRSyntaxLanguage::Zsh;
	else if (name == "fish")
		language = MRSyntaxLanguage::Fish;
	else if (name == "perl" || name == "pl")
		language = MRSyntaxLanguage::Perl;
	else if (name == "swift" || name == "sw")
		language = MRSyntaxLanguage::Swift;
	else if (name == "rust" || name == "rs")
		language = MRSyntaxLanguage::Rust;
	else if (name == "go")
		language = MRSyntaxLanguage::Go;
	else if (name == "kotlin" || name == "kt" || name == "kts")
		language = MRSyntaxLanguage::Kotlin;
	else if (name == "csharp" || name == "cs" || name == "c#")
		language = MRSyntaxLanguage::CSharp;
	else if (name == "pascal" || name == "pas")
		language = MRSyntaxLanguage::Pascal;
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
		case MRSyntaxLanguage::Yaml:
			return "YAML";
		case MRSyntaxLanguage::Xml:
			return "XML";
		case MRSyntaxLanguage::Bash:
			return "Bash";
		case MRSyntaxLanguage::Zsh:
			return "zsh";
		case MRSyntaxLanguage::Fish:
			return "fish";
		case MRSyntaxLanguage::Perl:
			return "Perl";
		case MRSyntaxLanguage::Swift:
			return "Swift";
		case MRSyntaxLanguage::Rust:
			return "Rust";
		case MRSyntaxLanguage::Go:
			return "Go";
		case MRSyntaxLanguage::Kotlin:
			return "Kotlin";
		case MRSyntaxLanguage::CSharp:
			return "C#";
		case MRSyntaxLanguage::Pascal:
			return "Pascal";
		case MRSyntaxLanguage::Systemd:
			return "systemd";
		case MRSyntaxLanguage::MRMAC:
			return "MRMAC";
		case MRSyntaxLanguage::Make:
			return "Make";
		case MRSyntaxLanguage::Markdown:
			return "Markdown";
		case MRSyntaxLanguage::Latex:
			return "LaTeX";
	}
	return "PlainText";
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
	if (languageOption == "auto") {
		MRSyntaxClassification classification = tmrClassifySyntaxLanguage(inputPath, inputPath, text);
		language = classification.language;
		if (language == MRSyntaxLanguage::PlainText) language = tmrDetectSyntaxLanguage(inputPath, inputPath);
	}

	std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
	if (!output) {
		std::cerr << "cannot open output: " << outputPath << "\n";
		return 1;
	}
	output << "LANGUAGE: " << languageName(language) << "\n";
	output << mrBuildOutlineTrainingAscii(text, language);
	return 0;
}
