#include "MRFileEditor.hpp"
#include "../../app/MRPrivilegedFileBroker.hpp"
#include "../../config/settings/MRSettingsStorage.hpp"

#include <cerrno>
#include <chrono>
#include <fstream>
#include <unistd.h>

bool MRFileEditor::resolveSaveOptionsForPath(const char *path, MRTextSaveOptions &options, std::size_t *optionsHash) const {
	options = effectiveTextSaveOptionsForPath(path != nullptr ? path : "", optionsHash);
	if (mForceBinarySave) options.binaryMode = true;
	return true;
}

void MRFileEditor::invalidateSaveNormalizationCache() noexcept {
	mSaveNormalizationCache.valid = false;
	mSaveNormalizationCache.documentId = 0;
	mSaveNormalizationCache.version = 0;
	mSaveNormalizationCache.optionsHash = 0;
	mSaveNormalizationCache.sourceBytes = 0;
}

void MRFileEditor::noteSaveNormalizationThroughput(std::size_t sourceBytes, double runMicros) noexcept {
	if (sourceBytes == 0 || runMicros <= 0.0) return;
	const double sampleBytesPerMicro = static_cast<double>(sourceBytes) / std::max(1.0, runMicros);
	if (mSaveNormalizationThroughputBytesPerMicro <= 0.0) mSaveNormalizationThroughputBytesPerMicro = sampleBytesPerMicro;
	else
		mSaveNormalizationThroughputBytesPerMicro = mSaveNormalizationThroughputBytesPerMicro * 0.75 + sampleBytesPerMicro * 0.25;
	++mSaveNormalizationThroughputSamples;
}

bool MRFileEditor::canSaveInPlace() const {
	std::string persistentName;

	if (mReadOnly || !hasPersistentFileName()) return false;
	persistentName = trimAscii(fileName);
	if (upperAscii(persistentName) == "?NO-FILE?") return false;
	if (looksLikeUri(persistentName)) return false;
	return true;
}

bool MRFileEditor::canSaveAs() const {
	return !mReadOnly;
}

bool MRFileEditor::loadMappedFile(TStringView path, std::string &error) {
	MRTextBufferModel::Document document;
	const auto mapStartedAt = std::chrono::steady_clock::now();

	mLastLoadTiming = LoadTiming();
	if (!document.loadMappedFile(path, error)) {
		if (!mrPrivilegedFileBrokerAvailable()) return false;
		int fileDescriptor = mrPrivilegedFileBrokerOpenReadOnly(path, error);
		if (fileDescriptor < 0 || !document.loadMappedFile(fileDescriptor, path, error)) return false;
	}
	const double mappedLoadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mapStartedAt).count();
	const std::size_t lines = document.estimatedLineCount();

	mLastLoadTiming.valid = true;
	mLastLoadTiming.bytes = document.length();
	mLastLoadTiming.lines = lines;
	mLastLoadTiming.linesExact = document.exactLineCountKnown();
	mLastLoadTiming.mappedLoadMs = mappedLoadMs;
	mLastLoadTiming.lineCountMs = 0.0;
	setPersistentFileName(path);
	if (!adoptCommittedDocument(document, 0, 0, 0, false)) {
		clearPersistentFileName();
		mLastLoadTiming = LoadTiming();
		error = "Unable to adopt mapped document.";
		return false;
	}
	mBufferModel.clearUndoRedo();
	scheduleLineIndexWarmupIfNeeded();
	return true;
}

Boolean MRFileEditor::saveInPlace() noexcept {
	if (!canSaveInPlace()) return False;
	Boolean ok = writeDocumentToPath(fileName) ? True : False;
	if (ok == True) setDocumentModified(false);
	return ok;
}

