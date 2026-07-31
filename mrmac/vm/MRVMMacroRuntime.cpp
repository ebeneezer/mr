#include "MRVMRuntimeInternal.hpp"

#include "MRVMExecSessions.hpp"
#include "MRVMHash.hpp"
#include "MRVMKeymapRuntime.hpp"
#include "MRVMMacroSpecRuntime.hpp"
#include "MRVMProcessRuntime.hpp"
#include "MRVMRuntimeCatalog.hpp"
#include "MRVMRuntimeGlobals.hpp"
#include "MRVMRuntimeKv.hpp"
#include "MRVMValue.hpp"

#include "../MRMacroRunner.hpp"
#include "../ui/conventional/MRVMMacroDialogRuntime.hpp"
#include "../ui/modeless/MRMacroModelessUi.hpp"
#include "../ui/modeless/MRVMMacroModelessProcedures.hpp"
#include "../ui/modeless/MRVMModelessUiRuntime.hpp"
#include "../../app/MRCommandRouter.hpp"
#include "../../app/MRRuntimeScheduler.hpp"
#include "../../app/commands/MRWindowCommands.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/MRStatusLine.hpp"
#include "../../ui/MRWindowSupport.hpp"

#define Uses_TApplication
#define Uses_TKeys
#define Uses_TProgram
#include <tvision/tv.h>

#include <algorithm>
#include <initializer_list>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mrvm_runtime {

std::string parseNamedValue(const std::string &needle, const std::string &source) {
	std::size_t pos = source.find(needle);
	if (pos == std::string::npos) return std::string();
	pos += needle.size();

	// Optimization: In GCC/libstdc++, multiple find(char) calls are significantly
	// faster than a single find_first_of() due to SIMD/memchr acceleration.
	std::size_t endSpace = source.find(' ', pos);
	std::size_t endTab = source.find('\t', pos);
	std::size_t endCr = source.find('\r', pos);
	std::size_t endLf = source.find('\n', pos);
	std::size_t end = std::min({endSpace, endTab, endCr, endLf});

	if (end == std::string::npos) end = source.size();
	return source.substr(pos, end - pos);
}

std::vector<int> currentGlobalHashRoots() {
	return std::vector<int>();
}

Value ensureGlobalHashRoot(const std::string &name) {
	return g_runtimeEnv.runtimeKv.ensureRoot(name);
}

Value ensureGlobalHashChild(const Value &parent, const std::string &key) {
	return g_runtimeEnv.runtimeKv.ensureChild(parent, key);
}

Value replaceGlobalHashChild(const Value &parent, const std::string &key) {
	return g_runtimeEnv.runtimeKv.replaceChild(parent, key);
}

bool readRuntimeGlobalValueDirect(const std::string &name, GlobalEntry &entry) {
	return mrvmRuntimeGlobalRead(g_runtimeEnv.runtimeKv, name, entry);
}

std::string runtimeGlobalStringValue(const std::string &name) {
	GlobalEntry entry;

	if (!readRuntimeGlobalValueDirect(name, entry) || entry.type != TYPE_STR) return std::string();
	return mrvmValueAsString(entry.value);
}

int runtimeGlobalIntValue(const std::string &name) {
	GlobalEntry entry;

	if (!readRuntimeGlobalValueDirect(name, entry) || entry.type != TYPE_INT) return 0;
	return mrvmValueAsInt(entry.value);
}

std::vector<std::string> macroGlobalOrderValues() {
	return mrvmRuntimeGlobalOrderValues(g_runtimeEnv.runtimeKv);
}

std::size_t macroGlobalEnumIndex() {
	return mrvmRuntimeGlobalEnumIndex(g_runtimeEnv.runtimeKv);
}

void setMacroGlobalEnumIndex(std::size_t index) {
	mrvmRuntimeGlobalSetEnumIndex(g_runtimeEnv.runtimeKv, index);
}

void writeRuntimeGlobalValueDirect(const std::string &name, int type, const Value &value) {
	mrvmRuntimeGlobalWrite(g_runtimeEnv.runtimeKv, name, type, value);
}

bool isMacroVisibleRuntimeRootName(const std::string &key) {
	return key == "EXECSESSIONS" || key == "MODELESSUI" || key == "MACROCATALOG" || key == "MACRODEBUGGER" || key == "MACROGLOBALS" || key == "MACROSNIPPETS";
}

void setSessionGlobalValueDirect(BackgroundEditSession &session, const std::string &name, int type, const Value &value) {
	std::string key = mrvmUpperKey(name);
	GlobalEntry entry;

	entry.type = type;
	entry.value = value;
	entry.value.globalStorage = true;
	if (session.globals.find(key) == session.globals.end()) appendUniqueString(session.globalOrder, key);
	session.globals[key] = entry;
}

void setGlobalValue(const std::string &name, int type, const Value &value) {
	BackgroundEditSession *session = currentBackgroundEditSession();

	if (session != nullptr) {
		setSessionGlobalValueDirect(*session, name, type, value);
		return;
	}
	writeRuntimeGlobalValueDirect(name, type, value);
}

