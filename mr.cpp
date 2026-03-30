#include "mrmac/mrvm.hpp"
#include "app/TMREditorApp.hpp"
#include "ui/MRPalette.hpp"
int main(int argc, char **argv) {
	mrvmSetProcessContext(argc, argv);
	loadDefaultMultiEditPalette();
	TMREditorApp app;
	app.run();
	return 0;
}
