#include "MRBentoBox.hpp"
#include "MRBentoBoxRoleSupport.hpp"

#include "../MRWindowSupport.hpp"

#include "../../app/MRCommandRouter.hpp"

#include <utility>

namespace {

bool splitCommandTargetsSecondaryPane(ushort command) noexcept {
	switch (command) {
		case cmClose:
		case cmMrOtherBuildCurrentFile:
		case cmMrOtherGitChanges:
		case cmMrOtherStopProgram:
		case cmMrOtherRestartProgram:
		case cmMrOtherClearOutput:
		case cmMrOtherFindNextCompilerError:
		case cmMrOtherFindPreviousCompilerError:
		case cmMrMacroDebuggerContinue:
		case cmMrMacroDebuggerStep:
		case cmMrMacroDebuggerStop:
		case cmMrMacroDebuggerAddWatch:
		case cmMrMacroDebuggerEraseWatch:
		case cmMrMacroDebuggerRunHere:
			return false;
		default:
			return true;
	}
}

bool splitEventTargetsSecondaryPane(const TEvent &event) noexcept {
	if ((event.what & (evMouseDown | evMouseMove | evMouseUp | evMouseAuto | evMouseWheel | evKeyDown)) != 0) return true;
	if (event.what == evCommand) return splitCommandTargetsSecondaryPane(event.message.command);
	return false;
}

} // namespace

bool MRBentoBox::compilerDiagnosticsContextEstablished() const noexcept {
	return (buildOutputPane() != nullptr && problemsPane() != nullptr) || diagnosticsProjectionTask.taskId != 0 ||
	       compilerDiagnosticsParseSourceSnapshot != nullptr || compilerDiagnosticsOutputBufferId != 0 ||
	       compilerDiagnosticSourceChanges != nullptr || (compilerDiagnostics != nullptr && !compilerDiagnostics->empty());
}

void MRBentoBox::handleCommittedSourceEditor(MRFileEditor *committedEditor) {
	MRFileEditor *sourceEditor = getEditor();
	const bool sourceCommitted = sourceEditor != nullptr && committedEditor != nullptr &&
	                             (committedEditor == sourceEditor || committedEditor->documentId() == sourceEditor->documentId());

	if (!sourceCommitted || bentoSourceMutationTrackingActive) return;
	if (compilerDiagnosticsContextEstablished()) {
		const int pendingNavigation = pendingCompilerProblemNavigation;
		clearCompilerDiagnostics();
		compilerDiagnosticsParseSourceSnapshot.reset();
		compilerDiagnosticsParseRequired = false;
		compilerDiagnosticsSourceInvalidated = true;
		diagnosticsProjectionTask.projectionCurrent = false;
		if (pendingNavigation != 0) {
			pendingCompilerProblemNavigation = pendingNavigation;
			static_cast<void>(requestCompilerProblemNavigation(pendingNavigation > 0));
		}
	}
	refreshOutlinePanes(false);
	bentoProjectionDirty |= bpdContent | bpdChrome | bpdOverlay;
	flushBentoProjection();
}

