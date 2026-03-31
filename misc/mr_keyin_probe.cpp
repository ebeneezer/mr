#define Uses_TKeys
#include <tvision/tv.h>

#include "../mrmac/mrmac.h"
#include "../mrmac/mrvm.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {
bool writeProbeMacroFile(const char *path) {
	std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	out << "$MACRO KeyOnCtrlP TO <CtrlP> FROM EDIT;\n";
	out << "KEY_IN('X<Enter>Y');\n";
	out << "SET_GLOBAL_INT('KEYIN_HIT', 1);\n";
	out << "SET_GLOBAL_INT('KEYIN_ERR', ERROR_LEVEL);\n";
	out << "END_MACRO;\n";
	return out.good();
}

bool expectCompileError(const char *source, const char *expectedPart) {
	size_t bytecodeSize = 0;
	unsigned char *bytecode = compile_macro_code(source, &bytecodeSize);
	const char *errorText = get_last_compile_error();

	if (bytecode != NULL) {
		std::free(bytecode);
		std::fprintf(stderr, "Expected compile error but compilation succeeded.\n");
		return false;
	}
	if (errorText == NULL || *errorText == '\0') {
		std::fprintf(stderr, "Expected compile error text, but it is empty.\n");
		return false;
	}
	if (expectedPart != NULL && std::strstr(errorText, expectedPart) == NULL) {
		std::fprintf(stderr, "Compile error text mismatch: %s\n", errorText);
		return false;
	}
	return true;
}

bool containsText(const std::vector<std::string> &values, const char *needle) {
	return std::find(values.begin(), values.end(), std::string(needle)) != values.end();
}

bool checkGlobalInt(const std::map<std::string, int> &ints, const char *name, int expected) {
	std::map<std::string, int>::const_iterator it = ints.find(name);
	if (it == ints.end()) {
		std::fprintf(stderr, "Missing global %s.\n", name);
		return false;
	}
	if (it->second != expected) {
		std::fprintf(stderr, "Global %s mismatch: expected %d, got %d.\n", name, expected, it->second);
		return false;
	}
	return true;
}
} // namespace

int main() {
	static const char source[] =
	    "$MACRO KeyOnCtrlP TO <CtrlP> FROM EDIT;\n"
	    "KEY_IN('X<Enter>Y');\n"
	    "SET_GLOBAL_INT('KEYIN_HIT', 1);\n"
	    "SET_GLOBAL_INT('KEYIN_ERR', ERROR_LEVEL);\n"
	    "END_MACRO;\n";
	static const char macroPath[] = "/tmp/mr_keyin_probe.mrmac";
	size_t bytecodeSize = 0;
	unsigned char *bytecode = compile_macro_code(source, &bytecodeSize);
	std::string loaderSource;
	VirtualMachine vm;
	std::string executedMacroName;
	std::vector<std::string> globalOrder;
	std::map<std::string, int> globalInts;
	std::map<std::string, std::string> globalStrings;
	MRMacroExecutionProfile profile;

	if (bytecode == NULL) {
		std::fprintf(stderr, "Compilation failed: %s\n", get_last_compile_error());
		return 1;
	}

	profile = mrvmAnalyzeBytecode(bytecode, bytecodeSize);
	if (mrvmCanRunStagedInBackground(profile)) {
		std::fprintf(stderr, "KEY_IN macro should not be staged-background eligible.\n");
		std::free(bytecode);
		return 1;
	}
	if (!containsText(mrvmUnsupportedStagedSymbols(profile), "KEY_IN")) {
		std::fprintf(stderr, "KEY_IN should be reported as unsupported staged symbol.\n");
		std::free(bytecode);
		return 1;
	}
	std::free(bytecode);

	if (!writeProbeMacroFile(macroPath)) {
		std::fprintf(stderr, "Unable to create KEY_IN probe macro file.\n");
		return 1;
	}
	loaderSource = "$MACRO Main;\nLOAD_MACRO_FILE('";
	loaderSource += macroPath;
	loaderSource += "');\nEND_MACRO;\n";
	bytecode = compile_macro_code(loaderSource.c_str(), &bytecodeSize);
	if (bytecode == NULL) {
		std::fprintf(stderr, "Compilation failed: %s\n", get_last_compile_error());
		std::remove(macroPath);
		return 1;
	}
	vm.execute(bytecode, bytecodeSize);
	std::free(bytecode);

	if (!mrvmRunAssignedMacroForKey(kbCtrlP, 0, executedMacroName, nullptr) ||
	    executedMacroName != "KeyOnCtrlP") {
		std::fprintf(stderr, "Ctrl+P macro dispatch failed.\n");
		std::remove(macroPath);
		return 1;
	}

	mrvmUiCopyGlobals(globalOrder, globalInts, globalStrings);
	if (!checkGlobalInt(globalInts, "KEYIN_HIT", 1)) {
		std::remove(macroPath);
		return 1;
	}
	if (!checkGlobalInt(globalInts, "KEYIN_ERR", 1001)) {
		std::remove(macroPath);
		return 1;
	}

	if (!expectCompileError("$MACRO Bad;\nKEY_IN(1);\nEND_MACRO;\n",
	                       "Type mismatch or syntax error.")) {
		std::remove(macroPath);
		return 1;
	}

	std::remove(macroPath);

	std::puts("KEY_IN probe passed.");
	return 0;
}