void setGlobalValueFromStore(const std::string &name, int type, const Value &value, MRVMHashStore &localStore) {
	BackgroundEditSession *session = currentBackgroundEditSession();
	Value stored = value;

	if (type == TYPE_HASH || mrvmValueIsArrayType(type)) stored = mrvmHashCopyValueForStore(value, localStore, g_runtimeEnv.runtimeKv.globalStore(), g_runtimeEnv.runtimeKv.globalStore(), true);
	else
		stored.globalStorage = true;
	if (session != nullptr && type != TYPE_HASH && !mrvmValueIsArrayType(type)) {
		setSessionGlobalValueDirect(*session, name, type, stored);
		return;
	}
	writeRuntimeGlobalValueDirect(name, type, stored);
}

bool readGlobalValue(const std::string &name, GlobalEntry &entry) {
	const std::string key = mrvmUpperKey(name);
	BackgroundEditSession *session = currentBackgroundEditSession();
	if (session != nullptr) {
		std::map<std::string, GlobalEntry>::const_iterator sessionIt = session->globals.find(key);
		if (sessionIt != session->globals.end()) {
			entry = sessionIt->second;
			return true;
		}
	}
	if (readRuntimeGlobalValueDirect(key, entry)) return true;
	if (isMacroVisibleRuntimeRootName(key)) {
		Value root = ensureGlobalHashRoot(key);
		entry.type = TYPE_HASH;
		entry.value = root;
		return true;
	}
	return false;
}

MRMacroDebugVariableScope macroDebugVariableScope(const std::string &name, const std::set<std::string> &closureVariableNames, const std::set<std::string> &sessionVariableNames) {
	if (closureVariableNames.find(name) != closureVariableNames.end()) return mrdVariableClosure;
	if (sessionVariableNames.find(name) != sessionVariableNames.end()) return mrdVariableSession;
	return mrdVariableLocal;
}

static const char *macroDebugArrayElementTypeText(int type) noexcept {
	switch (type) {
		case TYPE_INT_ARRAY:
			return "int";
		case TYPE_STR_ARRAY:
			return "str";
		case TYPE_CHAR_ARRAY:
			return "char";
		case TYPE_REAL_ARRAY:
			return "real";
		case TYPE_HASH_ARRAY:
			return "hash";
		default:
			return "array";
	}
}

std::string macroDebugValueText(const Value &value, const MRVMHashStore &localStore, const MRVMHashStore &globalStore) {
	std::ostringstream out;

	if (value.type == TYPE_HASH) {
		try {
			const MRVMHashStore &store = mrvmHashRuntimeStoreForValue(localStore, globalStore, value);
			out << "hash{" << store.keys(value.hashHandle).size() << " keys}";
		} catch (const std::exception &) {
			out << "hash{invalid}";
		}
		return out.str();
	}
	if (mrvmValueIsArrayType(value.type)) {
		out << macroDebugArrayElementTypeText(value.type) << "[" << value.arrayValues.size() << "]";
		return out.str();
	}
	return mrvmValueAsString(value);
}

void appendMacroDebugVariableSnapshots(MRMacroDebugRunResult &result, const std::map<std::string, Value> &variableStore, const std::set<std::string> &closureVariableNames, const std::set<std::string> &sessionVariableNames, const MRVMHashStore &localStore, const MRVMHashStore &globalStore) {
	for (const std::pair<const std::string, Value> &entry : variableStore) {
		MRMacroDebugVariableSnapshot variable;

		variable.name = entry.first;
		variable.type = entry.second.type;
		variable.valueText = macroDebugValueText(entry.second, localStore, globalStore);
		variable.scope = macroDebugVariableScope(entry.first, closureVariableNames, sessionVariableNames);
		result.variables.push_back(variable);
	}
}

void appendMacroDebugAppGlobalSnapshots(MRMacroDebugRunResult &result, const MRVMHashStore &globalStore) {
	const std::vector<std::string> order = macroGlobalOrderValues();

	for (const std::string &key : order) {
		GlobalEntry entry;
		MRMacroDebugVariableSnapshot variable;

		if (!readRuntimeGlobalValueDirect(key, entry)) continue;
		variable.name = key;
		variable.type = entry.type;
		variable.valueText = macroDebugValueText(entry.value, globalStore, globalStore);
		variable.scope = mrdVariableAppGlobal;
		result.variables.push_back(variable);
	}
}

void hashWriteInt(const Value &hash, const std::string &key, int value) {
	mrvmHashWriteValue(g_runtimeEnv.runtimeKv.globalStore(), g_runtimeEnv.runtimeKv.globalStore(), hash, key, mrvmMakeInt(value));
}

Value ensureExecSessionsChildPath(std::initializer_list<const char *> keys) {
	return mrvmExecSessionsEnsureChildPath(g_runtimeEnv.runtimeKv, keys);
}

bool findExecSessionsChildPath(std::initializer_list<const char *> keys, Value &child) {
	return mrvmExecSessionsFindChildPath(g_runtimeEnv.runtimeKv, keys, child);
}

bool readLoadedMacroFileByKey(const std::string &fileKey, LoadedMacroFile &file) {
	return mrvmRuntimeCatalogReadLoadedFile(g_runtimeEnv.runtimeKv, fileKey, file);
}

