#include "MRVMExecSessions.hpp"

#include "MRVMHash.hpp"
#include "MRVMValue.hpp"

#include "../MRVM.hpp"
#include "../mrmac.h"
#include "../../app/commands/MRWindowCommands.hpp"
#include "../../app/utils/MRStringUtils.hpp"
#include "../../ui/MRWindowSupport.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string_view>

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;
std::recursive_mutex &mrvmExecutionMutex() noexcept;
bool dispatchMRKeymapAction(std::string_view actionId, std::string_view sequenceText = {}, MREditWindow *targetWindow = nullptr);

namespace {
using Value = VirtualMachine::Value;

Value makeIntValue(int value) {
	Value result;
	result.type = TYPE_INT;
	result.i = value;
	return result;
}

Value makeStringValue(const std::string &value) {
	Value result;
	result.type = TYPE_STR;
	result.s = value;
	return result;
}

bool parseUint64Text(const std::string &text, std::uint64_t &value) {
	value = 0;
	if (text.empty()) return false;
	for (std::size_t index = 0; index < text.size(); ++index) {
		const unsigned char ch = static_cast<unsigned char>(text[index]);
		std::uint64_t digit;
		if (ch < static_cast<unsigned char>('0') || ch > static_cast<unsigned char>('9')) return false;
		digit = static_cast<std::uint64_t>(ch - static_cast<unsigned char>('0'));
		if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
		value = value * 10 + digit;
	}
	return true;
}

int valueAsInt(const Value &value, int fallback) {
	std::uint64_t parsed = 0;

	if (value.type == TYPE_INT) return value.i;
	if (value.type == TYPE_STR && parseUint64Text(value.s, parsed)) return static_cast<int>(parsed);
	return fallback;
}

std::uint64_t valueAsUint64(const Value &value, std::uint64_t fallback) {
	std::uint64_t parsed = 0;

	if (value.type == TYPE_INT) return value.i > 0 ? static_cast<std::uint64_t>(value.i) : 0;
	if (value.type == TYPE_STR && parseUint64Text(value.s, parsed)) return parsed;
	return fallback;
}

std::string valueAsString(const Value &value) {
	if (value.type == TYPE_STR) return value.s;
	if (value.type == TYPE_INT) return std::to_string(value.i);
	return std::string();
}

void hashWriteInt(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key, int value) {
	MRVMHashStore &store = runtimeKv.globalStore();

	mrvmHashWriteValue(store, store, hash, key, makeIntValue(value));
}

void hashWriteUint(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key, std::uint64_t value) {
	MRVMHashStore &store = runtimeKv.globalStore();

	mrvmHashWriteValue(store, store, hash, key, makeStringValue(std::to_string(value)));
}

void hashWriteString(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key, const std::string &value) {
	MRVMHashStore &store = runtimeKv.globalStore();

	mrvmHashWriteValue(store, store, hash, key, makeStringValue(value));
}

int hashReadInt(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key, int fallback = 0) {
	MRVMHashStore &store = runtimeKv.globalStore();

	if (!mrvmHashContainsValue(store, store, hash, key)) return fallback;
	return valueAsInt(mrvmHashReadValue(store, store, hash, key), fallback);
}

std::uint64_t hashReadUint(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key, std::uint64_t fallback = 0) {
	MRVMHashStore &store = runtimeKv.globalStore();

	if (!mrvmHashContainsValue(store, store, hash, key)) return fallback;
	return valueAsUint64(mrvmHashReadValue(store, store, hash, key), fallback);
}

std::string hashReadString(MRVMRuntimeKv &runtimeKv, const Value &hash, const std::string &key) {
	MRVMHashStore &store = runtimeKv.globalStore();

	if (!mrvmHashContainsValue(store, store, hash, key)) return std::string();
	return valueAsString(mrvmHashReadValue(store, store, hash, key));
}

Value copyValueToGlobalStore(MRVMRuntimeKv &runtimeKv, const Value &value, MRVMHashStore &localStore) {
	Value stored = value;
	MRVMHashStore &globalStore = runtimeKv.globalStore();

	if (value.type == TYPE_HASH || mrvmValueIsArrayType(value.type)) return mrvmHashCopyValueForStore(value, localStore, globalStore, globalStore, true);
	stored.globalStorage = true;
	return stored;
}

bool findClosureState(MRVMRuntimeKv &runtimeKv, const std::string &closureId, Value &state) {
	Value closures;
	Value closure;

	if (closureId.empty()) return false;
	if (!mrvmExecSessionsFindChildPath(runtimeKv, {"closures"}, closures)) return false;
	if (!runtimeKv.findChild(closures, closureId, closure)) return false;
	return runtimeKv.findChild(closure, "state", state);
}

Value ensureSessionVariables(MRVMRuntimeKv &runtimeKv, MRMacroExecutionSessionId sessionId) {
	Value byId = mrvmExecSessionsEnsureChildPath(runtimeKv, {"sessions", "byId"});
	Value session = runtimeKv.ensureChild(byId, std::to_string(sessionId));
	Value variables = runtimeKv.ensureChild(session, "variables");
	return runtimeKv.ensureChild(variables, "byName");
}

bool findSessionVariables(MRVMRuntimeKv &runtimeKv, MRMacroExecutionSessionId sessionId, Value &variables) {
	Value byId;
	Value session;
	Value variableRoot;

	if (sessionId == 0) return false;
	if (!mrvmExecSessionsFindChildPath(runtimeKv, {"sessions", "byId"}, byId)) return false;
	if (!runtimeKv.findChild(byId, std::to_string(sessionId), session)) return false;
	if (!runtimeKv.findChild(session, "variables", variableRoot)) return false;
	return runtimeKv.findChild(variableRoot, "byName", variables);
}

void writeRuntimeScheduledConsumerConfigHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MRRuntimeScheduledConsumerConfig &config) {
	Value owner = runtimeKv.replaceChild(hash, "owner");

	hashWriteUint(runtimeKv, hash, "intervalMs", config.intervalMs);
	hashWriteString(runtimeKv, hash, "macroSpec", config.macroSpec);
	hashWriteString(runtimeKv, hash, "macroSource", config.macroSource);
	hashWriteString(runtimeKv, hash, "entryName", config.entryName);
	hashWriteString(runtimeKv, hash, "closureId", config.closureId);
	hashWriteInt(runtimeKv, hash, "overrunPolicy", static_cast<int>(config.overrunPolicy));
	mrvmExecSessionsWriteOwner(runtimeKv, owner, config.owner);
}

