#include "MRGdbSession.hpp"

#include "MRGdbMi.hpp"
#include "../../mrmac/vm/MRVMRuntimeKv.hpp"
#include "../../mrmac/vm/MRVMValue.hpp"
#include "../../mrmac/mrmac.h"
#include <mutex>
#include <cstdlib>

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;
std::recursive_mutex &mrvmExecutionMutex() noexcept;

#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <poll.h>
#include <set>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <utility>

class MRGdbControlChannel {
  public:
	MRGdbControlChannel() noexcept : readFd(-1), writeFd(-1), mutex(), commands() {}
	~MRGdbControlChannel() {
		if (readFd >= 0) ::close(readFd);
		if (writeFd >= 0) ::close(writeFd);
	}

	int readFd;
	int writeFd;
	std::mutex mutex;
	std::deque<MRGdbCommand> commands;
};

namespace {

enum class PendingMiKind : unsigned char {
	None,
	Threads,
	Execution,
	ToggleQuery,
	BreakpointAssertQuery,
	BreakpointMutation,
	BreakpointRefresh,
	FrameDepth,
	VariableNames,
	VariableCreate,
	VariableChildren,
	VariableAssign,
	Evaluate,
	WatchCreate
};

struct PendingMiCommand {
	PendingMiCommand() noexcept : kind(PendingMiKind::None), text(), file(), objectName(), line(0), depth(0), rowLimit(0), refreshGeneration(0), watch(false) {}
	PendingMiKind kind;
	std::string text;
	std::string file;
	std::string objectName;
	std::string assertion;
	std::string threadId;
	int line;
	int depth;
	std::size_t rowLimit;
	std::uint64_t refreshGeneration;
	bool watch;
};

struct GdbProcess {
	pid_t pid = -1;
	int inputFd = -1;
	int outputFd = -1;
	int errorFd = -1;
	int ptyMasterFd = -1;
	int ptySlaveFd = -1;
	std::string ptySlaveName;
	bool childExited = false;
	int waitStatus = 0;
	bool outputOpen = false;
	bool errorOpen = false;
	bool quitRequested = false;
	VirtualMachine::Value state;
	VirtualMachine::Value session;
	MRGdbMiStream miStream;
	~GdbProcess();

	VirtualMachine::Value read(const std::string &key) const;
	void write(const std::string &key, const VirtualMachine::Value &value);
	std::uint64_t number(const std::string &key) const;
	void setNumber(const std::string &key, std::uint64_t value);
	void clear(const std::string &branch);
	std::vector<std::string> keys(const std::string &branch) const;
	PendingMiCommand takePending(const std::string &branch, const std::string &key);
	void storePending(const std::string &branch, const std::string &key, const PendingMiCommand &pending);
	VirtualMachine::Value threadState(const std::string &id) const;
	std::vector<std::string> takeVariableObjects(const std::string &id);
	VirtualMachine::Value threadFrame() const;
	void appendVariable(const MRGdbMiVariable &variable, bool watch);
	std::vector<MRGdbMiVariable> variables(bool watch) const;
};

GdbProcess::~GdbProcess() {
	if (session.type != TYPE_HASH) return;
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &kv = mrvmRuntimeKv();
	kv.eraseChild(session, "worker");
	kv.eraseChild(session, "threads");
	kv.eraseChild(session, "breakpoints");
	if (kv.globalStore().contains(session.hashHandle, "retiring")) {
		const VirtualMachine::Value app = kv.ensureRoot("APPLICATIONUI");
		const VirtualMachine::Value debugger = kv.ensureChild(app, "debugger");
		const VirtualMachine::Value retiring = kv.ensureChild(debugger, "retiring");
		const std::string generation = kv.globalStore().read(session.hashHandle, "generation").s;
		kv.eraseChild(retiring, generation);
	}
}

VirtualMachine::Value GdbProcess::read(const std::string &key) const {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMHashStore &store = mrvmRuntimeKv().globalStore();
	return store.contains(state.hashHandle, key) ? store.read(state.hashHandle, key) : VirtualMachine::Value();
}

void GdbProcess::write(const std::string &key, const VirtualMachine::Value &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	mrvmRuntimeKv().globalStore().write(state.hashHandle, key, value);
}

std::uint64_t GdbProcess::number(const std::string &key) const {
	const VirtualMachine::Value value = read(key);
	return value.type == TYPE_STR ? std::strtoull(value.s.c_str(), nullptr, 10) : 0;
}

void GdbProcess::setNumber(const std::string &key, std::uint64_t value) {
	write(key, mrvmMakeString(std::to_string(value)));
}

void GdbProcess::clear(const std::string &branch) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	if (branch == "localVariables" || branch == "watchVariables") {
		if (!read("valueThreadId").s.empty()) mrvmRuntimeKv().eraseChild(threadFrame(), branch);
	} else mrvmRuntimeKv().replaceChild(state, branch);
}

std::vector<std::string> GdbProcess::keys(const std::string &branch) const {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	VirtualMachine::Value entries;
	const VirtualMachine::Value parent = branch == "watches" ? threadState(read("valueThreadId").s) : state;
	if (!mrvmRuntimeKv().findChild(parent, branch, entries)) return {};
	return mrvmRuntimeKv().globalStore().keys(entries.hashHandle);
}

void GdbProcess::storePending(const std::string &branch, const std::string &key, const PendingMiCommand &pending) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &kv = mrvmRuntimeKv();
	MRVMHashStore &store = kv.globalStore();
	const VirtualMachine::Value entries = kv.ensureChild(state, branch);
	const VirtualMachine::Value item = kv.ensureChild(entries, key);
	store.write(item.hashHandle, "threadId", mrvmMakeString(pending.threadId));
	store.write(item.hashHandle, "kind", mrvmMakeInt(static_cast<int>(pending.kind)));
	store.write(item.hashHandle, "text", mrvmMakeString(pending.text));
	store.write(item.hashHandle, "file", mrvmMakeString(pending.file));
	store.write(item.hashHandle, "objectName", mrvmMakeString(pending.objectName));
	store.write(item.hashHandle, "assertion", mrvmMakeString(pending.assertion));
	store.write(item.hashHandle, "line", mrvmMakeInt(pending.line));
	store.write(item.hashHandle, "depth", mrvmMakeInt(pending.depth));
	store.write(item.hashHandle, "rowLimit", mrvmMakeString(std::to_string(pending.rowLimit)));
	store.write(item.hashHandle, "refreshGeneration", mrvmMakeString(std::to_string(pending.refreshGeneration)));
	store.write(item.hashHandle, "watch", mrvmMakeInt(pending.watch));
}

PendingMiCommand GdbProcess::takePending(const std::string &branch, const std::string &key) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &kv = mrvmRuntimeKv();
	MRVMHashStore &store = kv.globalStore();
	VirtualMachine::Value entries, item;
	PendingMiCommand pending;
	if (!kv.findChild(state, branch, entries) || !kv.findChild(entries, key, item)) return pending;
	pending.threadId = store.read(item.hashHandle, "threadId").s;
	pending.kind = static_cast<PendingMiKind>(store.read(item.hashHandle, "kind").i);
	pending.text = store.read(item.hashHandle, "text").s;
	pending.file = store.read(item.hashHandle, "file").s;
	pending.objectName = store.read(item.hashHandle, "objectName").s;
	pending.assertion = store.read(item.hashHandle, "assertion").s;
	pending.line = store.read(item.hashHandle, "line").i;
	pending.depth = store.read(item.hashHandle, "depth").i;
	pending.rowLimit = std::strtoull(store.read(item.hashHandle, "rowLimit").s.c_str(), nullptr, 10);
	pending.refreshGeneration = std::strtoull(store.read(item.hashHandle, "refreshGeneration").s.c_str(), nullptr, 10);
	pending.watch = store.read(item.hashHandle, "watch").i != 0;
	kv.eraseChild(entries, key);
	return pending;
}

VirtualMachine::Value GdbProcess::threadState(const std::string &id) const {
	// Caller holds the VM execution mutex.
	MRVMRuntimeKv &kv = mrvmRuntimeKv();
	return kv.ensureChild(kv.ensureChild(session, "threads"), id);
}

std::vector<std::string> GdbProcess::takeVariableObjects(const std::string &id) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &kv = mrvmRuntimeKv();
	MRVMHashStore &store = kv.globalStore();
	const VirtualMachine::Value thread = threadState(id);
	VirtualMachine::Value roots, watches;
	std::vector<std::string> objects;
	if (kv.findChild(thread, "localVariableRoots", roots)) objects = store.keys(roots.hashHandle);
	kv.eraseChild(thread, "localVariableRoots");
	if (kv.findChild(thread, "watches", watches))
		for (const std::string &key : store.keys(watches.hashHandle)) {
			const VirtualMachine::Value watch = store.read(watches.hashHandle, key);
			const std::string name = store.read(watch.hashHandle, "objectName").s;
			if (!name.empty()) objects.push_back(name);
			store.write(watch.hashHandle, "objectName", mrvmMakeString(std::string()));
		}
	return objects;
}

VirtualMachine::Value GdbProcess::threadFrame() const {
	MRVMRuntimeKv &kv = mrvmRuntimeKv();
	return kv.ensureChild(kv.ensureChild(threadState(read("valueThreadId").s), "frames"), "0");
}

