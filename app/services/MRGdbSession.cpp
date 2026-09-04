#include "MRGdbSession.hpp"

#include "MRGdbMi.hpp"

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
	ToggleQuery,
	BreakpointMutation,
	BreakpointRefresh,
	VariableNames,
	VariableCreate,
	VariableChildren,
	VariableAssign,
	Evaluate,
	WatchCreate,
	WatchDelete,
	WatchUpdate
};

struct PendingMiCommand {
	PendingMiCommand() noexcept : kind(PendingMiKind::None), text(), file(), objectName(), line(0), depth(0), rowLimit(0), refreshGeneration(0) {}
	PendingMiKind kind;
	std::string text;
	std::string file;
	std::string objectName;
	int line;
	int depth;
	std::size_t rowLimit;
	std::uint64_t refreshGeneration;
};

struct GdbProcess {
	GdbProcess() noexcept
	    : pid(-1), inputFd(-1), outputFd(-1), errorFd(-1), ptyMasterFd(-1), ptySlaveFd(-1), sourcePath(), childExited(false), waitStatus(0), outputOpen(false), errorOpen(false), quitRequested(false), inferiorHasRun(false), nextToken(1), pending(), watches(), localVariableRoots(), localVariables(), variableExpansionQueue(), variableRefreshGeneration(0), variableOutstanding(0), variableChildrenRunning(false), miStream() {}

	pid_t pid;
	int inputFd;
	int outputFd;
	int errorFd;
	int ptyMasterFd;
	int ptySlaveFd;
	std::string ptySlaveName;
	std::string sourcePath;
	bool childExited;
	int waitStatus;
	bool outputOpen;
	bool errorOpen;
	bool quitRequested;
	bool inferiorHasRun;
	unsigned nextToken;
	std::map<unsigned, PendingMiCommand> pending;
	std::map<std::string, std::pair<std::string, std::string>> watches;
	std::vector<std::string> localVariableRoots;
	std::vector<MRGdbMiVariable> localVariables;
	std::deque<PendingMiCommand> variableExpansionQueue;
	std::uint64_t variableRefreshGeneration;
	std::size_t variableOutstanding;
	bool variableChildrenRunning;
	MRGdbMiStream miStream;
};

const int kVariableMaximumDepth = 8;
const std::size_t kVariableMaximumRows = 1024;

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

void postGdbEvent(const mr::coprocessor::TaskInfo &info, std::size_t sourceId, int targetBufferId, std::uint64_t generation, MRGdbEvent event) {
	mr::coprocessor::Result result;
	result.task = info;
	result.status = mr::coprocessor::TaskStatus::Completed;
	result.payload = std::make_shared<mr::coprocessor::GdbStreamEventPayload>(sourceId, targetBufferId, generation, std::move(event));
	mr::coprocessor::globalCoprocessor().post(std::move(result));
}

unsigned sendMi(GdbProcess &process, const std::string &command, PendingMiCommand pending = PendingMiCommand()) {
	const unsigned token = process.nextToken++;
	if (!writeAll(process.inputFd, std::to_string(token) + command + "\n")) return 0;
	if (pending.kind != PendingMiKind::None) process.pending[token] = std::move(pending);
	return token;
}

std::string sourceLocation(const std::string &file, int line) {
	return mrGdbMiQuote(file + ":" + std::to_string(line));
}

void requestBreakpointRefresh(GdbProcess &process) {
	PendingMiCommand pending;
	pending.kind = PendingMiKind::BreakpointRefresh;
	static_cast<void>(sendMi(process, "-break-list", std::move(pending)));
}

void requestStoppedState(GdbProcess &process) {
	for (const std::string &objectName : process.localVariableRoots)
		static_cast<void>(sendMi(process, "-var-delete " + objectName));
	process.localVariableRoots.clear();
	process.localVariables.clear();
	process.variableExpansionQueue.clear();
	++process.variableRefreshGeneration;
	process.variableOutstanding = 0;
	process.variableChildrenRunning = false;
	PendingMiCommand variables;
	variables.kind = PendingMiKind::VariableNames;
	variables.refreshGeneration = process.variableRefreshGeneration;
	if (sendMi(process, "-stack-list-variables --no-values", std::move(variables)) != 0) ++process.variableOutstanding;
	PendingMiCommand watches;
	watches.kind = PendingMiKind::WatchUpdate;
	static_cast<void>(sendMi(process, "-var-update --all-values *", std::move(watches)));
	requestBreakpointRefresh(process);
}