MRRuntimeScheduledConsumerConfig readRuntimeScheduledConsumerConfigHash(MRVMRuntimeKv &runtimeKv, const Value &hash) {
	MRRuntimeScheduledConsumerConfig config;
	Value owner;

	config.intervalMs = hashReadUint(runtimeKv, hash, "intervalMs");
	config.macroSpec = hashReadString(runtimeKv, hash, "macroSpec");
	config.macroSource = hashReadString(runtimeKv, hash, "macroSource");
	config.entryName = hashReadString(runtimeKv, hash, "entryName");
	config.closureId = hashReadString(runtimeKv, hash, "closureId");
	config.overrunPolicy = static_cast<MRRuntimeScheduleOverrunPolicy>(hashReadInt(runtimeKv, hash, "overrunPolicy", static_cast<int>(MRRuntimeScheduleOverrunPolicy::Skip)));
	if (runtimeKv.findChild(hash, "owner", owner)) config.owner = mrvmExecSessionsReadOwner(runtimeKv, owner);
	return config;
}

void writeRuntimeScheduledConsumerHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MRRuntimeScheduledConsumer &consumer) {
	Value config = runtimeKv.replaceChild(hash, "config");

	hashWriteUint(runtimeKv, hash, "consumerId", consumer.consumerId);
	hashWriteUint(runtimeKv, hash, "activeSessionId", consumer.activeSessionId);
	hashWriteUint(runtimeKv, hash, "nextDueMs", consumer.nextDueMs);
	writeRuntimeScheduledConsumerConfigHash(runtimeKv, config, consumer.config);
}

bool readRuntimeScheduledConsumerHash(MRVMRuntimeKv &runtimeKv, const Value &hash, MRRuntimeScheduledConsumer &consumer) {
	Value config;

	consumer.consumerId = hashReadUint(runtimeKv, hash, "consumerId");
	consumer.activeSessionId = hashReadUint(runtimeKv, hash, "activeSessionId");
	consumer.nextDueMs = hashReadUint(runtimeKv, hash, "nextDueMs");
	if (runtimeKv.findChild(hash, "config", config)) consumer.config = readRuntimeScheduledConsumerConfigHash(runtimeKv, config);
	return consumer.consumerId != 0 && consumer.config.intervalMs != 0;
}

void writeRuntimeSchedulerEventHash(MRVMRuntimeKv &runtimeKv, const Value &hash, const MRRuntimeSchedulerEvent &event) {
	Value owner = runtimeKv.replaceChild(hash, "owner");

	hashWriteUint(runtimeKv, hash, "eventId", event.eventId);
	hashWriteUint(runtimeKv, hash, "consumerId", event.consumerId);
	hashWriteInt(runtimeKv, hash, "kind", static_cast<int>(event.kind));
	hashWriteInt(runtimeKv, hash, "skipReason", static_cast<int>(event.skipReason));
	hashWriteUint(runtimeKv, hash, "sessionId", event.sessionId);
	hashWriteUint(runtimeKv, hash, "blockingSessionId", event.blockingSessionId);
	hashWriteUint(runtimeKv, hash, "dueAtMs", event.dueAtMs);
	hashWriteUint(runtimeKv, hash, "observedAtMs", event.observedAtMs);
	hashWriteString(runtimeKv, hash, "macroSpec", event.macroSpec);
	hashWriteString(runtimeKv, hash, "message", event.message);
	mrvmExecSessionsWriteOwner(runtimeKv, owner, event.owner);
}

bool readRuntimeSchedulerEventHash(MRVMRuntimeKv &runtimeKv, const Value &hash, MRRuntimeSchedulerEvent &event) {
	Value owner;

	event.eventId = hashReadUint(runtimeKv, hash, "eventId");
	event.consumerId = hashReadUint(runtimeKv, hash, "consumerId");
	if (runtimeKv.findChild(hash, "owner", owner)) event.owner = mrvmExecSessionsReadOwner(runtimeKv, owner);
	event.kind = static_cast<MRRuntimeSchedulerEventKind>(hashReadInt(runtimeKv, hash, "kind", static_cast<int>(MRRuntimeSchedulerEventKind::ConsumerRegistered)));
	event.skipReason = static_cast<MRRuntimeSchedulerSkipReason>(hashReadInt(runtimeKv, hash, "skipReason", static_cast<int>(MRRuntimeSchedulerSkipReason::None)));
	event.sessionId = hashReadUint(runtimeKv, hash, "sessionId");
	event.blockingSessionId = hashReadUint(runtimeKv, hash, "blockingSessionId");
	event.dueAtMs = hashReadUint(runtimeKv, hash, "dueAtMs");
	event.observedAtMs = hashReadUint(runtimeKv, hash, "observedAtMs");
	event.macroSpec = hashReadString(runtimeKv, hash, "macroSpec");
	event.message = hashReadString(runtimeKv, hash, "message");
	return event.eventId != 0;
}

