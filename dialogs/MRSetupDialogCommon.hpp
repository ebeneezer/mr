#ifndef MRSETUPDIALOGCOMMON_HPP
#define MRSETUPDIALOGCOMMON_HPP

#include <string>
#include <vector>

class TDialog;
class TRect;

enum MRSetupLayoutProfile {
	mrSetupLayoutCompact = 0,
	mrSetupLayoutRelaxed = 1
};

MRSetupLayoutProfile currentSetupLayoutProfile();
bool isSetupLayoutCompact();
TRect centeredSetupDialogRect(int width, int height);
TRect centeredSetupDialogRectForProfile(int compactWidth, int compactHeight, int relaxedWidth,
                                        int relaxedHeight);
void insertSetupStaticLine(TDialog *dialog, int x, int y, const char *text);
TDialog *createSetupSimplePreviewDialog(const char *title, int width, int height,
                                        const std::vector<std::string> &lines,
                                        bool showOkCancelHelp);
TDialog *createSetupSimplePreviewDialogForProfile(const char *title, int compactWidth, int compactHeight,
                                                  int relaxedWidth, int relaxedHeight,
                                                  const std::vector<std::string> &lines,
                                                  bool showOkCancelHelp);

#endif
