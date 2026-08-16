#ifndef MRMACRO_MODELESS_CONTROLS_HPP
#define MRMACRO_MODELESS_CONTROLS_HPP

#include <string>
#include <vector>

class TRect;
class TScrollBar;
class TView;

void sendMacroUiActivationCommand(TView *source, unsigned short command);

TView *createMacroUiListView(const TRect &bounds, TScrollBar *scrollBar, std::vector<std::string> values, unsigned short command);
void setMacroUiListItems(TView *view, std::vector<std::string> values, int start);
int macroUiListSelectedIndex(const TView *view);
std::string macroUiListSelectedText(const TView *view);

TView *createMacroUiGridView(const TRect &bounds, TScrollBar *scrollBar, std::vector<std::string> values, unsigned short command);
void setMacroUiGridItems(TView *view, std::vector<std::string> values, int start);
void refreshMacroUiGridItems(TView *view, std::vector<std::string> values);
int macroUiGridSelectedIndex(const TView *view);
std::string macroUiGridSelectedText(const TView *view);
std::string macroUiGridItemText(const std::string &value);

std::string macroUiTreeNodeItem(const std::string &nodeId, const std::string &parentId, const std::string &text, bool expanded);
bool macroUiTreeItemsValid(const std::vector<std::string> &values);
TView *createMacroUiTreeView(const TRect &bounds, TScrollBar *scrollBar, std::vector<std::string> values, unsigned short command, const std::string &windowId = std::string(), int controlId = 0);
void setMacroUiTreeItems(TView *view, std::vector<std::string> values, int start);
void refreshMacroUiTreeItems(TView *view, std::vector<std::string> values);
int macroUiTreeSelectedIndex(const TView *view);
std::string macroUiTreeSelectedText(const TView *view);

std::string macroUiTableColumnItem(const std::string &title, int width);
std::string macroUiTableRowItem(const std::string &rowId, const std::string &cells);
bool macroUiTableItemsValid(const std::vector<std::string> &values);
TView *createMacroUiTableView(const TRect &bounds, TScrollBar *scrollBar, std::vector<std::string> values, unsigned short command, const std::string &windowId = std::string(), int controlId = 0);
void setMacroUiTableItems(TView *view, std::vector<std::string> values, int start);
void refreshMacroUiTableItems(TView *view, std::vector<std::string> values);
int macroUiTableSelectedIndex(const TView *view);
std::string macroUiTableSelectedText(const TView *view);

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
