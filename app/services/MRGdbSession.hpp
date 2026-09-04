#ifndef MRGDBSESSION_HPP
#define MRGDBSESSION_HPP

#include "MRGdbMi.hpp"
#include "../../coprocessor/MRCoprocessor.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class MRGdbCommandKind : unsigned char {
	ContinueExecution,
	PauseExecution,
	RunToLocation,
	StepInto,
	StepOver,
	StepOut,
	ToggleBreakpoint,
	AddBreakpoint,
	AddWatch,
	EraseWatch,
	Evaluate,
	AssignVariable,
	TerminalInput,
	ResizeTerminal,
	Quit
};

struct MRGdbCommand {
	MRGdbCommand() noexcept;
	explicit MRGdbCommand(MRGdbCommandKind aKind) noexcept;

	MRGdbCommandKind kind;
	std::string text;
	std::string file;
	std::string objectName;
	int line;
	int columns;
	int rows;
};

enum class MRGdbEventKind : unsigned char {
	Started,
	DebuggerOutput,
	InferiorOutput,
	Running,
	Stopped,
	Variables,
	Watches,
	Breakpoints,
	Finished
};

struct MRGdbEvent {
	MRGdbEvent() noexcept;

	MRGdbEventKind kind;
	std::string text;
	std::string file;
	int line;
	std::vector<MRGdbMiVariable> variables;
	std::vector<int> breakpointLines;
};

namespace mr::coprocessor {

struct GdbEventPayload : Payload {
	GdbEventPayload() noexcept;
	GdbEventPayload(std::size_t aSourceId, int aTargetBufferId, std::uint64_t aGeneration, MRGdbEvent aEvent);

	std::size_t sourceId;
	int targetBufferId;
	std::uint64_t generation;
	MRGdbEvent event;
};

struct GdbStreamEventPayload final : GdbEventPayload, StreamingPayload {
	GdbStreamEventPayload(std::size_t aSourceId, int aTargetBufferId, std::uint64_t aGeneration, MRGdbEvent aEvent);
};

struct GdbFinishedPayload final : GdbEventPayload {
	GdbFinishedPayload(std::size_t aSourceId, int aTargetBufferId, std::uint64_t aGeneration, MRGdbEvent aEvent);
};

} // namespace mr::coprocessor

class MRGdbControlChannel;

class MRGdbSession {
  public:
	MRGdbSession() noexcept;
	~MRGdbSession();

	MRGdbSession(const MRGdbSession &) = delete;
	MRGdbSession &operator=(const MRGdbSession &) = delete;

	[[nodiscard]] bool start(const std::string &programPath, const std::string &sourcePath, int targetBufferId, std::string &errorMessage);
	[[nodiscard]] bool send(MRGdbCommand command);
	void stop() noexcept;
	void markFinished(std::uint64_t eventGeneration) noexcept;
	[[nodiscard]] bool active() const noexcept;
	[[nodiscard]] std::uint64_t currentGeneration() const noexcept;

  private:
	std::shared_ptr<MRGdbControlChannel> controlChannel;
	std::size_t sourceId;
	std::uint64_t taskId;
	std::uint64_t generation;
};

#endif
