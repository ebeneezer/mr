#include "MRFileEditor.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../config/settings/MRSettingsStorage.hpp"

#include <array>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace {

std::string directProbeTimestamp() {
	std::array<char, 32> buffer{};
	const std::time_t now = std::time(nullptr);
	const std::tm *tmNow = std::localtime(&now);

	if (tmNow == nullptr) return std::string("--:--:--");
	if (std::strftime(buffer.data(), buffer.size(), "%H:%M:%S", tmNow) == 0) return std::string("--:--:--");
	return std::string(buffer.data());
}

void appendDirectProbeLog(std::string_view message) {
	std::ofstream out(configuredLogFilePath(), std::ios::out | std::ios::app | std::ios::binary);

	if (!out) return;
	out << "[" << directProbeTimestamp() << "] " << message << '\n';
	out.flush();
}

template <class Duration> long long traceMicros(Duration duration) {
	return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

} // namespace

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
	if (!document.loadMappedFile(path, error)) return false;
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
	const bool mappedInPlaceSave = mBufferModel.document().hasMappedOriginal() && samePath(mBufferModel.document().mappedPath().c_str(), targetPath);
	bool backupMovedTarget = false;
	bool useTemporaryTarget = false;
	std::string temporaryTargetPath;
	std::string outputTargetPath;

	resolveSaveOptionsForPath(targetPath, saveOptions);

	if (backupEnabled) {
		fnsplit(targetPath, drive, dir, file, ext);
		char backupName[MAXPATH];
		fnmerge(backupName, drive, dir, file, ".bak");
		unlink(backupName);
		backupMovedTarget = rename(targetPath, backupName) == 0;
	}
	useTemporaryTarget = mappedInPlaceSave && !backupMovedTarget;
	outputTargetPath = targetPath != nullptr ? targetPath : "";
	if (useTemporaryTarget) {
		temporaryTargetPath = outputTargetPath + ".mr-save-tmp-" + std::to_string(static_cast<long long>(::getpid()));
		unlink(temporaryTargetPath.c_str());
		outputTargetPath = temporaryTargetPath;
	}

	std::ofstream out(outputTargetPath.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!out) {
		TEditor::editorDialog(edCreateError, targetPath);
		return false;
	}
	auto failWrite = [&]() -> bool {
		if (!temporaryTargetPath.empty()) unlink(temporaryTargetPath.c_str());
		TEditor::editorDialog(edWriteError, targetPath);
		return false;
	};

	if (saveOptions.binaryMode) {
		for (std::size_t i = 0; i < pieceCount; ++i) {
			mr::editor::PieceChunkView chunk = mBufferModel.document().pieceChunk(i);
			writeChunk(out, chunk.data, chunk.length);
			if (!out) return failWrite();
		}
		out.close();
		if (!out) return failWrite();
		if (!temporaryTargetPath.empty() && rename(temporaryTargetPath.c_str(), targetPath) != 0) return failWrite();
		return true;
	}
	const std::size_t sourceBytes = mBufferModel.document().length();
	const auto normalizeStartedAt = std::chrono::steady_clock::now();
	const std::size_t flushThresholdBytes = static_cast<std::size_t>(256) * 1024;
	MRTextSaveStreamState normalizeState;
	std::string outputBuffer;
	auto flushOutput = [&]() -> bool {
		if (outputBuffer.empty()) return true;
		writeChunk(out, outputBuffer.data(), outputBuffer.size());
		outputBuffer.clear();
		return static_cast<bool>(out);
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
	if (!out) return failWrite();
	out.close();
	if (!out) return failWrite();
	if (!temporaryTargetPath.empty() && rename(temporaryTargetPath.c_str(), targetPath) != 0) return failWrite();
	return true;
}


Boolean MRFileEditor::confirmSaveOrDiscardUntitled() {
	const char *detail = nullptr;
	std::string persistentName;

	if (hasPersistentFileName()) {
		persistentName = trimAscii(fileName);
		if (!persistentName.empty() && upperAscii(persistentName) != "?NO-FILE?") detail = persistentName.c_str();
	}
	const auto startedAt = std::chrono::steady_clock::now();
	appendDirectProbeLog("Phase1 discard untitled dialog begin");
	const mr::dialogs::UnsavedChangesChoice choice = mr::dialogs::showUnsavedChangesDialog("Save As", "Window has unsaved changes.", detail);
	{
		std::ostringstream trace;
		trace << "Phase1 discard untitled dialog end total_us=" << traceMicros(std::chrono::steady_clock::now() - startedAt) << " choice=" << static_cast<int>(choice);
		appendDirectProbeLog(trace.str());
	}
	switch (choice) {
		case mr::dialogs::UnsavedChangesChoice::Save:
			return saveAsWithPrompt();
		case mr::dialogs::UnsavedChangesChoice::Discard:
			appendDirectProbeLog("Phase1 discard untitled accepted");
			setDocumentModified(false);
			return True;
		default:
			return False;
	}
}

Boolean MRFileEditor::confirmSaveOrDiscardNamed() {
	const auto startedAt = std::chrono::steady_clock::now();
	appendDirectProbeLog("Phase1 discard named dialog begin");
	const mr::dialogs::UnsavedChangesChoice choice = mr::dialogs::showUnsavedChangesDialog("Save", "Save changes to:", fileName);
	{
		std::ostringstream trace;
		trace << "Phase1 discard named dialog end total_us=" << traceMicros(std::chrono::steady_clock::now() - startedAt) << " choice=" << static_cast<int>(choice);
		appendDirectProbeLog(trace.str());
	}
	switch (choice) {
		case mr::dialogs::UnsavedChangesChoice::Save:
			return saveInPlace();
		case mr::dialogs::UnsavedChangesChoice::Discard:
			appendDirectProbeLog("Phase1 discard named accepted");
			setDocumentModified(false);
			return True;
		default:
			return False;
	}
}
