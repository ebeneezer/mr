#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TObject
#define Uses_TEvent
#define Uses_TRect
#define Uses_TView
#define Uses_TButton
#define Uses_TKeys
#define Uses_TStaticText
#include <tvision/tv.h>

#include "MRCommandRouterSearchMultiFileSession.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "../../dialogs/setup/MRSetupCommon.hpp"
#include "../../ui/MRFrame.hpp"
#include "../../ui/MRWindowSupport.hpp"
#include "../../ui/widgets/MRNumericSlider.hpp"

namespace {

TFrame *initMrReplaceAllDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}

enum : ushort {
	cmMrMultiReplaceCancel = 4955,
	cmMrMultiReplaceContinue = 4956,
	cmMrMultiReplaceAbort = 4957,
	cmMrMultiReplaceRevert = 4958,
	cmMrMultiReplaceCompleted = 4959,
	cmMrMultiReplaceError = 4960,
	cmMrMultiReplaceReverted = 4961
};

constexpr std::chrono::milliseconds kReplaceWorkSlice(8);
constexpr std::chrono::milliseconds kReplaceProgressInterval(50);

class MultiFileReplaceDecisionDialog final : public MRDialogFoundation {
  public:
	MultiFileReplaceDecisionDialog()
	    : TWindowInit(initMrReplaceAllDialogFrame),
	      MRDialogFoundation(mr::dialogs::centeredDialogRect(72, 8), "CANCEL REPLACE ALL", 72, 8, initMrReplaceAllDialogFrame) {
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown && event.keyDown.keyCode == kbEsc) {
			endModal(cmMrMultiReplaceAbort);
			clearEvent(event);
			return;
		}
		if (event.what == evCommand) {
			switch (event.message.command) {
				case cmMrMultiReplaceContinue:
				case cmMrMultiReplaceAbort:
				case cmMrMultiReplaceRevert:
					endModal(event.message.command);
					clearEvent(event);
					return;
				case cmCancel:
				case cmClose:
					endModal(cmMrMultiReplaceAbort);
					clearEvent(event);
					return;
			}
		}
		MRDialogFoundation::handleEvent(event);
	}
};

ushort runMultiFileReplaceDecisionDialog(std::size_t completedCount) {
	MultiFileReplaceDecisionDialog *dialog = nullptr;
	ushort result = cmMrMultiReplaceContinue;
	const std::string prompt = std::to_string(completedCount) + " replacements completed. Continue, abort, or revert?";

	if (TProgram::deskTop == nullptr) return cmMrMultiReplaceAbort;
	dialog = new MultiFileReplaceDecisionDialog();
	dialog->insert(new TStaticText(TRect(2, 2, 70, 4), prompt.c_str()));
	{
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~C~ontinue", cmMrMultiReplaceContinue, bfDefault},
		                         mr::dialogs::DialogButtonSpec{"~A~bort", cmMrMultiReplaceAbort, bfNormal},
		                         mr::dialogs::DialogButtonSpec{"~R~evert", cmMrMultiReplaceRevert, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 2);
		mr::dialogs::insertUniformButtonRow(*dialog, (72 - metrics.rowWidth) / 2, 5, 2, buttons);
	}
	result = mr::dialogs::execDialog(dialog);
	switch (result) {
		case cmMrMultiReplaceContinue:
			return result;
		case cmMrMultiReplaceAbort:
		case cmMrMultiReplaceRevert:
			return result;
		default:
			return cmMrMultiReplaceAbort;
	}
}

