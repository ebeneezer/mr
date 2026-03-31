#include "../mrmac/mrmac.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
int checkMacro(int index, const char *name, const char *keyspec, int mode, int flags) {
	const char *actualName = get_compiled_macro_name(index);
	const char *actualKeyspec = get_compiled_macro_keyspec(index);
	int actualMode = get_compiled_macro_mode(index);
	int actualFlags = get_compiled_macro_flags(index);

	if (actualName == NULL || std::strcmp(actualName, name) != 0) {
		std::fprintf(stderr, "Name mismatch for macro %d.\n", index);
		return 1;
	}
	if (actualKeyspec == NULL || std::strcmp(actualKeyspec, keyspec) != 0) {
		std::fprintf(stderr, "TO mismatch for macro %s.\n", name);
		return 1;
	}
	if (actualMode != mode) {
		std::fprintf(stderr, "FROM mismatch for macro %s.\n", name);
		return 1;
	}
	if (actualFlags != flags) {
		std::fprintf(stderr, "Attribute mismatch for macro %s.\n", name);
		return 1;
	}
	return 0;
}

int expectCompileError(const char *source, const char *expectedPart) {
	size_t bytecodeSize = 0;
	unsigned char *bytecode = compile_macro_code(source, &bytecodeSize);
	const char *errorText = get_last_compile_error();

	if (bytecode != NULL) {
		std::free(bytecode);
		std::fprintf(stderr, "Expected compile error but compilation succeeded.\n");
		return 1;
	}
	if (errorText == NULL || *errorText == '\0') {
		std::fprintf(stderr, "Expected compile error text, but it is empty.\n");
		return 1;
	}
	if (expectedPart != NULL && std::strstr(errorText, expectedPart) == NULL) {
		std::fprintf(stderr, "Compile error text mismatch: %s\n", errorText);
		return 1;
	}
	return 0;
}
} // namespace

int main() {
	static const char source[] =
	    "$MACRO Alpha TO <AltB> FROM EDIT TRANS;\n"
	    "END_MACRO;\n"
	    "$MACRO Beta TO <CtrlF7> FROM DOS_SHELL DUMP;\n"
	    "END_MACRO;\n"
	    "$MACRO Gamma TO <F5> FROM ALL PERM;\n"
	    "END_MACRO;\n"
	    "$MACRO ShiftTab TO <ShftTAB> FROM EDIT;\n"
	    "END_MACRO;\n"
	    "$MACRO Delta;\n"
	    "END_MACRO;\n";
	size_t bytecodeSize = 0;
	unsigned char *bytecode = compile_macro_code(source, &bytecodeSize);
	int macroCount;

	if (bytecode == NULL) {
		std::fprintf(stderr, "Compilation failed: %s\n", get_last_compile_error());
		return 1;
	}
	std::free(bytecode);

	macroCount = get_compiled_macro_count();
	if (macroCount != 5) {
		std::fprintf(stderr, "Expected 5 macros, got %d.\n", macroCount);
		return 1;
	}

	if (checkMacro(0, "Alpha", "<AltB>", MACRO_MODE_EDIT, MACRO_ATTR_TRANS) != 0)
		return 1;
	if (checkMacro(1, "Beta", "<CtrlF7>", MACRO_MODE_DOS_SHELL, MACRO_ATTR_DUMP) != 0)
		return 1;
	if (checkMacro(2, "Gamma", "<F5>", MACRO_MODE_ALL, MACRO_ATTR_PERM) != 0)
		return 1;
	if (checkMacro(3, "ShiftTab", "<ShftTAB>", MACRO_MODE_EDIT, 0) != 0)
		return 1;
	if (checkMacro(4, "Delta", "", MACRO_MODE_EDIT, 0) != 0)
		return 1;

	if (expectCompileError("$MACRO Bad TO <NoSuchKey>;\nEND_MACRO;\n", "Keycode not supported.") !=
	    0)
		return 1;
	if (expectCompileError("$MACRO Bad TO <F1> TO <F2>;\nEND_MACRO;\n", "Duplicate TO clause.") !=
	    0)
		return 1;
	if (expectCompileError("$MACRO Bad FROM EDIT FROM ALL;\nEND_MACRO;\n", "Duplicate FROM clause.") !=
	    0)
		return 1;
	if (expectCompileError("$MACRO Bad FROM INVALID;\nEND_MACRO;\n", "Mode expected.") != 0)
		return 1;

	std::puts("TO/FROM probe passed.");
	return 0;
}