void GdbProcess::appendVariable(const MRGdbMiVariable &variable, bool watch) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &kv = mrvmRuntimeKv();
	MRVMHashStore &store = kv.globalStore();
	const char *branch = watch ? "watchVariables" : "localVariables";
	const char *countKey = watch ? "watchVariableCount" : "localVariableCount";
	const std::uint64_t count = number(countKey);
	const VirtualMachine::Value entries = kv.ensureChild(threadFrame(), branch);
	const VirtualMachine::Value item = kv.ensureChild(entries, std::to_string(count));
	store.write(item.hashHandle, "identity", mrvmMakeString(variable.identity));
	store.write(item.hashHandle, "name", mrvmMakeString(variable.name));
	store.write(item.hashHandle, "value", mrvmMakeString(variable.value));
	store.write(item.hashHandle, "type", mrvmMakeString(variable.type));
	store.write(item.hashHandle, "objectName", mrvmMakeString(variable.objectName));
	store.write(item.hashHandle, "parentObjectName", mrvmMakeString(variable.parentObjectName));
	store.write(item.hashHandle, "depth", mrvmMakeInt(variable.depth));
	store.write(item.hashHandle, "childCount", mrvmMakeInt(variable.childCount));
	setNumber(countKey, count + 1);
}

std::vector<MRGdbMiVariable> GdbProcess::variables(bool watch) const {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &kv = mrvmRuntimeKv();
	MRVMHashStore &store = kv.globalStore();
	VirtualMachine::Value entries, item;
	std::vector<MRGdbMiVariable> result;
	if (read("valueThreadId").s.empty() || !kv.findChild(threadFrame(), watch ? "watchVariables" : "localVariables", entries)) return result;
	const std::uint64_t count = number(watch ? "watchVariableCount" : "localVariableCount");
	result.reserve(count);
	for (std::uint64_t index = 0; index < count; ++index) {
		if (!kv.findChild(entries, std::to_string(index), item)) continue;
		MRGdbMiVariable variable;
		variable.identity = store.read(item.hashHandle, "identity").s;
		variable.name = store.read(item.hashHandle, "name").s;
		variable.value = store.read(item.hashHandle, "value").s;
		variable.type = store.read(item.hashHandle, "type").s;
		variable.objectName = store.read(item.hashHandle, "objectName").s;
		variable.parentObjectName = store.read(item.hashHandle, "parentObjectName").s;
		variable.depth = store.read(item.hashHandle, "depth").i;
		variable.childCount = store.read(item.hashHandle, "childCount").i;
		result.push_back(std::move(variable));
	}
	return result;
}

const int kVariableMaximumDepth = 8;
const std::size_t kVariableMaximumRows = 512 * 1024;

void closeFd(int &fd) noexcept {
	if (fd >= 0) ::close(fd);
	fd = -1;
}

void setCloseOnExec(int fd) noexcept {
	const int flags = ::fcntl(fd, F_GETFD, 0);
	if (flags >= 0) static_cast<void>(::fcntl(fd, F_SETFD, flags | FD_CLOEXEC));
}

void setNonBlocking(int fd) noexcept {
	const int flags = ::fcntl(fd, F_GETFL, 0);
	if (flags >= 0) static_cast<void>(::fcntl(fd, F_SETFL, flags | O_NONBLOCK));
}

bool writeAll(int fd, const std::string &text) {
	std::size_t offset = 0;
	while (offset < text.size()) {
		const ssize_t written = ::write(fd, text.data() + offset, text.size() - offset);
		if (written > 0) {
			offset += static_cast<std::size_t>(written);
			continue;
		}
		if (written < 0 && errno == EINTR) continue;
		return false;
	}
	return true;
}

void postGdbEvent(const GdbProcess &process, const mr::coprocessor::TaskInfo &info, std::size_t sourceId, int targetBufferId, std::uint64_t generation, MRGdbEvent event) {
	if (event.threadId.empty()) event.threadId = process.read("threadId").s;
	event.stopGeneration = process.number("stopGeneration");
	event.contextGeneration = process.number("contextGeneration");
	mr::coprocessor::Result result;
	result.task = info;
	result.status = mr::coprocessor::TaskStatus::Completed;
	result.payload = std::make_shared<mr::coprocessor::GdbStreamEventPayload>(sourceId, targetBufferId, generation, std::move(event));
	mr::coprocessor::globalCoprocessor().post(std::move(result));
}

unsigned sendMi(GdbProcess &process, const std::string &command, PendingMiCommand pending = PendingMiCommand()) {
	const unsigned token = static_cast<unsigned>(process.number("nextToken"));
	process.setNumber("nextToken", token + 1);
	if (!writeAll(process.inputFd, std::to_string(token) + command + "\n")) return 0;
	if (pending.kind != PendingMiKind::None) process.storePending("requests", std::to_string(token), pending);
	return token;
}

std::string sourceLocation(const std::string &file, int line) {
	return mrGdbMiQuote(file + ":" + std::to_string(line));
}

void dispatchBreakpointToggle(GdbProcess &process) {
	if (process.read("breakpointBusy").i || process.number("breakpointHead") == process.number("breakpointTail")) return;
	PendingMiCommand pending = process.takePending("breakpointToggles", std::to_string(process.number("breakpointHead")));
	process.setNumber("breakpointHead", process.number("breakpointHead") + 1);
	process.write("breakpointBusy", mrvmMakeInt(true));
	static_cast<void>(sendMi(process, "-break-list", std::move(pending)));
}

void requestBreakpointRefresh(GdbProcess &process) {
	PendingMiCommand pending;
	pending.kind = PendingMiKind::BreakpointRefresh;
	static_cast<void>(sendMi(process, "-break-list", std::move(pending)));
}

void requestStoppedState(GdbProcess &process, bool nextThread = false) {
	if (!nextThread) {
		std::vector<std::string> objects;
		{
			std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
			MRVMRuntimeKv &kv = mrvmRuntimeKv();
			VirtualMachine::Value threads;
			if (kv.findChild(process.session, "threads", threads))
				for (const std::string &id : kv.globalStore().keys(threads.hashHandle)) {
					const std::vector<std::string> retired = process.takeVariableObjects(id);
					objects.insert(objects.end(), retired.begin(), retired.end());
				}
		}
		for (const std::string &name : objects) static_cast<void>(sendMi(process, "-var-delete " + name));
		const std::string source = process.read("threadId").s;
		const std::string variables = process.read("variablesThreadId").s.empty() ? source : process.read("variablesThreadId").s;
		const std::string watches = process.read("watchesThreadId").s.empty() ? source : process.read("watchesThreadId").s;
		process.write("valueThreadId", mrvmMakeString(variables));
		process.write("nextValueThreadId", mrvmMakeString(watches == variables ? std::string() : watches));
	}
	if (process.read("valueThreadId").s.empty()) return;
	{
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		const VirtualMachine::Value thread = process.threadState(process.read("valueThreadId").s);
		MRVMHashStore &store = mrvmRuntimeKv().globalStore();
		process.write("valueFrameIdentity", store.read(thread.hashHandle, "frameIdentity"));
	}
	process.clear("localVariables");
	process.setNumber("localVariableCount", 0);
	process.clear("watchVariables");
	process.setNumber("watchVariableCount", 0);
	process.clear("expansions");
	process.setNumber("expansionHead", 0);
	process.setNumber("expansionTail", 0);
	process.setNumber("variableRefreshGeneration", process.number("variableRefreshGeneration") + 1);
	process.setNumber("variableOutstanding", 0);
	process.write("variableChildrenRunning", mrvmMakeInt(false));
	PendingMiCommand frame;
	frame.kind = PendingMiKind::FrameDepth;
	frame.refreshGeneration = process.number("variableRefreshGeneration");
	if (sendMi(process, "-stack-info-depth --thread " + process.read("valueThreadId").s, std::move(frame)) != 0) process.setNumber("variableOutstanding", process.number("variableOutstanding") + 1);
	PendingMiCommand variables;
	variables.kind = PendingMiKind::VariableNames;
	variables.refreshGeneration = process.number("variableRefreshGeneration");
	const std::string localsThread = process.read("variablesThreadId").s.empty() ? process.read("threadId").s : process.read("variablesThreadId").s;
	const std::string watchesThread = process.read("watchesThreadId").s.empty() ? process.read("threadId").s : process.read("watchesThreadId").s;
	if (process.read("valueThreadId").s == localsThread && sendMi(process, "-stack-list-variables --thread " + process.read("valueThreadId").s + " --frame 0 --no-values", std::move(variables)) != 0) process.setNumber("variableOutstanding", process.number("variableOutstanding") + 1);
	for (const std::string &watchId : process.read("valueThreadId").s == watchesThread ? process.keys("watches") : std::vector<std::string>()) {
		std::string expression, objectName;
		{
			std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
			MRVMRuntimeKv &kv = mrvmRuntimeKv();
			const VirtualMachine::Value watches = kv.ensureChild(process.threadState(process.read("valueThreadId").s), "watches");
			const VirtualMachine::Value watch = kv.ensureChild(watches, watchId);
			expression = kv.globalStore().read(watch.hashHandle, "expression").s;
			objectName = kv.globalStore().read(watch.hashHandle, "objectName").s;
			kv.globalStore().write(watch.hashHandle, "objectName", mrvmMakeString(std::string()));
		}
		if (!objectName.empty()) static_cast<void>(sendMi(process, "-var-delete " + objectName));
		PendingMiCommand create;
		create.kind = PendingMiKind::WatchCreate;
		create.text = watchId;
		create.refreshGeneration = process.number("variableRefreshGeneration");
		if (sendMi(process, "-var-create --thread " + process.read("valueThreadId").s + " --frame 0 - * " + mrGdbMiQuote(expression), std::move(create)) != 0) process.setNumber("variableOutstanding", process.number("variableOutstanding") + 1);
	}
	if (!nextThread) requestBreakpointRefresh(process);
}

