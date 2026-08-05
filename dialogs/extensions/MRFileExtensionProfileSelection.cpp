#include "MRFileExtensionProfileDrafts.hpp"

#include "../../app/commands/MRWindowCommands.hpp"
#include "../../app/utils/MRStringUtils.hpp"
#include "../../ui/MREditWindow.hpp"

namespace MRFileExtensionProfilesInternal {
namespace {

std::string focusedEditorExtension() {
	MREditWindow *window = currentEditorCommandWindow();
	std::string path;
	std::size_t slash = std::string::npos;
	std::size_t dot = std::string::npos;

	if (window == nullptr || !window->hasPersistentFileName()) return std::string();
	path = window->currentFileName();
	slash = path.find_last_of("/\\");
	dot = path.find_last_of('.');
	if (dot == std::string::npos || dot + 1 >= path.size()) return std::string();
	if (slash != std::string::npos && dot < slash) return std::string();
	return path.substr(dot + 1);
}

bool isLatexProfileExtension(const std::string &value) {
	const std::string upper = upperAscii(value);

	return upper == "TEX" || upper == "LTX" || upper == "STY" || upper == "CLS";
}

bool profileExtensionMatches(const std::string &selector, const std::string &extension) {
	std::string selectorUpper;
	std::string extensionUpper;

	if (selector == extension) return true;
	selectorUpper = upperAscii(selector);
	extensionUpper = upperAscii(extension);
	return selectorUpper == extensionUpper && isLatexProfileExtension(selectorUpper) && isLatexProfileExtension(extensionUpper);
}

} // namespace

int focusedEditorProfileIndex(const std::vector<EditProfileDraft> &drafts) {
	const std::string extension = focusedEditorExtension();

	if (extension.empty()) return -1;
	for (std::size_t index = 0; index < drafts.size(); ++index) {
		const EditProfileDraft &draft = drafts[index];
		std::vector<std::string> selectors;

		if (draft.isDefault) continue;
		selectors = splitExtensionLiteral(draft.extensionsLiteral);
		for (const std::string &selector : selectors)
			if (profileExtensionMatches(selector, extension)) return static_cast<int>(index);
	}
	return -1;
}

} // namespace MRFileExtensionProfilesInternal