std::string normalizeExecUiCommandAction(const std::string &command) {
	const std::string key = mrvmUpperKey(trimAscii(command));

	if (key.starts_with("MRMAC_") || key.starts_with("MR_")) return key;
	if (key == "TEXT_END" || key == "EOF" || key == "BOTTOM_OF_FILE") return "MRMAC_CURSOR_BOTTOM_OF_FILE";
	if (key == "TEXT_START" || key == "TOF" || key == "TOP_OF_FILE") return "MRMAC_CURSOR_TOP_OF_FILE";
	if (key == "LINE_END" || key == "EOL" || key == "END_OF_LINE") return "MRMAC_CURSOR_END_OF_LINE";
	if (key == "LINE_START" || key == "HOME" || key == "START_OF_LINE") return "MRMAC_CURSOR_HOME";
	if (key == "SCROLL_UP") return "MRMAC_VIEW_SCROLL_UP";
	if (key == "SCROLL_DOWN") return "MRMAC_VIEW_SCROLL_DOWN";
	return key;
}

MREditWindow *execUiCommandTargetWindow(const std::string &target) {
	const std::string key = mrvmUpperKey(trimAscii(target));

	if (key.empty() || key == "ACTIVE") return nullptr;
	if (key.starts_with("BUFFER:")) {
		const std::string idText = trimAscii(key.substr(7));
		if (idText.empty()) return nullptr;
		return findEditWindowByBufferId(std::atoi(idText.c_str()));
	}
	return nullptr;
}
} // namespace

Value mrvmExecSessionsEnsureRoot(MRVMRuntimeKv &runtimeKv) {
	return runtimeKv.ensureRoot("EXECSESSIONS");
}

bool mrvmExecSessionsFindRoot(MRVMRuntimeKv &runtimeKv, Value &root) {
	return runtimeKv.findRoot("EXECSESSIONS", root);
}

Value mrvmExecSessionsEnsureChildPath(MRVMRuntimeKv &runtimeKv, std::initializer_list<const char *> keys) {
	Value current = mrvmExecSessionsEnsureRoot(runtimeKv);

	for (const char *key : keys)
		current = runtimeKv.ensureChild(current, key != nullptr ? key : "");
	return current;
}

bool mrvmExecSessionsFindChildPath(MRVMRuntimeKv &runtimeKv, std::initializer_list<const char *> keys, Value &child) {
	Value current;

	if (!mrvmExecSessionsFindRoot(runtimeKv, current)) return false;
	for (const char *key : keys) {
		if (!runtimeKv.findChild(current, key != nullptr ? key : "", child)) return false;
		current = child;
	}
	return true;
}

std::uint64_t mrvmExecSessionsNextCounter(MRVMRuntimeKv &runtimeKv, const std::string &key) {
	Value counters = mrvmExecSessionsEnsureChildPath(runtimeKv, {"counters"});
	const std::uint64_t value = hashReadUint(runtimeKv, counters, key, 1);

	hashWriteUint(runtimeKv, counters, key, value + 1);
	return value;
}

void mrvmExecSessionsWriteOwner(MRVMRuntimeKv &runtimeKv, const Value &hash, const MRMacroExecutionOwner &owner) {
	hashWriteInt(runtimeKv, hash, "hasBuffer", owner.hasBuffer ? 1 : 0);
	hashWriteInt(runtimeKv, hash, "bufferId", owner.bufferId);
}

MRMacroExecutionOwner mrvmExecSessionsReadOwner(MRVMRuntimeKv &runtimeKv, const Value &hash) {
	MRMacroExecutionOwner owner;

	owner.hasBuffer = hashReadInt(runtimeKv, hash, "hasBuffer") != 0;
	owner.bufferId = hashReadInt(runtimeKv, hash, "bufferId");
	return owner;
}

void mrvmExecSessionsWriteSession(MRVMRuntimeKv &runtimeKv, const Value &hash, const MRMacroExecutionSession &session) {
	Value owner = runtimeKv.replaceChild(hash, "owner");

	hashWriteUint(runtimeKv, hash, "sessionId", session.sessionId);
	hashWriteInt(runtimeKv, hash, "route", static_cast<int>(session.route));
	hashWriteInt(runtimeKv, hash, "state", static_cast<int>(session.state));
	hashWriteUint(runtimeKv, hash, "taskId", session.taskId);
	hashWriteString(runtimeKv, hash, "label", session.label);
	mrvmExecSessionsWriteOwner(runtimeKv, owner, session.owner);
}

