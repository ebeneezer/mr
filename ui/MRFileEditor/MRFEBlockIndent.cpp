#include "MRFEBlockOps.hpp"
#include "MRFileEditor.hpp"

bool MRFEBlockOps::shiftCurrentBlockToTab(MRFileEditor &editor, bool indent, std::string *errorText) {
	if (errorText != nullptr) errorText->clear();
	normalize(editor);
	switch (mGeometry.mode) {
	case MRFEBlockMode::Line:
		return shiftCurrentLineBlockToTab(editor, indent, errorText);
	case MRFEBlockMode::Column:
		return shiftCurrentColumnBlockToTab(editor, indent, errorText);
	case MRFEBlockMode::Stream:
		return shiftCurrentStreamBlockToTab(editor, indent, errorText);
	case MRFEBlockMode::None:
		if (errorText != nullptr) *errorText = "Line, column or stream block required.";
		return false;
	}
	if (errorText != nullptr) *errorText = "Line, column or stream block required.";
	return false;
}
