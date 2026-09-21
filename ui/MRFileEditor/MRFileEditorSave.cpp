#include "MRFileEditor.hpp"
#include "../../app/MRPrivilegedFileBroker.hpp"
#include "../../config/settings/MRSettingsStorage.hpp"

#include <cerrno>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <fcntl.h>
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
	mLastSavedPath.clear();
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

bool MRFileEditor::hasBeenSavedInSession() const noexcept {
	return !mLastSavedPath.empty();
}

void MRFileEditor::updateAutosaveState() {
	if (!isDocumentModified()) {
		if (mAutosaveTimer != nullptr) killTimer(mAutosaveTimer);
		mAutosaveTimer = nullptr;
		mAutosaveDirtySince = {};
		return;
	}
	const auto now = std::chrono::steady_clock::now();
	if (mAutosaveDirtySince == std::chrono::steady_clock::time_point()) mAutosaveDirtySince = now;
	mAutosaveLastActivity = now;
	if (mAutosaveTimer == nullptr) mAutosaveTimer = setTimer(1000, 1000);
}

void MRFileEditor::autosaveIfDue() {
	if (!isDocumentModified()) {
		updateAutosaveState();
		return;
	}
	if (!canSaveInPlace()) return;
	MREditSetupSettings settings;
	effectiveEditSetupSettingsForPath(fileName, settings);
	const auto now = std::chrono::steady_clock::now();
	const bool inactivityDue = settings.autosaveInactivitySeconds > 0 && now - mAutosaveLastActivity >= std::chrono::seconds(settings.autosaveInactivitySeconds);
	const bool intervalDue = settings.autosaveIntervalSeconds > 0 && now - mAutosaveDirtySince >= std::chrono::seconds(settings.autosaveIntervalSeconds);
	if (!inactivityDue && !intervalDue) return;
	if (writeDocumentToPath(fileName, false)) {
		mBufferModel.markSaved();
		clearDirtyRanges();
		updateAutosaveState();
		syncFromEditorState(false);
	} else {
		// Retry after a full configured period, without opening a modal dialog.
		mAutosaveDirtySince = now;
		mAutosaveLastActivity = now;
	}
}