Boolean MRFileEditor::saveAsWithPrompt() noexcept {
	char saveName[MAXPATH];

	if (!canSaveAs()) return False;
	if (hasPersistentFileName()) strnzcpy(saveName, fileName, sizeof(saveName));
	else
		initRememberedLoadDialogPath(MRDialogHistoryScope::EditorSaveAs, saveName, sizeof(saveName), "*.*");
	if (TEditor::editorDialog(edSaveAs, saveName) == cmCancel) return False;
	fexpand(saveName);
	if (!samePath(saveName, fileName) && !confirmOverwriteForSaveAs(saveName)) return False;
	if (!writeDocumentToPath(saveName)) return False;
	rememberLoadDialogPath(MRDialogHistoryScope::EditorSaveAs, saveName);
	setPersistentFileName(saveName);
	if (owner != nullptr) message((TView *)owner, evBroadcast, cmUpdateTitle, 0);
	setDocumentModified(false);
	return True;
}

Boolean MRFileEditor::saveAsWithoutOverwritePrompt() noexcept {
	char saveName[MAXPATH];

	if (!canSaveAs()) return False;
	if (hasPersistentFileName()) strnzcpy(saveName, fileName, sizeof(saveName));
	else
		initRememberedLoadDialogPath(MRDialogHistoryScope::EditorSaveAs, saveName, sizeof(saveName), "*.*");
	if (TEditor::editorDialog(edSaveAs, saveName) == cmCancel) return False;
	fexpand(saveName);
	if (!writeDocumentToPath(saveName)) return False;
	rememberLoadDialogPath(MRDialogHistoryScope::EditorSaveAs, saveName);
	setPersistentFileName(saveName);
	if (owner != nullptr) message((TView *)owner, evBroadcast, cmUpdateTitle, 0);
	setDocumentModified(false);
	return True;
}

