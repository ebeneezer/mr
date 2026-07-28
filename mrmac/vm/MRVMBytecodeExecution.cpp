#include "MRVMBytecodeExecution.hpp"

#include "MRVMDelayRuntime.hpp"
#include "MRVMExecSessions.hpp"
#include "MRVMHash.hpp"
#include "MRVMIntrinsics.hpp"
#include "MRVMProcedureCatalog.hpp"
#include "MRVMProcedureExecution.hpp"
#include "MRVMRuntimeInternal.hpp"
#include "MRVMRuntimeKv.hpp"
#include "MRVMSystemVariables.hpp"
#include "MRVMValue.hpp"
#include "../mrmac.h"
#include "../../app/utils/MRConstants.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mrvm_runtime;

void VirtualMachine::executeAt(const unsigned char *bytecode, size_t length, size_t entryOffset, const std::string &parameterString, const std::string &macroName, bool resetState, bool firstRun) {
	executeAt(bytecode, length, entryOffset, parameterString, macroName, resetState, firstRun, false);
}

void VirtualMachine::executeAt(const unsigned char *bytecode, size_t length, size_t entryOffset, const std::string &parameterString, const std::string &macroName, bool resetState, bool firstRun, bool preserveExecutionState) {
	BytecodeExecution(*this, bytecode, length, entryOffset, parameterString, macroName, resetState, firstRun, preserveExecutionState).run();
}

VirtualMachine::BytecodeExecution::BytecodeExecution(VirtualMachine &machine, const unsigned char *sourceBytecode, std::size_t sourceLength, std::size_t sourceEntryOffset, const std::string &sourceParameterString, const std::string &sourceMacroName, bool sourceResetState, bool sourceFirstRun, bool sourcePreserveExecutionState) noexcept : vm(machine), bytecode(sourceBytecode), length(sourceLength), entryOffset(sourceEntryOffset), parameterString(sourceParameterString), macroName(sourceMacroName), resetState(sourceResetState), firstRun(sourceFirstRun), preserveExecutionState(sourcePreserveExecutionState), ip(sourceEntryOffset), callStack(), state(), parentState(nullptr), savedParameterString(), activeMacroName(), activeFirstRun(false), pushedMacroFrame(false), allowAsyncDelay(false), resumeFromDebug(false), resumeFromDelay(false), resumeGeneration(0) {
}

void VirtualMachine::BytecodeExecution::readInt(int &value) {
	std::memcpy(&value, &bytecode[ip], sizeof(int));
	ip += sizeof(int);
}

void VirtualMachine::BytecodeExecution::readDouble(double &value) {
	std::memcpy(&value, &bytecode[ip], sizeof(double));
	ip += sizeof(double);
}

void VirtualMachine::BytecodeExecution::readCString(std::string &value) {
	const char *text = reinterpret_cast<const char *>(&bytecode[ip]);
	value = text;
	ip += value.size() + 1;
}

std::vector<VirtualMachine::Value> VirtualMachine::BytecodeExecution::popArguments(unsigned char count) {
	std::vector<Value> args;
	args.reserve(count);
	for (unsigned char i = 0; i < count; ++i)
		args.push_back(vm.pop());
	std::reverse(args.begin(), args.end());
	return args;
}