void invalidateVariableRefresh(GdbProcess &process) noexcept {
	process.setNumber("variableRefreshGeneration", process.number("variableRefreshGeneration") + 1);
	process.setNumber("variableOutstanding", 0);
	process.clear("expansions");
	process.setNumber("expansionHead", 0);
	process.setNumber("expansionTail", 0);
	process.write("variableChildrenRunning", mrvmMakeInt(false));
	{
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		MRVMRuntimeKv &kv = mrvmRuntimeKv();
		VirtualMachine::Value threads;
		if (kv.findChild(process.session, "threads", threads))
			for (const std::string &id : kv.globalStore().keys(threads.hashHandle)) kv.eraseChild(process.threadState(id), "frames");
	}
	process.write("nextValueThreadId", mrvmMakeString(std::string()));
	process.setNumber("localVariableCount", 0);
	process.setNumber("watchVariableCount", 0);
}

void requestThreadSnapshot(GdbProcess &process) {
	invalidateVariableRefresh(process);
	PendingMiCommand pending;
	pending.kind = PendingMiKind::Threads;
	pending.refreshGeneration = process.number("variableRefreshGeneration");
	static_cast<void>(sendMi(process, "-thread-info", std::move(pending)));
}

bool breakpointTargetsSource(const MRGdbMiBreakpoint &breakpoint, const std::string &sourcePath) {
	if (sourcePath.empty() || breakpoint.file.empty()) return true;
	if (breakpoint.file == sourcePath) return true;
	const std::filesystem::path breakpointPath(breakpoint.file);
	const std::filesystem::path source(sourcePath);
	return breakpointPath.parent_path().empty() && breakpointPath.filename() == source.filename();
}

void postBreakpointProjection(const GdbProcess &process, const mr::coprocessor::TaskInfo &info, std::size_t sourceId, int targetBufferId, std::uint64_t generation, const std::string &raw) {
	std::vector<MRGdbMiBreakpoint> breakpoints;
	MRGdbEvent event;
	mrGdbMiBreakpoints(raw, breakpoints);
	event.kind = MRGdbEventKind::Breakpoints;
	{
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		MRVMRuntimeKv &kv = mrvmRuntimeKv();
		kv.replaceChild(process.session, "breakpoints");
		for (const MRGdbMiBreakpoint &breakpoint : breakpoints) {
			if (breakpoint.temporary) continue;
			const VirtualMachine::Value item = kv.ensureChild(kv.ensureChild(process.session, "breakpoints"), breakpoint.number);
			kv.globalStore().write(item.hashHandle, "file", mrvmMakeString(breakpoint.file));
			kv.globalStore().write(item.hashHandle, "line", mrvmMakeInt(breakpoint.line));
			kv.globalStore().write(item.hashHandle, "condition", mrvmMakeString(breakpoint.condition));
			if (breakpoint.line > 0 && breakpointTargetsSource(breakpoint, process.read("sourcePath").s)) event.breakpointLines.push_back(breakpoint.line);
		}
	}
	event.breakpoints = std::move(breakpoints);
	postGdbEvent(process, info, sourceId, targetBufferId, generation, std::move(event));
}

void handleToggleQuery(GdbProcess &process, const PendingMiCommand &pending, const std::string &raw) {
	std::vector<MRGdbMiBreakpoint> breakpoints;
	std::string matchingNumbers;
	mrGdbMiBreakpoints(raw, breakpoints);
	for (const MRGdbMiBreakpoint &breakpoint : breakpoints) {
		if (breakpoint.temporary) continue;
		if (breakpoint.originalLocation != pending.file + ":" + std::to_string(pending.line) &&
		    (breakpoint.line != pending.line || !breakpointTargetsSource(breakpoint, pending.file))) continue;
		if (!matchingNumbers.empty()) matchingNumbers += ' ';
		matchingNumbers += breakpoint.number;
	}
	PendingMiCommand mutation;
	mutation.kind = PendingMiKind::BreakpointMutation;
	mutation.file = pending.file;
	mutation.line = pending.line;
	mutation.assertion = pending.text;
	if (pending.kind == PendingMiKind::BreakpointAssertQuery) {
		const bool hasCondition = pending.text.find_first_not_of(" \t\r\n") != std::string::npos;
		if (matchingNumbers.empty() || matchingNumbers.find(' ') != std::string::npos) {
			process.write("breakpointBusy", mrvmMakeInt(false));
			dispatchBreakpointToggle(process);
			return;
		}
		mutation.text = "assert";
		static_cast<void>(sendMi(process, "-break-condition " + matchingNumbers + (hasCondition ? " " + mrGdbMiQuote(pending.text) : std::string()), mutation));
	} else if (matchingNumbers.empty()) {
		mutation.text = "add";
		static_cast<void>(sendMi(process, "-break-insert -f " + sourceLocation(pending.file, pending.line), mutation));
	} else {
		mutation.text = "delete";
		static_cast<void>(sendMi(process, "-break-delete " + matchingNumbers, mutation));
	}
}

void persistBreakpointMutation(const GdbProcess &process, const PendingMiCommand &pending) {
	if (pending.file.empty() || pending.line <= 0 || pending.text == "restore") return;
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &kv = mrvmRuntimeKv();
	MRVMHashStore &store = kv.globalStore();
	if (store.contains(process.session.hashHandle, "retiring")) return;
	const VirtualMachine::Value debugger = kv.ensureChild(kv.ensureRoot("APPLICATIONUI"), "debugger");
	const VirtualMachine::Value sources = kv.ensureChild(kv.ensureChild(debugger, "breakpoints"), "bySource");
	const VirtualMachine::Value source = kv.ensureChild(sources, pending.file);
	const VirtualMachine::Value lines = kv.ensureChild(source, "lines");
	const VirtualMachine::Value assertions = kv.ensureChild(source, "asserts");
	const std::string line = std::to_string(pending.line);
	if (pending.text == "delete") {
		kv.eraseChild(lines, line);
		kv.eraseChild(assertions, line);
	} else {
		store.write(lines.hashHandle, line, mrvmMakeInt(1));
		if (pending.assertion.empty()) kv.eraseChild(assertions, line);
		else store.write(assertions.hashHandle, line, mrvmMakeString(pending.assertion));
	}
}

bool variableRefreshCommand(PendingMiKind kind) noexcept {
	return kind == PendingMiKind::FrameDepth || kind == PendingMiKind::WatchCreate || kind == PendingMiKind::VariableNames || kind == PendingMiKind::VariableCreate || kind == PendingMiKind::VariableChildren;
}

void appendVariableTree(const std::string &parentObjectName, const std::vector<MRGdbMiVariable> &source, std::vector<MRGdbMiVariable> &target, std::set<std::string> &visited) {
	for (const MRGdbMiVariable &variable : source) {
		if (variable.parentObjectName != parentObjectName || !visited.insert(variable.objectName.empty() ? variable.identity : variable.objectName).second) continue;
		target.push_back(variable);
		if (!variable.objectName.empty()) appendVariableTree(variable.objectName, source, target, visited);
	}
}

void postVariableProjectionIfComplete(GdbProcess &process, const mr::coprocessor::TaskInfo &info, std::size_t sourceId, int targetBufferId, std::uint64_t generation) {
	if (process.number("variableOutstanding") != 0 || process.read("variableChildrenRunning").i || (process.number("expansionHead") != process.number("expansionTail"))) return;
	MRGdbEvent event;
	std::set<std::string> visited;
	event.kind = MRGdbEventKind::Variables;
	event.threadId = process.read("valueThreadId").s;
	event.text = process.read("valueFrameIdentity").s + ":0:" + std::to_string(process.number("frameDepth"));
	event.variables.reserve(process.number("localVariableCount"));
	appendVariableTree(std::string(), process.variables(false), event.variables, visited);
	postGdbEvent(process, info, sourceId, targetBufferId, generation, std::move(event));
	MRGdbEvent watchEvent;
	watchEvent.kind = MRGdbEventKind::Watches;
	watchEvent.threadId = process.read("valueThreadId").s;
	watchEvent.text = process.read("valueFrameIdentity").s + ":0:" + std::to_string(process.number("frameDepth"));
	visited.clear();
	appendVariableTree(std::string(), process.variables(true), watchEvent.variables, visited);
	postGdbEvent(process, info, sourceId, targetBufferId, generation, std::move(watchEvent));
	const std::string next = process.read("nextValueThreadId").s;
	if (!next.empty()) {
		process.write("nextValueThreadId", mrvmMakeString(std::string()));
		process.write("valueThreadId", mrvmMakeString(next));
		requestStoppedState(process, true);
	}
}

