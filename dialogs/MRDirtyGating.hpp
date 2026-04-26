#ifndef MRDIALOGDIRTYGATING_HPP
#define MRDIALOGDIRTYGATING_HPP

#include <functional>
#include <utility>
#include <vector>

namespace mr {
namespace dialogs {

enum class UnsavedChangesChoice : unsigned char {
	Save,
	Discard,
	Cancel
};

template <typename Draft, typename EqualFn>
[[nodiscard]] bool isDialogDraftDirty(const Draft &baselineDraft, const Draft &currentDraft,
                                      EqualFn &&equalFn) {
	return !std::invoke(std::forward<EqualFn>(equalFn), baselineDraft, currentDraft);
}

[[nodiscard]] UnsavedChangesChoice showUnsavedChangesDialog(const char *primaryLabel,
                                                            const char *headline,
                                                            const char *detail = nullptr);

[[nodiscard]] UnsavedChangesChoice runDialogDirtyGating(const char *headline,
                                                        const char *primaryLabel = "Save",
                                                        const char *detail = nullptr);

[[nodiscard]] UnsavedChangesChoice runDialogDirtyListGating(const char *dialogTitle,
                                                            const char *headline,
                                                            const char *itemsLabel,
                                                            const std::vector<std::string> &dirtyItems,
                                                            const char *primaryLabel = "Save");

} // namespace dialogs
} // namespace mr

#endif
