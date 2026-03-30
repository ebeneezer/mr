#ifndef MRUNSAVEDCHANGESDIALOG_HPP
#define MRUNSAVEDCHANGESDIALOG_HPP

namespace mr {
namespace dialogs {

enum class UnsavedChangesChoice : unsigned char {
	Save,
	Discard,
	Cancel
};

UnsavedChangesChoice showUnsavedChangesDialog(const char *primaryLabel, const char *headline,
                                              const char *detail = nullptr);

} // namespace dialogs
} // namespace mr

#endif