void dispatchNextVariableChildren(GdbProcess &process) {
	if (process.read("variableChildrenRunning").i) return;
	while ((process.number("expansionHead") != process.number("expansionTail"))) {
		PendingMiCommand children = process.takePending("expansions", std::to_string(process.number("expansionHead")));
		process.setNumber("expansionHead", process.number("expansionHead") + 1);
		if (children.refreshGeneration != process.number("variableRefreshGeneration")) continue;
		const std::size_t rowCount = children.watch ? process.number("watchVariableCount") : process.number("localVariableCount");
		if (rowCount >= kVariableMaximumRows) continue;
		children.rowLimit = kVariableMaximumRows - rowCount;
		const std::string command = "-var-list-children --all-values " + children.objectName + " 0 " + std::to_string(children.rowLimit);
		if (sendMi(process, command, std::move(children)) != 0) {
			process.setNumber("variableOutstanding", process.number("variableOutstanding") + 1);
			process.write("variableChildrenRunning", mrvmMakeInt(true));
			return;
		}
	}
}

void finishVariableCommand(GdbProcess &process, const PendingMiCommand &pending, const mr::coprocessor::TaskInfo &info, std::size_t sourceId, int targetBufferId, std::uint64_t generation) {
	if (!variableRefreshCommand(pending.kind) || pending.refreshGeneration != process.number("variableRefreshGeneration")) return;
	if (pending.kind == PendingMiKind::VariableChildren) process.write("variableChildrenRunning", mrvmMakeInt(false));
	if (process.number("variableOutstanding") > 0) process.setNumber("variableOutstanding", process.number("variableOutstanding") - 1);
	dispatchNextVariableChildren(process);
	postVariableProjectionIfComplete(process, info, sourceId, targetBufferId, generation);
}

void requestVariableChildren(GdbProcess &process, const std::string &objectName, int depth, bool watch = false) {
	const std::size_t rowCount = watch ? process.number("watchVariableCount") : process.number("localVariableCount");
	if (objectName.empty() || depth > kVariableMaximumDepth || rowCount >= kVariableMaximumRows || (process.number("expansionTail") - process.number("expansionHead")) >= 2 * kVariableMaximumRows) return;
	PendingMiCommand children;
	children.kind = PendingMiKind::VariableChildren;
	children.objectName = objectName;
	children.depth = depth;
	children.watch = watch;
	children.refreshGeneration = process.number("variableRefreshGeneration");
	process.storePending("expansions", std::to_string(process.number("expansionTail")), children);
	process.setNumber("expansionTail", process.number("expansionTail") + 1);
}

