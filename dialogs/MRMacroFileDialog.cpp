#define Uses_Dialogs
#define Uses_TObject
#define Uses_TProgram
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TFileDialog
#include <tvision/tv.h>

#include "MRMacroFileDialog.hpp"

#include "../mrmac/MRMacroRunner.hpp"

#include <cstring>

namespace {
ushort runDialogWithData(TDialog *dialog, void *data) {
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
} // namespace

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