bool mrvmExecSessionsReadSession(MRVMRuntimeKv &runtimeKv, const Value &hash, MRMacroExecutionSession &session) {
	Value owner;

	session.sessionId = hashReadUint(runtimeKv, hash, "sessionId");
	session.route = static_cast<MRMacroExecutionRoute>(hashReadInt(runtimeKv, hash, "route", static_cast<int>(MRMacroExecutionRoute::Unknown)));
	session.state = static_cast<MRMacroExecutionState>(hashReadInt(runtimeKv, hash, "state", static_cast<int>(MRMacroExecutionState::Created)));
	session.taskId = hashReadUint(runtimeKv, hash, "taskId");
	session.label = hashReadString(runtimeKv, hash, "label");
	if (runtimeKv.findChild(hash, "owner", owner)) session.owner = mrvmExecSessionsReadOwner(runtimeKv, owner);
	return session.sessionId != 0;
}

void mrvmExecSessionsWriteResult(MRVMRuntimeKv &runtimeKv, const Value &hash, const MRMacroExecutionResult &result) {
	Value sessionHash = runtimeKv.replaceChild(hash, "session");

	mrvmExecSessionsWriteSession(runtimeKv, sessionHash, result.session);
	hashWriteInt(runtimeKv, hash, "state", static_cast<int>(result.state));
	hashWriteString(runtimeKv, hash, "message", result.message);
}

bool mrvmExecSessionsReadResult(MRVMRuntimeKv &runtimeKv, const Value &hash, MRMacroExecutionResult &result) {
	Value sessionHash;

	if (!runtimeKv.findChild(hash, "session", sessionHash)) return false;
	if (!mrvmExecSessionsReadSession(runtimeKv, sessionHash, result.session)) return false;
	result.state = static_cast<MRMacroExecutionState>(hashReadInt(runtimeKv, hash, "state", static_cast<int>(MRMacroExecutionState::Created)));
	result.message = hashReadString(runtimeKv, hash, "message");
	return true;
}

std::vector<std::uint64_t> mrvmExecSessionsSortedHashUintKeys(MRVMRuntimeKv &runtimeKv, const Value &hash) {
	std::vector<std::uint64_t> ids;
	std::vector<std::string> keys = runtimeKv.globalStore().keys(hash.hashHandle);

	for (std::size_t index = 0; index < keys.size(); ++index) {
		std::uint64_t id = 0;
		if (parseUint64Text(keys[index], id)) ids.push_back(id);
	}
	std::sort(ids.begin(), ids.end());
	return ids;
}

void mrvmExecSessionsTrimHashByNumericKeys(MRVMRuntimeKv &runtimeKv, const Value &hash, std::size_t limit) {
	std::vector<std::uint64_t> ids = mrvmExecSessionsSortedHashUintKeys(runtimeKv, hash);
	std::size_t removeCount;

	if (ids.size() <= limit) return;
	removeCount = ids.size() - limit;
	for (std::size_t index = 0; index < removeCount; ++index)
		static_cast<void>(runtimeKv.eraseChild(hash, std::to_string(ids[index])));
}

Value mrvmExecSessionsEnsureClosureState(MRVMRuntimeKv &runtimeKv, const std::string &closureId, int tickMs) {
	Value closures = mrvmExecSessionsEnsureChildPath(runtimeKv, {"closures"});
	Value closure = runtimeKv.ensureChild(closures, closureId);
	Value state = runtimeKv.ensureChild(closure, "state");

	if (tickMs > 0) mrvmHashWriteValue(runtimeKv.globalStore(), runtimeKv.globalStore(), closure, "tick_ms", makeIntValue(tickMs));
	return state;
}

bool mrvmExecSessionsEraseClosureState(MRVMRuntimeKv &runtimeKv, const std::string &closureId) {
	Value closures;

	if (closureId.empty()) return false;
	if (!mrvmExecSessionsFindChildPath(runtimeKv, {"closures"}, closures)) return false;
	return runtimeKv.eraseChild(closures, closureId);
}

bool mrvmExecSessionsReadClosureVariable(MRVMRuntimeKv &runtimeKv, const std::string &closureId, const std::string &name, Value &value) {
	Value state;
	MRVMHashStore &globalStore = runtimeKv.globalStore();

	if (closureId.empty()) return false;
	if (!findClosureState(runtimeKv, closureId, state)) return false;
	if (state.type != TYPE_HASH || !mrvmHashContainsValue(globalStore, globalStore, state, name)) return false;
	value = mrvmHashReadValue(globalStore, globalStore, state, name);
	return true;
}

bool mrvmExecSessionsWriteClosureVariable(MRVMRuntimeKv &runtimeKv, const std::string &closureId, const std::string &name, const Value &value, MRVMHashStore &localStore) {
	Value state;
	Value stored;
	MRVMHashStore &globalStore = runtimeKv.globalStore();

	if (closureId.empty()) return false;
	if (!findClosureState(runtimeKv, closureId, state)) return false;
	stored = copyValueToGlobalStore(runtimeKv, value, localStore);
	mrvmHashWriteValue(globalStore, globalStore, state, name, stored);
	return true;
}

bool mrvmExecSessionsReadSessionVariable(MRVMRuntimeKv &runtimeKv, MRMacroExecutionSessionId sessionId, const std::string &name, Value &value) {
	Value variables;
	MRVMHashStore &globalStore = runtimeKv.globalStore();

	if (!findSessionVariables(runtimeKv, sessionId, variables)) return false;
	if (!mrvmHashContainsValue(globalStore, globalStore, variables, name)) return false;
	value = mrvmHashReadValue(globalStore, globalStore, variables, name);
	return true;
}

