#include "MRCommandRouterGit.hpp"

#include "../commands/MRExternalCommand.hpp"
#include "../commands/MRWindowCommands.hpp"
#include "../../coprocessor/MRCoprocessor.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/MRWindowSupport.hpp"

#include <filesystem>
#include <memory>
#include <sstream>
#include <utility>

namespace {

struct MRGitStatusPayload final : mr::coprocessor::Payload {
	std::string filePath;
	std::uint64_t generation;
	bool available;
	bool changed;

	MRGitStatusPayload(std::string aFilePath, std::uint64_t aGeneration, bool anAvailable, bool aChanged)
	    : filePath(std::move(aFilePath)), generation(aGeneration), available(anAvailable), changed(aChanged) {
	}
};

std::string gitStatusProbeCommand(const std::string &filePath) {
	const std::filesystem::path path(filePath);
	const std::string directory = path.parent_path().empty() ? std::string(".") : path.parent_path().string();
	const std::string fileName = path.filename().string();
	std::ostringstream command;

	command << "git_status=$(git -C " << quoteShellArgument(directory)
	        << " status --porcelain=v1 --untracked-files=all -- " << quoteShellArgument(fileName)
	        << " 2>/dev/null) || exit 20; "
	        << "[ -n \"$git_status\" ] && exit 10; exit 0";
	return command.str();
}

} // namespace

bool prepareMRGitChangesCommand(const std::string &filePath, MRGitChangesCommand &command) {
	const std::filesystem::path path(filePath);
	const std::string directory = path.parent_path().empty() ? std::string(".") : path.parent_path().string();
	const std::string fileName = path.filename().string();
	const std::string quotedDirectory = quoteShellArgument(directory);
	const std::string quotedFileName = quoteShellArgument(fileName);
	std::ostringstream shell;

	command = MRGitChangesCommand();
	if (fileName.empty()) return false;

	command.title = "Git Changes: " + fileName;
	shell << "repo=$(git -C " << quotedDirectory << " rev-parse --show-toplevel 2>/dev/null) || "
	      << "{ printf '%s\\n' '[not a Git work tree]'; exit 1; }; "
	      << "branch=$(git -C \"$repo\" branch --show-current 2>/dev/null); "
	      << "[ -n \"$branch\" ] || branch=$(git -C \"$repo\" rev-parse --short HEAD 2>/dev/null); "
	      << "printf 'Repository: %s\\nBranch: %s\\n\\nStatus:\\n' \"$repo\" \"$branch\"; "
	      << "git -C " << quotedDirectory << " --no-pager status --short --branch; "
	      << "printf '\\nChanges for %s:\\n' " << quotedFileName << "; "
	      << "if git -C " << quotedDirectory << " ls-files --error-unmatch -- " << quotedFileName << " >/dev/null 2>&1; then "
	      << "git -C " << quotedDirectory << " --no-pager diff HEAD -- " << quotedFileName << "; "
	      << "else git -C " << quotedDirectory << " --no-pager diff --no-index -- /dev/null " << quotedFileName << " || true; fi";
	command.commandLine = shell.str();
	return true;
}

void requestMRGitStatusProbe(MREditWindow *window) {
	if (window == nullptr || !window->hasPersistentFileName()) return;

	const std::string filePath = window->currentFileName();
	if (filePath.empty() || window->gitStatusProbeCurrentFor(filePath)) return;

	const std::uint64_t generation = window->beginGitStatusProbe(filePath);
	const std::string command = gitStatusProbeCommand(filePath);
	const std::size_t bufferId = static_cast<std::size_t>(window->bufferId());
	const std::uint64_t taskId = mr::coprocessor::globalCoprocessor().submit(
	    mr::coprocessor::Lane::Io, mr::coprocessor::TaskKind::Custom, bufferId, 0,
	    mr::coprocessor::ExecutionOwnerKind::EditorWindow, bufferId, "git-status",
	    [bufferId, command, filePath, generation](const mr::coprocessor::TaskInfo &info) {
		    mr::coprocessor::Result result = runExternalCommandTask(info, bufferId, command, MRBuildHookContext(), std::string(), std::string(), false);
		    const mr::coprocessor::ExternalIoFinishedPayload *finished =
		        dynamic_cast<const mr::coprocessor::ExternalIoFinishedPayload *>(result.payload.get());
		    const bool available = result.completed() && finished != nullptr &&
		                           !finished->signaled && (finished->exitCode == 0 || finished->exitCode == 10);
		    const bool changed = available && finished->exitCode == 10;

		    result.payload = std::make_shared<MRGitStatusPayload>(filePath, generation, available, changed);
		    return result;
	    });
	if (taskId == 0) {
		window->abandonGitStatusProbe(generation, filePath);
		return;
	}
	window->attachGitStatusProbeTask(taskId, generation, filePath);
}

bool dispatchMRGitStatusResult(const mr::coprocessor::Result &result) {
	const MRGitStatusPayload *payload = dynamic_cast<const MRGitStatusPayload *>(result.payload.get());

	if (payload == nullptr) return false;
	MREditWindow *window = findEditWindowByBufferId(static_cast<int>(result.task.executionOwnerLocalId));
	const bool adopted = window != nullptr &&
	                     window->adoptGitStatusProbe(result.task.id, payload->generation, payload->filePath,
	                                                 payload->available, payload->changed);
	mr::coprocessor::globalCoprocessor().noteResultAdoption(result, adopted);
	if (result.failed()) mrLogMessage((std::string("Git status probe failed: ") + result.error).c_str());
	return true;
}