class MultiFileReplaceAllDialog final : public MRDialogFoundation {
  public:
	MultiFileReplaceAllDialog(MultiFileSearchSession &aSession, std::size_t &aCompletedCount, std::string &aErrorText)
	    : TWindowInit(initMrReplaceAllDialogFrame),
	      MRDialogFoundation(mr::dialogs::centeredDialogRect(72, 7), "REPLACE ALL", 72, 7, initMrReplaceAllDialogFrame),
	      session(aSession),
	      checkpoints(aSession.files.size()),
	      completedCount(aCompletedCount),
	      errorText(aErrorText),
	      totalCount(sessionTotalMatchCount(aSession)) {
		eventMask |= evBroadcast;
		progressView = new MRProgressSlider(TRect(2, 2, 70, 3));
		insert(progressView);
		{
			const std::array buttons{mr::dialogs::DialogButtonSpec{"~C~ancel", cmMrMultiReplaceCancel, bfDefault}};
			const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 0);
			std::vector<TButton *> insertedButtons;
			mr::dialogs::insertUniformButtonRow(*this, (72 - metrics.rowWidth) / 2, 4, 0, buttons, 0, &insertedButtons);
			if (!insertedButtons.empty()) cancelButton = insertedButtons.front();
		}
		updateReplaceProgress();
	}

	Boolean valid(ushort command) override {
		if (command == cmCancel || command == cmClose) {
			if (phase == Phase::Replacing) requestCancelDecision();
			return False;
		}
		return MRDialogFoundation::valid(command);
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evCommand && (event.message.command == cmMrMultiReplaceCancel || event.message.command == cmCancel || event.message.command == cmClose)) {
			if (phase == Phase::Replacing) requestCancelDecision();
			clearEvent(event);
			return;
		}
		if (workTimer != nullptr && event.what == evBroadcast && event.message.command == cmTimerExpired && event.message.infoPtr == workTimer) {
			workTimer = nullptr;
			const bool continueWork = phase == Phase::Replacing ? performReplaceSlice() : performRevertStep();
			if (continueWork) armWorkTimer();
			clearEvent(event);
			return;
		}
		if (event.what == evNothing) {
			armWorkTimer();
			clearEvent(event);
			return;
		}
		MRDialogFoundation::handleEvent(event);
	}

  private:
	enum class Phase : unsigned char {
		Replacing,
		Reverting
	};

	void armWorkTimer() {
		if (workTimer == nullptr && owner != nullptr) workTimer = setTimer(1);
	}

	void stopWorkTimer() {
		if (workTimer == nullptr) return;
		killTimer(workTimer);
		workTimer = nullptr;
	}

	void updateReplaceProgress(bool force = false) {
		const auto now = std::chrono::steady_clock::now();

		if (!force && progressDrawn && now - lastProgressAt < kReplaceProgressInterval) return;
		progressDrawn = true;
		lastProgressAt = now;
		if (progressView != nullptr) progressView->setProgress(completedCount, totalCount, std::to_string(completedCount) + "/" + std::to_string(totalCount));
	}

	void updateRevertProgress(bool force = false) {
		const auto now = std::chrono::steady_clock::now();

		if (!force && progressDrawn && now - lastProgressAt < kReplaceProgressInterval) return;
		progressDrawn = true;
		lastProgressAt = now;
		if (progressView != nullptr) progressView->setProgress(revertedCount, completedCount, std::to_string(revertedCount) + "/" + std::to_string(completedCount));
	}

	void logReplacementPerformance(const char *outcome) {
		if (performanceLogged) return;
		performanceLogged = true;
		const auto activeElapsed = std::chrono::steady_clock::now() - startedAt - pausedDuration;
		const auto elapsedMs = std::max<std::int64_t>(1, std::chrono::duration_cast<std::chrono::milliseconds>(activeElapsed).count());
		const std::size_t replacementsPerSecond = completedCount * 1000 / static_cast<std::size_t>(elapsedMs);
		std::ostringstream line;

		line << "MFSAR Replace All " << (outcome != nullptr ? outcome : "ended") << ": replacements=" << completedCount << " elapsed_ms=" << elapsedMs << " replacements_per_second=" << replacementsPerSecond << ".";
		mrLogMessage(line.str());
	}

	std::size_t retainedReplacementCount() const noexcept {
		std::size_t count = 0;

		for (const MultiFileReplaceCheckpoint &checkpoint : checkpoints)
			count += checkpoint.replacements;
		return count;
	}

	void finishWithError() {
		stopWorkTimer();
		completedCount = retainedReplacementCount();
		session.valid = false;
		if (errorText.empty()) errorText = "Replace All failed.";
		if (phase == Phase::Replacing) {
			updateReplaceProgress(true);
			logReplacementPerformance("failed");
		} else
			updateRevertProgress(true);
		endModal(cmMrMultiReplaceError);
	}

	bool performReplaceStep() {
		while (fileIndex < session.files.size() && session.files[fileIndex].matches.empty())
			++fileIndex;
		if (fileIndex >= session.files.size()) {
			stopWorkTimer();
			session.valid = false;
			updateReplaceProgress(true);
			logReplacementPerformance("completed");
			endModal(cmMrMultiReplaceCompleted);
			return false;
		}

		std::size_t fileReplacementCount = 0;
		if (!replaceAllSessionMatchesInFile(session, fileIndex, checkpoints[fileIndex], fileReplacementCount, errorText)) {
			finishWithError();
			return false;
		}
		completedCount += fileReplacementCount;
		session.valid = false;
		++fileIndex;
		return true;
	}

	bool performReplaceSlice() {
		const auto deadline = std::chrono::steady_clock::now() + kReplaceWorkSlice;

		do {
			if (!performReplaceStep()) return false;
		} while (std::chrono::steady_clock::now() < deadline);
		updateReplaceProgress();
		return true;
	}

	void requestCancelDecision() {
		stopWorkTimer();
		updateReplaceProgress(true);
		const auto pauseStartedAt = std::chrono::steady_clock::now();
		const ushort decision = runMultiFileReplaceDecisionDialog(completedCount);
		pausedDuration += std::chrono::steady_clock::now() - pauseStartedAt;

		switch (decision) {
			case cmMrMultiReplaceContinue:
				updateReplaceProgress(true);
				armWorkTimer();
				return;
			case cmMrMultiReplaceAbort:
				session.valid = false;
				logReplacementPerformance("aborted");
				endModal(cmMrMultiReplaceAbort);
				return;
			default:
				break;
		}
		for (std::size_t index = 0; index < checkpoints.size(); ++index) {
			if (checkpoints[index].replacements == 0) continue;
			if (!validateSessionFileReplaceCheckpoint(session, index, checkpoints[index], errorText)) {
				finishWithError();
				return;
			}
		}
		phase = Phase::Reverting;
		revertFileIndex = checkpoints.size();
		if (cancelButton != nullptr) cancelButton->setState(sfDisabled, True);
		logReplacementPerformance("revert requested");
		updateRevertProgress(true);
		armWorkTimer();
	}

	bool performRevertStep() {
		while (revertFileIndex > 0) {
			const std::size_t index = --revertFileIndex;
			const std::size_t fileReplacementCount = checkpoints[index].replacements;

			if (fileReplacementCount == 0) continue;
			if (!revertSessionFileReplacements(session, index, checkpoints[index], errorText)) {
				finishWithError();
				return false;
			}
			revertedCount += fileReplacementCount;
			updateRevertProgress();
			return true;
		}
		stopWorkTimer();
		session.valid = false;
		updateRevertProgress(true);
		endModal(cmMrMultiReplaceReverted);
		return false;
	}

	MultiFileSearchSession &session;
	std::vector<MultiFileReplaceCheckpoint> checkpoints;
	std::size_t &completedCount;
	std::string &errorText;
	std::size_t totalCount = 0;
	std::size_t fileIndex = 0;
	std::size_t revertFileIndex = 0;
	std::size_t revertedCount = 0;
	MRProgressSlider *progressView = nullptr;
	TButton *cancelButton = nullptr;
	TTimerId workTimer = nullptr;
	Phase phase = Phase::Replacing;
	std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point lastProgressAt{};
	std::chrono::steady_clock::duration pausedDuration{};
	bool progressDrawn = false;
	bool performanceLogged = false;
};

} // namespace

MultiReplaceAllOutcome runMultiFileReplaceAllDialog(MultiFileSearchSession &session, std::size_t &completedCount, std::string &errorText) {
	completedCount = 0;
	errorText.clear();
	if (sessionTotalMatchCount(session) == 0) return MultiReplaceAllOutcome::Completed;
	if (TProgram::deskTop == nullptr) {
		errorText = "No desktop available for Replace All.";
		return MultiReplaceAllOutcome::Error;
	}

	const ushort result = mr::dialogs::execDialog(new MultiFileReplaceAllDialog(session, completedCount, errorText));
	switch (result) {
		case cmMrMultiReplaceCompleted:
			return MultiReplaceAllOutcome::Completed;
		case cmMrMultiReplaceAbort:
			return MultiReplaceAllOutcome::Aborted;
		case cmMrMultiReplaceReverted:
			return MultiReplaceAllOutcome::Reverted;
		default:
			if (errorText.empty()) errorText = "Replace All failed.";
			return MultiReplaceAllOutcome::Error;
	}
}
