#ifndef MRSCOPEDHISTORYDIALOGS_HPP
#define MRSCOPEDHISTORYDIALOGS_HPP

class TDialog;
class TFileDialog;

enum class MRDialogHistoryScope : unsigned char;

constexpr unsigned short cmMrScopedHistoryAcquire = 3870;
constexpr unsigned short cmMrScopedHistoryAcquireDialog = 3871;

namespace mr::ui {

[[nodiscard]] TFileDialog *createScopedFileDialog(MRDialogHistoryScope scope, const char *wildCard, const char *title, const char *inputName, unsigned short options);
[[nodiscard]] TDialog *createScopedDirectoryDialog(MRDialogHistoryScope scope, unsigned short options);

} // namespace mr::ui

#endif