void MRBentoBox::handleEvent(TEvent &event) {
	const bool mouseEvent = (event.what & (evMouseDown | evMouseMove | evMouseUp | evMouseAuto | evMouseWheel)) != 0;
	const TPoint localMouse = mouseEvent ? makeLocal(event.mouse.where) : TPoint();

	if (bentoProjectionAdoptionActive && event.what == evBroadcast &&
	    (event.message.command == cmMrEditorDocumentCommitted || event.message.command == cmUpdateTitle)) {
		MREditWindow::handleEvent(event);
		return;
	}
	if (event.what == evBroadcast && event.message.command == cmMrEditorDocumentCommitted) {
		MRFileEditor *committedEditor = static_cast<MRFileEditor *>(event.message.infoPtr);

		MREditWindow::handleEvent(event);
		handleCommittedSourceEditor(committedEditor);
		return;
	}
	if (!hasPaneSplit()) {
		MRFileEditor *sourceEditor = getEditor();
		MREditWindow *outputWindow = buildOutputPane();
		const bool compilerOutputActive = outputWindow != nullptr && outputWindow->hasTrackedExternalIoTasks();
		const bool trackSourceMutation = sourceEditor != nullptr && ((compilerDiagnosticsParseSourceSnapshot != nullptr && (compilerDiagnosticsParseRequired || compilerOutputActive)) ||
		                                                          (compilerDiagnostics != nullptr && !compilerDiagnostics->empty()) ||
		                                                          compilerDiagnosticSourceChanges != nullptr || diagnosticsProjectionTask.taskId != 0);
		MRTextBufferModel::ReadSnapshot oldSnapshot;
		if (trackSourceMutation) oldSnapshot = buffer().readSnapshot();
		const bool trackingWasActive = bentoSourceMutationTrackingActive;
		bentoSourceMutationTrackingActive = true;
		MREditWindow::handleEvent(event);
		bentoSourceMutationTrackingActive = trackingWasActive;
		if (trackSourceMutation) syncCompilerDiagnosticsAfterSourceMutation(oldSnapshot, sourceEditor->lastDocumentChangeSet());
		return;
	}
	const int mouseLeafId = mouseEvent ? leafAt(localMouse) : -1;
	if (event.what == evBroadcast && event.message.command == cmUpdateTitle) {
		const bool diagnosticsContext = compilerDiagnosticsContextEstablished();
		MREditWindow::handleEvent(event);
		if (!bentoSourceMutationTrackingActive) {
			if (diagnosticsContext && diagnosticsProjectionTask.sourcePath != currentFileName())
				handleCommittedSourceEditor(getEditor());
			else if (diagnosticsContext)
				static_cast<void>(refreshCompilerProblemsPane());
		}
		refreshOutlinePanes(false);
		bentoProjectionDirty |= bpdContent | bpdChrome;
		flushBentoProjection();
		return;
	}
	if (event.what == evCommand && event.message.command == mr::bento::cmPaneRoleAccepted) {
		acceptPaneRoleChoice();
		clearEvent(event);
		bentoProjectionDirty |= bpdChrome;
		flushBentoProjection();
		return;
	}
	if (event.what == evCommand && event.message.command == mr::bento::cmPaneActionAccepted) {
		acceptPaneActionChoice();
		clearEvent(event);
		bentoProjectionDirty |= bpdChrome;
		flushBentoProjection();
		return;
	}
	if (event.what == evCommand && event.message.command == mr::bento::cmFileComparePaneActionAccepted) {
		acceptFileCompareActionChoice();
		clearEvent(event);
		bentoProjectionDirty |= bpdContent | bpdChrome | bpdScrollBar | bpdOverlay;
		flushBentoProjection();
		return;
	}
	if (event.what == evCommand && event.message.command == cmClose) {
		if ((flags & wfClose) != 0 && (event.message.infoPtr == nullptr || event.message.infoPtr == this)) {
			clearEvent(event);
			if ((state & sfModal) == 0)
				close();
			else {
				event.what = evCommand;
				event.message.command = cmCancel;
				putEvent(event);
				clearEvent(event);
			}
		}
		return;
	}
	if (handleOuterFrameCloseMouse(event)) {
		bentoProjectionDirty |= bpdChrome;
		flushBentoProjection();
		return;
	}
	if (handlePaneDropListEvent(event)) {
		bentoProjectionDirty |= bpdChrome;
		flushBentoProjection();
		return;
	}
	if (event.what == evKeyDown && TKey(event.keyDown.keyCode, event.keyDown.controlKeyState) == TKey(kbCtrlTab)) {
		toggleActivePane();
		clearEvent(event);
		bentoProjectionDirty |= bpdChrome;
		flushBentoProjection();
		return;
	}
	if (macroDebuggerValueInput != nullptr && event.what == evMouseDown && !macroDebuggerValueInputContains(event.mouse.where)) {
		cancelMacroDebuggerValueInput();
		clearEvent(event);
		bentoProjectionDirty |= bpdContent | bpdChrome;
		flushBentoProjection();
		return;
	}
	if (handleMacroDebuggerFunctionKey(event)) return;
	if (bentoMode == bbmFileCompare && event.what == evKeyDown && mr::bento::paneRoleIsDiff(roleForLeaf(activeLeafId))) {
		const bool nextChangeKey = event.keyDown.keyCode == kbF8 && (event.keyDown.controlKeyState & kbShift) == 0;
		const bool previousChangeKey = event.keyDown.keyCode == kbShiftF8 || (event.keyDown.keyCode == kbF8 && (event.keyDown.controlKeyState & kbShift) != 0);

		if (nextChangeKey || previousChangeKey) {
			if (navigateFileCompareChange(nextChangeKey)) {
				clearEvent(event);
				return;
			}
		}
	}
	if (event.what == evKeyDown && compilerSidekickTracked && ctrlToArrow(event.keyDown.keyCode) == kbEsc) {
		clearTrackedCompilerSidekick(true);
		clearEvent(event);
		bentoProjectionDirty |= bpdContent | bpdChrome;
		flushBentoProjection();
		return;
	}
	if (event.what == evKeyDown && buildOutputPane() != nullptr && problemsPane() != nullptr) {
		const bool nextProblemKey = event.keyDown.keyCode == kbF8 && (event.keyDown.controlKeyState & kbShift) == 0;
		const bool previousProblemKey = event.keyDown.keyCode == kbShiftF8 || (event.keyDown.keyCode == kbF8 && (event.keyDown.controlKeyState & kbShift) != 0);
		if (nextProblemKey || previousProblemKey) {
			MREditWindow *targetWindow = editorCommandTarget();
			if (mrHandleRuntimeKeymapEvent(event, targetWindow != nullptr && targetWindow->isReadOnly() ? MRKeymapContext::ReadOnly : MRKeymapContext::Edit, targetWindow)) return;
			static_cast<void>(requestCompilerProblemNavigation(nextProblemKey));
			clearEvent(event);
			bentoProjectionDirty |= bpdContent | bpdChrome | bpdOverlay;
			flushBentoProjection();
			return;
		}
	}
	if (event.what == evMouseDown) {
		if (handleDividerChromeMouse(event)) {
			clearEvent(event);
			bentoProjectionDirty |= bpdChrome;
			flushBentoProjection();
			return;
		}
		const int dividerNode = nodeAtDivider(localMouse);
		if (dividerNode >= 0 && (event.mouse.buttons & mbLeftButton) != 0) {
			dragDivider(event, dividerNode);
			clearEvent(event);
			bentoProjectionDirty |= bpdLayout;
			flushBentoProjection();
			return;
		}
		if (bentoMode == bbmFileCompare && (event.mouse.buttons & mbRightButton) != 0) {
			const int targetLeafId = leafAt(localMouse);
			if (mr::bento::paneRoleIsDiff(roleForLeaf(targetLeafId)) && pointInRect(localMouse, contentBounds(paneBoundsForLeaf(targetLeafId)))) {
				showFileCompareActionList(event.mouse.where, targetLeafId);
				clearEvent(event);
				bentoProjectionDirty |= bpdChrome;
				flushBentoProjection();
				return;
			}
		}
	}
	if (bentoMode == bbmFileCompare && event.what == evMouseWheel) {
		const int wheelLeafId = leafAt(localMouse);
		const MRBentoPaneRole wheelRole = roleForLeaf(wheelLeafId);

		if (mr::bento::paneRoleIsDiff(wheelRole)) {
			MRPaneEditWindow *wheelPane = paneWindowForLeaf(wheelLeafId);
			MREditWindow *wheelWindow = wheelPane != nullptr ? static_cast<MREditWindow *>(wheelPane) : static_cast<MREditWindow *>(this);
			MRFileEditor *wheelEditor = wheelWindow != nullptr ? wheelWindow->getEditor() : nullptr;

			if (wheelEditor != nullptr) {
				static_cast<void>(wheelEditor->scrollWindowByWheel(event.mouse.wheel));
				clearEvent(event);
				syncFileCompareLinkedPaneFrom(wheelLeafId, false);
				if (wheelPane != nullptr) wheelWindow->drawView();
				bentoProjectionDirty |= bpdContent | bpdChrome | bpdScrollBar | bpdOverlay;
				flushBentoProjection();
				return;
			}
		}
	}
	const int previousActiveLeafId = activeLeafId;
	if (event.what == evMouseDown) setActivePaneForMouse(event.mouse.where);
	const int targetLeafId = event.what == evMouseWheel && mouseLeafId >= 0 ? mouseLeafId : activeLeafId;
	const bool mouseTargetsTargetPane = !mouseEvent || mouseLeafId == targetLeafId;
	MRPaneEditWindow *targetPane = paneWindowForLeaf(targetLeafId);
	if (targetPane != nullptr && mouseTargetsTargetPane && splitEventTargetsSecondaryPane(event)) {
		TRect targetBounds = paneBoundsForLeaf(targetLeafId);
		const MRBentoPaneRole activeRole = roleForLeaf(targetLeafId);
		const bool targetProjectsContentLocally = targetPane->projectsPaneContentLocally();
		const bool targetFocusChanged = previousActiveLeafId != activeLeafId;
		const bool problemsPaneActive = activeRole == bprProblems;
		const bool outlinePaneActive = mr::bento::paneRoleIsOutline(activeRole);
		const bool enterProblem = problemsPaneActive && event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter;
		const bool clickProblem = problemsPaneActive && event.what == evMouseDown && (event.mouse.buttons & mbLeftButton) != 0;
		const bool enterOutline = outlinePaneActive && event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter;
		const bool clickOutline = outlinePaneActive && event.what == evMouseDown && (event.mouse.buttons & mbLeftButton) != 0;
		const bool clickDebuggerVariable = activeRole == bprVariables && event.what == evMouseDown && (event.mouse.buttons & mbLeftButton) != 0;
		if (mouseEvent && !pointInRect(localMouse, contentBounds(targetBounds))) {
			if (targetPane != nullptr) targetPane->handleEvent(event);
			if (bentoMode == bbmFileCompare && mr::bento::paneRoleIsDiff(activeRole)) syncFileCompareLinkedPaneFrom(targetLeafId);
			if (!targetProjectsContentLocally) bentoProjectionDirty |= bpdContent;
			bentoProjectionDirty |= bpdChrome;
			flushBentoProjection();
			return;
		}
		if (enterProblem) {
			static_cast<void>(jumpToProblemAtCursor());
			clearEvent(event);
			bentoProjectionDirty |= bpdContent | bpdChrome | bpdOverlay;
			flushBentoProjection();
			return;
		}
		if (enterOutline) {
			static_cast<void>(jumpToOutlineAtCursor(activeRole));
			clearEvent(event);
			bentoProjectionDirty |= bpdContent | bpdChrome | bpdOverlay;
			flushBentoProjection();
			return;
		}
		MRFileEditor *sourceEditor = getEditor();
		MRFileEditor *targetEditor = targetPane->getEditor();
		const bool targetSharesSource = activeRole == bprSplitEditor;
		MREditWindow *outputWindow = buildOutputPane();
		const bool compilerOutputActive = outputWindow != nullptr && outputWindow->hasTrackedExternalIoTasks();
		const bool trackSourceMutation = targetSharesSource && sourceEditor != nullptr && targetEditor != nullptr &&
		                                 ((compilerDiagnosticsParseSourceSnapshot != nullptr && (compilerDiagnosticsParseRequired || compilerOutputActive)) ||
		                                  (compilerDiagnostics != nullptr && !compilerDiagnostics->empty()) ||
		                                  compilerDiagnosticSourceChanges != nullptr || diagnosticsProjectionTask.taskId != 0);
		MRTextBufferModel::ReadSnapshot oldSnapshot;
		TScrollBar *targetHorizontalScrollBar = targetPane != nullptr ? targetPane->horizontalEditorScrollBar() : nullptr;
		TScrollBar *targetVerticalScrollBar = targetPane != nullptr ? targetPane->verticalEditorScrollBar() : nullptr;
		const std::pair<bool, bool> targetRangeBefore = std::make_pair(targetHorizontalScrollBar != nullptr && targetHorizontalScrollBar->maxVal > targetHorizontalScrollBar->minVal, targetVerticalScrollBar != nullptr && targetVerticalScrollBar->maxVal > targetVerticalScrollBar->minVal);
		const bool trackFileCompareMutation = bentoMode == bbmFileCompare && fileComparePanesEditable() && mr::bento::paneRoleIsDiff(activeRole);
		std::size_t fileCompareVersionBefore = 0;
		if (trackSourceMutation) oldSnapshot = buffer().readSnapshot();
		if (targetPane != nullptr) {
			if (trackFileCompareMutation && targetEditor != nullptr) fileCompareVersionBefore = targetEditor->documentVersion();
			if (event.what == evMouseWheel && targetEditor != nullptr && !targetPane->ownsPaneWheelEvents()) {
				static_cast<void>(targetEditor->scrollWindowByWheel(event.mouse.wheel));
				clearEvent(event);
				bentoProjectionDirty |= bpdScrollBar;
			} else {
				const bool trackingWasActive = bentoSourceMutationTrackingActive;
				if (targetSharesSource) bentoSourceMutationTrackingActive = true;
				targetPane->handleEvent(event);
				bentoSourceMutationTrackingActive = trackingWasActive;
			}
		}
		if (clickDebuggerVariable && showMacroDebuggerValueInputAtCursor()) {
			clearEvent(event);
			bentoProjectionDirty |= bpdContent | bpdChrome;
			flushBentoProjection();
			return;
		}
		if (trackSourceMutation) syncCompilerDiagnosticsAfterSourceMutation(oldSnapshot, targetEditor->lastDocumentChangeSet());
		if (targetSharesSource) refreshOutlinePanes(false);
		if (clickProblem) {
			static_cast<void>(jumpToProblemAtCursor());
			clearEvent(event);
			bentoProjectionDirty |= bpdContent | bpdChrome | bpdOverlay;
			flushBentoProjection();
			return;
		}
		if (clickOutline) {
			static_cast<void>(jumpToOutlineAtCursor(activeRole));
			clearEvent(event);
			bentoProjectionDirty |= bpdContent | bpdChrome | bpdOverlay;
			flushBentoProjection();
			return;
		}
		targetHorizontalScrollBar = targetPane != nullptr ? targetPane->horizontalEditorScrollBar() : nullptr;
		targetVerticalScrollBar = targetPane != nullptr ? targetPane->verticalEditorScrollBar() : nullptr;
		const std::pair<bool, bool> targetRangeAfter = std::make_pair(targetHorizontalScrollBar != nullptr && targetHorizontalScrollBar->maxVal > targetHorizontalScrollBar->minVal, targetVerticalScrollBar != nullptr && targetVerticalScrollBar->maxVal > targetVerticalScrollBar->minVal);
		const bool targetRangeChanged = targetRangeAfter != targetRangeBefore;
		if (targetPane != nullptr && targetRangeChanged) targetPane->layoutPaneChrome();
		if (trackFileCompareMutation && targetPane != nullptr) targetPane->setReadOnly(false);
		const bool fileCompareMutated = trackFileCompareMutation && targetPane != nullptr && targetPane->getEditor() != nullptr && targetPane->getEditor()->documentVersion() != fileCompareVersionBefore;
		if (fileCompareMutated) refreshFileCompareAfterSourceMutation(activeRole);
		if (bentoMode == bbmFileCompare && mr::bento::paneRoleIsDiff(activeRole)) syncFileCompareLinkedPaneFrom(targetLeafId);
		if (!targetProjectsContentLocally) {
			bentoProjectionDirty |= bpdContent;
			if (!fileCompareMutated || targetRangeChanged) bentoProjectionDirty |= bpdChrome;
		} else if (targetRangeChanged || targetFocusChanged)
			bentoProjectionDirty |= bpdChrome;
		flushBentoProjection();
		return;
	}
	if (event.what == evMouseWheel && activeLeafId == 0 && paneWindowForLeaf(0) == nullptr && getEditor() != nullptr && pointInRect(localMouse, contentBounds(paneBoundsForLeaf(0)))) {
		MRFileEditor *sourceEditor = getEditor();
		static_cast<void>(sourceEditor->scrollWindowByWheel(event.mouse.wheel));
		clearEvent(event);
		bentoProjectionDirty |= bpdContent | bpdScrollBar | bpdChrome | bpdOverlay;
		syncFileCompareLinkedPaneFrom(0);
		flushBentoProjection();
		return;
	}
	MRFileEditor *sourceEditor = getEditor();
	MREditWindow *outputWindow = buildOutputPane();
	const bool compilerOutputActive = outputWindow != nullptr && outputWindow->hasTrackedExternalIoTasks();
	const bool trackSourceMutation = sourceEditor != nullptr && ((compilerDiagnosticsParseSourceSnapshot != nullptr && (compilerDiagnosticsParseRequired || compilerOutputActive)) ||
	                                                          (compilerDiagnostics != nullptr && !compilerDiagnostics->empty()) ||
	                                                          compilerDiagnosticSourceChanges != nullptr || diagnosticsProjectionTask.taskId != 0);
	const bool trackFileCompareMutation = bentoMode == bbmFileCompare && fileComparePanesEditable() && mr::bento::paneRoleIsDiff(roleForLeaf(0)) && sourceEditor != nullptr;
	const std::size_t fileCompareVersionBefore = trackFileCompareMutation ? sourceEditor->documentVersion() : 0;
	TScrollBar *sourceHorizontalScrollBar = horizontalEditorScrollBar();
	TScrollBar *sourceVerticalScrollBar = verticalEditorScrollBar();
	const std::pair<bool, bool> sourceRangeBefore = std::make_pair(sourceHorizontalScrollBar != nullptr && sourceHorizontalScrollBar->maxVal > sourceHorizontalScrollBar->minVal, sourceVerticalScrollBar != nullptr && sourceVerticalScrollBar->maxVal > sourceVerticalScrollBar->minVal);
	MRTextBufferModel::ReadSnapshot oldSnapshot;
	if (trackSourceMutation) oldSnapshot = buffer().readSnapshot();
	const bool trackingWasActive = bentoSourceMutationTrackingActive;
	bentoSourceMutationTrackingActive = true;
	MREditWindow::handleEvent(event);
	bentoSourceMutationTrackingActive = trackingWasActive;
	const bool fileCompareMutated = trackFileCompareMutation && sourceEditor->documentVersion() != fileCompareVersionBefore;
	if (fileCompareMutated) refreshFileCompareAfterSourceMutation(roleForLeaf(0));
	syncFileCompareLinkedPaneFrom(0);
	if (trackSourceMutation) syncCompilerDiagnosticsAfterSourceMutation(oldSnapshot, sourceEditor->lastDocumentChangeSet());
	refreshOutlinePanes(false);
	sourceHorizontalScrollBar = horizontalEditorScrollBar();
	sourceVerticalScrollBar = verticalEditorScrollBar();
	const std::pair<bool, bool> sourceRangeAfter = std::make_pair(sourceHorizontalScrollBar != nullptr && sourceHorizontalScrollBar->maxVal > sourceHorizontalScrollBar->minVal, sourceVerticalScrollBar != nullptr && sourceVerticalScrollBar->maxVal > sourceVerticalScrollBar->minVal);
	if (sourceRangeAfter != sourceRangeBefore) {
		bentoProjectionDirty |= bpdLayout | bpdOverlay;
	}
	else {
		bentoProjectionDirty |= bpdContent | bpdOverlay;
		if (!fileCompareMutated) bentoProjectionDirty |= bpdChrome;
	}
	flushBentoProjection();
}