bool loadedMacroFileExists(const std::string &fileKey) {
	return mrvmRuntimeCatalogLoadedFileExists(g_runtimeEnv.runtimeKv, fileKey);
}

void writeLoadedMacroFileByKey(const LoadedMacroFile &file) {
	mrvmRuntimeCatalogWriteLoadedFile(g_runtimeEnv.runtimeKv, file);
}

bool eraseLoadedMacroFileByKey(const std::string &fileKey) {
	return mrvmRuntimeCatalogEraseLoadedFile(g_runtimeEnv.runtimeKv, fileKey);
}

std::vector<std::string> loadedMacroFileKeys() {
	return mrvmRuntimeCatalogLoadedFileKeys(g_runtimeEnv.runtimeKv);
}

bool readLoadedMacroByKey(const std::string &macroKey, MacroRef &macroRef) {
	return mrvmRuntimeCatalogReadLoadedMacro(g_runtimeEnv.runtimeKv, macroKey, macroRef);
}

bool loadedMacroExists(const std::string &macroKey) {
	return mrvmRuntimeCatalogLoadedMacroExists(g_runtimeEnv.runtimeKv, macroKey);
}

void writeLoadedMacroByKey(const std::string &macroKey, const MacroRef &macroRef) {
	mrvmRuntimeCatalogWriteLoadedMacro(g_runtimeEnv.runtimeKv, macroKey, macroRef);
}

bool eraseLoadedMacroByKey(const std::string &macroKey) {
	return mrvmRuntimeCatalogEraseLoadedMacro(g_runtimeEnv.runtimeKv, macroKey);
}

std::vector<std::string> macroCatalogMacroOrder() {
	return mrvmRuntimeCatalogMacroOrder(g_runtimeEnv.runtimeKv);
}

void appendMacroCatalogMacroOrder(const std::string &macroKey) {
	mrvmRuntimeCatalogAppendMacroOrder(g_runtimeEnv.runtimeKv, macroKey);
}

void removeMacroCatalogMacroOrder(const std::string &macroKey) {
	mrvmRuntimeCatalogRemoveMacroOrder(g_runtimeEnv.runtimeKv, macroKey);
}

std::size_t macroCatalogMacroEnumIndex() {
	return mrvmRuntimeCatalogMacroEnumIndex(g_runtimeEnv.runtimeKv);
}

void setMacroCatalogMacroEnumIndex(std::size_t index) {
	mrvmRuntimeCatalogSetMacroEnumIndex(g_runtimeEnv.runtimeKv, index);
}

std::size_t macroCatalogLoadedMacroCount() {
	return mrvmRuntimeCatalogLoadedMacroCount(g_runtimeEnv.runtimeKv);
}

std::vector<IndexedBoundMacroEntry> macroCatalogIndexedBindings() {
	return mrvmRuntimeCatalogIndexedBindings(g_runtimeEnv.runtimeKv);
}

void writeMacroCatalogIndexedBindings(const std::vector<IndexedBoundMacroEntry> &bindings) {
	mrvmRuntimeCatalogWriteIndexedBindings(g_runtimeEnv.runtimeKv, bindings);
}

bool markMacroCatalogIndexedWarmupAttempted(const std::string &fileKey) {
	return mrvmRuntimeCatalogMarkIndexedWarmupAttempted(g_runtimeEnv.runtimeKv, fileKey);
}

std::size_t macroCatalogIndexedBindingCount() {
	return mrvmRuntimeCatalogIndexedBindingCount(g_runtimeEnv.runtimeKv);
}

std::string loadedFileBasenameKey(const LoadedMacroFile &file) {
	std::string source = !file.resolvedPath.empty() ? file.resolvedPath : file.displayName;

	if (source.empty()) source = file.fileKey;
	return mrvmMakeMacroFileKey(mrvmTruncatePathPart(source));
}

std::string resolveLoadedFileKeyForSpec(const std::string &fileSpec) {
	const std::string exactKey = mrvmMakeMacroFileKey(fileSpec);
	std::string matchedKey;
	const std::vector<std::string> fileKeys = loadedMacroFileKeys();

	if (fileSpec.empty()) return std::string();
	if (loadedMacroFileExists(exactKey)) return exactKey;
	for (const std::string &fileKey : fileKeys) {
		LoadedMacroFile file;
		if (!readLoadedMacroFileByKey(fileKey, file)) continue;
		if (loadedFileBasenameKey(file) != exactKey) continue;
		if (!matchedKey.empty() && matchedKey != fileKey) return std::string();
		matchedKey = fileKey;
	}
	return matchedKey;
}

bool fileSpecMatchesLoadedFileKey(const std::string &fileSpec, const std::string &targetFileKey) {
	const std::string resolvedKey = resolveLoadedFileKeyForSpec(fileSpec);

	if (targetFileKey.empty()) return fileSpec.empty();
	if (!resolvedKey.empty()) return resolvedKey == targetFileKey;
	return mrvmMakeMacroFileKey(fileSpec) == targetFileKey;
}

bool macroIsRunning(const std::string &macroKey) {
	for (auto &i : g_runtimeEnv.macroStack)
		if (mrvmUpperKey(i.macroName) == macroKey) return true;
	return false;
}

