#ifndef MRCOMMANDROUTER_HPP
#define MRCOMMANDROUTER_HPP

#include <tvision/tv.h>

[[nodiscard]] bool handleMRCommand(ushort command);
void clearTransientSearchSelectionOnUserInput(const TEvent &event);

#endif
