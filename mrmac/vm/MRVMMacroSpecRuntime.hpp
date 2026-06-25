#ifndef MRVM_MACRO_SPEC_RUNTIME_HPP
#define MRVM_MACRO_SPEC_RUNTIME_HPP

#include <string>
#include <vector>

std::string mrvmStripMrmacExtension(const std::string &value);
std::string mrvmMakeMacroFileKey(const std::string &value);
bool mrvmHasMrmacExtension(const std::string &path);
bool mrvmIsBootstrapIndexedMacroFile(const std::string &path);
bool mrvmParseRunMacroSpec(const std::string &spec, std::string &filePart, std::string &macroPart, std::string &paramPart);
std::vector<std::string> mrvmListMrmacFilesInDirectory(const std::string &directoryPath);
std::string mrvmResolveMacroFilePath(const std::string &spec, const std::string &macroDirectoryPath);

#endif
