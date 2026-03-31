#ifndef MRSETUPDIALOGCOMMON_HPP
#define MRSETUPDIALOGCOMMON_HPP

#include <string>
#include <vector>

class TDialog;
class TRect;

TRect centeredSetupDialogRect(int width, int height);
void insertSetupStaticLine(TDialog *dialog, int x, int y, const char *text);
TDialog *createSetupSimplePreviewDialog(const char *title, int width, int height,
                                        const std::vector<std::string> &lines,
                                        bool showOkCancelHelp);

#endif
