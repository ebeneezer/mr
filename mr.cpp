#include "mrmac/MRVM.hpp"
#include "app/MREditorApp.hpp"
#include "app/MRHelp.generated.hpp"
#include "app/MRPrivilegedFileBroker.hpp"
#include "app/MRUpdate.hpp"
#include "config/settings/MRSettingsRuntime.hpp"
#include "mrmac/vm/MRVMProcessRuntime.hpp"

#include <cstring>
#include <cerrno>
#include <ctime>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

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
	std::ofstream out(configuredLogFilePath(), std::ios::out | std::ios::app | std::ios::binary);
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
	int updateExitCode = 1;
	std::string updateError;
	switch (mrStartInternalUpdateApply(argc, argv, updateExitCode, updateError)) {
		case MRUpdateInternalStartup::ParentFinished:
			return updateExitCode;
		case MRUpdateInternalStartup::Failed:
			std::cerr << "mr: " << (updateError.empty() ? "internal update failed." : updateError) << '\n';
			return updateExitCode;
		case MRUpdateInternalStartup::RunApplication:
			break;
	}
	if (hasHelpFlag(argc, argv)) {
		std::cout << kMrEmbeddedHelpMarkdown;
		return 0;
	}
	int brokerExitCode = 1;
	std::string brokerError;
	switch (mrStartPrivilegedFileBroker(argc, argv, brokerExitCode, brokerError)) {
		case MRPrivilegedFileBrokerStartup::ParentFinished:
			return brokerExitCode;
		case MRPrivilegedFileBrokerStartup::Failed:
			std::cerr << "mr: " << (brokerError.empty() ? "privileged file broker startup failed." : brokerError) << '\n';
			return 1;
		case MRPrivilegedFileBrokerStartup::RunApplication:
			break;
	}
	mrvmSetProcessContext(argc, argv);
	const auto appScopeStartedAt = std::chrono::steady_clock::now();
	bool restartAfterUpdate = false;
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
		restartAfterUpdate = app.restartAfterExitRequested();
	}
	mrvmCloseAllForkedProcesses();
	{
		std::ostringstream line;
		line << "main phase after_app_scope took_ms="
		     << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - appScopeStartedAt).count() << ".";
		appendMainShutdownTrace(line.str());
	}
	if (restartAfterUpdate) {
		char *const restartArguments[] = {const_cast<char *>("mr"), const_cast<char *>("--internal-reload-workspace-after-update"), nullptr};
		::execv("/usr/local/bin/mr", restartArguments);
		std::cerr << "mr: unable to restart /usr/local/bin/mr: " << std::strerror(errno) << '\n';
		return 1;
	}
	return 0;
}
