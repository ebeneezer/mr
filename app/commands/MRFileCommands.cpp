#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TObject
#define Uses_MsgBox
#define Uses_TFileDialog
#include <tvision/tv.h>

#include "MRFileCommands.hpp"

#include "MRDialogPaths.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>

#include "MRWindowCommands.hpp"
#include "MRPerformance.hpp"

#include "../../ui/TMREditWindow.hpp"
#include "../../ui/MRWindowSupport.hpp"

namespace {
ushort execDialogWithDataLocal(TDialog *dialog, void *data) {
	ushort result = cmCancel;
	if (dialog == nullptr)
		return cmCancel;
	if (data != nullptr)
		dialog->setData(data);
	result = TProgram::deskTop->execView(dialog);
	if (result != cmCancel && data != nullptr)
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

	if (path == nullptr)
		return std::string();
	result = normalizeTvPath(trimPathInput(path));
	if (result.size() >= 2 && result[0] == '~' && result[1] == '/') {
		const char *home = std::getenv("HOME");
		if (home != nullptr && *home != '\0')
			return std::string(home) + result.substr(1);
	}
	return result;
}

bool hasWildcardPattern(const std::string &path) {
	return path.find('*') != std::string::npos || path.find('?') != std::string::npos;
}

std::size_t lastPathSeparator(const std::string &path) {
	std::size_t slash = path.find_last_of('/');
	std::size_t backslash = path.find_last_of('\\');

	if (slash == std::string::npos)
		return backslash;
	if (backslash == std::string::npos)
		return slash;
	return std::max(slash, backslash);
}

bool hasExtensionInBaseName(const std::string &path) {
	std::size_t sep = lastPathSeparator(path);
	std::size_t dot = path.find_last_of('.');

	return dot != std::string::npos && (sep == std::string::npos || dot > sep);
}

bool resolveWithConfiguredExtensions(const std::string &basePath, std::string &resolvedPath) {
	std::vector<std::string> extensions = configuredDefaultExtensionList();
	std::set<std::string> tried;

	for (auto ext : extensions) {
			std::string candidates[3];

		if (ext.empty())
			continue;
		candidates[0] = ext;
		candidates[1] = ext;
		candidates[2] = ext;
		for (std::size_t p = 0; p < ext.size(); ++p) {
			candidates[1][p] = static_cast<char>(std::tolower(static_cast<unsigned char>(ext[p])));
			candidates[2][p] = static_cast<char>(std::toupper(static_cast<unsigned char>(ext[p])));
		}

		for (const auto & c : candidates) {
			std::string candidate = basePath + "." + c;
			if (!tried.insert(candidate).second)
				continue;
			if (::access(candidate.c_str(), F_OK) == 0 && ::access(candidate.c_str(), R_OK) == 0) {
				resolvedPath = candidate;
				return true;
			}
		}
	}
	return false;
}
} // namespace

bool promptForPath(const char *title, char *fileName, std::size_t fileNameSize) {
	if (fileName == nullptr || fileNameSize == 0)
		return false;
	initRememberedLoadDialogPath(fileName, fileNameSize, "*.*");
	return execDialogWithDataLocal(new TFileDialog("*.*", title, "~N~ame", fdOpenButton, 100), fileName) !=
	       cmCancel;
}

bool resolveReadableExistingPath(const char *path, std::string &resolvedPath) {
	bool disableExtensionSearch = false;
	std::string rawInput = expandUserPath(path);

	resolvedPath = rawInput;
	if (!resolvedPath.empty() && resolvedPath.back() == '.' && !hasWildcardPattern(resolvedPath)) {
		disableExtensionSearch = true;
		resolvedPath.pop_back();
	}
	if (resolvedPath.empty()) {
		messageBox(mfError | mfOKButton, "No file name specified.");
		return false;
	}
	if (::access(resolvedPath.c_str(), F_OK) != 0 && !disableExtensionSearch &&
	    !hasWildcardPattern(resolvedPath) && !hasExtensionInBaseName(resolvedPath))
		resolveWithConfiguredExtensions(resolvedPath, resolvedPath);
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
	if (win == nullptr)
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

	if (win == nullptr)
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

	if (win == nullptr)
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
