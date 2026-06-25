#ifndef MRVM_EXEC_SESSIONS_HPP
#define MRVM_EXEC_SESSIONS_HPP

#include "MRVMRuntimeKv.hpp"

#include "../MRMacroExecutionSession.hpp"
#include "../../app/MRRuntimeScheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

VirtualMachine::Value mrvmExecSessionsEnsureRoot(MRVMRuntimeKv &runtimeKv);
bool mrvmExecSessionsFindRoot(MRVMRuntimeKv &runtimeKv, VirtualMachine::Value &root);
VirtualMachine::Value mrvmExecSessionsEnsureChildPath(MRVMRuntimeKv &runtimeKv, std::initializer_list<const char *> keys);
bool mrvmExecSessionsFindChildPath(MRVMRuntimeKv &runtimeKv, std::initializer_list<const char *> keys, VirtualMachine::Value &child);

std::uint64_t mrvmExecSessionsNextCounter(MRVMRuntimeKv &runtimeKv, const std::string &key);

void mrvmExecSessionsWriteOwner(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const MRMacroExecutionOwner &owner);
MRMacroExecutionOwner mrvmExecSessionsReadOwner(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash);
void mrvmExecSessionsWriteSession(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const MRMacroExecutionSession &session);
bool mrvmExecSessionsReadSession(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, MRMacroExecutionSession &session);
void mrvmExecSessionsWriteResult(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, const MRMacroExecutionResult &result);
bool mrvmExecSessionsReadResult(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, MRMacroExecutionResult &result);

std::vector<std::uint64_t> mrvmExecSessionsSortedHashUintKeys(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash);
void mrvmExecSessionsTrimHashByNumericKeys(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &hash, std::size_t limit);

VirtualMachine::Value mrvmExecSessionsEnsureClosureState(MRVMRuntimeKv &runtimeKv, const std::string &closureId, int tickMs);
bool mrvmExecSessionsEraseClosureState(MRVMRuntimeKv &runtimeKv, const std::string &closureId);
bool mrvmExecSessionsReadClosureVariable(MRVMRuntimeKv &runtimeKv, const std::string &closureId, const std::string &name, VirtualMachine::Value &value);
bool mrvmExecSessionsWriteClosureVariable(MRVMRuntimeKv &runtimeKv, const std::string &closureId, const std::string &name, const VirtualMachine::Value &value, MRVMHashStore &localStore);

bool mrvmExecSessionsReadSessionVariable(MRVMRuntimeKv &runtimeKv, MRMacroExecutionSessionId sessionId, const std::string &name, VirtualMachine::Value &value);
bool mrvmExecSessionsWriteSessionVariable(MRVMRuntimeKv &runtimeKv, MRMacroExecutionSessionId sessionId, const std::string &name, const VirtualMachine::Value &value, MRVMHashStore &localStore);
bool mrvmExecSessionsEraseSessionRuntimeState(MRVMRuntimeKv &runtimeKv, MRMacroExecutionSessionId sessionId);

void mrvmExecSessionsStoreRuntimeScheduledConsumer(MRVMRuntimeKv &runtimeKv, const MRRuntimeScheduledConsumer &consumer);
bool mrvmExecSessionsReadRuntimeScheduledConsumer(MRVMRuntimeKv &runtimeKv, MRRuntimeScheduledConsumerId consumerId, MRRuntimeScheduledConsumer &consumer);
bool mrvmExecSessionsUpdateRuntimeScheduledConsumerActiveSession(MRVMRuntimeKv &runtimeKv, MRRuntimeScheduledConsumerId consumerId, MRMacroExecutionSessionId activeSessionId);
bool mrvmExecSessionsUpdateRuntimeScheduledConsumerNextDue(MRVMRuntimeKv &runtimeKv, MRRuntimeScheduledConsumerId consumerId, std::uint64_t nextDueMs);
bool mrvmExecSessionsRemoveRuntimeScheduledConsumer(MRVMRuntimeKv &runtimeKv, MRRuntimeScheduledConsumerId consumerId);
std::vector<MRRuntimeScheduledConsumerId> mrvmExecSessionsRuntimeScheduledConsumerIds(MRVMRuntimeKv &runtimeKv);
bool mrvmExecSessionsReadRuntimeScheduledConsumerSchedule(MRVMRuntimeKv &runtimeKv, MRRuntimeScheduledConsumerId consumerId, std::uint64_t &intervalMs, MRMacroExecutionSessionId &activeSessionId, std::uint64_t &nextDueMs);
std::vector<MRRuntimeScheduledConsumer> mrvmExecSessionsRuntimeScheduledConsumers(MRVMRuntimeKv &runtimeKv);

void mrvmExecSessionsRecordRuntimeSchedulerEvent(MRVMRuntimeKv &runtimeKv, const MRRuntimeSchedulerEvent &event);
std::vector<MRRuntimeSchedulerEvent> mrvmExecSessionsRecentRuntimeSchedulerEvents(MRVMRuntimeKv &runtimeKv);

#endif