bool removeMacroFromRegistryByKey(const std::string &macroKey) {
	MacroRef macroRef;
	LoadedMacroFile file;
	std::string ownerSpec;

	if (!readLoadedMacroByKey(macroKey, macroRef)) return false;
	if (macroRef.scheduledConsumerId != 0) {
		removeRuntimeScheduledConsumer(macroRef.scheduledConsumerId);
		macroRef.scheduledConsumerId = 0;
	}
	static_cast<void>(composeLoadedMacroSpec(macroRef, ownerSpec));
	if (!ownerSpec.empty()) static_cast<void>(mrvmUiRemoveRuntimeMenusOwnedByMacroSpec(ownerSpec));

	const std::string fileKey = macroRef.fileKey;
	static_cast<void>(eraseLoadedMacroByKey(macroKey));
	removeMacroCatalogMacroOrder(macroKey);

	if (readLoadedMacroFileByKey(fileKey, file)) {
		file.macroNames.erase(std::remove(file.macroNames.begin(), file.macroNames.end(), macroKey), file.macroNames.end());
		if (file.macroNames.empty()) static_cast<void>(eraseLoadedMacroFileByKey(fileKey));
		else
			writeLoadedMacroFileByKey(file);
	}
	return true;
}

static void storeLastKeyPair(int key1, int key2) noexcept {
	g_runtimeEnv.key1 = key1;
	g_runtimeEnv.key2 = key2;
}

static bool keyPairFromEvent(const TEvent &event, int &key1, int &key2) noexcept {
	if (event.what != evKeyDown) return false;
	key1 = static_cast<unsigned char>(event.keyDown.charScan.charCode);
	key2 = static_cast<unsigned char>(event.keyDown.charScan.scanCode);
	return true;
}

static bool popQueuedKeyPair(int &key1, int &key2) noexcept {
	if (g_runtimeEnv.pushedKeys.empty()) return false;
	const MacroKeyCodePair pair = g_runtimeEnv.pushedKeys.front();
	g_runtimeEnv.pushedKeys.pop_front();
	key1 = pair.key1;
	key2 = pair.key2;
	storeLastKeyPair(key1, key2);
	return true;
}

bool pushQueuedKeyPair(int key1, int key2) noexcept {
	static constexpr std::size_t maxQueuedKeys = 16;
	if (g_runtimeEnv.pushedKeys.size() >= maxQueuedKeys) return false;
	g_runtimeEnv.pushedKeys.push_back({key1, key2});
	return true;
}

bool pollUiForKeyPair(bool blocking, int &key1, int &key2) {
	TApplication *app = dynamic_cast<TApplication *>(TProgram::application);
	TEvent event;

	if (app == nullptr || currentBackgroundEditSession() != nullptr) return false;
	for (;;) {
		static_cast<TView *>(app)->getEvent(event, blocking ? 100 : 0);
		if (event.what == evNothing) return false;
		if (keyPairFromEvent(event, key1, key2)) {
			storeLastKeyPair(key1, key2);
			return true;
		}
		app->handleEvent(event);
		if (!blocking) return false;
	}
}

bool readMacroKeyPair(bool blocking, int &key1, int &key2) {
	if (popQueuedKeyPair(key1, key2)) return true;
	return pollUiForKeyPair(blocking, key1, key2);
}

MacroFunctionLabelFrame &currentFunctionLabelFrame() {
	if (g_runtimeEnv.functionLabelStack.empty()) g_runtimeEnv.functionLabelStack.emplace_back();
	return g_runtimeEnv.functionLabelStack.back();
}

std::vector<std::string> visibleFunctionLabelsForMode(int mode) {
	static constexpr std::array<int, 13> supportedKeys = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 41, 42};
	const MacroFunctionLabelFrame &frame = currentFunctionLabelFrame();
	const auto &source = mode == MACRO_MODE_DOS_SHELL ? frame.shellLabels : frame.editLabels;
	std::vector<std::string> labels(source.size());

	for (int keyNumber : supportedKeys)
		if (keyNumber > 0 && keyNumber < static_cast<int>(source.size())) labels[static_cast<std::size_t>(keyNumber)] = source[static_cast<std::size_t>(keyNumber)];
	return labels;
}

void applyFunctionLabelState() {
	TApplication *app = dynamic_cast<TApplication *>(TProgram::application);
	MRStatusLine *statusLine;

	if (app == nullptr) return;
	statusLine = dynamic_cast<MRStatusLine *>(app->statusLine);
	if (statusLine == nullptr) return;
	statusLine->setMacroFunctionLabels(visibleFunctionLabelsForMode(currentUiMacroMode()));
	mrvmUiInvalidateScreenBase();
}

std::vector<std::string> resolveMacroUiListItems(const std::string &itemSpec) {
	const std::string key = mrvmModelessUiListKey(itemSpec);
	std::vector<std::string> values;

	if (mrvmModelessUiReadItemList(g_runtimeEnv.runtimeKv, key, values)) return values;
	return mrvmResolveMacroUiListItems(g_runtimeEnv.runtimeKv, itemSpec);
}