void handleMiRecord(GdbProcess &process, const MRGdbMiRecord &record, const mr::coprocessor::TaskInfo &info, std::size_t sourceId, int targetBufferId, std::uint64_t generation) {
	if (record.kind == MRGdbMiRecordKind::Console || record.kind == MRGdbMiRecordKind::Log) {
		if (!record.text.empty()) {
			MRGdbEvent event;
			event.kind = MRGdbEventKind::DebuggerOutput;
			event.text = record.text;
			postGdbEvent(process, info, sourceId, targetBufferId, generation, std::move(event));
		}
		return;
	}
	if (record.kind == MRGdbMiRecordKind::Target) return;
	if (record.kind == MRGdbMiRecordKind::Exec && record.resultClass == "running") {
		process.write("inferiorHasRun", mrvmMakeInt(true));
		process.write("stopped", mrvmMakeInt(false));
		invalidateVariableRefresh(process);
		MRGdbEvent event;
		event.kind = MRGdbEventKind::Running;
		postGdbEvent(process, info, sourceId, targetBufferId, generation, std::move(event));
		return;
	}
	if (record.kind == MRGdbMiRecordKind::Exec && record.resultClass == "stopped") {
		MRGdbEvent event;
		event.kind = MRGdbEventKind::Stopped;
		event.text = mrGdbMiField(record.raw, "reason");
		event.file = mrGdbMiField(record.raw, "fullname");
		if (event.file.empty()) event.file = mrGdbMiField(record.raw, "file");
		event.line = mrGdbMiIntField(record.raw, "line", 0);
		process.write("threadId", mrvmMakeString(mrGdbMiField(record.raw, "thread-id")));
		process.setNumber("stopGeneration", process.number("stopGeneration") + 1);
		process.write("frameIdentity", mrvmMakeString(event.file + ":" + mrGdbMiField(record.raw, "func") + ":" + mrGdbMiField(record.raw, "thread-id")));
		process.setNumber("frameDepth", 0);
		const bool inferiorExited = event.text.rfind("exited", 0) == 0;
		process.write("stopped", mrvmMakeInt(!inferiorExited));
		invalidateVariableRefresh(process);
		process.write("variablesThreadId", mrvmMakeString(std::string()));
		process.write("watchesThreadId", mrvmMakeString(std::string()));
		if (inferiorExited) {
			process.write("inferiorHasRun", mrvmMakeInt(false));
			std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
			mrvmRuntimeKv().eraseChild(process.session, "threads");
			process.write("valueThreadId", mrvmMakeString(std::string()));
		}
		postGdbEvent(process, info, sourceId, targetBufferId, generation, std::move(event));
		if (!inferiorExited) requestThreadSnapshot(process);
		return;
	}
	if (record.kind != MRGdbMiRecordKind::Result) return;
	const PendingMiCommand pending = process.takePending("requests", std::to_string(record.token));
	if (record.resultClass == "running") {
		process.write("inferiorHasRun", mrvmMakeInt(true));
		process.write("stopped", mrvmMakeInt(false));
		invalidateVariableRefresh(process);
		MRGdbEvent event;
		event.kind = MRGdbEventKind::Running;
		postGdbEvent(process, info, sourceId, targetBufferId, generation, std::move(event));
	}
	if ((variableRefreshCommand(pending.kind) || pending.kind == PendingMiKind::Threads || pending.kind == PendingMiKind::Evaluate || pending.kind == PendingMiKind::VariableAssign) && pending.refreshGeneration != process.number("variableRefreshGeneration")) {
		if (pending.kind == PendingMiKind::VariableCreate || pending.kind == PendingMiKind::WatchCreate) {
			const std::string objectName = mrGdbMiField(record.raw, "name");
			if (!objectName.empty()) static_cast<void>(sendMi(process, "-var-delete " + objectName));
		}
		return;
	}
	if (pending.kind == PendingMiKind::BreakpointMutation && pending.text == "restore" && process.number("breakpointRestores") > 0)
		process.setNumber("breakpointRestores", process.number("breakpointRestores") - 1);
	if (record.resultClass == "error") {
		if (pending.kind == PendingMiKind::ToggleQuery || pending.kind == PendingMiKind::BreakpointAssertQuery || pending.kind == PendingMiKind::BreakpointMutation)
			process.write("pendingExecution", mrvmMakeInt(0));
		if (pending.kind == PendingMiKind::ToggleQuery || pending.kind == PendingMiKind::BreakpointAssertQuery ||
		    (pending.kind == PendingMiKind::BreakpointMutation && pending.text != "restore")) {
			process.write("breakpointBusy", mrvmMakeInt(false));
			dispatchBreakpointToggle(process);
		}
		MRGdbEvent event;
		event.kind = MRGdbEventKind::DebuggerOutput;
		if (pending.kind == PendingMiKind::WatchCreate) {
			{
				std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
				MRVMRuntimeKv &kv = mrvmRuntimeKv();
				VirtualMachine::Value watches, watch;
				if (kv.findChild(process.threadState(process.read("valueThreadId").s), "watches", watches) && kv.findChild(watches, pending.text, watch)) {
					MRGdbMiVariable variable;
					variable.identity = pending.text;
					variable.name = kv.globalStore().read(watch.hashHandle, "expression").s;
					variable.value = "<out of scope>";
					process.appendVariable(variable, true);
				}
			}
			finishVariableCommand(process, pending, info, sourceId, targetBufferId, generation);
			return;
		}
		event.text = "GDB: " + mrGdbMiField(record.raw, "msg") + "\n";
		postGdbEvent(process, info, sourceId, targetBufferId, generation, std::move(event));
		if (pending.kind == PendingMiKind::Execution) {
			process.write("stopped", mrvmMakeInt(true));
			MRGdbEvent stopped;
			stopped.kind = MRGdbEventKind::Stopped;
			stopped.text = "command-error";
			postGdbEvent(process, info, sourceId, targetBufferId, generation, std::move(stopped));
			requestThreadSnapshot(process);
		} else if (pending.kind == PendingMiKind::Threads) {
			MRGdbEvent threads;
			threads.kind = MRGdbEventKind::Threads;
			process.write("threadId", mrvmMakeString(std::string()));
			postGdbEvent(process, info, sourceId, targetBufferId, generation, std::move(threads));
		}
		if (pending.kind == PendingMiKind::Evaluate || pending.kind == PendingMiKind::VariableAssign) requestStoppedState(process);
		finishVariableCommand(process, pending, info, sourceId, targetBufferId, generation);
		return;
	}
	switch (pending.kind) {
		case PendingMiKind::Threads: {
			MRGdbEvent event;
			event.kind = MRGdbEventKind::Threads;
			mrGdbMiThreads(record.raw, event.threads);
			const MRGdbMiThread *selected = nullptr;
			for (const MRGdbMiThread &thread : event.threads)
				if (thread.id == process.read("threadId").s) selected = &thread;
			if (selected == nullptr && !event.threads.empty()) selected = &event.threads.front();
			process.write("threadId", mrvmMakeString(selected != nullptr ? selected->id : std::string()));
			if (selected != nullptr) {
				event.file = selected->file;
				event.line = selected->line;
				process.write("frameIdentity", mrvmMakeString(selected->file + ":" + selected->function + ":" + selected->id));
			}
			std::vector<std::string> obsoleteObjects;
			{
				std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
				MRVMRuntimeKv &kv = mrvmRuntimeKv();
				const VirtualMachine::Value threads = kv.ensureChild(process.session, "threads");
				for (const std::string &id : kv.globalStore().keys(threads.hashHandle)) {
					bool present = false;
					for (const MRGdbMiThread &thread : event.threads) if (thread.id == id) present = true;
					if (!present) {
						const std::vector<std::string> retired = process.takeVariableObjects(id);
						obsoleteObjects.insert(obsoleteObjects.end(), retired.begin(), retired.end());
						kv.eraseChild(threads, id);
					}
				}
				for (const MRGdbMiThread &thread : event.threads) {
					const VirtualMachine::Value item = process.threadState(thread.id);
					kv.globalStore().write(item.hashHandle, "state", mrvmMakeString(thread.state));
					kv.globalStore().write(item.hashHandle, "frameIdentity", mrvmMakeString(thread.file + ":" + thread.function + ":" + thread.id));
				}
				for (const char *key : {"variablesThreadId", "watchesThreadId"}) {
					const std::string id = process.read(key).s;
					if (!id.empty() && !kv.globalStore().contains(threads.hashHandle, id)) process.write(key, mrvmMakeString(std::string()));
				}
			}
			for (const std::string &name : obsoleteObjects) static_cast<void>(sendMi(process, "-var-delete " + name));
			event.variablesThreadId = process.read("variablesThreadId").s.empty() ? process.read("threadId").s : process.read("variablesThreadId").s;
			event.watchesThreadId = process.read("watchesThreadId").s.empty() ? process.read("threadId").s : process.read("watchesThreadId").s;
			process.setNumber("frameDepth", 0);
			postGdbEvent(process, info, sourceId, targetBufferId, generation, std::move(event));
			if (!process.read("threadId").s.empty()) requestStoppedState(process);
			break;
		}

		case PendingMiKind::ToggleQuery:
		case PendingMiKind::BreakpointAssertQuery:
			handleToggleQuery(process, pending, record.raw);
			break;
		case PendingMiKind::BreakpointMutation:
			persistBreakpointMutation(process, pending);
			if (pending.text != "restore") {
				process.write("breakpointBusy", mrvmMakeInt(false));
				dispatchBreakpointToggle(process);
			}
			requestBreakpointRefresh(process);
			break;
		case PendingMiKind::BreakpointRefresh:
			postBreakpointProjection(process, info, sourceId, targetBufferId, generation, record.raw);
			break;
		case PendingMiKind::FrameDepth:
			if (pending.refreshGeneration == process.number("variableRefreshGeneration")) process.setNumber("frameDepth", mrGdbMiIntField(record.raw, "depth", 0));
			finishVariableCommand(process, pending, info, sourceId, targetBufferId, generation);
			break;
		case PendingMiKind::VariableNames: {
			if (pending.refreshGeneration == process.number("variableRefreshGeneration")) {
				std::vector<MRGdbMiVariable> variables;
				mrGdbMiVariables(record.raw, variables);
				for (const MRGdbMiVariable &variable : variables) {
					if (process.number("localVariableCount") + process.number("variableOutstanding") >= kVariableMaximumRows) break;
					PendingMiCommand create;
					create.kind = PendingMiKind::VariableCreate;
					create.text = variable.name;
					create.refreshGeneration = process.number("variableRefreshGeneration");
					if (sendMi(process, "-var-create --thread " + process.read("valueThreadId").s + " --frame 0 - * " + mrGdbMiQuote(variable.name), std::move(create)) != 0) process.setNumber("variableOutstanding", process.number("variableOutstanding") + 1);
				}
			}
			finishVariableCommand(process, pending, info, sourceId, targetBufferId, generation);
			break;
		}
		case PendingMiKind::VariableCreate: {
			if (pending.refreshGeneration == process.number("variableRefreshGeneration") && process.number("localVariableCount") < kVariableMaximumRows) {
				MRGdbMiVariable variable;
				variable.name = pending.text;
				variable.objectName = mrGdbMiField(record.raw, "name");
				variable.value = mrGdbMiField(record.raw, "value");
				variable.type = mrGdbMiField(record.raw, "type");
				variable.childCount = mrGdbMiIntField(record.raw, "numchild", 0);
				if (!variable.objectName.empty()) {
					{
						std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
						const VirtualMachine::Value roots = mrvmRuntimeKv().ensureChild(process.threadState(process.read("valueThreadId").s), "localVariableRoots");
						mrvmRuntimeKv().globalStore().write(roots.hashHandle, variable.objectName, mrvmMakeInt(1));
					}
					process.appendVariable(variable, false);
					if (variable.childCount > 0) requestVariableChildren(process, variable.objectName, 1);
				}
			}
			finishVariableCommand(process, pending, info, sourceId, targetBufferId, generation);
			break;
		}
		case PendingMiKind::VariableChildren: {
			const char *countKey = pending.watch ? "watchVariableCount" : "localVariableCount";
			if (pending.refreshGeneration == process.number("variableRefreshGeneration") && process.number(countKey) < kVariableMaximumRows) {
				std::vector<MRGdbMiVariable> children;
				mrGdbMiChildren(record.raw, pending.objectName, pending.depth, children);
				for (MRGdbMiVariable &child : children) {
					if (process.number(countKey) >= kVariableMaximumRows) break;
					const bool requestChildren = child.childCount > 0 && child.depth < kVariableMaximumDepth;
					const std::string objectName = child.objectName;
					process.appendVariable(child, pending.watch);
					if (requestChildren) requestVariableChildren(process, objectName, pending.depth + 1, pending.watch);
				}
			}
			finishVariableCommand(process, pending, info, sourceId, targetBufferId, generation);
			break;
		}
		case PendingMiKind::VariableAssign:
			requestStoppedState(process);
			break;
		case PendingMiKind::Evaluate: {
			MRGdbEvent event;
			event.kind = MRGdbEventKind::DebuggerOutput;
			event.text = "Evaluate: " + pending.text + " = " + mrGdbMiField(record.raw, "value") + "\n";
			postGdbEvent(process, info, sourceId, targetBufferId, generation, std::move(event));
			requestStoppedState(process);
			break;
		}
		case PendingMiKind::WatchCreate: {
			{
				std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
				MRVMRuntimeKv &kv = mrvmRuntimeKv();
				VirtualMachine::Value watches, watch;
				if (kv.findChild(process.threadState(process.read("valueThreadId").s), "watches", watches) && kv.findChild(watches, pending.text, watch)) {
					MRGdbMiVariable variable;
					variable.identity = pending.text;
					variable.name = kv.globalStore().read(watch.hashHandle, "expression").s;
					variable.objectName = mrGdbMiField(record.raw, "name");
					variable.value = mrGdbMiField(record.raw, "value");
					variable.type = mrGdbMiField(record.raw, "type");
					variable.childCount = mrGdbMiIntField(record.raw, "numchild", 0);
					kv.globalStore().write(watch.hashHandle, "objectName", mrvmMakeString(variable.objectName));
					process.appendVariable(variable, true);
					if (variable.childCount > 0) requestVariableChildren(process, variable.objectName, 1, true);
				}
			}
			finishVariableCommand(process, pending, info, sourceId, targetBufferId, generation);
			break;
		}
		case PendingMiKind::Execution:
		case PendingMiKind::None:
			break;
	}
}