bool mrvmExecSessionsWriteSessionVariable(MRVMRuntimeKv &runtimeKv, MRMacroExecutionSessionId sessionId, const std::string &name, const Value &value, MRVMHashStore &localStore) {
	Value variables;
	Value stored;
	MRVMHashStore &globalStore = runtimeKv.globalStore();

	if (sessionId == 0) return false;
	variables = ensureSessionVariables(runtimeKv, sessionId);
	stored = copyValueToGlobalStore(runtimeKv, value, localStore);
	mrvmHashWriteValue(globalStore, globalStore, variables, name, stored);
	return true;
}

bool mrvmExecSessionsEraseSessionRuntimeState(MRVMRuntimeKv &runtimeKv, MRMacroExecutionSessionId sessionId) {
	Value byId;

	if (sessionId == 0) return false;
	if (!mrvmExecSessionsFindChildPath(runtimeKv, {"sessions", "byId"}, byId)) return false;
	return runtimeKv.eraseChild(byId, std::to_string(sessionId));
}

void mrvmExecSessionsStoreRuntimeScheduledConsumer(MRVMRuntimeKv &runtimeKv, const MRRuntimeScheduledConsumer &consumer) {
	Value consumers = mrvmExecSessionsEnsureChildPath(runtimeKv, {"scheduler", "consumers"});
	Value consumerHash;

	if (consumer.consumerId == 0) return;
	consumerHash = runtimeKv.replaceChild(consumers, std::to_string(consumer.consumerId));
	writeRuntimeScheduledConsumerHash(runtimeKv, consumerHash, consumer);
}

bool mrvmExecSessionsReadRuntimeScheduledConsumer(MRVMRuntimeKv &runtimeKv, MRRuntimeScheduledConsumerId consumerId, MRRuntimeScheduledConsumer &consumer) {
	Value consumers;
	Value consumerHash;

	if (consumerId == 0 || !mrvmExecSessionsFindChildPath(runtimeKv, {"scheduler", "consumers"}, consumers)) return false;
	if (!runtimeKv.findChild(consumers, std::to_string(consumerId), consumerHash)) return false;
	return readRuntimeScheduledConsumerHash(runtimeKv, consumerHash, consumer);
}

bool mrvmExecSessionsUpdateRuntimeScheduledConsumerActiveSession(MRVMRuntimeKv &runtimeKv, MRRuntimeScheduledConsumerId consumerId, MRMacroExecutionSessionId activeSessionId) {
	Value consumers;
	Value consumerHash;

	if (consumerId == 0 || !mrvmExecSessionsFindChildPath(runtimeKv, {"scheduler", "consumers"}, consumers)) return false;
	if (!runtimeKv.findChild(consumers, std::to_string(consumerId), consumerHash)) return false;
	hashWriteUint(runtimeKv, consumerHash, "activeSessionId", activeSessionId);
	return true;
}

bool mrvmExecSessionsUpdateRuntimeScheduledConsumerNextDue(MRVMRuntimeKv &runtimeKv, MRRuntimeScheduledConsumerId consumerId, std::uint64_t nextDueMs) {
	Value consumers;
	Value consumerHash;

	if (consumerId == 0 || !mrvmExecSessionsFindChildPath(runtimeKv, {"scheduler", "consumers"}, consumers)) return false;
	if (!runtimeKv.findChild(consumers, std::to_string(consumerId), consumerHash)) return false;
	hashWriteUint(runtimeKv, consumerHash, "nextDueMs", nextDueMs);
	return true;
}

bool mrvmExecSessionsRemoveRuntimeScheduledConsumer(MRVMRuntimeKv &runtimeKv, MRRuntimeScheduledConsumerId consumerId) {
	Value consumers;

	if (consumerId == 0 || !mrvmExecSessionsFindChildPath(runtimeKv, {"scheduler", "consumers"}, consumers)) return false;
	return runtimeKv.eraseChild(consumers, std::to_string(consumerId));
}

std::vector<MRRuntimeScheduledConsumerId> mrvmExecSessionsRuntimeScheduledConsumerIds(MRVMRuntimeKv &runtimeKv) {
	Value consumers;

	if (!mrvmExecSessionsFindChildPath(runtimeKv, {"scheduler", "consumers"}, consumers)) return std::vector<MRRuntimeScheduledConsumerId>();
	return mrvmExecSessionsSortedHashUintKeys(runtimeKv, consumers);
}

bool mrvmExecSessionsReadRuntimeScheduledConsumerSchedule(MRVMRuntimeKv &runtimeKv, MRRuntimeScheduledConsumerId consumerId, std::uint64_t &intervalMs, MRMacroExecutionSessionId &activeSessionId, std::uint64_t &nextDueMs) {
	Value consumers;
	Value consumerHash;
	Value config;

	intervalMs = 0;
	activeSessionId = 0;
	nextDueMs = 0;
	if (consumerId == 0 || !mrvmExecSessionsFindChildPath(runtimeKv, {"scheduler", "consumers"}, consumers)) return false;
	if (!runtimeKv.findChild(consumers, std::to_string(consumerId), consumerHash)) return false;
	if (!runtimeKv.findChild(consumerHash, "config", config)) return false;
	intervalMs = hashReadUint(runtimeKv, config, "intervalMs");
	activeSessionId = hashReadUint(runtimeKv, consumerHash, "activeSessionId");
	nextDueMs = hashReadUint(runtimeKv, consumerHash, "nextDueMs");
	return intervalMs != 0;
}

