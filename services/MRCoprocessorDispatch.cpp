#define Uses_TGroup
#define Uses_TScrollBar
#include <tvision/tv.h>

#include "MRCoprocessorDispatch.hpp"

#include <sstream>
#include <string>
#include <vector>

#include "MRPerformance.hpp"
#include "MRWindowCommands.hpp"

#include "../ui/TMRFileEditor.hpp"
#include "../ui/TMRIndicator.hpp"
#include "../ui/TMREditWindow.hpp"
#include "../ui/MRWindowSupport.hpp"

namespace {
const char *coprocessorLaneName(mr::coprocessor::Lane lane) {
	switch (lane) {
		case mr::coprocessor::Lane::Io:
			return "io";
		case mr::coprocessor::Lane::Macro:
			return "macro";
		case mr::coprocessor::Lane::Compute:
		default:
			return "compute";
	}
}

std::string communicationExitLine(const mr::coprocessor::ExternalIoFinishedPayload &payload) {
	std::ostringstream out;

	out << "\n[process ";
	if (payload.signaled)
		out << "terminated by signal " << payload.signalNumber;
	else
		out << "exited with code " << payload.exitCode;
	out << "]\n";
	return out.str();
}

std::string macroDisplayName(const mr::coprocessor::TaskInfo &task, const char *payloadName = nullptr) {
	if (payloadName != nullptr && *payloadName != '\0')
		return payloadName;
	if (task.label.rfind("macro: ", 0) == 0)
		return task.label.substr(7);
	if (!task.label.empty())
		return task.label;
	return "macro";
}

std::string externalIoDisplayName(const mr::coprocessor::TaskInfo &task) {
	if (task.label.rfind("external-io: ", 0) == 0)
		return task.label.substr(13);
	if (!task.label.empty())
		return task.label;
	return "external command";
}

void recordTaskPerformance(const mr::coprocessor::Result &result, const std::string &action, TMREditWindow *win,
                           std::size_t documentId, std::size_t bytes, const std::string &detail) {
	mr::performance::recordBackgroundResult(
	    result, action, win != 0 ? static_cast<std::size_t>(win->bufferId()) : 0, documentId, bytes, detail);
}

void recordMacroPerformance(const mr::coprocessor::Result &result, TMREditWindow *win, std::size_t documentId,
                            std::size_t bytes, const std::string &detail,
                            mr::performance::Outcome outcome = mr::performance::Outcome::Completed) {
	if (outcome == mr::performance::Outcome::Completed) {
		recordTaskPerformance(result, "Background macro", win, documentId, bytes, detail);
		return;
	}
	mr::performance::recordBackgroundEvent(
	    result.task.lane, outcome, result.timing, "Background macro",
	    win != 0 ? static_cast<std::size_t>(win->bufferId()) : 0, documentId, bytes, detail);
}

std::string formatTimingSummary(const mr::coprocessor::TaskTiming &timing) {
	return " [q " + mr::performance::formatDuration(timing.queueMs()) + ", run " +
	       mr::performance::formatDuration(timing.runMs()) + ", total " +
	       mr::performance::formatDuration(timing.totalMs()) + "]";
}

void appendMacroLogLines(const std::vector<std::string> &logLines) {
	for (std::size_t i = 0; i < logLines.size(); ++i) {
		std::string prefixed = "  ";
		prefixed += logLines[i];
		mrLogMessage(prefixed.c_str());
	}
}

void releaseMacroTask(TMREditWindow *win, const mr::coprocessor::Result &result, const char *state) {
	int bufferId = win != 0 ? win->bufferId() : static_cast<int>(result.task.documentId);
	if (win != 0)
		win->releaseCoprocessorTask(result.task.id);
	mrTraceCoprocessorTaskRelease(bufferId, result.task.id, state);
}
} // namespace