bool startGdbProcess(GdbProcess &process, const std::string &programPath, const std::string &sourcePath, std::string &errorMessage) {
	int inputPipe[2] = {-1, -1};
	int outputPipe[2] = {-1, -1};
	int errorPipe[2] = {-1, -1};
	char slaveName[256] = {};

	process.ptyMasterFd = ::posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (process.ptyMasterFd < 0 || ::grantpt(process.ptyMasterFd) != 0 || ::unlockpt(process.ptyMasterFd) != 0 || ::ptsname_r(process.ptyMasterFd, slaveName, sizeof(slaveName)) != 0) {
		errorMessage = std::string("Unable to create inferior PTY: ") + std::strerror(errno);
		closeFd(process.ptyMasterFd);
		return false;
	}
	process.ptySlaveName = slaveName;
	process.ptySlaveFd = ::open(slaveName, O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (process.ptySlaveFd < 0) {
		errorMessage = std::string("Unable to open inferior PTY slave: ") + std::strerror(errno);
		closeFd(process.ptyMasterFd);
		return false;
	}
	process.write("sourcePath", mrvmMakeString(sourcePath));
	if (::pipe(inputPipe) != 0 || ::pipe(outputPipe) != 0 || ::pipe(errorPipe) != 0) {
		errorMessage = std::string("Unable to create GDB MI pipes: ") + std::strerror(errno);
		for (int *pipeFds : {inputPipe, outputPipe, errorPipe}) {
			if (pipeFds[0] >= 0) ::close(pipeFds[0]);
			if (pipeFds[1] >= 0) ::close(pipeFds[1]);
		}
		closeFd(process.ptyMasterFd);
		closeFd(process.ptySlaveFd);
		return false;
	}
	for (int *pipeFds : {inputPipe, outputPipe, errorPipe}) {
		setCloseOnExec(pipeFds[0]);
		setCloseOnExec(pipeFds[1]);
	}
	process.pid = ::fork();
	if (process.pid < 0) {
		errorMessage = std::string("Unable to fork GDB: ") + std::strerror(errno);
		for (int *pipeFds : {inputPipe, outputPipe, errorPipe}) {
			::close(pipeFds[0]);
			::close(pipeFds[1]);
		}
		closeFd(process.ptyMasterFd);
		closeFd(process.ptySlaveFd);
		return false;
	}
	if (process.pid == 0) {
		::setpgid(0, 0);
		::dup2(inputPipe[0], STDIN_FILENO);
		::dup2(outputPipe[1], STDOUT_FILENO);
		::dup2(errorPipe[1], STDERR_FILENO);
		for (int *pipeFds : {inputPipe, outputPipe, errorPipe}) {
			::close(pipeFds[0]);
			::close(pipeFds[1]);
		}
		::execlp("gdb", "gdb", "--nx", "--quiet", "--interpreter=mi3", programPath.c_str(), static_cast<char *>(nullptr));
		::_exit(127);
	}
	::setpgid(process.pid, process.pid);
	::close(inputPipe[0]);
	::close(outputPipe[1]);
	::close(errorPipe[1]);
	process.inputFd = inputPipe[1];
	process.outputFd = outputPipe[0];
	process.errorFd = errorPipe[0];
	process.outputOpen = true;
	process.errorOpen = true;
	setNonBlocking(process.outputFd);
	setNonBlocking(process.errorFd);
	setNonBlocking(process.ptyMasterFd);
	return true;
}

void terminateGdbProcess(GdbProcess &process) noexcept {
	if (!process.childExited && process.pid > 0) {
		const pid_t inferiorGroup = process.ptyMasterFd >= 0 ? ::tcgetpgrp(process.ptyMasterFd) : -1;
		if (inferiorGroup > 0) ::kill(-inferiorGroup, SIGTERM);
		::kill(-process.pid, SIGTERM);
		for (int count = 0; count < 10; ++count) {
			const pid_t waited = ::waitpid(process.pid, &process.waitStatus, WNOHANG);
			if (waited == process.pid) {
				process.childExited = true;
				break;
			}
			::usleep(10000);
		}
		if (!process.childExited) {
			if (inferiorGroup > 0) ::kill(-inferiorGroup, SIGKILL);
			::kill(-process.pid, SIGKILL);
			while (::waitpid(process.pid, &process.waitStatus, 0) < 0 && errno == EINTR) {}
			process.childExited = true;
		}
	}
	closeFd(process.inputFd);
	closeFd(process.outputFd);
	closeFd(process.errorFd);
	closeFd(process.ptyMasterFd);
	closeFd(process.ptySlaveFd);
}

void dispatchControlCommand(GdbProcess &process, const MRGdbCommand &command) {
	const bool execution = command.kind == MRGdbCommandKind::ContinueExecution || command.kind == MRGdbCommandKind::StepInto || command.kind == MRGdbCommandKind::StepOver || command.kind == MRGdbCommandKind::StepOut || command.kind == MRGdbCommandKind::RunToLocation;
	const bool selection = command.kind == MRGdbCommandKind::SelectThread || command.kind == MRGdbCommandKind::SelectVariablesThread || command.kind == MRGdbCommandKind::SelectWatchesThread;
	const bool refreshesValues = selection || command.kind == MRGdbCommandKind::Evaluate || command.kind == MRGdbCommandKind::AssignVariable || command.kind == MRGdbCommandKind::AddWatch || command.kind == MRGdbCommandKind::EraseWatch;
	bool contextual = execution && process.read("inferiorHasRun").i;
	switch (command.kind) {
		case MRGdbCommandKind::SelectThread:
		case MRGdbCommandKind::SelectVariablesThread:
		case MRGdbCommandKind::SelectWatchesThread:
		case MRGdbCommandKind::Evaluate:
		case MRGdbCommandKind::AssignVariable:
		case MRGdbCommandKind::AddWatch:
		case MRGdbCommandKind::EraseWatch: contextual = true; break;
		default: break;
	}
	if (contextual && (command.threadId.empty() || !process.read("stopped").i || command.stopGeneration != process.number("stopGeneration"))) return;
	if (contextual && !selection && command.contextGeneration != process.number("contextGeneration") + (refreshesValues ? 1 : 0)) return;
	if (contextual && !command.threadId.empty()) {
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		VirtualMachine::Value threads, thread;
		if (!mrvmRuntimeKv().findChild(process.session, "threads", threads) || !mrvmRuntimeKv().findChild(threads, command.threadId, thread)) return;
	}
	if (execution && (process.read("breakpointBusy").i || process.number("breakpointRestores") > 0)) {
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		process.write("pendingExecution", mrvmMakeInt(static_cast<int>(command.kind) + 1));
		process.write("pendingExecutionThread", mrvmMakeString(command.threadId));
		process.write("pendingExecutionFile", mrvmMakeString(command.file));
		process.write("pendingExecutionLine", mrvmMakeInt(command.line));
		process.setNumber("pendingExecutionStop", command.stopGeneration);
		process.setNumber("pendingExecutionContext", command.contextGeneration);
		return;
	}
	if (selection) {
		if (command.contextGeneration <= process.number("contextGeneration") || command.threadId.empty() || command.threadId.find_first_not_of("0123456789") != std::string::npos) return;
		invalidateVariableRefresh(process);
		process.setNumber("contextGeneration", command.contextGeneration);
		process.write(command.kind == MRGdbCommandKind::SelectThread ? "threadId" : command.kind == MRGdbCommandKind::SelectVariablesThread ? "variablesThreadId" : "watchesThreadId", mrvmMakeString(command.threadId));
		requestThreadSnapshot(process);
		return;
	}
	if (refreshesValues) process.setNumber("contextGeneration", command.contextGeneration);
	PendingMiCommand resume;
	resume.kind = PendingMiKind::Execution;
	switch (command.kind) {
		case MRGdbCommandKind::SelectThread:
		case MRGdbCommandKind::SelectVariablesThread:
		case MRGdbCommandKind::SelectWatchesThread:
			break;
		case MRGdbCommandKind::ContinueExecution:
			static_cast<void>(sendMi(process, process.read("inferiorHasRun").i ? "-exec-continue" : "-exec-run", resume));
			break;
		case MRGdbCommandKind::PauseExecution:
			process.write("pendingExecution", mrvmMakeInt(0));
			static_cast<void>(sendMi(process, "-exec-interrupt --all"));
			break;
		case MRGdbCommandKind::RunToLocation:
			static_cast<void>(sendMi(process, "-break-insert -t " + (command.threadId.empty() ? std::string() : "-p " + command.threadId + " ") + sourceLocation(command.file, command.line)));
			static_cast<void>(sendMi(process, process.read("inferiorHasRun").i ? "-exec-continue" : "-exec-run", resume));
			break;
		case MRGdbCommandKind::StepInto:
			static_cast<void>(sendMi(process, process.read("inferiorHasRun").i ? "-exec-step --thread " + process.read("threadId").s : "-exec-run --start", resume));
			break;
		case MRGdbCommandKind::StepOver:
			static_cast<void>(sendMi(process, process.read("inferiorHasRun").i ? "-exec-next --thread " + process.read("threadId").s : "-exec-run --start", resume));
			break;
		case MRGdbCommandKind::StepOut:
			static_cast<void>(sendMi(process, "-exec-finish --thread " + process.read("threadId").s, resume));
			break;
		case MRGdbCommandKind::ToggleBreakpoint:
		case MRGdbCommandKind::SetBreakpointAssert: {
			PendingMiCommand pending;
			pending.kind = command.kind == MRGdbCommandKind::SetBreakpointAssert ? PendingMiKind::BreakpointAssertQuery : PendingMiKind::ToggleQuery;
			pending.text = command.text;
			pending.file = command.file;
			pending.line = command.line;
			process.storePending("breakpointToggles", std::to_string(process.number("breakpointTail")), pending);
			process.setNumber("breakpointTail", process.number("breakpointTail") + 1);
			dispatchBreakpointToggle(process);
			break;
		}
		case MRGdbCommandKind::AddBreakpoint: {
			PendingMiCommand pending;
			pending.kind = PendingMiKind::BreakpointMutation;
			pending.text = "restore";
			const bool hasCondition = command.text.find_first_not_of(" \t\r\n") != std::string::npos;
			if (sendMi(process, "-break-insert -f " + (hasCondition ? "--force-condition -c " + mrGdbMiQuote(command.text) + " " : std::string()) + sourceLocation(command.file, command.line), pending) != 0)
				process.setNumber("breakpointRestores", process.number("breakpointRestores") + 1);
			break;
		}
		case MRGdbCommandKind::AddWatch: {
			if (command.text.empty()) break;
			{
				std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
				MRVMRuntimeKv &kv = mrvmRuntimeKv();
				const VirtualMachine::Value watches = kv.ensureChild(process.threadState(command.threadId), "watches");
				const VirtualMachine::Value watch = kv.ensureChild(watches, "watch" + std::to_string(process.number("nextToken")));
				process.setNumber("nextToken", process.number("nextToken") + 1);
				kv.globalStore().write(watch.hashHandle, "expression", mrvmMakeString(command.text));
				kv.globalStore().write(watch.hashHandle, "objectName", mrvmMakeString(std::string()));
			}
			requestStoppedState(process);
			break;
		}
		case MRGdbCommandKind::EraseWatch: {
			std::string objectName;
			{
				std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
				MRVMRuntimeKv &kv = mrvmRuntimeKv();
				const VirtualMachine::Value watches = kv.ensureChild(process.threadState(command.threadId), "watches");
				for (const std::string &watchId : kv.globalStore().keys(watches.hashHandle)) {
					const VirtualMachine::Value watch = kv.globalStore().read(watches.hashHandle, watchId);
					const std::string expression = kv.globalStore().read(watch.hashHandle, "expression").s;
					const std::string name = kv.globalStore().read(watch.hashHandle, "objectName").s;
					if (watchId != command.text && expression != command.text && name != command.text) continue;
					objectName = name;
					kv.eraseChild(watches, watchId);
					break;
				}
			}
			if (!objectName.empty()) static_cast<void>(sendMi(process, "-var-delete " + objectName));
			requestStoppedState(process);
			break;
		}
		case MRGdbCommandKind::Evaluate: {
			invalidateVariableRefresh(process);
			PendingMiCommand pending;
			pending.kind = PendingMiKind::Evaluate;
			pending.refreshGeneration = process.number("variableRefreshGeneration");
			pending.text = command.text;
			static_cast<void>(sendMi(process, "-data-evaluate-expression --thread " + command.threadId + " --frame 0 " + mrGdbMiQuote(command.text), std::move(pending)));
			break;
		}
		case MRGdbCommandKind::AssignVariable: {
			bool found = false;
			{
				std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
				MRVMRuntimeKv &kv = mrvmRuntimeKv();
				VirtualMachine::Value entries, item;
				if (!command.objectName.empty() && kv.findChild(kv.ensureChild(kv.ensureChild(process.threadState(command.threadId), "frames"), "0"), "localVariables", entries))
					for (const std::string &index : kv.globalStore().keys(entries.hashHandle)) {
						if (!kv.findChild(entries, index, item)) continue;
						if (kv.globalStore().read(item.hashHandle, "objectName").s != command.objectName) continue;
						found = true;
						break;
					}
			}
			if (!found) { requestStoppedState(process); return; }
			invalidateVariableRefresh(process);
			PendingMiCommand pending;
			pending.kind = PendingMiKind::VariableAssign;
			pending.refreshGeneration = process.number("variableRefreshGeneration");
			pending.objectName = command.objectName;
			pending.text = command.text;
			if (!command.objectName.empty()) static_cast<void>(sendMi(process, "-var-assign " + command.objectName + " " + mrGdbMiQuote(command.text), std::move(pending)));
			break;
		}
		case MRGdbCommandKind::TerminalInput:
			static_cast<void>(writeAll(process.ptyMasterFd, command.text));
			break;
		case MRGdbCommandKind::ResizeTerminal: {
			struct winsize size;
			std::memset(&size, 0, sizeof(size));
			size.ws_col = static_cast<unsigned short>(command.columns > 0 ? command.columns : 1);
			size.ws_row = static_cast<unsigned short>(command.rows > 0 ? command.rows : 1);
			static_cast<void>(::ioctl(process.ptyMasterFd, TIOCSWINSZ, &size));
			const pid_t inferiorGroup = ::tcgetpgrp(process.ptyMasterFd);
			if (inferiorGroup > 0) ::kill(-inferiorGroup, SIGWINCH);
			break;
		}
		case MRGdbCommandKind::Quit:
			process.write("pendingExecution", mrvmMakeInt(0));
			process.quitRequested = true;
			static_cast<void>(sendMi(process, "-gdb-exit"));
			break;
	}
	if (execution) {
		process.write("stopped", mrvmMakeInt(false));
		invalidateVariableRefresh(process);
	}
}

void drainControlChannel(const std::shared_ptr<MRGdbControlChannel> &channel, GdbProcess &process) {
	std::array<char, 64> wakeBytes{};
	while (::read(channel->readFd, wakeBytes.data(), wakeBytes.size()) > 0) {}
	std::deque<MRGdbCommand> commands;
	{
		std::lock_guard<std::mutex> lock(channel->mutex);
		commands.swap(channel->commands);
	}
	for (const MRGdbCommand &command : commands) dispatchControlCommand(process, command);
}

void readMiOutput(GdbProcess &process, const mr::coprocessor::TaskInfo &info, std::size_t sourceId, int targetBufferId, std::uint64_t generation) {
	std::array<char, 4096> buffer{};
	for (;;) {
		const ssize_t count = ::read(process.outputFd, buffer.data(), buffer.size());
		if (count > 0) {
			std::vector<MRGdbMiRecord> records;
			process.miStream.append(buffer.data(), static_cast<std::size_t>(count), records);
			for (const MRGdbMiRecord &record : records) handleMiRecord(process, record, info, sourceId, targetBufferId, generation);
			continue;
		}
		if (count == 0) process.outputOpen = false;
		if (count <= 0) break;
	}
}

void readTextOutput(const GdbProcess &process, int fd, MRGdbEventKind kind, const mr::coprocessor::TaskInfo &info, std::size_t sourceId, int targetBufferId, std::uint64_t generation, bool &open) {
	std::array<char, 4096> buffer{};
	for (;;) {
		const ssize_t count = ::read(fd, buffer.data(), buffer.size());
		if (count > 0) {
			MRGdbEvent event;
			event.kind = kind;
			event.text.assign(buffer.data(), static_cast<std::size_t>(count));
			postGdbEvent(process, info, sourceId, targetBufferId, generation, std::move(event));
			continue;
		}
		if (count == 0) open = false;
		if (count <= 0) break;
	}
}

mr::coprocessor::Result runGdbSessionTask(const mr::coprocessor::TaskInfo &info, const std::shared_ptr<MRGdbControlChannel> &channel, std::size_t sourceId, int targetBufferId, std::uint64_t generation, const std::string &programPath, const std::string &sourcePath, const VirtualMachine::Value &state) {
	mr::coprocessor::Result result;
	GdbProcess process;
	process.session = state;
	{
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		process.state = mrvmRuntimeKv().ensureChild(state, "worker");
	}
	process.setNumber("nextToken", 1);
	std::string errorMessage;
	bool ptyOpen = true;
	result.task = info;
	if (!startGdbProcess(process, programPath, sourcePath, errorMessage)) {
		result.status = mr::coprocessor::TaskStatus::Failed;
		result.error = errorMessage;
		MRGdbEvent event;
		event.kind = MRGdbEventKind::Finished;
		event.text = errorMessage;
		result.payload = std::make_shared<mr::coprocessor::GdbFinishedPayload>(sourceId, targetBufferId, generation, std::move(event));
		return result;
	}
	static_cast<void>(sendMi(process, "-gdb-set mi-async on"));
	static_cast<void>(sendMi(process, "-gdb-set confirm off"));
	static_cast<void>(sendMi(process, "-gdb-set debuginfod enabled off"));
	static_cast<void>(sendMi(process, "-inferior-tty-set " + mrGdbMiQuote(process.ptySlaveName)));
	const std::filesystem::path sourceDirectory = std::filesystem::path(sourcePath).parent_path();
	if (!sourceDirectory.empty()) static_cast<void>(sendMi(process, "-environment-cd " + mrGdbMiQuote(sourceDirectory.string())));
	static_cast<void>(sendMi(process, "-enable-pretty-printing"));
	MRGdbEvent started;
	started.kind = MRGdbEventKind::Started;
	started.text = "GDB/MI ready; inferior PTY " + process.ptySlaveName + "\n";
	postGdbEvent(process, info, sourceId, targetBufferId, generation, std::move(started));
	while (!process.childExited) {
		std::array<struct pollfd, 4> pollFds{};
		pollFds[0] = {channel->readFd, POLLIN, 0};
		pollFds[1] = {process.outputFd, static_cast<short>(POLLIN | POLLHUP), 0};
		pollFds[2] = {process.errorFd, static_cast<short>(POLLIN | POLLHUP), 0};
		pollFds[3] = {process.ptyMasterFd, static_cast<short>(POLLIN | POLLHUP), 0};
		const int pollResult = ::poll(pollFds.data(), pollFds.size(), 50);
		if (pollResult < 0 && errno != EINTR) {
			errorMessage = std::string("GDB channel poll failed: ") + std::strerror(errno);
			break;
		}
		if ((pollFds[0].revents & POLLIN) != 0) drainControlChannel(channel, process);
		if (process.outputOpen && (pollFds[1].revents & (POLLIN | POLLHUP)) != 0) readMiOutput(process, info, sourceId, targetBufferId, generation);
		if (process.errorOpen && (pollFds[2].revents & (POLLIN | POLLHUP)) != 0) readTextOutput(process, process.errorFd, MRGdbEventKind::DebuggerOutput, info, sourceId, targetBufferId, generation, process.errorOpen);
		if (ptyOpen && (pollFds[3].revents & POLLIN) != 0) readTextOutput(process, process.ptyMasterFd, MRGdbEventKind::InferiorOutput, info, sourceId, targetBufferId, generation, ptyOpen);
		if (process.read("pendingExecution").i && !process.read("breakpointBusy").i && process.number("breakpointRestores") == 0) {
			MRGdbCommand command;
			{
				std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
				command.kind = static_cast<MRGdbCommandKind>(process.read("pendingExecution").i - 1);
				command.threadId = process.read("pendingExecutionThread").s;
				command.file = process.read("pendingExecutionFile").s;
				command.line = process.read("pendingExecutionLine").i;
				command.stopGeneration = process.number("pendingExecutionStop");
				command.contextGeneration = process.number("pendingExecutionContext");
				process.write("pendingExecution", mrvmMakeInt(0));
			}
			dispatchControlCommand(process, command);
		}
		if (info.cancelRequested()) {
			errorMessage = "GDB session cancelled.";
			break;
		}
		const pid_t waited = ::waitpid(process.pid, &process.waitStatus, WNOHANG);
		if (waited == process.pid) process.childExited = true;
		else if (waited < 0 && errno != EINTR) {
			errorMessage = std::string("Unable to wait for GDB: ") + std::strerror(errno);
			break;
		}
	}
	if (errorMessage.empty() && process.childExited && WIFEXITED(process.waitStatus) && WEXITSTATUS(process.waitStatus) != 0 && !process.quitRequested) {
		if (WEXITSTATUS(process.waitStatus) == 127) errorMessage = "Unable to execute gdb from PATH.";
		else errorMessage = "GDB exited with status " + std::to_string(WEXITSTATUS(process.waitStatus)) + ".";
	}
	if (errorMessage.empty() && process.childExited && WIFSIGNALED(process.waitStatus) && !process.quitRequested)
		errorMessage = "GDB terminated by signal " + std::to_string(WTERMSIG(process.waitStatus)) + ".";
	terminateGdbProcess(process);
	MRGdbEvent finished;
	finished.kind = MRGdbEventKind::Finished;
	finished.text = errorMessage;
	result.payload = std::make_shared<mr::coprocessor::GdbFinishedPayload>(sourceId, targetBufferId, generation, std::move(finished));
	if (info.cancelRequested()) result.status = mr::coprocessor::TaskStatus::Cancelled;
	else if (!errorMessage.empty()) {
		result.status = mr::coprocessor::TaskStatus::Failed;
		result.error = errorMessage;
	} else result.status = mr::coprocessor::TaskStatus::Completed;
	return result;
}

} // namespace

