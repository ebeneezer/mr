#ifndef MRCOMMANDROUTERGIT_HPP
#define MRCOMMANDROUTERGIT_HPP

#include <string>

class MREditWindow;

namespace mr {
namespace coprocessor {
struct Result;
}
}

struct MRGitChangesCommand {
	std::string title;
	std::string commandLine;
};

[[nodiscard]] bool prepareMRGitChangesCommand(const std::string &filePath, MRGitChangesCommand &command);
void requestMRGitStatusProbe(MREditWindow *window);
[[nodiscard]] bool dispatchMRGitStatusResult(const mr::coprocessor::Result &result);

#endif