std::vector<MRRuntimeScheduledConsumer> mrvmExecSessionsRuntimeScheduledConsumers(MRVMRuntimeKv &runtimeKv) {
	Value consumers;
	std::vector<MRRuntimeScheduledConsumer> result;
	std::vector<std::uint64_t> consumerIds;

	if (!mrvmExecSessionsFindChildPath(runtimeKv, {"scheduler", "consumers"}, consumers)) return result;
	consumerIds = mrvmExecSessionsSortedHashUintKeys(runtimeKv, consumers);
	for (std::size_t index = 0; index < consumerIds.size(); ++index) {
		const std::uint64_t consumerId = consumerIds[index];
		Value consumerHash;
		MRRuntimeScheduledConsumer consumer;
		if (!runtimeKv.findChild(consumers, std::to_string(consumerId), consumerHash)) continue;
		if (readRuntimeScheduledConsumerHash(runtimeKv, consumerHash, consumer)) result.push_back(consumer);
	}
	return result;
}

void mrvmExecSessionsRecordRuntimeSchedulerEvent(MRVMRuntimeKv &runtimeKv, const MRRuntimeSchedulerEvent &event) {
	Value events = mrvmExecSessionsEnsureChildPath(runtimeKv, {"scheduler", "events", "recent"});
	Value eventHash;

	if (event.eventId == 0) return;
	eventHash = runtimeKv.replaceChild(events, std::to_string(event.eventId));
	writeRuntimeSchedulerEventHash(runtimeKv, eventHash, event);
	mrvmExecSessionsTrimHashByNumericKeys(runtimeKv, events, 64);
}

std::vector<MRRuntimeSchedulerEvent> mrvmExecSessionsRecentRuntimeSchedulerEvents(MRVMRuntimeKv &runtimeKv) {
	Value events;
	std::vector<MRRuntimeSchedulerEvent> result;
	std::vector<std::uint64_t> eventIds;

	if (!mrvmExecSessionsFindChildPath(runtimeKv, {"scheduler", "events", "recent"}, events)) return result;
	eventIds = mrvmExecSessionsSortedHashUintKeys(runtimeKv, events);
	for (std::size_t index = 0; index < eventIds.size(); ++index) {
		const std::uint64_t eventId = eventIds[index];
		Value eventHash;
		MRRuntimeSchedulerEvent event;
		if (!runtimeKv.findChild(events, std::to_string(eventId), eventHash)) continue;
		if (readRuntimeSchedulerEventHash(runtimeKv, eventHash, event)) result.push_back(event);
	}
	return result;
}

bool mrvmStoreExecSessionClosureInt(const std::string &closureId, const std::string &lvalue, int value) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	MRVMHashStore localStore;
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	bool stored = false;

	if (trimAscii(closureId).empty() || trimAscii(lvalue).empty()) return false;
	stored = mrvmExecSessionsWriteClosureVariable(runtimeKv, closureId, lvalue, mrvmMakeInt(value), localStore);
	if (!stored) mrLogMessage("MRMac exec session stale closure result discarded: closure='" + closureId + "' lvalue='" + lvalue + "'.");
	return stored;
}

bool mrvmApplyExecUiCommandRequest(const MRMacroExecUiCommandRequest &request) {
	MREditWindow *targetWindow = execUiCommandTargetWindow(request.target);
	const std::string action = normalizeExecUiCommandAction(request.command);
	const bool accepted = dispatchMRKeymapAction(action, std::string_view(), targetWindow);

	if (!request.closureId.empty() && !request.lvalue.empty()) static_cast<void>(mrvmStoreExecSessionClosureInt(request.closureId, request.lvalue, accepted ? 1 : 0));
	return accepted;
}

MRMacroExecutionSessionId mrvmNextMacroExecutionSessionId() {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmExecSessionsNextCounter(mrvmRuntimeKv(), "nextSessionId");
}

void mrvmStoreActiveMacroExecutionSession(const MRMacroExecutionSession &session) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value activeByTask = mrvmExecSessionsEnsureChildPath(runtimeKv, {"sessions", "active", "byTask"});
	Value sessionHash;

	if (session.taskId == 0) return;
	sessionHash = runtimeKv.replaceChild(activeByTask, std::to_string(session.taskId));
	mrvmExecSessionsWriteSession(runtimeKv, sessionHash, session);
}

std::vector<MRMacroExecutionSession> mrvmActiveMacroExecutionSessions() {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value activeByTask;
	std::vector<MRMacroExecutionSession> sessions;
	std::vector<std::uint64_t> taskIds;

	if (!mrvmExecSessionsFindChildPath(runtimeKv, {"sessions", "active", "byTask"}, activeByTask)) return sessions;
	taskIds = mrvmExecSessionsSortedHashUintKeys(runtimeKv, activeByTask);
	for (std::size_t index = 0; index < taskIds.size(); ++index) {
		const std::uint64_t taskId = taskIds[index];
		Value sessionHash;
		MRMacroExecutionSession session;
		if (!runtimeKv.findChild(activeByTask, std::to_string(taskId), sessionHash)) continue;
		if (mrvmExecSessionsReadSession(runtimeKv, sessionHash, session)) sessions.push_back(session);
	}
	return sessions;
}

