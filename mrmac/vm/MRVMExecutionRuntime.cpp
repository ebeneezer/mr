#include "MRVMRuntimeState.hpp"
#include "MRVMValue.hpp"

#include "../mrmac.h"

#include <algorithm>
#include <utility>

namespace {
static void appendUniqueString(std::vector<std::string> &values, const std::string &value) {
	if (value.empty()) return;
	if (std::find(values.begin(), values.end(), value) == values.end()) values.push_back(value);
}

struct MRVMStagedExecutionContext {
	const MRMacroStagedExecutionInput &input;
	std::shared_ptr<std::atomic_bool> cancelFlag;
	BackgroundEditSession session;

	struct InstallGuard {
		BackgroundEditSession *previousSession;
		std::shared_ptr<std::atomic_bool> previousCancelFlag;

		explicit InstallGuard(MRVMStagedExecutionContext &context) noexcept
		    : previousSession(g_backgroundEditSession), previousCancelFlag(g_backgroundMacroCancelFlag) {
			g_backgroundEditSession = &context.session;
			g_backgroundMacroCancelFlag = context.cancelFlag;
		}

		~InstallGuard() {
			g_backgroundEditSession = previousSession;
			g_backgroundMacroCancelFlag = previousCancelFlag;
		}
	};

	MRVMStagedExecutionContext(const MRMacroStagedExecutionInput &stagedInput, std::shared_ptr<std::atomic_bool> stagedCancelFlag)
	    : input(stagedInput), cancelFlag(std::move(stagedCancelFlag)), session() {
		if (cancelFlag == nullptr) cancelFlag = std::make_shared<std::atomic_bool>(false);
	}

	void initializeSession() {
		session.document = input.document;
		session.transaction = mr::editor::StagedEditTransaction(input.baseVersion, "macro-staged-write");
		session.cursorOffset = input.cursorOffset;
		session.selectionStart = input.selectionStart;
		session.selectionEnd = input.selectionEnd;
		session.blockMode = input.blockMode;
		session.blockMarkingOn = input.blockMarkingOn;
		session.blockAnchor = input.blockAnchor;
		session.blockEnd = input.blockEnd;
		session.firstSave = input.firstSave;
		session.eofInMemory = input.eofInMemory;
		session.bufferId = input.bufferId;
		session.temporaryFile = input.temporaryFile;
		session.temporaryFileName = input.temporaryFileName;
		session.currentWindow = input.currentWindow;
		session.linkStatus = input.linkStatus;
		session.windowCount = input.windowCount;
		session.windowGeometryValid = input.windowGeometryValid;
		session.windowX1 = input.windowX1;
		session.windowY1 = input.windowY1;
		session.windowX2 = input.windowX2;
		session.windowY2 = input.windowY2;
		session.globals.clear();
		session.globalOrder.clear();
		session.globalEnumIndex = 0;
		for (const auto &i : input.globalOrder)
			appendUniqueString(session.globalOrder, mrvmUpperKey(i));
		for (const auto &globalInt : input.globalInts) {
			GlobalEntry entry;
			std::string key = mrvmUpperKey(globalInt.first);
			entry.type = TYPE_INT;
			entry.value = mrvmMakeInt(globalInt.second);
			if (session.globals.find(key) == session.globals.end()) appendUniqueString(session.globalOrder, key);
			session.globals[key] = entry;
		}
		for (const auto &globalString : input.globalStrings) {
			GlobalEntry entry;
			std::string key = mrvmUpperKey(globalString.first);
			entry.type = TYPE_STR;
			entry.value = mrvmMakeString(globalString.second);
			if (session.globals.find(key) == session.globals.end()) appendUniqueString(session.globalOrder, key);
			session.globals[key] = entry;
		}
		session.loadedMacroDisplayNames.clear();
		session.macroOrder.clear();
		session.macroEnumIndex = 0;
		session.deferredUiCommands.clear();
		for (const auto &i : input.macroOrder)
			appendUniqueString(session.macroOrder, mrvmUpperKey(i));
		for (const auto &macroDisplayName : input.macroDisplayNames) {
			std::string key = mrvmUpperKey(macroDisplayName.first);
			session.loadedMacroDisplayNames[key] = macroDisplayName.second;
			if (std::find(session.macroOrder.begin(), session.macroOrder.end(), key) == session.macroOrder.end()) session.macroOrder.push_back(key);
		}
		session.lastSearchValid = input.lastSearchValid;
		session.lastSearchStart = input.lastSearchStart;
		session.lastSearchEnd = input.lastSearchEnd;
		session.lastSearchCursor = input.lastSearchCursor;
		session.ignoreCase = input.ignoreCase;
		session.tabExpand = input.tabExpand;
		session.markStack.clear();
		for (unsigned long i : input.markStack)
			session.markStack.push_back(static_cast<unsigned int>(i));
		session.insertMode = input.insertMode;
		session.indentLevel = input.indentLevel;
		session.pageLines = std::max(1, input.pageLines);
		session.fileName = input.fileName;
		session.fileChanged = input.fileChanged;
		session.screenWidth = input.screenWidth;
		session.screenHeight = input.screenHeight;
		session.screenCursorX = input.screenCursorX;
		session.screenCursorY = input.screenCursorY;
		session.clampState();
	}