void invalidateVariableRefresh(GdbProcess &process) noexcept {
	++process.variableRefreshGeneration;
	process.variableOutstanding = 0;
	process.variableExpansionQueue.clear();
	process.variableChildrenRunning = false;
	process.localVariables.clear();
}

void postWatchProjection(GdbProcess &process, const mr::coprocessor::TaskInfo &info, std::size_t sourceId, int targetBufferId, std::uint64_t generation) {
	MRGdbEvent event;
	event.kind = MRGdbEventKind::Watches;
	if (process.watches.empty()) event.text = "(no watches)\n";
	else for (const auto &watch : process.watches)
		event.text += watch.first + ": " + watch.second.first + " = " + watch.second.second + "\n";
	postGdbEvent(info, sourceId, targetBufferId, generation, std::move(event));
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
	for (const MRGdbMiBreakpoint &breakpoint : breakpoints)
		if (breakpoint.line > 0 && breakpointTargetsSource(breakpoint, process.sourcePath)) event.breakpointLines.push_back(breakpoint.line);
	postGdbEvent(info, sourceId, targetBufferId, generation, std::move(event));
}

void handleToggleQuery(GdbProcess &process, const PendingMiCommand &pending, const std::string &raw) {
	std::vector<MRGdbMiBreakpoint> breakpoints;
	std::string matchingNumbers;
	mrGdbMiBreakpoints(raw, breakpoints);
	for (const MRGdbMiBreakpoint &breakpoint : breakpoints) {
		if (breakpoint.line != pending.line) continue;
		if (!breakpointTargetsSource(breakpoint, pending.file)) continue;
		if (!matchingNumbers.empty()) matchingNumbers += ' ';
		matchingNumbers += breakpoint.number;
	}
	PendingMiCommand mutation;
	mutation.kind = PendingMiKind::BreakpointMutation;
	if (matchingNumbers.empty())
		static_cast<void>(sendMi(process, "-break-insert " + sourceLocation(pending.file, pending.line), std::move(mutation)));
	else
		static_cast<void>(sendMi(process, "-break-delete " + matchingNumbers, std::move(mutation)));
}

bool variableRefreshCommand(PendingMiKind kind) noexcept {
	return kind == PendingMiKind::VariableNames || kind == PendingMiKind::VariableCreate || kind == PendingMiKind::VariableChildren;
}

void appendVariableTree(const std::string &parentObjectName, const std::vector<MRGdbMiVariable> &source, std::vector<MRGdbMiVariable> &target, std::set<std::string> &visited) {
	for (const MRGdbMiVariable &variable : source) {
		if (variable.parentObjectName != parentObjectName || !visited.insert(variable.objectName).second) continue;
		target.push_back(variable);
		appendVariableTree(variable.objectName, source, target, visited);
	}
}

void postVariableProjectionIfComplete(GdbProcess &process, const mr::coprocessor::TaskInfo &info, std::size_t sourceId, int targetBufferId, std::uint64_t generation) {
	if (process.variableOutstanding != 0 || process.variableChildrenRunning || !process.variableExpansionQueue.empty()) return;
	MRGdbEvent event;
	std::set<std::string> visited;
	event.kind = MRGdbEventKind::Variables;
	event.variables.reserve(process.localVariables.size());
	appendVariableTree(std::string(), process.localVariables, event.variables, visited);
	postGdbEvent(info, sourceId, targetBufferId, generation, std::move(event));
}

void dispatchNextVariableChildren(GdbProcess &process) {
	if (process.variableChildrenRunning) return;
	while (!process.variableExpansionQueue.empty()) {
		PendingMiCommand children = std::move(process.variableExpansionQueue.front());

		process.variableExpansionQueue.pop_front();
		if (children.refreshGeneration != process.variableRefreshGeneration) continue;
		if (process.localVariables.size() >= kVariableMaximumRows) {
			process.variableExpansionQueue.clear();
			return;
		}
		children.rowLimit = kVariableMaximumRows - process.localVariables.size();
		if (sendMi(process, "-var-list-children --all-values " + children.objectName + " 0 " + std::to_string(children.rowLimit), std::move(children)) != 0) {
			++process.variableOutstanding;
			process.variableChildrenRunning = true;
			return;
		}
	}
}

