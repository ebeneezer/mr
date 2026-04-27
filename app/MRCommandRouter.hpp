#ifndef MRCOMMANDROUTER_HPP
#define MRCOMMANDROUTER_HPP

#include <tvision/tv.h>

#include <string_view>

class MREditWindow;

[[nodiscard]] bool handleMRCommand(ushort command);
[[nodiscard]] bool dispatchMRKeymapAction(std::string_view actionId, std::string_view sequenceText = {},
                                          MREditWindow *targetWindow = nullptr);
[[nodiscard]] bool dispatchMRKeymapMacro(std::string_view macroSpec);
void clearTransientSearchSelectionOnUserInput(const TEvent &event);

#endif
