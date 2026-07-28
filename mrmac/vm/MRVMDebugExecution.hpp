#ifndef MRVM_DEBUG_EXECUTION_HPP
#define MRVM_DEBUG_EXECUTION_HPP

#include "../MRVM.hpp"

#include <cstddef>
#include <string>
#include <vector>

struct VirtualMachine::MRMacroDebugChildFrame {
	std::unique_ptr<VirtualMachine> vm;
	MRMacroDebugRunResult result;
	std::string macroKey;
	std::string fileKey;
	std::size_t parentInstructionOffset;
	bool unloadAfterCompletion;
	bool evictTransientAfterCompletion;

	MRMacroDebugChildFrame() : vm(), result(), macroKey(), fileKey(), parentInstructionOffset(0), unloadAfterCompletion(false), evictTransientAfterCompletion(false) {
	}
};

class VirtualMachine::DebugExecution final {
 public:
	explicit DebugExecution(VirtualMachine &machine) noexcept;
	MRMacroDebugWatchSnapshot evaluateWatchExpression(const std::string &expression);
	bool writeScalarVariable(const MRMacroDebugVariableSnapshot &variable, const std::string &valueText, std::vector<MRMacroDebugVariableSnapshot> &updatedVariables, std::string &errorMessage);
	MRMacroDebugRunResult start(const unsigned char *bytecode, std::size_t length, std::size_t entryOffset, const std::string &parameterString, const std::string &macroName, const std::vector<std::size_t> &breakpointOffsets, bool firstRun, const std::string &macroKey, const std::string &sourcePath);
	MRMacroDebugRunResult continueExecution(const std::vector<std::size_t> &breakpointOffsets, std::size_t instructionBudget);
	MRMacroDebugRunResult step(const std::vector<std::size_t> &breakpointOffsets, MRMacroDebugStepMode mode);

 private:
	VirtualMachine &vm;

	void appendCallStack(MRMacroDebugRunResult &result) const;
	void appendParentCallStack(MRMacroDebugRunResult &result, std::size_t parentInstructionOffset) const;
};

#endif