void runMacroModelessCommand(const std::string &windowId, int, const MRMacroModelessSelection &selection, const std::string &macroSpec) {
	if (selection.controlId != 0) {
		mrvmModelessUiWriteIndexValue(g_runtimeEnv.runtimeKv, selection.controlId, selection.index);
		mrvmModelessUiWriteTextValue(g_runtimeEnv.runtimeKv, selection.controlId, selection.text);
	}
	if (!macroSpec.empty()) {
		MRMacroExecutionOwner owner;
		MRMacroExecutionSession session;
		std::string errorText;

		owner.modelessWindowId = windowId;
		if (!runMacroSpecByNameAsExecutionSessionForOwner(macroSpec.c_str(), owner, &session, &errorText, false)) {
			runtimeErrorLevel() = 1001;
			if (!errorText.empty()) static_cast<void>(mrvmUiMarquee(2, errorText));
		}
	}
}

void showMacroModelessDialog(const std::vector<Value> &args) {
	const std::string windowId = mrvmModelessUiListKey(mrvmValueAsString(args[0]));

	if (windowId.empty()) throw std::runtime_error("UI_MODELESS_SHOW expects a non-empty window id.");
	setMacroModelessListResolver(resolveMacroUiListItems);
	setMacroModelessCommandRunner(runMacroModelessCommand);
	runtimeReturnInt() = showMacroModelessWindow(mrvmBuildMacroModelessDefinition(g_runtimeEnv.runtimeKv, windowId)) ? 1 : 0;
	runtimeErrorLevel() = runtimeReturnInt() == 1 ? 0 : 1001;
}

void updateMacroModelessDialog(const std::vector<Value> &args) {
	const std::string windowId = mrvmModelessUiListKey(mrvmValueAsString(args[0]));

	if (windowId.empty()) throw std::runtime_error("UI_MODELESS_UPDATE expects a non-empty window id.");
	setMacroModelessListResolver(resolveMacroUiListItems);
	setMacroModelessCommandRunner(runMacroModelessCommand);
	runtimeReturnInt() = updateMacroModelessWindow(mrvmBuildMacroModelessDefinition(g_runtimeEnv.runtimeKv, windowId)) ? 1 : 0;
	runtimeErrorLevel() = runtimeReturnInt() == 1 ? 0 : 1001;
}

void updateMacroModelessDisplayLine(const std::vector<Value> &args) {
	const std::string windowId = mrvmModelessUiListKey(mrvmValueAsString(args[0]));
	const int displayIndex = mrvmValueAsInt(args[1]);

	if (windowId.empty()) throw std::runtime_error("UI_MODELESS_DISPLAY expects a non-empty window id.");
	if (displayIndex <= 0) throw std::runtime_error("UI_MODELESS_DISPLAY expects a positive display index.");
	runtimeReturnInt() = updateMacroModelessDisplay(windowId, displayIndex, mrvmValueAsString(args[2])) ? 1 : 0;
	runtimeErrorLevel() = runtimeReturnInt() == 1 ? 0 : 1001;
}

void closeMacroModelessDialog(const std::vector<Value> &args) {
	const std::string windowId = mrvmModelessUiListKey(mrvmValueAsString(args[0]));

	if (windowId.empty()) throw std::runtime_error("UI_MODELESS_CLOSE expects a non-empty window id.");
	runtimeReturnInt() = closeMacroModelessWindow(windowId) ? 1 : 0;
	runtimeErrorLevel() = runtimeReturnInt() == 1 ? 0 : 1001;
}

void listExecSessionClosures(const std::vector<Value> &args) {
	const std::string key = mrvmModelessUiListKey(mrvmValueAsString(args[0]));
	const std::vector<MRRuntimeScheduledConsumer> consumers = runtimeScheduledConsumers();
	Value consoleRoot;
	Value consoleList;
	Value ids;
	int row = 0;

	if (currentBackgroundEditSession() != nullptr) throw std::runtime_error("EXEC_SESSION_LIST is not available in background mode.");
	if (key.empty()) throw std::runtime_error("EXEC_SESSION_LIST expects a non-empty list name.");

	mrvmModelessUiClearItemList(g_runtimeEnv.runtimeKv, key);
	if (!findExecSessionsChildPath({"console"}, consoleRoot)) consoleRoot = ensureExecSessionsChildPath({"console"});
	consoleList = replaceGlobalHashChild(consoleRoot, key);
	ids = ensureGlobalHashChild(consoleList, "ids");
	for (std::size_t index = 0; index < consumers.size(); ++index) {
		const MRRuntimeScheduledConsumer &consumer = consumers[index];
		if (consumer.config.closureId.empty()) continue;
		if (consumer.consumerId > static_cast<MRRuntimeScheduledConsumerId>(std::numeric_limits<int>::max())) continue;
		++row;
		hashWriteInt(ids, std::to_string(row), static_cast<int>(consumer.consumerId));

		std::string line = std::to_string(row);
		line += "  #";
		line += std::to_string(consumer.consumerId);
		line += "  ";
		line += consumer.activeSessionId != 0 ? "running " : "waiting ";
		line += "tick=";
		line += std::to_string(consumer.config.intervalMs);
		line += "  ";
		line += consumer.config.entryName.empty() ? consumer.config.closureId : consumer.config.entryName;
		mrvmModelessUiAddItemListValue(g_runtimeEnv.runtimeKv, key, line);
	}
	hashWriteInt(consoleList, "count", row);
	runtimeReturnInt() = row;
	runtimeErrorLevel() = 0;
}

