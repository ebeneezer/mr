#ifndef MRVM_RUNTIME_DEBUGGER_HPP
#define MRVM_RUNTIME_DEBUGGER_HPP

#include "MRVMRuntimeKv.hpp"

#include <cstddef>
#include <string>
#include <vector>

struct MRMacroDebuggerBreakpoint {
	std::string macroKey;
	bool enabled;
	int line;
	std::size_t sourceStartOffset;
	std::size_t sourceEndOffset;
	std::size_t bytecodeOffset;
	int debuggableKind;
	std::string conditionText;

	MRMacroDebuggerBreakpoint();
};

struct MRMacroDebuggerWatch {
	std::string macroKey;
	std::string expression;
	bool enabled;

	MRMacroDebuggerWatch();
};

bool mrvmRuntimeDebuggerWriteLineBreakpoint(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, int line, bool enabled, const std::string &conditionText);
bool mrvmRuntimeDebuggerReadLineBreakpoint(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, int line, MRMacroDebuggerBreakpoint &breakpoint);
bool mrvmRuntimeDebuggerEraseLineBreakpoint(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, int line);
bool mrvmRuntimeDebuggerLineBreakpointsForMacro(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, std::vector<MRMacroDebuggerBreakpoint> &breakpoints);
bool mrvmRuntimeDebuggerSetLineBreakpointsEnabledForMacro(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, bool enabled);
bool mrvmRuntimeDebuggerEraseLineBreakpointsForMacro(MRVMRuntimeKv &runtimeKv, const std::string &macroKey);
bool mrvmRuntimeDebuggerEnabledBreakpointOffsetsForMacro(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, std::vector<std::size_t> &bytecodeOffsets);
bool mrvmRuntimeDebuggerWriteWatch(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, const std::string &expression, bool enabled);
bool mrvmRuntimeDebuggerEraseWatch(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, const std::string &expression);
bool mrvmRuntimeDebuggerEraseWatchesForMacro(MRVMRuntimeKv &runtimeKv, const std::string &macroKey);
bool mrvmRuntimeDebuggerWatchesForMacro(MRVMRuntimeKv &runtimeKv, const std::string &macroKey, std::vector<MRMacroDebuggerWatch> &watches);

#endif
