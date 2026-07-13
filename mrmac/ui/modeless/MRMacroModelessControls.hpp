#ifndef MRMACRO_MODELESS_CONTROLS_HPP
#define MRMACRO_MODELESS_CONTROLS_HPP

#include <string>
#include <vector>

class TRect;
class TScrollBar;
class TView;

TView *createMacroModelessListView(const TRect &bounds, TScrollBar *scrollBar, std::vector<std::string> values, unsigned short command);
void setMacroModelessListItems(TView *view, std::vector<std::string> values, int start);
int macroModelessListSelectedIndex(const TView *view);
std::string macroModelessListSelectedText(const TView *view);

TView *createMacroModelessGridView(const TRect &bounds, TScrollBar *scrollBar, std::vector<std::string> values, unsigned short command);
void setMacroModelessGridItems(TView *view, std::vector<std::string> values, int start);
void refreshMacroModelessGridItems(TView *view, std::vector<std::string> values);
int macroModelessGridSelectedIndex(const TView *view);
std::string macroModelessGridSelectedText(const TView *view);

TView *createMacroModelessTextInput(const TRect &bounds, int width, const std::string &windowId, const std::string &fieldId, const std::string &text);
bool setMacroModelessTextInputValue(TView *view, const std::string &text);

TView *createMacroModelessBoolInput(const TRect &bounds, const std::string &windowId, const std::string &fieldId, const std::string &caption, bool value);
bool setMacroModelessBoolInputValue(TView *view, bool value);

TView *createMacroModelessIntInput(const TRect &bounds, int width, const std::string &windowId, const std::string &fieldId, int value);
bool setMacroModelessIntInputValue(TView *view, int value);

TView *createMacroModelessProgressView(const TRect &bounds, const std::string &windowId, const std::string &fieldId);
void redrawMacroModelessProgressView(TView *view);

TView *createMacroModelessLogView(const TRect &bounds, const std::string &windowId, const std::string &logId);
void redrawMacroModelessLogView(TView *view);

TView *createMacroModelessSelectInput(const TRect &bounds, TScrollBar *scrollBar, std::vector<std::string> options, const std::string &windowId, const std::string &fieldId, const std::string &value);
bool setMacroModelessSelectInputValue(TView *view, const std::string &value);
bool setMacroModelessSelectInputOptions(TView *view, std::vector<std::string> options, const std::string &value);

#endif