bool MRFileEditor::writeDocumentToPath(const char *targetPath, bool interactive) {
	MRTextSaveOptions saveOptions;
	MREditSetupSettings settings;
	effectiveEditSetupSettingsForPath(targetPath != nullptr ? targetPath : "", settings);
	const std::size_t pieceCount = mBufferModel.document().pieceCount();
	const bool backupEnabled = settings.backupFiles && settings.backupMethod != "OFF" &&
	                           (settings.backupFrequency == "EVERY_SAVE" || !samePath(mLastSavedPath.c_str(), targetPath));
	const bool privilegedSave = targetPath != nullptr && mrPrivilegedFileBrokerAllowsPath(targetPath);
	const bool brokerBackup = privilegedSave && backupEnabled && settings.backupMethod == "BAK_FILE" && settings.backupExtension == "bak";
	int outputDescriptor = -1;
	std::string saveError;
	std::string temporaryTargetPath;
	std::string temporaryBackupPath;
	std::string outputTargetPath = targetPath != nullptr ? targetPath : "";
	std::filesystem::path backupPath;
	struct stat originalStatus {};
	const bool targetExists = ::stat(outputTargetPath.c_str(), &originalStatus) == 0;

	resolveSaveOptionsForPath(targetPath, saveOptions);
	auto failWrite = [&]() -> bool {
		if (saveError.empty()) saveError = std::strerror(errno);
		if (outputDescriptor >= 0) {
			::close(outputDescriptor);
			outputDescriptor = -1;
		}
		if (privilegedSave) mrPrivilegedFileBrokerAbortSave();
		if (!temporaryTargetPath.empty()) ::unlink(temporaryTargetPath.c_str());
		if (!temporaryBackupPath.empty()) ::unlink(temporaryBackupPath.c_str());
		mrLogMessage("Save failed for " + outputTargetPath + ": " + saveError);
		if (interactive) TEditor::editorDialog(edWriteError, targetPath);
		else mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Autosave failed: " + outputTargetPath + ": " + saveError, mr::messageline::Kind::Warning, mr::messageline::kPriorityMedium);
		return false;
	};
	if (outputTargetPath.empty() || (targetExists && !S_ISREG(originalStatus.st_mode))) {
		saveError = "Save target is not a regular file.";
		return failWrite();
	}
	if (targetExists && !privilegedSave) {
		if (::access(outputTargetPath.c_str(), W_OK) != 0) return failWrite();
		std::error_code error;
		outputTargetPath = std::filesystem::canonical(outputTargetPath, error).string();
		if (error) {
			saveError = error.message();
			return failWrite();
		}
	}
	if (backupEnabled && !brokerBackup) {
		backupPath = outputTargetPath;
		if (settings.backupMethod == "DIRECTORY") backupPath = std::filesystem::path(settings.backupDirectory) / backupPath.filename();
		else backupPath.replace_extension("." + settings.backupExtension);
		std::error_code error;
		if (backupPath == std::filesystem::path(outputTargetPath) || std::filesystem::equivalent(backupPath, outputTargetPath, error)) {
			saveError = "Backup path identifies the original file.";
			return failWrite();
		}
	}
	if (privilegedSave) {
		if (!mrPrivilegedFileBrokerBeginSave(targetPath, brokerBackup, outputDescriptor, saveError)) return failWrite();
	} else {
		temporaryTargetPath = outputTargetPath + ".mr-save-XXXXXX";
		outputDescriptor = ::mkstemp(temporaryTargetPath.data());
		if (outputDescriptor < 0) {
			temporaryTargetPath.clear();
			return failWrite();
		}
		if (targetExists && ::fchmod(outputDescriptor, originalStatus.st_mode & 07777) != 0) return failWrite();
	}
	auto writeBytes = [&](const char *data, std::size_t length) -> bool {
		while (length > 0) {
			const std::size_t part = std::min<std::size_t>(length, static_cast<std::size_t>(1024) * 1024 * 1024);
			ssize_t written = ::write(outputDescriptor, data, part);
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
		if (::fsync(outputDescriptor) != 0) return failWrite();
		if (::close(outputDescriptor) != 0) {
			outputDescriptor = -1;
			return failWrite();
		}
		outputDescriptor = -1;
		if (backupEnabled && !brokerBackup && (targetExists || privilegedSave)) {
			// Copy to a new inode: an older backup may still back a mapped document.
			temporaryBackupPath = backupPath.string() + ".mr-backup-XXXXXX";
			int backupDescriptor = ::mkstemp(temporaryBackupPath.data());
			if (backupDescriptor < 0) {
				temporaryBackupPath.clear();
				return failWrite();
			}
			int sourceDescriptor = privilegedSave ? mrPrivilegedFileBrokerOpenReadOnly(targetPath, saveError) : ::open(outputTargetPath.c_str(), O_RDONLY | O_CLOEXEC);
			bool copied = sourceDescriptor >= 0;
			char bytes[65536];
			while (copied) {
				ssize_t count = ::read(sourceDescriptor, bytes, sizeof(bytes));
				if (count == 0) break;
				if (count < 0) {
					if (errno == EINTR) continue;
					copied = false;
					break;
				}
				ssize_t offset = 0;
				while (offset < count) {
					ssize_t written = ::write(backupDescriptor, bytes + offset, static_cast<std::size_t>(count - offset));
					if (written > 0) offset += written;
					else if (written < 0 && errno == EINTR) continue;
					else { copied = false; break; }
				}
			}
			if (!copied) saveError = std::strerror(errno);
			if (sourceDescriptor >= 0) ::close(sourceDescriptor);
			if (copied && ::fsync(backupDescriptor) != 0) { saveError = std::strerror(errno); copied = false; }
			if (::close(backupDescriptor) != 0) { saveError = std::strerror(errno); copied = false; }
			if (!copied || ::rename(temporaryBackupPath.c_str(), backupPath.c_str()) != 0) return failWrite();
			temporaryBackupPath.clear();
		}
		if (privilegedSave) {
			if (!mrPrivilegedFileBrokerCommitSave(saveError)) return failWrite();
		} else if (::rename(temporaryTargetPath.c_str(), outputTargetPath.c_str()) != 0) return failWrite();
		temporaryTargetPath.clear();
		mLastSavedPath = targetPath;
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