MRGdbCommand::MRGdbCommand() noexcept : kind(MRGdbCommandKind::ContinueExecution), text(), file(), objectName(), line(0), columns(0), rows(0) {}
MRGdbCommand::MRGdbCommand(MRGdbCommandKind aKind) noexcept : kind(aKind), text(), file(), objectName(), line(0), columns(0), rows(0) {}
MRGdbEvent::MRGdbEvent() noexcept : kind(MRGdbEventKind::DebuggerOutput), text(), file(), line(0), variables(), breakpointLines() {}

namespace mr::coprocessor {
GdbEventPayload::GdbEventPayload() noexcept : sourceId(0), targetBufferId(0), generation(0), event() {}
GdbEventPayload::GdbEventPayload(std::size_t aSourceId, int aTargetBufferId, std::uint64_t aGeneration, MRGdbEvent aEvent)
    : sourceId(aSourceId), targetBufferId(aTargetBufferId), generation(aGeneration), event(std::move(aEvent)) {}
GdbStreamEventPayload::GdbStreamEventPayload(std::size_t aSourceId, int aTargetBufferId, std::uint64_t aGeneration, MRGdbEvent aEvent)
    : GdbEventPayload(aSourceId, aTargetBufferId, aGeneration, std::move(aEvent)), StreamingPayload() {}
GdbFinishedPayload::GdbFinishedPayload(std::size_t aSourceId, int aTargetBufferId, std::uint64_t aGeneration, MRGdbEvent aEvent)
    : GdbEventPayload(aSourceId, aTargetBufferId, aGeneration, std::move(aEvent)) {}
} // namespace mr::coprocessor