void stopExecSessionClosure(const std::vector<Value> &args) {
	const int requestedId = mrvmValueAsInt(args[0]);
	const std::vector<MRRuntimeScheduledConsumer> consumers = runtimeScheduledConsumers();
	std::string closureId;
	bool removed = false;

	if (currentBackgroundEditSession() != nullptr) throw std::runtime_error("EXEC_SESSION_STOP is not available in background mode.");
	if (requestedId > 0) {
		for (std::size_t index = 0; index < consumers.size(); ++index) {
			const MRRuntimeScheduledConsumer &consumer = consumers[index];
			if (consumer.consumerId != static_cast<MRRuntimeScheduledConsumerId>(requestedId)) continue;
			closureId = consumer.config.closureId;
			break;
		}
		removed = removeRuntimeScheduledConsumer(static_cast<MRRuntimeScheduledConsumerId>(requestedId));
	}
	if (removed && !closureId.empty()) static_cast<void>(mrvmExecSessionsEraseClosureState(g_runtimeEnv.runtimeKv, closureId));
	runtimeReturnInt() = removed ? 1 : 0;
	runtimeErrorLevel() = removed ? 0 : 1001;
}

int currentUiMacroMode() {
	MREditWindow *win = activeMacroEditWindow();
	if (win != nullptr && win->isCommunicationWindow()) return MACRO_MODE_DOS_SHELL;
	return MACRO_MODE_EDIT;
}

bool macroAllowsUiMode(const MacroRef &macroRef, int mode) noexcept {
	return macroRef.fromMode == MACRO_MODE_ALL || macroRef.fromMode == mode;
}

bool executeLoadedMacro(const std::string &macroKey, const std::string &paramPart, std::vector<std::string> *logSink) {
	MacroRef macroRef;
	LoadedMacroFile file;
	VirtualMachine childVm;
	bool backgroundStaged = currentBackgroundEditSession() != nullptr;
	bool childFirstRun;
	bool childDump;
	bool childTransient;
	std::string childFileKey;

	if (!readLoadedMacroByKey(macroKey, macroRef)) {
		runtimeErrorLevel() = 5001;
		return false;
	}

	if (!readLoadedMacroFileByKey(macroRef.fileKey, file)) {
		runtimeErrorLevel() = 5001;
		return false;
	}

	if (backgroundStaged) {
		if (file.bytecode.empty() || !currentBackgroundChildMacroAllowed(file)) {
			runtimeErrorLevel() = 5001;
			return false;
		}
	} else if (!ensureLoadedFileResident(macroRef.fileKey))
		return false;

	if (!readLoadedMacroFileByKey(macroRef.fileKey, file) || file.bytecode.empty()) {
		runtimeErrorLevel() = 5001;
		return false;
	}

	childFirstRun = macroRef.firstRunPending;
	childDump = macroRef.dumpAttr;
	childTransient = macroRef.transientAttr;
	childFileKey = macroRef.fileKey;
	macroRef.firstRunPending = false;
	writeLoadedMacroByKey(macroKey, macroRef);

	childVm.setExecutionSessionContext(currentExecutionSessionId());
	if (macroRef.closureUnit) childVm.setClosureContext(macroRef.closureId);
	childVm.executeAt(file.bytecode.data(), file.bytecode.size(), macroRef.entryOffset, paramPart, macroRef.displayName, false, childFirstRun);
	if (logSink != nullptr) logSink->insert(logSink->end(), childVm.log.begin(), childVm.log.end());
	if (childDump) unloadMacroFromRegistry(macroKey);
	else if (childTransient)
		evictTransientFileImage(childFileKey);
	runtimeErrorLevel() = 0;
	return true;
}

bool executeLoadedMacroWithConfiguredKeymapBatch(const std::string &macroKey, const std::string &paramPart, std::vector<std::string> *logSink) {
	MacroRef macroRef;
	LoadedMacroFile file;
	bool configuredKeymapBatch = false;
	std::string keymapBatchError;

	if (readLoadedMacroByKey(macroKey, macroRef) && readLoadedMacroFileByKey(macroRef.fileKey, file)) {
		const std::vector<std::string> unsupported = mrvmUnsupportedStagedSymbols(file.profile);
		for (const std::string &symbol : unsupported)
			if (symbol == "KEYMAP_RESET" || symbol == "KEYMAP_PROFILE" || symbol == "KEYMAP_BIND" || symbol == "ACTIVE_KEYMAP_PROFILE") {
				configuredKeymapBatch = true;
				break;
			}
	}
	if (configuredKeymapBatch) mrvmBeginConfiguredKeymapBatch();
	const bool executed = executeLoadedMacro(macroKey, paramPart, logSink);
	if (configuredKeymapBatch && !mrvmEndConfiguredKeymapBatch(&keymapBatchError)) {
		runtimeErrorLevel() = 1001;
		if (logSink != nullptr) logSink->push_back("VM Error: keymap batch flush failed: " + (keymapBatchError.empty() ? std::string("invalid keymap batch.") : keymapBatchError));
		return false;
	}
	return executed;
}

