#pragma once

#include <string>
#include <string_view>

enum class MRPrivilegedFileBrokerStartup {
	RunApplication,
	ParentFinished,
	Failed
};

MRPrivilegedFileBrokerStartup mrStartPrivilegedFileBroker(int argc, char **argv, int &exitCode, std::string &error);

[[nodiscard]] bool mrPrivilegedFileBrokerAvailable() noexcept;
[[nodiscard]] bool mrPrivilegedFileBrokerAllowsPath(std::string_view path) noexcept;
[[nodiscard]] int mrPrivilegedFileBrokerOpenReadOnly(std::string_view path, std::string &error);
[[nodiscard]] bool mrPrivilegedFileBrokerBeginSave(std::string_view path, bool backupEnabled, int &fileDescriptor, std::string &error);
[[nodiscard]] bool mrPrivilegedFileBrokerCommitSave(std::string &error);
void mrPrivilegedFileBrokerAbortSave() noexcept;
