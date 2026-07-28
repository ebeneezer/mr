#ifndef MRVM_BYTECODE_EXECUTION_HPP
#define MRVM_BYTECODE_EXECUTION_HPP

#include "../MRVM.hpp"
#include "MRVMRuntimeState.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class VirtualMachine::BytecodeExecution final {
 public:
	BytecodeExecution(VirtualMachine &machine, const unsigned char *sourceBytecode, std::size_t sourceLength, std::size_t sourceEntryOffset, const std::string &sourceParameterString, const std::string &sourceMacroName, bool sourceResetState, bool sourceFirstRun, bool sourcePreserveExecutionState) noexcept;
	void run();

 private:
	friend class ProcedureExecution;
	friend class MacroProcedures;

	VirtualMachine &vm;
	const unsigned char *bytecode;
	std::size_t length;
	std::size_t entryOffset;
	const std::string &parameterString;
	const std::string &macroName;
	bool resetState;
	bool firstRun;
	bool preserveExecutionState;
	std::size_t ip;
	std::vector<std::size_t> callStack;
	ExecutionState state;
	ExecutionState *parentState;
	std::string savedParameterString;
	std::string activeMacroName;
	bool activeFirstRun;
	bool pushedMacroFrame;
	bool allowAsyncDelay;
	bool resumeFromDebug;
	bool resumeFromDelay;
	std::uint64_t resumeGeneration;

	void readInt(int &value);
	void readDouble(double &value);
	void readCString(std::string &value);
	std::vector<Value> popArguments(unsigned char count);
};

#endif
