#ifndef MRVM_PROCEDURE_EXECUTION_HPP
#define MRVM_PROCEDURE_EXECUTION_HPP

#include "../MRVM.hpp"
#include "MRVMProcedureCatalog.hpp"

#include <cstddef>
#include <string>
#include <vector>

class VirtualMachine::ProcedureExecution final {
 public:
	ProcedureExecution(VirtualMachine &machine, BytecodeExecution &bytecodeExecution) noexcept;
	InstructionFlow execute(const std::string &name, const std::vector<Value> &args, std::size_t instructionOffset);

 private:
	VirtualMachine &vm;
	BytecodeExecution &execution;
};

class VirtualMachine::ConfigurationProcedures final {
 public:
	explicit ConfigurationProcedures(VirtualMachine &machine) noexcept;
	InstructionFlow execute(MRVMProcedureCatalog::Procedure procedure, const std::string &name, const std::vector<Value> &args);

 private:
	VirtualMachine &vm;
};

class VirtualMachine::RuntimeProcedures final {
 public:
	explicit RuntimeProcedures(VirtualMachine &machine) noexcept;
	InstructionFlow execute(MRVMProcedureCatalog::Procedure procedure, const std::string &name, const std::vector<Value> &args, bool allowAsyncDelay);

 private:
	VirtualMachine &vm;
};

class VirtualMachine::EditorProcedures final {
 public:
	EditorProcedures() noexcept = default;
	InstructionFlow execute(MRVMProcedureCatalog::Procedure procedure, const std::string &name, const std::vector<Value> &args);
};

class VirtualMachine::MacroProcedures final {
 public:
	MacroProcedures(VirtualMachine &machine, BytecodeExecution &bytecodeExecution) noexcept;
	InstructionFlow execute(MRVMProcedureCatalog::Procedure procedure, const std::string &name, const std::vector<Value> &args, std::size_t instructionOffset);

 private:
	VirtualMachine &vm;
	BytecodeExecution &execution;
};

#endif
