#define Uses_TGroup
#define Uses_TScrollBar
#include <tvision/tv.h>

#include "MRCoprocessorDispatch.hpp"

#include <chrono>
#include <sstream>
#include <string>
#include <vector>

#include "MRPerformance.hpp"
#include "MRWindowCommands.hpp"

#include "../mrmac/mrvm.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/TMRFileEditor.hpp"
#include "../ui/TMRIndicator.hpp"
#include "../ui/TMREditWindow.hpp"
#include "../ui/MRWindowSupport.hpp"

namespace {
const char *kLineIndexWarmAction = "Line index warming";
const char *kSyntaxWarmAction = "Syntax warming";
const char *kMiniMapRenderAction = "Mini map rendering";
const char *kSaveNormalizationWarmAction = "Save normalization warming";

const char *coprocessorLaneName(mr::coprocessor::Lane lane) {
	switch (lane) {
		case mr::coprocessor::Lane::Io:
			return "io";
		case mr::coprocessor::Lane::MiniMap:
			return "minimap";
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
	    result, action, win != nullptr ? static_cast<std::size_t>(win->bufferId()) : 0, documentId, bytes, detail);
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
	    win != nullptr ? static_cast<std::size_t>(win->bufferId()) : 0, documentId, bytes, detail);
}

std::string formatTimingSummary(const mr::coprocessor::TaskTiming &timing) {
	return " [q " + mr::performance::formatDuration(timing.queueMs()) + ", run " +
	       mr::performance::formatDuration(timing.runMs()) + ", total " +
	       mr::performance::formatDuration(timing.totalMs()) + "]";
}

long long roundedMilliseconds(double valueMs) {
	if (valueMs <= 0.0)
		return 0;
	return static_cast<long long>(valueMs + 0.5);
}

void postMiniMapHeroEvent(const mr::coprocessor::TaskTiming &timing, const mr::coprocessor::MiniMapWarmupPayload &payload) {
	if (payload.totalLines <= 1)
		return;
	const long long totalMs = roundedMilliseconds(timing.totalMs());
	const std::string timeStr = totalMs >= 1 ? std::to_string(totalMs) : "< 1";
	const std::string heroText =
	    "mini map render " + timeStr + " ms, " +
	    std::to_string(payload.rowCount) + "x" + std::to_string(payload.bodyWidth) + " glyphs, " +
	    std::to_string(payload.totalLines) + " lines";

	mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEvent, heroText, mr::messageline::Kind::Success,
	                               mr::messageline::kPriorityHigh);
	mr::messageline::clearOwner(mr::messageline::Owner::HeroEventFollowup);
}

void appendMacroLogLines(const std::vector<std::string> &logLines) {
	for (const auto & logLine : logLines) {
		std::string prefixed = "  ";
		prefixed += logLine;
		mrLogMessage(prefixed.c_str());
	}
}

void releaseMacroTask(TMREditWindow *win, const mr::coprocessor::Result &result, const char *state) {
	int bufferId = win != nullptr ? win->bufferId() : static_cast<int>(result.task.documentId);
	if (win != nullptr)
		win->releaseCoprocessorTask(result.task.id);
	mrTraceCoprocessorTaskRelease(bufferId, result.task.id, state);
}

const char *deferredUiCommandName(int type) {
	switch (type) {
		case mrducCreateWindow:
			return "CREATE_WINDOW";
		case mrducDeleteWindow:
			return "DELETE_WINDOW";
		case mrducModifyWindow:
			return "MODIFY_WINDOW";
		case mrducLinkWindow:
			return "LINK_WINDOW";
		case mrducUnlinkWindow:
			return "UNLINK_WINDOW";
		case mrducZoom:
			return "ZOOM";
		case mrducRedraw:
			return "REDRAW";
		case mrducNewScreen:
			return "NEW_SCREEN";
		case mrducSwitchWindow:
			return "SWITCH_WINDOW";
		case mrducSizeWindow:
			return "SIZE_WINDOW";
		default:
			return "UNKNOWN";
	}
}