void clearRegisteredBindingsForKey(const TKey *key, int mode, bool clearAllModes) {
	const std::vector<std::string> orderValues = macroCatalogMacroOrder();
	for (const std::string &macroKey : orderValues) {
		MacroRef macroRef;
		if (!readLoadedMacroByKey(macroKey, macroRef)) continue;
		if (!macroRef.hasAssignedKey) continue;
		if (!clearAllModes && macroRef.fromMode != mode) continue;
		if (key != nullptr && !mrvmBindingKeysEqual(macroRef.assignedKey, *key)) continue;
		macroRef.hasAssignedKey = false;
		macroRef.assignedKeySpec.clear();
		writeLoadedMacroByKey(macroKey, macroRef);
	}
	{
		std::vector<IndexedBoundMacroEntry> indexed = macroCatalogIndexedBindings();
		indexed.erase(std::remove_if(indexed.begin(), indexed.end(),
		                             [&](const IndexedBoundMacroEntry &entry) {
			                             if (key != nullptr && !mrvmBindingKeysEqual(entry.key, *key)) return false;
			                             return clearAllModes || mode == MACRO_MODE_ALL || mode == MACRO_MODE_EDIT || mode == MACRO_MODE_DOS_SHELL;
		                             }),
		              indexed.end());
		writeMacroCatalogIndexedBindings(indexed);
	}
}

bool executeRuntimeMacroSpec(const std::string &spec, std::vector<std::string> *logLines) {
	std::string filePart;
	std::string macroPart;
	std::string paramPart;
	std::string targetFileKey;
	std::string macroKey;
	MacroRef macroRef;

	if (!mrvmParseRunMacroSpec(spec, filePart, macroPart, paramPart)) {
		runtimeErrorLevel() = 5001;
		return false;
	}

	macroKey = mrvmUpperKey(macroPart);
	if (!filePart.empty()) targetFileKey = resolveLoadedFileKeyForSpec(filePart);
	if (!filePart.empty() && targetFileKey.empty()) targetFileKey = mrvmMakeMacroFileKey(filePart);

	if (!readLoadedMacroByKey(macroKey, macroRef) || (!targetFileKey.empty() && macroRef.fileKey != targetFileKey)) {
		if (!filePart.empty()) {
			if (!loadMacroFileIntoRegistry(filePart, &targetFileKey)) return false;
		} else {
			if (!loadMacroFileIntoRegistry(macroPart, &targetFileKey)) return false;
		}
		static_cast<void>(readLoadedMacroByKey(macroKey, macroRef));
	}

	if (macroRef.displayName.empty() || (!targetFileKey.empty() && macroRef.fileKey != targetFileKey)) {
		runtimeErrorLevel() = 5001;
		return false;
	}
	return executeLoadedMacroWithConfiguredKeymapBatch(macroKey, paramPart, logLines);
}

bool currentExecutingMacroSpecFromRuntimeStack(std::string &macroSpec) {
	const std::string macroDisplayName = !g_runtimeEnv.macroStack.empty() ? trimAscii(g_runtimeEnv.macroStack.back().macroName) : std::string();
	MacroRef macroRef;
	LoadedMacroFile file;
	std::string fileDisplayName;

	macroSpec.clear();
	if (macroDisplayName.empty() || !readLoadedMacroByKey(mrvmUpperKey(macroDisplayName), macroRef)) return false;
	if (!readLoadedMacroFileByKey(macroRef.fileKey, file)) return false;
	fileDisplayName = !file.displayName.empty() ? file.displayName : file.resolvedPath;
	if (fileDisplayName.empty()) return false;
	macroSpec = fileDisplayName + "^" + macroRef.displayName;
	return true;
}

bool composeLoadedMacroSpec(const MacroRef &macroRef, std::string &macroSpec) {
	LoadedMacroFile file;
	std::string fileDisplayName;

	macroSpec.clear();
	if (!readLoadedMacroFileByKey(macroRef.fileKey, file)) return false;
	fileDisplayName = !file.displayName.empty() ? file.displayName : file.resolvedPath;
	if (fileDisplayName.empty() || macroRef.displayName.empty()) return false;
	macroSpec = fileDisplayName + "^" + macroRef.displayName;
	return true;
}

bool macroSpecTargetsLoadedMacro(const std::string &spec, const std::string &targetFileKey, const std::string &targetMacroKey) {
	std::string filePart;
	std::string macroPart;
	std::string paramPart;
	const bool parsed = mrvmParseRunMacroSpec(spec, filePart, macroPart, paramPart);

	if (!parsed || mrvmUpperKey(macroPart) != targetMacroKey) return false;
	if (targetFileKey.empty()) return true;
	if (filePart.empty()) return false;
	return fileSpecMatchesLoadedFileKey(filePart, targetFileKey);
}

bool dispatchEditorCommandEvent(ushort command) {
	MRFileEditor *editor = currentEditor();
	TEvent event;

	if (editor == nullptr) return false;
	std::memset(&event, 0, sizeof(event));
	event.what = evCommand;
	event.message.command = command;
	editor->handleEvent(event);
	return true;
}