void handleCoprocessorResult(const mr::coprocessor::Result &result) {
	if (result.completed()) {
		const mr::coprocessor::IndicatorBlinkPayload *blink =
		    dynamic_cast<const mr::coprocessor::IndicatorBlinkPayload *>(result.payload.get());
		if (blink != nullptr) {
			TMRIndicator::applyBlinkUpdate(blink->indicatorId, blink->channel, blink->generation,
			                               blink->visible);
			return;
		}

		const mr::coprocessor::LineIndexWarmupPayload *warmup =
		    dynamic_cast<const mr::coprocessor::LineIndexWarmupPayload *>(result.payload.get());
		if (warmup != nullptr) {
			std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
			bool recorded = false;
			for (std::size_t i = 0; i < windows.size(); ++i) {
				TMRFileEditor *editor = windows[i] != nullptr ? windows[i]->getEditor() : nullptr;
				if (editor == nullptr)
					continue;
				if (editor->documentId() != result.task.documentId ||
				    editor->documentVersion() != result.task.baseVersion)
					continue;
				editor->applyLineIndexWarmup(warmup->warmup, result.task.baseVersion);
				if (!recorded) {
					recordTaskPerformance(result, "Line index warmup", windows[i], editor->documentId(),
					                      editor->bufferLength(), windows[i]->currentFileName());
					recorded = true;
				}
			}
			if (!recorded)
				recordTaskPerformance(result, "Line index warmup", 0, result.task.documentId, 0, result.task.label);
			return;
		}

		const mr::coprocessor::SyntaxWarmupPayload *syntax =
		    dynamic_cast<const mr::coprocessor::SyntaxWarmupPayload *>(result.payload.get());
		if (syntax != nullptr) {
			std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
			bool recorded = false;
			for (std::size_t i = 0; i < windows.size(); ++i) {
				TMRFileEditor *editor = windows[i] != nullptr ? windows[i]->getEditor() : nullptr;
				if (editor == nullptr)
					continue;
				if (editor->documentId() != result.task.documentId ||
				    editor->documentVersion() != result.task.baseVersion)
					continue;
				editor->applySyntaxWarmup(*syntax, result.task.baseVersion, result.task.id);
				if (!recorded) {
					recordTaskPerformance(result, "Syntax warmup", windows[i], editor->documentId(),
					                      editor->bufferLength(), windows[i]->currentFileName());
					recorded = true;
				}
			}
			if (!recorded)
				recordTaskPerformance(result, "Syntax warmup", 0, result.task.documentId, 0, result.task.label);
			return;
		}

		const mr::coprocessor::ExternalIoChunkPayload *chunk =
		    dynamic_cast<const mr::coprocessor::ExternalIoChunkPayload *>(result.payload.get());
		if (chunk != nullptr) {
			TMREditWindow *win = findEditWindowByBufferId(static_cast<int>(chunk->channelId));
			if (win != 0) {
				win->appendTextBuffer(chunk->text.c_str());
				win->setReadOnly(true);
				win->setFileChanged(false);
			}
			return;
		}

		const mr::coprocessor::ExternalIoFinishedPayload *finished =
		    dynamic_cast<const mr::coprocessor::ExternalIoFinishedPayload *>(result.payload.get());
		if (finished != nullptr) {
			std::ostringstream line;
			TMREditWindow *win = findEditWindowByBufferId(static_cast<int>(finished->channelId));
			if (win != 0) {
				std::string exitLine = communicationExitLine(*finished);
				win->appendTextBuffer(exitLine.c_str());
				win->setReadOnly(true);
				win->setFileChanged(false);
				recordTaskPerformance(result, "External command", win, win->documentId(), win->bufferLength(),
				                      externalIoDisplayName(result.task));
				win->releaseCoprocessorTask(result.task.id);
			} else {
				recordTaskPerformance(result, "External command", 0, 0, 0, externalIoDisplayName(result.task));
			}
			mrTraceCoprocessorTaskRelease(static_cast<int>(finished->channelId), result.task.id, "finished");
			line << "Communication session #" << finished->channelId << " ";
			if (finished->signaled)
				line << "terminated by signal " << finished->signalNumber;
			else
				line << "finished with exit code " << finished->exitCode;
			mrLogMessage(line.str().c_str());
			return;
		}

		const mr::coprocessor::MacroJobFinishedPayload *macro =
		    dynamic_cast<const mr::coprocessor::MacroJobFinishedPayload *>(result.payload.get());
		const mr::coprocessor::MacroJobStagedPayload *staged =
		    dynamic_cast<const mr::coprocessor::MacroJobStagedPayload *>(result.payload.get());
		if (staged != nullptr) {
			std::ostringstream line;
			TMREditWindow *win = findEditWindowByBufferId(static_cast<int>(result.task.documentId));
			TMRFileEditor *editor = win != 0 ? win->getEditor() : 0;
			bool accepted = false;
			bool textChanged = false;
			std::size_t currentVersion = 0;
			std::string summary;

			if (win != 0 && editor != 0) {
				currentVersion = editor->documentVersion();
				TMRTextBufferModel::CommitResult commit = editor->applyStagedTransaction(
				    staged->transaction, staged->cursorOffset, staged->selectionStart,
				    staged->selectionEnd, staged->fileChanged);
				if (!commit.conflicted()) {
					editor->setInsertModeEnabled(staged->insertMode);
					win->setIndentLevel(staged->indentLevel);
					if (!staged->fileName.empty())
						win->setCurrentFileName(staged->fileName.c_str());
					accepted = true;
					textChanged = commit.applied();
				}
			}

			line << "Background staged macro '" << staged->displayName << "'";
			if (accepted) {
				if (textChanged)
					line << " committed";
				else
					line << " applied state without text changes";
				if (staged->hadError)
					line << " with VM errors";
				line << formatTimingSummary(result.timing) << ".";
				summary = line.str();
				recordMacroPerformance(result, win, editor != 0 ? editor->documentId() : 0,
				                       editor != 0 ? editor->bufferLength() : 0, staged->displayName);
				if (win != 0)
					win->noteBackgroundMacroCompleted(summary);
				releaseMacroTask(win, result, textChanged ? "committed" : "state-only");
			} else {
				line << " conflicted with a newer document state";
				if (currentVersion != 0)
					line << " (snapshot " << result.task.baseVersion << ", current " << currentVersion << ")";
				line << "; commit aborted without rebase" << formatTimingSummary(result.timing) << ".";
				summary = line.str();
				recordMacroPerformance(result, win, editor != 0 ? editor->documentId() : 0,
				                       editor != 0 ? editor->bufferLength() : 0, staged->displayName,
				                       mr::performance::Outcome::Conflict);
				if (win != 0)
					win->noteBackgroundMacroConflict(summary);
				releaseMacroTask(win, result, "conflict");
			}
			mrLogMessage(summary.c_str());
			appendMacroLogLines(staged->logLines);
			return;
		}
		if (macro != nullptr) {
			std::ostringstream line;
			TMREditWindow *win = findEditWindowByBufferId(static_cast<int>(result.task.documentId));
			std::string summary;

			recordMacroPerformance(result, win, win != 0 ? win->documentId() : 0,
			                       win != 0 ? win->bufferLength() : 0, macro->displayName);
			line << "Background macro '" << macro->displayName << "' finished";
			if (macro->hadError)
				line << " with VM errors";
			line << formatTimingSummary(result.timing) << ".";
			summary = line.str();
			if (win != 0)
				win->noteBackgroundMacroCompleted(summary);
			releaseMacroTask(win, result, "finished");
			mrLogMessage(summary.c_str());
			appendMacroLogLines(macro->logLines);
			return;
		}
	}

	if (result.task.kind == mr::coprocessor::TaskKind::ExternalIo) {
		TMREditWindow *win = findEditWindowByBufferId(static_cast<int>(result.task.documentId));
		if (win != 0) {
			win->releaseCoprocessorTask(result.task.id);
			if (result.cancelled()) {
				win->appendTextBuffer("\n[process cancelled]\n");
				win->setReadOnly(true);
				win->setFileChanged(false);
			} else if (result.failed()) {
				std::string line = "\n[process failed: " + result.error + "]\n";
				win->appendTextBuffer(line.c_str());
				win->setReadOnly(true);
				win->setFileChanged(false);
			}
		}
		recordTaskPerformance(result, "External command", win, win != 0 ? win->documentId() : 0,
		                      win != 0 ? win->bufferLength() : 0, externalIoDisplayName(result.task));
		if (result.cancelled())
			mrTraceCoprocessorTaskRelease(static_cast<int>(result.task.documentId), result.task.id, "cancelled");
		else if (result.failed())
			mrTraceCoprocessorTaskRelease(static_cast<int>(result.task.documentId), result.task.id, "failed");
	}

	if (result.task.kind == mr::coprocessor::TaskKind::MacroJob) {
		TMREditWindow *win = findEditWindowByBufferId(static_cast<int>(result.task.documentId));
		std::string name = macroDisplayName(result.task);
		std::string summary;

		if (result.cancelled()) {
			summary = "Background macro '" + name + "' cancelled" + formatTimingSummary(result.timing) + ".";
			recordMacroPerformance(result, win, win != 0 ? win->documentId() : 0,
			                       win != 0 ? win->bufferLength() : 0, name,
			                       mr::performance::Outcome::Cancelled);
			if (win != 0)
				win->noteBackgroundMacroCancelled(summary);
			releaseMacroTask(win, result, "cancelled");
			mrLogMessage(summary.c_str());
		} else if (result.failed()) {
			std::ostringstream line;
			line << "Background macro '" << name << "' failed";
			if (!result.error.empty())
				line << ": " << result.error;
			line << formatTimingSummary(result.timing) << ".";
			summary = line.str();
			recordMacroPerformance(result, win, win != 0 ? win->documentId() : 0,
			                       win != 0 ? win->bufferLength() : 0, name,
			                       mr::performance::Outcome::Failed);
			if (win != 0)
				win->noteBackgroundMacroFailed(summary);
			releaseMacroTask(win, result, "failed");
			mrLogMessage(summary.c_str());
		}
		if (result.failed())
			return;
	}

	if (result.task.kind == mr::coprocessor::TaskKind::LineIndexWarmup) {
		std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
		bool recorded = false;
		for (std::size_t i = 0; i < windows.size(); ++i) {
			TMRFileEditor *editor = windows[i] != nullptr ? windows[i]->getEditor() : nullptr;
			if (editor == nullptr)
				continue;
			if (editor->documentId() != result.task.documentId)
				continue;
			if (!recorded) {
				recordTaskPerformance(result, "Line index warmup", windows[i], editor->documentId(),
				                      editor->bufferLength(), windows[i]->currentFileName());
				recorded = true;
			}
			editor->clearLineIndexWarmupTask(result.task.id);
		}
		if (!recorded)
			recordTaskPerformance(result, "Line index warmup", 0, result.task.documentId, 0, result.task.label);
	}

	if (result.task.kind == mr::coprocessor::TaskKind::SyntaxWarmup) {
		std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
		bool recorded = false;
		for (std::size_t i = 0; i < windows.size(); ++i) {
			TMRFileEditor *editor = windows[i] != nullptr ? windows[i]->getEditor() : nullptr;
			if (editor == nullptr)
				continue;
			if (editor->documentId() != result.task.documentId)
				continue;
			if (!recorded) {
				recordTaskPerformance(result, "Syntax warmup", windows[i], editor->documentId(),
				                      editor->bufferLength(), windows[i]->currentFileName());
				recorded = true;
			}
			editor->clearSyntaxWarmupTask(result.task.id);
		}
		if (!recorded)
			recordTaskPerformance(result, "Syntax warmup", 0, result.task.documentId, 0, result.task.label);
	}

	if (!result.failed())
		return;

	std::ostringstream line;
	line << "Coprocessor[" << coprocessorLaneName(result.task.lane) << "] "
	     << (result.task.label.empty() ? "task" : result.task.label)
	     << " failed";
	if (!result.error.empty())
		line << ": " << result.error;
	mrLogMessage(line.str().c_str());
}

void mrTraceCoprocessorTaskCancel(int bufferId, std::uint64_t taskId) {
	std::ostringstream line;

	line << "Cancelling coprocessor task #" << taskId << " for window #" << bufferId << ".";
	mrLogMessage(line.str().c_str());
}

void mrTraceCoprocessorTaskRelease(int bufferId, std::uint64_t taskId, const char *state) {
	std::ostringstream line;

	line << "Released coprocessor task #" << taskId << " for window #" << bufferId;
	if (state != 0 && *state != '\0')
		line << " (" << state << ")";
	line << ".";
	mrLogMessage(line.str().c_str());
}
