#include "mrmac/mrvm.hpp"
#include "app/MREditorApp.hpp"
#include "app/MRHelp.generated.hpp"
#include "ui/MRPalette.hpp"

#include <cstring>
#include <iostream>

namespace {
bool hasHelpFlag(int argc, char **argv) {
	for (int i = 1; argv != nullptr && i < argc; ++i) {
		const char *arg = argv[i];
		if (arg == nullptr)
			continue;
		if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0)
			return true;
	}
	return false;
}
} // namespace

int main(int argc, char **argv) {
	if (hasHelpFlag(argc, argv)) {
		std::cout << kMrEmbeddedHelpMarkdown;
		return 0;
	}
	mrvmSetProcessContext(argc, argv);
	loadDefaultMultiEditPalette();
	MREditorApp app;
	app.run();
	return 0;
}