std::vector<MRMacroExecutionSession> mrvmActiveMacroExecutionSessionsForOwner(const MRMacroExecutionOwner &owner) {
	std::vector<MRMacroExecutionSession> sessions = mrvmActiveMacroExecutionSessions();
	std::vector<MRMacroExecutionSession> matches;

	for (std::size_t index = 0; index < sessions.size(); ++index) {
		const MRMacroExecutionSession &session = sessions[index];
		if (macroExecutionOwnerMatches(session.owner, owner)) matches.push_back(session);
	}
	return matches;
}

bool mrvmMarkMacroExecutionSessionCancellationRequestedForTask(std::uint64_t taskId) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value activeByTask;
	Value sessionHash;
	MRMacroExecutionSession session;

	if (taskId == 0 || !mrvmExecSessionsFindChildPath(runtimeKv, {"sessions", "active", "byTask"}, activeByTask)) return false;
	if (!runtimeKv.findChild(activeByTask, std::to_string(taskId), sessionHash)) return false;
	if (!mrvmExecSessionsReadSession(runtimeKv, sessionHash, session)) return false;
	session.state = MRMacroExecutionState::CancellationRequested;
	mrvmExecSessionsWriteSession(runtimeKv, sessionHash, session);
	return true;
}

bool mrvmTakeActiveMacroExecutionSessionForTask(std::uint64_t taskId, MRMacroExecutionSession &session) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value activeByTask;
	Value sessionHash;
	const std::string key = std::to_string(taskId);

	if (taskId == 0 || !mrvmExecSessionsFindChildPath(runtimeKv, {"sessions", "active", "byTask"}, activeByTask)) return false;
	if (!runtimeKv.findChild(activeByTask, key, sessionHash)) return false;
	if (!mrvmExecSessionsReadSession(runtimeKv, sessionHash, session)) return false;
	return runtimeKv.eraseChild(activeByTask, key);
}

void mrvmRecordMacroExecutionResult(MRMacroExecutionSession session, MRMacroExecutionState state, const std::string &message) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value recentResults = mrvmExecSessionsEnsureChildPath(runtimeKv, {"results", "recent"});
	Value resultHash;
	MRMacroExecutionResult result;
	const std::uint64_t resultId = mrvmExecSessionsNextCounter(runtimeKv, "nextResultId");

	session.state = state;
	result.session = session;
	result.state = state;
	result.message = message;
	resultHash = runtimeKv.replaceChild(recentResults, std::to_string(resultId));
	mrvmExecSessionsWriteResult(runtimeKv, resultHash, result);
	mrvmExecSessionsTrimHashByNumericKeys(runtimeKv, recentResults, 32);
	static_cast<void>(mrvmExecSessionsEraseSessionRuntimeState(runtimeKv, session.sessionId));
}

std::vector<MRMacroExecutionResult> mrvmRecentMacroExecutionResults() {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value recentResults;
	std::vector<MRMacroExecutionResult> results;
	std::vector<std::uint64_t> resultIds;

	if (!mrvmExecSessionsFindChildPath(runtimeKv, {"results", "recent"}, recentResults)) return results;
	resultIds = mrvmExecSessionsSortedHashUintKeys(runtimeKv, recentResults);
	for (std::size_t index = 0; index < resultIds.size(); ++index) {
		const std::uint64_t resultId = resultIds[index];
		Value resultHash;
		MRMacroExecutionResult result;
		if (!runtimeKv.findChild(recentResults, std::to_string(resultId), resultHash)) continue;
		if (mrvmExecSessionsReadResult(runtimeKv, resultHash, result)) results.push_back(result);
	}
	return results;
}

void mrvmStorePendingForegroundMacroExecutionSession(const MRMacroExecutionSession &session) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value pendingDelay = mrvmExecSessionsEnsureChildPath(runtimeKv, {"sessions", "pending", "foregroundDelay"});
	Value sessionHash;

	if (session.sessionId == 0) return;
	sessionHash = runtimeKv.replaceChild(pendingDelay, std::to_string(session.sessionId));
	mrvmExecSessionsWriteSession(runtimeKv, sessionHash, session);
}

bool mrvmReadPendingForegroundMacroExecutionSession(MRMacroExecutionSessionId sessionId, MRMacroExecutionSession &session) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value pendingDelay;
	Value sessionHash;

	if (sessionId == 0 || !mrvmExecSessionsFindChildPath(runtimeKv, {"sessions", "pending", "foregroundDelay"}, pendingDelay)) return false;
	if (!runtimeKv.findChild(pendingDelay, std::to_string(sessionId), sessionHash)) return false;
	return mrvmExecSessionsReadSession(runtimeKv, sessionHash, session);
}

bool mrvmRemovePendingForegroundMacroExecutionSession(MRMacroExecutionSessionId sessionId) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value pendingDelay;

	if (sessionId == 0 || !mrvmExecSessionsFindChildPath(runtimeKv, {"sessions", "pending", "foregroundDelay"}, pendingDelay)) return false;
	return runtimeKv.eraseChild(pendingDelay, std::to_string(sessionId));
}

