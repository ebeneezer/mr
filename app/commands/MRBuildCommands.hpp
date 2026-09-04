#ifndef MRBUILDCOMMANDS_HPP
#define MRBUILDCOMMANDS_HPP

namespace mr::coprocessor {
struct ExternalIoFinishedPayload;
}

bool mrContinueDebuggerAfterBuild(const mr::coprocessor::ExternalIoFinishedPayload &payload);

#endif
