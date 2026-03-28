#define Uses_Dialogs
#define Uses_MsgBox
#define Uses_TObject
#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TFileDialog
#include <tvision/tv.h>

#include "mrmacrotest.hpp"

#include "../mrmac/mrmac.h"
#include "../mrmac/mrvm.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

static bool hasMrmacExtension(const std::string &path) {
	std::string::size_type pos = path.rfind('.');
	if (pos == std::string::npos)
		return false;

	std::string ext = path.substr(pos);
	for (std::string::size_type i = 0; i < ext.size(); ++i) {
		if (ext[i] >= 'A' && ext[i] <= 'Z')
			ext[i] = static_cast<char>(ext[i] - 'A' + 'a');
	}

	return ext == ".mrmac";
}

static std::string expandUserPath(const char *path) {
	if (path == 0)
		return std::string();

	if (path[0] == '~' && path[1] == '/') {
		const char *home = std::getenv("HOME");
		if (home != 0 && *home != '\0')
			return std::string(home) + (path + 1);
	}

	return std::string(path);
}

static bool readTextFile(const std::string &path, std::string &outContent, std::string &outError) {
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	std::ostringstream buffer;

	if (!in) {
		outError = "Could not open file.";
		return false;
	}

	buffer << in.rdbuf();

	if (!in.good() && !in.eof()) {
		outError = "Error while reading file.";
		return false;
	}

	outContent = buffer.str();
	return true;
}

static void showErrorBox(const char *title, const char *text) {
	char msg[1024];

	if (title == 0)
		title = "Error";
	if (text == 0)
		text = "Unknown error.";

	std::snprintf(msg, sizeof(msg), "%s:\n\n%s", title, text);
	messageBox(mfError | mfOKButton, "%s", msg);
}

static ushort runDialogWithData(TDialog *dialog, void *data) {
	ushort result = cmCancel;

	if (dialog == 0)
		return cmCancel;

	if (data != 0)
		dialog->setData(data);

	result = TProgram::deskTop->execView(dialog);

	if (result != cmCancel && data != 0)
		dialog->getData(data);

	TObject::destroy(dialog);
	return result;
}

static bool runMacroSource(const char *displayName, const char *source) {
	size_t bytecodeSize = 0;
	unsigned char *bytecode = 0;
	VirtualMachine vm;

	if (source == 0) {
		showErrorBox("Macro Loader", "No macro source available.");
		return false;
	}

	bytecode = compile_macro_code(source, &bytecodeSize);
	if (bytecode == 0) {
		const char *err = get_last_compile_error();
		if (err == 0 || *err == '\0')
			err = "Compilation failed.";
		showErrorBox(displayName != 0 ? displayName : "Macro Loader", err);
		return false;
	}

	vm.execute(bytecode, bytecodeSize);
	std::free(bytecode);
	return true;
}

bool runMacroFileByPath(const char *path) {
	std::string resolvedPath = expandUserPath(path);
	std::string source;
	std::string ioError;

	if (resolvedPath.empty()) {
		showErrorBox("Macro Loader", "No file name specified.");
		return false;
	}

	if (!hasMrmacExtension(resolvedPath)) {
		showErrorBox("Macro Loader", "Only .mrmac files are allowed.");
		return false;
	}

	if (!readTextFile(resolvedPath, source, ioError)) {
		showErrorBox(resolvedPath.c_str(), ioError.c_str());
		return false;
	}

	return runMacroSource(resolvedPath.c_str(), source.c_str());
}

bool runMacroFileDialog() {
	enum { FileNameBufferSize = 1024 };

	char fileName[FileNameBufferSize];
	ushort dialogResult;

	std::memset(fileName, 0, sizeof(fileName));

	dialogResult = runDialogWithData(
	    new TFileDialog("*.mrmac", "Load Macro File", "~N~ame", fdOpenButton, 100), fileName);

	if (dialogResult == cmCancel)
		return false;

	return runMacroFileByPath(fileName);
}