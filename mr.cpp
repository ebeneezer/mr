#include "mrmac/MRVM.hpp"
#include "app/MREditorApp.hpp"
#include "app/MRHelp.generated.hpp"
#include "ui/MRPalette.hpp"

#include <cstring>
#include <ctime>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {
bool hasHelpFlag(int argc, char **argv) {
	for (int i = 1; argv != nullptr && i < argc; ++i) {
		const char *arg = argv[i];
		if (arg == nullptr) continue;
		if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) return true;
	}
	return false;
}

void appendMainShutdownTrace(std::string_view message) {
	std::ofstream out("misc/mr.log", std::ios::out | std::ios::app | std::ios::binary);
	if (!out) return;
	const std::time_t now = std::time(nullptr);
	const std::tm *tmNow = std::localtime(&now);
	char buffer[32];
	if (tmNow != nullptr && std::strftime(buffer, sizeof(buffer), "%H:%M:%S", tmNow) != 0) out << "[" << buffer << "] ";
	else
		out << "[--:--:--] ";
	out << message << '\n';
	out.flush();
}
} // namespace

int main(int argc, char **argv) {
	if (hasHelpFlag(argc, argv)) {
		std::cout << kMrEmbeddedHelpMarkdown;
		return 0;
	}
	mrvmSetProcessContext(argc, argv);
	loadDefaultMultiEditPalette();
	const auto appScopeStartedAt = std::chrono::steady_clock::now();
	{
		MREditorApp app;
		const auto runStartedAt = std::chrono::steady_clock::now();
		app.run();
		{
			std::ostringstream line;
			line << "main phase after_app_run took_ms="
			     << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - runStartedAt).count() << ".";
			appendMainShutdownTrace(line.str());
		}
	}
	{
		std::ostringstream line;
		line << "main phase after_app_scope took_ms="
		     << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - appScopeStartedAt).count() << ".";
		appendMainShutdownTrace(line.str());
	}
	return 0;
}
