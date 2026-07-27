#include "MRVMExecutionInternal.hpp"

#include "MRVMExecSessions.hpp"
#include "MRVMHash.hpp"
#include "MRVMIntrinsics.hpp"
#include "MRVMProcedureCatalog.hpp"
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
	std::lock_guard<std::recursive_mutex> executionLock(g_vmExecutionMutex);
	bool resumeFromDebug = (bytecode == nullptr && length == 0 && mDebugPaused && !mDebugBytecode.empty() && mDebugIp <= mDebugLength);
	bool resumeFromDelay = (!resumeFromDebug && bytecode == nullptr && length == 0 && mAsyncDelayPending && mAsyncDelayReady && !mAsyncBytecode.empty() && mAsyncIp <= mAsyncLength);
	std::uint64_t resumeGeneration = mAsyncDelayGeneration;
	size_t ip = resumeFromDebug ? mDebugIp : (resumeFromDelay ? mAsyncIp : entryOffset);
	std::vector<size_t> callStack;
	ExecutionState state;
	ExecutionState *parentState = currentExecutionState();
	std::string savedParameterString;
	std::string activeMacroName;
	bool activeFirstRun = false;
	bool pushedMacroFrame = false;
	bool allowAsyncDelay = false;
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
	} executionSessionGuard(mExecutionSessionId != 0 ? mExecutionSessionId : g_executionSessionId);

	if (resumeFromDebug) {
		bytecode = mDebugBytecode.data();
		length = mDebugLength;
		callStack = mDebugCallStack;
		savedParameterString = mDebugSavedParameterString;
		state.parameterString = mDebugSavedParameterString;
		state.returnInt = mDebugReturnInt;
		state.returnStr = mDebugReturnStr;
		state.errorLevel = mDebugErrorLevel;
		activeMacroName = mDebugMacroName;
		activeFirstRun = mDebugFirstRun;
		mDebugPaused = false;
		if (!activeMacroName.empty()) {
			g_runtimeEnv.macroStack.emplace_back(activeMacroName, activeFirstRun);
			pushedMacroFrame = true;
		}
	} else if (resumeFromDelay) {
		bytecode = mAsyncBytecode.data();
		length = mAsyncLength;
		callStack = mAsyncCallStack;
		savedParameterString = mAsyncSavedParameterString;
		state.parameterString = mAsyncSavedParameterString;
		state.returnInt = mAsyncReturnInt;
		state.returnStr = mAsyncReturnStr;
		state.errorLevel = mAsyncErrorLevel;
		pushedMacroFrame = mAsyncMacroFramePushed;
		mAsyncDelayReady = false;
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
			variables.clear();
			mSessionVariableNames.clear();
			mHashStore->clearExceptRoots(currentGlobalHashRoots());
			cancelledExecution = false;
		}
		stack.clear();
		if (resetState) {
			log.clear();
			logTruncated = false;
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
	allowAsyncDelay = (mAsyncDelayEnabled && parentState == nullptr && currentBackgroundEditSession() == nullptr && g_backgroundMacroCancelFlag == nullptr);
	if (allowAsyncDelay && !resumeFromDelay) {
		mAsyncBytecode.assign(bytecode, bytecode + length);
		mAsyncLength = length;
	}
	ExecutionFrame frame{bytecode, length, ip, callStack, state, savedParameterString, activeMacroName, activeFirstRun, allowAsyncDelay};
	MRVMIntrinsics intrinsics(*this);

	auto readInt = [&](int &value) {
		std::memcpy(&value, &bytecode[ip], sizeof(int));
		ip += sizeof(int);
	};

	auto readDouble = [&](double &value) {
		std::memcpy(&value, &bytecode[ip], sizeof(double));
		ip += sizeof(double);
	};

	auto readCString = [&](std::string &value) {
		const char *textp = reinterpret_cast<const char *>(&bytecode[ip]);
		value = textp;
		ip += value.size() + 1;
	};

	auto popArgs = [&](unsigned char count) {
		std::vector<Value> args;
		args.reserve(count);
		for (unsigned char i = 0; i < count; ++i)
			args.push_back(pop());
		std::reverse(args.begin(), args.end());
		return args;
	};

	try {
		while (ip < length) {
			if (mDebugRunActive && mDebugPauseRequested) {
				mDebugStopped = true;
				mDebugStopReason = mrdStopPaused;
				mDebugStopOffset = ip;
				mDebugStackDepth = callStack.size();
				mDebugPaused = true;
				mDebugBytecode.assign(bytecode, bytecode + length);
				mDebugLength = length;
				mDebugIp = ip;
				mDebugCallStack = callStack;
				mDebugReturnInt = state.returnInt;
				mDebugReturnStr = state.returnStr;
				mDebugErrorLevel = state.errorLevel;
				mDebugSavedParameterString = savedParameterString;
				mDebugMacroName = activeMacroName;
				mDebugFirstRun = activeFirstRun;
				mDebugPauseRequested = false;
				break;
			}
			if (backgroundMacroCancelRequested()) {
				cancelledExecution = true;
				appendLogLine("VM Notice: Background macro cancelled.", true);
				runtimeErrorLevel() = 5007;
				break;
			}
			if (mDebugRunActive && std::binary_search(mDebugBreakpointOffsets.begin(), mDebugBreakpointOffsets.end(), ip)) {
				if (mDebugSkipCurrentOffset && ip == mDebugStopOffset) {
					mDebugSkipCurrentOffset = false;
				} else {
					mDebugStopped = true;
					mDebugStopReason = mrdStopBreakpoint;
					mDebugStopOffset = ip;
					mDebugStackDepth = callStack.size();
					mDebugPaused = true;
					mDebugBytecode.assign(bytecode, bytecode + length);
					mDebugLength = length;
					mDebugIp = ip;
					mDebugCallStack = callStack;
					mDebugReturnInt = state.returnInt;
					mDebugReturnStr = state.returnStr;
					mDebugErrorLevel = state.errorLevel;
					mDebugSavedParameterString = savedParameterString;
					mDebugMacroName = activeMacroName;
					mDebugFirstRun = activeFirstRun;
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
					push(mrvmMakeInt(val));
					appendLogLine("Push integer: " + std::to_string(val));
				} break;
				case OP_PUSH_R: {
					double val;
					readDouble(val);
					push(mrvmMakeReal(val));
					appendLogLine("Push real: " + mrvmValueAsString(mrvmMakeReal(val)));
				} break;
				case OP_PUSH_S: {
					std::string str;
					readCString(str);
					mrvmEnforceStringLength(str);
					push(mrvmMakeString(str));
					appendLogLine("Push string: " + str);
				} break;
				case OP_DEF_VAR: {
					std::string varName;
					int varType = static_cast<int>(bytecode[ip++]);
					Value value;
					readCString(varName);
					if (!mClosureId.empty()) {
						bool restored = false;
						mClosureVariableNames.insert(varName);
						if (mrvmExecSessionsReadClosureVariable(g_runtimeEnv.runtimeKv, mClosureId, varName, value)) {
							variables[varName] = mrvmCoerceForStore(value, varType);
							restored = true;
						} else if (varType == TYPE_HASH)
							variables[varName] = mrvmMakeHash(mHashStore->createHash());
						else
							variables[varName] = mrvmDefaultValueForType(varType);
						if (!restored) static_cast<void>(mrvmExecSessionsWriteClosureVariable(g_runtimeEnv.runtimeKv, mClosureId, varName, variables[varName], *mHashStore));
					} else if (currentExecutionSessionId() != 0) {
						bool restored = false;
						mSessionVariableNames.insert(varName);
						if (mrvmExecSessionsReadSessionVariable(g_runtimeEnv.runtimeKv, currentExecutionSessionId(), varName, value)) {
							variables[varName] = mrvmCoerceForStore(value, varType);
							restored = true;
						} else if (varType == TYPE_HASH)
							variables[varName] = mrvmMakeHash(mHashStore->createHash());
						else
							variables[varName] = mrvmDefaultValueForType(varType);
						if (!restored) static_cast<void>(mrvmExecSessionsWriteSessionVariable(g_runtimeEnv.runtimeKv, currentExecutionSessionId(), varName, variables[varName], *mHashStore));
					} else if (varType == TYPE_HASH)
						variables[varName] = mrvmMakeHash(mHashStore->createHash());
					else
						variables[varName] = mrvmDefaultValueForType(varType);
					appendLogLine("Define variable: " + varName);
				} break;
				case OP_LOAD_VAR: {
					std::string varName;
					bool handled = false;
					readCString(varName);

					Value special = MRVMSystemVariables::load(varName, handled);
					if (handled) push(special);
					else {
						std::map<std::string, Value>::const_iterator it = variables.find(varName);
						if (it == variables.end()) variables[varName] = mrvmMakeInt(0);
						push(variables[varName]);
					}
					appendLogLine("Load variable: " + varName);
				} break;
				case OP_STORE_VAR: {
					std::string varName;
					int targetType = static_cast<int>(bytecode[ip++]);
					readCString(varName);
					Value value = mrvmCoerceForStore(pop(), targetType);
					if (value.type == TYPE_STR) mrvmEnforceStringLength(value.s);
					if (!MRVMSystemVariables::store(varName, value)) variables[varName] = value;
					if (!mClosureId.empty() && mClosureVariableNames.find(varName) != mClosureVariableNames.end()) mrvmExecSessionsWriteClosureVariable(g_runtimeEnv.runtimeKv, mClosureId, varName, value, *mHashStore);
					else if (currentExecutionSessionId() != 0 && mSessionVariableNames.find(varName) != mSessionVariableNames.end())
						mrvmExecSessionsWriteSessionVariable(g_runtimeEnv.runtimeKv, currentExecutionSessionId(), varName, value, *mHashStore);
					appendLogLine("Store variable: " + varName);
				} break;
				case OP_HASH_LOAD: {
					std::string varName;
					Value key;
					std::map<std::string, Value>::const_iterator it;
					readCString(varName);
					key = pop();
					if (!mrvmIsStringLike(key)) throw std::runtime_error("type mismatch");
					it = variables.find(varName);
					if (it == variables.end() || it->second.type != TYPE_HASH) throw std::runtime_error("Invalid hash value.");
					push(mrvmHashReadValue(*mHashStore, g_runtimeEnv.runtimeKv.globalStore(), it->second, mrvmValueAsString(key)));
					appendLogLine("Load hash value: " + varName);
				} break;
				case OP_HASH_LOAD_VALUE: {
					Value key;
					Value hash;
					key = pop();
					hash = pop();
					if (hash.type != TYPE_HASH) throw std::runtime_error("Invalid hash value.");
					if (!mrvmIsStringLike(key)) throw std::runtime_error("type mismatch");
					push(mrvmHashReadValue(*mHashStore, g_runtimeEnv.runtimeKv.globalStore(), hash, mrvmValueAsString(key)));
					appendLogLine("Load hash value from expression.");
				} break;
				case OP_HASH_STORE: {
					std::string varName;
					Value value;
					Value key;
					std::map<std::string, Value>::const_iterator it;
					readCString(varName);
					value = pop();
					key = pop();
					if (!mrvmIsStringLike(key)) throw std::runtime_error("type mismatch");
					it = variables.find(varName);
					if (it == variables.end() || it->second.type != TYPE_HASH) throw std::runtime_error("Invalid hash value.");
					if (value.type == TYPE_STR) mrvmEnforceStringLength(value.s);
					mrvmHashWriteValue(*mHashStore, g_runtimeEnv.runtimeKv.globalStore(), it->second, mrvmValueAsString(key), value);
					if (!mClosureId.empty() && mClosureVariableNames.find(varName) != mClosureVariableNames.end()) mrvmExecSessionsWriteClosureVariable(g_runtimeEnv.runtimeKv, mClosureId, varName, it->second, *mHashStore);
					else if (currentExecutionSessionId() != 0 && mSessionVariableNames.find(varName) != mSessionVariableNames.end())
						mrvmExecSessionsWriteSessionVariable(g_runtimeEnv.runtimeKv, currentExecutionSessionId(), varName, it->second, *mHashStore);
					appendLogLine("Store hash value: " + varName);
				} break;
				case OP_HASH_STORE_VALUE: {
					Value value;
					Value key;
					Value hash;
					value = pop();
					key = pop();
					hash = pop();
					if (hash.type != TYPE_HASH) throw std::runtime_error("Invalid hash value.");
					if (!mrvmIsStringLike(key)) throw std::runtime_error("type mismatch");
					if (value.type == TYPE_STR) mrvmEnforceStringLength(value.s);
					mrvmHashWriteValue(*mHashStore, g_runtimeEnv.runtimeKv.globalStore(), hash, mrvmValueAsString(key), value);
					appendLogLine("Store hash value from expression.");
				} break;
				case OP_ARRAY_LOAD: {
					std::string varName;
					Value index;
					std::map<std::string, Value>::const_iterator it;
					readCString(varName);
					index = pop();
					if (index.type != TYPE_INT) throw std::runtime_error("type mismatch");
					it = variables.find(varName);
					if (it == variables.end() || !mrvmValueIsArrayType(it->second.type)) throw std::runtime_error("Invalid array value.");
					push(mrvmArrayReadValue(it->second, index.i));
					appendLogLine("Load array value: " + varName);
				} break;
				case OP_ARRAY_LOAD_VALUE: {
					Value index;
					Value arrayValue;
					index = pop();
					arrayValue = pop();
					if (index.type != TYPE_INT) throw std::runtime_error("type mismatch");
					push(mrvmArrayReadValue(arrayValue, index.i));
					appendLogLine("Load array value from expression.");
				} break;
				case OP_ARRAY_STORE: {
					std::string varName;
					Value value;
					Value index;
					std::map<std::string, Value>::iterator it;
					readCString(varName);
					value = pop();
					index = pop();
					if (index.type != TYPE_INT) throw std::runtime_error("type mismatch");
					it = variables.find(varName);
					if (it == variables.end() || !mrvmValueIsArrayType(it->second.type)) throw std::runtime_error("Invalid array value.");
					mrvmArrayWriteValue(it->second, index.i, value, *mHashStore, g_runtimeEnv.runtimeKv.globalStore());
					if (!mClosureId.empty() && mClosureVariableNames.find(varName) != mClosureVariableNames.end()) mrvmExecSessionsWriteClosureVariable(g_runtimeEnv.runtimeKv, mClosureId, varName, it->second, *mHashStore);
					else if (currentExecutionSessionId() != 0 && mSessionVariableNames.find(varName) != mSessionVariableNames.end())
						mrvmExecSessionsWriteSessionVariable(g_runtimeEnv.runtimeKv, currentExecutionSessionId(), varName, it->second, *mHashStore);
					appendLogLine("Store array value: " + varName);
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
					cond = pop();
					if (cond.type != TYPE_INT) throw std::runtime_error("IF/WHILE expression must be integer.");
					if (target < 0 || static_cast<size_t>(target) >= length) throw std::runtime_error("Invalid jump target in JZ.");
					if (cond.i == 0) ip = static_cast<size_t>(target);
				} break;
				case OP_ADD: {
					Value b = pop();
					Value a = pop();
					if (mrvmIsStringLike(a) && mrvmIsStringLike(b)) {
						std::string s = mrvmValueAsString(a) + mrvmValueAsString(b);
						mrvmEnforceStringLength(s);
						push(mrvmMakeString(s));
					} else if (mrvmIsNumeric(a) && mrvmIsNumeric(b)) {
						if (a.type == TYPE_REAL || b.type == TYPE_REAL) push(mrvmMakeReal(mrvmValueAsReal(a) + mrvmValueAsReal(b)));
						else
							push(mrvmMakeInt(a.i + b.i));
					} else
						throw std::runtime_error(MRConstants::kErrorTypeMismatch);
				} break;
				case OP_SUB: {
					Value b = pop();
					Value a = pop();
					if (!mrvmIsNumeric(a) || !mrvmIsNumeric(b)) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
					if (a.type == TYPE_REAL || b.type == TYPE_REAL) push(mrvmMakeReal(mrvmValueAsReal(a) - mrvmValueAsReal(b)));
					else
						push(mrvmMakeInt(a.i - b.i));
				} break;
				case OP_MUL: {
					Value b = pop();
					Value a = pop();
					if (!mrvmIsNumeric(a) || !mrvmIsNumeric(b)) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
					if (a.type == TYPE_REAL || b.type == TYPE_REAL) push(mrvmMakeReal(mrvmValueAsReal(a) * mrvmValueAsReal(b)));
					else
						push(mrvmMakeInt(a.i * b.i));
				} break;
				case OP_DIV: {
					Value b = pop();
					Value a = pop();
					if (!mrvmIsNumeric(a) || !mrvmIsNumeric(b)) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
					if ((b.type == TYPE_REAL && b.r == 0.0) || (b.type == TYPE_INT && b.i == 0)) throw std::runtime_error("Division by zero.");
					if (a.type == TYPE_REAL || b.type == TYPE_REAL) push(mrvmMakeReal(mrvmValueAsReal(a) / mrvmValueAsReal(b)));
					else
						push(mrvmMakeInt(a.i / b.i));
				} break;
				case OP_MOD: {
					Value b = pop();
					Value a = pop();
					if (a.type != TYPE_INT || b.type != TYPE_INT) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
					if (b.i == 0) throw std::runtime_error("Modulo by zero.");
					push(mrvmMakeInt(a.i % b.i));
				} break;
				case OP_NEG: {
					Value a = pop();
					if (!mrvmIsNumeric(a)) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
					if (a.type == TYPE_REAL) push(mrvmMakeReal(-a.r));
					else
						push(mrvmMakeInt(-a.i));
				} break;
				case OP_CMP_EQ:
				case OP_CMP_NE:
				case OP_CMP_LT:
				case OP_CMP_GT:
				case OP_CMP_LE:
				case OP_CMP_GE: {
					Value b = pop();
					Value a = pop();
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
					push(mrvmMakeInt(result));
				} break;
				case OP_AND: {
					Value b = pop();
					Value a = pop();
					push(mrvmMakeInt((mrvmValueAsInt(a) != 0 && mrvmValueAsInt(b) != 0) ? 1 : 0));
				} break;
				case OP_OR: {
					Value b = pop();
					Value a = pop();
					push(mrvmMakeInt((mrvmValueAsInt(a) != 0 || mrvmValueAsInt(b) != 0) ? 1 : 0));
				} break;
				case OP_NOT: {
					Value a = pop();
					push(mrvmMakeInt(mrvmValueAsInt(a) == 0 ? 1 : 0));
				} break;
				case OP_SHL: {
					Value b = pop();
					Value a = pop();
					push(mrvmMakeInt(mrvmValueAsInt(a) << mrvmValueAsInt(b)));
				} break;
				case OP_SHR: {
					Value b = pop();
					Value a = pop();
					push(mrvmMakeInt(mrvmValueAsInt(a) >> mrvmValueAsInt(b)));
				} break;
				case OP_BIT_AND: {
					Value b = pop();
					Value a = pop();
					push(mrvmMakeInt(mrvmValueAsInt(a) & mrvmValueAsInt(b)));
				} break;
				case OP_BIT_OR: {
					Value b = pop();
					Value a = pop();
					push(mrvmMakeInt(mrvmValueAsInt(a) | mrvmValueAsInt(b)));
				} break;
				case OP_BIT_XOR: {
					Value b = pop();
					Value a = pop();
					push(mrvmMakeInt(mrvmValueAsInt(a) ^ mrvmValueAsInt(b)));
				} break;
				case OP_INTRINSIC: {
					std::string name;
					readCString(name);
					unsigned char argc = bytecode[ip++];
					std::vector<Value> args = popArgs(argc);
					push(intrinsics.apply(name, args));
				} break;
				case OP_VAL:
				case OP_RVAL: {
					std::string varName;
					Value source;
					int resultCode = 0;
					readCString(varName);
					source = pop();
					if (!mrvmIsStringLike(source)) throw std::runtime_error(MRConstants::kErrorTypeMismatch);

					std::string textValue = mrvmValueAsString(source);
					if (opcode == OP_VAL) {
						int errorPos = mrvmFindValErrorPosition(textValue);
						if (errorPos == 0) {
							long long parsed = std::strtoll(textValue.c_str(), nullptr, 10);
							if (parsed < static_cast<long long>(std::numeric_limits<int>::min()) || parsed > static_cast<long long>(std::numeric_limits<int>::max())) throw std::runtime_error("Real to Integer conversion out of range.");
							variables[varName] = mrvmMakeInt(static_cast<int>(parsed));
						} else
							resultCode = errorPos;
					} else {
						int errorPos = mrvmFindRValErrorPosition(textValue);
						if (errorPos == 0) {
							char *endPtr = nullptr;
							double parsed = std::strtod(textValue.c_str(), &endPtr);
							(void)endPtr;
							variables[varName] = mrvmMakeReal(parsed);
						} else
							resultCode = errorPos;
					}
					push(mrvmMakeInt(resultCode));
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
							variables[targetVar] = mrvmMakeInt(it->second.type == TYPE_INT ? 1 : 0);
							push(mrvmMakeString(key));
							goto handledGlobalEnum;
						}
						variables[targetVar] = mrvmMakeInt(0);
						push(mrvmMakeString(""));
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
							variables[targetVar] = mrvmMakeInt(entry.type == TYPE_INT ? 1 : 0);
							push(mrvmMakeString(key));
							goto handledGlobalEnum;
						}
					}
					variables[targetVar] = mrvmMakeInt(0);
					push(mrvmMakeString(""));
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
					it = variables.find(varName);
					if (it == variables.end()) throw std::runtime_error("Variable expected.");
					if (it->second.type != TYPE_STR) throw std::runtime_error(MRConstants::kErrorTypeMismatch);
					switch (MRVMProcedureCatalog::classify(name)) {
						case MRVMProcedure::ExpandTabs: {
							std::string source = mrvmValueAsString(it->second);
							bool toVirtuals = currentRuntimeTabExpand();
							it->second = mrvmMakeString(expandTabsString(source, toVirtuals));
							if (varArgc > 1) {
								std::map<std::string, Value>::iterator indexIt = variables.find(indexVarName);
								if (indexIt == variables.end()) throw std::runtime_error("Variable expected.");
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
					std::vector<Value> args = popArgs(argc);
					const InstructionFlow flow = executeProcedure(frame, name, args, instructionOffset);

					if (flow == InstructionFlow::SkipPostInstruction) continue;
					if (flow == InstructionFlow::FinishExecution) finishExecution = true;
				} break;
				case OP_HALT: {
					appendLogLine("Program end reached.");
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
			if (mDebugRunActive && (mDebugStepMode == mrdStepInto || mDebugStepMode == mrdStepOver || (mDebugStepMode == mrdStepOut && callStack.size() < mDebugStepOutDepth))) {
				mDebugStopped = true;
				mDebugStopReason = mrdStopStep;
				mDebugStopOffset = ip;
				mDebugStackDepth = callStack.size();
				mDebugPaused = true;
				mDebugBytecode.assign(bytecode, bytecode + length);
				mDebugLength = length;
				mDebugIp = ip;
				mDebugCallStack = callStack;
				mDebugReturnInt = state.returnInt;
				mDebugReturnStr = state.returnStr;
				mDebugErrorLevel = state.errorLevel;
				mDebugSavedParameterString = savedParameterString;
				mDebugMacroName = activeMacroName;
				mDebugFirstRun = activeFirstRun;
				mDebugStepMode = mrdStepNone;
				break;
			}
			if (mDebugRunActive && mDebugInstructionBudget > 0 && --mDebugInstructionBudget == 0) {
				mDebugStopped = true;
				mDebugStopReason = mrdStopBudget;
				mDebugStopOffset = ip;
				mDebugStackDepth = callStack.size();
				mDebugPaused = true;
				mDebugBytecode.assign(bytecode, bytecode + length);
				mDebugLength = length;
				mDebugIp = ip;
				mDebugCallStack = callStack;
				mDebugReturnInt = state.returnInt;
				mDebugReturnStr = state.returnStr;
				mDebugErrorLevel = state.errorLevel;
				mDebugSavedParameterString = savedParameterString;
				mDebugMacroName = activeMacroName;
				mDebugFirstRun = activeFirstRun;
				break;
			}
		}
	} catch (const mrvm_execution::DelayYield &yield) {
		int millis = normalizeDelayMillis(yield.millis);
		std::uint64_t generation = mAsyncDelayGeneration + 1;
		appendLogLine("VM Notice: DELAY(" + std::to_string(millis) + ") yielded [gen " + std::to_string(generation) + "].", true);
		mAsyncDelayPending = true;
		mAsyncDelayReady = true;
		mAsyncDelayDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(millis);
		mAsyncIp = ip;
		mAsyncCallStack = callStack;
		mAsyncReturnInt = state.returnInt;
		mAsyncReturnStr = state.returnStr;
		mAsyncErrorLevel = state.errorLevel;
		mAsyncSavedParameterString = savedParameterString;
		mAsyncMacroFramePushed = pushedMacroFrame;
		mAsyncDelayGeneration = generation;
		mAsyncDelayMillis = millis;
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
		appendLogLine(std::string("VM Error: ") + ex.what(), true);
	}

	if (resumeFromDelay && resumeGeneration != mAsyncDelayGeneration) appendLogLine("VM Notice: stale DELAY resume generation ignored.", true);

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
	clearAsyncDelayState();
	if (mDebugRunActive && !mDebugStopped) {
		mDebugPaused = false;
		mDebugStopOffset = ip;
		mDebugStackDepth = callStack.size();
		mDebugBytecode.clear();
		mDebugLength = 0;
		mDebugIp = 0;
		mDebugCallStack.clear();
		mDebugReturnInt = 0;
		mDebugReturnStr.clear();
		mDebugErrorLevel = 0;
		mDebugSavedParameterString.clear();
		mDebugMacroName.clear();
		mDebugFirstRun = false;
		mDebugSkipCurrentOffset = false;
		mDebugPauseRequested = false;
		mDebugInstructionBudget = 0;
		mDebugStepMode = mrdStepNone;
	}
	if (pushedMacroFrame) g_runtimeEnv.macroStack.pop_back();
}