bool applyDeferredUiCommand(const MRMacroDeferredUiCommand &command) {
	switch (command.type) {
		case mrducCreateWindow:
			return mrvmUiCreateWindow();
		case mrducDeleteWindow:
			return mrvmUiDeleteCurrentWindow();
		case mrducModifyWindow:
			return mrvmUiModifyCurrentWindow();
		case mrducLinkWindow:
			return mrvmUiLinkCurrentWindow();
		case mrducUnlinkWindow:
			return mrvmUiUnlinkCurrentWindow();
		case mrducZoom:
			return mrvmUiZoomCurrentWindow();
		case mrducRedraw:
			return mrvmUiRedrawCurrentWindow();
		case mrducNewScreen:
			return mrvmUiNewScreen();
		case mrducSwitchWindow:
			return mrvmUiSwitchWindow(command.a1);
		case mrducSizeWindow:
			return mrvmUiSizeCurrentWindow(command.a1, command.a2, command.a3, command.a4);
		default:
			return false;
	}
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
			for (auto & window : windows) {
				TMRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
				if (editor == nullptr)
					continue;
				if (editor->documentId() != result.task.documentId) {
					editor->clearLineIndexWarmupTask(result.task.id);
					continue;
				}
				bool applied = false;
				if (editor->documentVersion() == result.task.baseVersion)
					applied = editor->applyLineIndexWarmup(warmup->warmup, result.task.baseVersion);
				if (!applied)
					editor->clearLineIndexWarmupTask(result.task.id);
				if (!recorded) {
					recordTaskPerformance(result, kLineIndexWarmAction, window, editor->documentId(),
					                      editor->bufferLength(), window->currentFileName());
					recorded = true;
				}
			}
			if (!recorded)
				recordTaskPerformance(result, kLineIndexWarmAction, nullptr, result.task.documentId, 0,
				                      result.task.label);
			return;
		}

			const mr::coprocessor::SyntaxWarmupPayload *syntax =
			    dynamic_cast<const mr::coprocessor::SyntaxWarmupPayload *>(result.payload.get());
			if (syntax != nullptr) {
			std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
			bool recorded = false;
			for (auto & window : windows) {
				TMRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
				if (editor == nullptr)
					continue;
				if (editor->documentId() != result.task.documentId) {
					editor->clearSyntaxWarmupTask(result.task.id);
					continue;
				}
				bool applied = false;
				if (editor->documentVersion() == result.task.baseVersion)
					applied = editor->applySyntaxWarmup(*syntax, result.task.baseVersion, result.task.id);
				if (!applied)
					editor->clearSyntaxWarmupTask(result.task.id);
				if (!recorded) {
					recordTaskPerformance(result, kSyntaxWarmAction, window, editor->documentId(),
					                      editor->bufferLength(), window->currentFileName());
					recorded = true;
				}
			}
				if (!recorded)
					recordTaskPerformance(result, kSyntaxWarmAction, nullptr, result.task.documentId, 0,
					                      result.task.label);
				return;
			}

			const mr::coprocessor::MiniMapWarmupPayload *miniMap =
			    dynamic_cast<const mr::coprocessor::MiniMapWarmupPayload *>(result.payload.get());
			if (miniMap != nullptr) {
				std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
				bool recorded = false;
				bool postedHero = false;
				for (auto &window : windows) {
					TMRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
					if (editor == nullptr)
						continue;
					if (editor->documentId() != result.task.documentId) {
						editor->clearMiniMapWarmupTask(result.task.id);
						continue;
					}
					bool applied = false;
					if (editor->documentVersion() == result.task.baseVersion)
						applied = editor->applyMiniMapWarmup(*miniMap, result.task.baseVersion, result.task.id);
					if (!applied)
						editor->clearMiniMapWarmupTask(result.task.id);
					if (applied) {
						const bool shouldPostHero = editor->shouldReportMiniMapInitialRender();
						editor->markMiniMapInitialRenderReported();
						if (shouldPostHero && !postedHero) {
							postMiniMapHeroEvent(result.timing, *miniMap);
							postedHero = true;
						}
					}
					if (!recorded) {
						recordTaskPerformance(result, kMiniMapRenderAction, window, editor->documentId(),
						                      editor->bufferLength(), window->currentFileName());
						recorded = true;
					}
				}
				if (!recorded)
					recordTaskPerformance(result, kMiniMapRenderAction, nullptr, result.task.documentId, 0,
					                      result.task.label);
				return;
			}

			const mr::coprocessor::SaveNormalizationWarmupPayload *saveNormalization =
			    dynamic_cast<const mr::coprocessor::SaveNormalizationWarmupPayload *>(result.payload.get());
			if (saveNormalization != nullptr) {
				std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
				bool recorded = false;
				for (auto & window : windows) {
					TMRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
					if (editor == nullptr)
						continue;
					if (editor->documentId() != result.task.documentId) {
						editor->clearSaveNormalizationWarmupTask(result.task.id);
						continue;
					}
					bool applied = false;
					if (editor->documentVersion() == result.task.baseVersion)
						applied = editor->applySaveNormalizationWarmup(
						    *saveNormalization, result.task.baseVersion, result.task.id,
						    static_cast<double>(result.timing.runMicros));
					if (!applied)
						editor->clearSaveNormalizationWarmupTask(result.task.id);
					if (!recorded) {
						recordTaskPerformance(result, kSaveNormalizationWarmAction, window, editor->documentId(),
						                      saveNormalization->sourceBytes, window->currentFileName());
						recorded = true;
					}
				}
				if (!recorded)
					recordTaskPerformance(result, kSaveNormalizationWarmAction, nullptr, result.task.documentId, 0,
					                      result.task.label);
				return;
			}

		const mr::coprocessor::ExternalIoChunkPayload *chunk =
		    dynamic_cast<const mr::coprocessor::ExternalIoChunkPayload *>(result.payload.get());
		if (chunk != nullptr) {
			TMREditWindow *win = findEditWindowByBufferId(static_cast<int>(chunk->channelId));
			if (win != nullptr) {
				win->appendTextBuffer(chunk->text.c_str());
				win->setReadOnly(true);
				win->setFileChanged(false);
			}
			return;
		}

		const mr::coprocessor::ExternalIoFinishedPayload *finished =
		    dynamic_cast<const mr::coprocessor::ExternalIoFinishedPayload *>(result.payload.get());
		if (finished != nullptr) {
			std::ostringstream statusLine;
			TMREditWindow *targetWindow = findEditWindowByBufferId(static_cast<int>(finished->channelId));
			if (targetWindow != nullptr) {
				std::string exitLine = communicationExitLine(*finished);
				targetWindow->appendTextBuffer(exitLine.c_str());
				targetWindow->setReadOnly(true);
				targetWindow->setFileChanged(false);
				recordTaskPerformance(result, "External command", targetWindow, targetWindow->documentId(),
				                      targetWindow->bufferLength(),
				                      externalIoDisplayName(result.task));
				targetWindow->releaseCoprocessorTask(result.task.id);
			} else {
				recordTaskPerformance(result, "External command", nullptr, 0, 0, externalIoDisplayName(result.task));
			}
			mrTraceCoprocessorTaskRelease(static_cast<int>(finished->channelId), result.task.id, "finished");
			statusLine << "Communication session #" << finished->channelId << " ";
			if (finished->signaled)
				statusLine << "terminated by signal " << finished->signalNumber;
			else
				statusLine << "finished with exit code " << finished->exitCode;
			mrLogMessage(statusLine.str().c_str());
			return;
		}

		const mr::coprocessor::MacroJobFinishedPayload *macro =
		    dynamic_cast<const mr::coprocessor::MacroJobFinishedPayload *>(result.payload.get());
		const mr::coprocessor::MacroJobStagedPayload *staged =
		    dynamic_cast<const mr::coprocessor::MacroJobStagedPayload *>(result.payload.get());
		if (staged != nullptr) {
			std::ostringstream statusLine;
			TMREditWindow *targetWindow = findEditWindowByBufferId(static_cast<int>(result.task.documentId));
			TMRFileEditor *targetEditor = targetWindow != nullptr ? targetWindow->getEditor() : nullptr;
			bool accepted = false;
			bool textChanged = false;
			std::size_t currentVersion = 0;
			std::string statusSummary;

			if (targetWindow != nullptr && targetEditor != nullptr) {
				currentVersion = targetEditor->documentVersion();
				TMRTextBufferModel::CommitResult commit = targetEditor->applyStagedTransaction(
				    staged->transaction, staged->cursorOffset, staged->selectionStart,
				    staged->selectionEnd, staged->fileChanged);
				if (!commit.conflicted()) {
					targetEditor->setInsertModeEnabled(staged->insertMode);
					targetWindow->setIndentLevel(staged->indentLevel);
					targetWindow->setCurrentFileName(staged->fileName.c_str());
					targetWindow->applyCommittedBlockState(staged->blockMode, staged->blockMarkingOn,
					                             static_cast<uint>(staged->blockAnchor),
					                             static_cast<uint>(staged->blockEnd));
					mrvmUiReplaceGlobals(staged->globalOrder, staged->globalInts, staged->globalStrings);
					mrvmUiReplaceWindowLastSearch(targetWindow, staged->fileName, staged->lastSearchValid,
					                             staged->lastSearchStart, staged->lastSearchEnd,
					                             staged->lastSearchCursor);
					mrvmUiReplaceRuntimeOptions(staged->ignoreCase, staged->tabExpand);
					mrvmUiReplaceWindowMarkStack(targetWindow, staged->markStack);
					mrvmUiSyncLinkedWindowsFrom(targetWindow);
					accepted = true;
					textChanged = commit.applied();
				}
			}

			statusLine << "Background staged macro '" << staged->displayName << "'";
			if (accepted) {
				if (textChanged)
					statusLine << " committed";
				else
					statusLine << " applied state without text changes";
				if (staged->hadError)
					statusLine << " with VM errors";
				statusLine << formatTimingSummary(result.timing) << ".";
				statusSummary = statusLine.str();
				recordMacroPerformance(result, targetWindow,
				                       targetEditor != nullptr ? targetEditor->documentId() : 0,
				                       targetEditor != nullptr ? targetEditor->bufferLength() : 0,
				                       staged->displayName);
				if (targetWindow != nullptr)
					targetWindow->noteBackgroundMacroCompleted(statusSummary);
				releaseMacroTask(targetWindow, result, textChanged ? "committed" : "state-only");
				if (!staged->deferredUiCommands.empty()) {
					TMREditWindow *applyWin =
					    findEditWindowByBufferId(static_cast<int>(result.task.documentId));
					std::size_t applied = 0;
					std::size_t failed = 0;
					if (applyWin != nullptr)
						mrvmUiSetCurrentWindow(applyWin);
					for (const auto & deferredUiCommand : staged->deferredUiCommands) {
						if (applyDeferredUiCommand(deferredUiCommand))
							++applied;
						else {
							std::string line = std::string("Deferred UI command failed: ") +
							                   deferredUiCommandName(deferredUiCommand.type);
							mrLogMessage(line.c_str());
							++failed;
						}
					}
					{
						std::ostringstream uiLine;
						uiLine << "Applied deferred UI commands for macro '" << staged->displayName
						       << "': ok=" << applied << ", failed=" << failed << ".";
						mrLogMessage(uiLine.str().c_str());
					}
				}
			} else {
				statusLine << " conflicted with a newer document state";
				if (currentVersion != 0)
					statusLine << " (snapshot " << result.task.baseVersion << ", current " << currentVersion << ")";
				statusLine << "; commit aborted without rebase" << formatTimingSummary(result.timing) << ".";
				statusSummary = statusLine.str();
				recordMacroPerformance(result, targetWindow,
				                       targetEditor != nullptr ? targetEditor->documentId() : 0,
				                       targetEditor != nullptr ? targetEditor->bufferLength() : 0, staged->displayName,
				                       mr::performance::Outcome::Conflict);
				if (targetWindow != nullptr)
					targetWindow->noteBackgroundMacroConflict(statusSummary);
				releaseMacroTask(targetWindow, result, "conflict");
			}
			mrLogMessage(statusSummary.c_str());
			appendMacroLogLines(staged->logLines);
			return;
		}
		if (macro != nullptr) {
			std::ostringstream statusLine;
			TMREditWindow *targetWindow = findEditWindowByBufferId(static_cast<int>(result.task.documentId));
			std::string statusSummary;

			recordMacroPerformance(result, targetWindow, targetWindow != nullptr ? targetWindow->documentId() : 0,
			                       targetWindow != nullptr ? targetWindow->bufferLength() : 0,
			                       macro->displayName);
			statusLine << "Background macro '" << macro->displayName << "' finished";
			if (macro->hadError)
				statusLine << " with VM errors";
			statusLine << formatTimingSummary(result.timing) << ".";
			statusSummary = statusLine.str();
			if (targetWindow != nullptr)
				targetWindow->noteBackgroundMacroCompleted(statusSummary);
			releaseMacroTask(targetWindow, result, "finished");
			mrLogMessage(statusSummary.c_str());
			appendMacroLogLines(macro->logLines);
			return;
		}
	}

	if (result.task.kind == mr::coprocessor::TaskKind::ExternalIo) {
		TMREditWindow *targetWindow = findEditWindowByBufferId(static_cast<int>(result.task.documentId));
		if (targetWindow != nullptr) {
			targetWindow->releaseCoprocessorTask(result.task.id);
			if (result.cancelled()) {
				targetWindow->appendTextBuffer("\n[process cancelled]\n");
				targetWindow->setReadOnly(true);
				targetWindow->setFileChanged(false);
			} else if (result.failed()) {
				std::string failureLine = "\n[process failed: " + result.error + "]\n";
				targetWindow->appendTextBuffer(failureLine.c_str());
				targetWindow->setReadOnly(true);
				targetWindow->setFileChanged(false);
			}
		}
		recordTaskPerformance(result, "External command", targetWindow,
		                      targetWindow != nullptr ? targetWindow->documentId() : 0,
		                      targetWindow != nullptr ? targetWindow->bufferLength() : 0,
		                      externalIoDisplayName(result.task));
		if (result.cancelled())
			mrTraceCoprocessorTaskRelease(static_cast<int>(result.task.documentId), result.task.id, "cancelled");
		else if (result.failed())
			mrTraceCoprocessorTaskRelease(static_cast<int>(result.task.documentId), result.task.id, "failed");
	}

	if (result.task.kind == mr::coprocessor::TaskKind::MacroJob) {
		TMREditWindow *targetWindow = findEditWindowByBufferId(static_cast<int>(result.task.documentId));
		std::string displayName = macroDisplayName(result.task);
		std::string statusSummary;

		if (result.cancelled()) {
			statusSummary = "Background macro '" + displayName + "' cancelled" +
			                formatTimingSummary(result.timing) + ".";
			recordMacroPerformance(result, targetWindow, targetWindow != nullptr ? targetWindow->documentId() : 0,
			                       targetWindow != nullptr ? targetWindow->bufferLength() : 0, displayName,
			                       mr::performance::Outcome::Cancelled);
			if (targetWindow != nullptr)
				targetWindow->noteBackgroundMacroCancelled(statusSummary);
			releaseMacroTask(targetWindow, result, "cancelled");
			mrLogMessage(statusSummary.c_str());
		} else if (result.failed()) {
			std::ostringstream failureLine;
			failureLine << "Background macro '" << displayName << "' failed";
			if (!result.error.empty())
				failureLine << ": " << result.error;
			failureLine << formatTimingSummary(result.timing) << ".";
			statusSummary = failureLine.str();
			recordMacroPerformance(result, targetWindow,
			                       targetWindow != nullptr ? targetWindow->documentId() : 0,
			                       targetWindow != nullptr ? targetWindow->bufferLength() : 0, displayName,
			                       mr::performance::Outcome::Failed);
			if (targetWindow != nullptr)
				targetWindow->noteBackgroundMacroFailed(statusSummary);
			releaseMacroTask(targetWindow, result, "failed");
			mrLogMessage(statusSummary.c_str());
		}
		if (result.failed())
			return;
	}

	if (result.task.kind == mr::coprocessor::TaskKind::LineIndexWarmup) {
		std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
		bool recorded = false;
		for (auto & window : windows) {
			TMRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
			if (editor == nullptr)
				continue;
			if (!recorded && editor->documentId() == result.task.documentId) {
				recordTaskPerformance(result, kLineIndexWarmAction, window, editor->documentId(),
				                      editor->bufferLength(), window->currentFileName());
				recorded = true;
			}
			editor->clearLineIndexWarmupTask(result.task.id);
		}
		if (!recorded)
			recordTaskPerformance(result, kLineIndexWarmAction, nullptr, result.task.documentId, 0, result.task.label);
	}

	if (result.task.kind == mr::coprocessor::TaskKind::SyntaxWarmup) {
		std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
		bool recorded = false;
		for (auto & window : windows) {
			TMRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
			if (editor == nullptr)
				continue;
			if (!recorded && editor->documentId() == result.task.documentId) {
				recordTaskPerformance(result, kSyntaxWarmAction, window, editor->documentId(),
				                      editor->bufferLength(), window->currentFileName());
				recorded = true;
			}
			editor->clearSyntaxWarmupTask(result.task.id);
		}
		if (!recorded)
			recordTaskPerformance(result, kSyntaxWarmAction, nullptr, result.task.documentId, 0, result.task.label);
	}

	if (result.task.kind == mr::coprocessor::TaskKind::MiniMapWarmup) {
		std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
		bool recorded = false;
		for (auto &window : windows) {
			TMRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
			if (editor == nullptr)
				continue;
			if (!recorded && editor->documentId() == result.task.documentId) {
				recordTaskPerformance(result, kMiniMapRenderAction, window, editor->documentId(),
				                      editor->bufferLength(), window->currentFileName());
				recorded = true;
			}
			editor->clearMiniMapWarmupTask(result.task.id);
		}
		if (!recorded)
			recordTaskPerformance(result, kMiniMapRenderAction, nullptr, result.task.documentId, 0, result.task.label);
	}

	if (result.task.kind == mr::coprocessor::TaskKind::SaveNormalizationWarmup) {
		std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
		bool recorded = false;
		for (auto &window : windows) {
			TMRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
			if (editor == nullptr)
				continue;
			if (!recorded && editor->documentId() == result.task.documentId) {
				recordTaskPerformance(result, kSaveNormalizationWarmAction, window, editor->documentId(),
				                      editor->bufferLength(), window->currentFileName());
				recorded = true;
			}
			editor->clearSaveNormalizationWarmupTask(result.task.id);
		}
		if (!recorded)
			recordTaskPerformance(result, kSaveNormalizationWarmAction, nullptr, result.task.documentId, 0,
			                      result.task.label);
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
	if (state != nullptr && *state != '\0')
		line << " (" << state << ")";
	line << ".";
	mrLogMessage(line.str().c_str());
}
