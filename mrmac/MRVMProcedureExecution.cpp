#include "MRVM.hpp"

#include "vm/MRVMExecutionInternal.hpp"
#include "vm/MRVMProcedureCatalog.hpp"

#include <stdexcept>
#include <string>
#include <vector>

VirtualMachine::InstructionFlow VirtualMachine::executeProcedure(ExecutionFrame &frame, const std::string &name, const std::vector<Value> &args, std::size_t instructionOffset) {
	const MRVMProcedure procedure = MRVMProcedureCatalog::classify(name);

	switch (MRVMProcedureCatalog::family(procedure)) {
		case MRVMProcedureCatalog::Family::Configuration:
			return executeConfigurationProcedure(procedure, name, args);
		case MRVMProcedureCatalog::Family::Runtime:
			return executeRuntimeProcedure(procedure, name, args, frame.allowAsyncDelay);
		case MRVMProcedureCatalog::Family::Editor:
			return executeEditorProcedure(procedure, name, args);
		case MRVMProcedureCatalog::Family::Macro:
			return executeMacroProcedure(frame, procedure, name, args, instructionOffset);
	}
	throw std::runtime_error("Unknown procedure family.");
}