void VirtualMachine::BytecodeExecution::run() {
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	resumeFromDebug = (bytecode == nullptr && length == 0 && vm.debugState.paused && !vm.debugState.bytecode.empty() && vm.debugState.ip <= vm.debugState.length);
	resumeFromDelay = (!resumeFromDebug && bytecode == nullptr && length == 0 && vm.delayState.pending && vm.delayState.ready && !vm.delayState.bytecode.empty() && vm.delayState.ip <= vm.delayState.length);
	resumeGeneration = vm.delayState.generation;
	ip = resumeFromDebug ? vm.debugState.ip : (resumeFromDelay ? vm.delayState.ip : entryOffset);
	parentState = currentExecutionState();
	struct ExecutionStateGuard {
		ExecutionState *previous;

		explicit ExecutionStateGuard(ExecutionState *next) noexcept : previous(g_executionState) {
			g_executionState = next;
		}

		~ExecutionStateGuard() {
			g_executionState = previous;
		}
	} executionStateGuard(&state);
	struct ExecutionSessionGuard {
		MRMacroExecutionSessionId previous;

		explicit ExecutionSessionGuard(MRMacroExecutionSessionId next) noexcept : previous(g_executionSessionId) {
			if (next != 0) g_executionSessionId = next;
		}

		~ExecutionSessionGuard() {
			g_executionSessionId = previous;
		}
	} executionSessionGuard(vm.mExecutionSessionId != 0 ? vm.mExecutionSessionId : g_executionSessionId);

	if (resumeFromDebug) {
		bytecode = vm.debugState.bytecode.data();
		length = vm.debugState.length;
		callStack = vm.debugState.callStack;
		savedParameterString = vm.debugState.savedParameterString;
		state.parameterString = vm.debugState.savedParameterString;
		state.returnInt = vm.debugState.returnInt;
		state.returnStr = vm.debugState.returnStr;
		state.errorLevel = vm.debugState.errorLevel;
		activeMacroName = vm.debugState.macroName;
		activeFirstRun = vm.debugState.firstRun;
		vm.debugState.paused = false;
		if (!activeMacroName.empty()) {
			g_runtimeEnv.macroStack.emplace_back(activeMacroName, activeFirstRun);
			pushedMacroFrame = true;
		}
	} else if (resumeFromDelay) {
		bytecode = vm.delayState.bytecode.data();
		length = vm.delayState.length;
		callStack = vm.delayState.callStack;
		savedParameterString = vm.delayState.savedParameterString;
		state.parameterString = vm.delayState.savedParameterString;
		state.returnInt = vm.delayState.returnInt;
		state.returnStr = vm.delayState.returnStr;
		state.errorLevel = vm.delayState.errorLevel;
		pushedMacroFrame = vm.delayState.macroFramePushed;
		vm.delayState.ready = false;
	} else {
		if (bytecode == nullptr || length == 0 || entryOffset >= length) return;
		savedParameterString = parentState != nullptr ? parentState->parameterString : g_runtimeEnv.parameterString;
		state.parameterString = savedParameterString;
		if (parentState != nullptr) {
			state.returnInt = parentState->returnInt;
			state.returnStr = parentState->returnStr;
			state.errorLevel = parentState->errorLevel;
		} else {
			state.returnInt = g_runtimeEnv.returnInt;
			state.returnStr = g_runtimeEnv.returnStr;
			state.errorLevel = g_runtimeEnv.errorLevel;
		}

		if (!preserveExecutionState) {
			vm.variables.clear();
			vm.mSessionVariableNames.clear();
			vm.mHashStore->clearExceptRoots(currentGlobalHashRoots());
			vm.cancelledExecution = false;
		}
		vm.stack.clear();
		if (resetState) {
			vm.log.clear();
			vm.logTruncated = false;
			setMacroGlobalEnumIndex(0);
			setMacroCatalogMacroEnumIndex(0);
			state.parameterString.clear();
			state.returnInt = 0;
			state.returnStr.clear();
			state.errorLevel = 0;
		}

		if (!macroName.empty()) {
			g_runtimeEnv.macroStack.emplace_back(macroName, firstRun);
			pushedMacroFrame = true;
		}
		activeMacroName = macroName;
		activeFirstRun = firstRun;
		state.parameterString = parameterString;
	}
	allowAsyncDelay = (vm.delayState.enabled && parentState == nullptr && currentBackgroundEditSession() == nullptr && g_backgroundMacroCancelFlag == nullptr);
	if (allowAsyncDelay && !resumeFromDelay) {
		vm.delayState.bytecode.assign(bytecode, bytecode + length);
		vm.delayState.length = length;
	}
	MRVMIntrinsics intrinsics(vm);

	try {
		while (ip < length) {
			if (vm.debugState.runActive && vm.debugState.pauseRequested) {
				vm.debugState.stopped = true;
				vm.debugState.stopReason = mrdStopPaused;
				vm.debugState.stopOffset = ip;
				vm.debugState.stackDepth = callStack.size();
				vm.debugState.capturePausedExecution(bytecode, length, ip, callStack, state, savedParameterString, activeMacroName, activeFirstRun);
				vm.debugState.pauseRequested = false;
				break;
			}
			if (backgroundMacroCancelRequested()) {
				vm.cancelledExecution = true;
				vm.appendLogLine("VM Notice: Background macro cancelled.", true);
				runtimeErrorLevel() = 5007;
				break;
			}
			if (vm.debugState.runActive && std::binary_search(vm.debugState.breakpointOffsets.begin(), vm.debugState.breakpointOffsets.end(), ip)) {
				if (vm.debugState.skipCurrentOffset && ip == vm.debugState.stopOffset) {
					vm.debugState.skipCurrentOffset = false;
				} else {
					vm.debugState.stopped = true;
					vm.debugState.stopReason = mrdStopBreakpoint;
					vm.debugState.stopOffset = ip;
					vm.debugState.stackDepth = callStack.size();
					vm.debugState.capturePausedExecution(bytecode, length, ip, callStack, state, savedParameterString, activeMacroName, activeFirstRun);
					break;
				}
			}
			const std::size_t instructionOffset = ip;
			unsigned char opcode = bytecode[ip++];
			bool finishExecution = false;

			switch (opcode) {
				case OP_PUSH_I: {
					int val;
					readInt(val);
					vm.push(mrvmMakeInt(val));
					vm.appendLogLine("Push integer: " + std::to_string(val));
				} break;
				case OP_PUSH_R: {
					double val;
					readDouble(val);
					vm.push(mrvmMakeReal(val));
					vm.appendLogLine("Push real: " + mrvmValueAsString(mrvmMakeReal(val)));
				} break;
				case OP_PUSH_S: {
					std::string str;
					readCString(str);
					mrvmEnforceStringLength(str);
					vm.push(mrvmMakeString(str));
					vm.appendLogLine("Push string: " + str);
				} break;
				case OP_DEF_VAR: {
					std::string varName;
					int varType = static_cast<int>(bytecode[ip++]);
					Value value;
					readCString(varName);
					if (!vm.mClosureId.empty()) {
						bool restored = false;
						vm.mClosureVariableNames.insert(varName);
						if (mrvmExecSessionsReadClosureVariable(g_runtimeEnv.runtimeKv, vm.mClosureId, varName, value)) {
							vm.variables[varName] = mrvmCoerceForStore(value, varType);
							restored = true;
						} else if (varType == TYPE_HASH)
							vm.variables[varName] = mrvmMakeHash(vm.mHashStore->createHash());
						else
							vm.variables[varName] = mrvmDefaultValueForType(varType);
						if (!restored) static_cast<void>(mrvmExecSessionsWriteClosureVariable(g_runtimeEnv.runtimeKv, vm.mClosureId, varName, vm.variables[varName], *vm.mHashStore));
					} else if (currentExecutionSessionId() != 0) {
						bool restored = false;
						vm.mSessionVariableNames.insert(varName);
						if (mrvmExecSessionsReadSessionVariable(g_runtimeEnv.runtimeKv, currentExecutionSessionId(), varName, value)) {
							vm.variables[varName] = mrvmCoerceForStore(value, varType);
							restored = true;
						} else if (varType == TYPE_HASH)
							vm.variables[varName] = mrvmMakeHash(vm.mHashStore->createHash());
						else
							vm.variables[varName] = mrvmDefaultValueForType(varType);
						if (!restored) static_cast<void>(mrvmExecSessionsWriteSessionVariable(g_runtimeEnv.runtimeKv, currentExecutionSessionId(), varName, vm.variables[varName], *vm.mHashStore));
					} else if (varType == TYPE_HASH)
						vm.variables[varName] = mrvmMakeHash(vm.mHashStore->createHash());
					else
						vm.variables[varName] = mrvmDefaultValueForType(varType);
					vm.appendLogLine("Define variable: " + varName);
				} break;
				case OP_LOAD_VAR: {
					std::string varName;
					bool handled = false;
					readCString(varName);

					Value special = MRVMSystemVariables::load(varName, handled);
					if (handled) vm.push(special);
					else {
						std::map<std::string, Value>::const_iterator it = vm.variables.find(varName);
						if (it == vm.variables.end()) vm.variables[varName] = mrvmMakeInt(0);
						vm.push(vm.variables[varName]);
					}
					vm.appendLogLine("Load variable: " + varName);
				} break;
				case OP_STORE_VAR: {
					std::string varName;
					int targetType = static_cast<int>(bytecode[ip++]);
					readCString(varName);
					Value value = mrvmCoerceForStore(vm.pop(), targetType);
					if (value.type == TYPE_STR) mrvmEnforceStringLength(value.s);
					if (!MRVMSystemVariables::store(varName, value)) vm.variables[varName] = value;
					if (!vm.mClosureId.empty() && vm.mClosureVariableNames.find(varName) != vm.mClosureVariableNames.end()) mrvmExecSessionsWriteClosureVariable(g_runtimeEnv.runtimeKv, vm.mClosureId, varName, value, *vm.mHashStore);
					else if (currentExecutionSessionId() != 0 && vm.mSessionVariableNames.find(varName) != vm.mSessionVariableNames.end())
						mrvmExecSessionsWriteSessionVariable(g_runtimeEnv.runtimeKv, currentExecutionSessionId(), varName, value, *vm.mHashStore);
					vm.appendLogLine("Store variable: " + varName);
				} break;
				case OP_HASH_LOAD: {
					std::string varName;
					Value key;
					std::map<std::string, Value>::const_iterator it;
					readCString(varName);
					key = vm.pop();
					if (!mrvmIsStringLike(key)) throw std::runtime_error("type mismatch");
					it = vm.variables.find(varName);
					if (it == vm.variables.end() || it->second.type != TYPE_HASH) throw std::runtime_error("Invalid hash value.");
					vm.push(mrvmHashReadValue(*vm.mHashStore, g_runtimeEnv.runtimeKv.globalStore(), it->second, mrvmValueAsString(key)));
					vm.appendLogLine("Load hash value: " + varName);
				} break;
				case OP_HASH_LOAD_VALUE: {
					Value key;
					Value hash;
					key = vm.pop();
					hash = vm.pop();
					if (hash.type != TYPE_HASH) throw std::runtime_error("Invalid hash value.");
					if (!mrvmIsStringLike(key)) throw std::runtime_error("type mismatch");
					vm.push(mrvmHashReadValue(*vm.mHashStore, g_runtimeEnv.runtimeKv.globalStore(), hash, mrvmValueAsString(key)));
					vm.appendLogLine("Load hash value from expression.");
				} break;
				case OP_HASH_STORE: {
					std::string varName;
					Value value;
					Value key;
					std::map<std::string, Value>::const_iterator it;
					readCString(varName);
					value = vm.pop();
					key = vm.pop();
					if (!mrvmIsStringLike(key)) throw std::runtime_error("type mismatch");
					it = vm.variables.find(varName);
					if (it == vm.variables.end() || it->second.type != TYPE_HASH) throw std::runtime_error("Invalid hash value.");
					if (value.type == TYPE_STR) mrvmEnforceStringLength(value.s);
					mrvmHashWriteValue(*vm.mHashStore, g_runtimeEnv.runtimeKv.globalStore(), it->second, mrvmValueAsString(key), value);
					if (!vm.mClosureId.empty() && vm.mClosureVariableNames.find(varName) != vm.mClosureVariableNames.end()) mrvmExecSessionsWriteClosureVariable(g_runtimeEnv.runtimeKv, vm.mClosureId, varName, it->second, *vm.mHashStore);
					else if (currentExecutionSessionId() != 0 && vm.mSessionVariableNames.find(varName) != vm.mSessionVariableNames.end())
						mrvmExecSessionsWriteSessionVariable(g_runtimeEnv.runtimeKv, currentExecutionSessionId(), varName, it->second, *vm.mHashStore);
					vm.appendLogLine("Store hash value: " + varName);
				} break;
				case OP_HASH_STORE_VALUE: {
					Value value;
					Value key;
					Value hash;
					value = vm.pop();
					key = vm.pop();
					hash = vm.pop();
					if (hash.type != TYPE_HASH) throw std::runtime_error("Invalid hash value.");
					if (!mrvmIsStringLike(key)) throw std::runtime_error("type mismatch");
					if (value.type == TYPE_STR) mrvmEnforceStringLength(value.s);
					mrvmHashWriteValue(*vm.mHashStore, g_runtimeEnv.runtimeKv.globalStore(), hash, mrvmValueAsString(key), value);
					vm.appendLogLine("Store hash value from expression.");
				} break;
				case OP_ARRAY_LOAD: {
					std::string varName;
					Value index;
					std::map<std::string, Value>::const_iterator it;
					readCString(varName);
					index = vm.pop();
					if (index.type != TYPE_INT) throw std::runtime_error("type mismatch");
					it = vm.variables.find(varName);
					if (it == vm.variables.end() || !mrvmValueIsArrayType(it->second.type)) throw std::runtime_error("Invalid array value.");
					vm.push(mrvmArrayReadValue(it->second, index.i));
					vm.appendLogLine("Load array value: " + varName);
				} break;
				case OP_ARRAY_LOAD_VALUE: {
					Value index;
					Value arrayValue;
					index = vm.pop();
					arrayValue = vm.pop();
					if (index.type != TYPE_INT) throw std::runtime_error("type mismatch");
					vm.push(mrvmArrayReadValue(arrayValue, index.i));
					vm.appendLogLine("Load array value from expression.");
				} break;
				case OP_ARRAY_STORE: {
					std::string varName;
					Value value;
					Value index;
					std::map<std::string, Value>::iterator it;
					readCString(varName);
					value = vm.pop();
					index = vm.pop();
					if (index.type != TYPE_INT) throw std::runtime_error("type mismatch");
					it = vm.variables.find(varName);
					if (it == vm.variables.end() || !mrvmValueIsArrayType(it->second.type)) throw std::runtime_error("Invalid array value.");
					mrvmArrayWriteValue(it->second, index.i, value, *vm.mHashStore, g_runtimeEnv.runtimeKv.globalStore());
					if (!vm.mClosureId.empty() && vm.mClosureVariableNames.find(varName) != vm.mClosureVariableNames.end()) mrvmExecSessionsWriteClosureVariable(g_runtimeEnv.runtimeKv, vm.mClosureId, varName, it->second, *vm.mHashStore);
					else if (currentExecutionSessionId() != 0 && vm.mSessionVariableNames.find(varName) != vm.mSessionVariableNames.end())
						mrvmExecSessionsWriteSessionVariable(g_runtimeEnv.runtimeKv, currentExecutionSessionId(), varName, it->second, *vm.mHashStore);
					vm.appendLogLine("Store array value: " + varName);
				} break;
				case OP_GOTO: {
					int target;
					readInt(target);
					if (target < 0 || static_cast<size_t>(target) >= length) throw std::runtime_error("Invalid jump target in GOTO.");
					ip = static_cast<size_t>(target);
				} break;
				case OP_CALL: {
					int target;
					readInt(target);
					if (target < 0 || static_cast<size_t>(target) >= length) throw std::runtime_error("Invalid jump target in CALL.");
					callStack.push_back(ip);
					ip = static_cast<size_t>(target);
				} break;
				case OP_RET: {
					if (callStack.empty()) throw std::runtime_error("RET without matching CALL.");
					ip = callStack.back();
					callStack.pop_back();
				} break;
				case OP_JZ: {
					int target;
					Value cond;
					readInt(target);
					cond = vm.pop();
					if (cond.type != TYPE_INT) throw std::runtime_error("IF/WHILE expression must be integer.");
					if (target < 0 || static_cast<size_t>(target) >= length) throw std::runtime_error("Invalid jump target in JZ.");
					if (cond.i == 0) ip = static_cast<size_t>(target);
				} break;
				case OP_ADD: {
					Value b = vm.pop();
					Value a = vm.pop();
					if (mrvmIsStringLike(a) && mrvmIsStringLike(b)) {
						std::string s = mrvmValueAsString(a) + mrvmValueAsString(b);
						mrvmEnforceStringLength(s);
						vm.push(mrvmMakeString(s));
					} else if (mrvmIsNumeric(a) && mrvmIsNumeric(b)) {
						if (a.type == TYPE_REAL || b.type == TYPE_REAL) vm.push(mrvmMakeReal(mrvmValueAsReal(a) + mrvmValueAsReal(b)));
						else
							vm.push(mrvmMakeInt(a.i + b.i));
					} else
						throw std::runtime_error(MRConstants::kErrorTypeMismatch);
				} break;
				case OP_SUB: {
					Value b = vm.pop();
					Value a = vm.pop();
					if (!mrvmIsNumeric(a) || !mrvmIsNumeric(b)) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
					if (a.type == TYPE_REAL || b.type == TYPE_REAL) vm.push(mrvmMakeReal(mrvmValueAsReal(a) - mrvmValueAsReal(b)));
					else
						vm.push(mrvmMakeInt(a.i - b.i));
				} break;
				case OP_MUL: {
					Value b = vm.pop();
					Value a = vm.pop();
					if (!mrvmIsNumeric(a) || !mrvmIsNumeric(b)) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
					if (a.type == TYPE_REAL || b.type == TYPE_REAL) vm.push(mrvmMakeReal(mrvmValueAsReal(a) * mrvmValueAsReal(b)));
					else
						vm.push(mrvmMakeInt(a.i * b.i));
				} break;
				case OP_DIV: {
					Value b = vm.pop();
					Value a = vm.pop();
					if (!mrvmIsNumeric(a) || !mrvmIsNumeric(b)) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
					if ((b.type == TYPE_REAL && b.r == 0.0) || (b.type == TYPE_INT && b.i == 0)) throw std::runtime_error("Division by zero.");
					if (a.type == TYPE_REAL || b.type == TYPE_REAL) vm.push(mrvmMakeReal(mrvmValueAsReal(a) / mrvmValueAsReal(b)));
					else
						vm.push(mrvmMakeInt(a.i / b.i));
				} break;
				case OP_MOD: {
					Value b = vm.pop();
					Value a = vm.pop();
					if (a.type != TYPE_INT || b.type != TYPE_INT) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
					if (b.i == 0) throw std::runtime_error("Modulo by zero.");
					vm.push(mrvmMakeInt(a.i % b.i));
				} break;
				case OP_NEG: {
					Value a = vm.pop();
					if (!mrvmIsNumeric(a)) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
					if (a.type == TYPE_REAL) vm.push(mrvmMakeReal(-a.r));
					else
						vm.push(mrvmMakeInt(-a.i));
				} break;
				case OP_CMP_EQ:
				case OP_CMP_NE:
				case OP_CMP_LT:
				case OP_CMP_GT:
				case OP_CMP_LE:
				case OP_CMP_GE: {
					Value b = vm.pop();
					Value a = vm.pop();
					int cmp = mrvmCompareValues(a, b);
					int result = 0;
					switch (opcode) {
						case OP_CMP_EQ:
							result = (cmp == 0);
							break;
						case OP_CMP_NE:
							result = (cmp != 0);
							break;
						case OP_CMP_LT:
							result = (cmp < 0);
							break;
						case OP_CMP_GT:
							result = (cmp > 0);
							break;
						case OP_CMP_LE:
							result = (cmp <= 0);
							break;
						case OP_CMP_GE:
							result = (cmp >= 0);
							break;
						default:
							break;
					}
					vm.push(mrvmMakeInt(result));
				} break;
				case OP_AND: {
					Value b = vm.pop();
					Value a = vm.pop();
					vm.push(mrvmMakeInt((mrvmValueAsInt(a) != 0 && mrvmValueAsInt(b) != 0) ? 1 : 0));
				} break;
				case OP_OR: {
					Value b = vm.pop();
					Value a = vm.pop();
					vm.push(mrvmMakeInt((mrvmValueAsInt(a) != 0 || mrvmValueAsInt(b) != 0) ? 1 : 0));
				} break;
				case OP_NOT: {
					Value a = vm.pop();
					vm.push(mrvmMakeInt(mrvmValueAsInt(a) == 0 ? 1 : 0));
				} break;
				case OP_SHL: {
					Value b = vm.pop();
					Value a = vm.pop();
					vm.push(mrvmMakeInt(mrvmValueAsInt(a) << mrvmValueAsInt(b)));
				} break;
				case OP_SHR: {
					Value b = vm.pop();
					Value a = vm.pop();
					vm.push(mrvmMakeInt(mrvmValueAsInt(a) >> mrvmValueAsInt(b)));
				} break;
				case OP_BIT_AND: {
					Value b = vm.pop();
					Value a = vm.pop();
					vm.push(mrvmMakeInt(mrvmValueAsInt(a) & mrvmValueAsInt(b)));
				} break;
				case OP_BIT_OR: {
					Value b = vm.pop();
					Value a = vm.pop();
					vm.push(mrvmMakeInt(mrvmValueAsInt(a) | mrvmValueAsInt(b)));
				} break;
				case OP_BIT_XOR: {
					Value b = vm.pop();
					Value a = vm.pop();
					vm.push(mrvmMakeInt(mrvmValueAsInt(a) ^ mrvmValueAsInt(b)));
				} break;
				case OP_INTRINSIC: {
					std::string name;
					readCString(name);
					unsigned char argc = bytecode[ip++];
					std::vector<Value> args = popArguments(argc);
					vm.push(intrinsics.apply(name, args));
				} break;
				case OP_VAL:
				case OP_RVAL: {
					std::string varName;
					Value source;
					int resultCode = 0;
					readCString(varName);
					source = vm.pop();
					if (!mrvmIsStringLike(source)) throw std::runtime_error(MRConstants::kErrorTypeMismatch);

					std::string textValue = mrvmValueAsString(source);
					if (opcode == OP_VAL) {
						int errorPos = mrvmFindValErrorPosition(textValue);
						if (errorPos == 0) {
							long long parsed = std::strtoll(textValue.c_str(), nullptr, 10);
							if (parsed < static_cast<long long>(std::numeric_limits<int>::min()) || parsed > static_cast<long long>(std::numeric_limits<int>::max())) throw std::runtime_error("Real to Integer conversion out of range.");
							vm.variables[varName] = mrvmMakeInt(static_cast<int>(parsed));
						} else
							resultCode = errorPos;
					} else {
						int errorPos = mrvmFindRValErrorPosition(textValue);
						if (errorPos == 0) {
							char *endPtr = nullptr;
							double parsed = std::strtod(textValue.c_str(), &endPtr);
							(void)endPtr;
							vm.variables[varName] = mrvmMakeReal(parsed);
						} else
							resultCode = errorPos;
					}
					vm.push(mrvmMakeInt(resultCode));
				} break;
				case OP_FIRST_GLOBAL:
				case OP_NEXT_GLOBAL: {
					std::string targetVar;
					readCString(targetVar);
					BackgroundEditSession *session = currentBackgroundEditSession();

					if (session != nullptr) {
						if (opcode == OP_FIRST_GLOBAL) session->globalEnumIndex = 0;
						while (session->globalEnumIndex < session->globalOrder.size()) {
							const std::string &key = session->globalOrder[session->globalEnumIndex++];
							std::map<std::string, GlobalEntry>::const_iterator it = session->globals.find(key);
							if (it == session->globals.end()) continue;
							vm.variables[targetVar] = mrvmMakeInt(it->second.type == TYPE_INT ? 1 : 0);
							vm.push(mrvmMakeString(key));
							goto handledGlobalEnum;
						}
						vm.variables[targetVar] = mrvmMakeInt(0);
						vm.push(mrvmMakeString(""));
						goto handledGlobalEnum;
					}

					if (opcode == OP_FIRST_GLOBAL) setMacroGlobalEnumIndex(0);
					{
						const std::vector<std::string> order = macroGlobalOrderValues();
						std::size_t index = macroGlobalEnumIndex();
						while (index < order.size()) {
							const std::string key = order[index++];
							GlobalEntry entry;
							setMacroGlobalEnumIndex(index);
							if (!readRuntimeGlobalValueDirect(key, entry)) continue;
							vm.variables[targetVar] = mrvmMakeInt(entry.type == TYPE_INT ? 1 : 0);
							vm.push(mrvmMakeString(key));
							goto handledGlobalEnum;
						}
					}
					vm.variables[targetVar] = mrvmMakeInt(0);
					vm.push(mrvmMakeString(""));
				handledGlobalEnum:;
				} break;
				case OP_PROC_VAR: {
					std::string name;
					std::string varName;
					std::string indexVarName;
					unsigned char varArgc = 0;
					std::map<std::string, Value>::iterator it;
					readCString(name);
					varArgc = bytecode[ip++];
					if (varArgc == 0 || varArgc > 2) throw std::runtime_error("Malformed variable procedure call.");
					readCString(varName);
					if (varArgc > 1) readCString(indexVarName);
					it = vm.variables.find(varName);
					if (it == vm.variables.end()) throw std::runtime_error("Variable expected.");
					if (it->second.type != TYPE_STR) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
					switch (MRVMProcedureCatalog::classify(name)) {
						case MRVMProcedure::ExpandTabs: {
							std::string source = mrvmValueAsString(it->second);
							bool toVirtuals = currentRuntimeTabExpand();
							it->second = mrvmMakeString(expandTabsString(source, toVirtuals));
							if (varArgc > 1) {
								std::map<std::string, Value>::iterator indexIt = vm.variables.find(indexVarName);
								if (indexIt == vm.variables.end()) throw std::runtime_error("Variable expected.");
								if (indexIt->second.type != TYPE_INT) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
								indexIt->second = mrvmMakeInt(expandedTabsAdjustedIndex(source, indexIt->second.i));
							}
							break;
						}
						case MRVMProcedure::TabsToSpaces:
							if (varArgc != 1) throw std::runtime_error("TABS_TO_SPACES expects one variable argument.");
							it->second = mrvmMakeString(tabsToSpacesString(mrvmValueAsString(it->second)));
							break;
						default:
							throw std::runtime_error("Unknown variable procedure.");
					}
				} break;
				case OP_PROC: {
					std::string name;
					readCString(name);
					unsigned char argc = bytecode[ip++];
					std::vector<Value> args = popArguments(argc);
					const InstructionFlow flow = ProcedureExecution(vm, *this).execute(name, args, instructionOffset);

					if (flow == InstructionFlow::SkipPostInstruction) continue;
					if (flow == InstructionFlow::FinishExecution) finishExecution = true;
				} break;
				case OP_HALT: {
					vm.appendLogLine("Program end reached.");
					finishExecution = true;
					break;
				} break;
				default: {
					char hexOp[10];
					std::snprintf(hexOp, sizeof(hexOp), "0x%02X", opcode);
					throw std::runtime_error(std::string("Unknown opcode ") + hexOp);
				}
			}

			if (finishExecution) break;

			if (g_backgroundMacroCancelFlag == nullptr && currentBackgroundEditSession() == nullptr) syncLinkedWindowsFrom(activeMacroEditWindow());
			if (vm.debugState.runActive && (vm.debugState.stepMode == mrdStepInto || vm.debugState.stepMode == mrdStepOver || (vm.debugState.stepMode == mrdStepOut && callStack.size() < vm.debugState.stepOutDepth))) {
				vm.debugState.stopped = true;
				vm.debugState.stopReason = mrdStopStep;
				vm.debugState.stopOffset = ip;
				vm.debugState.stackDepth = callStack.size();
				vm.debugState.capturePausedExecution(bytecode, length, ip, callStack, state, savedParameterString, activeMacroName, activeFirstRun);
				vm.debugState.stepMode = mrdStepNone;
				break;
			}
			if (vm.debugState.runActive && vm.debugState.instructionBudget > 0 && --vm.debugState.instructionBudget == 0) {
				vm.debugState.stopped = true;
				vm.debugState.stopReason = mrdStopBudget;
				vm.debugState.stopOffset = ip;
				vm.debugState.stackDepth = callStack.size();
				vm.debugState.capturePausedExecution(bytecode, length, ip, callStack, state, savedParameterString, activeMacroName, activeFirstRun);
				break;
			}
		}
	} catch (const mrvm_execution::DelayYield &yield) {
		int millis = vm.normalizeDelayMillis(yield.millis);
		std::uint64_t generation = vm.delayState.generation + 1;
		vm.appendLogLine("VM Notice: DELAY(" + std::to_string(millis) + ") yielded [gen " + std::to_string(generation) + "].", true);
		vm.delayState.pending = true;
		vm.delayState.ready = true;
		vm.delayState.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(millis);
		vm.delayState.ip = ip;
		vm.delayState.callStack = callStack;
		vm.delayState.returnInt = state.returnInt;
		vm.delayState.returnStr = state.returnStr;
		vm.delayState.errorLevel = state.errorLevel;
		vm.delayState.savedParameterString = savedParameterString;
		vm.delayState.macroFramePushed = pushedMacroFrame;
		vm.delayState.generation = generation;
		vm.delayState.millis = millis;
		if (parentState != nullptr) {
			parentState->returnInt = state.returnInt;
			parentState->returnStr = state.returnStr;
			parentState->errorLevel = state.errorLevel;
			parentState->parameterString = savedParameterString;
		} else {
			g_runtimeEnv.returnInt = state.returnInt;
			g_runtimeEnv.returnStr = state.returnStr;
			g_runtimeEnv.errorLevel = state.errorLevel;
			g_runtimeEnv.parameterString = savedParameterString;
		}
		return;
	} catch (const std::exception &ex) {
		vm.appendLogLine(std::string("VM Error: ") + ex.what(), true);
	}

	if (resumeFromDelay && resumeGeneration != vm.delayState.generation) vm.appendLogLine("VM Notice: stale DELAY resume generation ignored.", true);

	if (parentState != nullptr) {
		parentState->returnInt = state.returnInt;
		parentState->returnStr = state.returnStr;
		parentState->errorLevel = state.errorLevel;
		parentState->parameterString = savedParameterString;
	} else {
		g_runtimeEnv.returnInt = state.returnInt;
		g_runtimeEnv.returnStr = state.returnStr;
		g_runtimeEnv.errorLevel = state.errorLevel;
		g_runtimeEnv.parameterString = savedParameterString;
	}
	vm.clearAsyncDelayState();
	if (vm.debugState.runActive && !vm.debugState.stopped) {
		vm.debugState.stopOffset = ip;
		vm.debugState.stackDepth = callStack.size();
		vm.debugState.clearPausedExecution();
	}
	if (pushedMacroFrame) g_runtimeEnv.macroStack.pop_back();
}