std::vector<MRMacroExecutionSession> mrvmPendingForegroundMacroExecutionSessions() {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value pendingDelay;
	std::vector<MRMacroExecutionSession> sessions;
	std::vector<std::uint64_t> sessionIds;

	if (!mrvmExecSessionsFindChildPath(runtimeKv, {"sessions", "pending", "foregroundDelay"}, pendingDelay)) return sessions;
	sessionIds = mrvmExecSessionsSortedHashUintKeys(runtimeKv, pendingDelay);
	for (std::size_t index = 0; index < sessionIds.size(); ++index) {
		const std::uint64_t sessionId = sessionIds[index];
		Value sessionHash;
		MRMacroExecutionSession session;
		if (!runtimeKv.findChild(pendingDelay, std::to_string(sessionId), sessionHash)) continue;
		if (mrvmExecSessionsReadSession(runtimeKv, sessionHash, session)) sessions.push_back(session);
	}
	return sessions;
}

MRMacroExecutionSessionListenerId mrvmNextMacroExecutionSessionListenerId() {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmExecSessionsNextCounter(mrvmRuntimeKv(), "nextListenerId");
}

void mrvmRegisterMacroExecutionSessionListener(MRMacroExecutionSessionListenerId listenerId) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value listeners = mrvmExecSessionsEnsureChildPath(runtimeKv, {"listeners", "registered"});
	Value listenerHash;

	if (listenerId == 0) return;
	listenerHash = runtimeKv.replaceChild(listeners, std::to_string(listenerId));
	hashWriteUint(runtimeKv, listenerHash, "listenerId", listenerId);
}

void mrvmRemoveMacroExecutionSessionListener(MRMacroExecutionSessionListenerId listenerId) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value listeners;

	if (listenerId == 0 || !mrvmExecSessionsFindChildPath(runtimeKv, {"listeners", "registered"}, listeners)) return;
	static_cast<void>(runtimeKv.eraseChild(listeners, std::to_string(listenerId)));
}

void mrvmNoteMacroExecutionSessionStatusChanged() {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value status = mrvmExecSessionsEnsureChildPath(runtimeKv, {"status"});
	const std::uint64_t generation = hashReadUint(runtimeKv, status, "generation") + 1;

	hashWriteUint(runtimeKv, status, "generation", generation);
}

std::uint64_t mrvmMacroExecutionSessionStatusGeneration() {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	Value status;

	if (!mrvmExecSessionsFindChildPath(runtimeKv, {"status"}, status)) return 0;
	return hashReadUint(runtimeKv, status, "generation");
}

MRRuntimeScheduledConsumerId mrvmNextRuntimeScheduledConsumerId() {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmExecSessionsNextCounter(mrvmRuntimeKv(), "nextScheduledConsumerId");
}

void mrvmStoreRuntimeScheduledConsumer(const MRRuntimeScheduledConsumer &consumer) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	mrvmExecSessionsStoreRuntimeScheduledConsumer(mrvmRuntimeKv(), consumer);
}

bool mrvmReadRuntimeScheduledConsumer(MRRuntimeScheduledConsumerId consumerId, MRRuntimeScheduledConsumer &consumer) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmExecSessionsReadRuntimeScheduledConsumer(mrvmRuntimeKv(), consumerId, consumer);
}

bool mrvmUpdateRuntimeScheduledConsumerActiveSession(MRRuntimeScheduledConsumerId consumerId, MRMacroExecutionSessionId activeSessionId) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmExecSessionsUpdateRuntimeScheduledConsumerActiveSession(mrvmRuntimeKv(), consumerId, activeSessionId);
}

bool mrvmUpdateRuntimeScheduledConsumerNextDue(MRRuntimeScheduledConsumerId consumerId, std::uint64_t nextDueMs) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmExecSessionsUpdateRuntimeScheduledConsumerNextDue(mrvmRuntimeKv(), consumerId, nextDueMs);
}

bool mrvmRemoveRuntimeScheduledConsumer(MRRuntimeScheduledConsumerId consumerId) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmExecSessionsRemoveRuntimeScheduledConsumer(mrvmRuntimeKv(), consumerId);
}

std::vector<MRRuntimeScheduledConsumerId> mrvmRuntimeScheduledConsumerIds() {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmExecSessionsRuntimeScheduledConsumerIds(mrvmRuntimeKv());
}

bool mrvmReadRuntimeScheduledConsumerSchedule(MRRuntimeScheduledConsumerId consumerId, std::uint64_t &intervalMs, MRMacroExecutionSessionId &activeSessionId, std::uint64_t &nextDueMs) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmExecSessionsReadRuntimeScheduledConsumerSchedule(mrvmRuntimeKv(), consumerId, intervalMs, activeSessionId, nextDueMs);
}

std::vector<MRRuntimeScheduledConsumer> mrvmRuntimeScheduledConsumers() {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmExecSessionsRuntimeScheduledConsumers(mrvmRuntimeKv());
}

MRRuntimeSchedulerEventId mrvmNextRuntimeSchedulerEventId() {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmExecSessionsNextCounter(mrvmRuntimeKv(), "nextSchedulerEventId");
}

void mrvmRecordRuntimeSchedulerEvent(const MRRuntimeSchedulerEvent &event) {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	mrvmExecSessionsRecordRuntimeSchedulerEvent(mrvmRuntimeKv(), event);
}

std::vector<MRRuntimeSchedulerEvent> mrvmRecentRuntimeSchedulerEvents() {
	std::lock_guard<std::recursive_mutex> executionLock(mrvmExecutionMutex());
	return mrvmExecSessionsRecentRuntimeSchedulerEvents(mrvmRuntimeKv());
}
