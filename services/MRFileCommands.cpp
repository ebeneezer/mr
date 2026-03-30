#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TObject
#define Uses_MsgBox
#define Uses_TFileDialog
#include <tvision/tv.h>

#include "MRFileCommands.hpp"

#include "MRDialogPaths.hpp"
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

#include "MRWindowCommands.hpp"
#include "MRPerformance.hpp"

#include "../ui/TMREditWindow.hpp"
#include "../ui/MRWindowSupport.hpp"

namespace {
ushort execDialogWithDataLocal(TDialog *dialog, void *data) {
	ushort result = cmCancel;
	if (dialog == 0)
		return cmCancel;
	if (data != 0)
		dialog->setData(data);
	result = TProgram::deskTop->execView(dialog);
	if (result != cmCancel && data != 0)
		dialog->getData(data);
	TObject::destroy(dialog);
	if (result == cmHelp)
		mrShowProjectHelp();
	return result;
}

std::string normalizeTvPath(const std::string &path) {
	std::string result = path;
	std::size_t i;

	for (i = 0; i < result.size(); ++i)
		if (result[i] == '\\')
			result[i] = '/';
#ifdef __unix__
	if (result.size() >= 2 && ((result[0] >= 'A' && result[0] <= 'Z') ||
	                           (result[0] >= 'a' && result[0] <= 'z')) &&
	    result[1] == ':')
		result.erase(0, 2);
#endif
	return result;
}

std::string trimPathInput(const std::string &path) {
	std::size_t start = 0;
	std::size_t end = path.size();

	while (start < end && std::isspace(static_cast<unsigned char>(path[start])) != 0)
		++start;
	while (end > start &&
	       (std::isspace(static_cast<unsigned char>(path[end - 1])) != 0 ||
	        static_cast<unsigned char>(path[end - 1]) < 32))
		--end;

	std::string result = path.substr(start, end - start);
	if (result.size() >= 2 &&
	    ((result.front() == '"' && result.back() == '"') || (result.front() == '\'' && result.back() == '\'')))
		result = result.substr(1, result.size() - 2);
	return result;
}

std::string expandUserPath(const char *path) {
	std::string result;

	if (path == 0)
		return std::string();
	result = normalizeTvPath(trimPathInput(path));
	if (result.size() >= 2 && result[0] == '~' && result[1] == '/') {
		const char *home = std::getenv("HOME");
		if (home != 0 && *home != '\0')
			return std::string(home) + result.substr(1);
	}
	return result;
}
} // namespace

bool promptForPath(const char *title, char *fileName, std::size_t fileNameSize) {
	if (fileName == 0 || fileNameSize == 0)
		return false;
	initRememberedLoadDialogPath(fileName, fileNameSize, "*.*");
	return execDialogWithDataLocal(new TFileDialog("*.*", title, "~N~ame", fdOpenButton, 100), fileName) !=
	       cmCancel;
}

bool resolveReadableExistingPath(const char *path, std::string &resolvedPath) {
	resolvedPath = expandUserPath(path);
	if (resolvedPath.empty()) {
		messageBox(mfError | mfOKButton, "No file name specified.");
		return false;
	}
	if (access(resolvedPath.c_str(), F_OK) != 0) {
		messageBox(mfError | mfOKButton, "File does not exist:\n%s", resolvedPath.c_str());
		return false;
	}
	if (access(resolvedPath.c_str(), R_OK) != 0) {
		messageBox(mfError | mfOKButton, "File is not readable:\n%s", resolvedPath.c_str());
		return false;
	}
	rememberLoadDialogPath(resolvedPath.c_str());
	return true;
}

bool loadResolvedFileIntoWindow(TMREditWindow *win, const std::string &resolvedPath, const char *operationLabel) {
	auto startedAt = std::chrono::steady_clock::now();
	if (win == 0)
		return false;
	if (!win->loadFromFile(resolvedPath.c_str())) {
		messageBox(mfError | mfOKButton, "Unable to load file:\n%s", resolvedPath.c_str());
		return false;
	}
	mr::performance::recordUiEvent(operationLabel != nullptr ? operationLabel : "Load file",
	                               static_cast<std::size_t>(win->bufferId()), win->documentId(),
	                               win->bufferLength(),
	                               std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
	                                                                         startedAt)
	                                   .count(),
	                               resolvedPath);
	return true;
}

bool saveCurrentEditWindow() {
	TMREditWindow *win = currentEditWindow();

	if (win == 0)
		return false;
	if (win->isReadOnly()) {
		messageBox(mfInformation | mfOKButton, "Window is read-only.");
		mrLogMessage("Save rejected for read-only window.");
		return false;
	}
	if (!win->isFileChanged())
		return true;
	if (win->canSaveInPlace()) {
		auto startedAt = std::chrono::steady_clock::now();
		if (!win->saveCurrentFile()) {
			mrLogMessage("Save failed.");
			return false;
		}
		mr::performance::recordUiEvent(
		    "Save file", static_cast<std::size_t>(win->bufferId()), win->documentId(), win->bufferLength(),
		    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count(),
		    win->currentFileName());
		mrLogMessage("Window saved.");
		return true;
	}
	if (!win->saveCurrentFileAs()) {
		mrLogMessage("Save failed.");
		return false;
	}
	mrLogMessage("Window saved as a new file.");
	return true;
}

bool saveCurrentEditWindowAs() {
	TMREditWindow *win = currentEditWindow();

	if (win == 0)
		return false;
	if (win->isReadOnly()) {
		messageBox(mfInformation | mfOKButton, "Window is read-only.");
		mrLogMessage("Save As rejected for read-only window.");
		return false;
	}
	auto startedAt = std::chrono::steady_clock::now();
	if (!win->saveCurrentFileAs()) {
		mrLogMessage("Save As failed.");
		return false;
	}
	mr::performance::recordUiEvent(
	    "Save file as", static_cast<std::size_t>(win->bufferId()), win->documentId(), win->bufferLength(),
	    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count(),
	    win->currentFileName());
	mrLogMessage("Window saved as a new file.");
	return true;
}