void finishVariableCommand(GdbProcess &process, const PendingMiCommand &pending, const mr::coprocessor::TaskInfo &info, std::size_t sourceId, int targetBufferId, std::uint64_t generation) {
	if (!variableRefreshCommand(pending.kind) || pending.refreshGeneration != process.variableRefreshGeneration) return;
	if (pending.kind == PendingMiKind::VariableChildren) process.variableChildrenRunning = false;
	if (process.variableOutstanding > 0) --process.variableOutstanding;
	dispatchNextVariableChildren(process);
	postVariableProjectionIfComplete(process, info, sourceId, targetBufferId, generation);
}

void requestVariableChildren(GdbProcess &process, const std::string &objectName, int depth) {
	if (objectName.empty() || depth > kVariableMaximumDepth || process.localVariables.size() >= kVariableMaximumRows || process.variableExpansionQueue.size() >= kVariableMaximumRows) return;
	PendingMiCommand children;
	children.kind = PendingMiKind::VariableChildren;
	children.objectName = objectName;
	children.depth = depth;
	children.refreshGeneration = process.variableRefreshGeneration;
	process.variableExpansionQueue.push_back(std::move(children));
}

void handleMiRecord(GdbProcess &process, const MRGdbMiRecord &record, const mr::coprocessor::TaskInfo &info, std::size_t sourceId, int targetBufferId, std::uint64_t generation) {
	if (record.kind == MRGdbMiRecordKind::Console || record.kind == MRGdbMiRecordKind::Log) {
		if (!record.text.empty()) {
			MRGdbEvent event;
			event.kind = MRGdbEventKind::DebuggerOutput;
			event.text = record.text;
			postGdbEvent(info, sourceId, targetBufferId, generation, std::move(event));
		}
		return;
	}
	if (record.kind == MRGdbMiRecordKind::Target) return;
	if (record.kind == MRGdbMiRecordKind::Exec && record.resultClass == "running") {
		process.inferiorHasRun = true;
		invalidateVariableRefresh(process);
		MRGdbEvent event;
		event.kind = MRGdbEventKind::Running;
		postGdbEvent(info, sourceId, targetBufferId, generation, std::move(event));
		return;
	}
	if (record.kind == MRGdbMiRecordKind::Exec && record.resultClass == "stopped") {
		MRGdbEvent event;
		event.kind = MRGdbEventKind::Stopped;
		event.text = mrGdbMiField(record.raw, "reason");
		event.file = mrGdbMiField(record.raw, "fullname");
		if (event.file.empty()) event.file = mrGdbMiField(record.raw, "file");
		event.line = mrGdbMiIntField(record.raw, "line", 0);
		const bool inferiorExited = event.text.rfind("exited", 0) == 0;
		if (inferiorExited) process.inferiorHasRun = false;
		postGdbEvent(info, sourceId, targetBufferId, generation, std::move(event));
		if (!inferiorExited) requestStoppedState(process);
		return;
	}
	if (record.kind != MRGdbMiRecordKind::Result) return;
	PendingMiCommand pending;
	const std::map<unsigned, PendingMiCommand>::iterator found = process.pending.find(record.token);
	if (found != process.pending.end()) {
		pending = found->second;
		process.pending.erase(found);
	}
	if (record.resultClass == "running") {
		process.inferiorHasRun = true;
		invalidateVariableRefresh(process);
		MRGdbEvent event;
		event.kind = MRGdbEventKind::Running;
		postGdbEvent(info, sourceId, targetBufferId, generation, std::move(event));
	}
	if (record.resultClass == "error") {
		MRGdbEvent event;
		event.kind = MRGdbEventKind::DebuggerOutput;
		if (pending.kind == PendingMiKind::WatchCreate)
			event.text = "Watch '" + pending.text + "' unavailable: " + mrGdbMiField(record.raw, "msg") + ". Stop after its declaration and rebuild if the source changed.\n";
		else event.text = "GDB: " + mrGdbMiField(record.raw, "msg") + "\n";
		postGdbEvent(info, sourceId, targetBufferId, generation, std::move(event));
		finishVariableCommand(process, pending, info, sourceId, targetBufferId, generation);
		return;
	}
	switch (pending.kind) {
		case PendingMiKind::ToggleQuery:
			handleToggleQuery(process, pending, record.raw);
			break;
		case PendingMiKind::BreakpointMutation:
			requestBreakpointRefresh(process);
			break;
		case PendingMiKind::BreakpointRefresh:
			postBreakpointProjection(process, info, sourceId, targetBufferId, generation, record.raw);
			break;
		case PendingMiKind::VariableNames: {
			if (pending.refreshGeneration == process.variableRefreshGeneration) {
				std::vector<MRGdbMiVariable> variables;
				mrGdbMiVariables(record.raw, variables);
				for (const MRGdbMiVariable &variable : variables) {
					if (process.localVariables.size() + process.variableOutstanding >= kVariableMaximumRows) break;
					PendingMiCommand create;
					create.kind = PendingMiKind::VariableCreate;
					create.text = variable.name;
					create.refreshGeneration = process.variableRefreshGeneration;
					if (sendMi(process, "-var-create - * " + mrGdbMiQuote(variable.name), std::move(create)) != 0) ++process.variableOutstanding;
				}
			}
			finishVariableCommand(process, pending, info, sourceId, targetBufferId, generation);
			break;
		}
		case PendingMiKind::VariableCreate: {
			if (pending.refreshGeneration == process.variableRefreshGeneration && process.localVariables.size() < kVariableMaximumRows) {
				MRGdbMiVariable variable;
				variable.name = pending.text;
				variable.objectName = mrGdbMiField(record.raw, "name");
				variable.value = mrGdbMiField(record.raw, "value");
				variable.type = mrGdbMiField(record.raw, "type");
				variable.childCount = mrGdbMiIntField(record.raw, "numchild", 0);
				if (!variable.objectName.empty()) {
					process.localVariableRoots.push_back(variable.objectName);
					process.localVariables.push_back(variable);
					if (variable.childCount > 0) requestVariableChildren(process, variable.objectName, 1);
				}
			}
			finishVariableCommand(process, pending, info, sourceId, targetBufferId, generation);
			break;
		}
		case PendingMiKind::VariableChildren: {
			if (pending.refreshGeneration == process.variableRefreshGeneration && process.localVariables.size() < kVariableMaximumRows) {
				std::vector<MRGdbMiVariable> children;
				mrGdbMiChildren(record.raw, pending.objectName, pending.depth, children);
				for (MRGdbMiVariable &child : children) {
					if (process.localVariables.size() >= kVariableMaximumRows) break;
					const bool requestChildren = child.childCount > 0 && child.depth < kVariableMaximumDepth;
					const std::string objectName = child.objectName;
					process.localVariables.push_back(std::move(child));
					if (requestChildren) requestVariableChildren(process, objectName, pending.depth + 1);
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
			postGdbEvent(info, sourceId, targetBufferId, generation, std::move(event));
			requestStoppedState(process);
			break;
		}
		case PendingMiKind::WatchCreate: {
			const std::string objectName = mrGdbMiField(record.raw, "name");
			if (!objectName.empty()) process.watches[objectName] = std::make_pair(pending.text, mrGdbMiField(record.raw, "value"));
			postWatchProjection(process, info, sourceId, targetBufferId, generation);
			break;
		}
		case PendingMiKind::WatchDelete: {
			process.watches.erase(pending.text);
			postWatchProjection(process, info, sourceId, targetBufferId, generation);
			break;
		}
		case PendingMiKind::WatchUpdate: {
			std::vector<MRGdbMiVariable> changes;
			mrGdbMiChanges(record.raw, changes);
			for (const MRGdbMiVariable &change : changes) {
				const auto watch = process.watches.find(change.name);
				if (watch == process.watches.end()) continue;
				if (change.type == "false") watch->second.second = "<out of scope>";
				else if (!change.value.empty()) watch->second.second = change.value;
			}
			postWatchProjection(process, info, sourceId, targetBufferId, generation);
			break;
		}
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
	process.sourcePath = sourcePath;
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
	switch (command.kind) {
		case MRGdbCommandKind::ContinueExecution:
			static_cast<void>(sendMi(process, process.inferiorHasRun ? "-exec-continue" : "-exec-run"));
			break;
		case MRGdbCommandKind::PauseExecution:
			static_cast<void>(sendMi(process, "-exec-interrupt --all"));
			break;
		case MRGdbCommandKind::RunToLocation:
			static_cast<void>(sendMi(process, "-break-insert -t " + sourceLocation(command.file, command.line)));
			static_cast<void>(sendMi(process, process.inferiorHasRun ? "-exec-continue" : "-exec-run"));
			break;
		case MRGdbCommandKind::StepInto:
			static_cast<void>(sendMi(process, process.inferiorHasRun ? "-exec-step" : "-exec-run --start"));
			break;
		case MRGdbCommandKind::StepOver:
			static_cast<void>(sendMi(process, process.inferiorHasRun ? "-exec-next" : "-exec-run --start"));
			break;
		case MRGdbCommandKind::StepOut:
			static_cast<void>(sendMi(process, "-exec-finish"));
			break;
		case MRGdbCommandKind::ToggleBreakpoint: {
			PendingMiCommand pending;
			pending.kind = PendingMiKind::ToggleQuery;
			pending.file = command.file;
			pending.line = command.line;
			static_cast<void>(sendMi(process, "-break-list", std::move(pending)));
			break;
		}
		case MRGdbCommandKind::AddBreakpoint:
			static_cast<void>(sendMi(process, "-break-insert " + sourceLocation(command.file, command.line)));
			break;
		case MRGdbCommandKind::AddWatch: {
			PendingMiCommand pending;
			pending.kind = PendingMiKind::WatchCreate;
			pending.text = command.text;
			static_cast<void>(sendMi(process, "-var-create - * " + mrGdbMiQuote(command.text), std::move(pending)));
			break;
		}
		case MRGdbCommandKind::EraseWatch: {
			PendingMiCommand pending;
			pending.kind = PendingMiKind::WatchDelete;
			std::string objectName = command.text;
			if (process.watches.find(objectName) == process.watches.end())
				for (const auto &watch : process.watches)
					if (watch.second.first == command.text) { objectName = watch.first; break; }
			pending.text = objectName;
			static_cast<void>(sendMi(process, "-var-delete " + objectName, std::move(pending)));
			break;
		}
		case MRGdbCommandKind::Evaluate: {
			PendingMiCommand pending;
			pending.kind = PendingMiKind::Evaluate;
			pending.text = command.text;
			static_cast<void>(sendMi(process, "-data-evaluate-expression " + mrGdbMiQuote(command.text), std::move(pending)));
			break;
		}
		case MRGdbCommandKind::AssignVariable: {
			PendingMiCommand pending;
			pending.kind = PendingMiKind::VariableAssign;
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
			process.quitRequested = true;
			static_cast<void>(sendMi(process, "-gdb-exit"));
			break;
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

void readTextOutput(int fd, MRGdbEventKind kind, const mr::coprocessor::TaskInfo &info, std::size_t sourceId, int targetBufferId, std::uint64_t generation, bool &open) {
	std::array<char, 4096> buffer{};
	for (;;) {
		const ssize_t count = ::read(fd, buffer.data(), buffer.size());
		if (count > 0) {
			MRGdbEvent event;
			event.kind = kind;
			event.text.assign(buffer.data(), static_cast<std::size_t>(count));
			postGdbEvent(info, sourceId, targetBufferId, generation, std::move(event));
			continue;
		}
		if (count == 0) open = false;
		if (count <= 0) break;
	}
}

mr::coprocessor::Result runGdbSessionTask(const mr::coprocessor::TaskInfo &info, const std::shared_ptr<MRGdbControlChannel> &channel, std::size_t sourceId, int targetBufferId, std::uint64_t generation, const std::string &programPath, const std::string &sourcePath) {
	mr::coprocessor::Result result;
	GdbProcess process;
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
	postGdbEvent(info, sourceId, targetBufferId, generation, std::move(started));
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
		if (process.errorOpen && (pollFds[2].revents & (POLLIN | POLLHUP)) != 0) readTextOutput(process.errorFd, MRGdbEventKind::DebuggerOutput, info, sourceId, targetBufferId, generation, process.errorOpen);
		if (ptyOpen && (pollFds[3].revents & POLLIN) != 0) readTextOutput(process.ptyMasterFd, MRGdbEventKind::InferiorOutput, info, sourceId, targetBufferId, generation, ptyOpen);
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
	const std::uint64_t newTaskId = mr::coprocessor::globalCoprocessor().submitExternal(newSourceId, std::string("gdb: ") + programPath,
	    [channel, newSourceId, targetBufferId, newGeneration, programPath, sourcePath](const mr::coprocessor::TaskInfo &info) {
		    return runGdbSessionTask(info, channel, newSourceId, targetBufferId, newGeneration, programPath, sourcePath);
	    });
	if (newTaskId == 0) {
		mr::coprocessor::globalCoprocessor().unregisterExternalSource(newSourceId);
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

void MRGdbSession::markFinished(std::uint64_t eventGeneration) noexcept {
	if (eventGeneration != generation) return;
	sourceId = 0;
	taskId = 0;
	controlChannel.reset();
}

bool MRGdbSession::active() const noexcept { return sourceId != 0; }
std::uint64_t MRGdbSession::currentGeneration() const noexcept { return generation; }