MRGdbSession::MRGdbSession() noexcept : controlChannel(), sourceId(0), taskId(0), generation(0) {}
MRGdbSession::~MRGdbSession() { stop(); }

bool MRGdbSession::start(const std::string &programPath, const std::string &sourcePath, int targetBufferId, std::string &errorMessage) {
	stop();
	std::shared_ptr<MRGdbControlChannel> channel = std::make_shared<MRGdbControlChannel>();
	int wakePipe[2] = {-1, -1};
	if (::pipe(wakePipe) != 0) {
		errorMessage = std::string("Unable to create GDB control pipe: ") + std::strerror(errno);
		return false;
	}
	channel->readFd = wakePipe[0];
	channel->writeFd = wakePipe[1];
	setCloseOnExec(channel->readFd);
	setCloseOnExec(channel->writeFd);
	setNonBlocking(channel->readFd);
	setNonBlocking(channel->writeFd);
	const std::size_t newSourceId = mr::coprocessor::globalCoprocessor().registerExternalSource(mr::coprocessor::ExternalSourceKind::Pipe, "GDB/MI");
	const std::uint64_t newGeneration = static_cast<std::uint64_t>(newSourceId);
	VirtualMachine::Value state;
	{
		std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
		releaseRuntimeState(targetBufferId);
		MRVMRuntimeKv &kv = mrvmRuntimeKv();
		const VirtualMachine::Value app = kv.ensureRoot("APPLICATIONUI");
		const VirtualMachine::Value debugger = kv.ensureChild(app, "debugger");
		const VirtualMachine::Value sessions = kv.ensureChild(debugger, "sessions");
		const VirtualMachine::Value session = kv.replaceChild(sessions, std::to_string(targetBufferId));
		state = session;
		kv.ensureChild(session, "worker");
		kv.globalStore().write(session.hashHandle, "generation", mrvmMakeString(std::to_string(newGeneration)));
	}

	const std::uint64_t newTaskId = mr::coprocessor::globalCoprocessor().submitExternal(newSourceId, std::string("gdb: ") + programPath,
	    [channel, newSourceId, targetBufferId, newGeneration, programPath, sourcePath, state](const mr::coprocessor::TaskInfo &info) {
		    return runGdbSessionTask(info, channel, newSourceId, targetBufferId, newGeneration, programPath, sourcePath, state);
	    });
	if (newTaskId == 0) {
		mr::coprocessor::globalCoprocessor().unregisterExternalSource(newSourceId);
		{
			std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
			mrvmRuntimeKv().eraseChild(state, "worker");
			releaseRuntimeState(targetBufferId);
		}
		errorMessage = "Unable to start GDB external-source worker.";
		return false;
	}
	controlChannel = std::move(channel);
	sourceId = newSourceId;
	taskId = newTaskId;
	generation = newGeneration;
	errorMessage.clear();
	return true;
}

bool MRGdbSession::send(MRGdbCommand command) {
	if (sourceId == 0 || controlChannel == nullptr) return false;
	{
		std::lock_guard<std::mutex> lock(controlChannel->mutex);
		controlChannel->commands.push_back(std::move(command));
	}
	const char wake = 1;
	const ssize_t written = ::write(controlChannel->writeFd, &wake, 1);
	return written == 1 || (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
}

void MRGdbSession::stop() noexcept {
	if (sourceId == 0) return;
	static_cast<void>(send(MRGdbCommand(MRGdbCommandKind::Quit)));
	mr::coprocessor::globalCoprocessor().cancelExternalSource(sourceId);
	mr::coprocessor::globalCoprocessor().unregisterExternalSource(sourceId);
	sourceId = 0;
	taskId = 0;
	controlChannel.reset();
}

void MRGdbSession::releaseRuntimeState(int targetBufferId) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &kv = mrvmRuntimeKv();
	MRVMHashStore &store = kv.globalStore();
	VirtualMachine::Value app, debugger, sessions, session, worker;
	const std::string key = std::to_string(targetBufferId);
	if (!kv.findRoot("APPLICATIONUI", app) || !kv.findChild(app, "debugger", debugger) ||
	    !kv.findChild(debugger, "sessions", sessions) || !kv.findChild(sessions, key, session)) return;
	if (!kv.findChild(session, "worker", worker)) {
		kv.eraseChild(sessions, key);
		return;
	}
	// Transfer the owning reference without erasing the live worker's tree.
	const VirtualMachine::Value retiring = kv.ensureChild(debugger, "retiring");
	const std::string generation = store.read(session.hashHandle, "generation").s;
	store.write(session.hashHandle, "retiring", mrvmMakeInt(1));
	store.write(retiring.hashHandle, generation, session);
	store.erase(sessions.hashHandle, key);
}

void MRGdbSession::markFinished(std::uint64_t eventGeneration) noexcept {
	if (eventGeneration != generation) return;
	sourceId = 0;
	taskId = 0;
	controlChannel.reset();
}

bool MRGdbSession::active() const noexcept { return sourceId != 0; }
std::uint64_t MRGdbSession::currentGeneration() const noexcept { return generation; }
