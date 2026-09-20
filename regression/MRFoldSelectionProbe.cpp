#include "../ui/MRFileEditor/MRFileEditorFoldAnalysis.hpp"

#include <algorithm>
#include <iostream>

using namespace mr::editor::fold;

int main() {
	struct SelectionProbe {
		const char *name;
		MRSyntaxLanguage language;
		const char *text;
		std::vector<MRFoldSpan> expected;
	};
	const MRFoldSourceKind kind = MRFoldSourceKind::Generic;
	const std::vector<MRFoldSpan> simple = {{0, 5, 0, kind}, {1, 2, 1, kind}, {3, 4, 1, kind}};
	const std::vector<MRFoldSpan> braced = {{0, 7, 0, kind}, {1, 3, 1, kind}, {4, 6, 1, kind}};
	const SelectionProbe probes[] = {
		{"C", MRSyntaxLanguage::C, "switch (x) {\ncase '=':\n return 1;\ndefault:\n return 0;\n}", simple},
		{"C++", MRSyntaxLanguage::Cpp, "switch(x) {\ncase A::x:\n return 1;\ndefault:\n return 0;\n}", simple},
		{"JavaScript", MRSyntaxLanguage::JavaScript, "switch (x) {\ncase 'x':\n f();\ndefault:\n g();\n}", simple},
		{"C#", MRSyntaxLanguage::CSharp, "switch (x) {\ncase int n when n > 0:\n return n;\ndefault:\n return 0;\n}", simple},
		{"C# expression", MRSyntaxLanguage::CSharp, "var y = x switch {\n 1 =>\n  f(),\n _ =>\n  g(),\n};", simple},
		{"Swift", MRSyntaxLanguage::Swift, "switch x {\ncase .some(let y):\n f(y)\ndefault:\n g()\n}", simple},
		{"Go switch", MRSyntaxLanguage::Go, "switch x {\ncase 1, 2:\n f()\ndefault:\n g()\n}", simple},
		{"Go select", MRSyntaxLanguage::Go, "select {\ncase x := <-ch:\n f(x)\ndefault:\n g()\n}", simple},
		{"Rust", MRSyntaxLanguage::Rust, "match x {\n Some(x) =>\n  f(x),\n None =>\n  g(),\n}", simple},
		{"Kotlin", MRSyntaxLanguage::Kotlin, "when (x) {\n 1 ->\n  f()\n else ->\n  g()\n}", simple},
		{"Python", MRSyntaxLanguage::Python, "match x:\n case 1:\n  f()\n case _:\n  g()\nh()", {{0, 4, 0, kind}, {1, 2, 1, kind}, {3, 4, 1, kind}}},
		{"Bash", MRSyntaxLanguage::Bash, "case x in\n x)\n  f\n ;;\n *)\n  g\n ;;\nesac", braced},
		{"Zsh", MRSyntaxLanguage::Zsh, "case x in\n x)\n  f\n ;&\n *)\n  g\n ;;\nesac", braced},
		{"Fish", MRSyntaxLanguage::Fish, "switch x\n case x\n  f\n case '*'\n  g\nend", simple},
		{"Pascal", MRSyntaxLanguage::Pascal, "case x of\n 1:\n  f;\n else\n  g;\nend;", simple},
		{"BASIC", MRSyntaxLanguage::Basic, "SELECT CASE x\nCASE 1\n PRINT 1\nCASE ELSE\n PRINT 0\nEND SELECT", simple},
		{"Perl", MRSyntaxLanguage::Perl, "given ($x) {\n when (1) {\n  f();\n }\n default {\n  g();\n }\n}", braced},
		{"explicit case blocks", MRSyntaxLanguage::Cpp, "switch(x) {\ncase 1: {\n f();\n}\ndefault: {\n g();\n}\n}", braced},
		{"Pascal case blocks", MRSyntaxLanguage::Pascal, "case x of\n 1: begin\n  f;\n end;\n else begin\n  g;\n end;\nend;", braced},
		{"stacked labels", MRSyntaxLanguage::C, "switch(x) {\ncase 1:\ncase 2:\n f();\ndefault:\n g();\n}", {{0, 6, 0, kind}, {1, 3, 1, kind}, {4, 5, 1, kind}}},
		{"nested switch", MRSyntaxLanguage::C, "switch(x) {\ncase 1:\n switch(y) {\n case 2:\n  f();\n default:\n  g();\n }\n break;\ndefault:\n h();\n}",
		 {{0, 11, 0, kind}, {1, 8, 1, kind}, {2, 7, 2, kind}, {3, 4, 3, kind}, {5, 6, 3, kind}, {9, 10, 1, kind}}},
		{"comments and strings", MRSyntaxLanguage::JavaScript, "switch(x) {\ncase 1:\n /* default:\n } */\n f('case 2: {');\ndefault:\n g();\n}",
		 {{0, 7, 0, kind}, {1, 4, 1, kind}, {5, 6, 1, kind}}},
		{"multiline selector", MRSyntaxLanguage::Cpp, "switch (\n x\n)\n{\ncase 1:\n f();\ndefault:\n g();\n}",
		 {{0, 8, 0, kind}, {4, 5, 1, kind}, {6, 7, 1, kind}}},
		{"multiline case", MRSyntaxLanguage::Cpp, "switch(x) {\ncase\n 1:\n f();\ndefault:\n g();\n}", {{0, 6, 0, kind}, {1, 3, 1, kind}, {4, 5, 1, kind}}},
		{"inline statements", MRSyntaxLanguage::C, "switch(x) {\ncase 1: f();\ncase 2:\n g();\n}", {{0, 4, 0, kind}, {2, 3, 1, kind}}},
		{"C++ init statement", MRSyntaxLanguage::Cpp, "switch (int x = f(); x) {\ncase 1:\n f();\ndefault:\n g();\n}", simple},
		{"parenthesized shell patterns", MRSyntaxLanguage::Bash, "case x in\n (x)\n  f\n ;;\n (*)\n  g\n ;;\nesac", braced},
		{"Rust multiline pattern", MRSyntaxLanguage::Rust, "match x {\n Some(\n  x\n ) =>\n  f(x),\n None =>\n  g(),\n}", {{0, 7, 0, kind}, {1, 4, 1, kind}, {5, 6, 1, kind}}},
		{"Kotlin multiline pattern", MRSyntaxLanguage::Kotlin, "when (x) {\n 1,\n 2 ->\n  f()\n else ->\n  g()\n}", {{0, 6, 0, kind}, {1, 3, 1, kind}, {4, 5, 1, kind}}},
		{"ordinary child block", MRSyntaxLanguage::Cpp, "switch(x) {\ncase 1:\n if (a) {\n  f();\n }\n break;\ndefault:\n g();\n}", {{0, 8, 0, kind}, {1, 5, 1, kind}, {2, 4, 2, kind}, {6, 7, 1, kind}}},

		{"Rust multiline struct pattern", MRSyntaxLanguage::Rust, "match x {\n Foo {\n  field,\n } => {\n  f(field)\n }\n _ => {\n  g()\n }\n}", {{0, 9, 0, kind}, {1, 5, 1, kind}, {6, 8, 1, kind}}},
		{"nested Python match", MRSyntaxLanguage::Python, "match x:\n case 1:\n  match y:\n   case 2:\n    f()\n   case _:\n    g()\n case _:\n  h()\nk()", {{0, 8, 0, kind}, {1, 6, 1, kind}, {2, 6, 2, kind}, {3, 4, 3, kind}, {5, 6, 3, kind}, {7, 8, 1, kind}}},
		{"Bash quoted body", MRSyntaxLanguage::Bash, "case x in\n x)\n  echo \"done\"\n ;;\n *)\n  echo \"other\"\n ;;\nesac", braced},
		{"incomplete selector", MRSyntaxLanguage::C, "switch(x) {\ncase 1:\n f();", {}},

	};
	int failures = 0;
	for (const SelectionProbe &probe : probes) {
		const std::vector<std::string> lines = splitFoldTrainingLines(probe.text);
		for (std::size_t chunk = 1; chunk <= lines.size(); ++chunk) {
			MRFoldAnalysisState state;
			std::vector<MRFoldSpan> spans;
			for (std::size_t first = 0; first < lines.size(); first += chunk) {
				const std::size_t last = std::min(first + chunk, lines.size());
				const std::vector<std::string> part(lines.begin() + first, lines.begin() + last);
				const MRFoldScanOutput output = computeFoldSpansForLineTexts(part, part.size(), first, 0, lines.size(), probe.language, {}, state, last == lines.size());
				state = output.stateOut;
				spans.insert(spans.end(), output.spans.begin(), output.spans.end());
			}
			bool matches = spans.size() == probe.expected.size();
			for (const MRFoldSpan &expected : probe.expected) {
				bool found = false;
				for (const MRFoldSpan &span : spans)
					if (span.startLine == expected.startLine && span.endLine == expected.endLine && span.level == expected.level) found = true;
				if (!found) matches = false;
			}
			if (!matches) {
				std::cerr << probe.name << ": chunk " << chunk << ":";
				for (const MRFoldSpan &span : spans) std::cerr << " [" << span.startLine << ',' << span.endLine << ',' << span.level << ']';
				std::cerr << '\n';
				++failures;
				break;
			}
		}
	}
	const MRSyntaxLanguage continuationLanguages[] = {MRSyntaxLanguage::Bash, MRSyntaxLanguage::Fish, MRSyntaxLanguage::Perl,
	    MRSyntaxLanguage::JavaScript, MRSyntaxLanguage::Swift, MRSyntaxLanguage::Rust, MRSyntaxLanguage::Go};
	for (MRSyntaxLanguage language : continuationLanguages) {
		const char quote = language == MRSyntaxLanguage::JavaScript ? '`' : '"';
		MRSyntaxLineState input;
		input.mode = MRSyntaxMode::QuotedString;
		input.payload = static_cast<std::uint32_t>(quote);
		const MRSyntaxLineResult closed = tmrHighlightTextLine(language, std::string("text") + quote, input);
		const MRSyntaxLineResult escaped = tmrHighlightTextLine(language, std::string("text\\") + quote, input);
		if (closed.stateOut.mode == MRSyntaxMode::QuotedString || escaped.stateOut.mode != MRSyntaxMode::QuotedString) {
			std::cerr << "String continuation failed for language " << static_cast<int>(language) << '\n';
			++failures;
		}
	}
	for (MRSyntaxLanguage language : {MRSyntaxLanguage::Bash, MRSyntaxLanguage::Fish, MRSyntaxLanguage::Perl}) {
		const MRSyntaxLineResult closed = tmrHighlightTextLine(language, "print \"done\"");
		const MRSyntaxLineResult open = tmrHighlightTextLine(language, "print \"done\" \"open");
		const MRSyntaxLineResult reopened = tmrHighlightTextLine(language, "closed\" \"open", open.stateOut);
		if (closed.stateOut.mode == MRSyntaxMode::QuotedString || open.stateOut.mode != MRSyntaxMode::QuotedString || reopened.stateOut.mode != MRSyntaxMode::QuotedString) {
			std::cerr << "String opening failed for language " << static_cast<int>(language) << '\n';
			++failures;
		}
	}
	const std::string basicGutter = mrBuildFoldTrainingAscii("SELECT CASE x\nCASE 1\n PRINT 1\nCASE ELSE\n PRINT 0\nEND SELECT", MRSyntaxLanguage::Basic);
	if (basicGutter.find("╭  | SELECT CASE x") == std::string::npos || basicGutter.find("│╭ | CASE 1") == std::string::npos ||
	    basicGutter.find("│╭ | CASE ELSE") == std::string::npos || basicGutter.find("╰  | END SELECT") == std::string::npos) {
		std::cerr << "BASIC gutter projection failed\n" << basicGutter;
		++failures;
	}
	if (failures != 0) return 1;
	std::cout << "PASS: " << sizeof(probes) / sizeof(probes[0]) << " selection scenarios, every chunk size\n";
	return 0;
}