bool MRFileEditor::writeDocumentToPath(const char *targetPath) {
	char drive[MAXDRIVE];
	char dir[MAXDIR];
	char file[MAXFILE];
	char ext[MAXEXT];
	MRTextSaveOptions saveOptions;
	const std::size_t pieceCount = mBufferModel.document().pieceCount();
	const bool backupEnabled = configuredBackupFilesSetting();
	const bool privilegedSave = targetPath != nullptr && mrPrivilegedFileBrokerAllowsPath(targetPath);
	const bool mappedInPlaceSave = mBufferModel.document().hasMappedOriginal() && samePath(mBufferModel.document().mappedPath().c_str(), targetPath);
	bool backupMovedTarget = false;
	bool useTemporaryTarget = false;
	int privilegedDescriptor = -1;
	std::string brokerError;
	std::string temporaryTargetPath;
	std::string outputTargetPath;
	std::ofstream out;

	resolveSaveOptionsForPath(targetPath, saveOptions);

	if (backupEnabled && !privilegedSave) {
		fnsplit(targetPath, drive, dir, file, ext);
		char backupName[MAXPATH];
		fnmerge(backupName, drive, dir, file, ".bak");
		unlink(backupName);
		backupMovedTarget = rename(targetPath, backupName) == 0;
	}
	useTemporaryTarget = !privilegedSave && mappedInPlaceSave && !backupMovedTarget;
	outputTargetPath = targetPath != nullptr ? targetPath : "";
	if (useTemporaryTarget) {
		temporaryTargetPath = outputTargetPath + ".mr-save-tmp-" + std::to_string(static_cast<long long>(::getpid()));
		unlink(temporaryTargetPath.c_str());
		outputTargetPath = temporaryTargetPath;
	}

	if (privilegedSave) {
		if (!mrPrivilegedFileBrokerBeginSave(targetPath, backupEnabled, privilegedDescriptor, brokerError)) {
			if (!brokerError.empty()) mrLogMessage("Privileged save could not start: " + brokerError);
			TEditor::editorDialog(edCreateError, targetPath);
			return false;
		}
	} else {
		out.open(outputTargetPath.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
		if (!out) {
			TEditor::editorDialog(edCreateError, targetPath);
			return false;
		}
	}
	auto failWrite = [&]() -> bool {
		if (privilegedSave && !brokerError.empty()) mrLogMessage("Privileged save failed: " + brokerError);
		if (privilegedDescriptor >= 0) {
			::close(privilegedDescriptor);
			privilegedDescriptor = -1;
		}
		if (privilegedSave) mrPrivilegedFileBrokerAbortSave();
		if (!temporaryTargetPath.empty()) unlink(temporaryTargetPath.c_str());
		TEditor::editorDialog(edWriteError, targetPath);
		return false;
	};
	auto writeBytes = [&](const char *data, std::size_t length) -> bool {
		if (!privilegedSave) {
			writeChunk(out, data, length);
			return static_cast<bool>(out);
		}
		while (length > 0) {
			const std::size_t part = std::min<std::size_t>(length, static_cast<std::size_t>(1024) * 1024 * 1024);
			ssize_t written = ::write(privilegedDescriptor, data, part);
			if (written > 0) {
				data += written;
				length -= static_cast<std::size_t>(written);
				continue;
			}
			if (written < 0 && errno == EINTR) continue;
			return false;
		}
		return true;
	};
	auto finishWrite = [&]() -> bool {
		if (privilegedSave) {
			if (::close(privilegedDescriptor) != 0) {
				privilegedDescriptor = -1;
				return failWrite();
			}
			privilegedDescriptor = -1;
			if (!mrPrivilegedFileBrokerCommitSave(brokerError)) return failWrite();
			return true;
		}
		if (!out) return failWrite();
		out.close();
		if (!out) return failWrite();
		if (!temporaryTargetPath.empty() && rename(temporaryTargetPath.c_str(), targetPath) != 0) return failWrite();
		return true;
	};

	if (saveOptions.binaryMode) {
		for (std::size_t i = 0; i < pieceCount; ++i) {
			mr::editor::PieceChunkView chunk = mBufferModel.document().pieceChunk(i);
			if (!writeBytes(chunk.data, chunk.length)) return failWrite();
		}
		return finishWrite();
	}
	const std::size_t sourceBytes = mBufferModel.document().length();
	const auto normalizeStartedAt = std::chrono::steady_clock::now();
	const std::size_t flushThresholdBytes = static_cast<std::size_t>(256) * 1024;
	MRTextSaveStreamState normalizeState;
	std::string outputBuffer;
	auto flushOutput = [&]() -> bool {
		if (outputBuffer.empty()) return true;
		const bool written = writeBytes(outputBuffer.data(), outputBuffer.size());
		outputBuffer.clear();
		return written;
	};

	outputBuffer.reserve(flushThresholdBytes + 1024);
	for (std::size_t i = 0; i < pieceCount; ++i) {
		mr::editor::PieceChunkView chunk = mBufferModel.document().pieceChunk(i);
		if (chunk.length == 0) continue;
		appendNormalizedTextSaveChunk(std::string_view(chunk.data, chunk.length), saveOptions, normalizeState, outputBuffer);
		if (outputBuffer.size() >= flushThresholdBytes && !flushOutput()) return failWrite();
	}
	finalizeNormalizedTextSaveStream(saveOptions, normalizeState, outputBuffer);
	if (!flushOutput()) return failWrite();

	noteSaveNormalizationThroughput(sourceBytes, static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - normalizeStartedAt).count()));
	return finishWrite();
}


Boolean MRFileEditor::confirmSaveOrDiscardUntitled() {
	const char *detail = nullptr;
	std::string persistentName;

	if (hasPersistentFileName()) {
		persistentName = trimAscii(fileName);
		if (!persistentName.empty() && upperAscii(persistentName) != "?NO-FILE?") detail = persistentName.c_str();
	}
	const mr::dialogs::UnsavedChangesChoice choice = mr::dialogs::showUnsavedChangesDialog("Save As", "Window has unsaved changes.", detail);
	switch (choice) {
		case mr::dialogs::UnsavedChangesChoice::Save:
			return saveAsWithPrompt();
		case mr::dialogs::UnsavedChangesChoice::Discard:
			setDocumentModified(false);
			return True;
		default:
			return False;
	}
}

Boolean MRFileEditor::confirmSaveOrDiscardNamed() {
	const mr::dialogs::UnsavedChangesChoice choice = mr::dialogs::showUnsavedChangesDialog("Save", "Save changes to:", fileName);
	switch (choice) {
		case mr::dialogs::UnsavedChangesChoice::Save:
			return saveInPlace();
		case mr::dialogs::UnsavedChangesChoice::Discard:
			setDocumentModified(false);
			return True;
		default:
			return False;
	}
}
