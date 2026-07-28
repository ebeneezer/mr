#include "../MRVMProcedureExecution.hpp"

#include "../MRVMBytecodeExecution.hpp"

#include <stdexcept>
#include <string>
#include <vector>

VirtualMachine::ProcedureExecution::ProcedureExecution(VirtualMachine &machine, BytecodeExecution &bytecodeExecution) noexcept : vm(machine), execution(bytecodeExecution) {
}

VirtualMachine::InstructionFlow VirtualMachine::ProcedureExecution::execute(const std::string &name, const std::vector<Value> &args, std::size_t instructionOffset) {
	const MRVMProcedure procedure = MRVMProcedureCatalog::classify(name);

	switch (MRVMProcedureCatalog::family(procedure)) {
		case MRVMProcedureCatalog::Family::Configuration:
			return ConfigurationProcedures(vm).execute(procedure, name, args);
		case MRVMProcedureCatalog::Family::Runtime:
			return RuntimeProcedures(vm).execute(procedure, name, args, execution.allowAsyncDelay);
		case MRVMProcedureCatalog::Family::Editor:
			return EditorProcedures().execute(procedure, name, args);
		case MRVMProcedureCatalog::Family::Macro:
			return MacroProcedures(vm, execution).execute(procedure, name, args, instructionOffset);
	}
	throw std::runtime_error("Unknown procedure family.");
}
