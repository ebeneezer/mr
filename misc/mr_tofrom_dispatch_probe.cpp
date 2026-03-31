#define Uses_TKeys
#include <tvision/tv.h>

#include "../mrmac/mrmac.h"
#include "../mrmac/mrvm.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace {
bool writeProbeMacroFile(const char *path) {
	std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	out << "$MACRO HitEdit TO <AltB> FROM EDIT;\n";
	out << "SET_GLOBAL_INT('HIT_EDIT', 1);\n";
	out << "END_MACRO;\n";
	out << "$MACRO HitEditOverride TO <AltB> FROM EDIT;\n";
	out << "SET_GLOBAL_INT('HIT_EDIT', 2);\n";
	out << "END_MACRO;\n";
	out << "$MACRO HitShiftTab TO <ShftTAB> FROM EDIT;\n";
	out << "SET_GLOBAL_INT('HIT_SHIFT_TAB', 1);\n";
	out << "END_MACRO;\n";
	out << "$MACRO HitCtrlA TO <CtrlA> FROM EDIT;\n";
	out << "SET_GLOBAL_INT('HIT_CTRL_A', 1);\n";
	out << "END_MACRO;\n";
	out << "$MACRO HitAlt1 TO <Alt1> FROM EDIT;\n";
	out << "SET_GLOBAL_INT('HIT_ALT_1', 1);\n";
	out << "END_MACRO;\n";
	out << "$MACRO HitShell TO <AltS> FROM DOS_SHELL;\n";
	out << "SET_GLOBAL_INT('HIT_SHELL', 1);\n";
	out << "END_MACRO;\n";
	return out.good();
}
} // namespace

int main() {
	static const char macroPath[] = "/tmp/mr_tofrom_dispatch.mrmac";
	std::string loaderSource;
	size_t bytecodeSize = 0;
	unsigned char *bytecode = NULL;
	VirtualMachine vm;
	std::string executedMacroName;

	if (!writeProbeMacroFile(macroPath)) {
		std::fprintf(stderr, "Unable to create probe macro file.\n");
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

	if (!mrvmRunAssignedMacroForKey(kbAltB, 0, executedMacroName, nullptr) ||
	    executedMacroName != "HitEditOverride") {
		std::fprintf(stderr, "Edit-mode key dispatch failed.\n");
		std::remove(macroPath);
		return 1;
	}
	if (!mrvmRunAssignedMacroForKey(kbShiftTab, 0, executedMacroName, nullptr) ||
	    executedMacroName != "HitShiftTab") {
		std::fprintf(stderr, "Shift+Tab dispatch failed.\n");
		std::remove(macroPath);
		return 1;
	}
	if (!mrvmRunAssignedMacroForKey(kbCtrlA, 0, executedMacroName, nullptr) ||
	    executedMacroName != "HitCtrlA") {
		std::fprintf(stderr, "Ctrl+A dispatch failed.\n");
		std::remove(macroPath);
		return 1;
	}
	if (!mrvmRunAssignedMacroForKey(kbAlt1, 0, executedMacroName, nullptr) ||
	    executedMacroName != "HitAlt1") {
		std::fprintf(stderr, "Alt+1 dispatch failed.\n");
		std::remove(macroPath);
		return 1;
	}
	if (mrvmRunAssignedMacroForKey(kbAltS, 0, executedMacroName, nullptr)) {
		std::fprintf(stderr, "DOS_SHELL macro should not execute in EDIT mode.\n");
		std::remove(macroPath);
		return 1;
	}
	if (mrvmRunAssignedMacroForKey(kbF12, 0, executedMacroName, nullptr)) {
		std::fprintf(stderr, "Unexpected macro dispatch for unbound key.\n");
		std::remove(macroPath);
		return 1;
	}

	std::remove(macroPath);
	std::puts("TO/FROM dispatch probe passed.");
	return 0;
}
