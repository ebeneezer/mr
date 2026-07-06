#ifndef MRVM_PROCESS_RUNTIME_HPP
#define MRVM_PROCESS_RUNTIME_HPP

#include <string>
#include <vector>

struct MRVMSubshellResult {
	std::string output;
	int errorLevel;
};

std::string mrvmCommandFirstLine(const std::string &command);
int mrvmRunShellCommand(const std::string &command, const std::string &shellPath);
std::string mrvmDetectExecutablePathFromProc();
std::string mrvmNormalizeDirPath(const std::string &path);
std::string mrvmDetectExecutableDir(const std::string &argv0);
std::string mrvmDetectShellPath();
std::string mrvmDetectShellVersion(const std::string &shellPath);
int mrvmDetectCpuCode();
std::string mrvmGetenvValue(const std::string &name);
bool mrvmChangeDirectoryPath(const std::string &path);
bool mrvmDeleteFilePath(const std::string &path);
std::string mrvmProcessExpandUserPath(const std::string &path);
bool mrvmFileExistsPath(const std::string &path);
bool mrvmReadFileMetadata(const std::string &path, int *attrOut, int *sizeOut, int *timeOut);
MRVMSubshellResult mrvmRunSubshellCapture(const std::string &command, int timeoutMs, const std::string &shellPath);
int mrvmForkProcess(const std::vector<std::string> &arguments, int ownerBufferId = 0, const std::string &sourcePath = std::string(), const std::string &pdfPath = std::string());
void mrvmCloseForksForOwner(int ownerBufferId);
void mrvmCloseAllForkedProcesses();
void mrvmProcessRuntimeSetContext(int argc, char **argv);
std::vector<std::string> mrvmProcessRuntimeArguments();

#endif
