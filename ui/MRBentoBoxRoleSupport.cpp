#include "MRBentoBoxRoleSupport.hpp"

namespace {

static const char *kPaneActionReplace = "replace";
static const char *kPaneActionSplitRight = "split \xC4";
static const char *kPaneActionSplitDown = "split \xB3";
static const char *kFileCompareActionNext = "next diff";
static const char *kFileCompareActionPrevious = "prev diff";
static const char *kFileCompareActionApply = "apply diff";
static const MRBentoPaneTitleMenuSpec kPaneRoleTitleMenu{"role"};

struct BentoPaneActionDescriptor {
	const char *action;
	MRBentoPanePlacement placement;
};

static const BentoPaneActionDescriptor kPaneActions[] = {
	{kPaneActionReplace, bppReplace},
	{kPaneActionSplitRight, bppSplitDown},
	{kPaneActionSplitDown, bppSplitRight},
};

struct BentoPaneRoleDescriptor {
	MRBentoPaneRole role;
	const char *title;
	bool listed;
};

static const BentoPaneRoleDescriptor kPaneRoles[] = {
	{bprSource, "Source", false},
	{bprCompilerOutput, "Compiler Output", true},
	{bprAppOutput, "App Output", true},
	{bprProblems, "Problems", true},
	{bprDebuggerOutput, "Debugger Output", true},
	{bprWatches, "Watches", true},
	{bprVariables, "Variables", true},
	{bprStructure, "Structure", true},
	{bprFunctions, "Functions", true},
	{bprSplitEditor, "Split editor", true},
	{bprDiffOriginal, "Diff Original", true},
	{bprDiffCompare, "Diff Compare", true},
};

} // namespace

MRBentoPaneSpec::MRBentoPaneSpec() noexcept : role(bprCompilerOutput), bufferPolicy(bpbOwnBuffer), readOnly(true), suppressMiniMap(true), suppressWordWrap(true), scrollBarsAlwaysVisible(true), titleMenu(&kPaneRoleTitleMenu) {
}

MRBentoPaneSpec::MRBentoPaneSpec(MRBentoPaneRole aRole, MRBentoPaneBufferPolicy aBufferPolicy, bool aReadOnly, bool aSuppressMiniMap, bool aSuppressWordWrap, bool aScrollBarsAlwaysVisible, const MRBentoPaneTitleMenuSpec *aTitleMenu) noexcept
    : role(aRole), bufferPolicy(aBufferPolicy), readOnly(aReadOnly), suppressMiniMap(aSuppressMiniMap), suppressWordWrap(aSuppressWordWrap), scrollBarsAlwaysVisible(aScrollBarsAlwaysVisible), titleMenu(aTitleMenu) {
}

namespace mr::bento {

const char *paneRoleTitle(MRBentoPaneRole role) noexcept {
	for (const BentoPaneRoleDescriptor &descriptor : kPaneRoles)
		if (descriptor.role == role) return descriptor.title;
	return "Pane";
}

bool paneRoleIsOutline(MRBentoPaneRole role) noexcept {
	return role == bprStructure || role == bprFunctions;
}

bool paneRoleIsDiff(MRBentoPaneRole role) noexcept {
	return role == bprDiffOriginal || role == bprDiffCompare;
}

MRBentoPaneRole paneRoleForTitle(const std::string &title) noexcept {
	for (const BentoPaneRoleDescriptor &descriptor : kPaneRoles)
		if (descriptor.listed && title == descriptor.title) return descriptor.role;
	return bprCompilerOutput;
}

const MRBentoPaneTitleMenuSpec *paneRoleTitleMenu(MRBentoBoxMode mode) noexcept {
	return mode == bbmDocumentViewports ? nullptr : &kPaneRoleTitleMenu;
}

const char *paneActionReplace() noexcept {
	return kPaneActionReplace;
}

const char *fileCompareActionNext() noexcept {
	return kFileCompareActionNext;
}

const char *fileCompareActionPrevious() noexcept {
	return kFileCompareActionPrevious;
}

const char *fileCompareActionApply() noexcept {
	return kFileCompareActionApply;
}

std::vector<std::string> paneRoleChoices(MRBentoBoxMode mode) {
	std::vector<std::string> choices;

	for (const BentoPaneRoleDescriptor &descriptor : kPaneRoles) {
		if (!descriptor.listed) continue;
		if (mode == bbmFileCompare && !paneRoleIsDiff(descriptor.role)) continue;
		if (mode != bbmFileCompare && paneRoleIsDiff(descriptor.role)) continue;
		choices.push_back(descriptor.title);
	}
	return choices;
}

std::vector<std::string> paneActionChoices() {
	std::vector<std::string> choices;

	for (const BentoPaneActionDescriptor &descriptor : kPaneActions)
		choices.push_back(descriptor.action);
	return choices;
}

MRBentoPanePlacement panePlacementForAction(const std::string &action) noexcept {
	for (const BentoPaneActionDescriptor &descriptor : kPaneActions)
		if (action == descriptor.action) return descriptor.placement;
	return bppReplace;
}

} // namespace mr::bento