bool dispatchApplicationCommandEvent(ushort command) {
	TApplication *app = dynamic_cast<TApplication *>(TProgram::application);
	TEvent event;

	if (app == nullptr) return false;
	std::memset(&event, 0, sizeof(event));
	event.what = evCommand;
	event.message.command = command;
	app->handleEvent(event);
	return true;
}

bool executeBoundCommand(int commandId) {
	MRFileEditor *editor = currentEditor();

	switch (commandId) {
		case macdBackSpace:
			backspaceEditor(editor);
			return true;
		case macdBlockBegin:
		case macdBlockEnd:
		case macdBlockOff:
		case macdColBlockBegin:
		case macdCopyBlock:
			return true;
		case macdCr:
			carriageReturnEditor(editor);
			return true;
		case macdDeleteBlock:
			return true;
		case macdDelChar:
			deleteEditorChars(editor, 1);
			return true;
		case macdDelLine:
			deleteEditorLine(editor);
			return true;
		case macdDown:
			return moveEditorDown(editor);
		case macdEof:
			return moveEditorEof(editor);
		case macdEol:
			return moveEditorEol(editor);
		case macdFirstWord:
			return moveEditorFirstWord(editor);
		case macdGotoMark:
			return gotoEditorMark(currentEditorCommandWindow(), editor);
		case macdHome:
			return moveEditorHome(editor);
		case macdIndent:
			return indentEditor(editor);
		case macdKeyRecord:
			return dispatchApplicationCommandEvent(cmMrMacroToggleRecording);
		case macdLastPageBreak:
			return moveEditorLastPageBreak(editor);
		case macdLeft:
			return moveEditorLeft(editor);
		case macdMarkPos:
			return markEditorPosition(currentEditorCommandWindow(), editor);
		case macdMoveBlock:
			return true;
		case macdNextPageBreak:
			return moveEditorNextPageBreak(editor);
		case macdPageDown:
			return moveEditorPageDown(editor);
		case macdPageUp:
			return moveEditorPageUp(editor);
		case macdRight:
			return moveEditorRight(editor);
		case macdSaveFile:
			return activeMacroEditWindow() != nullptr && activeMacroEditWindow()->saveCurrentFile();
		case macdStrBlockBegin:
			return true;
		case macdTabLeft:
			return moveEditorTabLeft(editor);
		case macdTabRight:
			return moveEditorTabRight(editor);
		case macdTof:
			return moveEditorTof(editor);
		case macdUndent:
			return undentEditor(editor);
		case macdUndo:
			return dispatchEditorCommandEvent(cmMrEditUndo);
		case macdUp:
			return moveEditorUp(editor);
		case macdWordLeft:
			return moveEditorWordLeft(editor);
		case macdWordRight:
			return moveEditorWordRight(editor);
		default:
			return false;
	}
}

bool executeExplicitKeyBinding(const TKey &pressed, int mode, std::vector<std::string> *logLines) {
	mrvmLogCalculatorHotkeyState("vm-explicit-enter", pressed);
	for (std::size_t i = g_runtimeEnv.explicitKeyBindings.size(); i > 0; --i) {
		const MRVMExplicitKeyBinding &binding = g_runtimeEnv.explicitKeyBindings[i - 1];
		if (!mrvmBindingKeysEqual(binding.key, pressed) || !mrvmBindingModeMatches(binding.mode, mode)) continue;
		if (binding.kind == MRVMExplicitBindingKind::MacroSpec) mrvmLogCalculatorHotkeyState("vm-explicit-match", pressed, binding.macroSpec);
		else
			mrvmLogCalculatorHotkeyState("vm-explicit-match-cmd", pressed);
		if (binding.kind == MRVMExplicitBindingKind::MacroSpec) return executeRuntimeMacroSpec(binding.macroSpec, logLines);
		runtimeErrorLevel() = executeBoundCommand(binding.commandId) ? 0 : 1001;
		return runtimeErrorLevel() == 0;
	}
	return false;
}

bool projectRuntimeMenuKeyLabelsFromExplicitBindings(std::string *errorMessage) {
	const int mode = currentUiMacroMode();

	if (!mrvmUiClearRuntimeMenuKeyLabels(errorMessage)) return false;
	for (const MRVMExplicitKeyBinding &binding : g_runtimeEnv.explicitKeyBindings) {
		if (binding.kind != MRVMExplicitBindingKind::MacroSpec) continue;
		if (!mrvmBindingModeMatches(binding.mode, mode)) continue;
		if (!mrvmUiSetRuntimeMenuKeyLabelForMacroSpec(binding.macroSpec, mrvmMenuLabelFromBindingKey(binding.key), errorMessage)) return false;
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool fileContainsOnlyTransientMacros(const LoadedMacroFile &file) {
	if (file.macroNames.empty()) return false;
	for (const auto &macroName : file.macroNames) {
		MacroRef macroRef;
		if (!readLoadedMacroByKey(macroName, macroRef) || !macroRef.transientAttr) return false;
	}
	return true;
}

} // namespace mrvm_runtime
