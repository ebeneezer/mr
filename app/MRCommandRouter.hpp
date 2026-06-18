#ifndef MRCOMMANDROUTER_HPP
#define MRCOMMANDROUTER_HPP

#include <tvision/tv.h>

#include <string_view>

class MREditWindow;

[[nodiscard]] bool handleMRCommand(ushort command, void *commandInfo = nullptr);
[[nodiscard]] bool dispatchMRKeymapAction(std::string_view actionId, std::string_view sequenceText = {}, MREditWindow *targetWindow = nullptr);
[[nodiscard]] bool dispatchMRKeymapMacro(std::string_view macroSpec);
[[nodiscard]] bool showMRLspContextMenu(MREditWindow *targetWindow, TPoint where);
void notifyMRLspMouseActivity(TPoint where) noexcept;
void notifyMRLspKeyboardActivity() noexcept;
void mrApplyLspSupportSettingsChange();
void pumpMRLspService();
void clearTransientSearchSelectionOnUserInput(const TEvent &event);

#endif
