#define Uses_TEvent
#ifndef MRFUNCTIONKEYBINDINGS_HPP
#define MRFUNCTIONKEYBINDINGS_HPP

#include <tvision/tv.h>

#include "../ui/MRStatusLine.hpp"

#include <string>
#include <vector>

class MRBentoBox;

std::vector<MRStatusLine::FunctionKeyLabel> mrStartupFunctionKeyLabels(ushort modifiers = 0);
std::vector<MRStatusLine::FunctionKeyLabel> mrEditorFunctionKeyLabels(ushort modifiers = 0);
const std::vector<std::string> &mrSnippetSidekickHintLabels();
bool mrEditorFunctionKeyContextActive();
bool mrHandleStartupFunctionKey(TEvent &event);
bool mrHandleEditorFunctionKey(TEvent &event);
bool mrHandleFileCompareFunctionKey(TEvent &event);
bool mrHandleFileCompareCommand(TEvent &event);
MRBentoBox *mrCurrentMacroDebuggerBentoBox();

#endif
