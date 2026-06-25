#include "../MRSyntax.hpp"

const char *tmrSyntaxLanguageName(MRSyntaxLanguage language) noexcept {
	switch (language) {
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
		default:
			return "Plain Text";
	}
}

const char *tmrSyntaxLanguageMarker(MRSyntaxLanguage language) noexcept {
	switch (language) {
		case MRSyntaxLanguage::C:
			return "C";
		case MRSyntaxLanguage::Cpp:
			return "C++";
		case MRSyntaxLanguage::JavaScript:
			return "JS";
		case MRSyntaxLanguage::Python:
			return "Py";
		case MRSyntaxLanguage::Json:
			return "Jn";
		case MRSyntaxLanguage::Yaml:
			return "Ya";
		case MRSyntaxLanguage::Xml:
			return "Xm";
		case MRSyntaxLanguage::Bash:
			return "Ba";
		case MRSyntaxLanguage::Zsh:
			return "Zh";
		case MRSyntaxLanguage::Fish:
			return "Fi";
		case MRSyntaxLanguage::Perl:
			return "Pl";
		case MRSyntaxLanguage::Swift:
			return "Sw";
		case MRSyntaxLanguage::Rust:
			return "Rs";
		case MRSyntaxLanguage::Go:
			return "Go";
		case MRSyntaxLanguage::Kotlin:
			return "Kt";
		case MRSyntaxLanguage::CSharp:
			return "C#";
		case MRSyntaxLanguage::Pascal:
			return "Pa";
		case MRSyntaxLanguage::Systemd:
			return "Sd";
		case MRSyntaxLanguage::MRMAC:
			return "MR";
		case MRSyntaxLanguage::Make:
			return "MK";
		case MRSyntaxLanguage::Markdown:
			return "MD";
		case MRSyntaxLanguage::Latex:
			return "TeX";
		default:
			return "";
	}
}

std::uint32_t tmrSyntaxLanguageMarkerRgb(MRSyntaxLanguage language) noexcept {
	switch (language) {
		case MRSyntaxLanguage::C:
			return 0x7DB7E8;
		case MRSyntaxLanguage::Cpp:
			return 0x5C9DED;
		case MRSyntaxLanguage::JavaScript:
			return 0xD9A400;
		case MRSyntaxLanguage::Python:
			return 0x4AA3D8;
		case MRSyntaxLanguage::Json:
			return 0x9FB3C8;
		case MRSyntaxLanguage::Yaml:
			return 0x78B883;
		case MRSyntaxLanguage::Xml:
			return 0xD08A6A;
		case MRSyntaxLanguage::Bash:
			return 0x8FBF6A;
		case MRSyntaxLanguage::Zsh:
			return 0x6FBF73;
		case MRSyntaxLanguage::Fish:
			return 0x5FBF9A;
		case MRSyntaxLanguage::Perl:
			return 0xB084CC;
		case MRSyntaxLanguage::Swift:
			return 0xE58F65;
		case MRSyntaxLanguage::Rust:
			return 0xDEA584;
		case MRSyntaxLanguage::Go:
			return 0x6AA8FF;
		case MRSyntaxLanguage::Kotlin:
			return 0xB58CFF;
		case MRSyntaxLanguage::CSharp:
			return 0x8BC34A;
		case MRSyntaxLanguage::Pascal:
			return 0xD49A57;
		case MRSyntaxLanguage::Systemd:
			return 0xB0B87A;
		case MRSyntaxLanguage::MRMAC:
			return 0xE58F65;
		case MRSyntaxLanguage::Make:
			return 0x8FA8B6;
		case MRSyntaxLanguage::Markdown:
			return 0xC0A060;
		case MRSyntaxLanguage::Latex:
			return 0x6CB7A8;
		default:
			return 0;
	}
}