	MRMacroStagedJobResult buildResult(const VirtualMachine &vm) const {
		MRMacroStagedJobResult result;

		result.logLines = vm.log;
		result.cancelled = vm.wasCancelled();
		for (std::size_t i = 0; i < result.logLines.size(); ++i) {
			if (result.logLines[i].rfind("VM Error:", 0) == 0) {
				result.hadError = true;
				break;
			}
		}
		result.transaction = session.transaction;
		result.cursorOffset = session.cursorOffset;
		result.selectionStart = session.selectionStart;
		result.selectionEnd = session.selectionEnd;
		result.blockMode = session.blockMode;
		result.blockMarkingOn = session.blockMarkingOn;
		result.blockAnchor = session.blockAnchor;
		result.blockEnd = session.blockEnd;
		result.globalOrder.clear();
		result.globalInts.clear();
		result.globalStrings.clear();
		result.globalOrder.reserve(session.globalOrder.size());
		for (std::size_t i = 0; i < session.globalOrder.size(); ++i) {
			const std::string &key = session.globalOrder[i];
			std::map<std::string, GlobalEntry>::const_iterator it = session.globals.find(key);
			if (it == session.globals.end()) continue;
			result.globalOrder.push_back(key);
			if (it->second.type == TYPE_INT) result.globalInts[key] = mrvmValueAsInt(it->second.value);
			else if (it->second.type == TYPE_STR)
				result.globalStrings[key] = mrvmValueAsString(it->second.value);
		}
		result.macroOrder = session.macroOrder;
		result.macroDisplayNames = session.loadedMacroDisplayNames;
		result.deferredUiCommands = session.deferredUiCommands;
		result.lastSearchValid = session.lastSearchValid;
		result.lastSearchStart = session.lastSearchStart;
		result.lastSearchEnd = session.lastSearchEnd;
		result.lastSearchCursor = session.lastSearchCursor;
		result.ignoreCase = session.ignoreCase;
		result.tabExpand = session.tabExpand;
		result.markStack.reserve(session.markStack.size());
		for (unsigned int i : session.markStack)
			result.markStack.push_back(static_cast<std::size_t>(i));
		result.insertMode = session.insertMode;
		result.indentLevel = session.indentLevel;
		result.fileName = session.fileName;
		result.fileChanged = session.fileChanged;
		return result;
	}
};
}

MRMacroJobResult mrvmRunBytecodeBackgroundAt(const unsigned char *bytecode, std::size_t length, std::size_t entryOffset, const std::string &macroName, const std::string &closureId, MRMacroExecutionSessionId sessionId, std::shared_ptr<std::atomic_bool> cancelFlag) {
	MRMacroJobResult result;
	VirtualMachine vm;
	struct CancelGuard {
		std::shared_ptr<std::atomic_bool> savedFlag;

		explicit CancelGuard(std::shared_ptr<std::atomic_bool> flag) : savedFlag(g_backgroundMacroCancelFlag) {
			if (flag == nullptr) flag = std::make_shared<std::atomic_bool>(false);
			g_backgroundMacroCancelFlag = std::move(flag);
		}

		~CancelGuard() {
			g_backgroundMacroCancelFlag = savedFlag;
		}
	} cancelGuard(std::move(cancelFlag));

	vm.setVerboseLogging(false);
	vm.setExecutionSessionContext(sessionId);
	if (!closureId.empty()) vm.setClosureContext(closureId);
	vm.executeAt(bytecode, length, entryOffset, std::string(), macroName, true, false);
	result.logLines = vm.log;
	result.execUiCommandRequests = vm.execUiCommandRequests();
	result.cancelled = vm.wasCancelled();
	for (std::size_t i = 0; i < result.logLines.size(); ++i) {
		if (result.logLines[i].rfind("VM Error:", 0) == 0) {
			result.hadError = true;
			break;
		}
	}
	return result;
}

MRMacroJobResult mrvmRunBytecodeBackground(const unsigned char *bytecode, std::size_t length, std::shared_ptr<std::atomic_bool> cancelFlag) {
	return mrvmRunBytecodeBackgroundAt(bytecode, length, 0, std::string(), std::string(), 0, std::move(cancelFlag));
}

MRMacroStagedJobResult mrvmRunBytecodeStagedBackground(const unsigned char *bytecode, std::size_t length, const MRMacroStagedExecutionInput &input, MRMacroExecutionSessionId sessionId, std::shared_ptr<std::atomic_bool> cancelFlag) {
	VirtualMachine vm;
	MRVMStagedExecutionContext context(input, std::move(cancelFlag));

	context.initializeSession();
	MRVMStagedExecutionContext::InstallGuard installGuard(context);
	vm.setVerboseLogging(false);
	vm.setExecutionSessionContext(sessionId);
	vm.execute(bytecode, length);
	return context.buildResult(vm);
}
